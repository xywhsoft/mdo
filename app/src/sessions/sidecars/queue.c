#include <string.h>
#include "queue.h"
#include "binding.h"

void MdoQueueRelease(MdoQueue* Queue)
{
    size_t i;
    if ( Queue == NULL ) return;
    for ( i = 0u; i < Queue->Count; ++i ) xrtFree(Queue->Items[i].Text);
    memset(Queue, 0, sizeof(*Queue));
}

bool MdoQueueCaptureId(xstrview View, char* Output,
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

bool MdoQueueId(xstrview View,
    char Output[MDO_QUEUE_ID_SIZE + 1u])
{
    size_t i;
    if ( View.Size != MDO_QUEUE_ID_SIZE ) return false;
    for ( i = 0u; i < View.Size; ++i ) {
        unsigned char Byte = (unsigned char)View.Data[i];
        if ( !((Byte >= '0' && Byte <= '9') ||
               (Byte >= 'a' && Byte <= 'f')) ) return false;
    }
    memcpy(Output, View.Data, View.Size);
    Output[View.Size] = '\0';
    return true;
}

size_t MdoQueueDiscardFind(const MdoQueue* Queue, const char* Id)
{
    size_t i;
    for ( i = 0u; i < Queue->DiscardCount; ++i )
        if ( strcmp(Queue->DiscardImages[i], Id) == 0 ) return i;
    return SIZE_MAX;
}

bool MdoQueueDiscardAdd(MdoQueue* Queue, const char* Id)
{
    if ( MdoQueueDiscardFind(Queue, Id) != SIZE_MAX ) return true;
    if ( Queue->DiscardCount == MDO_QUEUE_DISCARD_MAX ) return false;
    memcpy(Queue->DiscardImages[Queue->DiscardCount++], Id, 33u);
    return true;
}

bool MdoQueueRunId(xstrview View, char Output[MDO_RUN_ID_CAPACITY])
{
    return View.Size > 4u && View.Size < MDO_RUN_ID_CAPACITY &&
        memcmp(View.Data, "run-", 4u) == 0 &&
        MdoQueueCaptureId(View, Output, MDO_RUN_ID_CAPACITY);
}

bool MdoQueueString(const xvalue* Object, cstr Key,
    xstrview* Text)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    return xrtValueType(Value) == XVALUE_STRING &&
        xrtValueGetString(Value, Text);
}

bool MdoQueueText(xstrview Text, bool AllowEmpty)
{
    return (AllowEmpty || Text.Size > 0u) &&
        Text.Size <= MDO_QUEUE_MAX_TEXT &&
        (Text.Size == 0u || memchr(Text.Data, 0, Text.Size) == NULL) &&
        xrtUtf8Valid(Text, NULL);
}

bool MdoQueueBool(const xvalue* Object, cstr Key, bool* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    return xrtValueType(Value) == XVALUE_BOOL &&
        xrtValueGetBool(Value, Result);
}

bool MdoQueueParseState(xstrview State, bool AllowStaged,
    MdoQueueState* Result)
{
    if ( AllowStaged && State.Size == 6u &&
         memcmp(State.Data, "staged", 6u) == 0 ) {
        *Result = MDO_QUEUE_STAGED;
        return true;
    }
    if ( State.Size == 7u && memcmp(State.Data, "pending", 7u) == 0 ) {
        *Result = MDO_QUEUE_PENDING;
        return true;
    }
    if ( State.Size == 7u && memcmp(State.Data, "sending", 7u) == 0 ) {
        *Result = MDO_QUEUE_SENDING;
        return true;
    }
    return false;
}

size_t MdoQueueFind(const MdoQueue* Queue, const char* Id)
{
    size_t i;
    for ( i = 0u; i < Queue->Count; ++i )
        if ( strcmp(Queue->Items[i].Id, Id) == 0 ) return i;
    return SIZE_MAX;
}

bool MdoQueueInsert(MdoQueue* Queue, const char* Id,
    xstrview Text, const char Attachments[4][33],
    size_t AttachmentCount, bool First, bool Priority,
    MdoQueueState State, const MdoComposerProfile* Profile)
{
    MdoQueueItem* Item;
    char* Copy;
    size_t Position = First && Queue->Count != 0u &&
        Queue->Items[0].State == MDO_QUEUE_SENDING ? 1u :
        (First ? 0u : Queue->Count);
    if ( Queue->Count >= MDO_QUEUE_MAX_ITEMS ||
         Text.Size > MDO_QUEUE_MAX_TOTAL_TEXT - Queue->TextBytes )
        return false;
    Copy = (char*)xrtMalloc(Text.Size + 1u);
    if ( Copy == NULL ) return false;
    if ( Text.Size != 0u ) memcpy(Copy, Text.Data, Text.Size);
    Copy[Text.Size] = '\0';
    if ( Position < Queue->Count )
        memmove(&Queue->Items[Position + 1u], &Queue->Items[Position],
            (Queue->Count - Position) * sizeof(Queue->Items[0]));
    Item = &Queue->Items[Position];
    memset(Item, 0, sizeof(*Item));
    memcpy(Item->Id, Id, MDO_QUEUE_ID_SIZE + 1u);
    Item->Text = Copy;
    Item->TextSize = Text.Size;
    memcpy(Item->Attachments, Attachments, sizeof(Item->Attachments));
    Item->AttachmentCount = AttachmentCount;
    Item->Priority = Priority;
    Item->State = State;
    if ( Profile != NULL ) Item->Profile = *Profile;
    Queue->TextBytes += Text.Size;
    Queue->Count++;
    return true;
}

