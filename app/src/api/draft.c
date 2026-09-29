#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "profile.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/sessions.h"

#define MDO_DRAFT_TEXT_MAX 65536u
#define MDO_DRAFT_FILE_MAX (256u * 1024u)
#define MDO_DRAFT_SUBMISSIONS_MAX 20u
#define MDO_DRAFT_SUBMISSIONS_TEXT_MAX (192u * 1024u)

typedef enum MdoDraftSubmissionState {
    MDO_DRAFT_PREPARED,
    MDO_DRAFT_POSTING,
    MDO_DRAFT_REJECTED
} MdoDraftSubmissionState;

typedef struct MdoDraftSubmission {
    char Id[33];
    char Text[MDO_DRAFT_TEXT_MAX + 1u];
    size_t TextSize;
    char Attachments[4][33];
    size_t AttachmentCount;
    bool Interrupt;
    MdoDraftSubmissionState State;
    MdoApiProfile Profile;
} MdoDraftSubmission;

typedef struct MdoDraftNewTask {
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char Title[MDO_SESSION_TITLE_CAPACITY];
    char AgentId[MDO_SESSION_IDENTITY_CAPACITY];
    char ModelId[MDO_SESSION_IDENTITY_CAPACITY];
    char ReasoningEffort[MDO_SESSION_REASONING_CAPACITY];
    char PermissionProfile[MDO_SESSION_REASONING_CAPACITY];
    bool Copying;
} MdoDraftNewTask;

typedef struct MdoDraft {
    uint64 Revision;
    bool RunAdmissionUncertain;
    char Text[MDO_DRAFT_TEXT_MAX + 1u];
    size_t TextSize;
    char Attachments[4][33];
    size_t AttachmentCount;
    MdoApiProfile ComposerProfile;
    MdoDraftSubmission* Submissions[MDO_DRAFT_SUBMISSIONS_MAX];
    size_t SubmissionCount;
    MdoDraftNewTask NewTask;
    bool HasNewTask;
} MdoDraft;

static xmutex* g_MdoDraftLock;
static bool MdoDraftRead(const char* Path, MdoDraft* Draft);

static void MdoDraftRelease(MdoDraft* Draft)
{
    size_t i;
    if ( Draft == NULL ) return;
    for ( i = 0u; i < Draft->SubmissionCount; ++i )
        xrtFree(Draft->Submissions[i]);
    xrtFree(Draft);
}

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
    if ( Ok && !*Referenced ) {
        for ( i = 0u; i < Draft->SubmissionCount; ++i ) {
            const MdoDraftSubmission* Item = Draft->Submissions[i];
            size_t j;
            for ( j = 0u; j < Item->AttachmentCount; ++j )
                if ( strcmp(Item->Attachments[j], Id) == 0 ) {
                    *Referenced = true;
                    break;
                }
            if ( *Referenced ) break;
        }
    }
    xrtMutexUnlock(g_MdoDraftLock);
    MdoDraftRelease(Draft);
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

static bool MdoDraftNewTaskString(const xvalue* Value, cstr Name,
    char* Output, size_t Capacity)
{
    const xvalue* Field = xrtValueObjectGet(Value, xrtStrView(Name));
    xstrview Text;
    if ( xrtValueType(Field) != XVALUE_STRING ||
         !xrtValueGetString(Field, &Text) || Text.Size == 0u ||
         Text.Size >= Capacity || memchr(Text.Data, 0, Text.Size) != NULL ||
         !xrtUtf8Valid(Text, NULL) ) return false;
    memcpy(Output, Text.Data, Text.Size);
    Output[Text.Size] = '\0';
    return true;
}

static bool MdoDraftNewTaskRead(const xvalue* Value,
    MdoDraftNewTask* Task, bool* Present)
{
    const xvalue* Phase;
    xstrview PhaseText;
    size_t i;
    *Present = false;
    memset(Task, 0, sizeof(*Task));
    if ( xrtValueType(Value) == XVALUE_NULL ) return true;
    if ( xrtValueType(Value) != XVALUE_OBJECT ||
         xrtValueCount(Value) != 8u ||
         !MdoDraftNewTaskString(Value, "project_id", Task->ProjectId,
            sizeof(Task->ProjectId)) ||
         !MdoDraftNewTaskString(Value, "session_id", Task->SessionId,
            sizeof(Task->SessionId)) ||
         !MdoDraftNewTaskString(Value, "title", Task->Title,
            sizeof(Task->Title)) ||
         !MdoDraftNewTaskString(Value, "agent_id", Task->AgentId,
            sizeof(Task->AgentId)) ||
         !MdoDraftNewTaskString(Value, "model_id", Task->ModelId,
            sizeof(Task->ModelId)) ||
         !MdoDraftNewTaskString(Value, "reasoning_effort",
            Task->ReasoningEffort, sizeof(Task->ReasoningEffort)) ||
         !MdoDraftNewTaskString(Value, "permission_profile",
            Task->PermissionProfile, sizeof(Task->PermissionProfile)) )
        return false;
    for ( i = 0u; Task->SessionId[i] != '\0'; ++i )
        if ( !((Task->SessionId[i] >= '0' && Task->SessionId[i] <= '9') ||
               (Task->SessionId[i] >= 'a' && Task->SessionId[i] <= 'f')) )
            return false;
    if ( i != 32u ) return false;
    for ( i = 0u; Task->ProjectId[i] != '\0'; ++i )
        if ( !((Task->ProjectId[i] >= 'a' && Task->ProjectId[i] <= 'z') ||
               (Task->ProjectId[i] >= 'A' && Task->ProjectId[i] <= 'Z') ||
               (Task->ProjectId[i] >= '0' && Task->ProjectId[i] <= '9') ||
               Task->ProjectId[i] == '-' || Task->ProjectId[i] == '_' ||
               (Task->ProjectId[i] == '.' && i != 0u)) ) return false;
    Phase = xrtValueObjectGet(Value, XRT_STR_LITERAL("phase"));
    if ( xrtValueType(Phase) != XVALUE_STRING ||
         !xrtValueGetString(Phase, &PhaseText) ) return false;
    if ( PhaseText.Size == 7u &&
         memcmp(PhaseText.Data, "copying", 7u) == 0 ) Task->Copying = true;
    else if ( PhaseText.Size != 8u ||
              memcmp(PhaseText.Data, "creating", 8u) != 0 ) return false;
    *Present = true;
    return true;
}

