#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../sessions/sidecars/binding.h"
#include "../../include/mdo/attachments.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/sessions.h"
#include "../../include/mdo/project_lifecycle.h"

#define MDO_ATTACHMENT_ID_BYTES 16u
#define MDO_ATTACHMENT_ID_LENGTH (MDO_ATTACHMENT_ID_BYTES * 2u)
#define MDO_ATTACHMENT_SESSION_MAX 16u
#define MDO_ATTACHMENT_SESSION_BYTES (32u * 1024u * 1024u)
#define MDO_ATTACHMENT_META_MAX MDO_ATTACHMENT_META_MAX_BYTES
#define MDO_ATTACHMENT_GRACE_US (24LL * 60LL * 60LL * 1000000LL)
#define MDO_ATTACHMENT_SWEEP_MAX 128u

typedef enum MdoAttachmentDiscardResult {
    MDO_ATTACHMENT_DISCARD_ERROR = 0,
    MDO_ATTACHMENT_DISCARD_MISSING,
    MDO_ATTACHMENT_DISCARD_IN_USE,
    MDO_ATTACHMENT_DISCARD_REMOVED
} MdoAttachmentDiscardResult;

static xmutex* g_MdoAttachmentLock;
static bool MdoAttachmentCollectExpired(const char* Project,
    const char* Session, const char* Directory);

bool MdoApiAttachmentsInit(void)
{
    if ( g_MdoAttachmentLock != NULL ) return true;
    g_MdoAttachmentLock = xrtMutexCreate();
    return g_MdoAttachmentLock != NULL;
}

void MdoApiAttachmentsUnit(void)
{
    if ( g_MdoAttachmentLock != NULL ) xrtMutexDestroy(g_MdoAttachmentLock);
    g_MdoAttachmentLock = NULL;
}

bool MdoApiAttachmentLock(void)
{
    return g_MdoAttachmentLock != NULL &&
        xrtMutexLock(g_MdoAttachmentLock);
}

bool MdoApiAttachmentCaptureTryLock(void)
{
    return g_MdoAttachmentLock != NULL && xrtMutexTryLock(g_MdoAttachmentLock);
}

void MdoApiAttachmentUnlock(void)
{
    if ( g_MdoAttachmentLock != NULL )
        (void)xrtMutexUnlock(g_MdoAttachmentLock);
}

static bool MdoAttachmentCaptureId(xstrview View, char* Output,
    size_t Capacity)
{
    size_t i;
    if ( View.Size == 0u || View.Size >= Capacity || View.Data[0] == '.' )
        return false;
    for ( i = 0u; i < View.Size; ++i ) {
        unsigned char Byte = (unsigned char)View.Data[i];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             (Byte == '.' && i != 0u) ) continue;
        return false;
    }
    memcpy(Output, View.Data, View.Size);
    Output[View.Size] = '\0';
    return true;
}

static bool MdoAttachmentPath(char* Output, size_t Capacity,
    const char* Project, const char* Session, const char* Name)
{
    int Written = snprintf(Output, Capacity, "sessions/%s/%s/attachments/%s",
        Project, Session, Name);
    return Written > 0 && (size_t)Written < Capacity;
}

static bool MdoAttachmentSession(const MdoApiContext* Context,
    char Project[MDO_PROJECT_ID_CAPACITY],
    char Session[MDO_SESSION_ID_CAPACITY], bool Upload)
{
    MdoSession* Handle;
    MdoSessionInfo Info;
    xwork_error Error;
    bool Ok;
    if ( Context->ParamCount != (Upload ? 2u : 3u) ||
         !MdoAttachmentCaptureId(Context->Params[0], Project,
            MDO_PROJECT_ID_CAPACITY) ||
         !MdoAttachmentCaptureId(Context->Params[1], Session,
            MDO_SESSION_ID_CAPACITY) ) return false;
    memset(&Error, 0, sizeof(Error));
    Handle = MdoSessionLoad(Project, Session, &Error);
    if ( Handle == NULL ) return false;
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    Ok = MdoSessionGetInfo(Handle, &Info) &&
        (!Upload || Info.Status == MDO_SESSION_ACTIVE);
    MdoSessionRelease(Handle);
    return Ok;
}

