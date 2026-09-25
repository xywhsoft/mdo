#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/sessions.h"

#define MDO_DRAFT_TEXT_MAX 65536u
#define MDO_DRAFT_FILE_MAX (256u * 1024u)

typedef struct MdoDraft {
    uint64 Revision;
    bool RunAdmissionUncertain;
    char Text[MDO_DRAFT_TEXT_MAX + 1u];
    size_t TextSize;
    char Attachments[4][33];
    size_t AttachmentCount;
} MdoDraft;

static xmutex* g_MdoDraftLock;
static bool MdoDraftRead(const char* Path, MdoDraft* Draft);

bool MdoApiDraftInit(void)
{
    if ( g_MdoDraftLock != NULL ) return true;
    g_MdoDraftLock = xrtMutexCreate();
    return g_MdoDraftLock != NULL;
}

void MdoApiDraftUnit(void)
{
    if ( g_MdoDraftLock != NULL ) xrtMutexDestroy(g_MdoDraftLock);
    g_MdoDraftLock = NULL;
}

bool MdoApiDraftAttachmentReferenced(const char* ProjectId,
    const char* SessionId, const char* Id, bool* Referenced)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoDraft* Draft;
    size_t i;
    bool Ok;
    int Written;
    if ( ProjectId == NULL || SessionId == NULL || Id == NULL ||
         Referenced == NULL ) return false;
    *Referenced = false;
    Written = snprintf(Path, sizeof(Path), "sessions/%s/%s/draft.json",
        ProjectId, SessionId);
    if ( Written <= 0 || (size_t)Written >= sizeof(Path) ) return false;
    Draft = (MdoDraft*)xrtMalloc(sizeof(*Draft));
    if ( Draft == NULL ) return false;
    xrtMutexLock(g_MdoDraftLock);
    Ok = MdoDraftRead(Path, Draft);
    if ( Ok ) for ( i = 0u; i < Draft->AttachmentCount; ++i )
        if ( strcmp(Draft->Attachments[i], Id) == 0 ) {
            *Referenced = true;
            break;
        }
    xrtMutexUnlock(g_MdoDraftLock);
    xrtFree(Draft);
    return Ok;
}

static bool MdoDraftCaptureId(xstrview View, char* Output,
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
             Byte == '.' ) continue;
        return false;
    }
    memcpy(Output, View.Data, View.Size);
    Output[View.Size] = '\0';
    return true;
}

static bool MdoDraftUInt(const xvalue* Object, cstr Name, uint64* Output)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Name));
    int64 Signed;
    if ( xrtValueType(Value) == XVALUE_UINT )
        return xrtValueGetUInt(Value, Output);
    if ( xrtValueType(Value) != XVALUE_INT ||
         !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Output = (uint64)Signed;
    return true;
}

static bool MdoDraftBool(const xvalue* Object, cstr Name, bool* Output)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Name));
    return xrtValueType(Value) == XVALUE_BOOL &&
        xrtValueGetBool(Value, Output);
}

static bool MdoDraftTextView(const xvalue* Object, cstr Name,
    xstrview* Text)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Name));
    if ( xrtValueType(Value) != XVALUE_STRING ||
         !xrtValueGetString(Value, Text) ||
         Text->Size > MDO_DRAFT_TEXT_MAX ||
         (Text->Size != 0u &&
          memchr(Text->Data, 0, Text->Size) != NULL) ||
         !xrtUtf8Valid(*Text, NULL) ) return false;
    return true;
}

static bool MdoDraftText(const xvalue* Object, cstr Name,
    char Output[MDO_DRAFT_TEXT_MAX + 1u], size_t* Size)
{
    xstrview Text;
    if ( !MdoDraftTextView(Object, Name, &Text) ) return false;
    if ( Text.Size != 0u ) memcpy(Output, Text.Data, Text.Size);
    Output[Text.Size] = '\0';
    *Size = Text.Size;
    return true;
}

