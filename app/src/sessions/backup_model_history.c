#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xllm-session.h>

#include "backup_internal.h"
#include "internal.h"
#include "sidecars/binding.h"

/* Index only borrowed views into our unpublished replay. The model owns all
 * strings; a UI visitor owns its strings only until it returns. No rendering,
 * repair, callbacks, catalog access or runtime attachment happens here. */
typedef struct MdoBackupLedgerEntry {
    xllm_session_entry_view View;
    bool Projected;
} MdoBackupLedgerEntry;

typedef struct MdoBackupAssistant { uint64 Turn, Sequence; size_t Entry; } MdoBackupAssistant;
typedef struct MdoBackupCall {
    uint64 Turn;
    const xllm_tool_call* Value;
    size_t Result; /* SIZE_MAX until a durable model result is indexed. */
} MdoBackupCall;

typedef struct MdoBackupModelIndex {
    const MdoSessionBackup* Backup;
    const MdoSessionBackupLimits* Limits;
    const xcancel* Cancel;
    xwork_error* Error;
    MdoBackupLedgerEntry* Entries;
    MdoBackupAssistant* Assistants;
    MdoBackupCall* Calls;
    size_t Count, AssistantCount, CallCount;
    MdoSessionBackupModelHistory Facts;
} MdoBackupModelIndex;

static bool MdoBackupModelConflict(MdoBackupModelIndex* Index, const char* Message)
{
    return MdoBackupError(Index->Error, XWORK_ERROR_IO, Message, "ui-events.jsonl");
}

static bool MdoBackupModelTime(MdoBackupModelIndex* Index)
{
    return MdoBackupCheck(Index->Limits, Index->Cancel, Index->Error);
}

static int MdoBackupAssistantOrder(const void* Left, const void* Right)
{
    const MdoBackupAssistant *A = Left, *B = Right;
    return A->Turn < B->Turn ? -1 : A->Turn > B->Turn ? 1 :
        (A->Sequence < B->Sequence ? -1 : A->Sequence > B->Sequence ? 1 : 0);
}

static int MdoBackupCallOrder(const void* Left, const void* Right)
{
    const MdoBackupCall *A = Left, *B = Right;
    return A->Turn < B->Turn ? -1 : A->Turn > B->Turn ? 1 : strcmp(A->Value->sId, B->Value->sId);
}

static MdoBackupCall* MdoBackupModelCallFind(MdoBackupModelIndex* Index, uint64 Turn, const char* Id)
{
    size_t Low = 0u, High = Index->CallCount;
    while ( Low < High ) {
        size_t Middle = Low + (High - Low) / 2u;
        MdoBackupCall* Call = &Index->Calls[Middle];
        int Order = Turn < Call->Turn ? -1 : Turn > Call->Turn ? 1 : strcmp(Id, Call->Value->sId);
        if ( Order == 0 ) return Call;
        if ( Order < 0 ) High = Middle; else Low = Middle + 1u;
    }
    return NULL;
}

static MdoBackupLedgerEntry* MdoBackupModelSequenceFind(MdoBackupModelIndex* Index, uint64 Sequence)
{
    size_t Low = 0u, High = Index->Count;
    while ( Low < High ) {
        size_t Middle = Low + (High - Low) / 2u;
        MdoBackupLedgerEntry* Entry = &Index->Entries[Middle];
        if ( Sequence == Entry->View.uSequence ) return Entry;
        if ( Sequence < Entry->View.uSequence ) High = Middle; else Low = Middle + 1u;
    }
    return NULL;
}

