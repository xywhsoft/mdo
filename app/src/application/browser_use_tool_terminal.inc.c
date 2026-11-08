/* SDK tools have no enclosing Agent run to settle a cancelled TOOL_START.
 * Retain only this original call's scalar identity, never borrowed event text.
 * The retained owner also keeps the permission/effect boundary alive. */
typedef struct BuToolTerminal {
    xatomic32 Refs, State; /* 0 absent, 4 capturing, 1 started, 3 finishing, 2 done */
    BuToolBoundary* Boundary;
    char Call[257], Tool[129];
    xwork_event Start;
} BuToolTerminal;

static bool BuToolTerminalId(cstr Text,size_t Capacity){
    if(!Text||!Text[0])return false;
    for(size_t I=1;I<Capacity;I++)if(!Text[I])return true;
    return false;
}
static BuToolTerminal* BuToolTerminalCreate(BuToolBoundary* Boundary,cstr Call,cstr Tool){
    if(!BuToolTerminalId(Call,257)||!BuToolTerminalId(Tool,129))return NULL;
    BuToolTerminal* T=xrtCalloc(1,sizeof(*T));if(!T)return NULL;
    if(!BuToolBoundaryRetain(Boundary)){xrtFree(T);return NULL;}
    T->Boundary=Boundary;memcpy(T->Call,Call,strlen(Call)+1);memcpy(T->Tool,Tool,strlen(Tool)+1);
    xrtAtomic32Init(&T->Refs,1);xrtAtomic32Init(&T->State,0);return T;
}
static bool BuToolTerminalRetain(void* Data){
    BuToolTerminal* T=Data;if(!T)return false;
    uint32 Refs=xrtAtomic32Load(&T->Refs,XMEMORY_ACQUIRE);
    for(;;){uint32 Expected=Refs;if(!Refs||Refs==UINT32_MAX)return false;
        if(xrtAtomic32CompareExchange(&T->Refs,&Expected,Refs+1,XMEMORY_ACQ_REL,XMEMORY_ACQUIRE))return true;
        Refs=Expected;}
}
static void BuToolTerminalRelease(void* Data){
    BuToolTerminal* T=Data;if(T&&xrtAtomic32FetchSub(&T->Refs,1,XMEMORY_ACQ_REL)==1){
        BuToolBoundaryRelease(T->Boundary);xrtFree(T);}
}
static bool BuToolTerminalEvent(void* Data,const xwork_event* E){
    BuToolTerminal* T=Data;
    if(!T||!E||E->uAgentDepth||!E->sToolCallId||!E->sToolName||
       strcmp(T->Call,E->sToolCallId)||strcmp(T->Tool,E->sToolName))return true;
    if(E->eKind==XWORK_EVENT_TOOL_START){
        uint32 Expected=0;
        if(!xrtAtomic32CompareExchange(&T->State,&Expected,4,XMEMORY_ACQ_REL,XMEMORY_ACQUIRE))return true;
        T->Start.uAgentId=E->uAgentId;T->Start.uRunId=E->uRunId;T->Start.uAgentTurn=E->uAgentTurn;
        T->Start.uParentRunId=E->uParentRunId;T->Start.uTaskId=E->uTaskId;
        T->Start.uDelegationId=E->uDelegationId;T->Start.uParentAgentTurn=E->uParentAgentTurn;
        T->Start.uEffects=E->uEffects;
        xrtAtomic32Store(&T->State,1,XMEMORY_RELEASE);
    }else if(E->eKind==XWORK_EVENT_TOOL_DONE){
        uint32 State=xrtAtomic32Load(&T->State,XMEMORY_ACQUIRE);
        if((State==1||State==3)&&E->uAgentId==T->Start.uAgentId&&E->uRunId==T->Start.uRunId&&E->uAgentTurn==T->Start.uAgentTurn)
            xrtAtomic32Store(&T->State,2,XMEMORY_RELEASE);
    }
    return true;
}
static bool BuToolTerminalFinish(BuToolTerminal* T,MdoAgentSession* Agent,
    const mdo_sdk_context_v1* C,xwork_error* Error){
    uint32 Expected=1;
    if(!T)return true;
    if(!xrtAtomic32CompareExchange(&T->State,&Expected,3,XMEMORY_ACQ_REL,XMEMORY_ACQUIRE))return Expected==0||Expected==2;
    mdo_error_v1 Outcome={0};Outcome.Size=sizeof(Outcome);
    (void)BuToolExecutionFailure(C,T->Boundary,false,Error,&Outcome);
    char Text[768];snprintf(Text,sizeof(Text),"%s%s",Outcome.Message,
        Outcome.MayHaveCompleted?" Tool effects may have occurred; inspect them before retrying.":"");
    xwork_event End=T->Start;End.eKind=XWORK_EVENT_TOOL_DONE;
    End.sToolName=T->Tool;End.sToolCallId=T->Call;End.sText=Text;End.iTextLength=strlen(Text);
    End.iOccurredAtUs=MdoUtcUs();End.uSchemaVersion=XWORK_EVENT_SCHEMA_VERSION;
    /* No success/effect receipt is invented and no model history is appended. */
    return MdoAgentSessionRecordToolFailure(Agent,&End,Error);
}
