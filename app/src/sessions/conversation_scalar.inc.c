/* Navigation needs only these scalar fields and a short text prefix. SAX
 * still validates the complete JSON/UTF-8 record, without constructing and
 * freeing a DOM with dozens of unrelated values for every token event. */
typedef struct MdoConversationScalar {
    MdoSessionEventInfo Info;
    char Text[MDO_CONVERSATION_SUMMARY_BYTES+1u];
    const char* Project;
    const char* Session;
    uint32 Seen;
} MdoConversationScalar;

static xjsonvisitaction MdoConversationScalarVisit(const xjsonevent* E, void* Data)
{
    MdoConversationScalar* S=Data;
    if (!E->HasName || E->Depth!=1u) return XJSON_VISIT_NEXT;
    const char* Names[]={"schema_version","event_id","occurred_at_us","project_id","session_id",
        "kind","agent_turn","user_message_sequence","agent_depth","run_id","text","success"};
    size_t I=0u;
    for (;I<sizeof(Names)/sizeof(*Names);++I) if(xrtStrEqual(E->Name,xrtStrView(Names[I]))) break;
    if (I==sizeof(Names)/sizeof(*Names)) return XJSON_VISIT_NEXT;
    uint32 Bit=1u<<I;
    if (S->Seen&Bit) return XJSON_VISIT_FAIL;
    S->Seen|=Bit;
    if (I==3u || I==4u || I==10u) {
        if (E->Type!=XJSON_EVENT_STRING || memchr(E->Value.String.Data,0,E->Value.String.Size)) return XJSON_VISIT_FAIL;
        if (I!=10u) return xrtStrEqual(E->Value.String,xrtStrView(I==3u?S->Project:S->Session))?XJSON_VISIT_NEXT:XJSON_VISIT_FAIL;
        size_t N=E->Value.String.Size;
        if (N>MDO_CONVERSATION_SUMMARY_BYTES) {
            N=MDO_CONVERSATION_SUMMARY_BYTES;
            while(N&&(((unsigned char)E->Value.String.Data[N]&0xc0u)==0x80u))--N;
        }
        memcpy(S->Text,E->Value.String.Data,N);S->Text[N]=0;
        return XJSON_VISIT_NEXT;
    }
    if (I==11u) {
        if(E->Type!=XJSON_EVENT_BOOL)return XJSON_VISIT_FAIL;
        S->Info.Success=E->Value.Boolean;return XJSON_VISIT_NEXT;
    }
    if (I==2u) {
        if(E->Type==XJSON_EVENT_INT)S->Info.OccurredAt=E->Value.Integer;
        else if(E->Type==XJSON_EVENT_UINT&&E->Value.Unsigned<=INT64_MAX)S->Info.OccurredAt=(int64)E->Value.Unsigned;
        else return XJSON_VISIT_FAIL;
        return XJSON_VISIT_NEXT;
    }
    uint64 N;
    if(E->Type==XJSON_EVENT_UINT)N=E->Value.Unsigned;
    else if(E->Type==XJSON_EVENT_INT&&E->Value.Integer>=0)N=(uint64)E->Value.Integer;
    else return XJSON_VISIT_FAIL;
    switch(I){
    case 0: if(!N||N>MDO_SESSION_EVENT_SCHEMA)return XJSON_VISIT_FAIL;S->Info.SchemaVersion=(uint32)N;break;
    case 1: if(!N)return XJSON_VISIT_FAIL;S->Info.EventId=N;break;
    case 5: if(N>MDO_AGENT_EVENT_MODEL_RETRY)return XJSON_VISIT_FAIL;S->Info.Kind=(xwork_event_kind)N;break;
    case 6:S->Info.AgentTurn=N;break;
    case 7:S->Info.UserMessageSequence=N;break;
    case 8:if(N>UINT32_MAX)return XJSON_VISIT_FAIL;S->Info.AgentDepth=(uint32)N;break;
    case 9:S->Info.RunId=N;break;
    }
    return XJSON_VISIT_NEXT;
}

/* Native canonical journals put this bounded numeric/owner header before
 * payloads. Irrelevant reasoning text only needs allocation-free full JSON
 * validation, not dozens of decoded SAX name/value callbacks. Noncanonical,
 * legacy and malformed prefixes retain the general SAX path. */
static bool MdoConversationLiteral(const char** P,const char* End,const char* Text)
{
    size_t N=strlen(Text);
    if ((size_t)(End-*P)<N || memcmp(*P,Text,N))return false;
    *P+=N;return true;
}
static bool MdoConversationNumber(const char** P,const char* End,uint64* Value)
{
    const char* Start=*P;uint64 N=0u;
    while(*P<End&&**P>='0'&&**P<='9'){
        unsigned D=(unsigned)(**P-'0');if(N>(UINT64_MAX-D)/10u)return false;N=N*10u+D;++*P;
    }
    *Value=N;return *P>Start;
}
static bool MdoConversationReasoningHeader(xstrview Json,MdoConversationScalar* S)
{
    const char* P=Json.Data;const char* End=P+Json.Size;
    uint64 Schema,Id,Unused,Time,Kind;
    if(!MdoConversationLiteral(&P,End,"{\"schema_version\":")||!MdoConversationNumber(&P,End,&Schema)||!Schema||Schema>MDO_SESSION_EVENT_SCHEMA||
       !MdoConversationLiteral(&P,End,",\"event_id\":")||!MdoConversationNumber(&P,End,&Id)||!Id||
       !MdoConversationLiteral(&P,End,",\"source_event_id\":")||!MdoConversationNumber(&P,End,&Unused)||
       !MdoConversationLiteral(&P,End,",\"occurred_at_us\":")||!MdoConversationNumber(&P,End,&Time)||Time>INT64_MAX||
       !MdoConversationLiteral(&P,End,",\"project_id\":\"")||!MdoConversationLiteral(&P,End,S->Project)||
       !MdoConversationLiteral(&P,End,"\",\"session_id\":\"")||!MdoConversationLiteral(&P,End,S->Session)||
       !MdoConversationLiteral(&P,End,"\",\"kind\":")||!MdoConversationNumber(&P,End,&Kind)||
       Kind!=XWORK_EVENT_MODEL_REASONING_DELTA||!MdoConversationLiteral(&P,End,",")||!xrtJsonValid(Json))return false;
    S->Info.EventId=Id;S->Info.SchemaVersion=(uint32)Schema;S->Info.Kind=XWORK_EVENT_MODEL_REASONING_DELTA;
    return true;
}

static bool MdoConversationScalarRead(const char* Project,const char* Session,xstrview Json,MdoConversationScalar* S)
{
    memset(S,0,sizeof(*S));S->Project=Project;S->Session=Session;S->Info.Text=S->Text;
    if(MdoConversationReasoningHeader(Json,S))return true;
    xjsonreadconfig C;xrtJsonReadConfigInit(&C);C.MaxDepth=6u;C.MaxInputBytes=MDO_SESSION_EVENT_RECORD_LIMIT;
    C.MaxValues=64u;C.MaxContainerItems=40u;
    if(xrtJsonVisit(Json,&C,MdoConversationScalarVisit,S)!=XJSON_VISIT_DONE)return false;
    uint32 Required=(1u<<12u)-1u;
    if(S->Info.SchemaVersion<3u)Required&=~(1u<<7u);
    return (S->Seen&Required)==Required;
}