static cstr MdoAttachmentMime(xstrview Type, const unsigned char* Data,
    size_t Size)
{
    static const unsigned char Png[] =
        { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
    if ( Size >= sizeof(Png) && memcmp(Data, Png, sizeof(Png)) == 0 &&
         (Type.Size == 0u || (Type.Size == 9u &&
            memcmp(Type.Data, "image/png", 9u) == 0)) ) return "image/png";
    if ( Size >= 4u && Data[0] == 0xffu && Data[1] == 0xd8u &&
         Data[2] == 0xffu &&
         (Type.Size == 0u || (Type.Size == 10u &&
            memcmp(Type.Data, "image/jpeg", 10u) == 0)) )
        return "image/jpeg";
    if ( Size >= 16u && memcmp(Data, "RIFF", 4u) == 0 &&
         memcmp(Data + 8u, "WEBP", 4u) == 0 &&
         (memcmp(Data + 12u, "VP8 ", 4u) == 0 ||
          memcmp(Data + 12u, "VP8L", 4u) == 0 ||
          memcmp(Data + 12u, "VP8X", 4u) == 0) &&
         (Type.Size == 0u || (Type.Size == 10u &&
            memcmp(Type.Data, "image/webp", 10u) == 0)) )
        return "image/webp";
    return NULL;
}

static bool MdoAttachmentType(const MdoApiContext* Context, xstrview* Type)
{
    const xhttpfield* Field = NULL;
    xhttpnext Next = xrtHttpFieldGetUnique(Context->Request->head->Fields,
        Context->Request->head->FieldCount, XRT_STR_LITERAL("Content-Type"),
        &Field);
    if ( Next != XHTTP_NEXT_ITEM || Field == NULL ) return false;
    *Type = xrtStrTrim(Field->Value);
    return true;
}

static bool MdoAttachmentNameValid(xstrview Name)
{
    size_t i;
    if ( Name.Size > MDO_ATTACHMENT_NAME_MAX_BYTES ||
         !xrtUtf8Valid(Name, NULL) ) return false;
    for ( i = 0u; i < Name.Size; ++i ) {
        unsigned char Byte = (unsigned char)Name.Data[i];
        if ( Byte < 0x20u || Byte == 0x7fu || Byte == '/' || Byte == '\\' )
            return false;
    }
    return true;
}

static bool MdoAttachmentName(const MdoApiContext* Context,
    char Output[MDO_ATTACHMENT_NAME_MAX_BYTES + 1u])
{
    const xhttpfield* Field = NULL;
    size_t Size = 0u;
    xhttpnext Next = xrtHttpFieldGetUnique(Context->Request->head->Fields,
        Context->Request->head->FieldCount,
        XRT_STR_LITERAL("X-Mdo-File-Name"), &Field);
    Output[0] = '\0';
    if ( Next == XHTTP_NEXT_END ) return true;
    if ( Next != XHTTP_NEXT_ITEM || Field == NULL ||
         Field->Value.Size > MDO_ATTACHMENT_NAME_MAX_BYTES * 3u ||
         !xrtPercentDecode(xrtStrTrim(Field->Value), Output,
            MDO_ATTACHMENT_NAME_MAX_BYTES, &Size) ||
         !MdoAttachmentNameValid(xrtStrViewN(Output, Size)) ) return false;
    Output[Size] = '\0';
    return true;
}

static bool MdoAttachmentQuota(const char* Directory, size_t NewSize)
{
    bool Exists = false;
    xfileinfo Info;
    xdir Dir = NULL;
    xdirentry Entry;
    xdirnext Next;
    size_t Count = 0u;
    size_t Total = 0u;
    bool Ok = true;
    if ( !MdoHomeExternalStat(Directory, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_DIRECTORY ) return false;
    Dir = MdoHomeOpenDirectory(Directory, XDIR_STAT);
    if ( Dir == NULL ) return false;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Dir, &Entry)) == XDIR_NEXT_ITEM ) {
        if ( Entry.Info.Type != XFILE_TYPE_FILE ||
             Entry.Name.Size != MDO_ATTACHMENT_ID_LENGTH + 4u ||
             memcmp(Entry.Name.Data + MDO_ATTACHMENT_ID_LENGTH,
                ".bin", 4u) != 0 ) continue;
        if ( (Entry.Info.Available & XFILE_INFO_SIZE) == 0u ||
             Entry.Info.Size > MDO_ATTACHMENT_SESSION_BYTES - Total ) {
            Ok = false;
            break;
        }
        Total += (size_t)Entry.Info.Size;
        if ( ++Count >= MDO_ATTACHMENT_SESSION_MAX ) { Ok = false; break; }
    }
    if ( Next == XDIR_NEXT_ERROR ) Ok = false;
    if ( !xrtDirClose(Dir) ) Ok = false;
    return Ok && NewSize <= MDO_ATTACHMENT_SESSION_BYTES - Total;
}

static bool MdoAttachmentNewId(char Output[MDO_ATTACHMENT_ID_LENGTH + 1u])
{
    static const char Hex[] = "0123456789abcdef";
    uint8 Bytes[MDO_ATTACHMENT_ID_BYTES];
    size_t i;
    if ( !xrtSecureRandom(Bytes, sizeof(Bytes)) ) return false;
    for ( i = 0u; i < sizeof(Bytes); ++i ) {
        Output[i * 2u] = Hex[Bytes[i] >> 4u];
        Output[i * 2u + 1u] = Hex[Bytes[i] & 15u];
    }
    Output[MDO_ATTACHMENT_ID_LENGTH] = '\0';
    return true;
}

