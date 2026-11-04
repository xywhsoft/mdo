/* Display projection, not model context. Recent content is read in one file
 * view instead of replaying 32 events per network round trip. Each operation
 * has bounded retained memory and scan work. Display history pages finish a
 * reply and its preceding execution before crossing a page boundary when
 * possible. Bad or oversized sections yield local notices and a continuation,
 * never erase other readable events in the conversation.
 * No sidecar, migration or write is needed to browse an old conversation. */
#define MDO_VIEW_EVENTS 96u
#define MDO_VIEW_BYTES (96u * 1024u)
#define MDO_VIEW_SCAN (16u * 1024u * 1024u)
#define MDO_VIEW_TEXT 8192u
#define MDO_VIEW_REPLY_TEXT (1024u * 1024u)
#define MDO_VIEW_HARD_BYTES (4u * 1024u * 1024u)
#define MDO_VIEW_HARD_EVENTS 4096u

void MdoSessionConversationPageNotice(MdoConversationPageInfo* Page,
    MdoConversationNoticeKind Kind, uint64 BeforeEventId, uint64 Offset)
{
    for (size_t i = 0u; i < Page->NoticeCount; ++i)
        if (Page->Notices[i].Kind == Kind && Page->Notices[i].BeforeEventId == BeforeEventId) return;
    if (Page->NoticeCount < MDO_CONVERSATION_NOTICE_LIMIT)
        Page->Notices[Page->NoticeCount++] = (MdoConversationNotice){ Kind, BeforeEventId, Offset };
}

/* A corrupt suffix must not cause an unbounded search before the first page.
 * A later byte-cursor page can establish its own latest valid ID if necessary. */
static uint64 MdoViewLatest(MdoEventReader* Reader, const char* Project,
    const char* Session, MdoConversationPageInfo* Page)
{
    uint64 End = Reader->Size, Scanned = 0u; xstrview Record;
    while (End && Scanned < MDO_VIEW_SCAN) {
        uint64 Previous = End; MdoSessionEventOwned Event;
        if (!MdoEventReaderPrevious(Reader, &End, &Record)) break;
        Scanned += Previous - End;
        if (MdoEventsParse(Project, Session, Record, &Event)) {
            uint64 Id = Event.Info.EventId; MdoEventsOwnedUnit(&Event); return Id;
        }
        MdoSessionConversationPageNotice(Page, MDO_CONVERSATION_INVALID_RECORD, 0u, End);
        xrtClearError();
    }
    return 0u;
}

static bool MdoViewEpoch(MdoEventReader* Reader, char Output[65])
{
    xsha256 Hash; uint8 Digest[32]; char Identity[160]; xstrview First;
    const xfileinfo* Info = &Reader->Info;
    /* Unsupported identities deliberately include mtime: they fall back to
     * a fresh snapshot on append rather than falsely validating old data. */
    int Length = snprintf(Identity, sizeof(Identity), "%llu/%llu/%lld/%lld",
        (unsigned long long)Info->Device, (unsigned long long)Info->Identity,
        (long long)Info->Created,
        (long long)((Info->Available & XFILE_INFO_IDENTITY) ? 0 : Info->Modified));
    xrtSha256Init(&Hash);
    bool Ok = Length > 0 && (size_t)Length < sizeof(Identity) &&
        xrtSha256Update(&Hash, Identity, (size_t)Length);
    Reader->Position = 0u;
    if (Ok && MdoEventReaderNext(Reader, &First))
        Ok = xrtSha256Update(&Hash, First.Data, First.Size);
    Ok = Ok && !Reader->Failed && xrtSha256Final(&Hash, Digest);
    if (Ok) {
        for (size_t i = 0u; i < 32u; ++i) snprintf(Output + i * 2u, 3u, "%02x", Digest[i]);
    }
    return Ok;
}

static bool MdoViewCurrent(const char* Path, const MdoEventReader* Reader)
{
    xfileinfo Info; bool Exists = false;
    if (!MdoHomeExternalStat(Path, &Exists, &Info)) return false;
    if (!Reader->File) return !Exists;
    return Exists && Info.Size >= Reader->Size &&
        (!(Reader->Info.Available & XFILE_INFO_IDENTITY) ||
         ((Info.Available & XFILE_INFO_IDENTITY) && Info.Device == Reader->Info.Device &&
          Info.Identity == Reader->Info.Identity));
}

