/* Display projection, not model context. Recent content is read in one file
 * view instead of replaying 32 events per network round trip. Each operation
 * has bounded retained memory and scan work; very long turns have continuations.
 * No sidecar, migration or write is needed to browse an old conversation. */
#define MDO_VIEW_EVENTS 96u
#define MDO_VIEW_BYTES (96u * 1024u)
#define MDO_VIEW_SCAN (16u * 1024u * 1024u)
#define MDO_VIEW_TEXT 8192u

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

MdoSessionEventSnapshot* MdoSessionConversationPage(const char* Project,
    const char* Session, uint64 Before, uint64 After, const char* Epoch,
    size_t Turns, MdoConversationPageInfo* Page, xwork_error* Error)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoSessionEventSnapshot* Snapshot = NULL;
    MdoEventReader* Reader = NULL;
    xstrview Record; uint64 End, Scanned = 0u, Previous = 0u;
    size_t Bytes = 0u, Starts = 0u, Streams[MDO_VIEW_EVENTS], StreamCount = 0u;
    bool Delta;
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
    memcpy(Snapshot->Epoch, Page->Epoch, sizeof(Snapshot->Epoch));
    Snapshot->LatestId = MdoEventReaderLatest(Reader, Project, Session, &Snapshot->HistoryLost);
    Delta = !Before && Epoch && *Epoch && !strcmp(Epoch, Page->Epoch) && After <= Snapshot->LatestId;
    Page->Delta = Delta;
    Snapshot->NextCursor = Delta ? After : Snapshot->LatestId;
    End = Reader->Size;
    if (Before) {
        MdoEventReaderAfter(Reader, Project, Session, Before - 1u);
        while (Reader->Position < Reader->Size) {
            uint64 At = Reader->Position; MdoSessionEventOwned Event;
            if (!MdoEventReaderNext(Reader, &Record)) break;
            if (!MdoEventsParse(Project, Session, Record, &Event)) { Snapshot->HistoryLost = true; xrtClearError(); continue; }
            bool Found = Event.Info.EventId >= Before;
            MdoEventsOwnedUnit(&Event);
            if (Found) { End = At; break; }
        }
    } else if (Delta) MdoEventReaderAfter(Reader, Project, Session, After);

    while (Scanned < MDO_VIEW_SCAN && (Delta ? MdoEventReaderNext(Reader, &Record) :
            MdoEventReaderPrevious(Reader, &End, &Record))) {
        MdoSessionEventOwned Event; size_t Merge = SIZE_MAX;
        Scanned += Record.Size + 1u;
        if (!MdoEventsParse(Project, Session, Record, &Event)) {
            Snapshot->HistoryLost = true; xrtClearError(); continue;
        }
        if (Delta && Event.Info.EventId <= After) { MdoEventsOwnedUnit(&Event); continue; }
        if ((Previous && (Delta ? Event.Info.EventId <= Previous : Event.Info.EventId >= Previous)) ||
            Event.Info.EventId > Snapshot->LatestId) { MdoEventsOwnedUnit(&Event); goto changed; }
        if (Event.Info.Kind == XWORK_EVENT_AGENT_START || Event.Info.Kind == XWORK_EVENT_MODEL_START ||
            Event.Info.Kind == XWORK_EVENT_MODEL_DONE) StreamCount = 0u;
        bool Start = Event.Info.Kind == XWORK_EVENT_AGENT_START && Event.Info.AgentDepth == 0u &&
            (Event.Info.UserMessageSequence || Event.Info.SchemaVersion < 3u);
        if (Delta && Start && Starts == Turns) { MdoEventsOwnedUnit(&Event); break; }
        size_t Prefix = MdoViewPrefix(Event.Text, MdoViewDelta(&Event.Info) ? MDO_VIEW_TEXT : 4096u);
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
            char Combined[MDO_VIEW_TEXT * 2u + 1u];
            if (Delta) { memcpy(Combined, Other->Text, A); memcpy(Combined + A, Event.Text, B + 1u); }
            else { memcpy(Combined, Event.Text, B); memcpy(Combined + B, Other->Text, A + 1u); }
            size_t Keep = MdoViewPrefix(Combined, MDO_VIEW_TEXT);
            char* Text = MdoEventsCopy(xrtStrViewN(Combined, Keep), MDO_VIEW_TEXT);
            if (!Text) { MdoEventsOwnedUnit(&Event); goto memory; }
            MdoSessionEventOwned Candidate = *Other; Candidate.Text = Text;
            size_t OldCost = MdoViewCost(Other), NewCost = MdoViewCost(&Candidate);
            if (NewCost > MDO_VIEW_BYTES - Bytes + OldCost) {
                xrtFree(Text); MdoEventsOwnedUnit(&Event); break;
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
            if (Snapshot->Count >= MDO_VIEW_EVENTS || (Snapshot->Count && Cost > MDO_VIEW_BYTES - Bytes)) {
                MdoEventsOwnedUnit(&Event); break;
            }
            if (!MdoEventsSnapshotGrow(Snapshot)) { MdoEventsOwnedUnit(&Event); goto memory; }
            Previous = Event.Info.EventId;
            if (Delta) Snapshot->NextCursor = Previous;
            if (MdoViewDelta(&Event.Info)) Streams[StreamCount++] = Snapshot->Count;
            Snapshot->Events[Snapshot->Count++] = Event; Bytes += Cost;
        }
        if (Start && ++Starts == Turns && !Delta) break;
    }
    if (Reader->Failed) goto io;
    if (!MdoViewCurrent(Path, Reader)) goto changed;
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
    } else Page->HasMore = Snapshot->NextCursor < Snapshot->LatestId;
    MdoEventReaderClose(Reader); return Snapshot;
invalid:
    MdoEventsError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid conversation snapshot"); return NULL;
changed:
    MdoEventsError(Error, XWORK_ERROR_CONTEXT, "conversation changed while reading"); goto fail;
memory:
    MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot allocate conversation snapshot"); goto fail;
io:
    MdoEventsError(Error, XWORK_ERROR_IO, "cannot read conversation snapshot");
fail:
    MdoEventReaderClose(Reader); MdoSessionEventSnapshotRelease(Snapshot); return NULL;
}
