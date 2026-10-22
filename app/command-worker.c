/* Private child-process bootstrap, never included in the product unity. */
#include <xsbase.h>
#include "mdo/command.h"
#include <stdio.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <process.h>
#else
#include <poll.h>
#include <unistd.h>
#include <signal.h>
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
        struct pollfd P={0,POLLIN,0};int Ready=poll(&P,1,50);
        if(Ready>0&&(P.revents&(POLLHUP|POLLERR|POLLNVAL))){
            /* Only our own process group, never the host's group. */
            (void)kill(-getpid(),SIGKILL);_exit(125);
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
void ServiceInit(XS_HostInfo* H) {
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
    xs_tcc_config Config;xsTccConfigInit(&Config);Config.Flags=XS_TCC_RESTRICT_HOST_SYMBOLS;Config.ApplicationRoot=CWRoot;
    TCCState* Tcc=xsCreateTCCEx(&Config,NULL);mdo_result Code=MDO_RESULT_ERROR;
    if(Tcc){tcc_set_error_func(Tcc,NULL,CWCompileError);tcc_add_include_path(Tcc,CWRoot);}
    union {void* P;mdo_command_run_v1 F;} Entry={0};
    if(Input&&Source&&Tcc&&JobReady&&(!Monitor||Watch)&&tcc_compile_string(Tcc,Source)>=0&&tcc_relocate(Tcc)>=0) {
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
            Code=Entry.F(&C,CWGet(Input,"arguments"),&Result);
        }else snprintf(CWError,sizeof(CWError),"Missing mdoCommandRun entry point");
    }
    if(Code!=MDO_RESULT_OK&&!CWError[0])snprintf(CWError,sizeof(CWError),"Command compilation or execution failed (%d)",Code);
    if(Result){
        char* Encoded=CWJson(Result);
        if(!Encoded||strlen(Encoded)>65536u){Code=MDO_RESULT_LIMIT;snprintf(CWError,sizeof(CWError),"Command result exceeds 64 KiB");xrtValueRelease(Result);Result=NULL;}
        xrtFree(Encoded);
    }
    xrtValueObjectSetNew(Output,XRT_STR_LITERAL("ok"),xrtValueBool(Code==MDO_RESULT_OK));
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
