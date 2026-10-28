#include "internal.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/time.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/project_lifecycle.h"
#include "../../include/mdo/projects.h"
#include "../../include/mdo/project_binding.h"
#include "../../include/mdo/distribution.h"
#if !defined(_WIN32) && !defined(_WIN64)
#if defined(__TINYC__) && defined(__linux__)
/* Same stable libc ABI used by the optional POSIX power adapter. The compact
 * TCC header set omits fcntl.h; Android/Linux have identical values here. */
extern int fcntl(int Descriptor,int Command,...);
#define MDO_TERM_GETFL 3
#define MDO_TERM_SETFL 4
#define MDO_TERM_NONBLOCK 04000
#else
#include <fcntl.h>
#define MDO_TERM_GETFL F_GETFL
#define MDO_TERM_SETFL F_SETFL
#define MDO_TERM_NONBLOCK O_NONBLOCK
#endif
#endif

/* A terminal belongs to an immutable SDK scope, not to a panel or an HTTP
 * request. Only metadata is durable: commands and terminal bytes are never
 * replayed by this manager after a service restart. */
#define SDK_TERMINAL_SLOTS 32u
#define SDK_TERMINAL_ACTIVE 8u
#define SDK_TERMINAL_OUTPUT (1024u*1024u)
#define SDK_TERMINAL_INPUT 65536u
#define SDK_TERMINAL_CHUNK 8192u
#define SDK_TERMINAL_PATH "data/application/terminals.json"
#define SDK_TERMINAL_STORE (256u*1024u)
static const cstr SdkTerminalMethods[]={"terminal.capabilities","terminal.create","terminal.list","terminal.get","terminal.attach","terminal.detach","terminal.read","terminal.write","terminal.resize","terminal.close","terminal.release","terminal.prune"};
typedef struct SdkTerminalWrite {uint64 Sequence;size_t Bytes;char Hash[65];} SdkTerminalWrite;
typedef struct SdkTerminal {
    xvalue* Record;
    xprocess* Process;
    xthread *Reader,*Writer,*Waiter;
    xcond* Changed;
    MdoProjectLease* Lease;
    MdoCliUse* Commands;
    unsigned char *Output,*Input;
    size_t OutputStart,OutputSize,InputStart,InputSize;
    uint64 OutputTotal,InputAdmitted,InputWritten,InputDropped;
    uint64 Sequence,LeaseUntil;
    char Client[256],Token[37],InputEpoch[37],Error[512];
    SdkTerminalWrite Writes[32];
    unsigned Pins;
    bool Starting,Closing,Exited,ReaderDone,WriterDone,WaiterDone,Releasing,InputFailed,ReceiptUncertain;
} SdkTerminal;
static struct {xmutex* Lock;SdkTerminal Slots[SDK_TERMINAL_SLOTS];char Epoch[37];bool Stopping,Ready;} SdkTerminals;
static bool SdkTerminalFail(mdo_error_v1* E,cstr Code,cstr Text,unsigned Status){return MdoApplicationError(E,Code,Text,"terminal",Status,false);}
static bool SdkTerminalEqual(cstr A,cstr B){return !strcmp(A?A:"",B?B:"");}
static bool SdkTerminalOwn(const mdo_sdk_context_v1* C,const SdkTerminal* T,mdo_error_v1* E){
    const xvalue* Scope=SdkGet(T->Record,"context");
    if(!SdkTerminalEqual(C->ExtensionId,SdkField(T->Record,"owner")))return SdkTerminalFail(E,"terminal_owner_mismatch","Terminal belongs to another extension",403);
    if(!SdkTerminalEqual(C->ProjectId,SdkField(Scope,"project"))||!SdkTerminalEqual(C->SessionId,SdkField(Scope,"session"))||!SdkTerminalEqual(C->RunId,SdkField(Scope,"run"))||!SdkTerminalEqual(C->Workspace,SdkField(Scope,"workspace")))return SdkTerminalFail(E,"terminal_context_mismatch","Use the terminal's original project and session context",409);
    return true;
}
static bool SdkTerminalSave(void){
    xvalue* Root=xrtValueObject(),*Rows=xrtValueArray();SdkPutUInt(Root,"schema_version",1);
    for(size_t i=0;i<SDK_TERMINAL_SLOTS;i++)if(SdkTerminals.Slots[i].Record)xrtValueArrayAppend(Rows,SdkTerminals.Slots[i].Record);
    xrtValueObjectSetNew(Root,XRT_STR_LITERAL("terminals"),Rows);char* Json=SdkJson(Root);size_t N=Json?strlen(Json):0;
    bool Ok=Json&&N<=SDK_TERMINAL_STORE&&MdoHomeAtomicWrite(SDK_TERMINAL_PATH,Json,N,false);xrtFree(Json);xrtValueRelease(Root);return Ok;
}
/* Releasing output storage must not make a lost creation request replayable.
 * Receipts are constant-time lookups by hashed owner/context/key, never a scan
 * or a second copy of shell argv, credentials, input or terminal output. */