static bool MdoViewDelta(const MdoSessionEventInfo* Info)
{
    return Info->Kind == XWORK_EVENT_MODEL_TEXT_DELTA ||
        Info->Kind == XWORK_EVENT_MODEL_REASONING_DELTA;
}

static size_t MdoViewCost(const MdoSessionEventOwned* Event)
{
    const char* Fields[] = { Event->Text, Event->ToolName, Event->ToolCallId,
        Event->ArtifactPath, Event->Model, Event->ModelId };
    size_t Cost = 1536u;
    for (size_t i = 0u; i < sizeof(Fields)/sizeof(Fields[0]); ++i)
        for (const uint8* p = (const uint8*)Fields[i]; *p; ++p)
            Cost += *p < 32u ? 12u : (*p == '"' || *p == '\\') ? 4u : 1u;
    return Cost;
}

static size_t MdoViewPrefix(const char* Text, size_t Limit)
{
    size_t Length = strlen(Text);
    if (Length <= Limit) return Length;
    Length = Limit;
    while (Length && (((uint8)Text[Length] & 0xc0u) == 0x80u)) --Length;
    return Length;
}

static MdoSessionEventSnapshot* MdoReadConversationPage(const char* Project,
    const char* Session, uint64 Before, uint64 After, const char* Epoch,
    size_t Turns, bool ReplyGroups, bool HasScanBefore, uint64 ScanBefore,
    MdoConversationPageInfo* Page, xwork_error* Error)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoSessionEventSnapshot* Snapshot = NULL;
    MdoEventReader* Reader = NULL;
    xstrview Record; uint64 End, Scanned = 0u, Previous = 0u;
    size_t Bytes = 0u, Starts = 0u, Replies = 0u, Streams[MDO_VIEW_HARD_EVENTS], StreamCount = 0u;
    uint64 ReplyRun = 0u, ReplyAgent = 0u, ReplyTurn = 0u;
    size_t BoundaryCount = 0u, BoundaryBytes = 0u;
    uint64 BoundaryEnd = 0u, BoundaryRun = 0u, BoundaryAgent = 0u, BoundaryTurn = 0u;
    bool HaveBoundary = false;
    bool Delta, HaveReply = false;
    xworkErrorInit(Error);
    if (!Page || !Turns || Turns > 4u || (Before && After) ||
        !MdoEventsIdValid(Project, MDO_PROJECT_ID_CAPACITY) ||
        !MdoEventsIdValid(Session, MDO_SESSION_ID_CAPACITY) ||
        !MdoEventsPath(Path, Project, Session)) goto invalid;
    memset(Page, 0, sizeof(*Page));
    Snapshot = (MdoSessionEventSnapshot*)xrtCalloc(1u, sizeof(*Snapshot));
    if (!Snapshot) goto memory;
    xrtAtomic32Init(&Snapshot->Refs, 1u);
    Reader = MdoEventReaderOpen(Path);
    if (!Reader || !MdoViewEpoch(Reader, Page->Epoch)) goto io;
    Page->StableEpoch = Reader->File != NULL &&
        (Reader->Info.Available & XFILE_INFO_IDENTITY) != 0u;
    memcpy(Snapshot->Epoch, Page->Epoch, sizeof(Snapshot->Epoch));
    Snapshot->LatestId = MdoViewLatest(Reader, Project, Session, Page);
    bool LatestUnknown = !Snapshot->LatestId && Reader->Size;
    Delta = !HasScanBefore && !LatestUnknown && !Before && Epoch && *Epoch &&
        !strcmp(Epoch, Page->Epoch) && After <= Snapshot->LatestId;
    Page->Delta = Delta;
    Snapshot->NextCursor = Delta ? After : Snapshot->LatestId;
    End = Reader->Size;
    if (HasScanBefore) {
        if (!Epoch || strcmp(Epoch, Page->Epoch) || ScanBefore > Reader->Size) goto changed;
        End = ScanBefore;
    } else if (Before) {
        MdoEventReaderAfter(Reader, Project, Session, Before - 1u);
        while (Reader->Position < Reader->Size) {
            uint64 At = Reader->Position; MdoSessionEventOwned Event;
            if (!MdoEventReaderNext(Reader, &Record)) break;
            if (!MdoEventsParse(Project, Session, Record, &Event)) { xrtClearError(); continue; }
            bool Found = Event.Info.EventId >= Before;
            MdoEventsOwnedUnit(&Event);
            if (Found) { End = At; break; }
        }
    } else if (Delta) MdoEventReaderAfter(Reader, Project, Session, After);

    while (Scanned < MDO_VIEW_SCAN) {
        uint64 RecordEnd = End, RecordStart = Reader->Position;
        if (!(Delta ? MdoEventReaderNext(Reader, &Record) :
                MdoEventReaderPrevious(Reader, &End, &Record))) break;
        MdoSessionEventOwned Event; size_t Merge = SIZE_MAX;
        Scanned += Delta ? Reader->Position - RecordStart : RecordEnd - End;
        if (!MdoEventsParse(Project, Session, Record, &Event)) {
            MdoSessionConversationPageNotice(Page, MDO_CONVERSATION_INVALID_RECORD, Previous,
                Delta ? RecordStart : End);
            StreamCount = 0u; xrtClearError(); continue;
        }
        if (Delta && Event.Info.EventId <= After) { MdoEventsOwnedUnit(&Event); continue; }
        if (LatestUnknown && !Snapshot->LatestId) {
            Snapshot->LatestId = Event.Info.EventId; Snapshot->NextCursor = Event.Info.EventId;
        }
        if ((Previous && (Delta ? Event.Info.EventId <= Previous : Event.Info.EventId >= Previous)) ||
            Event.Info.EventId > Snapshot->LatestId) {
            MdoSessionConversationPageNotice(Page, MDO_CONVERSATION_INVALID_RECORD, Previous,
                Delta ? RecordStart : End);
            MdoEventsOwnedUnit(&Event); StreamCount = 0u; continue;
        }
        /* A history group contains one public reply and the execution before
         * it. Byte/event budgets are soft targets: finish the group before
         * paging, so no stream prefix or half execution becomes a new layout. */
        bool AtomicReplies = ReplyGroups && !Delta;
        if (!AtomicReplies && Snapshot->Count && Bytes >= MDO_VIEW_BYTES) {
            MdoEventsOwnedUnit(&Event); break;
        }
        bool Main = Event.Info.AgentDepth == 0u;
        bool RootStart = Main && Event.Info.Kind == XWORK_EVENT_AGENT_START;
        bool SameReply = HaveReply && Event.Info.RunId == ReplyRun &&
            Event.Info.AgentId == ReplyAgent && Event.Info.AgentTurn == ReplyTurn;
        bool Filled = Replies >= Turns || Bytes >= MDO_VIEW_BYTES || Snapshot->Count >= MDO_VIEW_EVENTS;
        if (AtomicReplies && Main && Event.Info.Kind == XWORK_EVENT_MODEL_DONE) {
            /* Tool-only model calls belong to the same process. Remember the
             * boundary, but use it only when that call has a public reply. */
            HaveBoundary = true; BoundaryCount = Snapshot->Count; BoundaryBytes = Bytes;
            BoundaryEnd = RecordEnd; BoundaryRun = Event.Info.RunId;
            BoundaryAgent = Event.Info.AgentId; BoundaryTurn = Event.Info.AgentTurn;
        }
        if (AtomicReplies && Main && Event.Info.Kind == XWORK_EVENT_MODEL_TEXT_DELTA && !SameReply) {
            if (HaveReply && Filled && HaveBoundary && Event.Info.RunId == BoundaryRun &&
                Event.Info.AgentId == BoundaryAgent && Event.Info.AgentTurn == BoundaryTurn) {
                MdoEventsOwnedUnit(&Event);
                while (Snapshot->Count > BoundaryCount)
                    MdoEventsOwnedUnit(&Snapshot->Events[--Snapshot->Count]);
                Bytes = BoundaryBytes; End = BoundaryEnd;
                break;
            }
            ++Replies; HaveReply = true;
            ReplyRun = Event.Info.RunId; ReplyAgent = Event.Info.AgentId; ReplyTurn = Event.Info.AgentTurn;
        }
        if (Event.Info.Kind == XWORK_EVENT_AGENT_START || Event.Info.Kind == XWORK_EVENT_MODEL_START ||
            Event.Info.Kind == XWORK_EVENT_MODEL_DONE) StreamCount = 0u;
        bool Start = Event.Info.Kind == XWORK_EVENT_AGENT_START && Event.Info.AgentDepth == 0u &&
            (Event.Info.UserMessageSequence || Event.Info.SchemaVersion < 3u);
        if (Delta && Start && Starts == Turns) { MdoEventsOwnedUnit(&Event); break; }
        size_t TextLimit = ReplyGroups && Main &&
            (Event.Info.Kind == XWORK_EVENT_MODEL_TEXT_DELTA || Event.Info.Kind == XWORK_EVENT_AGENT_START)
            ? MDO_VIEW_REPLY_TEXT : MdoViewDelta(&Event.Info) ? MDO_VIEW_TEXT : 4096u;
        size_t Prefix = MdoViewPrefix(Event.Text, TextLimit);
        if (AtomicReplies && Main && Event.Text[Prefix] &&
            Event.Info.Kind == XWORK_EVENT_MODEL_TEXT_DELTA) {
            MdoSessionConversationPageNotice(Page, MDO_CONVERSATION_TEXT_LIMIT, Event.Info.EventId, End);
        }
        if (Event.Text[Prefix]) { Event.Text[Prefix] = '\0'; Event.Info.TextTruncated = true; }
        if (MdoViewDelta(&Event.Info)) {
            for (size_t i = 0u; i < StreamCount; ++i) {
                const MdoSessionEventInfo* Other = &Snapshot->Events[Streams[i]].Info;
                if (Other->Kind == Event.Info.Kind && Other->RunId == Event.Info.RunId &&
                    Other->AgentId == Event.Info.AgentId && Other->AgentDepth == Event.Info.AgentDepth &&
                    Other->AgentTurn == Event.Info.AgentTurn) { Merge = Streams[i]; break; }
            }
        }
        if (Merge != SIZE_MAX) {
            MdoSessionEventOwned* Other = &Snapshot->Events[Merge];
            size_t A = strlen(Other->Text), B = strlen(Event.Text);
            if (A > MDO_VIEW_REPLY_TEXT || B > MDO_VIEW_REPLY_TEXT - A) {
                MdoSessionConversationPageNotice(Page, MDO_CONVERSATION_TEXT_LIMIT, Event.Info.EventId, End);
                MdoEventsOwnedUnit(&Event); StreamCount = 0u;
                if (!Delta) End = RecordEnd;
                break;
            }
            char* Combined = (char*)xrtMalloc(A + B + 1u);
            if (!Combined) { MdoEventsOwnedUnit(&Event); goto memory; }
            if (Delta) { memcpy(Combined, Other->Text, A); memcpy(Combined + A, Event.Text, B + 1u); }
            else { memcpy(Combined, Event.Text, B); memcpy(Combined + B, Other->Text, A + 1u); }
            size_t Keep = MdoViewPrefix(Combined, TextLimit);
            char* Text = MdoEventsCopy(xrtStrViewN(Combined, Keep), TextLimit);
            xrtFree(Combined);
            if (!Text) { MdoEventsOwnedUnit(&Event); goto memory; }
            MdoSessionEventOwned Candidate = *Other; Candidate.Text = Text;
            size_t OldCost = MdoViewCost(Other), NewCost = MdoViewCost(&Candidate);
            size_t ByteLimit = AtomicReplies ? MDO_VIEW_HARD_BYTES : MDO_VIEW_BYTES;
            if (NewCost > ByteLimit - Bytes + OldCost) {
                xrtFree(Text); MdoEventsOwnedUnit(&Event);
                MdoSessionConversationPageNotice(Page, MDO_CONVERSATION_EVENT_LIMIT,
                    Snapshot->Count ? Snapshot->Events[Snapshot->Count-1u].Info.EventId : 0u, End);
                if (!Delta) End = RecordEnd;
                break;
            }
            Bytes = Bytes - OldCost + NewCost;
            xrtFree(Other->Text); Other->Text = Text; Other->Info.Text = Text;
            Other->Info.TextTruncated |= Event.Info.TextTruncated || A + B > Keep;
            if (Delta) Other->Info.AggregateEndId = Event.Info.EventId;
            else { Other->Info.AggregateEndId = Other->Info.AggregateEndId ? Other->Info.AggregateEndId : Other->Info.EventId;
                Other->Info.EventId = Event.Info.EventId; Other->Info.OccurredAt = Event.Info.OccurredAt; }
            Previous = Event.Info.EventId;
            if (Delta) Snapshot->NextCursor = Previous;
            MdoEventsOwnedUnit(&Event);
        } else {
            size_t Cost = MdoViewCost(&Event);
            size_t ByteLimit = AtomicReplies || !Snapshot->Count ? MDO_VIEW_HARD_BYTES : MDO_VIEW_BYTES;
            size_t EventLimit = AtomicReplies ? MDO_VIEW_HARD_EVENTS : MDO_VIEW_EVENTS;
            if (Snapshot->Count >= EventLimit || Cost > ByteLimit - Bytes) {
                MdoSessionConversationPageNotice(Page, MDO_CONVERSATION_EVENT_LIMIT,
                    Event.Info.EventId, Delta ? RecordStart : End);
                MdoEventsOwnedUnit(&Event);
                if (!Delta) End = RecordEnd;
                break;
            }
            if (!MdoEventsSnapshotGrow(Snapshot)) { MdoEventsOwnedUnit(&Event); goto memory; }
            Previous = Event.Info.EventId;
            if (Delta) Snapshot->NextCursor = Previous;
            if (MdoViewDelta(&Event.Info)) Streams[StreamCount++] = Snapshot->Count;
            Snapshot->Events[Snapshot->Count++] = Event; Bytes += Cost;
        }
        if (AtomicReplies && RootStart) {
            if (Replies >= Turns || Bytes >= MDO_VIEW_BYTES || Snapshot->Count >= MDO_VIEW_EVENTS) break;
            HaveReply = false; /* A resumed/reused run starts a distinct reply. */
        }
        if (!ReplyGroups && Start && ++Starts == Turns && !Delta) break;
    }
    if (!Delta && Scanned >= MDO_VIEW_SCAN && End)
        MdoSessionConversationPageNotice(Page, MDO_CONVERSATION_SCAN_LIMIT,
            Snapshot->Count ? Snapshot->Events[Snapshot->Count-1u].Info.EventId : 0u, End);
    if (Reader->Failed) goto io;
    if (!Delta) {
        /* Reverse scanning aggregates in reverse order; original event IDs
         * put interleaved reasoning/tasks back into their canonical order. */
        for (size_t i = 1u; i < Snapshot->Count; ++i) {
            MdoSessionEventOwned Event = Snapshot->Events[i]; size_t j = i;
            while (j && Snapshot->Events[j-1u].Info.EventId > Event.Info.EventId) {
                Snapshot->Events[j] = Snapshot->Events[j-1u]; --j;
            }
            Snapshot->Events[j] = Event;
        }
        Page->NextBefore = Snapshot->Count ? Snapshot->Events[0].Info.EventId : Before;
        Page->HasMore = End != 0u;
        Page->NextScanBefore = End; Page->HasScanBefore = true;
        /* Keep reply paging independent of input paging. A partial user turn
         * still needs its real input ID/text for fork and retry actions. Read
         * only that anchor, never insert old execution or an extra user row. */
        const MdoSessionEventInfo* FirstMain = NULL;
        for (size_t i = 0u; i < Snapshot->Count; ++i) {
            if (!Snapshot->Events[i].Info.AgentDepth) {
                FirstMain = &Snapshot->Events[i].Info; break;
            }
        }
        if (ReplyGroups && FirstMain &&
            FirstMain->Kind != MDO_SESSION_EVENT_HISTORY_TRUNCATED &&
            !(FirstMain->Kind == XWORK_EVENT_AGENT_START &&
              (FirstMain->UserMessageSequence || FirstMain->SchemaVersion < 3u))) {
            uint64 ContextEnd = End, ContextScanned = 0u, ContextStart = MdoMonotonicUs();
            while (ContextScanned < 32u * (uint64)MDO_VIEW_SCAN &&
                   MdoMonotonicUs()-ContextStart < 3000000u &&
                   MdoEventReaderPrevious(Reader, &ContextEnd, &Record)) {
                MdoSessionEventOwned Input;
                MdoConversationScalar Scalar;
                ContextScanned = End - ContextEnd;
                if (!MdoConversationScalarRead(Project, Session, Record, &Scalar)) {
                    xrtClearError(); continue;
                }
                if (Scalar.Info.Kind != MDO_SESSION_EVENT_HISTORY_TRUNCATED &&
                    !(Scalar.Info.Kind == XWORK_EVENT_AGENT_START && !Scalar.Info.AgentDepth)) continue;
                if (!MdoEventsParse(Project, Session, Record, &Input)) {
                    xrtClearError(); continue;
                }
                bool Boundary = Input.Info.Kind == MDO_SESSION_EVENT_HISTORY_TRUNCATED;
                bool RootInput = Input.Info.Kind == XWORK_EVENT_AGENT_START &&
                    !Input.Info.AgentDepth &&
                    (Input.Info.UserMessageSequence || Input.Info.SchemaVersion < 3u);
                if (RootInput && Input.Info.SchemaVersion >= 3u && Input.Info.UserMessageSequence &&
                    Input.Info.EventId < FirstMain->EventId) {
                    size_t Keep = MdoViewPrefix(Input.Text, 4096u);
                    if (Input.Text[Keep]) {
                        Input.Text[Keep] = '\0'; Input.Info.TextTruncated = true;
                    }
                    Snapshot->ActionPrompt = Input;
                    Page->ActionPrompt = Input.Info;
                    /* A thinking-only/retrying suffix has no public reply on
                     * which to attach action_prompt. Include its real input
                     * row, with its original durable identity, instead. */
                    bool HasReply = false;
                    for (size_t I=0u; I<Snapshot->Count; ++I)
                        if (!Snapshot->Events[I].Info.AgentDepth && Snapshot->Events[I].Info.Kind==XWORK_EVENT_MODEL_TEXT_DELTA)
                            HasReply = true;
                    if (!HasReply) {
                        MdoSessionEventOwned Prompt;
                        if (!MdoEventsParse(Project,Session,Record,&Prompt)) goto io;
                        if (!MdoEventsSnapshotGrow(Snapshot)) { MdoEventsOwnedUnit(&Prompt); goto memory; }
                        memmove(Snapshot->Events+1u,Snapshot->Events,Snapshot->Count*sizeof(*Snapshot->Events));
                        Snapshot->Events[0]=Prompt; ++Snapshot->Count;
                    }
                    break;
                }
                MdoEventsOwnedUnit(&Input);
                if (Boundary || RootInput) break;
            }
        }
    } else {
        Page->HasMore = Snapshot->NextCursor < Snapshot->LatestId;
        /* No event-ID progress through a corrupt prefix: recover from a cold
         * bounded suffix instead of returning a permanently stuck delta. */
        if (Page->HasMore && !Snapshot->Count && Scanned >= MDO_VIEW_SCAN) {
            MdoEventReaderClose(Reader); MdoSessionEventSnapshotRelease(Snapshot);
            return MdoReadConversationPage(Project, Session, 0u, 0u, "", Turns,
                ReplyGroups, false, 0u, Page, Error);
        }
    }
    if (Reader->Failed) goto io;
    if (!MdoViewCurrent(Path, Reader)) goto changed;
    MdoEventReaderClose(Reader); return Snapshot;
