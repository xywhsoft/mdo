#include <stdio.h>
#include <string.h>
#include "../../include/mdo/update.h"
#include "../../include/mdo/home.h"

#if defined(_WIN32) || defined(_WIN64)
__declspec(dllimport) unsigned long __stdcall GetCurrentProcessId(void);
__declspec(dllimport) int __stdcall GetProcessTimes(void*,void*,void*,void*,void*);
typedef struct { uint32 Low, High; } MdoUpdateFileTime;
static bool MdoUpdateWinPut(xvalue* Value,cstr Key,cstr Text)
{ return xrtValueObjectSetNew(Value,xrtStrView(Key),xrtValueString(xrtStrView(Text))); }
bool MdoUpdateWindowsInstall(cstr Source,cstr Hash)
{
    size_t ScriptBytes = 0, JsonBytes = 0, i; bool Ok = false;
    bytes Script = xsAppReadAll("update/install.ps1",&ScriptBytes);
    str ScriptPath = NULL, ParamsPath = NULL, Folder = NULL, Program = NULL, Json = NULL;
    xvalue* Params = xrtValueObject(); xvalue* Args = xrtValueArray(); xprocess* Process = NULL;
    MdoHomeSnapshot Home = {0}; Home.Size = sizeof(Home);
    MdoUpdateFileTime Created, End, Kernel, User; char Identity[32], Pid[24];
    if (!Script || !Params || !Args || !MdoHomeGetSnapshot(&Home) ||
        !GetProcessTimes((void*)(intptr_t)-1,&Created,&End,&Kernel,&User)) goto done;
    snprintf(Identity,sizeof(Identity),"%llu",(unsigned long long)(((uint64)Created.High<<32)|Created.Low));
    snprintf(Pid,sizeof(Pid),"%lu",GetCurrentProcessId());
    Ok = MdoUpdateWinPut(Params,"target",Source) && MdoUpdateWinPut(Params,"sha256",Hash) &&
        MdoUpdateWinPut(Params,"pid",Pid) && MdoUpdateWinPut(Params,"identity",Identity) &&
        MdoUpdateWinPut(Params,"home",Home.Path) && MdoUpdateWinPut(Params,"work_dir",xsAppPath());
    Ok = Ok && xrtValueArrayAppendNew(Args,xrtValueString(XRT_STR_LITERAL("--"))) &&
        xrtValueArrayAppendNew(Args,xrtValueString(XRT_STR_LITERAL("--home"))) &&
        xrtValueArrayAppendNew(Args,xrtValueString(xrtStrView(Home.Path)));
    for (i=0;Ok && i<xsAppArgumentCount();++i) {
        cstr Arg = xsAppArgument((uint32)i);
        if (!strcmp(Arg,"--home")) { ++i; continue; }
        if (!strncmp(Arg,"--home=",7)) continue;
        Ok = xrtValueArrayAppendNew(Args,xrtValueString(xrtStrView(Arg)));
    }
    if (!Ok || !xrtValueObjectSetNew(Params,XRT_STR_LITERAL("args"),Args)) goto done;
    Args = NULL; Json = xrtJsonStringify(Params,false,&JsonBytes);
    Ok = Json && MdoHomeRemove("data/update/install.ready",false) &&
        MdoHomeRemove("data/update/install.go",false) &&
        MdoHomeAtomicWrite("data/update/install.ps1",Script,ScriptBytes,false) &&
        MdoHomeAtomicWrite("data/update/install.json",Json,JsonBytes,false);
    if (!Ok) goto done;
    ScriptPath = MdoHomeExternalPath("data/update/install.ps1");
    ParamsPath = MdoHomeExternalPath("data/update/install.json");
    Folder = MdoHomeExternalPath("data/update");
    str Windows = xrtEnvGet("SystemRoot");
    Program = Windows ? xrtPathJoin(Windows,"System32/WindowsPowerShell/v1.0/powershell.exe") : NULL;
    xrtFree(Windows);
    if (!ScriptPath || !ParamsPath || !Folder || !Program) { Ok = false; goto done; }
    const cstr Arguments[] = {"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass",
        "-File",ScriptPath,"-Parameters",ParamsPath};
    xprocessconfig Config; xrtProcessConfigInit(&Config);
    Config.Program = Program; Config.Args = Arguments; Config.ArgCount = sizeof(Arguments)/sizeof(Arguments[0]);
    Config.WorkDir = Folder; Config.HideWindow = true;
    Config.NewGroup = false; /* helper must survive the old host's exit */
    Config.Stdin.Mode = Config.Stdout.Mode = Config.Stderr.Mode = XPROCESS_IO_NULL;
    Process = xrtProcessSpawn(&Config); Ok = Process != NULL;
    xdeadline Deadline = xrtDeadlineAfter(UINT64_C(10000000));
    bool Ready = false;
    while (Ok && !xrtDeadlineExpired(Deadline) && !xrtThreadStopping()) {
        bool Exists = false; xfileinfo Info;
        if (MdoHomeExternalStat("data/update/install.ready",&Exists,&Info) && Exists &&
            Info.Type == XFILE_TYPE_FILE) { Ready = true; break; }
        if (xrtProcessState(Process) != XPROCESS_RUNNING) break;
        xrtSleep(100);
    }
    Ok = Ready && MdoHomeAtomicWrite("data/update/install.go",Hash,strlen(Hash),false);
    if (Ok) xsAppRequestStop(); /* helper is ready before graceful shutdown */
    else if (Process) xrtProcessTerminate(Process); /* only our own helper */
done:
    xrtProcessDestroy(Process); xrtFree(Script); xrtFree(Json);
    xrtFree(ScriptPath); xrtFree(ParamsPath); xrtFree(Folder); xrtFree(Program);
    xrtValueRelease(Params); xrtValueRelease(Args); return Ok;
}
#else
bool MdoUpdateWindowsInstall(cstr Source,cstr Hash) { (void)Source; (void)Hash; return false; }
#endif