static bool SdkTerminalReceiptPath(const mdo_sdk_context_v1* C,cstr Key,char Path[128]){
    xvalue* Identity=xrtValueObject();SdkPut(Identity,"owner",C->ExtensionId);SdkPut(Identity,"project",C->ProjectId);SdkPut(Identity,"session",C->SessionId);SdkPut(Identity,"run",C->RunId);SdkPut(Identity,"workspace",C->Workspace);SdkPut(Identity,"key",Key);
    char* Json=SdkJson(Identity);char Hash[65];bool Ok=Json&&MdoExtensionHash(Json,Hash);xrtFree(Json);xrtValueRelease(Identity);
    if(Ok)snprintf(Path,128,"data/application/terminal-receipts/%s.json",Hash);
    return Ok;
}
static bool SdkTerminalReceiptExists(cstr Path,cstr Hash,mdo_error_v1* E){
    bool Exists=false;xfileinfo Info={0};
    if(!MdoHomeExternalStat(Path,&Exists,&Info))return SdkTerminalFail(E,"terminal_receipt_unreadable","Cannot inspect the terminal admission receipt; no process was started",409),true;
    if(!Exists)return false;
    xfile File=Info.Type==XFILE_TYPE_FILE&&Info.Size<=4096?MdoHomeOpenRead(Path):NULL;char Text[4097];xvalue* Receipt=NULL;
    if(File&&xrtReadFull(File,Text,(size_t)Info.Size,NULL)){Text[Info.Size]=0;Receipt=SdkParse(Text);}if(File)xrtClose(File);
    cstr Previous=SdkField(Receipt,"request_hash"),Id=SdkField(Receipt,"id");bool Valid=Previous&&SdkIdValid(Id);
    if(!Valid)SdkTerminalFail(E,"terminal_receipt_unreadable","Terminal admission receipt is unreadable; no process was replayed",409);
    else if(strcmp(Hash,Previous))SdkTerminalFail(E,"terminal_identity_conflict","This creation key was already admitted with different settings",409);
    else SdkTerminalFail(E,"terminal_intent_retired","This creation intent was already admitted and retired; use a new key for a new terminal",410);
    xrtValueRelease(Receipt);return true;
}
static bool SdkTerminalReceiptSave(cstr Path,const xvalue* Record){
    xvalue* Receipt=xrtValueObject();SdkPutUInt(Receipt,"schema_version",1);SdkPut(Receipt,"id",SdkField(Record,"id"));SdkPut(Receipt,"request_hash",SdkField(Record,"request_hash"));
    char* Json=SdkJson(Receipt);bool Ok=Json&&MdoHomeAtomicWrite(Path,Json,strlen(Json),false);xrtFree(Json);xrtValueRelease(Receipt);return Ok;
}
static void SdkTerminalFree(SdkTerminal* T){
    if(T->Reader){xrtThreadWait(T->Reader);xrtThreadDestroy(T->Reader);}if(T->Writer){xrtThreadWait(T->Writer);xrtThreadDestroy(T->Writer);}if(T->Waiter){xrtThreadWait(T->Waiter);xrtThreadDestroy(T->Waiter);}
    if(T->Process)xrtProcessDestroy(T->Process);
    if(T->Changed)xrtCondDestroy(T->Changed);
    if(T->Output)xrtSecureZero(T->Output,SDK_TERMINAL_OUTPUT);
    if(T->Input)xrtSecureZero(T->Input,SDK_TERMINAL_INPUT);
    xrtFree(T->Output);xrtFree(T->Input);MdoProjectLeaseRelease(T->Lease);MdoCliUseEnd(T->Commands);xrtValueRelease(T->Record);xrtSecureZero(T,sizeof(*T));
}
static xvalue* SdkTerminalInfo(const SdkTerminal* T){
    xvalue* R=xrtValueClone(T->Record);SdkPut(R,"epoch",SdkTerminals.Epoch);
    SdkPut(R,"state",T->Starting?"starting":T->Closing&&!T->Exited?"closing":T->Exited?"ended":"running");
    SdkPutUInt(R,"pid",T->Process?xrtProcessId(T->Process):0);SdkPutUInt(R,"cursor",T->OutputTotal);SdkPutUInt(R,"oldest",T->OutputTotal-T->OutputSize);
    SdkPutUInt(R,"retained_bytes",T->OutputSize);SdkPutUInt(R,"output_capacity",SDK_TERMINAL_OUTPUT);SdkPutUInt(R,"input_queued",T->InputSize);SdkPutUInt(R,"input_admitted",T->InputAdmitted);SdkPutUInt(R,"input_written",T->InputWritten);SdkPutUInt(R,"input_dropped",T->InputDropped);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("eof"),xrtValueBool(T->ReaderDone));xrtValueObjectSetNew(R,XRT_STR_LITERAL("input_failed"),xrtValueBool(T->InputFailed));
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("drained"),xrtValueBool(T->ReaderDone&&T->WriterDone&&T->WaiterDone&&!T->Starting));
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("receipt_uncertain"),xrtValueBool(T->ReceiptUncertain));
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("control_busy"),xrtValueBool(T->Token[0]&&!MdoDeadlineExpired(T->LeaseUntil)));
    if(T->Error[0])SdkPut(R,"error",T->Error);
    return R;
}
static bool SdkTerminalLease(SdkTerminal* T,const xvalue* P,mdo_error_v1* E){
    if(!T->Token[0]||MdoDeadlineExpired(T->LeaseUntil)||!SdkTerminalEqual(SdkField(P,"client"),T->Client)||!SdkTerminalEqual(SdkField(P,"control_token"),T->Token)||!SdkTerminalEqual(SdkField(P,"input_epoch"),T->InputEpoch))return SdkTerminalFail(E,"terminal_control_expired","Acquire terminal control again; uncertain input must not be replayed",409);
    if(!SdkTerminalEqual(SdkField(P,"epoch"),SdkTerminals.Epoch))return SdkTerminalFail(E,"terminal_epoch_changed","Terminal service restarted; commands must not be replayed",410);
    T->LeaseUntil=MdoDeadlineAfterUs(15000000);return true;
}
static void SdkTerminalDropInput(SdkTerminal* T){
    T->InputDropped+=T->InputSize;if(T->Input)xrtSecureZero(T->Input,SDK_TERMINAL_INPUT);T->InputSize=T->InputStart=0;
}
static bool SdkTerminalPrepareIo(xprocess* Process){
    (void)Process; /* Windows configures cancellable pipes inside xrt. */
#if !defined(_WIN32) && !defined(_WIN64)
    /* A blocking master write can remain asleep even after the slave exits.
     * xrt exposes borrowed native descriptors; preserve their flags and opt
     * only our owned PTY into nonblocking IO. Child/slave flags are unchanged. */
    for(unsigned Stream=XPROCESS_STDIN;Stream<=XPROCESS_STDOUT;Stream++){
        intptr_t Native=xrtProcessStreamNative(Process,(xprocessstream)Stream);if(Native<0||Native>INT32_MAX)return false;
        int Flags=fcntl((int)Native,MDO_TERM_GETFL,0);if(Flags<0||fcntl((int)Native,MDO_TERM_SETFL,Flags|MDO_TERM_NONBLOCK)<0)return false;
    }
#endif
    return true;
}
static int32 SdkTerminalReadPump(void* User){
    SdkTerminal* T=User;unsigned char Bytes[8192];
    for(;;){int64 N=xrtProcessRead(T->Process,XPROCESS_STDOUT,Bytes,sizeof(Bytes));
        if(N<0&&xrtGetError()&&xrtErrorKind(xrtGetError())==XERR_AGAIN){
            xrtMutexLock(SdkTerminals.Lock);bool Stop=T->Closing||T->Exited||SdkTerminals.Stopping;if(Stop){T->ReaderDone=true;xrtCondBroadcast(T->Changed);}xrtMutexUnlock(SdkTerminals.Lock);
            if(Stop)break;
            xrtSleep(25);
            continue;
        }
        if(N<=0){
            xrtMutexLock(SdkTerminals.Lock);if(N<0&&!T->Closing&&!T->Exited&&xrtProcessState(T->Process)!=XPROCESS_EXITED){T->InputFailed=true;snprintf(T->Error,sizeof(T->Error),"%s",xrtGetError()?xrtErrorMessage(xrtGetError()):"Terminal output read failed");}
            T->ReaderDone=true;xrtCondBroadcast(T->Changed);xrtMutexUnlock(SdkTerminals.Lock);break;
        }
        xrtMutexLock(SdkTerminals.Lock);size_t Count=(size_t)N,Free=SDK_TERMINAL_OUTPUT-T->OutputSize;
        if(Count>Free){size_t Drop=Count-Free;T->OutputStart=(T->OutputStart+Drop)%SDK_TERMINAL_OUTPUT;T->OutputSize-=Drop;}
        for(size_t i=0;i<Count;i++)T->Output[(T->OutputStart+T->OutputSize+i)%SDK_TERMINAL_OUTPUT]=Bytes[i];
        T->OutputSize+=Count;T->OutputTotal+=Count;xrtCondBroadcast(T->Changed);xrtMutexUnlock(SdkTerminals.Lock);
    }xrtSecureZero(Bytes,sizeof(Bytes));return 0;
}
static int32 SdkTerminalWritePump(void* User){
    SdkTerminal* T=User;unsigned char Bytes[SDK_TERMINAL_CHUNK];
    for(;;){
        xrtMutexLock(SdkTerminals.Lock);
        while(!T->InputSize&&!T->Closing&&!T->Exited&&!SdkTerminals.Stopping)xrtCondWait(T->Changed,SdkTerminals.Lock);
        if(T->Closing||T->Exited||SdkTerminals.Stopping){SdkTerminalDropInput(T);T->WriterDone=true;xrtCondBroadcast(T->Changed);xrtMutexUnlock(SdkTerminals.Lock);break;}
        size_t Count=T->InputSize<sizeof(Bytes)?T->InputSize:sizeof(Bytes);
        for(size_t i=0;i<Count;i++){Bytes[i]=T->Input[(T->InputStart+i)%SDK_TERMINAL_INPUT];T->Input[(T->InputStart+i)%SDK_TERMINAL_INPUT]=0;}
        T->InputStart=(T->InputStart+Count)%SDK_TERMINAL_INPUT;T->InputSize-=Count;xrtMutexUnlock(SdkTerminals.Lock);
        size_t At=0;while(At<Count){int64 N=xrtProcessWrite(T->Process,Bytes+At,Count-At);
            if(N<0&&xrtGetError()&&xrtErrorKind(xrtGetError())==XERR_AGAIN){xrtMutexLock(SdkTerminals.Lock);bool Stop=T->Closing||T->Exited||SdkTerminals.Stopping;xrtMutexUnlock(SdkTerminals.Lock);if(Stop)break;xrtSleep(10);continue;}
            if(N<=0)break;
            At+=(size_t)N;
        }
        xrtSecureZero(Bytes,Count);xrtMutexLock(SdkTerminals.Lock);T->InputWritten+=At;
        if(At!=Count){T->InputDropped+=Count-At;bool Stopped=T->Closing||T->Exited||SdkTerminals.Stopping||xrtProcessState(T->Process)==XPROCESS_EXITED;T->InputFailed=!Stopped;SdkTerminalDropInput(T);if(!Stopped)snprintf(T->Error,sizeof(T->Error),"%s",xrtGetError()?xrtErrorMessage(xrtGetError()):"Terminal input write failed");T->WriterDone=true;xrtCondBroadcast(T->Changed);xrtMutexUnlock(SdkTerminals.Lock);break;}
        xrtCondBroadcast(T->Changed);xrtMutexUnlock(SdkTerminals.Lock);
    }return 0;
}
static int32 SdkTerminalWaitPump(void* User){
    SdkTerminal* T=User;xwaitresult Wait=xrtProcessWait(T->Process);xprocessstatus Status={0};bool Has=Wait==XWAIT_OK&&xrtProcessStatus(T->Process,&Status);
    /* A completed shell may leave children holding the PTY open. The process
     * handle still owns its Job/group; finish it before waiting on I/O pumps. */
    bool TreeClean=Wait!=XWAIT_OK||xrtProcessKillTree(T->Process);
    xrtMutexLock(SdkTerminals.Lock);if(!TreeClean&&!T->Error[0])snprintf(T->Error,sizeof(T->Error),"Cannot finish terminal descendants");T->Exited=true;SdkPut(T->Record,"state","ended");SdkPut(T->Record,"reason",SdkTerminals.Stopping?"service_stopped":T->Closing?"closed":"process_exited");
    if(Has){xrtValueObjectSetNew(T->Record,XRT_STR_LITERAL("exit_code"),xrtValueInt(Status.Code));xrtValueObjectSetNew(T->Record,XRT_STR_LITERAL("signal"),xrtValueInt(Status.Signal));}
    else if(!T->Error[0])snprintf(T->Error,sizeof(T->Error),"Terminal process wait failed");
    T->Token[0]=0;SdkTerminalDropInput(T);xrtCondBroadcast(T->Changed);T->ReceiptUncertain=!SdkTerminalSave();
    while(!T->ReaderDone||!T->WriterDone||T->Pins)xrtCondWaitFor(T->Changed,SdkTerminals.Lock,25);
    xprocess* Process=T->Process;T->Process=NULL;MdoProjectLease* Lease=T->Lease;T->Lease=NULL;MdoCliUse* Commands=T->Commands;T->Commands=NULL;xrtMutexUnlock(SdkTerminals.Lock);
    /* No callback can still be inside xrtProcessRead/Write here. Retiring the
     * owned endpoint closes the PTY (HUP on POSIX / ConPTY+Job on Windows),
     * including children left behind by an exited shell. */
    if(Process)xrtProcessDestroy(Process);
    MdoCliUseEnd(Commands);
    MdoProjectLeaseRelease(Lease);
    xrtMutexLock(SdkTerminals.Lock);T->WaiterDone=true;xrtCondBroadcast(T->Changed);xrtMutexUnlock(SdkTerminals.Lock);return 0;
}
bool MdoApplicationTerminalsInit(void){
    SdkTerminals.Lock=xrtMutexCreate();if(!SdkTerminals.Lock)return false;
    SdkTerminals.Ready=SdkRandomId("pty-",SdkTerminals.Epoch);bool Exists=false;xfileinfo Info={0};
    if(!SdkTerminals.Ready||!MdoHomeExternalStat(SDK_TERMINAL_PATH,&Exists,&Info)){SdkTerminals.Ready=false;return true;}if(!Exists)return true;
    if(Info.Type!=XFILE_TYPE_FILE||Info.Size>SDK_TERMINAL_STORE){SdkTerminals.Ready=false;return true;}
    xfile File=MdoHomeOpenRead(SDK_TERMINAL_PATH);char* Text=xrtMalloc((size_t)Info.Size+1);xvalue* Root=NULL;
    if(File&&Text&&xrtReadFull(File,Text,(size_t)Info.Size,NULL)){Text[Info.Size]=0;Root=SdkParse(Text);}if(File)xrtClose(File);xrtFree(Text);
    uint64 Version=0;const xvalue* Rows=SdkGet(Root,"terminals");
    bool Valid=Root&&SdkUInt(SdkGet(Root,"schema_version"),&Version)&&Version==1&&xrtValueType(Rows)==XVALUE_ARRAY&&xrtValueCount(Rows)<=SDK_TERMINAL_SLOTS;
    for(size_t i=0;Valid&&i<xrtValueCount(Rows);i++){
        const xvalue* Row=xrtValueArrayGet(Rows,i);const xvalue* Context=SdkGet(Row,"context");
        Valid=SdkIdValid(SdkField(Row,"id"))&&SdkIdValid(SdkField(Row,"owner"))&&SdkField(Row,"key")&&SdkField(Row,"request_hash")&&xrtValueType(Context)==XVALUE_OBJECT&&SdkField(Context,"project")&&SdkField(Context,"session")&&SdkField(Context,"run")&&SdkField(Context,"workspace");
        for(size_t j=0;Valid&&j<i;j++)if(!strcmp(SdkField(Row,"id"),SdkField(SdkTerminals.Slots[j].Record,"id")))Valid=false;
        if(Valid){SdkTerminal* T=&SdkTerminals.Slots[i];T->Record=xrtValueClone(Row);T->Exited=T->ReaderDone=T->WriterDone=T->WaiterDone=true;
            cstr State=SdkField(Row,"state");if(!State||strcmp(State,"ended"))SdkPut(T->Record,"reason","service_restarted");SdkPut(T->Record,"state","ended");}
    }
    xrtValueRelease(Root);SdkTerminals.Ready=Valid;return true;
}
void MdoApplicationTerminalsStop(void){
    if(!SdkTerminals.Lock)return;
    xrtMutexLock(SdkTerminals.Lock);
    SdkTerminals.Stopping=true;
    for(size_t i=0;i<SDK_TERMINAL_SLOTS;i++){SdkTerminal* T=&SdkTerminals.Slots[i];if(T->Record){T->Closing=true;if(T->Changed)xrtCondBroadcast(T->Changed);}}
    xrtMutexUnlock(SdkTerminals.Lock);
    for(;;){xrtMutexLock(SdkTerminals.Lock);bool Starting=false;for(size_t i=0;i<SDK_TERMINAL_SLOTS;i++)if(SdkTerminals.Slots[i].Starting)Starting=true;xrtMutexUnlock(SdkTerminals.Lock);if(!Starting)break;xrtSleep(10);}
    xprocess* Processes[SDK_TERMINAL_SLOTS];xthread* Threads[SDK_TERMINAL_SLOTS*3];size_t Count=0,ThreadCount=0;xrtMutexLock(SdkTerminals.Lock);
    for(size_t i=0;i<SDK_TERMINAL_SLOTS;i++){SdkTerminal* T=&SdkTerminals.Slots[i];if(!T->Record||T->Releasing)continue;if(T->Process)Processes[Count++]=xrtProcessRef(T->Process);
        if(T->Reader)Threads[ThreadCount++]=xrtThreadRef(T->Reader);
        if(T->Writer)Threads[ThreadCount++]=xrtThreadRef(T->Writer);
        if(T->Waiter)Threads[ThreadCount++]=xrtThreadRef(T->Waiter);
    }xrtMutexUnlock(SdkTerminals.Lock);
    for(size_t i=0;i<Count;i++){(void)xrtProcessKillTree(Processes[i]);xrtProcessDestroy(Processes[i]);}
    /* ServiceUnit drains these callbacks BEFORE ProjectLifecycleUnit. Native
     * terminal leases must never outlive that manager or the script generation. */
    for(size_t i=0;i<ThreadCount;i++){xrtThreadWait(Threads[i]);xrtThreadDestroy(Threads[i]);}
    MdoProjectLease* Leases[SDK_TERMINAL_SLOTS];size_t LeaseCount=0;xrtMutexLock(SdkTerminals.Lock);
    for(size_t i=0;i<SDK_TERMINAL_SLOTS;i++){SdkTerminal* T=&SdkTerminals.Slots[i];if(T->Lease){Leases[LeaseCount++]=T->Lease;T->Lease=NULL;}}xrtMutexUnlock(SdkTerminals.Lock);
    for(size_t i=0;i<LeaseCount;i++)MdoProjectLeaseRelease(Leases[i]);
}
void MdoApplicationTerminalsUnit(void){
    if(!SdkTerminals.Lock)return;
    MdoApplicationTerminalsStop();
    for(size_t i=0;i<SDK_TERMINAL_SLOTS;i++)SdkTerminalFree(&SdkTerminals.Slots[i]);
    xrtMutexDestroy(SdkTerminals.Lock);
    memset(&SdkTerminals,0,sizeof(SdkTerminals));
}
xvalue* MdoApplicationTerminalsCatalog(void){
    xvalue* A=xrtValueArray();for(size_t i=0;i<sizeof(SdkTerminalMethods)/sizeof(*SdkTerminalMethods);i++){xvalue* Row=xrtValueObject();SdkPut(Row,"id",SdkTerminalMethods[i]);SdkPut(Row,"domain","terminal");SdkPut(Row,"transport","native");xrtValueArrayAppendNew(A,Row);}return A;
}
static char* SdkTerminalDefault(void){
#if defined(_WIN32) || defined(_WIN64)
    char* System=xrtEnvGet("SystemRoot");char* Program=System&&System[0]?xrtPathJoin(System,"System32/WindowsPowerShell/v1.0/powershell.exe"):NULL;xrtFree(System);xfileinfo Info={0};
    if(Program&&xrtPathStat(Program,true,&Info)&&Info.Type==XFILE_TYPE_FILE)return Program;
    xrtFree(Program);
    Program=xrtEnvGet("ComSpec");if(Program&&Program[0])return Program;xrtFree(Program);return xrtStrDup("cmd.exe");
#elif defined(__ANDROID__)
    return xrtStrDup("/system/bin/sh");
#else
    char* Shell=xrtEnvGet("SHELL");if(Shell&&Shell[0])return Shell;xrtFree(Shell);return xrtStrDup("/bin/sh");
#endif
}
static bool SdkTerminalCreateImpl(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    if(!xrtProcessTerminalSupported())return SdkTerminalFail(E,"terminal_unsupported","The current runtime has no supported PTY backend",501);
    cstr Key=SdkField(P,"key"),Program=SdkField(P,"program"),Title=SdkField(P,"title");const xvalue* Args=SdkGet(P,"args");uint64 Columns=80,Rows=24;
    if(!SdkIdValid(Key)||strlen(Key)>128||(Program&&(!Program[0]||strlen(Program)>4096))||(Title&&strlen(Title)>256)||(Args&&xrtValueType(Args)!=XVALUE_ARRAY)||xrtValueCount(Args)>32||
       (SdkGet(P,"columns")&&!SdkUInt(SdkGet(P,"columns"),&Columns))||(SdkGet(P,"rows")&&!SdkUInt(SdkGet(P,"rows"),&Rows))||Columns<2||Columns>500||Rows<2||Rows>300)return SdkTerminalFail(E,"terminal_arguments_invalid","Supply a stable creation key, program argv and valid terminal dimensions",400);
    cstr Argv[32];size_t Total=0;for(size_t i=0;i<xrtValueCount(Args);i++){Argv[i]=SdkText(xrtValueArrayGet(Args,i));if(!Argv[i]||strlen(Argv[i])>8192||(Total+=strlen(Argv[i]))>32768)return SdkTerminalFail(E,"terminal_arguments_invalid","Terminal arguments exceed the string budget",400);}
    MdoProjectLease* AdmissionLease=NULL;if(!SdkJobProject(C,"terminal.create",&AdmissionLease,E))return false;
    char* Default=Program?NULL:SdkTerminalDefault();if(!Program)Program=Default;
    cstr RequestedDirectory=SdkField(P,"cwd");char* DefaultDirectory=!RequestedDirectory&&SdkTerminalEqual(C->ProjectId,"default")?MdoHomeDefaultWorkspacePath(false):NULL;
    bool DefaultWorkspace=DefaultDirectory&&(!C->Workspace||!C->Workspace[0]||SdkTerminalEqual(C->Workspace,DefaultDirectory));xrtFree(DefaultDirectory);
    xvalue* Path=xrtValueObject();SdkPut(Path,"path",RequestedDirectory?RequestedDirectory:".");char* Directory=DefaultWorkspace?MdoHomeDefaultWorkspacePath(true):SdkNativePath(C,Path,false,E);xrtValueRelease(Path);xfileinfo Stat={0};
    if(!Directory||!Program||!xrtPathStat(Directory,true,&Stat)||Stat.Type!=XFILE_TYPE_DIRECTORY){MdoProjectLeaseRelease(AdmissionLease);xrtFree(Directory);xrtFree(Default);if(!E->Code[0])SdkTerminalFail(E,"terminal_directory_invalid","Select an existing terminal directory",400);return false;}
    xvalue* Canonical=SdkCanonicalValue(P,0);char* Json=Canonical?SdkJson(Canonical):NULL;char Hash[65];bool HasHash=Json&&MdoExtensionHash(Json,Hash);if(Json)xrtSecureZero(Json,strlen(Json));xrtFree(Json);xrtValueRelease(Canonical);
    char ReceiptPath[128];HasHash=HasHash&&SdkTerminalReceiptPath(C,Key,ReceiptPath);
    if(!HasHash){MdoProjectLeaseRelease(AdmissionLease);xrtFree(Directory);xrtFree(Default);return SdkTerminalFail(E,"terminal_arguments_invalid","Terminal request cannot be serialized",400);}
    xrtMutexLock(SdkTerminals.Lock);SdkTerminal* T=NULL;size_t Active=0;
    for(size_t i=0;i<SDK_TERMINAL_SLOTS;i++){
        SdkTerminal* Slot=&SdkTerminals.Slots[i];if(Slot->Record){mdo_error_v1 Ignored={0};if(SdkTerminalEqual(Key,SdkField(Slot->Record,"key"))&&SdkTerminalOwn(C,Slot,&Ignored)){
            bool Same=!strcmp(Hash,SdkField(Slot->Record,"request_hash"));if(Same)*R=SdkTerminalInfo(Slot);xrtMutexUnlock(SdkTerminals.Lock);MdoProjectLeaseRelease(AdmissionLease);xrtFree(Directory);xrtFree(Default);
            return Same||SdkTerminalFail(E,"terminal_identity_conflict","This creation key already belongs to another request",409);
        }if(!Slot->Exited)Active++;}else if(!T&&!Slot->Releasing)T=Slot;
    }
    if(SdkTerminalReceiptExists(ReceiptPath,Hash,E)){xrtMutexUnlock(SdkTerminals.Lock);MdoProjectLeaseRelease(AdmissionLease);xrtFree(Directory);xrtFree(Default);return false;}
    if(SdkTerminals.Stopping||!T||Active>=SDK_TERMINAL_ACTIVE){xrtMutexUnlock(SdkTerminals.Lock);MdoProjectLeaseRelease(AdmissionLease);xrtFree(Directory);xrtFree(Default);return SdkTerminalFail(E,SdkTerminals.Stopping?"sdk_stopping":"terminal_limit",SdkTerminals.Stopping?"Application is stopping":"Close running terminals or release ended terminals before creating more",SdkTerminals.Stopping?503:429);}
    T->Lease=AdmissionLease;
    char Id[37]={0};bool Ready=SdkRandomId("pty-",Id);T->Record=xrtValueObject();SdkPut(T->Record,"id",Id);SdkPut(T->Record,"key",Key);SdkPut(T->Record,"request_hash",Hash);SdkPut(T->Record,"owner",C->ExtensionId);SdkPut(T->Record,"title",Title?Title:"\xe7\xbb\x88\xe7\xab\xaf");SdkPut(T->Record,"program",Program);SdkPut(T->Record,"cwd",Directory);SdkPut(T->Record,"state","starting");SdkPutUInt(T->Record,"columns",Columns);SdkPutUInt(T->Record,"rows",Rows);
    xvalue* Scope=xrtValueObject();SdkPut(Scope,"project",C->ProjectId);SdkPut(Scope,"session",C->SessionId);SdkPut(Scope,"run",C->RunId);SdkPut(Scope,"workspace",C->Workspace);SdkPut(Scope,"device",C->DeviceId);xrtValueObjectSetNew(T->Record,XRT_STR_LITERAL("context"),Scope);
    T->Output=xrtMalloc(SDK_TERMINAL_OUTPUT);T->Input=xrtMalloc(SDK_TERMINAL_INPUT);T->Changed=xrtCondCreate();T->Starting=true;
    Ready=Ready&&T->Record&&Scope&&T->Output&&T->Input&&T->Changed&&SdkTerminalReceiptSave(ReceiptPath,T->Record)&&SdkTerminalSave();
    if(!Ready){T->Starting=false;SdkTerminalFree(T);xrtMutexUnlock(SdkTerminals.Lock);xrtFree(Directory);xrtFree(Default);return SdkTerminalFail(E,"terminal_admission_failed","Terminal was not started because its admission receipt could not be stored",503);}
    xrtMutexUnlock(SdkTerminals.Lock);
    const xprocessenv Env[]={{"TERM","xterm-256color"},{"COLORTERM","truecolor"},{"TERM_PROGRAM","mdo"}};
    xprocessconfig Config;xrtProcessConfigInit(&Config);Config.Program=Program;Config.Args=Argv;Config.ArgCount=xrtValueCount(Args);Config.WorkDir=Directory;Config.HideWindow=false;Config.NewGroup=true;Config.Terminal=true;Config.Columns=(uint32)Columns;Config.Rows=(uint32)Rows;Config.Env=Env;Config.EnvCount=sizeof(Env)/sizeof(*Env);Config.Stdin.Mode=Config.Stdout.Mode=XPROCESS_IO_PIPE;Config.Stderr.Mode=XPROCESS_IO_MERGE;
    xrtMutexLock(SdkTerminals.Lock);bool Cancelled=T->Closing||SdkTerminals.Stopping||SdkCancelled(C);xrtMutexUnlock(SdkTerminals.Lock);
    MdoCliUse* Commands=NULL;char CommandError[512]={0};bool CommandsReady=Cancelled||MdoCliUseBegin(&Commands,CommandError);
    xprocess* Process=Cancelled||!CommandsReady?NULL:xrtProcessSpawn(&Config);mdo_error_v1 SpawnError={0};bool IoReady=Process&&SdkTerminalPrepareIo(Process);
    if(Cancelled)SdkTerminalFail(&SpawnError,"sdk_cancelled","Terminal creation was cancelled before starting a process",499);
    else if(!CommandsReady)SdkTerminalFail(&SpawnError,"sdk_commands_unavailable",CommandError,503);
    else if(!Process)SdkNativeFailure(&SpawnError,"terminal");
    else if(!IoReady)SdkTerminalFail(&SpawnError,"terminal_io_setup_failed","Cannot configure cancellable PTY I/O",500);
    xrtFree(Directory);
    xrtFree(Default);
    if(!Process){MdoCliUseEnd(Commands);Commands=NULL;}
    xrtMutexLock(SdkTerminals.Lock);T->Process=Process;T->Commands=Commands;bool Spawned=Process!=NULL;
    if(Process){if(IoReady){T->Reader=xrtThreadCreate(SdkTerminalReadPump,T,0);T->Writer=xrtThreadCreate(SdkTerminalWritePump,T,0);}T->Waiter=xrtThreadCreate(SdkTerminalWaitPump,T,0);T->ReaderDone=!T->Reader;T->WriterDone=!T->Writer;T->WaiterDone=!T->Waiter;Spawned=IoReady&&T->Reader&&T->Writer&&T->Waiter;}
    T->Starting=false;
    if(!Spawned||SdkTerminals.Stopping||T->Closing){T->Closing=true;if(!Process){T->Exited=T->ReaderDone=T->WriterDone=T->WaiterDone=true;SdkPut(T->Record,"state","ended");SdkPut(T->Record,"reason",Cancelled?"cancelled_before_start":"not_started");}else SdkPut(T->Record,"state","closing");
        if(!Spawned)snprintf(T->Error,sizeof(T->Error),"%s",SpawnError.Message[0]?SpawnError.Message:"Terminal I/O could not start");
        if(T->Changed)xrtCondBroadcast(T->Changed);
    }else SdkPut(T->Record,"state","running");
    T->ReceiptUncertain=!SdkTerminalSave();*R=SdkTerminalInfo(T);bool Kill=Process&&T->Closing;if(Kill)T->Pins++;MdoProjectLease* UnstartedLease=NULL;if(!Process){UnstartedLease=T->Lease;T->Lease=NULL;}xrtMutexUnlock(SdkTerminals.Lock);MdoProjectLeaseRelease(UnstartedLease);
    if(Kill){(void)xrtProcessKillTree(Process);bool Exited=!T->Waiter&&xrtProcessWaitFor(Process,5000)==XWAIT_OK;xrtMutexLock(SdkTerminals.Lock);if(Exited){T->Exited=true;SdkPut(T->Record,"state","ended");}T->Pins--;xrtMutexUnlock(SdkTerminals.Lock);}
    if(!Spawned){xrtValueRelease(*R);*R=NULL;if(SpawnError.Code[0]){*E=SpawnError;E->MayHaveCompleted=Process!=NULL;}else MdoApplicationError(E,"terminal_start_failed","Terminal I/O could not start; inspect its retained creation record","terminal",500,Process!=NULL);return false;}return true;
}
static bool SdkTerminalCreate(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    xwork_error Error;MdoProjectLease* Lease=MdoProjectLeaseAcquire(C->ProjectId&&C->ProjectId[0]?C->ProjectId:"default",MDO_PROJECT_LEASE_SHARED,&Error);
    MdoProjectDefinitionLease* Writer=Lease?MdoProjectDefinitionAcquire(Lease,&Error):NULL;
    if(!Writer){MdoProjectLeaseRelease(Lease);return SdkTerminalFail(E,"terminal_project_busy","Project definitions are changing; no terminal was admitted",409);}
    bool Ok=SdkTerminalCreateImpl(C,P,R,E);MdoProjectDefinitionRelease(Writer);MdoProjectLeaseRelease(Lease);return Ok;
}
bool MdoApplicationTerminalsProjectBusy(cstr Project){
    if(!SdkTerminals.Lock)return false;
    bool Busy=false;
    char Key[65];
    size_t N=strlen(Project);
    if(N>=sizeof(Key))return true;
    while(N&&Project[N-1]=='.')N--;
    for(size_t i=0;i<N;i++)Key[i]=Project[i]>='A'&&Project[i]<='Z'?Project[i]+'a'-'A':Project[i];
    Key[N]=0;
    xrtMutexLock(SdkTerminals.Lock);
    for(size_t i=0;i<SDK_TERMINAL_SLOTS;i++){
        SdkTerminal* T=&SdkTerminals.Slots[i];if(!T->Record||T->Releasing)continue;
        cstr Id=SdkField(SdkGet(T->Record,"context"),"project");if(!Id)continue;size_t Size=strlen(Id);while(Size&&Id[Size-1]=='.')Size--;if(Size!=N)continue;
        bool Same=true;for(size_t j=0;j<N;j++){char Ch=Id[j]>='A'&&Id[j]<='Z'?Id[j]+'a'-'A':Id[j];if(Ch!=Key[j])Same=false;}
        if(Same&&(T->Starting||!T->Exited||!T->ReaderDone||!T->WriterDone||!T->WaiterDone||T->Pins)){Busy=true;break;}
    }xrtMutexUnlock(SdkTerminals.Lock);return Busy;
}
/* Explicit housekeeping only: it never reveals another context's bytes or
 * changes a live PTY. A corrupt/unreachable workspace is not proof of removal.
 * Keep the independent admission receipts so cleanup cannot replay creation. */