invalid:
    MdoEventsError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid conversation snapshot"); return NULL;
changed:
    MdoEventsError(Error, XWORK_ERROR_CONTEXT, "conversation changed while reading"); goto fail;
memory:
    MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot allocate conversation snapshot"); goto fail;
io:
    MdoEventsError(Error, XWORK_ERROR_IO, "cannot read conversation snapshot");
    goto fail;
fail:
    MdoEventReaderClose(Reader); MdoSessionEventSnapshotRelease(Snapshot); return NULL;
}

MdoSessionEventSnapshot* MdoSessionConversationPage(const char* Project,
    const char* Session, uint64 Before, uint64 After, const char* Epoch,
    size_t Turns, MdoConversationPageInfo* Page, xwork_error* Error)
{ return MdoReadConversationPage(Project, Session, Before, After, Epoch, Turns, false, false, 0u, Page, Error); }

MdoSessionEventSnapshot* MdoSessionConversationReplyPage(const char* Project,
    const char* Session, uint64 Before, uint64 After, const char* Epoch,
    size_t Replies, MdoConversationPageInfo* Page, xwork_error* Error)
{ return MdoReadConversationPage(Project, Session, Before, After, Epoch, Replies, true, false, 0u, Page, Error); }

MdoSessionEventSnapshot* MdoSessionConversationReplyPageAt(const char* Project,
    const char* Session, uint64 Before, uint64 After, const char* Epoch,
    size_t Replies, bool HasScanBefore, uint64 ScanBefore,
    MdoConversationPageInfo* Page, xwork_error* Error)
{ return MdoReadConversationPage(Project, Session, Before, After, Epoch, Replies, true,
    HasScanBefore, ScanBefore, Page, Error); }
