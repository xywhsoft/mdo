#include <string.h>
#include "draft.h"
#include "binding.h"

void MdoDraftUnit(MdoDraft* Draft)
{
    size_t i;
    if ( Draft == NULL ) return;
    for ( i = 0u; i < Draft->SubmissionCount; ++i ) xrtFree(Draft->Submissions[i]);
    memset(Draft, 0, sizeof(*Draft));
}

bool MdoDraftCaptureId(xstrview View, char* Output,
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

bool MdoDraftUInt(const xvalue* Object, cstr Name, uint64* Output)
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

bool MdoDraftBool(const xvalue* Object, cstr Name, bool* Output)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Name));
    return xrtValueType(Value) == XVALUE_BOOL &&
        xrtValueGetBool(Value, Output);
}

bool MdoDraftTextView(const xvalue* Object, cstr Name,
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

bool MdoDraftProjectProfileRead(const xvalue* Value, MdoComposerProfile* Profile)
{
    return MdoComposerProfileParse(Value, true, Profile);
}

bool MdoDraftNewTaskRead(const xvalue* Value,
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
         memcmp(PhaseText.Data, "copying", 7u) == 0 )
        Task->Phase = MDO_DRAFT_TASK_COPYING;
    else if ( PhaseText.Size == 8u &&
              memcmp(PhaseText.Data, "rejected", 8u) == 0 )
        Task->Phase = MDO_DRAFT_TASK_REJECTED;
    else if ( PhaseText.Size != 8u ||
              memcmp(PhaseText.Data, "creating", 8u) != 0 ) return false;
    *Present = true;
    return true;
}

bool MdoDraftSubmissionRead(const xvalue* Value,
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
          !MdoComposerProfileParse(ProfileValue, false, &Submission->Profile) ||
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
         !MdoImageIdsRead(xrtValueObjectGet(Value,
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

bool MdoDraftSubmissionsRead(const xvalue* Value,
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

bool MdoDraftParse(xstrview Json, MdoDraftScope Scope, MdoDraft* Draft)
{
    bool Global = Scope == MDO_DRAFT_GLOBAL;
    bool Project = Scope == MDO_DRAFT_PROJECT;
    xjsonreadconfig Config;
    xvalue* Root = NULL;
    const xvalue* ComposerProfile;
    uint64 Schema;
    bool Ok = false;
    if ( Draft == NULL ) return false;
    memset(Draft, 0, sizeof(*Draft));
    if ( Scope != MDO_DRAFT_SESSION && Scope != MDO_DRAFT_GLOBAL && Scope != MDO_DRAFT_PROJECT ) return false;
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_DRAFT_FILE_MAX;
    Config.MaxDepth = 6u;
    Config.MaxValues = 256u;
    Config.MaxContainerItems = MDO_DRAFT_SUBMISSIONS_MAX;
    Root = xrtJsonRead(Json, &Config);
    ComposerProfile = xrtValueObjectGet(Root,
        XRT_STR_LITERAL("composer_profile"));
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         !MdoDraftUInt(Root, "schema_version", &Schema) ||
         (Schema < 1u || Schema > 8u || (Schema == 8u && !Project)) ||
         xrtValueCount(Root) != (Schema == 1u ? 3u :
            (Schema == 2u ? 4u : (Schema == 3u ? 5u :
                (Schema >= 6u && Global ? 7u : 6u)))) +
                (ComposerProfile != NULL ? 1u : 0u) ||
         (ComposerProfile != NULL && (Schema < 7u ||
          !(Schema == 8u ? MdoDraftProjectProfileRead(ComposerProfile,
              &Draft->ComposerProfile) : MdoComposerProfileParse(ComposerProfile, false,
              &Draft->ComposerProfile)) ||
          !Draft->ComposerProfile.Present)) ||
         !MdoDraftUInt(Root, "revision", &Draft->Revision) ||
         Draft->Revision == 0u ||
         !MdoDraftText(Root, "text", Draft->Text, &Draft->TextSize) ||
         (Schema >= 2u &&
          !MdoImageIdsRead(xrtValueObjectGet(Root,
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
    if ( !Ok ) MdoDraftUnit(Draft);
    return Ok;
}
