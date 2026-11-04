#define ServiceInit TurnChangesProductInit
#include "generated/mdo_unity.c"
#undef ServiceInit
static unsigned Failed,Calls;
static char* Workspace;
#define CHECK(X) do{if(!(X)){Failed++;printf("FAIL %u %s\n",__LINE__,#X);fflush(stdout);}}while(0)
static void Write(cstr Name,cstr Text){char* Path=xrtPathJoin(Workspace,Name);xfile F=xrtOpen(Path,XFILE_WRITE|XFILE_CREATE|XFILE_TRUNCATE);CHECK(F&&xrtWriteAtFull(F,0,Text,strlen(Text),NULL));xrtClose(F);xrtFree(Path);}
static void Git(const cstr* Args,size_t Count){
    xprocessconfig C;xrtProcessConfigInit(&C);C.Program="git";C.Args=Args;C.ArgCount=Count;C.WorkDir=Workspace;C.HideWindow=true;
    xprocessrunoptions O;xrtProcessRunOptionsInit(&O);O.Timeout=5000;xprocessresult R={0};CHECK(xrtProcessRun(&C,&O,&R)&&R.Status.Code==0);xrtProcessResultUnit(&R);
}
static xllm_result Model(void* User,const xllm_request* Request,const xllm_stream_callbacks* Callbacks,xllm_response** Out,xllm_error* Error){
    (void)User;(void)Request;(void)Callbacks;Calls++;
    if(Calls==1){
        Write("tracked.txt","first reply\n");Write("新增文件.txt","new text\n");Write("third.txt","third\n");Write("fourth.txt","fourth\n");
        char* Path=xrtPathJoin(Workspace,"removed.txt");CHECK(remove(Path)==0);xrtFree(Path);
        const cstr Add[]={"add","--all"};Git(Add,2);
        const cstr Commit[]={"-c","user.name=QA","-c","user.email=qa@example.invalid","commit","-m","committed during reply"};Git(Commit,sizeof(Commit)/sizeof(*Commit));
    }else if(Calls==2)Write("tracked.txt","second reply\n");
    xllm_response* R=calloc(1,sizeof(*R));R->sContent=strdup("Completed offline changes.");R->sModel=strdup("fixture");R->uHttpStatus=200;
    R->sFinishReason=strdup("stop");R->eFinish=XLLM_FINISH_STOP;*Out=R;xllmErrorInit(Error);return XLLM_RESULT_OK;
}
static void Run(cstr Session){
    MdoRunStartOptions O;MdoRunStartOptionsInit(&O);O.ProjectId="qa";O.SessionId=Session;O.Prompt="Perform offline file fixture";
    MdoRunInfo Info={0};Info.Size=sizeof(Info);xwork_error E;CHECK(MdoRunStart(&O,&Info,&E));
    if(!Info.Id[0]){printf("START ERROR %s\n",E.sMessage);return;}
    uint64 End=MdoDeadlineAfterUs(15000000);
    do{CHECK(MdoRunManagerPump(NULL,&E));Info.Size=sizeof(Info);CHECK(MdoRunGetInfo(Info.Id,&Info));if(!Info.Terminal)xrtSleep(10);}while(!Info.Terminal&&!MdoDeadlineExpired(End));
    CHECK(Info.Terminal&&Info.Result==XWORK_RESULT_OK);
}
static xvalue* Read(mdo_sdk_context_v1* C,uint64 Turn,cstr Path){
    xvalue* P=xrtValueObject(),*R=NULL;SdkPut(P,"scope","turn");SdkPutUInt(P,"turn",Turn);if(Path)SdkPut(P,"path",Path);mdo_error_v1 E={0};
    bool Ok=SdkChangesCall(C,Path?"changes.diff":"changes.list",P,&R,&E);CHECK(Ok);if(!Ok)printf("READ ERROR %s %s\n",E.Code,E.Message);xrtValueRelease(P);return R;
}
void ServiceInit(XS_HostInfo* Host){
    TurnChangesProductInit(Host);CHECK(MdoBootstrapRuntime()!=NULL);Workspace=xrtEnvGet("MDO_CHANGES_WORKSPACE");
    CHECK(MdoConfigImport(MDO_CONFIG_SETTINGS,XRT_STR_LITERAL("{\"schema_version\":1,\"patch\":{\"agent\":{\"memory\":false,\"web_search\":false}}}")));
    MdoRunManagerUnit();MdoRunManagerOptions Manager;MdoRunManagerOptionsInit(&Manager);Manager.Automatic=false;Manager.OnModelComplete=Model;xwork_error E;
    CHECK(MdoRunManagerInit(MdoBootstrapRuntime(),&Manager,&E));
    MdoProjectCreateOptions Project;MdoProjectCreateOptionsInit(&Project);Project.Id="qa";Project.Name="Review fixture";Project.WorkspaceRoot=Workspace;CHECK(MdoProjectCreate(&Project,NULL,&E));
    MdoSessionCreateOptions O;MdoSessionCreateOptionsInit(&O);O.ProjectId="qa";O.RequestedId="turn-review";O.Title="Review fixture";
    O.Agent.WorkspaceRoot=Workspace;O.Agent.AgentId="mdo.default";O.Agent.ModelId="history-fixture";O.Agent.Protocol=MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;O.Agent.PermissionProfile="full-access";O.Agent.OnModelComplete=Model;
    MdoSession* Session=MdoSessionCreate(&O,&E);CHECK(Session!=NULL);MdoSessionRelease(Session);
    Run("turn-review");Run("turn-review");Run("turn-review");
    /* Oversized snapshots must not prevent a normal Agent reply. */
    char* Large=xrtPathJoin(Workspace,"too-large.bin");xfile File=xrtOpen(Large,XFILE_WRITE|XFILE_CREATE|XFILE_TRUNCATE);
    CHECK(File&&xrtWriteAtFull(File,17u*1024u*1024u,"x",1,NULL));xrtClose(File);Run("turn-review");CHECK(remove(Large)==0);xrtFree(Large);
    MdoSessionEventSnapshot* Events=MdoSessionEventReplay("qa","turn-review",0,256,&E);uint64 Turns[4]={0};size_t Count=0;
    for(size_t i=0;Events&&i<MdoSessionEventSnapshotCount(Events);i++){MdoSessionEventInfo Info={0};Info.Size=sizeof(Info);if(MdoSessionEventSnapshotAt(Events,i,&Info)&&Info.Kind==XWORK_EVENT_AGENT_START&&!Info.AgentDepth&&Count<4)Turns[Count++]=Info.EventId;}
    MdoSessionEventSnapshotRelease(Events);CHECK(Count==4);
    MdoApplicationInvocation Invocation={0};mdo_sdk_context_v1 C;MdoApplicationContext(&C,&Invocation,"local-client","qa-review","qa","turn-review","",Workspace,0);
    xvalue* First=Read(&C,Turns[0],NULL);const xvalue* Files=SdkGet(First,"files");CHECK(xrtValueCount(Files)==5);CHECK(!SdkGitFind((xvalue*)Files,"preexisting.txt"));CHECK(SdkGitFind((xvalue*)Files,"removed.txt"));
    char* Original=SdkJson(First);xvalue* Second=Read(&C,Turns[1],NULL);CHECK(xrtValueCount(SdkGet(Second,"files"))==1);CHECK(SdkGitFind((xvalue*)SdkGet(Second,"files"),"tracked.txt"));
    xvalue* Empty=Read(&C,Turns[2],NULL);CHECK(xrtValueCount(SdkGet(Empty,"files"))==0);
    xvalue* P=xrtValueObject(),*Unavailable=NULL;SdkPut(P,"scope","turn");SdkPutUInt(P,"turn",Turns[3]);mdo_error_v1 CaptureError={0};
    CHECK(!SdkChangesCall(&C,"changes.list",P,&Unavailable,&CaptureError)&&!strcmp(CaptureError.Code,"git_turn_unavailable"));xrtValueRelease(P);xrtValueRelease(Unavailable);
    Write("tracked.txt","external edit after all replies\n");
    xvalue* Again=Read(&C,Turns[0],NULL);char* Replayed=SdkJson(Again);CHECK(Original&&Replayed&&!strcmp(Original,Replayed));
    xvalue* Diff=Read(&C,Turns[0],"tracked.txt");char* Text=SdkJson(Diff);CHECK(Text&&strstr(Text,"+first reply")&&!strstr(Text,"second reply")&&!strstr(Text,"external edit"));
    xvalue* New=Read(&C,Turns[0],"新增文件.txt");CHECK(xrtValueCount(SdkGet(New,"rows"))>0);
    printf("review_failed=%u\nreview_turn=%llu\nprobe_done=1\n",Failed,(unsigned long long)Turns[0]);fflush(stdout);
    xrtFree(Text);xrtFree(Original);xrtFree(Replayed);xrtValueRelease(First);xrtValueRelease(Second);xrtValueRelease(Empty);xrtValueRelease(Again);xrtValueRelease(Diff);xrtValueRelease(New);
}
