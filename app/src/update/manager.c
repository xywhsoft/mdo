#include "../../include/mdo/distribution.h"
#include <stdio.h>
#include <string.h>
#include "../../include/mdo/update.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/runs.h"
#include "../../include/mdo/schedules.h"
#include "../../include/mdo/bootstrap.h"

#ifndef MDO_UPDATE_ORIGIN
#define MDO_UPDATE_ORIGIN "https://ai.xywhsoft.com"
#endif
#define MDO_UPDATE_LIMIT (512u*1024u*1024u)
#ifndef MDO_UPDATE_CHECK_INTERVAL
#define MDO_UPDATE_CHECK_INTERVAL UINT64_C(600000000)
#endif
static struct {
    xnetengine* Engine;
    xmutex* Lock;
    xthread* Thread;
    xcancel* Cancel;
    str Source;
    unsigned Command;
    xdeadline NextCheck;
    MdoUpdateStatus Status;
} g_MdoUpdate;

static bool MdoUpdateText(xvalue* Root,cstr Key,char* Out,size_t Capacity)
{
    xstrview Text;
    if (!xrtValueGetString(xrtValueObjectGet(Root,xrtStrView(Key)),&Text) ||
        Text.Size >= Capacity || memchr(Text.Data,0,Text.Size) ||
        !xrtUtf8Valid(Text,NULL)) return false;
    memcpy(Out,Text.Data,Text.Size); Out[Text.Size] = 0; return true;
}
static bool MdoUpdateHashValid(cstr Hash)
{
    size_t i;
    if (strlen(Hash) != 64) return false;
    for (i=0;i<64;++i) if (!((Hash[i]>='0' && Hash[i]<='9') ||
        (Hash[i]>='a' && Hash[i]<='f'))) return false;
    return true;
}
static void MdoUpdateHex(const uint8 Digest[32],char Hash[65])
{
    size_t i; for (i=0;i<32;++i) snprintf(Hash+i*2,3,"%02x",(unsigned)Digest[i]);
}
static bool MdoUpdateFileHash(cstr Path,char Hash[65],bool Packed)
{
    xfile File = xrtOpen(Path,XFILE_READ); uint64 Size; size_t Got;
    uint8 Buffer[65536], Digest[32]; xsha256 Sha; bool Ok = false;
    if (!File) return false;
    if (!xrtFileSize(File,&Size) || !Size || Size > MDO_UPDATE_LIMIT) goto done;
    if (Packed) {
        if (Size < 32 || !xrtSeek(File,-32,XSEEK_END,NULL) ||
            !xrtRead(File,Buffer,32,&Got) || Got != 32 || memcmp(Buffer,"XRTPEND\0",8) ||
            !xrtSeek(File,0,XSEEK_START,NULL)) goto done;
    }
    xrtSha256Init(&Sha);
    uint64 Total = 0;
    while (!xrtCancelRequested(g_MdoUpdate.Cancel) && xrtRead(File,Buffer,sizeof(Buffer),&Got)) {
        if (!Got) { Ok = Total == Size && xrtSha256Final(&Sha,Digest); break; }
        Total += Got;
        if (Total > Size || !xrtSha256Update(&Sha,Buffer,Got)) break;
    }
    if (Ok) MdoUpdateHex(Digest,Hash);
done:
    xrtClose(File); return Ok;
}
static cstr MdoUpdateDownloadRelative(void)
{
#if defined(__linux__) && !defined(__ANDROID__)
    return "data/update/new.bin";
#elif defined(__ANDROID__)
    return "data/update/new.apk";
#else
    return "data/update/new.exe";
#endif
}
static bool MdoUpdateFetch(cstr Url,size_t Limit,XS_FetchResponse* Response)
{
    XS_FetchRequest Request = {0};
    Request.Size = sizeof(Request); Request.Version = XS_FETCH_REQUEST_VERSION;
    Request.Url = Url; Request.MaxBodyBytes = Limit; Request.Timeout = UINT64_C(120000000);
    Request.IdleTimeout = UINT64_C(15000000); Request.Cancel = g_MdoUpdate.Cancel;
    /* No redirects or transparent decompression: exact authenticated bytes. */
    memset(Response,0,sizeof(*Response)); Response->Size = sizeof(*Response);
    return xsFetch(&Request,Response);
}
static void MdoUpdateComplete(MdoUpdateStatus* Status,cstr State,cstr Message)
{
    snprintf(Status->State,sizeof(Status->State),"%s",State);
    snprintf(Status->Message,sizeof(Status->Message),"%s",Message ? Message : "");
    Status->Busy = false; Status->Installing = false;
}
#include "policy.inc.c"
static void MdoUpdateDoCheck(MdoUpdateStatus* Status)
{
    char Url[160]; MdoUpdateStatus Next;
    XS_FetchResponse Response; xvalue* Root = NULL; xjsonreadconfig Config;
    bool Ok;
    if (!Status->LocalHash[0] &&
        !MdoUpdateFileHash(g_MdoUpdate.Source,Status->LocalHash,!strcmp(Status->Platform,"windows-x86_64"))) {
        MdoUpdateComplete(Status,"error","Cannot hash the running package"); return;
    }
    Next = *Status;
    snprintf(Url,sizeof(Url),MDO_UPDATE_ORIGIN "/update/version?platform=%s&edition=%s&build_id=%llu",Status->Platform,Status->Edition,(unsigned long long)MdoBuildId());
    Ok = MdoUpdateFetch(Url,8192,&Response);
    if (Ok && Response.Status == 404) {
        /* Absence is not an authenticated policy revocation. Keep a known
         * mandatory publication until a valid manifest says otherwise. */
        if (Status->Required && Status->Available) {
            MdoUpdateComplete(Status,"error","Required update is temporarily unavailable; retry checking");
            goto done;
        }
        Status->Available = false; Status->Ready = false;
        MdoUpdateComplete(Status,"no-package","No update has been published");
        goto done;
    }
    if (!Ok || Response.Status != 200) {
        MdoUpdateComplete(Status,"error","Update service is unavailable; retry checking"); goto done;
    }
    xrtJsonReadConfigInit(&Config); Config.MaxInputBytes = 8192;
    Config.MaxDepth = 4; Config.MaxValues = 64;
    Root = xrtJsonRead(xrtStrViewN((cstr)Response.Body,Response.BodySize),&Config);
    Ok = MdoUpdatePolicyParse(Root,&Next) && !strcmp(Next.Edition,Status->Edition);
    if (!Ok) { MdoUpdateComplete(Status,"error","Invalid update metadata"); goto done; }
    Next.Available = strcmp(Status->LocalHash,Next.Hash) != 0 && (!Next.BuildId || Next.BuildId>MdoBuildId());
    Next.Ready = Next.Available && Status->Ready && !strcmp(Status->Hash,Next.Hash);
    Ok = MdoUpdatePolicySave(&Next);
    *Status = Next;
    MdoUpdateComplete(Status,Status->Available ? (Status->Ready ? "ready" : "available") : "current",
        Ok ? "" : "Cannot save update policy; check Home write permissions");
done:
    xrtValueRelease(Root); xsFetchResponseUnit(&Response);
}
static void MdoUpdateDoDownload(MdoUpdateStatus* Status)
{
    char Path[160], Hash[65];
    snprintf(Path,sizeof(Path),"%s",Status->DownloadPath);
    if(!Path[0])snprintf(Path,sizeof(Path),"/update/download/%s/%s",Status->Platform,Status->Hash);
    cstr Relative=MdoUpdateDownloadRelative();
    bool Ok=MdoTransferDownload(g_MdoUpdate.Engine,Path,Relative,Status->Bytes,Status->Hash,g_MdoUpdate.Cancel);
    str Native=Ok?MdoHomeExternalPath(Relative):NULL;
    if(Ok)Ok=Native&&MdoUpdateFileHash(Native,Hash,!strcmp(Status->Platform,"windows-x86_64"))&&!strcmp(Hash,Status->Hash);
    xrtFree(Native); if(!Ok)MdoHomeRemove(Relative,false);
    Status->Ready=Ok;
    MdoUpdateComplete(Status,Ok?"ready":"available",Ok?"":"Download failed or checksum mismatch");
}