static xvalue* MdoDraftNewTaskValue(const MdoDraft* Draft)
{
    xvalue* Value;
    const MdoDraftNewTask* Task = &Draft->NewTask;
    if ( !Draft->HasNewTask ) return xrtValueNull();
    Value = xrtValueObject();
    if ( Value != NULL &&
         MdoApiValueSetString(Value, "project_id", Task->ProjectId) &&
         MdoApiValueSetString(Value, "session_id", Task->SessionId) &&
         MdoApiValueSetString(Value, "title", Task->Title) &&
         MdoApiValueSetString(Value, "agent_id", Task->AgentId) &&
         MdoApiValueSetString(Value, "model_id", Task->ModelId) &&
         MdoApiValueSetString(Value, "reasoning_effort",
            Task->ReasoningEffort) &&
         MdoApiValueSetString(Value, "permission_profile",
            Task->PermissionProfile) &&
         MdoApiValueSetString(Value, "phase",
            Task->Copying ? "copying" : "creating") ) return Value;
    xrtValueRelease(Value);
    return NULL;
}

static bool MdoDraftSubmissionRead(const xvalue* Value,
    MdoDraftSubmission* Submission, bool Legacy, bool AllowRejected,
    bool AllowProfile)
{
    const xvalue* IdValue;
    const xvalue* StateValue;
    const xvalue* ProfileValue;
    xstrview Id;
    xstrview State;
    size_t i;
    memset(Submission, 0, sizeof(*Submission));
    ProfileValue = xrtValueObjectGet(Value, XRT_STR_LITERAL("profile"));
    if ( xrtValueType(Value) != XVALUE_OBJECT ||
         xrtValueCount(Value) != (Legacy ? 4u : 5u) +
            (ProfileValue != NULL ? 1u : 0u) ||
         (ProfileValue != NULL && (!AllowProfile ||
          !MdoApiProfileRead(ProfileValue, &Submission->Profile) ||
          !Submission->Profile.Present)) ) return false;
    IdValue = xrtValueObjectGet(Value, XRT_STR_LITERAL("id"));
    if ( xrtValueType(IdValue) != XVALUE_STRING ||
         !xrtValueGetString(IdValue, &Id) || Id.Size != 32u ) return false;
    for ( i = 0u; i < Id.Size; ++i ) {
        unsigned char Byte = (unsigned char)Id.Data[i];
        if ( !((Byte >= '0' && Byte <= '9') ||
               (Byte >= 'a' && Byte <= 'f')) ) return false;
    }
    memcpy(Submission->Id, Id.Data, Id.Size);
    if ( !MdoDraftText(Value, "text", Submission->Text,
            &Submission->TextSize) ||
         !MdoAttachmentIdsRead(xrtValueObjectGet(Value,
            XRT_STR_LITERAL("attachments")), Submission->Attachments,
            &Submission->AttachmentCount) ||
         !MdoDraftBool(Value, "interrupt", &Submission->Interrupt) ||
         (Submission->TextSize == 0u &&
          Submission->AttachmentCount == 0u) ) return false;
    if ( Legacy ) {
        Submission->State = MDO_DRAFT_POSTING;
    } else {
        StateValue = xrtValueObjectGet(Value,
            XRT_STR_LITERAL("state"));
        if ( xrtValueType(StateValue) != XVALUE_STRING ||
             !xrtValueGetString(StateValue, &State) ) return false;
        if ( State.Size == 7u &&
             memcmp(State.Data, "posting", 7u) == 0 )
            Submission->State = MDO_DRAFT_POSTING;
        else if ( State.Size == 8u &&
                  memcmp(State.Data, "prepared", 8u) == 0 )
            Submission->State = MDO_DRAFT_PREPARED;
        else if ( AllowRejected && State.Size == 8u &&
                  memcmp(State.Data, "rejected", 8u) == 0 )
            Submission->State = MDO_DRAFT_REJECTED;
        else
            return false;
    }
    return true;
}

