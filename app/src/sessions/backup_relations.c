#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backup_internal.h"
#include "internal.h"
#include "sidecars/binding.h"
#include "sidecars/feedback.h"
#include "sidecars/queue.h"

/* Keep only copied facts and offsets into the owned UI bytes. The fact index
 * and removal ranges are each capped by those bytes, never by untrusted IDs.
 * No event strings, DOMs or runtime handles survive a visitor callback. */
typedef struct MdoBackupEventFact {
    uint64 Id, Source, Run;
    size_t Offset, Bytes;
    xwork_event_kind Kind;
    uint32 Depth;
    char QueueId[33];
    bool Success, Todo, Truncated;
} MdoBackupEventFact;

typedef struct MdoBackupRemovedRange { uint64 First, End; } MdoBackupRemovedRange;
typedef struct MdoBackupReceiptFact { MdoQueueReceipt Value; bool Seen; } MdoBackupReceiptFact;

typedef struct MdoBackupHistoryIndex {
    const MdoSessionBackup* Backup;
    const MdoSessionBackupLimits* Limits;
    const xcancel* Cancel;
    xwork_error* Error;
    const MdoBackupOwnedFile* Ui;
    MdoBackupEventFact* Events;
    MdoBackupRemovedRange* Removed;
    MdoBackupReceiptFact* Receipts;
    size_t Count, RangeCount, ReceiptCount;
    MdoBackupRelations Relations;
} MdoBackupHistoryIndex;

static bool MdoBackupHistoryError(MdoBackupHistoryIndex* Index, const char* Message, const char* Path)
{
    const xerror* Cause = xrtGetError();
    return MdoBackupError(Index->Error, Cause != NULL && xrtErrorKind(Cause) == XERR_MEMORY ?
        XWORK_ERROR_OUT_OF_MEMORY : XWORK_ERROR_IO, Message, Path);
}

static bool MdoBackupHistoryTime(MdoBackupHistoryIndex* Index)
{
    return MdoBackupCheck(Index->Limits, Index->Cancel, Index->Error);
}

static MdoBackupReceiptFact* MdoBackupReceiptFind(MdoBackupHistoryIndex* Index, const char* Id)
{
    size_t Low = 0u, High = Index->ReceiptCount;
    while ( Low < High ) {
        size_t Middle = Low + (High - Low) / 2u;
        int Order = strcmp(Id, Index->Receipts[Middle].Value.Id);
        if ( Order == 0 ) return &Index->Receipts[Middle];
        if ( Order < 0 ) High = Middle; else Low = Middle + 1u;
    }
    return NULL;
}

static const MdoBackupEventFact* MdoBackupEventFind(const MdoBackupHistoryIndex* Index, uint64 Id)
{
    size_t Low = 0u, High = Index->Count;
    while ( Low < High ) {
        size_t Middle = Low + (High - Low) / 2u;
        if ( Id == Index->Events[Middle].Id ) return &Index->Events[Middle];
        if ( Id < Index->Events[Middle].Id ) High = Middle; else Low = Middle + 1u;
    }
    return NULL;
}

static bool MdoBackupEventCopy(const MdoSessionEventInfo* Event, void* Data)
{
    MdoBackupEventFact* Fact = (MdoBackupEventFact*)Data;
    Fact->Id = Event->EventId; Fact->Source = Event->SourceEventId; Fact->Run = Event->RunId;
    Fact->Kind = Event->Kind; Fact->Depth = Event->AgentDepth; Fact->Success = Event->Success;
    Fact->Truncated = Event->TextTruncated;
    Fact->Todo = Event->Kind == XWORK_EVENT_TOOL_DONE && Event->Success && Event->AgentDepth == 0u &&
        strcmp(Event->ToolName, "mdo.todo") == 0;
    memcpy(Fact->QueueId, Event->QueueItemId, sizeof(Fact->QueueId));
    return true;
}

static int MdoBackupRangeCompare(const void* Left, const void* Right)
{
    const MdoBackupRemovedRange *A = Left, *B = Right;
    return A->First < B->First ? -1 : A->First > B->First ? 1 :
        (A->End < B->End ? -1 : A->End > B->End ? 1 : 0);
}