static bool MdoAttachmentHexId(xstrview View,
    char Output[MDO_ATTACHMENT_ID_LENGTH + 1u])
{
    size_t i;
    if ( View.Size != MDO_ATTACHMENT_ID_LENGTH ) return false;
    for ( i = 0u; i < View.Size; ++i ) {
        unsigned char Byte = (unsigned char)View.Data[i];
        if ( !((Byte >= '0' && Byte <= '9') ||
               (Byte >= 'a' && Byte <= 'f')) ) return false;
    }
    memcpy(Output, View.Data, View.Size);
    Output[View.Size] = '\0';
    return true;
}

static bool MdoAttachmentMetaUInt(const xvalue* Root, cstr Key, uint64* Output)
{
    const xvalue* Value = xrtValueObjectGet(Root, xrtStrView(Key));
    int64 Signed;
    if ( xrtValueType(Value) == XVALUE_UINT )
        return xrtValueGetUInt(Value, Output);
    if ( xrtValueType(Value) != XVALUE_INT ||
         !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Output = (uint64)Signed;
    return true;
}

/* The caller owns the returned value. Old five-field v1 sidecars remain
 * readable; named uploads use six-field v2 sidecars with bounded UTF-8. */
static xvalue* MdoAttachmentMetadata(const char* Project,
    const char* Session, const char* Id)
{
    char Name[MDO_ATTACHMENT_ID_LENGTH + 6u];
    char Path[MDO_SESSION_PATH_CAPACITY];
    char Bytes[MDO_ATTACHMENT_META_MAX + 1u];
    bool Exists = false;
    xfileinfo Info;
    xfile File = NULL;
    xjsonreadconfig Config;
    xvalue* Root = NULL;
    const xvalue* Value;
    xstrview StoredId;
    xstrview Mime;
    xstrview FileName;
    uint64 Schema;
    uint64 Size;
    uint64 CreatedAt;
    bool Ok = false;
    snprintf(Name, sizeof(Name), "%s.json", Id);
    if ( !MdoAttachmentPath(Path, sizeof(Path), Project, Session, Name) ||
         !MdoHomeExternalStat(Path, &Exists, &Info) || !Exists ||
         Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size == 0u || Info.Size > MDO_ATTACHMENT_META_MAX )
        return NULL;
    File = MdoHomeOpenRead(Path);
    if ( File == NULL ||
         !xrtReadFull(File, Bytes, (size_t)Info.Size, NULL) ) goto done;
    Bytes[Info.Size] = '\0';
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_ATTACHMENT_META_MAX;
    Config.MaxDepth = 3u;
    Config.MaxValues = 12u;
    Root = xrtJsonRead(xrtStrViewN(Bytes, (size_t)Info.Size), &Config);
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         !MdoAttachmentMetaUInt(Root, "schema_version", &Schema) ||
         (Schema != 1u && Schema != 2u) ||
         xrtValueCount(Root) != (Schema == 1u ? 5u : 6u) ) goto done;
    Value = xrtValueObjectGet(Root, XRT_STR_LITERAL("id"));
    if ( !xrtValueGetString(Value, &StoredId) ||
         StoredId.Size != MDO_ATTACHMENT_ID_LENGTH ||
         memcmp(StoredId.Data, Id, MDO_ATTACHMENT_ID_LENGTH) != 0 )
        goto done;
    Value = xrtValueObjectGet(Root, XRT_STR_LITERAL("mime_type"));
    if ( !xrtValueGetString(Value, &Mime) ||
         !((Mime.Size == 9u && memcmp(Mime.Data, "image/png", 9u) == 0) ||
           (Mime.Size == 10u && memcmp(Mime.Data, "image/jpeg", 10u) == 0) ||
           (Mime.Size == 10u && memcmp(Mime.Data, "image/webp", 10u) == 0)) ||
         !MdoAttachmentMetaUInt(Root, "size", &Size) || Size == 0u ||
         Size > MDO_API_IMAGE_MAX_BYTES ||
         !MdoAttachmentMetaUInt(Root, "created_at", &CreatedAt) ||
         CreatedAt == 0u || CreatedAt > INT64_MAX ) goto done;
    if ( Schema == 2u ) {
        Value = xrtValueObjectGet(Root, XRT_STR_LITERAL("file_name"));
        if ( !xrtValueGetString(Value, &FileName) || FileName.Size == 0u ||
             !MdoAttachmentNameValid(FileName) ) goto done;
    }
    Ok = true;
done:
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    if ( !Ok ) { xrtValueRelease(Root); Root = NULL; }
    return Root;
}

static bool MdoAttachmentCreatedAt(const char* Project,
    const char* Session, const char* Id, xtime* CreatedAt)
{
    xvalue* Root = MdoAttachmentMetadata(Project, Session, Id);
    uint64 Stamp;
    bool Ok = Root != NULL &&
        MdoAttachmentMetaUInt(Root, "created_at", &Stamp);
    if ( Ok ) *CreatedAt = (xtime)Stamp;
    xrtValueRelease(Root);
    return Ok;
}

bool MdoAttachmentIdsRead(const xvalue* Array, char Ids[4][33], size_t* Count)
{
    return MdoImageIdsRead(Array, Ids, Count);
}