static bool MdoDraftSubmissionsRead(const xvalue* Value,
    MdoDraft* Draft, bool Legacy, bool AllowRejected, bool AllowProfile)
{
    size_t Count;
    size_t Total = 0u;
    size_t i;
    if ( Legacy ) {
        if ( xrtValueType(Value) == XVALUE_NULL ) return true;
        Count = 1u;
    } else {
        if ( xrtValueType(Value) != XVALUE_ARRAY ) return false;
        Count = xrtValueCount(Value);
        if ( Count > MDO_DRAFT_SUBMISSIONS_MAX ) return false;
    }
    for ( i = 0u; i < Count; ++i ) {
        const xvalue* Item = Legacy ? Value : xrtValueArrayGet(Value, i);
        MdoDraftSubmission* Submission =
            (MdoDraftSubmission*)xrtMalloc(sizeof(*Submission));
        size_t j;
        if ( Submission == NULL ) return false;
        if ( !MdoDraftSubmissionRead(Item, Submission, Legacy,
                AllowRejected, AllowProfile) ||
             Submission->TextSize >
                MDO_DRAFT_SUBMISSIONS_TEXT_MAX - Total ) {
            xrtFree(Submission);
            return false;
        }
        for ( j = 0u; j < Draft->SubmissionCount; ++j )
            if ( strcmp(Draft->Submissions[j]->Id, Submission->Id) == 0 ) {
                xrtFree(Submission);
                return false;
            }
        Total += Submission->TextSize;
        Draft->Submissions[Draft->SubmissionCount++] = Submission;
    }
    return true;
}

static xvalue* MdoDraftSubmissionValue(const MdoDraftSubmission* Submission,
    bool Legacy)
{
    xvalue* Value;
    if ( Submission == NULL ) return xrtValueNull();
    Value = xrtValueObject();
    if ( Value != NULL &&
         MdoApiValueSetString(Value, "id", Submission->Id) &&
         MdoApiValueSetStringView(Value, "text",
            xrtStrViewN(Submission->Text, Submission->TextSize)) &&
         MdoAttachmentIdsWriteValue(Value, Submission->Attachments,
            Submission->AttachmentCount) &&
         MdoApiValueSetBool(Value, "interrupt",
            Submission->Interrupt) &&
         MdoApiProfileSet(Value, "profile", &Submission->Profile) &&
         (Legacy || MdoApiValueSetString(Value, "state",
            Submission->State == MDO_DRAFT_POSTING ? "posting" :
            (Submission->State == MDO_DRAFT_REJECTED ? "rejected" :
                "prepared"))) ) return Value;
    xrtValueRelease(Value);
    return NULL;
}

