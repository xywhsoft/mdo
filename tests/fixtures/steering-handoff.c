#define ServiceInit SteeringProductInit
#include "generated/mdo_unity.c"
#undef ServiceInit
static unsigned Failed,Calls;
static xatomic32 ModelEntered;
static char* Workspace;
#define CHECK(X) do{if(!(X)){Failed++;printf("FAIL %u %s\n",__LINE__,#X);fflush(stdout);}}while(0)
static void Write(cstr Text){char* P=xrtPathJoin(Workspace,"tracked.txt");xfile F=xrtOpen(P,XFILE_WRITE|XFILE_CREATE|XFILE_TRUNCATE);CHECK(F&&xrtWriteAtFull(F,0,Text,strlen(Text),NULL));xrtClose(F);xrtFree(P);}
static xllm_result Model(void* U,const xllm_request* R,const xllm_stream_callbacks* C,xllm_response** Out,xllm_error* E){
    (void)U;(void)C;Calls++;
    if(Calls==1||Calls==3){
        Write(Calls==1?"cancelled first reply\n":"manual stop\n");xrtAtomic32Store(&ModelEntered,1,XMEMORY_RELEASE);
        uint64 End=MdoDeadlineAfterUs(5000000);
        while(!xrtCancelRequested(R->pCancel)&&!MdoDeadlineExpired(End))xrtSleep(5);
        xllmErrorInit(E);return XLLM_RESULT_CANCELLED;
    }
    Write("steered reply\n");
    xllm_response* Reply=calloc(1,sizeof(*Reply));Reply->sContent=strdup("Followed the new instruction.");Reply->sModel=strdup("fixture");
    Reply->uHttpStatus=200;Reply->sFinishReason=strdup("stop");Reply->eFinish=XLLM_FINISH_STOP;*Out=Reply;xllmErrorInit(E);return XLLM_RESULT_OK;
}
static MdoRunInfo Start(cstr Prompt){
    MdoRunStartOptions O;MdoRunStartOptionsInit(&O);O.ProjectId="qa";O.SessionId="steering";O.Prompt=Prompt;
    MdoRunInfo I={0};I.Size=sizeof(I);xwork_error E;CHECK(MdoRunStart(&O,&I,&E));return I;
}
static void Wait(MdoRunInfo* I,bool Cancel){
    uint64 End=MdoDeadlineAfterUs(15000000);xwork_error E;
    if(Cancel){while(!xrtAtomic32Load(&ModelEntered,XMEMORY_ACQUIRE)&&!MdoDeadlineExpired(End))xrtSleep(5);CHECK(MdoRunCancel(I->Id,NULL,&E));}
    do{CHECK(MdoRunManagerPump(NULL,&E));I->Size=sizeof(*I);CHECK(MdoRunGetInfo(I->Id,I));if(!I->Terminal)xrtSleep(10);}while(!I->Terminal&&!MdoDeadlineExpired(End));
    CHECK(I->Terminal);CHECK(I->State==(Cancel?XWORK_RUN_CANCELLED:XWORK_RUN_SUCCEEDED));
}
static xvalue* Route(bool (*Fn)(MdoApiContext*),xvalue* Body,unsigned Status){
    xhttp1head Head={0};Head.MethodCode=Body?XHTTP_METHOD_POST:XHTTP_METHOD_GET;
    xhttpfield Field={XRT_STR_LITERAL("Content-Type"),XRT_STR_LITERAL("application/json")};Head.Fields=&Field;Head.FieldCount=1;
    XS_HttpReq Req={0};Req.head=&Head;MdoApplicationResponse Response={0};MdoApiContext C={0};C.Request=&Req;
    C.ParamCount=2;C.Params[0]=XRT_STR_LITERAL("qa");C.Params[1]=XRT_STR_LITERAL("steering");C.ApplicationBody=Body;C.ApplicationResponse=&Response;
    CHECK(Fn(&C));if(Response.Status!=Status)printf("ROUTE status=%u expected=%u body=%s\n",Response.Status,Status,Response.Body?Response.Body:"");CHECK(Response.Status==Status);
    xvalue* V=Response.Body?xrtJsonParse(xrtStrViewN(Response.Body,Response.Bytes)):NULL;
    MdoQueueResponseUnit(&Response);return V;
}
static xvalue* Recovery(unsigned Status){return Route(MdoApiSessionRecoveryRoute,NULL,Status);}
static void Paused(bool Pause){
    char Path[MDO_SESSION_PATH_CAPACITY];MdoQueue Q;CHECK(MdoQueueRunPath(Path,"qa","steering"));xrtMutexLock(g_MdoQueueLock);
    CHECK(MdoQueueRead(Path,"qa","steering",&Q));Q.Paused=Pause;CHECK(MdoQueueWrite(Path,&Q));MdoQueueRelease(&Q);xrtMutexUnlock(g_MdoQueueLock);
}
void ServiceInit(XS_HostInfo* Host){
    SteeringProductInit(Host);CHECK(MdoBootstrapRuntime()!=NULL);MdoApiQueueStop();Workspace=xrtEnvGet("MDO_CHANGES_WORKSPACE");
    CHECK(MdoConfigImport(MDO_CONFIG_SETTINGS,XRT_STR_LITERAL("{\"schema_version\":1,\"patch\":{\"agent\":{\"memory\":false,\"web_search\":false}}}")));
    MdoRunManagerUnit();MdoRunManagerOptions Manager;MdoRunManagerOptionsInit(&Manager);Manager.Automatic=false;Manager.OnModelComplete=Model;xwork_error E;
    CHECK(MdoRunManagerInit(MdoBootstrapRuntime(),&Manager,&E));
    MdoProjectCreateOptions P;MdoProjectCreateOptionsInit(&P);P.Id="qa";P.Name="Steering fixture";P.WorkspaceRoot=Workspace;CHECK(MdoProjectCreate(&P,NULL,&E));
    MdoSessionCreateOptions O;MdoSessionCreateOptionsInit(&O);O.ProjectId="qa";O.RequestedId="steering";O.Title="Steering fixture";
    O.Agent.WorkspaceRoot=Workspace;O.Agent.AgentId="mdo.default";O.Agent.ModelId="history-fixture";O.Agent.Protocol=MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;O.Agent.PermissionProfile="full-access";O.Agent.OnModelComplete=Model;
    MdoSession* S=MdoSessionCreate(&O,&E);CHECK(S!=NULL);MdoSessionInfo Info={0};Info.Size=sizeof(Info);CHECK(MdoSessionGetInfo(S,&Info));MdoSessionRelease(S);
    MdoRunInfo First=Start("Original task");
    xvalue* Body=xrtJsonParse(XRT_STR_LITERAL("{\"id\":\"11111111111111111111111111111111\",\"text\":\"New instruction\",\"first\":true,\"priority\":true,\"managed\":true}"));
    xvalue* Reply=Route(MdoApiQueueRoute,Body,201);xrtValueRelease(Reply);xrtValueRelease(Body);
    Wait(&First,true);
    /* The queue owns the next input even when no worker currently owns the
     * runtime. A background recovery read must not offer or steal this turn. */
    Reply=Recovery(409);xrtValueRelease(Reply);
    Paused(true);Reply=Recovery(200);const xvalue* Data=SdkGet(Reply,"data");
    CHECK(SdkGet(Data,"resume_required")&&SdkField(Data,"recovery_token"));
    xvalue* Resume=xrtValueObject(),*Abandon=xrtValueObject();SdkPut(Resume,"recovery_token",SdkField(Data,"recovery_token"));xrtValueObjectSetNew(Resume,XRT_STR_LITERAL("decisions"),xrtValueArray());
    xrtValueObjectSet(Abandon,XRT_STR_LITERAL("revision"),SdkGet(Data,"revision"));xrtValueObjectSet(Abandon,XRT_STR_LITERAL("last_sequence"),SdkGet(Data,"last_sequence"));xrtValueRelease(Reply);
    Paused(false);Reply=Route(MdoApiSessionResumeRoute,Resume,409);xrtValueRelease(Reply);Reply=Route(MdoApiSessionAbandonRoute,Abandon,409);xrtValueRelease(Reply);xrtValueRelease(Resume);xrtValueRelease(Abandon);
    MdoQueueDispatchSession(&Info);
    MdoRunSnapshot* Runs=MdoRunSnapshotCreate(&E);MdoRunInfo Next={0};
    for(size_t i=0;i<MdoRunSnapshotCount(Runs);i++){MdoRunInfo I={0};I.Size=sizeof(I);if(MdoRunSnapshotAt(Runs,i,&I)&&strcmp(I.Id,First.Id))Next=I;}MdoRunSnapshotRelease(Runs);
    CHECK(Next.Id[0]);if(Next.Id[0])Wait(&Next,false);MdoQueueDispatchSession(&Info);CHECK(Calls==2);
    Reply=Recovery(200);bool Required=true;xrtValueGetBool(SdkGet(SdkGet(Reply,"data"),"resume_required"),&Required);CHECK(!Required);xrtValueRelease(Reply);
    /* Both captures are immutable and readable after cancellation/new input. */
    MdoSessionEventSnapshot* Events=MdoSessionEventReplay("qa","steering",0,256,&E);uint64 Turns[2]={0};unsigned Count=0;
    for(size_t i=0;i<MdoSessionEventSnapshotCount(Events);i++){MdoSessionEventInfo I={0};I.Size=sizeof(I);if(MdoSessionEventSnapshotAt(Events,i,&I)&&I.Kind==XWORK_EVENT_AGENT_START&&!I.AgentDepth&&Count<2)Turns[Count++]=I.EventId;}MdoSessionEventSnapshotRelease(Events);CHECK(Count==2);
    MdoApplicationInvocation Invocation={0};mdo_sdk_context_v1 C;MdoApplicationContext(&C,&Invocation,"local-client","qa-steer","qa","steering","",Workspace,0);
    for(unsigned i=0;i<Count;i++){xvalue* Params=xrtValueObject(),*Changes=NULL;SdkPut(Params,"scope","turn");SdkPutUInt(Params,"turn",Turns[i]);mdo_error_v1 Err={0};
        bool Ok=SdkChangesCall(&C,"changes.list",Params,&Changes,&Err);if(!Ok)printf("CHANGES %s %s\n",Err.Code,Err.Message);CHECK(Ok);CHECK(xrtValueCount(SdkGet(Changes,"files"))==1);xrtValueRelease(Params);xrtValueRelease(Changes);}
    xrtAtomic32Store(&ModelEntered,0,XMEMORY_RELEASE);MdoRunInfo Manual=Start("Manual stop");Wait(&Manual,true);
    Reply=Recovery(200);Required=false;xrtValueGetBool(SdkGet(SdkGet(Reply,"data"),"resume_required"),&Required);CHECK(Required);xrtValueRelease(Reply);
    /* A normal non-Git workspace has no review, not a broken review. */
    O.ProjectId="default";O.RequestedId="plain";O.Agent.WorkspaceRoot=NULL;
    S=MdoSessionCreate(&O,&E);CHECK(S!=NULL);MdoSessionRelease(S);
    MdoRunStartOptions Plain;MdoRunStartOptionsInit(&Plain);Plain.ProjectId="default";Plain.SessionId="plain";Plain.Prompt="Plain task";
    MdoRunInfo PlainRun={0};PlainRun.Size=sizeof(PlainRun);CHECK(MdoRunStart(&Plain,&PlainRun,&E));Wait(&PlainRun,false);
    Events=MdoSessionEventReplay("default","plain",0,1,&E);MdoSessionEventInfo Anchor={0};Anchor.Size=sizeof(Anchor);CHECK(MdoSessionEventSnapshotAt(Events,0,&Anchor));MdoSessionEventSnapshotRelease(Events);
    char* PlainWorkspace=MdoHomeDefaultWorkspacePath(false);MdoApplicationContext(&C,&Invocation,"local-client","qa-plain","default","plain","",PlainWorkspace,0);
    xvalue* Params=xrtValueObject(),*Changes=NULL;SdkPut(Params,"scope","turn");SdkPutUInt(Params,"turn",Anchor.EventId);mdo_error_v1 Err={0};
    CHECK(!SdkChangesCall(&C,"changes.list",Params,&Changes,&Err));if(strcmp(Err.Code,"git_not_repository"))printf("PLAIN REVIEW ERROR %s %s\n",Err.Code,Err.Message);CHECK(!strcmp(Err.Code,"git_not_repository"));
    char ReceiptPath[512];CHECK(SdkGitTurnPath(&C,Anchor.EventId,ReceiptPath));bool Missing=false;char* Old=MdoExtensionRead(ReceiptPath,true,16384,&Missing);xvalue* Legacy=SdkParse(Old);xrtFree(Old);
    CHECK(Legacy!=NULL);SdkPut(Legacy,"reason","git_revision_invalid");char* Json=SdkJson(Legacy);CHECK(Json&&MdoHomeAtomicWrite(ReceiptPath,Json,strlen(Json),false));xrtFree(Json);xrtValueRelease(Legacy);
    memset(&Err,0,sizeof(Err));CHECK(!SdkChangesCall(&C,"changes.list",Params,&Changes,&Err));CHECK(!strcmp(Err.Code,"git_not_repository"));
    xrtValueRelease(Params);xrtValueRelease(Changes);xrtFree(PlainWorkspace);
    printf("steering_failed=%u calls=%u probe_done=1\n",Failed,Calls);fflush(stdout);
}