bool MdoAttachmentIdsExist(const char* Project, const char* Session,
    const char Ids[4][33], size_t Count)
{
    size_t i;
    size_t Total = 0u;
    if ( Project == NULL || Session == NULL || Ids == NULL || Count > 4u )
        return false;
    for ( i = 0u; i < Count; ++i ) {
        char Name[MDO_ATTACHMENT_ID_LENGTH + 6u];
        char Path[MDO_SESSION_PATH_CAPACITY];
        char Checked[MDO_ATTACHMENT_ID_LENGTH + 1u];
        bool Exists = false;
        xfileinfo Info;
        if ( !MdoAttachmentHexId(xrtStrView(Ids[i]), Checked) )
            return false;
        snprintf(Name, sizeof(Name), "%s.json", Ids[i]);
        if ( !MdoAttachmentPath(Path, sizeof(Path), Project, Session,
                Name) ||
             !MdoHomeExternalStat(Path, &Exists, &Info) || !Exists ||
             Info.Type != XFILE_TYPE_FILE ) return false;
        snprintf(Name, sizeof(Name), "%s.bin", Ids[i]);
        if ( !MdoAttachmentPath(Path, sizeof(Path), Project, Session,
                Name) ||
             !MdoHomeExternalStat(Path, &Exists, &Info) || !Exists ||
             Info.Type != XFILE_TYPE_FILE ||
             (Info.Available & XFILE_INFO_SIZE) == 0u ||
             Info.Size == 0u || Info.Size > MDO_API_IMAGE_MAX_BYTES ||
             Info.Size > 16u * 1024u * 1024u - Total )
            return false;
        Total += (size_t)Info.Size;
    }
    return true;
}

bool MdoAttachmentIdsWriteValue(xvalue* Object, const char Ids[4][33],
    size_t Count)
{
    xvalue* Array = xrtValueArray();
    size_t i;
    bool Ok = Array != NULL && Object != NULL && Count <= 4u;
    for ( i = 0u; Ok && i < Count; ++i )
        Ok = MdoApiValueAppendString(Array, Ids[i]);
    if ( Ok ) Ok = MdoApiValueSetTake(Object, "attachments", &Array);
    xrtValueRelease(Array);
    return Ok;
}