static xvalue* MdoDraftSubmissionsValue(const MdoDraft* Draft)
{
    xvalue* Array = xrtValueArray();
    size_t i;
    bool Ok = Array != NULL;
    for ( i = 0u; Ok && i < Draft->SubmissionCount; ++i ) {
        xvalue* Item = MdoDraftSubmissionValue(Draft->Submissions[i], false);
        Ok = MdoApiValueAppendTake(Array, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) return Array;
    xrtValueRelease(Array);
    return NULL;
}

static bool MdoDraftSubmissionAttachmentsExist(const MdoDraft* Draft,
    const char* Project, const char* Session)
{
    size_t i;
    for ( i = 0u; i < Draft->SubmissionCount; ++i ) {
        const MdoDraftSubmission* Item = Draft->Submissions[i];
        if ( Item->AttachmentCount != 0u &&
             (Project == NULL || Session == NULL ||
              !MdoAttachmentIdsExist(Project, Session,
                Item->Attachments, Item->AttachmentCount)) ) return false;
    }
    return true;
}

static void MdoDraftReplaceSubmissions(MdoDraft* Target, MdoDraft* Source)
{
    size_t i;
    for ( i = 0u; i < Target->SubmissionCount; ++i )
        xrtFree(Target->Submissions[i]);
    memset(Target->Submissions, 0, sizeof(Target->Submissions));
    Target->SubmissionCount = Source->SubmissionCount;
    for ( i = 0u; i < Source->SubmissionCount; ++i ) {
        Target->Submissions[i] = Source->Submissions[i];
        Source->Submissions[i] = NULL;
    }
    Source->SubmissionCount = 0u;
}

static bool MdoDraftRead(const char* Path, MdoDraft* Draft)
{
    bool Global = strcmp(Path, "data/draft.json") == 0;
    bool Exists = false;
    xfileinfo Info;
    xfile File = NULL;
    char* Bytes = NULL;
    xjsonreadconfig Config;
    xvalue* Root = NULL;
    const xvalue* ComposerProfile;
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
    Config.MaxDepth = 6u;
    Config.MaxValues = 256u;
    Config.MaxContainerItems = MDO_DRAFT_SUBMISSIONS_MAX;
    Root = xrtJsonRead(xrtStrViewN(Bytes, (size_t)Info.Size), &Config);
    ComposerProfile = xrtValueObjectGet(Root,
        XRT_STR_LITERAL("composer_profile"));
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         !MdoDraftUInt(Root, "schema_version", &Schema) ||
         (Schema < 1u || Schema > 7u) ||
         xrtValueCount(Root) != (Schema == 1u ? 3u :
            (Schema == 2u ? 4u : (Schema == 3u ? 5u :
                (Schema >= 6u && Global ? 7u : 6u)))) +
                (ComposerProfile != NULL ? 1u : 0u) ||
         (ComposerProfile != NULL && (Schema != 7u ||
          !MdoApiProfileRead(ComposerProfile,
            &Draft->ComposerProfile) ||
          !Draft->ComposerProfile.Present)) ||
         !MdoDraftUInt(Root, "revision", &Draft->Revision) ||
         Draft->Revision == 0u ||
         !MdoDraftText(Root, "text", Draft->Text, &Draft->TextSize) ||
         (Schema >= 2u &&
          !MdoAttachmentIdsRead(xrtValueObjectGet(Root,
                XRT_STR_LITERAL("attachments")), Draft->Attachments,
                &Draft->AttachmentCount)) ||
         (Schema >= 3u && !MdoDraftBool(Root,
            "run_admission_uncertain", &Draft->RunAdmissionUncertain)) ||
         (Schema == 4u && !MdoDraftSubmissionsRead(
            xrtValueObjectGet(Root, XRT_STR_LITERAL("submission")),
            Draft, true, false, false)) ||
         (Schema >= 5u && !MdoDraftSubmissionsRead(
            xrtValueObjectGet(Root, XRT_STR_LITERAL("submissions")),
            Draft, false, Schema >= 6u && !Global, Schema >= 7u)) ||
         (Schema >= 6u && Global && !MdoDraftNewTaskRead(
            xrtValueObjectGet(Root, XRT_STR_LITERAL("new_task")),
            &Draft->NewTask, &Draft->HasNewTask)) ||
         (Draft->HasNewTask && Draft->SubmissionCount != 0u &&
          strcmp(Draft->Submissions[0]->Id,
            Draft->NewTask.SessionId) != 0) )
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
    bool Global = strcmp(Path, "data/draft.json") == 0;
    xvalue* Root = xrtValueObject();
    xvalue* Submissions = MdoDraftSubmissionsValue(Draft);
    xvalue* NewTask = Global ? MdoDraftNewTaskValue(Draft) : NULL;
    char* Json = NULL;
    size_t Size = 0u;
    bool Ok = Root != NULL && Submissions != NULL &&
        (!Global || NewTask != NULL) &&
        MdoApiValueSetUInt(Root, "schema_version", 7u) &&
        MdoApiValueSetUInt(Root, "revision", Draft->Revision) &&
        MdoApiValueSetStringView(Root, "text",
            xrtStrViewN(Draft->Text, Draft->TextSize)) &&
        MdoAttachmentIdsWriteValue(Root, Draft->Attachments,
            Draft->AttachmentCount) &&
        MdoApiValueSetBool(Root, "run_admission_uncertain",
            Draft->RunAdmissionUncertain) &&
        MdoApiProfileSet(Root, "composer_profile",
            &Draft->ComposerProfile) &&
        MdoApiValueSetTake(Root, "submissions", &Submissions) &&
        (!Global || MdoApiValueSetTake(Root, "new_task", &NewTask));
    if ( Ok ) Json = xrtJsonStringify(Root, false, &Size);
    if ( Json != NULL && Size <= MDO_DRAFT_FILE_MAX )
        Ok = MdoHomeAtomicWrite(Path, Json, Size, false);
    else Ok = false;
    xrtFree(Json);
    xrtValueRelease(Submissions);
    xrtValueRelease(NewTask);
    xrtValueRelease(Root);
    return Ok;
}