bool MdoQueueParse(xstrview Json, MdoQueue* Queue)
{
    xvalue* Root = NULL;
    const xvalue* Items;
    xjsonreadconfig Config;
    uint64 Schema;
    size_t i;
    bool Ok = false;
    if ( Queue == NULL ) return false;
    memset(Queue, 0, sizeof(*Queue));
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_QUEUE_FILE_MAX;
    Config.MaxDepth = 5u;
    Config.MaxValues = MDO_QUEUE_DISCARD_MAX + MDO_QUEUE_MAX_ITEMS * 16u + 5u;
    Config.MaxContainerItems = MDO_QUEUE_DISCARD_MAX;
    Root = xrtJsonRead(Json, &Config);
    Items = Root != NULL ? xrtValueObjectGet(Root,
        XRT_STR_LITERAL("items")) : NULL;
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         (xrtValueCount(Root) != 2u && xrtValueCount(Root) != 3u) ||
         xrtValueType(Items) != XVALUE_ARRAY ||
         xrtValueCount(Items) > MDO_QUEUE_MAX_ITEMS ) goto done;
    {
        const xvalue* Version = xrtValueObjectGet(Root,
            XRT_STR_LITERAL("schema_version"));
        int64 Signed;
        if ( xrtValueType(Version) == XVALUE_UINT ) {
            if ( !xrtValueGetUInt(Version, &Schema) ) goto done;
        } else if ( xrtValueType(Version) == XVALUE_INT ) {
            if ( !xrtValueGetInt(Version, &Signed) || Signed < 0 ) goto done;
            Schema = (uint64)Signed;
        } else goto done;
        if ( Schema < 1u || Schema > 7u ) goto done;
    }
    if ( xrtValueCount(Root) != (Schema >= 6u ? 3u : 2u) ) goto done;
    if ( Schema >= 6u ) {
        const xvalue* Discard = xrtValueObjectGet(Root,
            XRT_STR_LITERAL("discard_images"));
        if ( xrtValueType(Discard) != XVALUE_ARRAY ||
             xrtValueCount(Discard) > MDO_QUEUE_DISCARD_MAX ) goto done;
        for ( i = 0u; i < xrtValueCount(Discard); ++i ) {
            xstrview Id;
            char Checked[33];
            const xvalue* Value = xrtValueArrayGet(Discard, i);
            if ( xrtValueType(Value) != XVALUE_STRING ||
                 !xrtValueGetString(Value, &Id) ||
                 !MdoQueueId(Id, Checked) ||
                 MdoQueueDiscardFind(Queue, Checked) != SIZE_MAX ||
                 !MdoQueueDiscardAdd(Queue, Checked) ) goto done;
        }
    }
    for ( i = 0u; i < xrtValueCount(Items); ++i ) {
        const xvalue* Entry = xrtValueArrayGet(Items, i);
        xstrview Id;
        xstrview Text;
        xstrview StateView;
        xstrview RunIdView = { 0 };
        char RunId[MDO_RUN_ID_CAPACITY] = { 0 };
        const xvalue* RunIdValue = xrtValueObjectGet(Entry,
            XRT_STR_LITERAL("run_id"));
        const xvalue* ProfileValue = xrtValueObjectGet(Entry,
            XRT_STR_LITERAL("profile"));
        MdoComposerProfile Profile = { 0 };
        char IdText[MDO_QUEUE_ID_SIZE + 1u];
        char Attachments[4][33] = {{ 0 }};
        size_t AttachmentCount = 0u;
        MdoQueueState State;
        bool Priority = false;
        if ( xrtValueType(Entry) != XVALUE_OBJECT ||
             xrtValueCount(Entry) != (Schema == 1u ? 3u :
                (Schema == 2u ? 4u : 5u +
                    (Schema >= 5u && RunIdValue != NULL ? 1u : 0u))) +
                    (ProfileValue != NULL ? 1u : 0u) ||
             (ProfileValue != NULL && (Schema != 7u ||
              !MdoComposerProfileParse(ProfileValue, false, &Profile) ||
              !Profile.Present)) ||
             !MdoQueueString(Entry, "id", &Id) ||
             !MdoQueueString(Entry, "text", &Text) ||
             !MdoQueueString(Entry, "state", &StateView) ||
             !MdoQueueId(Id, IdText) ||
             (Schema >= 2u &&
              !MdoImageIdsRead(xrtValueObjectGet(Entry,
                XRT_STR_LITERAL("attachments")), Attachments,
                &AttachmentCount)) ||
             (Schema >= 3u &&
              !MdoQueueBool(Entry, "priority", &Priority)) ||
             !MdoQueueText(Text, AttachmentCount != 0u) ||
             !MdoQueueParseState(StateView, Schema >= 4u, &State) ||
             (RunIdValue != NULL &&
              (Schema < 5u || State != MDO_QUEUE_SENDING ||
               xrtValueType(RunIdValue) != XVALUE_STRING ||
               !xrtValueGetString(RunIdValue, &RunIdView) ||
               !MdoQueueRunId(RunIdView, RunId))) ||
             MdoQueueFind(Queue, IdText) != SIZE_MAX ||
             !MdoQueueInsert(Queue, IdText, Text, Attachments,
                AttachmentCount, false, Priority, State, &Profile) ) goto done;
        if ( RunIdValue != NULL )
            memcpy(Queue->Items[Queue->Count - 1u].RunId, RunId, sizeof(RunId));
    }
    Ok = true;