bool MdoApiAttachmentsRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char Session[MDO_SESSION_ID_CAPACITY];
    char Directory[MDO_SESSION_PATH_CAPACITY];
    char Path[MDO_SESSION_PATH_CAPACITY];
    char MetaPath[MDO_SESSION_PATH_CAPACITY];
    char Name[MDO_ATTACHMENT_ID_LENGTH + 6u];
    char Id[MDO_ATTACHMENT_ID_LENGTH + 1u];
    char FileName[MDO_ATTACHMENT_NAME_MAX_BYTES + 1u];
    char Url[2u * MDO_SESSION_PATH_CAPACITY];
    char* Data = NULL;
    size_t Size = 0u;
    MdoApiBodyStatus Status;
    xstrview Type;
    cstr Mime;
    xvalue* Reply;
    bool Ok;
    bool Exists;
    xfileinfo Existing;
    size_t Attempt;
    int Written;
    if ( !MdoAttachmentSession(Context, Project, Session, true) )
        return MdoApiReplyError(Context, 404u, "session_not_found",
            "An active session is required for image upload", NULL);
    if ( !MdoAttachmentType(Context, &Type) )
        return MdoApiReplyError(Context, 415u, "unsupported_media_type",
            "Content-Type must be image/png, image/jpeg, or image/webp", NULL);
    if ( !MdoAttachmentName(Context, FileName) )
        return MdoApiReplyError(Context, 400u, "image_name_invalid",
            "X-Mdo-File-Name must be one percent-encoded UTF-8 leaf name of at most 1024 bytes", NULL);
    Status = MdoApiBinaryBodyRead(Context, MDO_API_IMAGE_MAX_BYTES,
        &Data, &Size);
    if ( Status != MDO_API_BODY_OK )
        return MdoApiReplyError(Context,
            Status == MDO_API_BODY_TOO_LARGE ? 413u : 400u,
            Status == MDO_API_BODY_TOO_LARGE ? "image_too_large" :
                "image_body_invalid",
            Status == MDO_API_BODY_TOO_LARGE ?
                "An image may not exceed 8 MiB" :
                "The image body could not be decoded", NULL);
    Mime = MdoAttachmentMime(Type, (const unsigned char*)Data, Size);
    if ( Mime == NULL ) {
        xrtFree(Data);
        return MdoApiReplyError(Context, 415u, "image_type_invalid",
            "Image bytes do not match a supported Content-Type", NULL);
    }
    Written = snprintf(Directory, sizeof(Directory),
        "sessions/%s/%s/attachments", Project, Session);
    if ( Written <= 0 || (size_t)Written >= sizeof(Directory) ) {
        xrtFree(Data);
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "The session path is too long", NULL);
    }
    if ( g_MdoAttachmentLock == NULL ||
         !xrtMutexLock(g_MdoAttachmentLock) ) {
        xrtFree(Data);
        return MdoApiReplyError(Context, 503u, "attachment_unavailable",
            "Image storage is unavailable", NULL);
    }
    Ok = MdoAttachmentQuota(Directory, Size);
    if ( !Ok && MdoAttachmentCollectExpired(Project, Session, Directory) )
        Ok = MdoAttachmentQuota(Directory, Size);
    for ( Attempt = 0u; Ok && Attempt < 4u; ++Attempt ) {
        Ok = MdoAttachmentNewId(Id);
        if ( !Ok ) break;
        Written = snprintf(Name, sizeof(Name), "%s.bin", Id);
        Ok = Written > 0 && (size_t)Written < sizeof(Name) &&
            MdoAttachmentPath(Path, sizeof(Path), Project, Session, Name);
        if ( !Ok ) break;
        Written = snprintf(Name, sizeof(Name), "%s.json", Id);
        Ok = Written > 0 && (size_t)Written < sizeof(Name) &&
            MdoAttachmentPath(MetaPath, sizeof(MetaPath), Project, Session,
                Name);
        if ( !Ok ) break;
        if ( !MdoHomeExternalStat(Path, &Exists, &Existing) ) {
            Ok = false;
            break;
        }
        if ( Exists ) continue;
        if ( !MdoHomeExternalStat(MetaPath, &Exists, &Existing) ) {
            Ok = false;
            break;
        }
        if ( !Exists ) break;
    }
    if ( Attempt == 4u ) Ok = false;
    if ( Ok ) {
        xvalue* Metadata = xrtValueObject();
        char* Meta = NULL;
        size_t MetaSize = 0u;
        Ok = Metadata != NULL &&
            MdoApiValueSetUInt(Metadata, "schema_version", FileName[0] ? 2u : 1u) &&
            MdoApiValueSetString(Metadata, "id", Id) &&
            MdoApiValueSetString(Metadata, "mime_type", Mime) &&
            MdoApiValueSetUInt(Metadata, "size", Size) &&
            MdoApiValueSetInt(Metadata, "created_at", xrtNow()) &&
            (!FileName[0] || MdoApiValueSetString(Metadata, "file_name", FileName));
        if ( Ok ) Meta = xrtJsonStringify(Metadata, false, &MetaSize);
        Ok = Meta != NULL && MetaSize <= MDO_ATTACHMENT_META_MAX &&
            MdoHomeAtomicWrite(Path, Data, Size, false);
        if ( Ok && !MdoHomeAtomicWrite(MetaPath, Meta, MetaSize,
                false) ) {
            (void)MdoHomeRemove(Path, false);
            Ok = false;
        }
        xrtFree(Meta);
        xrtValueRelease(Metadata);
    }
    (void)xrtMutexUnlock(g_MdoAttachmentLock);
    xrtFree(Data);
    if ( !Ok ) return MdoApiReplyError(Context, 507u,
        "attachment_storage_full",
        "The session image quota or storage limit was reached", NULL);
    Written = snprintf(Url, sizeof(Url),
        "/api/v1/projects/%s/sessions/%s/attachments/%s",
        Project, Session, Id);
    Reply = xrtValueObject();
    if ( Written <= 0 || (size_t)Written >= sizeof(Url) || Reply == NULL ||
         !MdoApiValueSetString(Reply, "id", Id) ||
         !MdoApiValueSetString(Reply, "mime_type", Mime) ||
         !MdoApiValueSetString(Reply, "file_name", FileName) ||
         !MdoApiValueSetUInt(Reply, "size", Size) ||
         !MdoApiValueSetString(Reply, "url", Url) ) {
        xrtValueRelease(Reply);
        (void)MdoHomeRemove(MetaPath, false);
        (void)MdoHomeRemove(Path, false);
        return MdoApiReplyError(Context, 500u, "attachment_response_failed",
            "The image was stored but its response could not be created",
            NULL);
    }
    return MdoApiReplySuccessTake(Context, 201u, Reply, NULL);
}