static xvalue* MdoDraftResponse(const MdoDraft* Draft, bool Global)
{
    xvalue* Data = xrtValueObject();
    xvalue* Submission = MdoDraftSubmissionValue(
        Draft->SubmissionCount ? Draft->Submissions[0] : NULL, true);
    xvalue* Submissions = MdoDraftSubmissionsValue(Draft);
    xvalue* NewTask = MdoDraftNewTaskValue(Draft);
    if ( Data != NULL && Submission != NULL && Submissions != NULL &&
         NewTask != NULL &&
         MdoApiValueSetUInt(Data, "revision", Draft->Revision) &&
         MdoApiValueSetStringView(Data, "text",
            xrtStrViewN(Draft->Text, Draft->TextSize)) &&
         MdoAttachmentIdsWriteValue(Data, Draft->Attachments,
            Draft->AttachmentCount) &&
         MdoApiValueSetBool(Data, "run_admission_uncertain",
            Draft->RunAdmissionUncertain) &&
         MdoApiProfileSet(Data, "composer_profile",
            &Draft->ComposerProfile) &&
         MdoApiValueSetTake(Data, "submission", &Submission) &&
         MdoApiValueSetTake(Data, "submissions", &Submissions) &&
         (!Global || MdoApiValueSetTake(Data, "new_task", &NewTask)) )
        return Data;
    xrtValueRelease(Submission);
    xrtValueRelease(Submissions);
    xrtValueRelease(NewTask);
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
    MdoDraft* Incoming = NULL;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    uint64 ExpectedRevision;
    xstrview IncomingText = { 0 };
    char IncomingAttachments[4][33] = {{ 0 }};
    size_t IncomingCount = 0u;
    bool IncomingUncertain = false;
    bool UncertainPresent = false;
    bool SubmissionPresent = false;
    bool SubmissionsPresent = false;
    bool NewTaskPresent = false;
    bool ComposerProfilePresent = false;
    bool Ok;
    bool Conflict = false;
    bool AttachmentLocked = false;
    int Written;
    xvalue* Data;

    if ( Context->ParamCount == 0u ) {
        memcpy(Path, "data/draft.json", sizeof("data/draft.json"));
    } else if ( Context->ParamCount == 1u &&
         MdoDraftCaptureId(Context->Params[0], ProjectId,
            sizeof(ProjectId)) ) {
        Written = snprintf(Path, sizeof(Path),
            "data/project-drafts/%s.json", ProjectId);
        if ( Written <= 0 || (size_t)Written >= sizeof(Path) )
            return MdoApiReplyError(Context, 400u, "invalid_path",
                "The project identifier is invalid", NULL);
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
    memset(Draft, 0, sizeof(*Draft));
    ExpectedRevision = 0u;
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_PUT ) {
        Incoming = (MdoDraft*)xrtMalloc(sizeof(*Incoming));
        if ( Incoming == NULL ) {
            MdoDraftRelease(Draft);
            return MdoApiReplyError(Context, 503u, "draft_unavailable",
                "The draft could not be allocated", NULL);
        }
        memset(Incoming, 0, sizeof(*Incoming));
        BodyStatus = MdoApiJsonBodyRead(Context, &Body);
        if ( BodyStatus != MDO_API_BODY_OK ) {
            MdoDraftRelease(Incoming);
            MdoDraftRelease(Draft);
            return MdoApiReplyBodyError(Context, BodyStatus);
        }
        if ( Context->ParamCount == 2u ) {
            AttachmentLocked = MdoApiAttachmentLock();
            if ( !AttachmentLocked ) {
                MdoApiJsonBodyUnit(&Body);
                MdoDraftRelease(Incoming);
                MdoDraftRelease(Draft);
                return MdoApiReplyError(Context, 503u,
                    "attachment_unavailable",
                    "Image storage is unavailable", NULL);
            }
        }
        const xvalue* Attachments = xrtValueObjectGet(Body.Value,
            XRT_STR_LITERAL("attachments"));
        const xvalue* Uncertain = xrtValueObjectGet(Body.Value,
            XRT_STR_LITERAL("run_admission_uncertain"));
        const xvalue* Submission = xrtValueObjectGet(Body.Value,
            XRT_STR_LITERAL("submission"));
        const xvalue* Submissions = xrtValueObjectGet(Body.Value,
            XRT_STR_LITERAL("submissions"));
        const xvalue* NewTask = xrtValueObjectGet(Body.Value,
            XRT_STR_LITERAL("new_task"));
        const xvalue* ComposerProfile = xrtValueObjectGet(Body.Value,
            XRT_STR_LITERAL("composer_profile"));
        UncertainPresent = Uncertain != NULL;
        SubmissionPresent = Submission != NULL;
        SubmissionsPresent = Submissions != NULL;
        NewTaskPresent = NewTask != NULL;
        ComposerProfilePresent = ComposerProfile != NULL;
        Ok = xrtValueType(Body.Value) == XVALUE_OBJECT &&
            xrtValueCount(Body.Value) == (Attachments == NULL ? 2u : 3u) +
                (UncertainPresent ? 1u : 0u) +
                (SubmissionPresent ? 1u : 0u) +
                (SubmissionsPresent ? 1u : 0u) +
                (NewTaskPresent ? 1u : 0u) +
                (ComposerProfilePresent ? 1u : 0u) &&
            !(SubmissionPresent && SubmissionsPresent) &&
            (!NewTaskPresent || Context->ParamCount == 0u) &&
            MdoDraftUInt(Body.Value, "revision", &ExpectedRevision) &&
            MdoDraftTextView(Body.Value, "text", &IncomingText) &&
            (!ComposerProfilePresent || MdoApiProfileRead(
                ComposerProfile, &Incoming->ComposerProfile)) &&
            (!UncertainPresent || MdoDraftBool(Body.Value,
                "run_admission_uncertain", &IncomingUncertain)) &&
            (!SubmissionPresent || MdoDraftSubmissionsRead(Submission,
                Incoming, true, false, true)) &&
            (!SubmissionsPresent || MdoDraftSubmissionsRead(Submissions,
                Incoming, false, Context->ParamCount == 2u, true)) &&
            (!NewTaskPresent || MdoDraftNewTaskRead(NewTask,
                &Incoming->NewTask, &Incoming->HasNewTask)) &&
            (!Incoming->HasNewTask || !SubmissionsPresent ||
             Incoming->SubmissionCount == 0u ||
             strcmp(Incoming->Submissions[0]->Id,
                Incoming->NewTask.SessionId) == 0) &&
            MdoDraftSubmissionAttachmentsExist(Incoming,
                Context->ParamCount == 2u ? ProjectId : NULL,
                Context->ParamCount == 2u ? SessionId : NULL) &&
            (Attachments == NULL ||
             MdoAttachmentIdsRead(Attachments, IncomingAttachments,
                &IncomingCount)) &&
            (IncomingCount == 0u ||
             (Context->ParamCount == 2u &&
              MdoAttachmentIdsExist(ProjectId, SessionId,
                IncomingAttachments, IncomingCount))) &&
            (Context->ParamCount != 1u ||
             (Incoming->SubmissionCount == 0u && !Incoming->HasNewTask &&
              !IncomingUncertain && IncomingCount == 0u));
        if ( !Ok ) {
            if ( AttachmentLocked ) MdoApiAttachmentUnlock();
            MdoApiJsonBodyUnit(&Body);
            MdoDraftRelease(Incoming);
            MdoDraftRelease(Draft);
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
            if ( ComposerProfilePresent )
                Draft->ComposerProfile = Incoming->ComposerProfile;
            if ( SubmissionPresent || SubmissionsPresent )
                MdoDraftReplaceSubmissions(Draft, Incoming);
            if ( NewTaskPresent ) {
                Draft->NewTask = Incoming->NewTask;
                Draft->HasNewTask = Incoming->HasNewTask;
            }
            if ( Draft->HasNewTask && Draft->SubmissionCount != 0u &&
                 strcmp(Draft->Submissions[0]->Id,
                    Draft->NewTask.SessionId) != 0 ) Ok = false;
            if ( Ok ) Ok = MdoDraftWrite(Path, Draft);
        }
    }
    Data = Ok && !Conflict ? MdoDraftResponse(Draft,
        Context->ParamCount == 0u) : NULL;
    xrtMutexUnlock(g_MdoDraftLock);
    if ( AttachmentLocked ) MdoApiAttachmentUnlock();
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_PUT )
        MdoApiJsonBodyUnit(&Body);
    MdoDraftRelease(Incoming);
    MdoDraftRelease(Draft);
    if ( Conflict ) return MdoApiReplyError(Context, 409u,
        "draft_conflict", "The draft changed in another window", NULL);
    if ( Data == NULL ) return MdoApiReplyError(Context, 503u,
        "draft_unavailable", "The draft could not be read or saved", NULL);
    if ( Context->ParamCount == 2u &&
         Context->Request->head->MethodCode == XHTTP_METHOD_GET ) {
        /* A damaged orphan is left for inspection. Either way, cleanup
         * cannot turn a valid draft read into an error response. */
        (void)MdoApiAttachmentSweepExpired(ProjectId, SessionId);
        xrtClearError();
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

/* Append an intent under the same lock as draft PUT. A stale tab cannot
 * replace another tab's submissions, and a repeated ID is safe to retry. */
bool MdoApiDraftSubmissionAppendRoute(MdoApiContext* Context)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    char ProjectId[MDO_PROJECT_ID_CAPACITY] = { 0 };
    char SessionId[MDO_SESSION_ID_CAPACITY] = { 0 };
    MdoSession* Session;
    MdoDraft* Draft;
    MdoDraftSubmission* Incoming;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    xwork_error Error;
    xvalue* Data = NULL;
    size_t i;
    size_t Total = 0u;
    int Written;
    bool Ok;
    bool AttachmentLocked;
    bool Duplicate = false;
    bool Collision = false;
    bool Full = false;
    bool Added = false;

    if ( Context->ParamCount != 2u ||
         !MdoDraftCaptureId(Context->Params[0], ProjectId,
            sizeof(ProjectId)) ||
         !MdoDraftCaptureId(Context->Params[1], SessionId,
            sizeof(SessionId)) )
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "Project and session identifiers are invalid", NULL);
    Written = snprintf(Path, sizeof(Path), "sessions/%s/%s/draft.json",
        ProjectId, SessionId);
    if ( Written <= 0 || (size_t)Written >= sizeof(Path) )
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "Project and session identifiers are invalid", NULL);
    memset(&Error, 0, sizeof(Error));
    Session = MdoSessionLoad(ProjectId, SessionId, &Error);
    if ( Session == NULL ) return MdoApiReplyError(Context, 404u,
        "session_not_found", "The requested session does not exist", NULL);
    MdoSessionRelease(Session);
    Draft = (MdoDraft*)xrtMalloc(sizeof(*Draft));
    Incoming = (MdoDraftSubmission*)xrtMalloc(sizeof(*Incoming));
    if ( Draft == NULL || Incoming == NULL ) {
        xrtFree(Draft);
        xrtFree(Incoming);
        return MdoApiReplyError(Context, 503u, "draft_unavailable",
            "The draft could not be allocated", NULL);
    }
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK ) {
        xrtFree(Incoming);
        xrtFree(Draft);
        return MdoApiReplyBodyError(Context, BodyStatus);
    }
    Ok = MdoDraftSubmissionRead(Body.Value, Incoming, false, false,
        true) &&
        Incoming->State == MDO_DRAFT_PREPARED;
    if ( !Ok ) {
        MdoApiJsonBodyUnit(&Body);
        xrtFree(Incoming);
        xrtFree(Draft);
        return MdoApiReplyError(Context, 422u, "draft_submission_invalid",
            "Expected a prepared submission with bounded text", NULL);
    }
    AttachmentLocked = MdoApiAttachmentLock();
    if ( !AttachmentLocked ) {
        MdoApiJsonBodyUnit(&Body);
        xrtFree(Incoming);
        xrtFree(Draft);
        return MdoApiReplyError(Context, 503u, "attachment_unavailable",
            "Image storage is unavailable", NULL);
    }
    if ( !MdoAttachmentIdsExist(ProjectId, SessionId,
            Incoming->Attachments, Incoming->AttachmentCount) ) {
        MdoApiAttachmentUnlock();
        MdoApiJsonBodyUnit(&Body);
        xrtFree(Incoming);
        xrtFree(Draft);
        return MdoApiReplyError(Context, 422u, "draft_submission_invalid",
            "The submission references an unavailable image", NULL);
    }
    xrtMutexLock(g_MdoDraftLock);
    Ok = MdoDraftRead(Path, Draft);
    if ( Ok ) {
        for ( i = 0u; i < Draft->SubmissionCount; ++i ) {
            const MdoDraftSubmission* Existing = Draft->Submissions[i];
            if ( strcmp(Existing->Id, Incoming->Id) != 0 ) {
                Total += Existing->TextSize;
                continue;
            }
            Duplicate = true;
            Collision = Existing->TextSize != Incoming->TextSize ||
                (Incoming->TextSize != 0u &&
                 memcmp(Existing->Text, Incoming->Text,
                    Incoming->TextSize) != 0) ||
                Existing->AttachmentCount != Incoming->AttachmentCount ||
                memcmp(Existing->Attachments, Incoming->Attachments,
                    sizeof(Incoming->Attachments)) != 0 ||
                Existing->Interrupt != Incoming->Interrupt ||
                !MdoApiProfileEqual(&Existing->Profile,
                    &Incoming->Profile);
            break;
        }
        if ( !Duplicate ) {
            Full = Draft->SubmissionCount >= MDO_DRAFT_SUBMISSIONS_MAX ||
                Incoming->TextSize >
                    MDO_DRAFT_SUBMISSIONS_TEXT_MAX - Total ||
                Draft->Revision == UINT64_MAX;
            if ( !Full ) {
                Draft->Submissions[Draft->SubmissionCount++] = Incoming;
                Incoming = NULL;
                Draft->Revision++;
                Ok = MdoDraftWrite(Path, Draft);
                Added = Ok;
            }
        }
        if ( Ok && !Full && !Collision ) Data = MdoDraftResponse(Draft,
            false);
    }
    xrtMutexUnlock(g_MdoDraftLock);
    MdoApiAttachmentUnlock();
    MdoApiJsonBodyUnit(&Body);
    xrtFree(Incoming);
    MdoDraftRelease(Draft);
    if ( Collision ) return MdoApiReplyError(Context, 409u,
        "draft_submission_conflict",
        "The submission ID already has different content", NULL);
    if ( Full ) return MdoApiReplyError(Context, 422u,
        "draft_full", "The draft has reached its submission limit", NULL);
    if ( !Ok || Data == NULL ) return MdoApiReplyError(Context, 503u,
        "draft_unavailable", "The draft could not be read or saved", NULL);
    return MdoApiReplySuccessTake(Context, Added ? 201u : 200u, Data, NULL);
}