static bool MdoDraftRead(const char* Path, MdoDraft* Draft)
{
    bool Exists = false;
    xfileinfo Info;
    xfile File = NULL;
    char* Bytes = NULL;
    xjsonreadconfig Config;
    xvalue* Root = NULL;
    uint64 Schema;
    bool Ok = false;
    memset(Draft, 0, sizeof(*Draft));
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size > MDO_DRAFT_FILE_MAX || Info.Size > SIZE_MAX - 1u )
        return false;
    File = MdoHomeOpenRead(Path);
    Bytes = (char*)xrtMalloc((size_t)Info.Size + 1u);
    if ( File == NULL || Bytes == NULL ||
         (Info.Size != 0u && !xrtReadFull(File, Bytes,
            (size_t)Info.Size, NULL)) ) goto done;
    Bytes[Info.Size] = '\0';
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_DRAFT_FILE_MAX;
    Config.MaxDepth = 4u;
    Config.MaxValues = 16u;
    Config.MaxContainerItems = 8u;
    Root = xrtJsonRead(xrtStrViewN(Bytes, (size_t)Info.Size), &Config);
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         !MdoDraftUInt(Root, "schema_version", &Schema) ||
         (Schema != 1u && Schema != 2u && Schema != 3u) ||
         xrtValueCount(Root) != (Schema == 1u ? 3u :
            (Schema == 2u ? 4u : 5u)) ||
         !MdoDraftUInt(Root, "revision", &Draft->Revision) ||
         Draft->Revision == 0u ||
         !MdoDraftText(Root, "text", Draft->Text, &Draft->TextSize) ||
         (Schema >= 2u &&
          !MdoAttachmentIdsRead(xrtValueObjectGet(Root,
                XRT_STR_LITERAL("attachments")), Draft->Attachments,
                &Draft->AttachmentCount)) ||
         (Schema == 3u && !MdoDraftBool(Root,
            "run_admission_uncertain", &Draft->RunAdmissionUncertain)) )
        goto done;
    Ok = true;
done:
    xrtValueRelease(Root);
    xrtFree(Bytes);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    return Ok;
}

static bool MdoDraftWrite(const char* Path, const MdoDraft* Draft)
{
    xvalue* Root = xrtValueObject();
    char* Json = NULL;
    size_t Size = 0u;
    bool Ok = Root != NULL &&
        MdoApiValueSetUInt(Root, "schema_version", 3u) &&
        MdoApiValueSetUInt(Root, "revision", Draft->Revision) &&
        MdoApiValueSetStringView(Root, "text",
            xrtStrViewN(Draft->Text, Draft->TextSize)) &&
        MdoAttachmentIdsWriteValue(Root, Draft->Attachments,
            Draft->AttachmentCount) &&
        MdoApiValueSetBool(Root, "run_admission_uncertain",
            Draft->RunAdmissionUncertain);
    if ( Ok ) Json = xrtJsonStringify(Root, false, &Size);
    if ( Json != NULL && Size <= MDO_DRAFT_FILE_MAX )
        Ok = MdoHomeAtomicWrite(Path, Json, Size, false);
    else Ok = false;
    xrtFree(Json);
    xrtValueRelease(Root);
    return Ok;
}

static xvalue* MdoDraftResponse(const MdoDraft* Draft)
{
    xvalue* Data = xrtValueObject();
    if ( Data != NULL &&
         MdoApiValueSetUInt(Data, "revision", Draft->Revision) &&
         MdoApiValueSetStringView(Data, "text",
            xrtStrViewN(Draft->Text, Draft->TextSize)) &&
         MdoAttachmentIdsWriteValue(Data, Draft->Attachments,
            Draft->AttachmentCount) &&
         MdoApiValueSetBool(Data, "run_admission_uncertain",
            Draft->RunAdmissionUncertain) ) return Data;
    xrtValueRelease(Data);
    return NULL;
}