static bool MdoUpdateIdle(void)
{
    MdoRunManagerStatus Runs = {0}; MdoScheduleExecutorSnapshot Schedules = {0};
    Runs.Size = sizeof(Runs); Schedules.Size = sizeof(Schedules);
    if (!MdoRunManagerGetStatus(&Runs) || !MdoScheduleExecutorGetSnapshot(&Schedules) ||
        Runs.ActiveRuns || Runs.StartingRuns || Schedules.ActiveRuns) return false;
    xwork_error Error; xwork_task_snapshot* Tasks =
        xworkRuntimeTaskSnapshot(MdoBootstrapRuntime(),0,&Error);
    bool Idle = Tasks != NULL; size_t i;
    for (i=0;Idle && i<xworkTaskSnapshotCount(Tasks);++i) {
        xwork_task_info Info; xworkTaskInfoInit(&Info);
        Idle = xworkTaskSnapshotTaskAt(Tasks,i,&Info) &&
            Info.eState != XWORK_TASK_RUNNING && Info.eState != XWORK_TASK_PENDING;
    }
    xworkTaskSnapshotRelease(Tasks); return Idle;
}
/* Request cooperative stops; run managers retain partial results and leases
 * until workers finish. Never wait for workers while holding update.Lock. */
static void MdoUpdateStopTasks(void)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime(); xwork_error Error;
    xwork_task_snapshot* Tasks = Runtime ? xworkRuntimeTaskSnapshot(Runtime,0,&Error) : NULL;
    size_t i;
    for (i=0;Tasks && i<xworkTaskSnapshotCount(Tasks);++i) {
        xwork_task_info Info; xworkTaskInfoInit(&Info);
        if (!xworkTaskSnapshotTaskAt(Tasks,i,&Info) ||
            (Info.eState != XWORK_TASK_RUNNING && Info.eState != XWORK_TASK_PENDING)) continue;
        /* Keep future scheduled occurrences; only their active execution is
         * cancelled. Shell/Agent pending work must not cross the boundary. */
        if (Info.eKind == XWORK_TASK_SCHEDULED && Info.eState == XWORK_TASK_PENDING) continue;
        bool Handled = false;
        if (MdoScheduleExecutorCancelTask(Info.uTaskId,&Handled,&Error) && !Handled)
            (void)xworkRuntimeCancelTask(Runtime,Info.uTaskId,&Error);
    }
    xworkTaskSnapshotRelease(Tasks);
}
static void MdoUpdateDoInstall(MdoUpdateStatus* Status)
{
    bool Ok = false;
    if (!MdoUpdateIdle()) {
        MdoUpdateComplete(Status,"ready","Finish active tasks before installing the update"); return;
    }
    if (!strcmp(Status->Platform,"windows-x86_64")) {
#if !defined(MDO_SERVER_BUILD)
        if (!xsAppConfirm("更新包已校验。墨斗将关闭、替换程序并重新打开，配置和会话会保留。\n\n是否继续？")) {
            MdoUpdateComplete(Status,"ready","Installation cancelled or native window unavailable"); return;
        }
#endif
        Ok = MdoUpdateWindowsInstall(g_MdoUpdate.Source,Status->Hash);
    } else if (!strncmp(Status->Platform,"linux-",6)) {
        Ok = MdoUpdateLinuxInstall(g_MdoUpdate.Source,Status->Hash);
    } else {
        str Path = MdoHomeExternalPath("data/update/new.apk");
        Ok = Path && xsAppInstallPackage(Path,Status->Hash); xrtFree(Path);
        while (Ok && xsAppPackageInstallPending() && !xrtThreadStopping() &&
            !xrtCancelRequested(g_MdoUpdate.Cancel)) xrtSleep(200);
    }
    MdoUpdateComplete(Status,"ready",Ok ? "Installation requested in the native window" :
        "Cannot start installer; keep the downloaded package and retry");
}
static int32 MdoUpdateThread(ptr Data)
{
    (void)Data; xdeadline NextStop = xrtDeadlineAfter(0);
    while (!xrtThreadStopping()) {
        MdoUpdateStatus Status; unsigned Command; xdeadline NextCheck;
        xrtMutexLock(g_MdoUpdate.Lock);
        Command = g_MdoUpdate.Command; g_MdoUpdate.Command = 0;
        Status = g_MdoUpdate.Status;
        NextCheck = g_MdoUpdate.NextCheck;
        xrtMutexUnlock(g_MdoUpdate.Lock);
        if (!Command) {
            if (xrtDeadlineExpired(NextCheck)) (void)MdoUpdateCheck();
            if (Status.Required && Status.Available && xrtDeadlineExpired(NextStop)) {
                MdoUpdateStopTasks(); NextStop = xrtDeadlineAfter(UINT64_C(1000000));
            }
            xrtSleep(100); continue;
        }
        if (Command == 1) MdoUpdateDoCheck(&Status);
        else if (Command == 2) MdoUpdateDoDownload(&Status);
        else if (Command == 3) MdoUpdateDoInstall(&Status);
        else {
            bool Ok = MdoUpdateIdle() && (!strncmp(Status.Platform,"linux-",6) || xsAppConfirm("退出墨斗？当前配置和会话会保留。"));
            MdoUpdateComplete(&Status,Status.Ready ? "ready" : "available",Ok ? "" :
                "Close the native application window to exit");
            if (Ok) xsAppRequestStop();
        }
        if (xrtCancelRequested(g_MdoUpdate.Cancel))
            MdoUpdateComplete(&Status,Status.Ready ? "ready" : "error","Update operation cancelled");
        xrtMutexLock(g_MdoUpdate.Lock); g_MdoUpdate.Status = Status;
        g_MdoUpdate.NextCheck = xrtDeadlineAfter(MDO_UPDATE_CHECK_INTERVAL);
        xrtMutexUnlock(g_MdoUpdate.Lock);
        if (Status.Required && Status.Available) MdoUpdateStopTasks();
    }
    return 0;
}
static bool MdoUpdateRequest(unsigned Command)
{
    bool Ok = false;
    if (!g_MdoUpdate.Lock || !g_MdoUpdate.Thread) return false;
    xrtMutexLock(g_MdoUpdate.Lock);
    MdoUpdateStatus* Status = &g_MdoUpdate.Status;
    if (Status->Enabled && !Status->Busy &&
        (Command == 1 || (Command == 2 && Status->Available && !Status->Ready) ||
         (Command == 3 && Status->Ready) || (Command == 4 && Status->Required && Status->Available))) {
        xcancel* Cancel = xrtCancelCreate();
        if (Cancel) {
            xrtCancelDestroy(g_MdoUpdate.Cancel); g_MdoUpdate.Cancel = Cancel;
            Status->Busy = true; Status->Installing = Command == 3;
            snprintf(Status->State,sizeof(Status->State),"%s",Command == 1 ? "checking" :
                Command == 2 ? "downloading" : "installing");
            Status->Message[0] = 0; g_MdoUpdate.Command = Command; Ok = true;
        }
    }
    xrtMutexUnlock(g_MdoUpdate.Lock); return Ok;
}
bool MdoUpdateCheck(void) { return MdoUpdateRequest(1); }
bool MdoUpdateCheckEdition(cstr Edition)
{
    if(!g_MdoUpdate.Lock||!Edition||strcmp(MdoToolPlatform(),"android-arm64-v8a")||(strcmp(Edition,"lite")&&strcmp(Edition,"full")))return false;
    xrtMutexLock(g_MdoUpdate.Lock);bool Ok=!g_MdoUpdate.Status.Busy&&!(g_MdoUpdate.Status.Required&&g_MdoUpdate.Status.Available);
    if(Ok)snprintf(g_MdoUpdate.Status.Edition,sizeof(g_MdoUpdate.Status.Edition),"%s",Edition);
    xrtMutexUnlock(g_MdoUpdate.Lock);return Ok&&MdoUpdateRequest(1);
}
bool MdoUpdateDownload(void) { return MdoUpdateRequest(2); }
bool MdoUpdateInstall(void) { return MdoUpdateRequest(3); }
bool MdoUpdateExit(void) { return MdoUpdateRequest(4); }
void MdoUpdateCancel(void)
{
    if (!g_MdoUpdate.Lock) return;
    xrtMutexLock(g_MdoUpdate.Lock);
    if (!g_MdoUpdate.Status.Installing) xrtCancelRequest(g_MdoUpdate.Cancel);
    xrtMutexUnlock(g_MdoUpdate.Lock);
}
bool MdoUpdateGetStatus(MdoUpdateStatus* Status)
{
    if (!Status || !g_MdoUpdate.Lock) return false;
    xrtMutexLock(g_MdoUpdate.Lock); *Status = g_MdoUpdate.Status; xrtMutexUnlock(g_MdoUpdate.Lock); return true;
}
bool MdoUpdateInstalling(void)
{
    MdoUpdateStatus Status; return MdoUpdateGetStatus(&Status) && Status.Installing;
}
bool MdoUpdateBlocked(void)
{
    MdoUpdateStatus Status;
    return MdoUpdateGetStatus(&Status) && Status.Enabled && Status.Required && Status.Available;
}
void MdoUpdateSetEngine(xnetengine* Engine) {g_MdoUpdate.Engine=Engine;}
bool MdoUpdateInit(void)
{
    if (g_MdoUpdate.Lock) return true;
    g_MdoUpdate.Lock = xrtMutexCreate();
    if (!g_MdoUpdate.Lock) return false;
    snprintf(g_MdoUpdate.Status.State,sizeof(g_MdoUpdate.Status.State),"disabled");
    snprintf(g_MdoUpdate.Status.Edition,sizeof(g_MdoUpdate.Status.Edition),"%s",MdoEdition());
    xfile ResultFile = MdoHomeOpenRead("data/update/install-result.json");
    if (ResultFile) {
        char Text[2048]; size_t Bytes = 0; uint64 Length = 0;
        if (xrtFileSize(ResultFile,&Length) && Length < sizeof(Text) &&
            xrtRead(ResultFile,Text,(size_t)Length,&Bytes) && Bytes == Length) {
            xjsonreadconfig Config; xrtJsonReadConfigInit(&Config); Config.MaxInputBytes = sizeof(Text);
            xvalue* Result = xrtJsonRead(xrtStrViewN(Text,Bytes),&Config); char State[16];
            if (MdoUpdateText(Result,"status",State,sizeof(State)) && !strcmp(State,"failed"))
                MdoUpdateText(Result,"message",g_MdoUpdate.Status.LastInstallMessage,sizeof(g_MdoUpdate.Status.LastInstallMessage));
            xrtValueRelease(Result);
        }
        xrtClose(ResultFile);
    }
#if defined(__ANDROID__)
    cstr Source = xsAppPackagePath();
    if (Source) {
        g_MdoUpdate.Source = xrtStrDup(Source);
        snprintf(g_MdoUpdate.Status.Platform,sizeof(g_MdoUpdate.Status.Platform),"android-arm64-v8a");
    }
#elif defined(_WIN32) || defined(_WIN64)
    g_MdoUpdate.Source = xrtPathExecutable();
    xfile File = g_MdoUpdate.Source ? xrtOpen(g_MdoUpdate.Source,XFILE_READ) : NULL;
    char Tail[8]; size_t Got = 0;
    bool Packed = File && xrtSeek(File,-32,XSEEK_END,NULL) && xrtRead(File,Tail,8,&Got) &&
        Got == 8 && !memcmp(Tail,"XRTPEND\0",8);
    xrtClose(File);
    if (!Packed) { xrtFree(g_MdoUpdate.Source); g_MdoUpdate.Source = NULL; }
    else snprintf(g_MdoUpdate.Status.Platform,sizeof(g_MdoUpdate.Status.Platform),"windows-x86_64");
#elif defined(__linux__) && defined(MDO_PRODUCT_LIBC)
    g_MdoUpdate.Source = xrtPathExecutable();
    xfile File = g_MdoUpdate.Source ? xrtOpen(g_MdoUpdate.Source,XFILE_READ) : NULL;
    char Tail[8]; size_t Got = 0;
    bool Packed = File && xrtSeek(File,-32,XSEEK_END,NULL) && xrtRead(File,Tail,8,&Got) &&
        Got == 8 && !memcmp(Tail,"XRTPEND\0",8);
    xrtClose(File);
    if (!Packed) { xrtFree(g_MdoUpdate.Source); g_MdoUpdate.Source = NULL; }
    else snprintf(g_MdoUpdate.Status.Platform,sizeof(g_MdoUpdate.Status.Platform),"%s-%s",MdoToolPlatform(),MDO_PRODUCT_LIBC);
#endif
    if (!g_MdoUpdate.Source) return true;
    g_MdoUpdate.Status.Enabled = true;
    /* Resolve a previously accepted mandatory policy before executors start.
     * Ordinary first launches remain read only and may work offline. */
    MdoUpdatePolicyLoad(&g_MdoUpdate.Status);
    g_MdoUpdate.Thread = xrtThreadCreate(MdoUpdateThread,NULL,0);
    if (!g_MdoUpdate.Thread) {
        /* Keep cached policy in force even if this worker cannot be created. */
        MdoUpdateComplete(&g_MdoUpdate.Status,"error","Cannot start update worker; restart the application");
        return true;
    }
    return MdoUpdateCheck();
}
void MdoUpdateUnit(void)
{
    if (g_MdoUpdate.Thread) {
        xrtMutexLock(g_MdoUpdate.Lock); xrtCancelRequest(g_MdoUpdate.Cancel); xrtMutexUnlock(g_MdoUpdate.Lock);
        xrtThreadStop(g_MdoUpdate.Thread); xrtThreadWait(g_MdoUpdate.Thread); xrtThreadDestroy(g_MdoUpdate.Thread);
    }
    xrtCancelDestroy(g_MdoUpdate.Cancel); xrtMutexDestroy(g_MdoUpdate.Lock); xrtFree(g_MdoUpdate.Source);
    memset(&g_MdoUpdate,0,sizeof(g_MdoUpdate));
}