bool MdoAttachmentReadForRun(const char* Project, const char* Session,
    const char* Id, char** Output, size_t* Size, cstr* Mime)
{
    char Name[MDO_ATTACHMENT_ID_LENGTH + 6u];
    char Checked[MDO_ATTACHMENT_ID_LENGTH + 1u];
    char Path[MDO_SESSION_PATH_CAPACITY];
    char MetaPath[MDO_SESSION_PATH_CAPACITY];
    bool Exists = false;
    xfileinfo Info;
    xfile File = NULL;
    char* Data = NULL;
    bool Ok;
    if ( Project == NULL || Session == NULL || Id == NULL ||
         Output == NULL || Size == NULL || Mime == NULL ||
         !MdoAttachmentHexId(xrtStrView(Id), Checked) ) return false;
    *Output = NULL;
    *Size = 0u;
    *Mime = NULL;
    snprintf(Name, sizeof(Name), "%s.json", Id);
    if ( !MdoAttachmentPath(MetaPath, sizeof(MetaPath), Project, Session,
            Name) ||
         !MdoHomeExternalStat(MetaPath, &Exists, &Info) || !Exists ||
         Info.Type != XFILE_TYPE_FILE )
        return false;
    snprintf(Name, sizeof(Name), "%s.bin", Id);
    if ( !MdoAttachmentPath(Path, sizeof(Path), Project, Session, Name) ||
         !MdoHomeExternalStat(Path, &Exists, &Info) || !Exists ||
         Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u || Info.Size == 0u ||
         Info.Size > MDO_API_IMAGE_MAX_BYTES )
        return false;
    File = MdoHomeOpenRead(Path);
    Data = (char*)xrtMalloc((size_t)Info.Size);
    Ok = File != NULL && Data != NULL &&
        xrtReadFull(File, Data, (size_t)Info.Size, NULL);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    if ( !Ok ) {
        xrtFree(Data);
        return false;
    }
    *Mime = MdoAttachmentMime((xstrview){0},
        (const unsigned char*)Data, (size_t)Info.Size);
    if ( *Mime == NULL ) {
        xrtFree(Data);
        return false;
    }
    *Output = Data;
    *Size = (size_t)Info.Size;
    return true;
}

/* Caller holds the attachment lock. Reference readers take their own locks
 * only after it; writers acquire locks in the same order. */
static MdoAttachmentDiscardResult MdoAttachmentDiscardLocked(
    const char* Project, const char* Session, const char* Id)
{
    char Name[MDO_ATTACHMENT_ID_LENGTH + 6u];
    char DataPath[MDO_SESSION_PATH_CAPACITY];
    char MetaPath[MDO_SESSION_PATH_CAPACITY];
    MdoSession* Handle;
    MdoSessionInfo Info;
    xwork_error Error;
    bool DataExists = false;
    bool MetaExists = false;
    bool Referenced = false;
    xfileinfo FileInfo;
    bool Ok;
    snprintf(Name, sizeof(Name), "%s.bin", Id);
    if ( !MdoAttachmentPath(DataPath, sizeof(DataPath), Project,
            Session, Name) ) return MDO_ATTACHMENT_DISCARD_ERROR;
    snprintf(Name, sizeof(Name), "%s.json", Id);
    if ( !MdoAttachmentPath(MetaPath, sizeof(MetaPath), Project,
            Session, Name) ) return MDO_ATTACHMENT_DISCARD_ERROR;
    memset(&Error, 0, sizeof(Error));
    Handle = MdoSessionLoad(Project, Session, &Error);
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    Ok = Handle != NULL && MdoSessionGetInfo(Handle, &Info);
    if ( Handle != NULL ) MdoSessionRelease(Handle);
    if ( !Ok ) return MDO_ATTACHMENT_DISCARD_ERROR;
    if ( Info.RuntimeOpen ) return MDO_ATTACHMENT_DISCARD_IN_USE;
    Ok = MdoHomeExternalStat(DataPath, &DataExists, &FileInfo) &&
        (!DataExists || FileInfo.Type == XFILE_TYPE_FILE) &&
        MdoHomeExternalStat(MetaPath, &MetaExists, &FileInfo) &&
        (!MetaExists || FileInfo.Type == XFILE_TYPE_FILE);
    if ( !Ok ) return MDO_ATTACHMENT_DISCARD_ERROR;
    if ( !DataExists && !MetaExists ) return MDO_ATTACHMENT_DISCARD_MISSING;
    Ok = MdoApiDraftAttachmentReferenced(Project, Session,
        Id, &Referenced);
    if ( Ok && !Referenced )
        Ok = MdoApiQueueAttachmentReferenced(Project, Session,
            Id, &Referenced);
    if ( Ok && !Referenced ) {
        Ok = MdoSessionAttachmentRecordReferenced(Project, Session,
            Id, &Referenced);
        if ( Ok && Referenced ) {
            Ok = MdoSessionAttachmentPruneRemoved(Project, Session);
            if ( Ok ) Ok = MdoSessionAttachmentRecordReferenced(Project,
                Session, Id, &Referenced);
        }
    }
    if ( !Ok ) return MDO_ATTACHMENT_DISCARD_ERROR;
    if ( Referenced ) return MDO_ATTACHMENT_DISCARD_IN_USE;
    if ( DataExists && !MdoHomeRemove(DataPath, false) )
        return MDO_ATTACHMENT_DISCARD_ERROR;
    if ( MetaExists && !MdoHomeRemove(MetaPath, false) )
        return MDO_ATTACHMENT_DISCARD_ERROR;
    return MDO_ATTACHMENT_DISCARD_REMOVED;
}

/* Reclaim only old uploads on session reopen or quota pressure. Recent
 * uploads may still be in a browser tab whose draft has not been saved.
 * A crash can leave either half of the data/metadata pair; for a data-only
 * file, use its modification time only when metadata is truly absent. */