static bool SdkTerminalOrphaned(const SdkTerminal* T){
    const xvalue* Scope=SdkGet(T->Record,"context");cstr Project=SdkField(Scope,"project"),Workspace=SdkField(Scope,"workspace");
    if(!Project||!Project[0]||!strcmp(Project,"default")||!Workspace||!Workspace[0])return false;
    MdoProjectInfo Info={0};Info.Size=sizeof(Info);bool Found=false;xwork_error Error;
    if(!MdoProjectGet(Project,&Info,&Found,&Error))return false;
    if(!Found)return true;
    MdoProjectBinding Current={0};Current.Size=sizeof(Current);xfileinfo Previous={0};
    if(!MdoProjectBindingGet(Project,&Current,&Error)||!xrtPathStat(Workspace,true,&Previous)||Previous.Type!=XFILE_TYPE_DIRECTORY)return false;
    return Previous.Device!=Current.WorkspaceIdentity.Device||Previous.Identity!=Current.WorkspaceIdentity.Identity;
}
static bool SdkTerminalPrune(const mdo_sdk_context_v1* C,xvalue** R,mdo_error_v1* E){
    xwork_error Error;MdoProjectLease* Lease=MdoProjectLeaseAcquire("default",MDO_PROJECT_LEASE_SHARED,&Error);
    MdoProjectDefinitionLease* Writer=Lease?MdoProjectDefinitionAcquire(Lease,&Error):NULL;
    if(!Writer){MdoProjectLeaseRelease(Lease);return SdkTerminalFail(E,"terminal_cleanup_busy","Project definitions are changing; retry cleanup after they settle",409);}
    SdkTerminal Retired[SDK_TERMINAL_SLOTS];size_t Slots[SDK_TERMINAL_SLOTS],Count=0;bool Ok=false;
    xrtMutexLock(SdkTerminals.Lock);
    if(SdkTerminals.Stopping){SdkTerminalFail(E,"sdk_stopping","Terminal service is stopping",503);goto done;}
    for(size_t i=0;i<SDK_TERMINAL_SLOTS;i++){
        SdkTerminal* T=&SdkTerminals.Slots[i];
        if(!T->Record||T->Releasing||T->Starting||!T->Exited||!T->ReaderDone||!T->WriterDone||!T->WaiterDone||T->Pins||!SdkTerminalEqual(C->ExtensionId,SdkField(T->Record,"owner"))||!SdkTerminalOrphaned(T))continue;
        Slots[Count]=i;Retired[Count++]=*T;T->Record=NULL;T->Releasing=true;
    }
    if(Count&&!SdkTerminalSave()){
        for(size_t i=0;i<Count;i++)SdkTerminals.Slots[Slots[i]]=Retired[i];
        SdkTerminalFail(E,"terminal_store_failed","Ended terminal records could not be removed; original records were retained",503);goto done;
    }
    xrtMutexUnlock(SdkTerminals.Lock);
    for(size_t i=0;i<Count;i++)SdkTerminalFree(&Retired[i]);
    xrtMutexLock(SdkTerminals.Lock);for(size_t i=0;i<Count;i++)memset(&SdkTerminals.Slots[Slots[i]],0,sizeof(SdkTerminal));
    *R=xrtValueObject();SdkPutUInt(*R,"released",Count);Ok=true;
done:xrtMutexUnlock(SdkTerminals.Lock);MdoProjectDefinitionRelease(Writer);MdoProjectLeaseRelease(Lease);xrtClearError();return Ok;
}
bool MdoApplicationTerminalsCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E,bool* Found){
    *Found=false;for(size_t i=0;i<sizeof(SdkTerminalMethods)/sizeof(*SdkTerminalMethods);i++)if(!strcmp(Method,SdkTerminalMethods[i]))*Found=true;if(!*Found)return true;
    if(!SdkTerminals.Lock||!SdkTerminals.Ready)return SdkTerminalFail(E,"terminal_store_unavailable","Terminal metadata is unavailable or corrupt; no process was restarted",503);
    if(!strcmp(Method,"terminal.capabilities")){*R=xrtValueObject();xrtValueObjectSetNew(*R,XRT_STR_LITERAL("supported"),xrtValueBool(xrtProcessTerminalSupported()));SdkPut(*R,"epoch",SdkTerminals.Epoch);char* Program=SdkTerminalDefault();SdkPut(*R,"default_program",Program);xrtFree(Program);SdkPutUInt(*R,"max_active",SDK_TERMINAL_ACTIVE);SdkPutUInt(*R,"max_sessions",SDK_TERMINAL_SLOTS);SdkPutUInt(*R,"output_capacity",SDK_TERMINAL_OUTPUT);SdkPutUInt(*R,"input_capacity",SDK_TERMINAL_INPUT);SdkPutUInt(*R,"max_input_chunk",SDK_TERMINAL_CHUNK);SdkPutUInt(*R,"max_read_chunk",65536);SdkPutUInt(*R,"max_wait_ms",1000);SdkPutUInt(*R,"input_lease_ms",15000);SdkPutUInt(*R,"max_columns",500);SdkPutUInt(*R,"max_rows",300);return true;}
    if(!strcmp(Method,"terminal.create"))return SdkTerminalCreate(C,P,R,E);
    if(!strcmp(Method,"terminal.prune"))return SdkTerminalPrune(C,R,E);
    xrtMutexLock(SdkTerminals.Lock);
    if(!strcmp(Method,"terminal.list")){*R=xrtValueArray();for(size_t i=0;i<SDK_TERMINAL_SLOTS;i++){SdkTerminal* T=&SdkTerminals.Slots[i];mdo_error_v1 Ignored={0};if(T->Record&&!T->Releasing&&SdkTerminalOwn(C,T,&Ignored))xrtValueArrayAppendNew(*R,SdkTerminalInfo(T));}xrtMutexUnlock(SdkTerminals.Lock);return true;}
    cstr Id=SdkField(P,"id");SdkTerminal* T=NULL;for(size_t i=0;Id&&i<SDK_TERMINAL_SLOTS;i++)if(SdkTerminals.Slots[i].Record&&SdkTerminalEqual(Id,SdkField(SdkTerminals.Slots[i].Record,"id")))T=&SdkTerminals.Slots[i];
    bool Ok=false;if(!T){SdkTerminalFail(E,"terminal_missing","Terminal is no longer retained",404);goto done;}if(!SdkTerminalOwn(C,T,E))goto done;
    if(T->Releasing){SdkTerminalFail(E,"terminal_releasing","Terminal is being released",409);goto done;}
    if(!strcmp(Method,"terminal.get")){*R=SdkTerminalInfo(T);Ok=true;goto done;}
    if(SdkTerminals.Stopping){SdkTerminalFail(E,"sdk_stopping","Terminal service is stopping",503);goto done;}
    if(!strcmp(Method,"terminal.attach")){
        cstr Client=SdkField(P,"client");bool Control=false;if(!SdkIdValid(Client)){SdkTerminalFail(E,"terminal_arguments_invalid","Supply an explicit client identity",400);goto done;}xrtValueGetBool(SdkGet(P,"control"),&Control);
        if(Control&&!T->Exited&&!T->Closing&&!T->Starting){
            if(T->Token[0]&&!MdoDeadlineExpired(T->LeaseUntil)&&strcmp(Client,T->Client)){SdkTerminalFail(E,"terminal_control_busy","Another client currently controls this terminal",409);goto done;}
            if(!T->Token[0]||MdoDeadlineExpired(T->LeaseUntil)){
                if(!SdkRandomId("ctl-",T->Token)||!SdkRandomId("inp-",T->InputEpoch)){SdkNativeFailure(E,"terminal.control");goto done;}
                T->Sequence=0;memset(T->Writes,0,sizeof(T->Writes));snprintf(T->Client,sizeof(T->Client),"%s",Client);
            }T->LeaseUntil=MdoDeadlineAfterUs(15000000);
        }
        *R=SdkTerminalInfo(T);if(Control&&!T->Exited&&!T->Starting&&!T->Closing){SdkPut(*R,"control_token",T->Token);SdkPut(*R,"input_epoch",T->InputEpoch);SdkPutUInt(*R,"next_sequence",T->Sequence+1);SdkPutUInt(*R,"lease_ms",15000);}Ok=true;goto done;
    }
    if(!strcmp(Method,"terminal.detach")){if(!SdkTerminalLease(T,P,E))goto done;T->Token[0]=0;*R=SdkTerminalInfo(T);Ok=true;goto done;}
    if(!strcmp(Method,"terminal.read")){
        uint64 Cursor=0,Limit=16384,Wait=0;if((SdkGet(P,"cursor")&&!SdkUInt(SdkGet(P,"cursor"),&Cursor))||(SdkGet(P,"limit")&&!SdkUInt(SdkGet(P,"limit"),&Limit))||!Limit||Limit>65536||(SdkGet(P,"wait_ms")&&!SdkUInt(SdkGet(P,"wait_ms"),&Wait))||Wait>1000){SdkTerminalFail(E,"terminal_arguments_invalid","Invalid terminal cursor, read limit or wait",400);goto done;}
        if(SdkField(P,"epoch")&&!SdkTerminalEqual(SdkField(P,"epoch"),SdkTerminals.Epoch)){SdkTerminalFail(E,"terminal_epoch_changed","Terminal service restarted; begin a new view without replaying input",410);goto done;}
        if(Cursor>T->OutputTotal){SdkTerminalFail(E,"terminal_cursor_invalid","Output cursor is ahead of this terminal",416);goto done;}
        uint64 End=MdoDeadlineAfterUs(Wait*1000);T->Pins++;
        while(Wait&&Cursor==T->OutputTotal&&!T->ReaderDone&&!SdkTerminals.Stopping&&!MdoDeadlineExpired(End)&&!SdkCancelled(C))xrtCondWaitFor(T->Changed,SdkTerminals.Lock,25);
        T->Pins--;if(SdkCancelled(C)){SdkTerminalFail(E,"sdk_cancelled","Terminal read cancelled; process remains running",499);goto done;}
        uint64 Oldest=T->OutputTotal-T->OutputSize,Start=Cursor<Oldest?Oldest:Cursor;size_t Count=(size_t)(T->OutputTotal-Start);if(Count>Limit)Count=(size_t)Limit;unsigned char Bytes[65536];
        for(size_t i=0;i<Count;i++)Bytes[i]=T->Output[(T->OutputStart+(size_t)(Start-Oldest)+i)%SDK_TERMINAL_OUTPUT];
        *R=SdkTerminalInfo(T);SdkPutUInt(*R,"offset",Start);SdkPutUInt(*R,"next",Start+Count);xrtValueObjectSetNew(*R,XRT_STR_LITERAL("gap"),xrtValueBool(Start!=Cursor));Ok=SdkByteValue(*R,Bytes,Count);xrtSecureZero(Bytes,Count);goto done;
    }
    if(!strcmp(Method,"terminal.write")){
        if(T->Starting||T->Closing||T->Exited||T->InputFailed){SdkTerminalFail(E,"terminal_not_writable","Terminal is starting, ended, closing or its input failed",409);goto done;}if(!SdkTerminalLease(T,P,E))goto done;
        uint64 Sequence=0;if(!SdkUInt(SdkGet(P,"sequence"),&Sequence)||!Sequence){SdkTerminalFail(E,"terminal_arguments_invalid","Supply the next positive input sequence",400);goto done;}
        cstr Text=SdkField(P,"text"),Encoded=SdkField(P,"base64");if((Text&&strlen(Text)>SDK_TERMINAL_CHUNK)||(Encoded&&strlen(Encoded)>SDK_TERMINAL_CHUNK*2)){SdkTerminalFail(E,"terminal_input_limit","Split terminal input into chunks of at most 8192 bytes",413);goto done;}
        unsigned char* Bytes=NULL;size_t Count=0;if(!SdkInputBytes(P,&Bytes,&Count,E))goto done;
        if(Count>SDK_TERMINAL_CHUNK){xrtSecureZero(Bytes,Count);xrtFree(Bytes);SdkTerminalFail(E,"terminal_input_limit","Split terminal input into chunks of at most 8192 bytes",413);goto done;}
        char* Base64=SdkBase64(Bytes,Count);char Hash[65];bool HasHash=Base64&&MdoExtensionHash(Base64,Hash);if(Base64)xrtSecureZero(Base64,strlen(Base64));xrtFree(Base64);bool Duplicate=Sequence<=T->Sequence;
        SdkTerminalWrite* Receipt=&T->Writes[Sequence%32];
        if(!HasHash|| (Duplicate&&(Receipt->Sequence!=Sequence||strcmp(Hash,Receipt->Hash))) ){SdkTerminalFail(E,"terminal_input_uncertain","Input sequence was reused or its receipt expired; inspect output instead of replaying input",409);}
        else if(!Duplicate&&Sequence!=T->Sequence+1)SdkTerminalFail(E,"terminal_input_order","Input sequence is out of order",409);
        else if(!Duplicate&&Count>SDK_TERMINAL_INPUT-T->InputSize)SdkTerminalFail(E,"terminal_input_backpressure","Terminal input queue is full; retain this unadmitted chunk and wait",429);
        else{
            if(!Duplicate){for(size_t i=0;i<Count;i++)T->Input[(T->InputStart+T->InputSize+i)%SDK_TERMINAL_INPUT]=Bytes[i];T->InputSize+=Count;T->InputAdmitted+=Count;T->Sequence=Sequence;Receipt->Sequence=Sequence;Receipt->Bytes=Count;snprintf(Receipt->Hash,sizeof(Receipt->Hash),"%s",Hash);xrtCondBroadcast(T->Changed);}
            *R=SdkTerminalInfo(T);SdkPutUInt(*R,"sequence",Sequence);SdkPutUInt(*R,"accepted",Count);xrtValueObjectSetNew(*R,XRT_STR_LITERAL("duplicate"),xrtValueBool(Duplicate));Ok=true;
        }xrtSecureZero(Bytes,Count);xrtFree(Bytes);goto done;
    }
    if(!strcmp(Method,"terminal.resize")){
        uint64 Columns=0,Rows=0;if(!SdkUInt(SdkGet(P,"columns"),&Columns)||!SdkUInt(SdkGet(P,"rows"),&Rows)||Columns<2||Columns>500||Rows<2||Rows>300){SdkTerminalFail(E,"terminal_arguments_invalid","Invalid terminal dimensions",400);goto done;}
        if(!SdkTerminalLease(T,P,E))goto done;
        if(!T->Process||T->Closing||T->Exited){SdkTerminalFail(E,"terminal_not_writable","Terminal has ended",409);
        goto done;
        }
        T->Pins++;xprocess* Process=xrtProcessRef(T->Process);xrtMutexUnlock(SdkTerminals.Lock);bool Resized=xrtProcessResize(Process,(uint32)Columns,(uint32)Rows);xrtProcessDestroy(Process);xrtMutexLock(SdkTerminals.Lock);T->Pins--;
        if(!Resized){SdkNativeFailure(E,"terminal");goto done;}SdkPutUInt(T->Record,"columns",Columns);SdkPutUInt(T->Record,"rows",Rows);*R=SdkTerminalInfo(T);Ok=true;goto done;
    }
    if(!strcmp(Method,"terminal.close")){
        if(!SdkTerminalEqual(SdkField(P,"epoch"),SdkTerminals.Epoch)){SdkTerminalFail(E,"terminal_epoch_changed","Read the current terminal identity before closing it",410);goto done;}
        T->Closing=true;SdkPut(T->Record,"state",T->Exited?"ended":"closing");T->Token[0]=0;SdkTerminalDropInput(T);if(T->Changed)xrtCondBroadcast(T->Changed);T->ReceiptUncertain=!SdkTerminalSave();
        xprocess* Process=T->Process?xrtProcessRef(T->Process):NULL;T->Pins++;xrtMutexUnlock(SdkTerminals.Lock);bool Closed=!Process||xrtProcessKillTree(Process);if(Process)xrtProcessDestroy(Process);xrtMutexLock(SdkTerminals.Lock);T->Pins--;
        if(!Closed){SdkNativeFailure(E,"terminal");if(E)E->MayHaveCompleted=true;goto done;}*R=SdkTerminalInfo(T);Ok=true;goto done;
    }
    if(!strcmp(Method,"terminal.release")){
        if(!T->Exited||T->Starting||!T->ReaderDone||!T->WriterDone||!T->WaiterDone||T->Pins){SdkTerminalFail(E,"terminal_busy","End the terminal and wait for its I/O before releasing it",409);goto done;}
        xvalue* Record=T->Record;T->Record=NULL;if(!SdkTerminalSave()){T->Record=Record;SdkTerminalFail(E,"terminal_store_failed","Terminal metadata could not be removed; no process was replayed",503);goto done;}T->Releasing=true;SdkTerminal Retired=*T;Retired.Record=Record;
        xrtMutexUnlock(SdkTerminals.Lock);SdkTerminalFree(&Retired);xrtMutexLock(SdkTerminals.Lock);memset(T,0,sizeof(*T));*R=xrtValueObject();Ok=true;goto done;
    }
done:xrtMutexUnlock(SdkTerminals.Lock);return Ok;
}
