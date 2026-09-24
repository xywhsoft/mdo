#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/sessions.h"

#define MDO_ATTACHMENT_ID_BYTES 16u
#define MDO_ATTACHMENT_ID_LENGTH (MDO_ATTACHMENT_ID_BYTES * 2u)
#define MDO_ATTACHMENT_SESSION_MAX 16u
#define MDO_ATTACHMENT_SESSION_BYTES (32u * 1024u * 1024u)

static xmutex* g_MdoAttachmentLock;

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

bool MdoApiAttachmentsRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char Session[MDO_SESSION_ID_CAPACITY];
    char Directory[MDO_SESSION_PATH_CAPACITY];
    char Path[MDO_SESSION_PATH_CAPACITY];
    char MetaPath[MDO_SESSION_PATH_CAPACITY];
    char Name[MDO_ATTACHMENT_ID_LENGTH + 6u];
    char Id[MDO_ATTACHMENT_ID_LENGTH + 1u];
    char Meta[256];
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
        Written = snprintf(Meta, sizeof(Meta),
            "{\"schema_version\":1,\"id\":\"%s\",\"mime_type\":\"%s\","
            "\"size\":%llu,\"created_at\":%lld}", Id, Mime,
            (unsigned long long)Size, (long long)xrtNow());
        Ok = Written > 0 && (size_t)Written < sizeof(Meta) &&
            MdoHomeAtomicWrite(Path, Data, Size, false);
        if ( Ok && !MdoHomeAtomicWrite(MetaPath, Meta, (size_t)Written,
                false) ) {
            (void)MdoHomeRemove(Path, false);
            Ok = false;
        }
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

bool MdoApiAttachmentRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char Session[MDO_SESSION_ID_CAPACITY];
    char Id[MDO_ATTACHMENT_ID_LENGTH + 1u];
    char Name[MDO_ATTACHMENT_ID_LENGTH + 6u];
    char Path[MDO_SESSION_PATH_CAPACITY];
    char MetaPath[MDO_SESSION_PATH_CAPACITY];
    bool Exists = false;
    xfileinfo Info;
    xfile File = NULL;
    char* Data = NULL;
    cstr Mime;
    bool Ok;
    if ( !MdoAttachmentSession(Context, Project, Session, false) ||
         !MdoAttachmentHexId(Context->Params[2], Id) )
        return MdoApiReplyError(Context, 404u, "attachment_not_found",
            "The image does not exist in this session", NULL);
    snprintf(Name, sizeof(Name), "%s.json", Id);
    if ( !MdoAttachmentPath(MetaPath, sizeof(MetaPath), Project, Session,
            Name) ||
         !MdoHomeExternalStat(MetaPath, &Exists, &Info) || !Exists ||
         Info.Type != XFILE_TYPE_FILE )
        return MdoApiReplyError(Context, 404u, "attachment_not_found",
            "The image does not exist in this session", NULL);
    snprintf(Name, sizeof(Name), "%s.bin", Id);
    if ( !MdoAttachmentPath(Path, sizeof(Path), Project, Session, Name) ||
         !MdoHomeExternalStat(Path, &Exists, &Info) || !Exists ||
         Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u || Info.Size == 0u ||
         Info.Size > MDO_API_IMAGE_MAX_BYTES )
        return MdoApiReplyError(Context, 404u, "attachment_not_found",
            "The image does not exist in this session", NULL);
    File = MdoHomeOpenRead(Path);
    Data = (char*)xrtMalloc((size_t)Info.Size);
    Ok = File != NULL && Data != NULL &&
        xrtReadFull(File, Data, (size_t)Info.Size, NULL);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    if ( !Ok ) {
        xrtFree(Data);
        return MdoApiReplyError(Context, 503u, "attachment_read_failed",
            "The image could not be read", NULL);
    }
    Mime = MdoAttachmentMime((xstrview){0},
        (const unsigned char*)Data, (size_t)Info.Size);
    if ( Mime == NULL ) {
        xrtFree(Data);
        return MdoApiReplyError(Context, 422u, "attachment_corrupt",
            "The stored image signature is invalid", NULL);
    }
    Ok = MdoApiReplyImage(Context, Data, (size_t)Info.Size, Mime);
    xrtFree(Data);
    return Ok;
}
