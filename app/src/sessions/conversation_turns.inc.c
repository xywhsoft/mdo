/* Lightweight reverse pagination, derived directly from the authoritative UI
 * journal. No second message database, persisted index, or startup migration.
 * Only the requested turn summaries and one JSONL record occupy memory. */
#define MDO_CONVERSATION_SUMMARY_BYTES 512u

typedef struct MdoConversationSummary {
    uint64 First;
    uint64 End;
    int64 Time;
    const char* State;
    char Question[MDO_CONVERSATION_SUMMARY_BYTES + 1u];
    char Answer[MDO_CONVERSATION_SUMMARY_BYTES + 1u];
} MdoConversationSummary;

static void MdoConversationPrefix(char* Output, const char* Text)
{
    size_t Size = strlen(Text);
    if ( Size > MDO_CONVERSATION_SUMMARY_BYTES ) {
        Size = MDO_CONVERSATION_SUMMARY_BYTES;
        while ( Size != 0u && (((unsigned char)Text[Size] & 0xc0u) == 0x80u) ) --Size;
    }
    memcpy(Output, Text, Size);
    Output[Size] = '\0';
}

static void MdoConversationPrepend(char* Output, const char* Text)
{
    char Combined[2u * MDO_CONVERSATION_SUMMARY_BYTES + 1u];
    char Prefix[MDO_CONVERSATION_SUMMARY_BYTES + 1u];
    size_t Size;
    MdoConversationPrefix(Prefix, Text);
    Size = strlen(Prefix);
    memcpy(Combined, Prefix, Size);
    strcpy(Combined + Size, Output);
    MdoConversationPrefix(Output, Combined);
}

static xvalue* MdoConversationSummaryValue(const MdoConversationSummary* Turn)
{
    xvalue* Item = xrtValueObject();
    if ( !MdoEventsObjectTake(Item, "first_event_id", xrtValueUInt(Turn->First)) ||
         !MdoEventsObjectTake(Item, "end_event_id", xrtValueUInt(Turn->End)) ||
         !MdoEventsObjectTake(Item, "time", xrtValueInt(Turn->Time)) ||
         !MdoEventsObjectString(Item, "state", Turn->State, strlen(Turn->State)) ||
         !MdoEventsObjectString(Item, "question", Turn->Question, strlen(Turn->Question)) ||
         !MdoEventsObjectString(Item, "answer", Turn->Answer, strlen(Turn->Answer)) ) {
        xrtValueRelease(Item);
        return NULL;
    }
    return Item;
}