static bool MdoAttachmentCollectExpired(const char* Project,
    const char* Session, const char* Directory)
{
    char Candidates[MDO_ATTACHMENT_SWEEP_MAX][MDO_ATTACHMENT_ID_LENGTH + 1u];
    size_t CandidateCount = 0u;
    size_t Visited = 0u;
    bool Exists = false;
    xfileinfo Info;
    xdir Dir;
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_END;
    bool Ok = true;
    xtime Cutoff = xrtNow() - MDO_ATTACHMENT_GRACE_US;
    size_t i;
    if ( !MdoHomeExternalStat(Directory, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_DIRECTORY ) return false;
    Dir = MdoHomeOpenDirectory(Directory, XDIR_STAT);
    if ( Dir == NULL ) return false;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Dir, &Entry)) == XDIR_NEXT_ITEM ) {
        xtime Stamp = 0;
        char Id[MDO_ATTACHMENT_ID_LENGTH + 1u];
        char Name[MDO_ATTACHMENT_ID_LENGTH + 6u];
        char Path[MDO_SESSION_PATH_CAPACITY];
        xfileinfo Counterpart;
        bool CounterpartExists = false;
        bool DataFile;
        bool MetaFile;
        if ( ++Visited > MDO_ATTACHMENT_SWEEP_MAX * 4u ) {
            Ok = false;
            break;
        }
        if ( Entry.Info.Type != XFILE_TYPE_FILE ||
             (Entry.Name.Size != MDO_ATTACHMENT_ID_LENGTH + 4u &&
              Entry.Name.Size != MDO_ATTACHMENT_ID_LENGTH + 5u) ||
             !MdoAttachmentHexId(xrtStrViewN(Entry.Name.Data,
                MDO_ATTACHMENT_ID_LENGTH), Id) ) continue;
        DataFile = Entry.Name.Size == MDO_ATTACHMENT_ID_LENGTH + 4u &&
            memcmp(Entry.Name.Data + MDO_ATTACHMENT_ID_LENGTH,
                ".bin", 4u) == 0;
        MetaFile = Entry.Name.Size == MDO_ATTACHMENT_ID_LENGTH + 5u &&
            memcmp(Entry.Name.Data + MDO_ATTACHMENT_ID_LENGTH,
                ".json", 5u) == 0;
        if ( !DataFile && !MetaFile ) continue;
        if ( DataFile &&
             !MdoAttachmentCreatedAt(Project, Session, Id, &Stamp) ) {
            snprintf(Name, sizeof(Name), "%s.json", Id);
            if ( !MdoAttachmentPath(Path, sizeof(Path), Project, Session,
                    Name) ||
                 !MdoHomeExternalStat(Path, &CounterpartExists,
                    &Counterpart) ) { Ok = false; break; }
            if ( CounterpartExists ||
                 (Entry.Info.Available & XFILE_INFO_MODIFY_TIME) == 0u ||
                 Entry.Info.Modified <= 0 ) continue;
            Stamp = Entry.Info.Modified;
        } else if ( MetaFile ) {
            if ( !MdoAttachmentCreatedAt(Project, Session, Id, &Stamp) )
                continue; /* Corrupt metadata needs manual inspection. */
            snprintf(Name, sizeof(Name), "%s.bin", Id);
            if ( !MdoAttachmentPath(Path, sizeof(Path), Project, Session,
                    Name) ||
                 !MdoHomeExternalStat(Path, &CounterpartExists,
                    &Counterpart) ) { Ok = false; break; }
            if ( CounterpartExists ) continue; /* The data entry owns it. */
        }
        if ( Stamp > Cutoff ) continue;
        if ( CandidateCount == MDO_ATTACHMENT_SWEEP_MAX ) {
            Ok = false;
            break;
        }
        memcpy(Candidates[CandidateCount++], Id, sizeof(Id));
    }
    if ( Next == XDIR_NEXT_ERROR ) Ok = false;
    if ( !xrtDirClose(Dir) ) Ok = false;
    if ( !Ok ) return false;
    if ( CandidateCount != 0u &&
         !MdoSessionAttachmentPruneRemoved(Project, Session) ) return false;
    for ( i = 0u; i < CandidateCount; ++i ) {
        MdoAttachmentDiscardResult Result = MdoAttachmentDiscardLocked(
            Project, Session, Candidates[i]);
        if ( Result == MDO_ATTACHMENT_DISCARD_ERROR ) return false;
    }
    return true;
}