/* Keyed transitions avoid rewriting a stale copy of the whole intent list. */
bool MdoApiDraftSubmissionRoute(MdoApiContext* Context)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    char ProjectId[MDO_PROJECT_ID_CAPACITY] = { 0 };
    char SessionId[MDO_SESSION_ID_CAPACITY] = { 0 };
    char Id[33] = { 0 };
    MdoSession* Session;
    MdoDraft* Draft;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    xwork_error Error;
    xvalue* Data = NULL;
    xstrview StateText = { 0 };
    MdoDraftSubmissionState State = MDO_DRAFT_PREPARED;
    bool Change = Context->Request->head->MethodCode == XHTTP_METHOD_PUT;
    bool Ok;
    bool Missing = false;
    bool InvalidTransition = false;
    bool Modified = false;
    size_t Index;
    int Written;

    if ( Context->ParamCount != 3u ||
         !MdoDraftCaptureId(Context->Params[0], ProjectId,
            sizeof(ProjectId)) ||
         !MdoDraftCaptureId(Context->Params[1], SessionId,
            sizeof(SessionId)) ||
         Context->Params[2].Size != 32u )
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "Project, session or submission ID is invalid", NULL);
    for ( Index = 0u; Index < 32u; ++Index ) {
        unsigned char Byte = (unsigned char)Context->Params[2].Data[Index];
        if ( !((Byte >= '0' && Byte <= '9') ||
               (Byte >= 'a' && Byte <= 'f')) )
            return MdoApiReplyError(Context, 400u, "invalid_path",
                "Submission ID is invalid", NULL);
    }
    memcpy(Id, Context->Params[2].Data, 32u);
    Written = snprintf(Path, sizeof(Path), "sessions/%s/%s/draft.json",
        ProjectId, SessionId);
    if ( Written <= 0 || (size_t)Written >= sizeof(Path) )
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "Project and session identifiers are invalid", NULL);
    memset(&Error, 0, sizeof(Error));
    Session = MdoSessionLoad(ProjectId, SessionId, &Error);
    if ( Session == NULL ) return MdoApiReplyError(Context, 404u,
        "session_not_found", "The requested session does not exist", NULL);
    MdoSessionRelease(Session);
    if ( Change ) {
        BodyStatus = MdoApiJsonBodyRead(Context, &Body);
        if ( BodyStatus != MDO_API_BODY_OK )
            return MdoApiReplyBodyError(Context, BodyStatus);
        const xvalue* Value = xrtValueObjectGet(Body.Value,
            XRT_STR_LITERAL("state"));
        Ok = xrtValueType(Body.Value) == XVALUE_OBJECT &&
            xrtValueCount(Body.Value) == 1u &&
            xrtValueType(Value) == XVALUE_STRING &&
            xrtValueGetString(Value, &StateText);
        if ( Ok && StateText.Size == 8u &&
             memcmp(StateText.Data, "prepared", 8u) == 0 )
            State = MDO_DRAFT_PREPARED;
        else if ( Ok && StateText.Size == 7u &&
                  memcmp(StateText.Data, "posting", 7u) == 0 )
            State = MDO_DRAFT_POSTING;
        else if ( Ok && StateText.Size == 8u &&
                  memcmp(StateText.Data, "rejected", 8u) == 0 )
            State = MDO_DRAFT_REJECTED;
        else Ok = false;
        MdoApiJsonBodyUnit(&Body);
        if ( !Ok ) return MdoApiReplyError(Context, 422u,
            "draft_state_invalid", "Expected a valid submission state", NULL);
    }
    Draft = (MdoDraft*)xrtMalloc(sizeof(*Draft));
    if ( Draft == NULL ) return MdoApiReplyError(Context, 503u,
        "draft_unavailable", "The draft could not be allocated", NULL);
    xrtMutexLock(g_MdoDraftLock);
    Ok = MdoDraftRead(Path, Draft);
    if ( Ok ) {
        for ( Index = 0u; Index < Draft->SubmissionCount; ++Index )
            if ( strcmp(Draft->Submissions[Index]->Id, Id) == 0 ) break;
        Missing = Index == Draft->SubmissionCount;
        if ( Change && !Missing ) {
            MdoDraftSubmissionState Previous =
                Draft->Submissions[Index]->State;
            InvalidTransition = Previous != State &&
                !((Previous == MDO_DRAFT_PREPARED &&
                   State == MDO_DRAFT_POSTING) ||
                  (Previous == MDO_DRAFT_POSTING &&
                   (State == MDO_DRAFT_REJECTED ||
                    State == MDO_DRAFT_PREPARED)) ||
                  (Previous == MDO_DRAFT_REJECTED &&
                   State == MDO_DRAFT_PREPARED));
            if ( !InvalidTransition && Previous != State ) {
                Draft->Submissions[Index]->State = State;
                Modified = true;
            }
        } else if ( !Change && !Missing ) {
            const MdoDraftSubmission* Removed = Draft->Submissions[Index];
            if ( Draft->TextSize == Removed->TextSize &&
                 memcmp(Draft->Text, Removed->Text,
                    Removed->TextSize) == 0 &&
                 Draft->AttachmentCount == Removed->AttachmentCount &&
                 memcmp(Draft->Attachments, Removed->Attachments,
                    sizeof(Draft->Attachments)) == 0 ) {
                Draft->Text[0] = '\0';
                Draft->TextSize = 0u;
                memset(Draft->Attachments, 0, sizeof(Draft->Attachments));
                Draft->AttachmentCount = 0u;
            }
            xrtFree(Draft->Submissions[Index]);
            for ( ; Index + 1u < Draft->SubmissionCount; ++Index )
                Draft->Submissions[Index] = Draft->Submissions[Index + 1u];
            Draft->Submissions[--Draft->SubmissionCount] = NULL;
            Modified = true;
        }
        if ( Modified ) {
            if ( Draft->Revision == UINT64_MAX ) Ok = false;
            else {
                Draft->Revision++;
                Ok = MdoDraftWrite(Path, Draft);
            }
        }
        if ( Ok && !InvalidTransition && (!Change || !Missing) )
            Data = MdoDraftResponse(Draft, false);
    }
    xrtMutexUnlock(g_MdoDraftLock);
    MdoDraftRelease(Draft);
    if ( Missing && Change ) return MdoApiReplyError(Context, 404u,
        "draft_submission_not_found",
        "The submission no longer exists", NULL);
    if ( InvalidTransition ) return MdoApiReplyError(Context, 409u,
        "draft_state_conflict",
        "The submission state changed in another window", NULL);
    if ( !Ok || Data == NULL ) return MdoApiReplyError(Context, 503u,
        "draft_unavailable", "The draft could not be read or saved", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
