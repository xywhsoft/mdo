/* Private child-process bootstrap, never included in the product unity. */
#include <xsbase.h>
#include "mdo/command.h"
#include "mdo/sdk.h"
#include <xllm-time.h>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <process.h>
#else
/* The pinned compact TCC SDK intentionally omits POSIX system headers.
 * These stable libc declarations cover only the Linux/Android pipe watchdog. */
struct CWPosixPoll {int fd;short events,revents;};
extern int poll(struct CWPosixPoll*,unsigned long,int);
extern int getpid(void);
extern int kill(int,int);
extern void _exit(int);
#endif

#define CW_LIMIT (1024u*1024u)
static char CWError[2048];
static unsigned CWSequence;
static char* CWRoot;
static xatomic32 CWDone;
static int32 CWParentWatch(void* Unused) {
    (void)Unused;
    while(!xrtAtomic32Load(&CWDone,XMEMORY_ACQUIRE)){
#if defined(_WIN32) || defined(_WIN64)
        DWORD Available=0;
        if(!PeekNamedPipe(GetStdHandle(STD_INPUT_HANDLE),NULL,0,NULL,&Available,NULL))_exit(125);
        xrtSleep(50);
#else
        struct CWPosixPoll P={0,1,0};int Ready=poll(&P,1,50);
        if(Ready>0&&(P.revents&(16|8|32))){
            /* Only our own process group, never the host's group. */
            (void)kill(-getpid(),9);_exit(125);
        }
#endif
    }
    return 0;
}
static cstr CWText(const xvalue* V,cstr K) {
    xstrview T={0};return xrtValueGetString(xrtValueObjectGet(V,xrtStrView(K)),&T)&&
        !memchr(T.Data,0,T.Size)?T.Data:"";
}
static const char* CWString(const xvalue* V) {
    xstrview T={0};return xrtValueGetString(V,&T)&&!memchr(T.Data,0,T.Size)?T.Data:NULL;
}
static const xvalue* CWGet(const xvalue* V,const char* K){return xrtValueObjectGet(V,xrtStrView(K));}
static xvalue* CWParse(const char* S){return S&&strlen(S)<=CW_LIMIT?xrtJsonParse(xrtStrView(S)):NULL;}
static const xvalue* CWAt(const xvalue* V,size_t I){return xrtValueArrayGet(V,I);}
static char* CWJson(const xvalue* V){return xrtJsonStringify(V,false,NULL);}
static mdo_command_value_kind CWKind(const xvalue* V) {
    switch(xrtValueType(V)){
    case XVALUE_NULL:return MDO_COMMAND_NULL;case XVALUE_BOOL:return MDO_COMMAND_BOOL;
    case XVALUE_INT:return MDO_COMMAND_INT;case XVALUE_UINT:return MDO_COMMAND_UINT;
    case XVALUE_FLOAT:return MDO_COMMAND_NUMBER;case XVALUE_STRING:return MDO_COMMAND_STRING;
    case XVALUE_ARRAY:return MDO_COMMAND_ARRAY;case XVALUE_OBJECT:return MDO_COMMAND_OBJECT;
    default:return MDO_COMMAND_OTHER;
    }
}
static char* CWPath(cstr Name){return xrtPathJoin(CWRoot,Name);}
static xvalue* CWRead(cstr Name) {
    char* P=CWPath(Name);size_t N=0;char* S=(char*)xrtFileReadAllLimit(P,CW_LIMIT,&N);
    xvalue* V=S&&!memchr(S,0,N)?xrtJsonParse(xrtStrViewN(S,N)):NULL;
    xrtFree(P);xrtFree(S);return V;
}
static bool CWWrite(cstr Name,const xvalue* V) {
    char* S=CWJson(V);char* P=CWPath(Name);
    bool Ok=S&&strlen(S)<=CW_LIMIT&&xrtFileWriteAtomic(P,(xbytesview){(const unsigned char*)S,strlen(S)});
    xrtFree(P);xrtFree(S);return Ok;
}
static mdo_result CWCall(const mdo_command_context_v1* C,cstr Method,const xvalue* Params,xvalue** Result) {
    (void)C;*Result=NULL;if(++CWSequence>128u)return MDO_RESULT_LIMIT;
    char Name[64];xvalue* Q=xrtValueObject();
    bool Ok=Q&&xrtValueObjectSetNew(Q,XRT_STR_LITERAL("method"),xrtValueString(xrtStrView(Method)))&&
        xrtValueObjectSetNew(Q,XRT_STR_LITERAL("params"),Params?xrtValueClone(Params):xrtValueNull());
    snprintf(Name,sizeof(Name),"request-%u.json",CWSequence);Ok=Ok&&CWWrite(Name,Q);xrtValueRelease(Q);
    if(!Ok){snprintf(CWError,sizeof(CWError),"Cannot publish host request");return MDO_RESULT_ERROR;}
    snprintf(Name,sizeof(Name),"response-%u.json",CWSequence);
    xvalue* Reply=NULL;
    /* Parent owns the deadline and kills this complete process group. */
    while(!(Reply=CWRead(Name))) { xrtClearError();xrtSleep(20); }
    bool Success=false;xrtValueGetBool(CWGet(Reply,"ok"),&Success);
    if(Success)*Result=xrtValueClone(CWGet(Reply,"result"));
    else snprintf(CWError,sizeof(CWError),"%s",CWText(Reply,"error"));
    xrtValueRelease(Reply);return Success?MDO_RESULT_OK:MDO_RESULT_ERROR;
}
static void CWCompileError(void* Unused,cstr Message) {
    (void)Unused;size_t N=strlen(CWError);
    if(N<sizeof(CWError)-1)snprintf(CWError+N,sizeof(CWError)-N,"%s%s",N?"\n":"",Message);
}
static bool CWSdkSet(xvalue* V,cstr K,const xvalue* Child){return xrtValueObjectSet(V,xrtStrView(K),Child);}
static xvalue* CWSdkString(cstr S){return xrtValueString(xrtStrView(S?S:""));}
static cstr CWSdkKey(const xvalue* V,size_t I){xstrview K={0};return xrtValueObjectAt(V,I,&K)&&!memchr(K.Data,0,K.Size)?K.Data:NULL;}
static mdo_value_kind CWSdkKind(const xvalue* V){return (mdo_value_kind)CWKind(V);}
static bool CWSdkUInt(const xvalue* V,uint64* N){int64 I=0;return xrtValueGetUInt(V,N)||(xrtValueGetInt(V,&I)&&I>=0&&(*N=(uint64)I,true));}
static uint64 CWSdkClock(void){return xllmClockUs();}
static bool CWSdkCancelled(const mdo_sdk_context_v1* C){return C->Deadline&&xllmDeadlineExpired(C->Deadline);}
static mdo_result CWSdkCall(const mdo_sdk_context_v1* C,cstr M,const xvalue* P,xvalue** R,mdo_error_v1* E){
    (void)C;*R=NULL;if(E){memset(E,0,sizeof(*E));E->Size=sizeof(*E);}
    xvalue* Q=xrtValueObject(),*Frame=NULL;
    bool Ok=Q&&M&&xrtValueObjectSetNew(Q,XRT_STR_LITERAL("method"),CWSdkString(M))&&
        xrtValueObjectSetNew(Q,XRT_STR_LITERAL("params"),P?xrtValueClone(P):xrtValueNull());
    mdo_result Code=Ok?CWCall(NULL,"sdk.invoke",Q,&Frame):MDO_RESULT_ERROR;xrtValueRelease(Q);
    if(Code==MDO_RESULT_OK){int64 N=-1;if(xrtValueGetInt(CWGet(Frame,"code"),&N))Code=(mdo_result)N;
        if(CWGet(Frame,"result"))*R=xrtValueClone(CWGet(Frame,"result"));
        const xvalue* Failure=CWGet(Frame,"error");
        if(E){snprintf(E->Code,sizeof(E->Code),"%s",CWText(Failure,"code"));snprintf(E->Message,sizeof(E->Message),"%s",CWText(Failure,"message"));
            snprintf(E->Stage,sizeof(E->Stage),"%s",CWText(Failure,"stage"));uint64 Status=0;CWSdkUInt(CWGet(Failure,"status"),&Status);E->Status=(uint32)Status;
            snprintf(E->Cause,sizeof(E->Cause),"%s",CWText(Failure,"cause"));
            xrtValueGetBool(CWGet(Failure,"retryable"),&E->Retryable);xrtValueGetBool(CWGet(Failure,"may_have_completed"),&E->MayHaveCompleted);}
        if(Code!=MDO_RESULT_OK)snprintf(CWError,sizeof(CWError),"%s: %s",CWText(Failure,"code"),CWText(Failure,"message"));
    }else if(E){snprintf(E->Code,sizeof(E->Code),"sdk_transport_failed");snprintf(E->Message,sizeof(E->Message),"%s",CWError);E->MayHaveCompleted=true;}
    xrtValueRelease(Frame);return Code;
}
static const mdo_values_v1 CWSdkValues={sizeof(CWSdkValues),xrtValueNull,xrtValueBool,xrtValueInt,xrtValueUInt,xrtValueFloat,
    CWSdkString,xrtValueObject,xrtValueArray,xrtValueClone,CWSdkSet,xrtValueArrayAppend,CWGet,CWAt,CWSdkKey,xrtValueCount,CWSdkKind,
    CWString,xrtValueGetBool,xrtValueGetInt,CWSdkUInt,xrtValueGetFloat,CWParse,CWJson,xrtValueRelease,xrtFree};
