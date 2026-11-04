/* Stream text is volatile until a semantic boundary. Live previews are
 * replacement snapshots, never journal appends; a crash may lose the current
 * unfinished event. Completed long events are split only after completion. */
#define MDO_STREAM_MEMORY_LIMIT (32u * 1024u * 1024u)
#define MDO_STREAM_EVENT_LIMIT (16u * 1024u * 1024u)
#define MDO_STREAM_COUNT_LIMIT 64u

typedef struct MdoEventStream {
    struct MdoEventStream* Next;
    xvalue* Record;
    char* Text;
    size_t Size, Capacity, Written, BaseBytes;
    uint64 Agent, Run, Turn, First, LastPreview, LastPublish;
    uint32 Depth;
    xwork_event_kind Kind;
    MdoSessionEventSource Source;
} MdoEventStream;

static bool MdoEventsStreamDelta(const xwork_event* Event)
{
    return Event->eKind == XWORK_EVENT_MODEL_TEXT_DELTA ||
        Event->eKind == XWORK_EVENT_MODEL_REASONING_DELTA;
}

static size_t MdoEventsStreamPrefix(const MdoEventStream* Stream, size_t Offset)
{
    size_t N=0u, Cost=0u, Budget=MDO_SESSION_EVENT_RECORD_LIMIT-Stream->BaseBytes-512u;
    while (Offset+N<Stream->Size && N<MDO_SESSION_EVENT_TEXT_LIMIT) {
        unsigned char C=(unsigned char)Stream->Text[Offset+N];
        size_t Add=C<32u?6u:(C=='"'||C=='\\')?2u:1u;
        if (Add>Budget-Cost) break;
        Cost+=Add; ++N;
    }
    while (Offset+N<Stream->Size && N && (((unsigned char)Stream->Text[Offset+N]&0xc0u)==0x80u)) --N;
    return N;
}

static char* MdoEventsStreamJson(MdoSessionEventBridge* Bridge, MdoEventStream* Stream,
    size_t Offset, size_t Count, bool Volatile, size_t* Size)
{
    xvalue* Range=xrtValueObject();
    bool Ok=Range && MdoEventsObjectTake(Range,"first",xrtValueUInt(Stream->First)) &&
        MdoEventsObjectTake(Range,"through",xrtValueUInt(Stream->LastPreview)) &&
        MdoEventsObjectTake(Range,"volatile",xrtValueBool(Volatile)) &&
        MdoEventsObjectTake(Stream->Record,"stream",Range);
    if (!Ok) return NULL;
    Ok=MdoEventsObjectTake(Stream->Record,"event_id",xrtValueUInt(Bridge->NextEventId)) &&
        MdoEventsObjectString(Stream->Record,"text",Stream->Text+Offset,Count) &&
        MdoEventsObjectTake(Stream->Record,"text_truncated",xrtValueBool(Volatile && Count<Stream->Size));
    return Ok?xrtJsonStringify(Stream->Record,false,Size):NULL;
}

static bool MdoEventsStreamCommit(MdoSessionEventBridge* Bridge, MdoEventStream* Stream)
{
    while (Stream->Written<Stream->Size) {
        size_t Count=MdoEventsStreamPrefix(Stream,Stream->Written), Size=0u;
        if (!Count || !Bridge->NextEventId || Bridge->NextEventId==UINT64_MAX) return false;
        char* Json=MdoEventsStreamJson(Bridge,Stream,Stream->Written,Count,false,&Size);
        bool Ok=Json && Size<=MDO_SESSION_EVENT_RECORD_LIMIT && MdoEventsAppendBytes(Bridge->Path,Json,Size);
        if (Ok) {
            MdoSessionsInternalEventIssued(Bridge->ProjectId,Bridge->SessionId,Bridge->NextEventId,true);
            MdoSessionsInternalPublish(Bridge->ProjectId,Bridge->SessionId,xrtStrViewN(Json,Size),false);
            ++Bridge->NextEventId; Stream->Written+=Count;
        }
        xrtFree(Json); if (!Ok) return false;
    }
    if ( Stream->Size && !Stream->Depth && Stream->Kind==XWORK_EVENT_MODEL_TEXT_DELTA )
        (void)MdoSessionRecordActivity(Bridge->ProjectId,Bridge->SessionId,true,MDO_SESSION_OUTCOME_KEEP);
    return true;
}

static void MdoEventsStreamFree(MdoSessionEventBridge* Bridge, MdoEventStream* Stream)
{
    Bridge->StreamBytes-=Stream->Capacity;
    xrtValueRelease(Stream->Record); xrtFree(Stream->Text); xrtFree(Stream);
}

static void MdoEventsStreamDiscard(MdoSessionEventBridge* Bridge)
{
    while (Bridge->Streams) {
        MdoEventStream* Stream=Bridge->Streams; Bridge->Streams=Stream->Next;
        MdoEventsStreamFree(Bridge,Stream);
    }
}