static bool MdoBackupModelIndexRead(MdoBackupModelIndex* Index, const xllm_session* Model)
{
    size_t i, Count = xllmSessionEntryCount(Model), Calls = 0u;
    if ( Count > Index->Limits->TotalBytes / sizeof(*Index->Entries) ) goto limit;
    Index->Entries = (MdoBackupLedgerEntry*)xrtCalloc(Count != 0u ? Count : 1u, sizeof(*Index->Entries));
    Index->Assistants = (MdoBackupAssistant*)xrtCalloc(Count != 0u ? Count : 1u, sizeof(*Index->Assistants));
    if ( Index->Entries == NULL || Index->Assistants == NULL ) goto memory;
    for ( i = 0u; i < Count; ++i ) {
        xllm_session_entry_view* View = &Index->Entries[i].View;
        const xllm_message* Message;
        if ( !MdoBackupModelTime(Index) ) return false;
        View->iSize = sizeof(*View);
        if ( !xllmSessionEntryAt(Model, i, View) || View->pMessage == NULL || View->uSequence == 0u ||
             (i != 0u && View->uSequence <= Index->Entries[i - 1u].View.uSequence) )
            return MdoBackupModelConflict(Index, "model replay has no stable retained entry view");
        Message = View->pMessage;
        if ( Message->iToolCallCount > SIZE_MAX - Calls ) goto limit;
        Calls += Message->iToolCallCount;
        if ( Message->eRole == XLLM_ROLE_ASSISTANT ) {
            MdoBackupAssistant* Assistant = &Index->Assistants[Index->AssistantCount++];
            Assistant->Turn = View->uTurn; Assistant->Sequence = View->uSequence; Assistant->Entry = i;
        }
        ++Index->Count;
    }
    if ( Calls > Index->Limits->TotalBytes / sizeof(*Index->Calls) ) goto limit;
    Index->Calls = (MdoBackupCall*)xrtCalloc(Calls != 0u ? Calls : 1u, sizeof(*Index->Calls));
    if ( Index->Calls == NULL ) goto memory;
    for ( i = 0u; i < Index->Count; ++i ) {
        const xllm_session_entry_view* View = &Index->Entries[i].View;
        size_t j;
        if ( !MdoBackupModelTime(Index) ) return false;
        for ( j = 0u; j < View->pMessage->iToolCallCount; ++j ) {
            MdoBackupCall* Call = &Index->Calls[Index->CallCount++];
            Call->Turn = View->uTurn; Call->Value = &View->pMessage->pToolCalls[j]; Call->Result = SIZE_MAX;
        }
    }
    qsort(Index->Assistants, Index->AssistantCount, sizeof(*Index->Assistants), MdoBackupAssistantOrder);
    qsort(Index->Calls, Index->CallCount, sizeof(*Index->Calls), MdoBackupCallOrder);
    for ( i = 0u; i < Index->Count; ++i ) {
        const xllm_session_entry_view* View = &Index->Entries[i].View;
        if ( !MdoBackupModelTime(Index) ) return false;
        if ( View->pMessage->eRole == XLLM_ROLE_TOOL ) {
            MdoBackupCall* Call = MdoBackupModelCallFind(Index, View->uTurn, View->pMessage->sToolCallId);
            if ( Call == NULL || Call->Result != SIZE_MAX )
                return MdoBackupModelConflict(Index, "model replay has contradictory tool identities");
            Call->Result = i;
        }
    }
    return true;
limit:
    return MdoBackupError(Index->Error, XWORK_ERROR_LIMIT, "model history index exceeds byte budget", NULL);
memory:
    return MdoBackupError(Index->Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot allocate model history index", NULL);
}

static bool MdoBackupLinkText(const char* Ui, const char* Model, bool Truncated)
{
    size_t Bytes = strlen(Ui != NULL ? Ui : "");
    const char* Content = Model != NULL ? Model : "";
    return Truncated ? strncmp(Content, Ui != NULL ? Ui : "", Bytes) == 0 :
        strcmp(Content, Ui != NULL ? Ui : "") == 0;
}

static bool MdoBackupModelImageEvidence(MdoBackupModelIndex* Index,
    const MdoSessionEventInfo* Event, bool* HasImages)
{
    char Path[80], Ids[4][33];
    uint64 Run;
    size_t Count;
    const MdoBackupOwnedFile* Binding;
    (void)snprintf(Path, sizeof(Path), "attachments/events/%llu.json", (unsigned long long)Event->EventId);
    Binding = MdoBackupFind(Index->Backup, Path);
    if ( Binding == NULL ) {
        (void)snprintf(Path, sizeof(Path), "attachments/runs/%llu.json", (unsigned long long)Event->RunId);
        Binding = MdoBackupFind(Index->Backup, Path);
    }
    *HasImages = false;
    if ( Binding == NULL ) return true;
    xrtClearError();
    if ( !MdoImageBindingParse(xrtStrViewN(Binding->Data, Binding->Bytes), 0u, &Run, Ids, &Count) ) {
        const xerror* Cause = xrtGetError();
        return MdoBackupError(Index->Error, Cause != NULL && xrtErrorKind(Cause) == XERR_MEMORY ?
            XWORK_ERROR_OUT_OF_MEMORY : XWORK_ERROR_IO, "cannot inspect model prompt image evidence", Binding->Path);
    }
    *HasImages = Count != 0u;
    return true;
}

/* TEXT parts are authoritative for multimodal messages. Compare their ordered
 * concatenation without allocating a second, potentially large prompt. */
static bool MdoBackupMessagePartsText(const char* Ui, const xllm_message* Message, bool Truncated)
{
    size_t Offset = 0u, Bytes = strlen(Ui != NULL ? Ui : ""), i;
    if ( Message->iPartCount == 0u ) return MdoBackupLinkText(Ui, Message->sContent, Truncated);
    for ( i = 0u; i < Message->iPartCount; ++i ) {
        const xllm_part* Part = &Message->pParts[i];
        size_t Length, Compare;
        if ( Part->eKind != XLLM_PART_TEXT || Part->sText == NULL ) continue;
        Length = strlen(Part->sText); Compare = Length < Bytes - Offset ? Length : Bytes - Offset;
        if ( Compare != 0u && memcmp(Ui + Offset, Part->sText, Compare) != 0 ) return false;
        Offset += Compare;
        if ( Compare < Length ) return Truncated;
    }
    return Offset == Bytes;
}

static bool MdoBackupModelEvent(const MdoSessionEventInfo* Event, void* Data)
{
    MdoBackupModelIndex* Index = (MdoBackupModelIndex*)Data;
    MdoBackupLedgerEntry* Entry = NULL;
    bool Partial = Event->TextTruncated;
    if ( !MdoBackupModelTime(Index) ) return false;
    if ( Event->AgentDepth != 0u ) return true; /* Child ledgers are not the root ledger. */
    if ( Event->Kind == XWORK_EVENT_AGENT_START ) {
        bool PartsLost, HasImages;
        if ( Event->UserMessageSequence == 0u ) goto unverified; /* resume or pre-sequence UI */
        Entry = MdoBackupModelSequenceFind(Index, Event->UserMessageSequence);
        if ( Entry == NULL || Entry->Projected || Entry->View.uTurn != Event->AgentTurn ||
             Entry->View.pMessage->eRole != XLLM_ROLE_USER || Entry->View.uFlags != 0u )
            return MdoBackupModelConflict(Index, "UI prompt does not match its retained model sequence");
        // Older writers omit multimodal parts, including the prompt text in
        // those parts. A valid image binding is positive evidence of that loss,
        // never permission to invent or repair the missing model content.
        if ( !MdoBackupModelImageEvidence(Index, Event, &HasImages) ) return false;
        PartsLost = Entry->View.pMessage->iPartCount == 0u && HasImages;
        if ( !(PartsLost && Entry->View.pMessage->sContent == NULL) &&
             !MdoBackupMessagePartsText(Event->Text, Entry->View.pMessage, Partial) )
            return MdoBackupModelConflict(Index, "UI prompt conflicts with its retained model text");
        Partial = Partial || PartsLost;
    } else if ( Event->Kind == XWORK_EVENT_MODEL_DONE && Event->Success ) {
        size_t Low = 0u, High = Index->AssistantCount;
        if ( Event->AgentTurn == 0u ) goto unverified;
        while ( Low < High ) {
            size_t Middle = Low + (High - Low) / 2u;
            if ( Index->Assistants[Middle].Turn < Event->AgentTurn ) Low = Middle + 1u; else High = Middle;
        }
        if ( Low >= Index->AssistantCount || Index->Assistants[Low].Turn != Event->AgentTurn )
            return MdoBackupModelConflict(Index, "UI model completion has no retained assistant turn");
        if ( Low + 1u < Index->AssistantCount && Index->Assistants[Low + 1u].Turn == Event->AgentTurn ) goto unverified;
        Entry = &Index->Entries[Index->Assistants[Low].Entry];
        if ( Entry->Projected || !MdoBackupMessagePartsText(Event->Text, Entry->View.pMessage, Partial) )
            return MdoBackupModelConflict(Index, "UI model completion conflicts with its retained assistant text");
    } else if ( Event->Kind == XWORK_EVENT_TOOL_START || Event->Kind == XWORK_EVENT_TOOL_DONE ||
                Event->Kind == XWORK_EVENT_RECOVERY_RESOLVED ) {
        MdoBackupCall* Call;
        if ( Event->AgentTurn == 0u || Event->ToolCallId[0] == '\0' ) goto unverified;
        Call = MdoBackupModelCallFind(Index, Event->AgentTurn, Event->ToolCallId);
        if ( Call == NULL ) {
            if ( Partial ) goto unverified; /* The ID itself may have been bounded. */
            return MdoBackupModelConflict(Index, "UI tool event has no retained model call identity");
        }
        if ( !MdoBackupLinkText(Event->ToolName, Call->Value->sName, Partial) ||
             (Event->Kind == XWORK_EVENT_TOOL_START &&
              !MdoBackupLinkText(Event->Text, Call->Value->sArgumentsJson, Partial)) )
            return MdoBackupModelConflict(Index, "UI tool event conflicts with model call name or arguments");
        if ( Event->Kind != XWORK_EVENT_TOOL_START ) {
            // TOOL_DONE is emitted before appending its model result. A crash
            // in that window is evidence to preserve, not bytes to repair.
            if ( Call->Result == SIZE_MAX ) Partial = true;
            else Index->Entries[Call->Result].Projected = true;
        }
    } else return true;
    if ( Entry != NULL ) Entry->Projected = true;
    ++Index->Facts.MatchedUiRecords;
    if ( Partial ) ++Index->Facts.UnverifiedUiRecords;
    return true;
unverified:
    ++Index->Facts.UnverifiedUiRecords;
    return true;
}

bool MdoSessionBackupCheckModelHistory(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel,
    MdoSessionBackupModelHistory* History, xwork_error* Error)
{
    MdoSessionBackupLimits Budget;
    MdoBackupModelIndex Index = {0};
    const MdoBackupOwnedFile* Ui;
    xllm_session* Model = NULL;
    size_t Offset = 0u, i;
    bool Ok = false;
    if ( Error != NULL ) xworkErrorInit(Error);
    if ( History == NULL || History->Size != sizeof(*History) )
        return MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "model history output size is invalid", NULL);
    memset(History, 0, sizeof(*History)); History->Size = sizeof(*History);
    if ( !MdoBackupLimits(Limits, &Budget, 30000000u, Error) ) return false;
    Model = MdoSessionBackupReplayModel(Backup, &Budget, Cancel, Error);
    if ( Model == NULL ) return false;
    Index.Backup = Backup; Index.Limits = &Budget; Index.Cancel = Cancel; Index.Error = Error;
    Index.Facts.Size = sizeof(Index.Facts);
    if ( !MdoBackupModelIndexRead(&Index, Model) ) goto done;
    Ui = MdoBackupFind(Backup, "ui-events.jsonl");
    while ( Ui != NULL && Offset < Ui->Bytes ) {
        const char* End = (const char*)memchr(Ui->Data + Offset, '\n', Ui->Bytes - Offset);
        const xerror* Cause;
        if ( !MdoBackupModelTime(&Index) ) goto done;
        xrtClearError();
        if ( End == NULL || !MdoSessionsInternalEventVisit(Backup->Info.ProjectId, Backup->Info.Id,
                xrtStrViewN(Ui->Data + Offset, (size_t)(End - Ui->Data - Offset)), MdoBackupModelEvent, &Index) ) {
            if ( Error != NULL && Error->eCode != XWORK_ERROR_NONE ) goto done;
            Cause = xrtGetError();
            (void)MdoBackupError(Error, Cause != NULL && xrtErrorKind(Cause) == XERR_MEMORY ?
                XWORK_ERROR_OUT_OF_MEMORY : XWORK_ERROR_IO, "cannot read retained model/UI history", "ui-events.jsonl");
            goto done;
        }
        Offset = (size_t)(End - Ui->Data) + 1u;
    }
    for ( i = 0u; i < Index.Count; ++i ) {
        const MdoBackupLedgerEntry* Entry = &Index.Entries[i];
        if ( !MdoBackupModelTime(&Index) ) goto done;
        if ( Entry->View.pMessage->eRole != XLLM_ROLE_SYSTEM && Entry->View.uFlags == 0u && !Entry->Projected )
            ++Index.Facts.UnprojectedModelMessages;
    }
    if ( !MdoBackupModelTime(&Index) ) goto done;
    *History = Index.Facts; Ok = true;
done:
    xrtFree(Index.Entries); xrtFree(Index.Assistants); xrtFree(Index.Calls);
    xllmSessionDestroy(Model);
    return Ok;
}