static bool MdoBackupReferenceRemoved(const MdoBackupHistoryIndex* Index, uint64 Id)
{
    size_t Low = 0u, High = Index->RangeCount;
    while ( Low < High ) {
        size_t Middle = Low + (High - Low) / 2u;
        const MdoBackupRemovedRange* Range = &Index->Removed[Middle];
        if ( Id < Range->First ) High = Middle;
        else if ( Id >= Range->End ) Low = Middle + 1u;
        else return true;
    }
    return false;
}

static bool MdoBackupIndexRead(MdoBackupHistoryIndex* Index)
{
    size_t i, Offset = 0u, Capacity;
    if ( Index->Backup->History.Records > SIZE_MAX )
        return MdoBackupError(Index->Error, XWORK_ERROR_LIMIT, "session backup history count exceeds platform size", "ui-events.jsonl");
    Capacity = (size_t)Index->Backup->History.Records;
    for ( i = 0u; i < Index->Backup->Count; ++i )
        if ( strncmp(Index->Backup->Files[i].Path, "queue-receipts/", 15u) == 0 ) ++Index->ReceiptCount;
    Index->Receipts = (MdoBackupReceiptFact*)xrtCalloc(Index->ReceiptCount != 0u ? Index->ReceiptCount : 1u,
        sizeof(*Index->Receipts));
    if ( Index->Receipts == NULL ) goto memory;
    Index->ReceiptCount = 0u;
    /* Files are sorted by exact path, so the receipt subset is already sorted. */
    for ( i = 0u; i < Index->Backup->Count; ++i ) {
        const MdoBackupOwnedFile* File = &Index->Backup->Files[i];
        char Id[33];
        if ( !MdoBackupHistoryTime(Index) ) return false;
        if ( strncmp(File->Path, "queue-receipts/", 15u) != 0 ) continue;
        memcpy(Id, File->Path + 15u, 32u); Id[32] = '\0'; xrtClearError();
        if ( !MdoQueueReceiptParse(xrtStrViewN(File->Data, File->Bytes), Id,
                &Index->Receipts[Index->ReceiptCount].Value) )
            return MdoBackupHistoryError(Index, "invalid session backup queue receipt", File->Path);
        ++Index->ReceiptCount;
    }
    if ( Index->Ui == NULL || Capacity == 0u ) return true;
    /* A valid 25+-field UI record is larger than its fact. Reject a hostile
     * count before multiplication/allocation even if common syntax passed. */
    if ( Capacity > Index->Ui->Bytes / sizeof(*Index->Events) )
        return MdoBackupError(Index->Error, XWORK_ERROR_LIMIT, "session backup history index exceeds UI bytes", "ui-events.jsonl");
    Index->Events = (MdoBackupEventFact*)xrtCalloc(Capacity, sizeof(*Index->Events));
    Index->Removed = (MdoBackupRemovedRange*)xrtCalloc(Capacity, sizeof(*Index->Removed));
    if ( Index->Events == NULL || Index->Removed == NULL ) goto memory;
    while ( Offset < Index->Ui->Bytes ) {
        const char* End = (const char*)memchr(Index->Ui->Data + Offset, '\n', Index->Ui->Bytes - Offset);
        MdoBackupEventFact* Event;
        if ( !MdoBackupHistoryTime(Index) ) return false;
        if ( End == NULL || Index->Count >= Capacity ) goto invalid;
        Event = &Index->Events[Index->Count];
        Event->Offset = Offset; Event->Bytes = (size_t)(End - Index->Ui->Data - Offset);
        xrtClearError();
        if ( !MdoSessionsInternalEventVisit(Index->Backup->Info.ProjectId, Index->Backup->Info.Id,
                xrtStrViewN(Index->Ui->Data + Offset, Event->Bytes), MdoBackupEventCopy, Event) )
            return MdoBackupHistoryError(Index, "invalid session backup UI schema or identity", "ui-events.jsonl");
        if ( Event->Id == UINT64_MAX || (Index->Count != 0u && Event->Id <= Index->Events[Index->Count - 1u].Id) ) goto invalid;
        if ( Event->Kind == MDO_SESSION_EVENT_HISTORY_TRUNCATED && Event->Source != 0u ) {
            MdoBackupRemovedRange* Range = &Index->Removed[Index->RangeCount++];
            if ( Event->Source >= Event->Id ) goto invalid;
            Range->First = Event->Source; Range->End = Event->Id;
        }
        if ( Event->QueueId[0] != '\0' ) {
            MdoBackupReceiptFact* Receipt = MdoBackupReceiptFind(Index, Event->QueueId);
            if ( Event->Run == 0u || Receipt == NULL || Receipt->Seen ||
                 (Receipt->Value.Schema == 3u && Receipt->Value.AgentRunId != Event->Run) ) goto invalid;
            Receipt->Seen = true;
        }
        ++Index->Count; Offset += Event->Bytes + 1u;
    }
    if ( Index->Count != Capacity ) goto invalid;
    qsort(Index->Removed, Index->RangeCount, sizeof(*Index->Removed), MdoBackupRangeCompare);
    {
        size_t Count = 0u;
        for ( i = 0u; i < Index->RangeCount; ++i ) {
            MdoBackupRemovedRange Range = Index->Removed[i];
            if ( Count != 0u && Range.First <= Index->Removed[Count - 1u].End ) {
                if ( Range.End > Index->Removed[Count - 1u].End ) Index->Removed[Count - 1u].End = Range.End;
            } else Index->Removed[Count++] = Range;
        }
        Index->RangeCount = Count;
    }
    for ( i = 0u; i < Index->Count; ++i ) {
        if ( !MdoBackupHistoryTime(Index) ) return false;
        if ( MdoBackupReferenceRemoved(Index, Index->Events[i].Id) ) goto invalid;
    }
    return true;
memory:
    return MdoBackupError(Index->Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot allocate session backup history index", NULL);
invalid:
    return MdoBackupError(Index->Error, XWORK_ERROR_IO, "session backup has contradictory retained history or queue start evidence", "ui-events.jsonl");
}

/* A missing prefix is unknown; a durable marker is positive removal evidence.
 * Missing IDs inside the surviving range, or beyond it, are contradictions. */
static bool MdoBackupReferenceFind(MdoBackupHistoryIndex* Index, uint64 Id,
    const char* Path, const MdoBackupEventFact** Event)
{
    *Event = MdoBackupEventFind(Index, Id);
    if ( *Event != NULL ) return true;
    if ( MdoBackupReferenceRemoved(Index, Id) ) { ++Index->Relations.RemovedHistoryReferences; return true; }
    if ( Index->Count == 0u || Id < Index->Events[0].Id ) {
        ++Index->Relations.UnverifiedHistoryReferences; return true;
    }
    return MdoBackupError(Index->Error, XWORK_ERROR_IO, "session backup reference is missing from retained history", Path);
}

static bool MdoBackupHistoryNumber(const char* Name, uint64* Number)
{
    uint64 Parsed = 0u;
    size_t i;
    for ( i = 0u; Name[i] >= '0' && Name[i] <= '9'; ++i ) {
        unsigned Digit = (unsigned)(Name[i] - '0');
        if ( Parsed > (UINT64_MAX - Digit) / 10u ) return false;
        Parsed = Parsed * 10u + Digit;
    }
    if ( i == 0u || Parsed == 0u || strcmp(Name + i, ".json") != 0 ) return false;
    *Number = Parsed; return true;
}

static bool MdoBackupBindings(MdoBackupHistoryIndex* Index)
{
    size_t i;
    for ( i = 0u; i < Index->Backup->Count; ++i ) {
        const MdoBackupOwnedFile* File = &Index->Backup->Files[i];
        bool Run = strncmp(File->Path, "attachments/runs/", 17u) == 0;
        uint64 Id, AgentRun;
        char Images[4][33];
        size_t Count;
        const MdoBackupEventFact* Event = NULL;
        if ( !Run && strncmp(File->Path, "attachments/events/", 19u) != 0 ) continue;
        if ( !MdoBackupHistoryTime(Index) ) return false;
        xrtClearError();
        if ( !MdoBackupHistoryNumber(File->Path + (Run ? 17u : 19u), &Id) ||
             !MdoImageBindingParse(xrtStrViewN(File->Data, File->Bytes), Run ? Id : 0u, &AgentRun, Images, &Count) )
            return MdoBackupHistoryError(Index, "invalid session backup image binding", File->Path);
        if ( Run ) {
            /* Legacy numeric run IDs can be reused after restart. Current
             * event bindings override them; never compare the two blindly. */
            ++Index->Relations.UnverifiedHistoryReferences;
        } else {
            if ( !MdoBackupReferenceFind(Index, Id, File->Path, &Event) ) return false;
            if ( Event != NULL && (Event->Kind != XWORK_EVENT_AGENT_START || Event->Depth != 0u ||
                    Event->Run != AgentRun) )
                return MdoBackupError(Index->Error, XWORK_ERROR_IO, "session backup image binding does not match a main Agent start", File->Path);
        }
    }
    return true;
}

typedef struct MdoBackupTodoCompare { const xvalue* Stored; bool Equal; } MdoBackupTodoCompare;

static bool MdoBackupTodoEvent(const MdoSessionEventInfo* Event, void* Data)
{
    MdoBackupTodoCompare* Compare = (MdoBackupTodoCompare*)Data;
    xvalue* Input = MdoSessionsInternalTodoParse(xrtStrView(Event->Text), false);
    Compare->Equal = Input != NULL && xrtValueEqual(xrtValueObjectGet(Input, XRT_STR_LITERAL("items")),
        xrtValueObjectGet(Compare->Stored, XRT_STR_LITERAL("items")));
    xrtValueRelease(Input);
    return Compare->Equal;
}

static bool MdoBackupProjections(MdoBackupHistoryIndex* Index)
{
    const MdoBackupOwnedFile* File = MdoBackupFind(Index->Backup, "feedback.json");
    if ( File != NULL ) {
        MdoFeedbackItem Items[MDO_FEEDBACK_MAX_ITEMS];
        size_t Count, i;
        xrtClearError();
        if ( !MdoFeedbackParse(xrtStrViewN(File->Data, File->Bytes), Items, &Count) )
            return MdoBackupHistoryError(Index, "invalid session backup feedback", File->Path);
        for ( i = 0u; i < Count; ++i ) {
            const MdoBackupEventFact* Event;
            if ( !MdoBackupHistoryTime(Index) || !MdoBackupReferenceFind(Index, Items[i].EventId, File->Path, &Event) ) return false;
            if ( Event != NULL && (Event->Kind != XWORK_EVENT_MODEL_DONE || !Event->Success) )
                return MdoBackupError(Index->Error, XWORK_ERROR_IO, "session backup feedback does not match a completed model event", File->Path);
        }
    }
    File = MdoBackupFind(Index->Backup, "todo.json");
    if ( File != NULL ) {
        xvalue* Root;
        uint64 Id;
        const MdoBackupEventFact* Event = NULL;
        bool Ok;
        xrtClearError(); Root = MdoSessionsInternalTodoParse(xrtStrViewN(File->Data, File->Bytes), true);
        if ( Root == NULL ) return MdoBackupHistoryError(Index, "invalid session backup todo", File->Path);
        Ok = MdoBackupUInt(Root, "event_id", &Id);
        if ( Ok && Id == 0u ) Ok = xrtValueCount(xrtValueObjectGet(Root, XRT_STR_LITERAL("items"))) == 0u;
        if ( Ok && Id != 0u ) {
            size_t i;
            bool LegacyClear = false;
            /* Todo reconciliation historically treats a zero-source history
             * marker as a reset. Feedback/images require an explicit range. */
            for ( i = 0u; i < Index->Count; ++i )
                if ( Index->Events[i].Kind == MDO_SESSION_EVENT_HISTORY_TRUNCATED &&
                     Index->Events[i].Source == 0u && Id < Index->Events[i].Id ) { LegacyClear = true; break; }
            if ( LegacyClear ) ++Index->Relations.RemovedHistoryReferences;
            else Ok = MdoBackupReferenceFind(Index, Id, File->Path, &Event);
        }
        if ( Ok && Event != NULL ) {
            MdoBackupTodoCompare Compare = {Root, false};
            Ok = Event->Todo && !Event->Truncated && MdoSessionsInternalEventVisit(Index->Backup->Info.ProjectId,
                Index->Backup->Info.Id, xrtStrViewN(Index->Ui->Data + Event->Offset, Event->Bytes), MdoBackupTodoEvent, &Compare);
        }
        xrtValueRelease(Root);
        if ( !Ok ) {
            if ( Index->Error != NULL && Index->Error->eCode != XWORK_ERROR_NONE ) return false;
            return MdoBackupHistoryError(Index, "session backup todo does not match its retained projection event", File->Path);
        }
    }
    return true;
}

static bool MdoBackupQueueRelations(MdoBackupHistoryIndex* Index)
{
    const MdoBackupOwnedFile* File = MdoBackupFind(Index->Backup, "queue.json");
    size_t i;
    if ( File != NULL ) {
        MdoQueue Queue;
        bool Ok;
        xrtClearError();
        if ( !MdoQueueParse(xrtStrViewN(File->Data, File->Bytes), &Queue) )
            return MdoBackupHistoryError(Index, "invalid session backup queue", File->Path);
        Ok = true;
        for ( i = 0u; Ok && i < Queue.Count; ++i ) {
            MdoQueueItem* Item = &Queue.Items[i];
            MdoBackupReceiptFact* Receipt = MdoBackupReceiptFind(Index, Item->Id);
            char Run[MDO_RUN_ID_CAPACITY] = {0};
            if ( !MdoBackupHistoryTime(Index) ) { MdoQueueRelease(&Queue); return false; }
            if ( Receipt != NULL && (Receipt->Value.Schema == 1u ||
                    (Receipt->Value.Schema == 3u && Receipt->Seen)) )
                memcpy(Run, Receipt->Value.RunId, sizeof(Run));
            Ok = MdoQueueReceiptApply(Item, Receipt != NULL, Run);
        }
        MdoQueueRelease(&Queue);
        if ( !Ok ) {
            if ( Index->Error != NULL && Index->Error->eCode != XWORK_ERROR_NONE ) return false;
            return MdoBackupError(Index->Error, XWORK_ERROR_IO, "session backup queue conflicts with durable receipt evidence", File->Path);
        }
    }
    for ( i = 0u; i < Index->ReceiptCount; ++i )
        if ( !Index->Receipts[i].Seen || Index->Receipts[i].Value.Schema == 2u )
            ++Index->Relations.UnverifiedHistoryReferences;
    return true;
}

bool MdoBackupRelationsValidate(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel,
    MdoBackupRelations* Relations, xwork_error* Error)
{
    MdoBackupHistoryIndex Index = {0};
    bool Ok;
    if ( Backup == NULL || Limits == NULL || Relations == NULL )
        return MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "backup, limits and relation output are required", NULL);
    memset(Relations, 0, sizeof(*Relations));
    Index.Backup = Backup; Index.Limits = Limits; Index.Cancel = Cancel; Index.Error = Error;
    Index.Ui = MdoBackupFind(Backup, "ui-events.jsonl");
    Ok = MdoBackupHistoryTime(&Index) && MdoBackupIndexRead(&Index) &&
        MdoBackupBindings(&Index) && MdoBackupProjections(&Index) && MdoBackupQueueRelations(&Index) &&
        MdoBackupHistoryTime(&Index);
    if ( Ok ) *Relations = Index.Relations;
    xrtFree(Index.Events); xrtFree(Index.Removed); xrtFree(Index.Receipts);
    return Ok;
}