static const mdo_application_v1 CWSdkApi={sizeof(CWSdkApi),MDO_SDK_VERSION,&CWSdkValues,CWSdkCall,CWSdkCancelled,CWSdkClock,NULL,NULL};
void ServiceInit(XS_HostInfo* H) {
#if defined(_WIN32) || defined(_WIN64)
    SetErrorMode(0x8003u); /* Worker faults must not wait for an OS crash dialog. */
#endif
    CWRoot=xrtPathParent(H->DevFile);
    xvalue* Input=CWRead("input.json"),*Output=xrtValueObject(),*Result=NULL;
    bool Monitor=false;xrtValueGetBool(CWGet(Input,"watch_parent"),&Monitor);
#if defined(_WIN32) || defined(_WIN64)
    /* The child owns this nested Job handle. Closing it on process death also
     * terminates subprocesses spawned by a C command after the parent died. */
    HANDLE Job=Monitor?CreateJobObjectW(NULL,NULL):NULL;bool JobReady=!Monitor;
    if(Job){JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits={0};Limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        JobReady=SetInformationJobObject(Job,JobObjectExtendedLimitInformation,&Limits,sizeof(Limits))&&AssignProcessToJobObject(Job,GetCurrentProcess());}
#else
    bool JobReady=true;
#endif
    xthread* Watch=Monitor?xrtThreadCreate(CWParentWatch,NULL,0):NULL;
    char* SourcePath=CWPath("command.c");size_t Length=0;
    char* Source=(char*)xrtFileReadAllLimit(SourcePath,128u*1024u,&Length);xrtFree(SourcePath);
    xs_tcc_config Config;xsTccConfigInit(&Config);Config.Flags=0;Config.ApplicationRoot=CWRoot;
    TCCState* Tcc=xsCreateTCCEx(&Config,NULL);mdo_result Code=MDO_RESULT_ERROR;
    if(Tcc){tcc_set_error_func(Tcc,NULL,CWCompileError);tcc_add_include_path(Tcc,CWRoot);}
    union {void* P;mdo_command_run_v1 F;} Entry={0};bool Compiled=false;
    if(Input&&Source&&Tcc&&JobReady&&(!Monitor||Watch)&&tcc_compile_string(Tcc,Source)>=0&&tcc_relocate(Tcc)>=0) {
        Compiled=true;
        Entry.P=tcc_get_symbol(Tcc,MDO_COMMAND_ENTRY);
        if(Entry.F){
            mdo_command_host_v1 Host={sizeof(Host),1,xrtValueCount,CWAt,CWGet,CWString,
                CWParse,CWJson,xrtFree,xrtValueRelease,CWCall,CWKind,xrtValueGetBool,xrtValueGetInt,xrtValueGetUInt,xrtValueGetFloat};
            mdo_command_context_v1 C={0};C.Size=sizeof(C);C.AbiVersion=1;
            C.InvocationId=CWText(Input,"invocation_id");C.CommandId=CWText(Input,"command_id");
            C.ProjectId=CWText(Input,"project_id");C.SessionId=CWText(Input,"session_id");
            C.RunId=CWText(Input,"run_id");C.Workspace=CWText(Input,"workspace");
            C.SourceRevision=CWText(Input,"revision");
            C.RawArguments=CWText(Input,"raw_arguments");
            int64 Deadline=0;if(xrtValueGetInt(CWGet(Input,"deadline"),&Deadline)&&Deadline>0)C.Deadline=(uint64)Deadline;
            C.Host=&Host;C.Error=CWError;C.ErrorCapacity=sizeof(CWError);
            mdo_sdk_context_v1 Application={0};Application.Size=sizeof(Application);Application.Version=MDO_SDK_VERSION;Application.Api=&CWSdkApi;
            Application.ExtensionId=C.CommandId;Application.InvocationId=C.InvocationId;Application.ProjectId=C.ProjectId;
            Application.SessionId=C.SessionId;Application.RunId=C.RunId;Application.Workspace=C.Workspace;Application.DeviceId="local";Application.Deadline=C.Deadline;
            uint64 Origin=0;int64 SignedOrigin=0;
            if(!xrtValueGetUInt(CWGet(Input,"call_origin"),&Origin)&&xrtValueGetInt(CWGet(Input,"call_origin"),&SignedOrigin)&&SignedOrigin>=0)Origin=(uint64)SignedOrigin;
            Application.Origin=Origin<=MDO_ORIGIN_EXTENSION?(uint32)Origin:MDO_ORIGIN_UNKNOWN;
            C.Application=&Application;
            Code=Entry.F(&C,CWGet(Input,"arguments"),&Result);
        }else snprintf(CWError,sizeof(CWError),"Missing mdoCommandRun entry point");
    }
    if(Code!=MDO_RESULT_OK&&!CWError[0])snprintf(CWError,sizeof(CWError),"%s (%d)",Compiled?"Command returned an error":"Command worker setup or compilation failed",Code);
    if(Result){
        char* Encoded=CWJson(Result);
        if(!Encoded||strlen(Encoded)>65536u){Code=MDO_RESULT_LIMIT;snprintf(CWError,sizeof(CWError),"Command result exceeds 64 KiB");xrtValueRelease(Result);Result=NULL;}
        xrtFree(Encoded);
    }
    xrtValueObjectSetNew(Output,XRT_STR_LITERAL("ok"),xrtValueBool(Code==MDO_RESULT_OK));
    xrtValueObjectSetNew(Output,XRT_STR_LITERAL("code"),xrtValueInt(Code));
    const char* ErrorCode=!Compiled?"command_compile_failed":!Entry.F?"command_entry_missing":Code==MDO_RESULT_CANCELLED?"command_cancelled":
        Code==MDO_RESULT_TIMEOUT?"command_timeout":Code==MDO_RESULT_LIMIT?"command_limit":Code==MDO_RESULT_OK?"":"command_failed";
    xrtValueObjectSetNew(Output,XRT_STR_LITERAL("error_code"),xrtValueString(xrtStrView(ErrorCode)));
    xrtValueObjectSetNew(Output,XRT_STR_LITERAL("error"),xrtValueString(xrtStrView(CWError)));
    if(Result)xrtValueObjectSet(Output,XRT_STR_LITERAL("result"),Result);
    (void)CWWrite("final.json",Output);
    xrtAtomic32Store(&CWDone,1,XMEMORY_RELEASE);
    if(Watch){xrtThreadWait(Watch);xrtThreadDestroy(Watch);}
    xrtValueRelease(Result);xrtValueRelease(Output);xrtValueRelease(Input);xrtFree(Source);
    if(Tcc)tcc_delete(Tcc);
    xrtFree(CWRoot);CWRoot=NULL;
    xsAppRequestStop();
}
void ServiceUnit(XS_HostInfo* H){(void)H;}
