/* The before-tool hook runs only after permission admission. Its small owned
 * marker outlives the SDK driver when a retained Agent/subagent owner needs it.
 * Cancellation before permission cannot have dispatched this tool. A failure
 * after admission must retain uncertainty, even if the executor lost its result. */
typedef struct BuToolBoundary {xatomic32 Refs; xatomic32 Entered;} BuToolBoundary;
static BuToolBoundary* BuToolBoundaryCreate(void){
    BuToolBoundary* B=xrtCalloc(1,sizeof(*B));if(!B)return NULL;
    xrtAtomic32Init(&B->Refs,1u);xrtAtomic32Init(&B->Entered,0u);return B;
}
static bool BuToolBoundaryRetain(void* Data){
    BuToolBoundary* B=Data;if(!B)return false;
    uint32 Refs=xrtAtomic32Load(&B->Refs,XMEMORY_ACQUIRE);
    for(;;){
        if(!Refs||Refs==UINT32_MAX)return false;
        uint32 Expected=Refs;
        if(xrtAtomic32CompareExchange(&B->Refs,&Expected,Refs+1u,XMEMORY_ACQ_REL,XMEMORY_ACQUIRE))return true;
        Refs=Expected;
    }
}
static void BuToolBoundaryRelease(void* Data){
    BuToolBoundary* B=Data;if(B&&xrtAtomic32FetchSub(&B->Refs,1u,XMEMORY_ACQ_REL)==1u)xrtFree(B);
}
static xwork_hook_action BuToolBoundaryHook(void* Data,const xwork_hook_event* Event){
    if(Data&&Event&&Event->ePhase==XWORK_HOOK_BEFORE_TOOL)
        xrtAtomic32Store(&((BuToolBoundary*)Data)->Entered,1u,XMEMORY_RELEASE);
    return XWORK_HOOK_CONTINUE;
}
static bool BuToolExecutionFailure(const mdo_sdk_context_v1* C,const BuToolBoundary* Boundary,
    bool HasResult,const xwork_error* Error,mdo_error_v1* E){
    MdoApplicationInvocation* I=C?C->Opaque:NULL;
    bool Cancelled=I&&I->Cancel&&xrtCancelRequested(I->Cancel);
    bool Timed=C&&C->Deadline&&MdoDeadlineExpired(C->Deadline);
    bool Possible=HasResult||(Boundary&&xrtAtomic32Load(&Boundary->Entered,XMEMORY_ACQUIRE)!=0u);
    const char* Message=Cancelled?(Possible?"Managed tool was cancelled; inspect any dispatched effects before retrying":"Managed tool was cancelled before execution"):
        Timed?(Possible?"Managed tool deadline expired; inspect any dispatched effects before retrying":"Managed tool deadline expired before execution"):
        Error&&Error->sMessage[0]?Error->sMessage:"Managed tool execution did not retain an outcome";
    bool Result=MdoApplicationError(E,Cancelled?"sdk_cancelled":Timed?"sdk_deadline_expired":"sdk_tool_execution_failed",
        Message,"tool",Cancelled?499:Timed?504:409,Possible);
    if(E&&(Cancelled||Timed)&&Error&&Error->sMessage[0])snprintf(E->Cause,sizeof(E->Cause),"%s",Error->sMessage);
    return Result;
}
