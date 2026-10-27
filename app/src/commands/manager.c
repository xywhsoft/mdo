#include "../../include/mdo/commands.h"
#include "../../include/mdo/application.h"
#include "../../include/mdo/project_lifecycle.h"
#include "../../include/mdo/time.h"
#include "../remote/loopback.h"
#include "../api/write_admission.h"
#include "../../include/mdo/sessions.h"

#define COMMAND_JOBS 64u
#define COMMAND_ACTIVE 4u
#define COMMAND_RESULT_LIMIT (1024u*1024u)
typedef struct CommandJob {
    char Id[37],Hash[65],Directory[128];
    char* Workspace;
    xvalue *Input,*Info,*Actions;
    xthread* Thread;
    xprocess* Process;
    xcancel* Cancel;
    MdoProjectLease* Lease;
    MdoExtensionLease* ExtensionLease;
    uint64 Deadline,Sequence;
    bool Terminal,TimedOut;
} CommandJob;
static struct {
    xmutex* Lock;
    xthread* Watch;
    XS_ServerInfo* Server;
    CommandJob Jobs[COMMAND_JOBS];
    uint64 Sequence;
    bool Stopping,StorageBlocked;
} CommandManager;

bool MdoCommandInvocationIdValid(cstr Id) {
    if(!Id||strlen(Id)!=36||strncmp(Id,"cmd-",4))return false;
    for(size_t i=4;i<36;i++)if(!((Id[i]>='0'&&Id[i]<='9')||(Id[i]>='a'&&Id[i]<='f')))return false;
    return true;
}
static void CommandReceiptPath(cstr Id,char Path[128]){snprintf(Path,128,"data/commands/%s.json",Id);}
static bool CommandSave(CommandJob* J) {
    char P[128];CommandReceiptPath(J->Id,P);char* Text=xrtJsonStringify(J->Info,false,NULL);
    bool Ok=Text&&strlen(Text)<=COMMAND_RESULT_LIMIT&&MdoHomeAtomicWrite(P,Text,strlen(Text),false);
    xrtFree(Text);return Ok;
}
static xvalue* CommandReadJson(cstr Path) {
    bool Missing=false;char* S=MdoExtensionRead(Path,true,COMMAND_RESULT_LIMIT,&Missing);
    xvalue* V=S?xrtJsonParse(xrtStrView(S)):NULL;xrtFree(S);return V;
}
static xvalue* CommandReadReceipt(cstr Id) {
    char Path[128];CommandReceiptPath(Id,Path);xvalue* V=CommandReadJson(Path);
    if(V&&xrtValueType(V)==XVALUE_OBJECT&&CommandText(V,"id")&&!strcmp(CommandText(V,"id"),Id)&&CommandText(V,"request_hash"))return V;
    xrtValueRelease(V);bool Exists=false;xfileinfo Info={0};
    if(MdoHomeExternalStat(Path,&Exists,&Info)&&!Exists)return NULL;
    /* Corrupt/unreadable is never equivalent to not admitted. Preserve the
     * original receipt and refuse to run this identity again. */
    V=xrtValueObject();CommandSet(V,"id",Id);CommandSet(V,"state","uncertain");
    CommandSet(V,"error","Command receipt cannot be read safely; a mutation may have completed. Inspect the target before repeating.");
    xrtValueObjectSetNew(V,XRT_STR_LITERAL("terminal"),xrtValueBool(true));return V;
}
static bool CommandJobWrite(CommandJob* J,cstr Name,cstr Text) {
    char P[256];snprintf(P,sizeof(P),"%s/%s",J->Directory,Name);
    return Text&&MdoHomeAtomicWrite(P,Text,strlen(Text),false);
}
static bool CommandJobJson(CommandJob* J,cstr Name,const xvalue* V) {
    char* S=xrtJsonStringify(V,false,NULL);bool Ok=S&&strlen(S)<=COMMAND_RESULT_LIMIT&&CommandJobWrite(J,Name,S);
    xrtFree(S);return Ok;
}
static xvalue* CommandJobRead(CommandJob* J,cstr Name) {
    char P[256];snprintf(P,sizeof(P),"%s/%s",J->Directory,Name);return CommandReadJson(P);
}
static CommandJob* CommandFind(cstr Id) {
    for(size_t i=0;i<COMMAND_JOBS;i++)if(!strcmp(CommandManager.Jobs[i].Id,Id))return &CommandManager.Jobs[i];
    return NULL;
}
static void CommandDispose(CommandJob* J) {
    if(J->Thread){xrtThreadWait(J->Thread);xrtThreadDestroy(J->Thread);}
    xrtValueRelease(J->Input);xrtValueRelease(J->Info);xrtValueRelease(J->Actions);
    xrtCancelDestroy(J->Cancel);xrtFree(J->Workspace);MdoProjectLeaseRelease(J->Lease);MdoExtensionsRelease(J->ExtensionLease);memset(J,0,sizeof(*J));
}
static void CommandCleanup(CommandJob* J,unsigned Count) {
    const char* Names[]={"input.json","command.c","worker.c","xs.json","web/index.html","mdo/command.h","mdo/module.h","mdo/sdk.h","mdo/workbench.h","final.json"};
    char P[256];
    for(size_t i=0;i<sizeof(Names)/sizeof(*Names);i++){
        snprintf(P,sizeof(P),"%s/%s",J->Directory,Names[i]);(void)MdoHomeRemove(P,false);
    }
    for(unsigned i=1;i<=Count;i++){
        snprintf(P,sizeof(P),"%s/request-%u.json",J->Directory,i);(void)MdoHomeRemove(P,false);
        snprintf(P,sizeof(P),"%s/response-%u.json",J->Directory,i);(void)MdoHomeRemove(P,false);
    }
    snprintf(P,sizeof(P),"%s/web",J->Directory);(void)MdoHomeRemoveEmptyDirectory(P);
    snprintf(P,sizeof(P),"%s/mdo",J->Directory);(void)MdoHomeRemoveEmptyDirectory(P);
    (void)MdoHomeRemoveEmptyDirectory(J->Directory);xrtClearError();
}
typedef struct CommandHttpResult { unsigned Status;char* Bytes;size_t Size;char Etag[256]; } CommandHttpResult;
static bool CommandHttpHead(const xhttp1head* H,void* Data){
    CommandHttpResult* R=Data;R->Status=H->Status;
    for(size_t i=0;i<H->FieldCount;i++)if(xrtStrCaseEqual(H->Fields[i].Name,XRT_STR_LITERAL("ETag"))&&H->Fields[i].Value.Size<sizeof(R->Etag))
        snprintf(R->Etag,sizeof(R->Etag),"%.*s",(int)H->Fields[i].Value.Size,H->Fields[i].Value.Data);
    return true;
}
static bool CommandHttpBody(xbytesview B,void* Data) {
    CommandHttpResult* R=Data;if(B.Size>COMMAND_RESULT_LIMIT-R->Size)return false;
    char* Next=xrtRealloc(R->Bytes,R->Size+B.Size+1);if(!Next)return false;
    R->Bytes=Next;memcpy(Next+R->Size,B.Data,B.Size);R->Size+=B.Size;Next[R->Size]=0;return true;
}
static xvalue* CommandApiCall(CommandJob* J,const xvalue* P,char Error[512]) {
    cstr Method=CommandText(P,"method"),Path=CommandText(P,"path");
    /* Commands cannot recursively spawn commands or stop/update their own host.
     * This is a stability boundary, not a native-code security sandbox. */
    if(!Method||!Path||strstr(Path,"/commands")||strstr(Path,"/command-invocations")||strstr(Path,"/update")||strstr(Path,"/distribution")){
        snprintf(Error,512,"Unsupported local API target");return NULL;
    }
    const xvalue* Body=xrtValueObjectGet(P,XRT_STR_LITERAL("body"));
    char* Json=Body?xrtJsonStringify(Body,false,NULL):NULL;
    XS_FetchHeader Headers[16];size_t Count=0;char WriteToken[MDO_API_WRITE_TOKEN_CAPACITY];bool SuppliedToken=false;
    const xvalue* Fields=xrtValueObjectGet(P,XRT_STR_LITERAL("headers"));
    bool Ok=!Fields||xrtValueType(Fields)==XVALUE_OBJECT;
    for(size_t i=0;Ok&&i<xrtValueCount(Fields);i++){
        xstrview Name={0},Text={0};const xvalue* V=xrtValueObjectAt(Fields,i,&Name);
        Ok=Count<14&&xrtValueGetString(V,&Text)&&!memchr(Name.Data,0,Name.Size)&&!memchr(Text.Data,0,Text.Size);
        if(Ok){Headers[Count].Name=Name.Data;Headers[Count++].Value=Text.Data;}
        if(xrtStrCaseEqual(Name,XRT_STR_LITERAL("X-Mdo-Write-Token")))SuppliedToken=true;
    }
    if(!SuppliedToken&&strcmp(Method,"GET")&&strcmp(Method,"HEAD")){
        Ok=Ok&&MdoApiWriteToken(WriteToken);Headers[Count++]=(XS_FetchHeader){"X-Mdo-Write-Token",WriteToken};
    }
    if(Json){Headers[Count].Name="Content-Type";Headers[Count++].Value="application/json";}
    MdoRemoteHttpRequest Request={0};Request.Method=Method;Request.Target=Path;
    Request.Headers=Headers;Request.HeaderCount=Count;Request.Body=(xbytesview){(const unsigned char*)Json,Json?strlen(Json):0};
    CommandHttpResult Received={0};MdoRemoteHttpSink Sink={CommandHttpHead,CommandHttpBody,&Received};
    Ok=Ok&&(!Body||Json)&&MdoRemoteLoopbackCall(CommandManager.Server,&Request,J->Cancel,&Sink);
    xrtFree(Json);
    xvalue* Result=Ok?xrtValueObject():NULL;
    xvalue* Data=Received.Bytes?xrtJsonParse(xrtStrViewN(Received.Bytes,Received.Size)):NULL;
    if(Result)Ok=xrtValueObjectSetNew(Result,XRT_STR_LITERAL("status"),xrtValueUInt(Received.Status))&&
        CommandSet(Result,"etag",Received.Etag)&&xrtValueObjectSetNew(Result,XRT_STR_LITERAL("body"),Data?Data:xrtValueNull());
    else xrtValueRelease(Data);
    xrtFree(Received.Bytes);
    if(!Ok||!Result){xrtValueRelease(Result);snprintf(Error,512,"Local API request failed; a sent mutation may already have completed. Do not retry blindly.");return NULL;}
    return Result;
}
static xvalue* CommandHostCall(CommandJob* J,const xvalue* Q,char Error[512]) {
    cstr Method=CommandText(Q,"method");const xvalue* Params=xrtValueObjectGet(Q,XRT_STR_LITERAL("params"));
    if(!Method){snprintf(Error,512,"Host method is missing");return NULL;}
    if(!strcmp(Method,"sdk.invoke")){
        mdo_sdk_context_v1 C;MdoApplicationInvocation Invocation={0};Invocation.Cancel=J->Cancel;Invocation.Actions=J->Actions;
        MdoApplicationContext(&C,&Invocation,CommandText(J->Input,"command_id"),J->Id,CommandText(J->Input,"project_id"),
            CommandText(J->Input,"session_id"),CommandText(J->Input,"run_id"),J->Workspace,J->Deadline);
        C.ExtensionKind="commands";C.DirectoryId=CommandText(J->Input,"directory_id");C.ExtensionRoot=CommandText(J->Input,"extension_root");
        uint64 Origin=0;int64 SignedOrigin=0;const xvalue* Source=xrtValueObjectGet(J->Input,XRT_STR_LITERAL("call_origin"));
        if(!xrtValueGetUInt(Source,&Origin)&&xrtValueGetInt(Source,&SignedOrigin)&&SignedOrigin>=0)Origin=(uint64)SignedOrigin;
        C.Origin=Origin<=MDO_ORIGIN_EXTENSION?(uint32)Origin:MDO_ORIGIN_UNKNOWN;
        mdo_error_v1 E={0};xvalue* Result=NULL;cstr Service=CommandText(Params,"method");
        mdo_result Code=MdoApplicationCall(&C,Service,xrtValueObjectGet(Params,XRT_STR_LITERAL("params")),&Result,&E);
        xvalue* Frame=xrtValueObject(),*Failure=xrtValueObject();
        xrtValueObjectSetNew(Frame,XRT_STR_LITERAL("code"),xrtValueInt(Code));
        xrtValueObjectSetNew(Frame,XRT_STR_LITERAL("result"),Result?Result:xrtValueNull());
        CommandSet(Failure,"code",E.Code);CommandSet(Failure,"message",E.Message);CommandSet(Failure,"stage",E.Stage);
        CommandSet(Failure,"cause",E.Cause);
        xrtValueObjectSetNew(Failure,XRT_STR_LITERAL("status"),xrtValueUInt(E.Status));
        xrtValueObjectSetNew(Failure,XRT_STR_LITERAL("retryable"),xrtValueBool(E.Retryable));
        xrtValueObjectSetNew(Failure,XRT_STR_LITERAL("may_have_completed"),xrtValueBool(E.MayHaveCompleted));
        xrtValueObjectSetNew(Frame,XRT_STR_LITERAL("error"),Failure);return Frame;
    }
    if(!strcmp(Method,"api.request"))return CommandApiCall(J,Params,Error);
    if(!strcmp(Method,"session.create")){
        cstr Project=CommandText(J->Input,"project_id"),Requested=CommandText(Params,"project_id");
        if((Params&&xrtValueType(Params)!=XVALUE_OBJECT)||(Requested&&strcmp(Requested,Project))){snprintf(Error,512,"session.create requires an object for the invocation project");return NULL;}
        xvalue* Body=Params?xrtValueClone(Params):xrtValueObject(),*Request=xrtValueObject();
        bool Ok=Body&&Request&&CommandSet(Body,"project_id",Project)&&CommandSet(Request,"method","POST")&&CommandSet(Request,"path","/api/v1/sessions")&&
            xrtValueObjectSet(Request,XRT_STR_LITERAL("body"),Body);
        xvalue* Reply=Ok?CommandApiCall(J,Request,Error):NULL;uint64 Status=0;
        Ok=Reply&&CommandUInt(xrtValueObjectGet(Reply,XRT_STR_LITERAL("status")),&Status)&&Status>=200&&Status<300;
        const xvalue* Envelope=xrtValueObjectGet(Reply,XRT_STR_LITERAL("body"));
        xvalue* Result=Ok?xrtValueClone(xrtValueObjectGet(Envelope,XRT_STR_LITERAL("data"))):NULL;
        if(!Ok&&!Error[0]){cstr Message=CommandText(xrtValueObjectGet(Envelope,XRT_STR_LITERAL("error")),"message");snprintf(Error,512,"%.500s",Message?Message:"Session creation failed");}
        xrtValueRelease(Reply);xrtValueRelease(Body);xrtValueRelease(Request);return Result;
    }
    if(!strcmp(Method,"session.compact")){
        MdoSessionRuntimeOptions Options;MdoSessionRuntimeOptionsInit(&Options);Options.Cancel=J->Cancel;Options.Deadline=J->Deadline;
        xwork_error E;MdoSession* Session=MdoSessionOpen(CommandText(J->Input,"project_id"),CommandText(J->Input,"session_id"),&Options,&E);
        bool Ok=Session&&MdoSessionCompact(Session,&E);MdoSessionRelease(Session);
        if(!Ok){snprintf(Error,512,"%.500s",E.sMessage[0]?E.sMessage:"Context compaction failed");return NULL;}
        xvalue* Action=xrtValueObject(),*Result=xrtValueObject();
        Ok=Action&&Result&&CommandSet(Action,"type","ui.refresh")&&xrtValueArrayAppend(J->Actions,Action)&&
            CommandSet(Result,"message","上下文已压缩，原始会话记录仍可查看。");
        xrtValueRelease(Action);
        if(!Ok){xrtValueRelease(Result);snprintf(Error,512,"Context compacted but its UI result is unavailable");return NULL;}
        return Result;
    }
    static const char* Methods[]={"ui.new","ui.model","ui.settings","ui.theme","ui.help","ui.stop","ui.session","ui.mcp","skill.invoke"};
    for(size_t i=0;i<sizeof(Methods)/sizeof(*Methods);i++)if(!strcmp(Method,Methods[i])){
        xvalue* A=xrtValueObject();
        bool Ok=A&&xrtValueCount(J->Actions)<16&&CommandSet(A,"type",Method)&&
            xrtValueObjectSetNew(A,XRT_STR_LITERAL("params"),Params?xrtValueClone(Params):xrtValueObject())&&
            xrtValueArrayAppend(J->Actions,A);
        xrtValueRelease(A);
        if(!Ok){snprintf(Error,512,"Command UI action limit reached");return NULL;}
        return xrtValueObject();
    }
    snprintf(Error,512,"Unknown command host method: %.120s",Method);return NULL;
}
static bool CommandCopyResource(CommandJob* J,cstr Source,cstr Target) {
    size_t N=0;char* Text=(char*)xrtVfsReadAllLimit(MdoHomeApplicationVfs(),Source,256u*1024u,&N);
    bool Ok=Text&&!memchr(Text,0,N)&&CommandJobWrite(J,Target,Text);xrtFree(Text);return Ok;
}
static int32 CommandExecute(void* Value) {
    CommandJob* J=Value;unsigned Sequence=1;char Error[512]={0};cstr State="failed",ErrorCode="command_worker_start_failed";xvalue* Final=NULL;
    xrtMutexLock(CommandManager.Lock);CommandSet(J->Info,"state","running");CommandSave(J);xrtMutexUnlock(CommandManager.Lock);
    char* Directory=MdoHomeExternalPath(J->Directory);char* Executable=NULL,*ConfigPath=NULL;
    const char* Args[2];size_t ArgCount=0;
    if(!CommandCopyResource(J,"/app/command-worker.c","worker.c")||
       !CommandCopyResource(J,"/app/generated/module-sdk/mdo/command.h","mdo/command.h")||
       !CommandCopyResource(J,"/app/generated/module-sdk/mdo/module.h","mdo/module.h")||
       !CommandCopyResource(J,"/app/generated/module-sdk/mdo/sdk.h","mdo/sdk.h")||
       !CommandCopyResource(J,"/app/generated/module-sdk/mdo/workbench.h","mdo/workbench.h")||
       !CommandJobJson(J,"input.json",J->Input)||!CommandJobWrite(J,"web/index.html","Command worker")||
       !CommandJobWrite(J,"xs.json","{\"engine\":{\"workers\":1},\"services\":[{\"enabled\":true,\"class\":\"custom\",\"name\":\"command-worker\",\"host_default\":{\"enabled\":true,\"path\":\"web\",\"devlang\":\"c\",\"devfile\":\"worker.c\"}}]}")){
        snprintf(Error,sizeof(Error),"Cannot prepare isolated command worker");goto finish;
    }
    (void)MdoHomeSetMode(J->Directory,0700);
#if defined(__ANDROID__)
    cstr Native=getenv("MDO_NATIVE_DIR");Executable=Native?xrtPathJoin(Native,"libmdo_command_worker.so"):NULL;
    ConfigPath=Native?xrtPathJoin(Native,"libxs.so"):NULL;Args[0]=Directory;Args[1]=ConfigPath;ArgCount=2;
#else
    Executable=xrtPathExecutable();ConfigPath=xrtPathJoin(Directory,"xs.json");Args[0]=ConfigPath;Args[1]="--no-vfs";ArgCount=2;
#endif
    xprocessconfig Config;xrtProcessConfigInit(&Config);Config.Program=Executable;Config.Args=Args;Config.ArgCount=ArgCount;
    Config.WorkDir=Directory;Config.HideWindow=true;Config.NewGroup=true;
    Config.Stdin.Mode=XPROCESS_IO_PIPE;Config.Stdout.Mode=XPROCESS_IO_NULL;Config.Stderr.Mode=XPROCESS_IO_NULL;
    xrtMutexLock(CommandManager.Lock);
    if(!xrtCancelRequested(J->Cancel))J->Process=Executable?xrtProcessSpawn(&Config):NULL;
    xrtMutexUnlock(CommandManager.Lock);
    if(!J->Process){snprintf(Error,sizeof(Error),"Isolated command worker could not start");goto finish;}
    ErrorCode="command_worker_lost";
    for(;;){
        if(xrtCancelRequested(J->Cancel))break;
        char Name[64];snprintf(Name,sizeof(Name),"request-%u.json",Sequence);
        xvalue* Q=Sequence<=128?CommandJobRead(J,Name):NULL;
        if(Q){
            xvalue* Reply=xrtValueObject();xvalue* Result=CommandHostCall(J,Q,Error);xrtValueRelease(Q);
            bool Ok=Reply&&xrtValueObjectSetNew(Reply,XRT_STR_LITERAL("ok"),xrtValueBool(Result!=NULL))&&
                CommandSet(Reply,"error",Error)&&xrtValueObjectSetNew(Reply,XRT_STR_LITERAL("result"),Result?Result:xrtValueNull());
            snprintf(Name,sizeof(Name),"response-%u.json",Sequence);Ok=Ok&&CommandJobJson(J,Name,Reply);xrtValueRelease(Reply);
            if(!Ok){snprintf(Error,sizeof(Error),"Command host response exceeded limits or could not be saved");break;}
            Sequence++;Error[0]=0;
        }
        Final=CommandJobRead(J,"final.json");if(Final)break;
        if(xrtProcessWaitFor(J->Process,20)==XWAIT_OK){
            Final=CommandJobRead(J,"final.json");
            if(!Final){
                xprocessstatus Exit={0};(void)xrtProcessStatus(J->Process,&Exit);
                snprintf(Error,sizeof(Error),"Command worker exited without a result (exit=%ld, signal=%ld; crash or bootstrap failure)",(long)Exit.Code,(long)Exit.Signal);
            }
            break;
        }
        xrtClearError();
    }
finish:
    xrtMutexLock(CommandManager.Lock);xprocess* Process=J->Process;J->Process=NULL;xrtMutexUnlock(CommandManager.Lock);
    if(Process){(void)xrtProcessKillTree(Process);(void)xrtProcessWait(Process);xrtProcessDestroy(Process);}
    if(xrtCancelRequested(J->Cancel)){State=J->TimedOut?"timed_out":"cancelled";ErrorCode=J->TimedOut?"command_timeout":"command_cancelled";snprintf(Error,sizeof(Error),"%s",J->TimedOut?"Command execution timed out":"Command execution cancelled");}
    else if(Final){
        bool Ok=false;xrtValueGetBool(xrtValueObjectGet(Final,XRT_STR_LITERAL("ok")),&Ok);
        ErrorCode=CommandText(Final,"error_code");if(!ErrorCode)ErrorCode="command_failed";
        if(Ok)State="succeeded";
        else {
            int64 Code=0;xrtValueGetInt(xrtValueObjectGet(Final,XRT_STR_LITERAL("code")),&Code);
            if(Code==-2)State="cancelled";else if(Code==-4)State="timed_out";
            snprintf(Error,sizeof(Error),"%.500s",CommandText(Final,"error")?CommandText(Final,"error"):"Command failed");
        }
    }
    CommandCleanup(J,Sequence>128?128:Sequence);xrtFree(Directory);xrtFree(Executable);xrtFree(ConfigPath);
    /* Publish terminal state last. A retained result may be read repeatedly;
     * the originating UI applies its action list once per invocation. */
    xrtMutexLock(CommandManager.Lock);
    CommandSet(J->Info,"state",State);CommandSet(J->Info,"error",Error);CommandSet(J->Info,"error_code",ErrorCode);
    if(Final&&!strcmp(State,"succeeded")){
        const xvalue* Result=xrtValueObjectGet(Final,XRT_STR_LITERAL("result"));
        xrtValueObjectSetNew(J->Info,XRT_STR_LITERAL("result"),Result?xrtValueClone(Result):xrtValueObject());
        xrtValueObjectSetNew(J->Info,XRT_STR_LITERAL("actions"),xrtValueClone(J->Actions));
    }
    xrtValueObjectSetNew(J->Info,XRT_STR_LITERAL("terminal"),xrtValueBool(true));
    if(!CommandSave(J)){CommandSet(J->Info,"error","Command finished but its receipt could not be saved; do not repeat a mutation blindly");}
    MdoProjectLeaseRelease(J->Lease);J->Lease=NULL;MdoExtensionsRelease(J->ExtensionLease);J->ExtensionLease=NULL;J->Terminal=true;
    xrtValueRelease(Final);xrtMutexUnlock(CommandManager.Lock);return 0;
}
static int32 CommandWatch(void* Unused) {
    (void)Unused;
    for(;;){
        xrtMutexLock(CommandManager.Lock);bool Stop=CommandManager.Stopping;
        for(size_t i=0;i<COMMAND_JOBS;i++){
            CommandJob* J=&CommandManager.Jobs[i];if(!J->Id[0]||J->Terminal)continue;
            if(Stop||MdoDeadlineExpired(J->Deadline)){
                if(!xrtCancelRequested(J->Cancel))J->TimedOut=!Stop;
                xrtCancelRequest(J->Cancel);if(J->Process)(void)xrtProcessKillTree(J->Process);
            }
        }
        xrtMutexUnlock(CommandManager.Lock);if(Stop)return 0;xrtSleep(20);
    }
}
static bool CommandRecover(void) {
    xdir Dir=MdoHomeOpenDirectory("data/commands",XDIR_STAT);
    if(!Dir)return CommandMissing();
    xdirentry E;xdirnext Next;size_t Count=0;
    while((Next=xrtDirNext(Dir,&E))==XDIR_NEXT_ITEM){
        if(E.Name.Size!=41||memcmp(E.Name.Data+36,".json",5))continue;
        char Id[37],Path[128];memcpy(Id,E.Name.Data,36);Id[36]=0;
        if(!MdoCommandInvocationIdValid(Id)||Count>=COMMAND_JOBS){xrtDirClose(Dir);return false;}
        CommandReceiptPath(Id,Path);xvalue* V=CommandReadReceipt(Id);
        if(!V){
            xrtValueRelease(V);xrtDirClose(Dir);return false;
        }
        CommandJob* J=&CommandManager.Jobs[Count++];snprintf(J->Id,sizeof(J->Id),"%s",Id);
        J->Info=V;J->Sequence=++CommandManager.Sequence;J->Terminal=true;
        bool Terminal=false;xrtValueGetBool(xrtValueObjectGet(V,XRT_STR_LITERAL("terminal")),&Terminal);
        if(!Terminal){
            CommandSet(V,"state","interrupted");CommandSet(V,"error","Host restarted before completion; a mutation may have completed. Inspect its result before repeating.");
            xrtValueObjectSetNew(V,XRT_STR_LITERAL("terminal"),xrtValueBool(true));
            if(!CommandSave(J)){xrtDirClose(Dir);return false;}
        }
        snprintf(J->Directory,sizeof(J->Directory),"data/cache/command-workers/%s",Id);
        bool Scratch=false;xfileinfo ScratchInfo={0};
        if(!MdoHomeExternalStat(J->Directory,&Scratch,&ScratchInfo)){xrtDirClose(Dir);return false;}
        if(Scratch){if(ScratchInfo.Type!=XFILE_TYPE_DIRECTORY){xrtDirClose(Dir);return false;}CommandCleanup(J,128);}
    }
    bool Ok=Next==XDIR_NEXT_END;return xrtDirClose(Dir)&&Ok;
}
bool MdoCommandManagerInit(XS_ServerInfo* Server) {
    if(CommandManager.Lock)return true;
    memset(&CommandManager,0,sizeof(CommandManager));
    CommandManager.Lock=xrtMutexCreate();CommandManager.Server=xsServerRetain(Server);
    if(CommandManager.Lock&&CommandManager.Server){
        CommandManager.StorageBlocked=!CommandRecover();
        if(CommandManager.StorageBlocked)fprintf(stderr,"[mdo] command receipt storage unavailable; new commands are blocked\n");
        CommandManager.Watch=xrtThreadCreate(CommandWatch,NULL,0);
    }
    if(!CommandManager.Watch){MdoCommandManagerUnit();return false;}return true;
}
void MdoCommandManagerUnit(void) {
    if(!CommandManager.Lock)return;
    xrtMutexLock(CommandManager.Lock);CommandManager.Stopping=true;xrtMutexUnlock(CommandManager.Lock);
    if(CommandManager.Watch){xrtThreadWait(CommandManager.Watch);xrtThreadDestroy(CommandManager.Watch);}
    for(size_t i=0;i<COMMAND_JOBS;i++)CommandDispose(&CommandManager.Jobs[i]);
    xsServerRelease(CommandManager.Server);xrtMutexDestroy(CommandManager.Lock);memset(&CommandManager,0,sizeof(CommandManager));
}
xvalue* MdoCommandGet(cstr Id) {
    if(!MdoCommandInvocationIdValid(Id)||!CommandManager.Lock)return NULL;
    xrtMutexLock(CommandManager.Lock);CommandJob* J=CommandFind(Id);xvalue* V=J?xrtValueClone(J->Info):NULL;
    if(!J){
        V=CommandReadReceipt(Id);
        bool Terminal=false;xrtValueGetBool(xrtValueObjectGet(V,XRT_STR_LITERAL("terminal")),&Terminal);
        if(V&&!Terminal){CommandSet(V,"state","interrupted");CommandSet(V,"error","Host restarted before completion; a mutation may have completed. Inspect its result before repeating.");xrtValueObjectSetNew(V,XRT_STR_LITERAL("terminal"),xrtValueBool(true));}
    }
    xrtMutexUnlock(CommandManager.Lock);return V;
}
bool MdoCommandCancel(cstr Id) {
    if(!MdoCommandInvocationIdValid(Id)||!CommandManager.Lock)return false;
    xrtMutexLock(CommandManager.Lock);CommandJob* J=CommandFind(Id);bool Found=J!=NULL;
    if(J&&!J->Terminal){xrtCancelRequest(J->Cancel);if(J->Process)(void)xrtProcessKillTree(J->Process);}
    xrtMutexUnlock(CommandManager.Lock);return Found;
}
xvalue* MdoCommandStart(const xvalue* Input,cstr Workspace,char Error[512]) {
    Error[0]=0;cstr Id=CommandText(Input,"invocation_id"),Command=CommandText(Input,"command_id"),Project=CommandText(Input,"project_id");
    if(!CommandManager.Lock||!MdoCommandInvocationIdValid(Id)||!MdoExtensionAlias(Command)||!MdoExtensionIdValid(Project))goto invalid;
    if(CommandManager.StorageBlocked){snprintf(Error,512,"Command receipt storage needs recovery before admitting new commands");return NULL;}
    char* Json=xrtJsonStringify(Input,false,NULL);char Hash[65];bool HasHash=Json&&MdoExtensionHash(Json,Hash);xrtFree(Json);if(!HasHash)goto invalid;
    xvalue* Existing=MdoCommandGet(Id);
    if(Existing){if(strcmp(Hash,CommandText(Existing,"request_hash")?CommandText(Existing,"request_hash"):"")){xrtValueRelease(Existing);goto conflict;}return Existing;}
    char *Source=NULL,Revision[65];xvalue* Meta=NULL;
    if(!MdoCommandResolve(Workspace,Command,&Source,&Meta,Revision,Error))return NULL;
    cstr Requested=CommandText(Input,"revision");
    if(Requested&&Requested[0]&&strcmp(Requested,Revision)){xrtFree(Source);xrtValueRelease(Meta);snprintf(Error,512,"Command source changed; refresh the catalog");return NULL;}
    bool NeedsSession=false;xrtValueGetBool(xrtValueObjectGet(Meta,XRT_STR_LITERAL("requires_session")),&NeedsSession);
    cstr Session=CommandText(Input,"session_id");
    if(NeedsSession&&(!Session||!Session[0])){xrtFree(Source);xrtValueRelease(Meta);snprintf(Error,512,"This command requires a session");return NULL;}
    uint64 Timeout=30000;xrtValueGetUInt(xrtValueObjectGet(Meta,XRT_STR_LITERAL("timeout_ms")),&Timeout);
    cstr DirectoryId=CommandText(Meta,"directory_id"),DirectoryRoot=CommandText(Meta,"directory_root"),Entry=CommandText(Meta,"path");
    MdoExtensionLease* ExtensionLease=MdoExtensionsAcquire("commands",DirectoryId,Error);
    if(!ExtensionLease){xrtFree(Source);xrtValueRelease(Meta);return NULL;}
    xwork_error LeaseError;MdoProjectLease* Lease=MdoProjectLeaseAcquire(Project,MDO_PROJECT_LEASE_SHARED,&LeaseError);
    if(!Lease){xrtFree(Source);xrtValueRelease(Meta);MdoExtensionsRelease(ExtensionLease);snprintf(Error,512,"Project is busy or unavailable");return NULL;}
    xrtMutexLock(CommandManager.Lock);size_t Active=0;CommandJob* Slot=NULL;
    for(size_t i=0;i<COMMAND_JOBS;i++){
        CommandJob* J=&CommandManager.Jobs[i];if(J->Id[0]&&!J->Terminal)Active++;
        if(!J->Id[0]||J->Terminal)if(!Slot||!J->Id[0]||(Slot->Id[0]&&J->Sequence<Slot->Sequence))Slot=J;
    }
    bool Ok=!CommandManager.Stopping&&Active<COMMAND_ACTIVE&&Slot&&!CommandFind(Id);
    if(Ok&&Slot->Id[0]){char P[128];CommandReceiptPath(Slot->Id,P);Ok=MdoHomeRemove(P,false);}
    if(Ok){
        if(Slot->Id[0])CommandDispose(Slot);
        snprintf(Slot->Id,sizeof(Slot->Id),"%s",Id);snprintf(Slot->Hash,sizeof(Slot->Hash),"%s",Hash);
        snprintf(Slot->Directory,sizeof(Slot->Directory),"data/cache/command-workers/%s",Id);
        Slot->Input=xrtValueClone(Input);Slot->Info=xrtValueObject();Slot->Actions=xrtValueArray();Slot->Cancel=xrtCancelCreate();
        Slot->Workspace=xrtStrDup(Workspace?Workspace:"");Slot->Lease=Lease;Lease=NULL;Slot->Sequence=++CommandManager.Sequence;
        Slot->ExtensionLease=ExtensionLease;ExtensionLease=NULL;
        char* NativeRoot=MdoHomeExternalPath(DirectoryRoot);
        cstr RelativeEntry=Entry&&DirectoryRoot&&!strncmp(Entry,DirectoryRoot,strlen(DirectoryRoot))&&Entry[strlen(DirectoryRoot)]=='/'?Entry+strlen(DirectoryRoot)+1:NULL;
        Slot->Deadline=MdoDeadlineAfterUs(Timeout*1000u);
        Ok=Slot->Input&&Slot->Info&&Slot->Actions&&Slot->Cancel&&Slot->Workspace&&CommandSet(Slot->Input,"workspace",Slot->Workspace)&&
            xrtValueObjectSetNew(Slot->Input,XRT_STR_LITERAL("watch_parent"),xrtValueBool(true))&&
            CommandSet(Slot->Input,"revision",Revision)&&NativeRoot&&RelativeEntry&&
            CommandSet(Slot->Input,"directory_id",DirectoryId)&&CommandSet(Slot->Input,"extension_root",NativeRoot)&&CommandSet(Slot->Input,"extension_entry",RelativeEntry)&&
            xrtValueObjectSetNew(Slot->Input,XRT_STR_LITERAL("deadline"),xrtValueUInt(Slot->Deadline))&&
            CommandSet(Slot->Info,"id",Id)&&CommandSet(Slot->Info,"command_id",Command)&&CommandSet(Slot->Info,"project_id",Project)&&
            CommandSet(Slot->Info,"session_id",Session)&&CommandSet(Slot->Info,"revision",Revision)&&CommandSet(Slot->Info,"request_hash",Hash)&&
            CommandSet(Slot->Info,"state","pending")&&xrtValueObjectSetNew(Slot->Info,XRT_STR_LITERAL("terminal"),xrtValueBool(false))&&
            CommandJobWrite(Slot,"command.c",Source)&&CommandSave(Slot);
        xrtFree(NativeRoot);
        if(Ok){
            Slot->Thread=xrtThreadCreate(CommandExecute,Slot,0);
            if(!Slot->Thread){
                CommandSet(Slot->Info,"state","failed");CommandSet(Slot->Info,"error","Worker thread could not start; no command was executed");
                xrtValueObjectSetNew(Slot->Info,XRT_STR_LITERAL("terminal"),xrtValueBool(true));Slot->Terminal=true;(void)CommandSave(Slot);
                MdoProjectLeaseRelease(Slot->Lease);Slot->Lease=NULL;CommandCleanup(Slot,0);
                MdoExtensionsRelease(Slot->ExtensionLease);Slot->ExtensionLease=NULL;
            }
        }
        if(!Ok){CommandCleanup(Slot,0);CommandDispose(Slot);}
    }
    xvalue* Result=Ok?xrtValueClone(Slot->Info):NULL;
    xrtMutexUnlock(CommandManager.Lock);MdoProjectLeaseRelease(Lease);MdoExtensionsRelease(ExtensionLease);xrtFree(Source);xrtValueRelease(Meta);
    if(!Ok)snprintf(Error,512,"Command admission failed (busy, storage unavailable, or duplicate submission); check the invocation receipt before resubmitting");
    return Result;
conflict:snprintf(Error,512,"Invocation ID belongs to a different command request");return NULL;
invalid:snprintf(Error,512,"Invalid command invocation or command manager unavailable");return NULL;
}