xvalue* MdoSessionConversationTurns(const char* Project, const char* Session,
    uint64 Before, size_t Limit, xwork_error* Error)
{
    MdoEventReader* Reader = NULL;
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoConversationSummary Turns[64];
    MdoConversationSummary Turn;
    xvalue* Result = NULL;
    xvalue* Items = NULL;
    xstrview Record;
    uint64 End, Latest = 0u, FinalModelTurn = 0u;
    size_t Count = 0u, i;
    bool HasMore = false, Lost = false, HasAnswer = false;
    xworkErrorInit(Error);
    if ( Limit == 0u || Limit > 64u ||
         !MdoEventsIdValid(Project, MDO_PROJECT_ID_CAPACITY) ||
         !MdoEventsIdValid(Session, MDO_SESSION_ID_CAPACITY) ||
         !MdoEventsPath(Path, Project, Session) ) {
        MdoEventsError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid conversation page");
        return NULL;
    }
    Reader = MdoEventReaderOpen(Path);
    if ( Reader == NULL ) goto fail;
    memset(&Turn, 0, sizeof(Turn));
    Turn.State = "running";
    End = Reader->Size;
    Latest = MdoEventReaderLatest(Reader, Project, Session, &Lost);
    if ( Before != 0u ) {
        MdoEventReaderAfter(Reader, Project, Session, Before - 1u);
        while ( Reader->Position < Reader->Size ) {
            uint64 Start = Reader->Position;
            MdoSessionEventOwned Probe;
            if ( !MdoEventReaderNext(Reader, &Record) ) break;
            if ( !MdoEventsParse(Project, Session, Record, &Probe) ) {
                Lost = true; xrtClearError(); continue;
            }
            if ( Probe.Info.EventId >= Before ) {
                End = Start;
                MdoEventsOwnedUnit(&Probe);
                break;
            }
            MdoEventsOwnedUnit(&Probe);
        }
    }
    while ( MdoEventReaderPrevious(Reader, &End, &Record) ) {
        MdoSessionEventOwned Owned;
        MdoSessionEventInfo* Event = &Owned.Info;
        if ( !MdoEventsParse(Project, Session, Record, &Owned) ) {
            Lost = true;
            xrtClearError();
            continue;
        }
        if ( Latest == 0u ) Latest = Event->EventId;
        if ( Turn.End == 0u ) Turn.End = Event->EventId;
        if ( Event->AgentDepth == 0u ) {
            if ( Event->Kind == XWORK_EVENT_AGENT_DONE || Event->Kind == XWORK_EVENT_ERROR ) {
                if ( strcmp(Turn.State, "running") == 0 ) {
                    Turn.State = Event->Kind == XWORK_EVENT_ERROR ? "failed" :
                        Event->Success ? "done" : "cancelled";
                    MdoConversationPrefix(Turn.Answer, Event->Text);
                }
            } else if ( Event->Kind == XWORK_EVENT_MODEL_TEXT_DELTA ) {
                if ( !HasAnswer ) {
                    Turn.Answer[0] = '\0';
                    FinalModelTurn = Event->AgentTurn;
                    HasAnswer = true;
                }
                if ( Event->AgentTurn == FinalModelTurn )
                    MdoConversationPrepend(Turn.Answer, Event->Text);
            } else if ( Event->Kind == XWORK_EVENT_AGENT_START &&
                        (Event->UserMessageSequence > 0u || Event->SchemaVersion < 3u) ) {
                Turn.First = Event->EventId;
                Turn.Time = Event->OccurredAt;
                MdoConversationPrefix(Turn.Question, Event->Text);
                if ( Before == 0u || Turn.First < Before ) {
                    if ( Count == Limit ) HasMore = true;
                    else Turns[Count++] = Turn;
                }
                memset(&Turn, 0, sizeof(Turn));
                Turn.State = "running";
                HasAnswer = false;
            }
        }
        MdoEventsOwnedUnit(&Owned);
        if ( HasMore ) break;
    }
    if ( Reader->Failed ) goto fail;
    Result = xrtValueObject();
    Items = xrtValueArray();
    if ( Result == NULL || Items == NULL ) goto fail;
    for ( i = Count; i != 0u; --i ) {
        xvalue* Item = MdoConversationSummaryValue(&Turns[i - 1u]);
        bool Ok = Item != NULL && xrtValueArrayAppendTake(Items, &Item);
        xrtValueRelease(Item);
        if ( !Ok ) goto fail;
    }
    if ( !MdoEventsObjectTake(Result, "latest_event_id", xrtValueUInt(Latest)) ||
         !MdoEventsObjectTake(Result, "next_before", xrtValueUInt(Count != 0u ? Turns[Count - 1u].First : Before)) ||
         !MdoEventsObjectTake(Result, "has_more", xrtValueBool(HasMore)) ||
         !MdoEventsObjectTake(Result, "history_lost", xrtValueBool(Lost)) ||
         !xrtValueObjectSetTake(Result, xrtStrView("items"), &Items) ) goto fail;
    MdoEventReaderClose(Reader);
    return Result;
fail:
    xrtValueRelease(Result);
    xrtValueRelease(Items);
    MdoEventReaderClose(Reader);
    MdoEventsError(Error, XWORK_ERROR_IO, "cannot read conversation history");
    return NULL;
}
