/* Windows-only portable command entry. Built with the locked, trimmed XRT.
 * The registry is read once. No shell, probe, installer or resident service. */
#define XRT_MODULE_JSON_READ
#define XRT_MODULE_FILE_WHOLE
#define XRT_MODULE_PATH_SYSTEM
#define XRT_MODULE_PROCESS
#define XRT_MODULE_ENVIRONMENT
#define XRT_IMPLEMENTATION
#include "xrt.h"
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#define FORWARD_CONFIG_LIMIT (1024u * 1024u)
#define FORWARD_ARG_LIMIT 1024u
static volatile LONG ForwardControl;

static BOOL WINAPI ForwardConsole(DWORD Event)
{
    if(Event==CTRL_C_EVENT||Event==CTRL_BREAK_EVENT){InterlockedExchange(&ForwardControl,1);return TRUE;}
    return FALSE;
}
static void ForwardError(cstr Message,cstr Detail)
{
    char Text[2048];int N=snprintf(Text,sizeof(Text),"mdo: %s%s%s\n",Message,Detail&&Detail[0]?": ":"",Detail?Detail:"");
    if(N<0)return;if((size_t)N>=sizeof(Text))N=sizeof(Text)-1;
    HANDLE Error=GetStdHandle(STD_ERROR_HANDLE);DWORD Mode,Done;
    if(GetConsoleMode(Error,&Mode)){
        wchar_t Wide[2048];int Count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,Text,N,Wide,2048);
        if(Count>0)WriteConsoleW(Error,Wide,(DWORD)Count,&Done,NULL);
    }else WriteFile(Error,Text,(DWORD)N,&Done,NULL);
}
static cstr ForwardText(const xvalue* Object,cstr Key,size_t Limit)
{
    xstrview V={0};return xrtValueGetString(xrtValueObjectGet(Object,xrtStrView(Key)),&V)&&V.Size<=Limit&&!memchr(V.Data,0,V.Size)?V.Data:NULL;
}
static str ForwardUtf8(const wchar_t* Text)
{
    int N=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,Text,-1,NULL,0,NULL,NULL);
    str Out=N>0?xrtMalloc((size_t)N):NULL;
    if(Out&&!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,Text,-1,Out,N,NULL,NULL)){xrtFree(Out);Out=NULL;}return Out;
}
static bool ForwardName(cstr Name)
{
    size_t N=Name?strlen(Name):0;if(!N||N>64||Name[0]=='.')return false;
    for(size_t I=0;I<N;I++)if(!((Name[I]>='a'&&Name[I]<='z')||(Name[I]>='0'&&Name[I]<='9')||strchr("_.+-",Name[I])))return false;
    return Name[N-1]!='.';
}
static bool ForwardInside(cstr Root,cstr Target)
{
    size_t N=strlen(Root);return !_strnicmp(Root,Target,N)&&(Root[N-1]=='\\'||Root[N-1]=='/'||Target[N]=='\\'||Target[N]=='/');
}
/* Keep this non-inheritable handle until process exit. Windows closes it when
 * this launcher dies (including TerminateProcess), killing all descendants.
 * Closing it explicitly while this process belongs to the Job kills us too. */