done:
    xrtValueRelease(Root);
    if ( !Ok ) MdoQueueRelease(Queue);
    return Ok;
}

bool MdoQueueReceiptParse(xstrview Json, const char* ExpectedId, MdoQueueReceipt* Receipt)
{
    xjsonreadconfig Config;
    xvalue* Root = NULL;
    uint64 Schema = 0u, PreparedAgentRunId = 0u;
    int64 Signed;
    const xvalue* Version;
    const xvalue* AgentRun;
    xstrview StoredId, StoredRun, StoredState;
    bool Ok = false;
    if ( ExpectedId == NULL || Receipt == NULL ) return false;
    memset(Receipt, 0, sizeof(*Receipt));
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_QUEUE_RECEIPT_FILE_MAX;
    Config.MaxDepth = 2u;
    Config.MaxValues = 8u;
    Root = xrtJsonRead(Json, &Config);
    Version = xrtValueObjectGet(Root, XRT_STR_LITERAL("schema_version"));
    if ( xrtValueType(Version) == XVALUE_UINT ) {
        if ( !xrtValueGetUInt(Version, &Schema) ) goto done;
    } else if ( xrtValueType(Version) == XVALUE_INT ) {
        if ( !xrtValueGetInt(Version, &Signed) || Signed < 0 ) goto done;
        Schema = (uint64)Signed;
    } else goto done;
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         xrtValueCount(Root) != (Schema == 3u ? 5u : 3u) ||
         (Schema != 1u && Schema != 2u && Schema != 3u) ||
         !MdoQueueString(Root, "id", &StoredId) ||
         StoredId.Size != MDO_QUEUE_ID_SIZE || strlen(ExpectedId) != MDO_QUEUE_ID_SIZE ||
         memcmp(StoredId.Data, ExpectedId, MDO_QUEUE_ID_SIZE) != 0 ) goto done;
    if ( Schema == 1u ) {
        if ( !MdoQueueString(Root, "run_id", &StoredRun) ||
             !MdoQueueRunId(StoredRun, Receipt->RunId) ) goto done;
    } else {
        if ( !MdoQueueString(Root, "state", &StoredState) ||
             StoredState.Size != 8u ||
             memcmp(StoredState.Data, "starting", 8u) != 0 ) goto done;
        if ( Schema == 3u ) {
            char CheckedRun[MDO_RUN_ID_CAPACITY];
            if ( !MdoQueueString(Root, "run_id", &StoredRun) ||
                 !MdoQueueRunId(StoredRun, CheckedRun) ) goto done;
            AgentRun = xrtValueObjectGet(Root,
                XRT_STR_LITERAL("agent_run_id"));
            if ( xrtValueType(AgentRun) == XVALUE_UINT ) {
                if ( !xrtValueGetUInt(AgentRun,
                        &PreparedAgentRunId) ) goto done;
            } else if ( xrtValueType(AgentRun) == XVALUE_INT ) {
                if ( !xrtValueGetInt(AgentRun, &Signed) ||
                     Signed <= 0 ) goto done;
                PreparedAgentRunId = (uint64)Signed;
            } else goto done;
            if ( PreparedAgentRunId == 0u ) goto done;
            memcpy(Receipt->RunId, CheckedRun, sizeof(CheckedRun));
            Receipt->AgentRunId = PreparedAgentRunId;
        }
    }
    Ok = MdoQueueId(StoredId, Receipt->Id);
    if ( Ok ) Receipt->Schema = (uint32)Schema;
done:
    xrtValueRelease(Root);
    if ( !Ok ) memset(Receipt, 0, sizeof(*Receipt));
    return Ok;
}

bool MdoQueueReceiptApply(MdoQueueItem* Item, bool Exists, const char* RunId)
{
    if ( Item == NULL || RunId == NULL || (Exists && Item->State != MDO_QUEUE_SENDING) ||
         (Item->RunId[0] != '\0' && Exists && strcmp(Item->RunId, RunId) != 0) ) return false;
    if ( Item->RunId[0] == '\0' && RunId[0] != '\0' )
        memcpy(Item->RunId, RunId, MDO_RUN_ID_CAPACITY);
    Item->StartClaimed = Exists && RunId[0] == '\0';
    return true;
}