bool MdoApiAttachmentSweepExpired(const char* ProjectId,
    const char* SessionId)
{
    char Directory[MDO_SESSION_PATH_CAPACITY];
    bool Ok;
    int Written;
    MdoProjectLease* Lease;
    if ( ProjectId == NULL || SessionId == NULL ) return false;
    Written = snprintf(Directory, sizeof(Directory),
        "sessions/%s/%s/attachments", ProjectId, SessionId);
    if ( Written <= 0 || (size_t)Written >= sizeof(Directory) ) return false;
    Lease = MdoProjectLeaseAcquire(ProjectId, MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return false;
    if ( !MdoApiAttachmentLock() ) {
        MdoProjectLeaseRelease(Lease);
        return false;
    }
    Ok = MdoAttachmentCollectExpired(ProjectId, SessionId, Directory);
    MdoApiAttachmentUnlock();
    MdoProjectLeaseRelease(Lease);
    return Ok;
}

bool MdoApiAttachmentInfoRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char Session[MDO_SESSION_ID_CAPACITY];
    char Id[MDO_ATTACHMENT_ID_LENGTH + 1u];
    char Name[MDO_ATTACHMENT_ID_LENGTH + 6u];
    char Path[MDO_SESSION_PATH_CAPACITY];
    xvalue* Metadata;
    uint64 Size;
    bool Exists = false;
    xfileinfo Info;
    bool Ok;
    if ( !MdoAttachmentSession(Context, Project, Session, false) ||
         !MdoAttachmentHexId(Context->Params[2], Id) )
        return MdoApiReplyError(Context, 404u, "attachment_not_found",
            "The image does not exist in this session", NULL);
    if ( !MdoApiAttachmentLock() )
        return MdoApiReplyError(Context, 503u, "attachment_unavailable",
            "Image storage is unavailable", NULL);
    Metadata = MdoAttachmentMetadata(Project, Session, Id);
    snprintf(Name, sizeof(Name), "%s.bin", Id);
    Ok = Metadata != NULL && MdoAttachmentMetaUInt(Metadata, "size", &Size) &&
        MdoAttachmentPath(Path, sizeof(Path), Project, Session, Name) &&
        MdoHomeExternalStat(Path, &Exists, &Info) && Exists &&
        Info.Type == XFILE_TYPE_FILE &&
        (Info.Available & XFILE_INFO_SIZE) != 0u && Info.Size == Size;
    MdoApiAttachmentUnlock();
    if ( !Ok ) {
        xrtValueRelease(Metadata);
        return MdoApiReplyError(Context, 404u, "attachment_not_found",
            "Valid image metadata is unavailable in this session", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Metadata, NULL);
}

bool MdoApiAttachmentRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char Session[MDO_SESSION_ID_CAPACITY];
    char Id[MDO_ATTACHMENT_ID_LENGTH + 1u];
    char* Data = NULL;
    size_t Size = 0u;
    cstr Mime = NULL;
    bool Ok;
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_DELETE ) {
        const xhttp1head* Head = Context->Request->head;
        MdoAttachmentDiscardResult Result;
        xvalue* Reply;
        if ( ((Head->Flags & (uint32)XHTTP1_CONTENT_LENGTH) != 0u &&
              Head->ContentLength != 0u) ||
             (Head->Flags & (uint32)XHTTP1_TRANSFER_ENCODING) != 0u )
            return MdoApiReplyError(Context, 400u, "body_not_allowed",
                "This operation does not accept a request body", NULL);
        if ( !MdoAttachmentSession(Context, Project, Session, false) ||
             !MdoAttachmentHexId(Context->Params[2], Id) )
            return MdoApiReplyError(Context, 404u, "attachment_not_found",
                "The image does not exist in this session", NULL);
        if ( !MdoApiAttachmentLock() )
            return MdoApiReplyError(Context, 503u, "attachment_unavailable",
                "Image storage is unavailable", NULL);
        Result = MdoAttachmentDiscardLocked(Project, Session, Id);
        MdoApiAttachmentUnlock();
        if ( (Result == MDO_ATTACHMENT_DISCARD_REMOVED ||
              Result == MDO_ATTACHMENT_DISCARD_MISSING) &&
             !MdoApiQueueDiscardAcknowledged(Project, Session, Id) )
            return MdoApiReplyError(Context, 503u,
                "queue_unavailable", "Image cleanup could not be recorded",
                NULL);
        if ( Result == MDO_ATTACHMENT_DISCARD_ERROR )
            return MdoApiReplyError(Context, 503u,
            "attachment_unavailable",
            "The image could not be checked or removed", NULL);
        if ( Result == MDO_ATTACHMENT_DISCARD_MISSING )
            return MdoApiReplyError(Context, 404u, "attachment_not_found",
                "The image does not exist in this session", NULL);
        if ( Result == MDO_ATTACHMENT_DISCARD_IN_USE )
            return MdoApiReplyError(Context, 409u,
            "attachment_in_use", "The image is still used by this session",
            NULL);
        Reply = xrtValueObject();
        if ( Reply == NULL ) return MdoApiReplyError(Context, 503u,
            "attachment_unavailable", "The image was removed", NULL);
        return MdoApiReplySuccessTake(Context, 200u, Reply, NULL);
    }
    if ( !MdoAttachmentSession(Context, Project, Session, false) ||
         !MdoAttachmentHexId(Context->Params[2], Id) ||
         !MdoAttachmentReadForRun(Project, Session, Id,
            &Data, &Size, &Mime) )
        return MdoApiReplyError(Context, 404u, "attachment_not_found",
            "The image does not exist in this session", NULL);
    Ok = MdoApiReplyImage(Context, Data, Size, Mime);
    xrtFree(Data);
    return Ok;
}