bool MdoApiDraftRoute(MdoApiContext* Context)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    char ProjectId[MDO_PROJECT_ID_CAPACITY] = { 0 };
    char SessionId[MDO_SESSION_ID_CAPACITY] = { 0 };
    MdoSession* Session;
    xwork_error Error;
    MdoDraft* Draft;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    uint64 ExpectedRevision;
    xstrview IncomingText = { 0 };
    char IncomingAttachments[4][33] = {{ 0 }};
    size_t IncomingCount = 0u;
    bool IncomingUncertain = false;
    bool UncertainPresent = false;
    bool Ok;
    bool Conflict = false;
    bool AttachmentLocked = false;
    xvalue* Data;

    if ( Context->ParamCount == 0u ) {
        memcpy(Path, "data/draft.json", sizeof("data/draft.json"));
    } else if ( Context->ParamCount == 2u &&
         MdoDraftCaptureId(Context->Params[0], ProjectId,
            sizeof(ProjectId)) &&
         MdoDraftCaptureId(Context->Params[1], SessionId,
            sizeof(SessionId)) &&
         snprintf(Path, sizeof(Path), "sessions/%s/%s/draft.json",
            ProjectId, SessionId) > 0 ) {
        memset(&Error, 0, sizeof(Error));
        Session = MdoSessionLoad(ProjectId, SessionId, &Error);
        if ( Session == NULL ) return MdoApiReplyError(Context, 404u,
            "session_not_found", "The requested session does not exist", NULL);
        MdoSessionRelease(Session);
    } else return MdoApiReplyError(Context, 400u, "invalid_path",
        "Project and session identifiers are invalid", NULL);

    Draft = (MdoDraft*)xrtMalloc(sizeof(*Draft));
    if ( Draft == NULL ) return MdoApiReplyError(Context, 503u,
        "draft_unavailable", "The draft could not be allocated", NULL);
    ExpectedRevision = 0u;
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_PUT ) {
        BodyStatus = MdoApiJsonBodyRead(Context, &Body);
        if ( BodyStatus != MDO_API_BODY_OK ) {
            xrtFree(Draft);
            return MdoApiReplyBodyError(Context, BodyStatus);
        }
        if ( Context->ParamCount == 2u ) {
            AttachmentLocked = MdoApiAttachmentLock();
            if ( !AttachmentLocked ) {
                MdoApiJsonBodyUnit(&Body);
                xrtFree(Draft);
                return MdoApiReplyError(Context, 503u,
                    "attachment_unavailable",
                    "Image storage is unavailable", NULL);
            }
        }
        const xvalue* Attachments = xrtValueObjectGet(Body.Value,
            XRT_STR_LITERAL("attachments"));
        const xvalue* Uncertain = xrtValueObjectGet(Body.Value,
            XRT_STR_LITERAL("run_admission_uncertain"));
        UncertainPresent = Uncertain != NULL;
        Ok = xrtValueType(Body.Value) == XVALUE_OBJECT &&
            xrtValueCount(Body.Value) == (Attachments == NULL ? 2u : 3u) +
                (UncertainPresent ? 1u : 0u) &&
            MdoDraftUInt(Body.Value, "revision", &ExpectedRevision) &&
            MdoDraftTextView(Body.Value, "text", &IncomingText) &&
            (!UncertainPresent || MdoDraftBool(Body.Value,
                "run_admission_uncertain", &IncomingUncertain)) &&
            (Attachments == NULL ||
             MdoAttachmentIdsRead(Attachments, IncomingAttachments,
                &IncomingCount)) &&
            (IncomingCount == 0u ||
             (Context->ParamCount == 2u &&
              MdoAttachmentIdsExist(ProjectId, SessionId,
                IncomingAttachments, IncomingCount)));
        if ( !Ok ) {
            if ( AttachmentLocked ) MdoApiAttachmentUnlock();
            MdoApiJsonBodyUnit(&Body);
            xrtFree(Draft);
            return MdoApiReplyError(Context, 422u, "draft_invalid",
                "Expected a revision and bounded UTF-8 text", NULL);
        }
    }
    xrtMutexLock(g_MdoDraftLock);
    Ok = MdoDraftRead(Path, Draft);
    if ( Ok && Context->Request->head->MethodCode == XHTTP_METHOD_PUT ) {
        Conflict = Draft->Revision != ExpectedRevision;
        if ( !Conflict && Draft->Revision == UINT64_MAX ) Ok = false;
        if ( Ok && !Conflict ) {
            Draft->Revision++;
            if ( IncomingText.Size != 0u )
                memcpy(Draft->Text, IncomingText.Data, IncomingText.Size);
            Draft->Text[IncomingText.Size] = '\0';
            Draft->TextSize = IncomingText.Size;
            memcpy(Draft->Attachments, IncomingAttachments,
                sizeof(Draft->Attachments));
            Draft->AttachmentCount = IncomingCount;
            if ( UncertainPresent )
                Draft->RunAdmissionUncertain = IncomingUncertain;
            Ok = MdoDraftWrite(Path, Draft);
        }
    }
    Data = Ok && !Conflict ? MdoDraftResponse(Draft) : NULL;
    xrtMutexUnlock(g_MdoDraftLock);
    if ( AttachmentLocked ) MdoApiAttachmentUnlock();
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_PUT )
        MdoApiJsonBodyUnit(&Body);
    xrtFree(Draft);
    if ( Conflict ) return MdoApiReplyError(Context, 409u,
        "draft_conflict", "The draft changed in another window", NULL);
    if ( Data == NULL ) return MdoApiReplyError(Context, 503u,
        "draft_unavailable", "The draft could not be read or saved", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