static bool ForwardContain(void)
{
    HANDLE Job=CreateJobObjectW(NULL,NULL);JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits={0};
    if(!Job)return false;Limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(Job,JobObjectExtendedLimitInformation,&Limits,sizeof(Limits))||!AssignProcessToJobObject(Job,GetCurrentProcess())){CloseHandle(Job);return false;}
    return true;
}
int wmain(int argc,wchar_t** argv)
{
    int Result=126;str Self=NULL,Bin=NULL,RegistryRoot=NULL,Data=NULL,Home=NULL,Name=NULL,Config=NULL,Target=NULL,RealHome=NULL,RealTarget=NULL;
    bytes Raw=NULL;xvalue* Registry=NULL;xprocess* Child=NULL;char** Args=NULL;size_t Count=0,PrefixCount=0;
    cstr Failure="Cannot initialize portable command",Detail="";char DepthText[12];
    if(argc<1||(size_t)argc-1>FORWARD_ARG_LIMIT){Failure="Too many command arguments";goto done;}
    Self=xrtPathExecutable();Bin=Self?xrtPathParent(Self):NULL;RegistryRoot=Bin?xrtPathParent(Bin):NULL;
    Data=RegistryRoot?xrtPathParent(RegistryRoot):NULL;Home=Data?xrtPathParent(Data):NULL;Name=Self?xrtPathStem(Self):NULL;
    if(!Home||!Name)goto done;for(char* C=Name;*C;C++)if(*C>='A'&&*C<='Z')*C+='a'-'A';
    if(!ForwardName(Name)){Failure="Invalid command entry name";goto done;}
    Config=xrtPathJoin(RegistryRoot,"state.json");size_t Size=0;Raw=Config?xrtFileReadAllLimit(Config,FORWARD_CONFIG_LIMIT,&Size):NULL;
    Failure="Command registry is missing or invalid";if(!Raw)goto done;
    xjsonreadconfig Json;xrtJsonReadConfigInit(&Json);Json.MaxInputBytes=FORWARD_CONFIG_LIMIT;Json.MaxDepth=24;Json.MaxValues=50000;Json.MaxStringBytes=65536;
    Registry=xrtJsonRead(xrtStrViewN((cstr)Raw,Size),&Json);if(!Registry)goto done;
    const xvalue* Commands=xrtValueObjectGet(Registry,XRT_STR_LITERAL("commands"));
    if(xrtValueType(Commands)!=XVALUE_OBJECT||xrtValueCount(Commands)>1024)goto done;
    const xvalue* Entry=xrtValueObjectGet(Commands,xrtStrView(Name));cstr Relative=ForwardText(Entry,"target",4096);
    Failure="Command is disabled or no longer installed";Detail=Name;if(!Relative)goto done;
    Failure="Command target escapes mdo Home";
    if(!xrtPathIsLocal(xrtStrView(Relative),XPATH_NATIVE)||!strncmp(Relative,"data/cli-tools/bin/",19)||!strncmp(Relative,"data/cli-tools/forwarders/",26))goto done;
    Target=xrtPathJoin(Home,Relative);RealHome=xrtPathReal(Home);RealTarget=Target?xrtPathReal(Target):NULL;
    Failure="Command program is missing or inaccessible";Detail=Relative;if(!RealHome||!RealTarget)goto done;
    Failure="Command target escapes mdo Home";if(!ForwardInside(RealHome,RealTarget)||!_stricmp(Self,RealTarget))goto done;
    const xvalue* Prefix=xrtValueObjectGet(Entry,XRT_STR_LITERAL("args"));
    Failure="Invalid command argument prefix";if(Prefix&&xrtValueType(Prefix)!=XVALUE_ARRAY)goto done;
    PrefixCount=xrtValueCount(Prefix);Count=PrefixCount+(size_t)argc-1;if(PrefixCount>64||Count>FORWARD_ARG_LIMIT)goto done;
    Args=xrtCalloc(Count+1,sizeof(*Args));if(!Args){Failure="Out of memory";goto done;}
    for(size_t I=0;I<PrefixCount;I++){xstrview V={0};if(!xrtValueGetString(xrtValueArrayGet(Prefix,I),&V)||V.Size>32768||memchr(V.Data,0,V.Size))goto done;Args[I]=xrtStrDupN(V.Data,V.Size);if(!Args[I])goto done;}
    for(int I=1;I<argc;I++){Args[PrefixCount+I-1]=ForwardUtf8(argv[I]);if(!Args[PrefixCount+I-1]){Failure="Invalid Unicode argument or out of memory";goto done;}}
    str Prior=xrtEnvGet("MDO_CLI_FORWARD_DEPTH");unsigned Depth=0;bool Valid=!Prior||!Prior[0];
    if(Prior&&Prior[0]){Valid=strlen(Prior)<=2;for(cstr C=Prior;Valid&&*C;C++){Valid=*C>='0'&&*C<='9';if(Valid)Depth=Depth*10+(unsigned)(*C-'0');}}xrtFree(Prior);
    Failure="Recursive command forwarding refused";if(!Valid||Depth>=4)goto done;snprintf(DepthText,sizeof(DepthText),"%u",Depth+1);
    Failure="Cannot contain the command process tree";Detail="";if(!ForwardContain())goto done;
    Failure="Cannot install command interrupt handler";if(!SetConsoleCtrlHandler(ForwardConsole,TRUE))goto done;
    xprocessenv Env={"MDO_CLI_FORWARD_DEPTH",DepthText};xprocessconfig Launch;xrtProcessConfigInit(&Launch);
    Launch.Program=RealTarget;Launch.Args=(const cstr*)Args;Launch.ArgCount=Count;Launch.Env=&Env;Launch.EnvCount=1;Launch.NewGroup=true;
    Failure="Cannot start command program";Detail=Relative;Child=xrtProcessSpawn(&Launch);if(!Child)goto done;
    for(;;){
        if(InterlockedExchange(&ForwardControl,0)){
            if(!xrtProcessInterrupt(Child)){/* Headless callers cancel through their enclosing Job. */xrtClearError();}
        }
        xwaitresult Wait=xrtProcessWaitFor(Child,25);if(Wait==XWAIT_TIMEOUT)continue;
        Failure="Cannot wait for command program";if(Wait!=XWAIT_OK)goto done;
        xprocessstatus Status;if(!xrtProcessStatus(Child,&Status))goto done;Result=Status.Code;Failure=NULL;break;
    }
done:
    if(Failure){ForwardError(Failure,Detail);cstr Native=xrtErrorMessage(xrtGetError());if(Native&&Native[0])ForwardError("Cause",Native);}
    xrtProcessDestroy(Child);if(Args){for(size_t I=0;I<Count;I++)xrtFree(Args[I]);xrtFree(Args);}
    xrtValueRelease(Registry);xrtFree(Raw);xrtFree(Self);xrtFree(Bin);xrtFree(RegistryRoot);xrtFree(Data);xrtFree(Home);xrtFree(Name);xrtFree(Config);xrtFree(Target);xrtFree(RealHome);xrtFree(RealTarget);
    return Result;
}
