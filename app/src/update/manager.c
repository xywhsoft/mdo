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
#define MDO_UPDATE_LIMIT (32u*1024u*1024u)
static struct {
    xmutex* Lock;
    xthread* Thread;
    xcancel* Cancel;
    str Source;
    unsigned Command;
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
static void MdoUpdateDoCheck(MdoUpdateStatus* Status)
{
    char Url[160], Expected[128], Download[128], Platform[32];
    XS_FetchResponse Response; xvalue* Root = NULL; xjsonreadconfig Config;
    bool Ok; uint64 Size = 0; int64 Signed;
    if (!Status->LocalHash[0] &&
        !MdoUpdateFileHash(g_MdoUpdate.Source,Status->LocalHash,!strcmp(Status->Platform,"windows-x86_64"))) {
        MdoUpdateComplete(Status,"error","Cannot hash the running package"); return;
    }
    snprintf(Url,sizeof(Url),MDO_UPDATE_ORIGIN "/update/version?platform=%s",Status->Platform);
    Ok = MdoUpdateFetch(Url,8192,&Response);
    if (Ok && Response.Status == 404) {
        MdoUpdateComplete(Status,"no-package","No update has been published");
        goto done;
    }
    if (!Ok || Response.Status != 200) {
        MdoUpdateComplete(Status,"error","Update service is unavailable; normal use is unaffected"); goto done;
    }
    xrtJsonReadConfigInit(&Config); Config.MaxInputBytes = 8192;
    Config.MaxDepth = 4; Config.MaxValues = 64;
    Root = xrtJsonRead(xrtStrViewN((cstr)Response.Body,Response.BodySize),&Config);
    xvalue* Number = xrtValueObjectGet(Root,XRT_STR_LITERAL("size"));
    Ok = xrtValueGetUInt(Number,&Size) ||
        (xrtValueGetInt(Number,&Signed) && Signed > 0 && (Size=(uint64)Signed)!=0);
    Ok = Ok && Size && Size <= MDO_UPDATE_LIMIT &&
        MdoUpdateText(Root,"platform",Platform,sizeof(Platform)) && !strcmp(Platform,Status->Platform) &&
        MdoUpdateText(Root,"sha256",Status->Hash,sizeof(Status->Hash)) && MdoUpdateHashValid(Status->Hash) &&
        MdoUpdateText(Root,"url",Download,sizeof(Download)) &&
        MdoUpdateText(Root,"notes",Status->Notes,sizeof(Status->Notes));
    snprintf(Expected,sizeof(Expected),"/update/download/%s/%s",Status->Platform,Status->Hash);
    Ok = Ok && !strcmp(Expected,Download);
    if (!Ok) { Status->Hash[0] = 0; MdoUpdateComplete(Status,"error","Invalid update metadata"); goto done; }
    Status->Bytes = Size; Status->Ready = false;
    MdoUpdateComplete(Status,!strcmp(Status->LocalHash,Status->Hash) ? "current" : "available","");
done:
    xrtValueRelease(Root); xsFetchResponseUnit(&Response);
}
static void MdoUpdateDoDownload(MdoUpdateStatus* Status)
{
    char Url[256], Hash[65]; uint8 Digest[32]; XS_FetchResponse Response; bool Ok;
    snprintf(Url,sizeof(Url),MDO_UPDATE_ORIGIN "/update/download/%s/%s",Status->Platform,Status->Hash);
    Ok = MdoUpdateFetch(Url,(size_t)Status->Bytes,&Response);
    Ok = Ok && Response.Status == 200 && Response.BodySize == Status->Bytes &&
        xrtSha256(Response.Body,Response.BodySize,Digest);
    if (Ok) { MdoUpdateHex(Digest,Hash); Ok = !strcmp(Hash,Status->Hash); }
    if (Ok && !strcmp(Status->Platform,"windows-x86_64"))
        Ok = Response.BodySize > 32 && !memcmp(Response.Body,"MZ",2) &&
            !memcmp(Response.Body+Response.BodySize-32,"XRTPEND\0",8);
    if (Ok && !xrtCancelRequested(g_MdoUpdate.Cancel))
        Ok = MdoHomeAtomicWrite(!strcmp(Status->Platform,"windows-x86_64") ?
            "data/update/new.exe" : "data/update/new.apk",Response.Body,Response.BodySize,false);
    else Ok = false;
    if (Ok) {
        str Path = MdoHomeExternalPath(!strcmp(Status->Platform,"windows-x86_64") ?
            "data/update/new.exe" : "data/update/new.apk");
        Ok = Path && MdoUpdateFileHash(Path,Hash,false) && !strcmp(Hash,Status->Hash); xrtFree(Path);
    }
    Status->Ready = Ok;
    MdoUpdateComplete(Status,Ok ? "ready" : "available",Ok ? "" :
        "Download failed or checksum mismatch; the running package was not changed");
    xsFetchResponseUnit(&Response);
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
static void MdoUpdateDoInstall(MdoUpdateStatus* Status)
{
    bool Ok = false;
    if (!MdoUpdateIdle()) {
        MdoUpdateComplete(Status,"ready","Finish active tasks before installing the update"); return;
    }
    if (!strcmp(Status->Platform,"windows-x86_64")) {
        if (!xsAppConfirm("更新包已校验。墨斗将关闭、替换程序并重新打开，配置和会话会保留。\n\n是否继续？")) {
            MdoUpdateComplete(Status,"ready","Installation cancelled or native window unavailable"); return;
        }
        Ok = MdoUpdateWindowsInstall(g_MdoUpdate.Source,Status->Hash);
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
    (void)Data;
    while (!xrtThreadStopping()) {
        MdoUpdateStatus Status; unsigned Command;
        xrtMutexLock(g_MdoUpdate.Lock);
        Command = g_MdoUpdate.Command; g_MdoUpdate.Command = 0;
        Status = g_MdoUpdate.Status;
        xrtMutexUnlock(g_MdoUpdate.Lock);
        if (!Command) { xrtSleep(100); continue; }
        if (Command == 1) MdoUpdateDoCheck(&Status);
        else if (Command == 2) MdoUpdateDoDownload(&Status);
        else MdoUpdateDoInstall(&Status);
        if (xrtCancelRequested(g_MdoUpdate.Cancel))
            MdoUpdateComplete(&Status,Status.Ready ? "ready" : "error","Update operation cancelled");
        xrtMutexLock(g_MdoUpdate.Lock); g_MdoUpdate.Status = Status; xrtMutexUnlock(g_MdoUpdate.Lock);
    }
    return 0;
}
static bool MdoUpdateRequest(unsigned Command)
{
    bool Ok = false;
    if (!g_MdoUpdate.Lock) return false;
    xrtMutexLock(g_MdoUpdate.Lock);
    MdoUpdateStatus* Status = &g_MdoUpdate.Status;
    if (Status->Enabled && !Status->Busy &&
        (Command == 1 || (Command == 2 && !strcmp(Status->State,"available")) ||
         (Command == 3 && Status->Ready))) {
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
bool MdoUpdateDownload(void) { return MdoUpdateRequest(2); }
bool MdoUpdateInstall(void) { return MdoUpdateRequest(3); }
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
bool MdoUpdateInit(void)
{
    if (g_MdoUpdate.Lock) return true;
    g_MdoUpdate.Lock = xrtMutexCreate();
    if (!g_MdoUpdate.Lock) return false;
    snprintf(g_MdoUpdate.Status.State,sizeof(g_MdoUpdate.Status.State),"disabled");
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
#endif
    if (!g_MdoUpdate.Source) return true;
    g_MdoUpdate.Status.Enabled = true;
    g_MdoUpdate.Thread = xrtThreadCreate(MdoUpdateThread,NULL,0);
    if (!g_MdoUpdate.Thread) { MdoUpdateUnit(); return false; }
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