static bool MdoEventsStreamFinish(MdoSessionEventBridge* Bridge, const xwork_event* Event)
{
    bool All=(Event->eKind==XWORK_EVENT_AGENT_START && !Event->uAgentDepth) ||
        (Event->eKind==XWORK_EVENT_AGENT_DONE && !Event->uAgentDepth);
    bool Boundary=All || Event->eKind==XWORK_EVENT_MODEL_START || Event->eKind==XWORK_EVENT_MODEL_DONE ||
        Event->eKind==XWORK_EVENT_TOOL_START || Event->eKind==XWORK_EVENT_COMPACTION_START ||
        Event->eKind==XWORK_EVENT_AGENT_DONE || Event->eKind==XWORK_EVENT_ERROR ||
        (uint32)Event->eKind==MDO_AGENT_EVENT_MODEL_RETRY;
    if (!Boundary) return true;
    MdoEventStream** Link=&Bridge->Streams;
    while (*Link) {
        MdoEventStream* Stream=*Link;
        if (!All && (Stream->Agent!=Event->uAgentId || Stream->Run!=Event->uRunId || Stream->Depth!=Event->uAgentDepth)) {
            Link=&Stream->Next; continue;
        }
        if (!MdoEventsStreamCommit(Bridge,Stream)) return false;
        *Link=Stream->Next; MdoEventsStreamFree(Bridge,Stream);
    }
    return true;
}

static bool MdoEventsStreamAppend(MdoSessionEventBridge* Bridge, const xwork_event* Event)
{
    if (!MdoEventsStreamDelta(Event)) return MdoEventsAppend(Bridge,Event);
    size_t Size=Event->iTextLength;
    if (Size==SIZE_MAX) Size=Event->sText?strlen(Event->sText):0u;
    if (!Size) return true;
    if (!Event->sText || Size>MDO_STREAM_EVENT_LIMIT || Event->bTextTruncated ||
        !xrtUtf8Valid(xrtStrViewN(Event->sText,Size),NULL)) return false;
    MdoEventStream* Stream; size_t Count=0u;
    for (Stream=Bridge->Streams;Stream;Stream=Stream->Next) {
        ++Count;
        if (Stream->Agent==Event->uAgentId && Stream->Run==Event->uRunId &&
            Stream->Depth==Event->uAgentDepth && Stream->Turn==Event->uAgentTurn && Stream->Kind==Event->eKind &&
            !strcmp(Stream->Source.Namespace,Bridge->Source.Namespace) && !strcmp(Stream->Source.Member,Bridge->Source.Member) &&
            !strcmp(Stream->Source.Agent,Bridge->Source.Agent)) break;
    }
    if (!Stream) {
        if (Count>=MDO_STREAM_COUNT_LIMIT) return false;
        Stream=xrtCalloc(1u,sizeof(*Stream)); if (!Stream) return false;
        MdoEventStream** Tail=&Bridge->Streams; while (*Tail) Tail=&(*Tail)->Next; *Tail=Stream;
    }
    if (!Stream->Record) {
        xwork_event Base=*Event; Base.sText=""; Base.iTextLength=0u;
        size_t Bytes=0u; bool Main=!Event->uAgentDepth && Event->tSessionStats.uContextWindowTokens;
        char* Json=MdoEventsRecord(Bridge,Bridge->NextEventId,&Base,Main?Bridge->ModelId:"",
            Main?Event->tSessionStats.uContextWindowTokens:0u,&Bridge->Source,&Bytes);
        Stream->Record=Json?xrtJsonParse(xrtStrViewN(Json,Bytes)):NULL; xrtFree(Json);
        if (!Stream->Record || Bytes+512u>=MDO_SESSION_EVENT_RECORD_LIMIT) return false;
        Stream->BaseBytes=Bytes; Stream->Agent=Event->uAgentId; Stream->Run=Event->uRunId;
        Stream->Turn=Event->uAgentTurn; Stream->Depth=Event->uAgentDepth; Stream->Kind=Event->eKind;
        Stream->Source=Bridge->Source; Stream->First=Bridge->NextEventId;
    }
    if (Size>MDO_STREAM_EVENT_LIMIT-Stream->Size) return false;
    size_t Need=Stream->Size+Size;
    if (Need>Stream->Capacity) {
        size_t Capacity=Stream->Capacity?Stream->Capacity:4096u;
        while (Capacity<Need) Capacity*=2u;
        if (Capacity-Stream->Capacity>MDO_STREAM_MEMORY_LIMIT-Bridge->StreamBytes) return false;
        char* Text=xrtRealloc(Stream->Text,Capacity); if (!Text) return false;
        Bridge->StreamBytes+=Capacity-Stream->Capacity; Stream->Capacity=Capacity; Stream->Text=Text;
    }
    memcpy(Stream->Text+Stream->Size,Event->sText,Size); Stream->Size=Need;
    uint64 Now=MdoMonotonicUs();
    if (!Stream->LastPublish || Now-Stream->LastPublish>=50000u) {
        if (!Bridge->NextEventId || Bridge->NextEventId==UINT64_MAX) return false;
        size_t Bytes=0u, Prefix=MdoEventsStreamPrefix(Stream,0u);
        char* Json=MdoEventsStreamJson(Bridge,Stream,0u,Prefix,true,&Bytes);
        if (!Json || Bytes>MDO_SESSION_EVENT_RECORD_LIMIT) { xrtFree(Json); return false; }
        MdoSessionsInternalEventIssued(Bridge->ProjectId,Bridge->SessionId,Bridge->NextEventId,false);
        MdoSessionsInternalPublish(Bridge->ProjectId,Bridge->SessionId,xrtStrViewN(Json,Bytes),false);
        Stream->LastPreview=Bridge->NextEventId++; Stream->LastPublish=Now;
        xrtFree(Json);
    }
    return true;
}
