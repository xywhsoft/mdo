#include "../../include/mdo/time.h"
#include "internal.h"

typedef struct MdoDtCaptureState {
    xvalue* Files;
    cstr Id;
    xcancel* Cancel;
    uint64 Bytes;
    size_t Index;
    xfileinfo Home;
    cstr HomePath;
    bool Check;
    xwork_error* Error;
} MdoDtCaptureState;

static bool MdoDtCaptureError(MdoDtCaptureState* Capture,cstr Message) {
    xworkErrorInit(Capture->Error);
    Capture->Error->eCode=XWORK_ERROR_IO;
    snprintf(Capture->Error->sMessage,sizeof(Capture->Error->sMessage),"%s",Message);
    return false;
}
static bool MdoDtAdd(MdoDtCaptureState* Capture,cstr Path,cstr Type,uint64 Bytes,cstr Hash,uint32 Mode) {
#if defined(_WIN32) || defined(_WIN64)
    /* Windows has ACLs, not POSIX mode bits. Send private, usable defaults
     * for receivers which do implement POSIX permissions. */
    Mode=!strcmp(Type,"directory")?0700u:0600u;
#endif
    if (Capture->Index>=MDO_DT_FILES || !MdoDtPath(Path) || Bytes>UINT64_C(9007199254740991)-Capture->Bytes)
        return MdoDtCaptureError(Capture,"project exceeds the file/path transfer budget");
    if (Capture->Check) {
        const xvalue* Row=xrtValueArrayGet(Capture->Files,Capture->Index++);
        uint64 Size=0;
        cstr ExpectedPath=MdoAccountText(Row,"path",2048u),ExpectedHash=MdoAccountText(Row,"sha256",64u);
        return ExpectedPath && ExpectedHash && !strcmp(ExpectedPath,Path) && !strcmp(ExpectedHash,Hash) &&
            MdoAccountGetUInt(xrtValueObjectGet(Row,XRT_STR_LITERAL("bytes")),&Size) && Size==Bytes;
    }
    xvalue* Row=xrtValueObject();
    bool Ok=Row && MdoAccountSetString(Row,"path",Path) && MdoAccountSetString(Row,"type",Type) &&
        MdoAccountSetString(Row,"sha256",Hash) && MdoAccountSetUInt(Row,"bytes",Bytes) &&
        MdoAccountSetUInt(Row,"mode",Mode&0777u) && xrtValueArrayAppendNew(Capture->Files,Row);
    if (!Ok) xrtValueRelease(Row);
    else { Capture->Index++; Capture->Bytes+=Bytes; }
    return Ok;
}
static bool MdoDtCopy(MdoDtCaptureState* Capture,xroot Root,cstr Relative,cstr Logical,const xfileinfo* Before) {
    xfileoptions Options; xrtFileOptionsInit(&Options); Options.Flags=XFILE_READ|XFILE_NOFOLLOW;
    xfile Input=xrtRootFileOpen(Root,Relative,&Options),Output=NULL;
    uint8 Buffer[65536],Digest[32]; char Hash[65],Path[256],Leaf[32];
    xsha256 Sha; xrtSha256Init(&Sha); uint64 Total=0; bool Ok=false;
    xfileinfo After,Opened;
    if (!Input || !xrtFileStat(Input,&Opened) || Opened.Identity!=Before->Identity || Opened.Device!=Before->Device) goto done;
    if (!Capture->Check) {
        if (!MdoDtDiskSpace(Capture->HomePath,Before->Size)) { MdoDtCaptureError(Capture,"source disk space is insufficient to prepare a fixed project copy"); goto done; }
        snprintf(Leaf,sizeof(Leaf),"files/%08u",(unsigned)Capture->Index);
        MdoDtTaskPath(Path,false,Capture->Id,Leaf);
        Output=MdoHomeOpenWrite(Path,XFILE_WRITE|XFILE_CREATE|XFILE_TRUNCATE);
        if (!Output) goto done;
    }
    while (!xrtCancelRequested(Capture->Cancel)) {
        size_t Read=0,Written=0;
        if (!xrtRead(Input,Buffer,sizeof(Buffer),&Read)) goto done;
        if (!Read) break;
        if (!xrtSha256Update(&Sha,Buffer,Read) || (Output && (!xrtWrite(Output,Buffer,Read,&Written) || Written!=Read))) goto done;
        Total+=Read;
        MdoDtKeepAwake();
    }
    if (xrtCancelRequested(Capture->Cancel) || Total!=Before->Size ||
        !xrtRootStat(Root,Relative,false,&After) || Before->Identity!=After.Identity ||
        Before->Device!=After.Device || Before->Modified!=After.Modified || Before->Changed!=After.Changed ||
        After.Size!=Total || !xrtSha256Final(&Sha,Digest) || (Output && !xrtFlush(Output))) goto done;
    for (size_t i=0;i<32;i++) snprintf(Hash+2*i,3,"%02x",Digest[i]);
    Ok=MdoDtAdd(Capture,Logical,"file",Total,Hash,Before->Mode);
done:
    xrtClose(Input); if (Output && !xrtClose(Output)) Ok=false;
    if (!Ok) MdoDtCaptureError(Capture,"file changed, became inaccessible, or could not be copied; retry after writers are idle");
    return Ok;
}
static bool MdoDtWalk(MdoDtCaptureState* Capture,xroot Root,cstr Relative,cstr Logical,unsigned Depth) {
    xfileinfo Before,After; bool Ok=false; xdir Directory=NULL; xdirentry Entry; xdirnext Next;
    if (Depth>64u || xrtCancelRequested(Capture->Cancel) ||
        !xrtRootStat(Root,Relative,false,&Before)) return false;
    if (Before.Device==Capture->Home.Device && Before.Identity==Capture->Home.Identity)
        return MdoDtCaptureError(Capture,"workspace includes mdo Home; choose a project directory outside the Home root");
    if (Before.Type==XFILE_TYPE_FILE) {
        cstr Name=strrchr(Relative,'/'); Name=Name?Name+1:Relative;
        if (!strcmp(Name,".git"))
            return MdoDtCaptureError(Capture,"Git worktree metadata points outside the project; use an independent repository before transferring");
        return MdoDtCopy(Capture,Root,Relative,Logical,&Before);
    }
    if (Before.Type!=XFILE_TYPE_DIRECTORY)
        return MdoDtCaptureError(Capture,"project contains a symbolic link or special file that cannot be transferred safely");
    if (!MdoDtAdd(Capture,Logical,"directory",0u,"",Before.Mode)) return false;
    Directory=xrtRootDirOpen(Root,Relative,XDIR_STAT);
    if (!Directory) return false;
    while ((Next=xrtDirNext(Directory,&Entry))==XDIR_NEXT_ITEM) {
        char Child[2049],Name[256],Target[2049];
        if (!(Entry.Flags&XDIR_ENTRY_UTF8) || Entry.Name.Size>=sizeof(Name)) goto done;
        memcpy(Name,Entry.Name.Data,Entry.Name.Size); Name[Entry.Name.Size]=0;
        if (!MdoDtPath(Name)) { MdoDtCaptureError(Capture,"project contains a filename incompatible with the target platform"); goto done; }
        int n=snprintf(Child,sizeof(Child),"%s%s%s",!strcmp(Relative,".")?"":Relative,!strcmp(Relative,".")?"":"/",Name);
        int m=snprintf(Target,sizeof(Target),"%s/%s",Logical,Name);
        if (n<=0 || (size_t)n>=sizeof(Child) || m<=0 || (size_t)m>=sizeof(Target) ||
            !MdoDtWalk(Capture,Root,Child,Target,Depth+1u)) goto done;
    }
    Ok=Next==XDIR_NEXT_END && xrtRootStat(Root,Relative,false,&After) &&
        Before.Identity==After.Identity && Before.Device==After.Device && Before.Modified==After.Modified;
done:
    if (!xrtDirClose(Directory)) Ok=false;
    return Ok;
}
static bool MdoDtCaptureSession(MdoDtCaptureState* Capture,cstr Project,cstr Session,MdoProjectLease* Owner) {
    MdoApiSessionCaptureGuard Guard; MdoSessionBackup* Backup=NULL;
    size_t Bytes=0; str Json=NULL; char Logical[96],Leaf[40],Path[256],Hash[65]; bool Ok=false;
    if (!MdoApiSessionCaptureAcquire(&Guard)) {
        xworkErrorInit(Capture->Error); Capture->Error->eCode=XWORK_ERROR_CONTEXT;
        snprintf(Capture->Error->sMessage,sizeof(Capture->Error->sMessage),"waiting for session drafts, queue or attachments to finish saving");
        return false;
    }
    Backup=MdoSessionBackupCaptureStored(Project,Session,Owner,NULL,Capture->Error);
    MdoApiSessionCaptureRelease(&Guard);
    if (Backup) Json=MdoSessionBackupEncode(Backup,NULL,&Bytes,Capture->Error);
    MdoSessionBackupRelease(Backup);
    snprintf(Logical,sizeof(Logical),"sessions/%s.json",Session);
    snprintf(Leaf,sizeof(Leaf),"files/%08u",(unsigned)Capture->Index);
    MdoDtTaskPath(Path,false,Capture->Id,Leaf);
    if (Json && MdoDtHex(Json,Bytes,Hash) && MdoDtWrite(Path,Json,Bytes))
        Ok=MdoDtAdd(Capture,Logical,"session",Bytes,Hash,0600u);
    xrtFree(Json); return Ok;
}
bool MdoDtCapture(xvalue* Task,xcancel* Cancel,xwork_error* Error) {
    cstr Id=MdoAccountText(Task,"id",32u),Project=MdoAccountText(Task,"project_id",64u);
    cstr Kind=MdoAccountText(Task,"kind",16u),Session=MdoAccountText(Task,"session_id",32u);
    bool Full=Kind && !strcmp(Kind,"project"),Ok=false,Found=false;
    MdoProjectLease* Owner=NULL; MdoProjectBinding Binding={0}; MdoProjectInfo Info={0};
    MdoDtCaptureState Capture={0}; xroot Workspace=NULL,Memory=NULL; xdir Sessions=NULL;
    xvalue* Manifest=NULL; char Path[256],MemoryPath[128]; str MemoryNative=NULL;
    xworkErrorInit(Error); Capture.Files=xrtValueArray(); Capture.Id=Id; Capture.Cancel=Cancel; Capture.Error=Error;
    Owner=MdoProjectLeaseAcquire(Project,Full?MDO_PROJECT_LEASE_EXCLUSIVE:MDO_PROJECT_LEASE_SHARED,Error);
    if (!Owner) goto done;
    Binding.Size=sizeof(Binding); Info.Size=sizeof(Info);
    if (!MdoProjectBindingPrepare(Project,&Binding,Error) || !MdoProjectGet(Project,&Info,&Found,Error)) goto done;
    MdoHomeSnapshot Home={0}; Home.Size=sizeof(Home);
    if (!MdoHomeGetSnapshot(&Home)) goto done;
    Capture.HomePath=Home.Path;
    xroot HomeRoot=xrtRootOpen(Home.Path);
    if (!HomeRoot || !xrtRootStat(HomeRoot,".",false,&Capture.Home)) { xrtRootClose(HomeRoot); goto done; }
    xrtRootClose(HomeRoot);
    if (Full) {
        snprintf(Path,sizeof(Path),"sessions/%s",Project);
        bool Exists=false; xfileinfo Stat;
        if (!MdoHomeExternalStat(Path,&Exists,&Stat)) goto done;
        if (Exists) {
            Sessions=MdoHomeOpenDirectory(Path,XDIR_STAT); xdirentry Entry; xdirnext Next;
            if (!Sessions) goto done;
            while ((Next=xrtDirNext(Sessions,&Entry))==XDIR_NEXT_ITEM) {
                char Sid[33];
                if (Entry.Info.Type!=XFILE_TYPE_DIRECTORY || !Entry.Name.Size || Entry.Name.Size>32u) goto done;
                memcpy(Sid,Entry.Name.Data,Entry.Name.Size); Sid[Entry.Name.Size]=0;
                if (!MdoDtSessionId(Sid) || !MdoDtCaptureSession(&Capture,Project,Sid,Owner)) goto done;
            }
            if (Next!=XDIR_NEXT_END) goto done;
            xrtDirClose(Sessions); Sessions=NULL;
        }
        size_t Start=Capture.Index;
        Workspace=xrtRootOpen(Binding.WorkspaceRoot);
        if (!Workspace || !MdoDtWalk(&Capture,Workspace,".","workspace",0u)) goto done;
        size_t End=Capture.Index;
        Capture.Check=true; Capture.Index=Start;
        if (!MdoDtWalk(&Capture,Workspace,".","workspace",0u) || Capture.Index!=End) goto done;
        Capture.Check=false; Capture.Index=End;
        snprintf(MemoryPath,sizeof(MemoryPath),"memory/projects/%s",Project);
        if (!MdoHomeExternalStat(MemoryPath,&Exists,&Stat)) goto done;
        if (Exists) {
            if (Stat.Type!=XFILE_TYPE_DIRECTORY) { MdoDtCaptureError(&Capture,"project memory root is a link or unsupported object"); goto done; }
            MemoryNative=MdoHomeExternalPath(MemoryPath); Memory=xrtRootOpen(MemoryNative);
            Start=Capture.Index;
            if (!Memory || !MdoDtWalk(&Capture,Memory,".","memory",0u)) goto done;
            End=Capture.Index; Capture.Check=true; Capture.Index=Start;
            if (!MdoDtWalk(&Capture,Memory,".","memory",0u) || Capture.Index!=End) goto done;
            Capture.Check=false; Capture.Index=End;
        }
        /* Legacy memory is preserved beside the new Markdown tree. */
        snprintf(MemoryPath,sizeof(MemoryPath),"memory/projects/%s.json",Project);
        if (!MdoHomeExternalStat(MemoryPath,&Exists,&Stat)) goto done;
        if (Exists) {
            size_t Bytes=0; char* Raw=MdoDtBytes(MemoryPath,8u*1024u*1024u,&Bytes); char Hash[65],Leaf[40];
            snprintf(Leaf,sizeof(Leaf),"files/%08u",(unsigned)Capture.Index); MdoDtTaskPath(Path,false,Id,Leaf);
            bool Copied=Raw && MdoDtHex(Raw,Bytes,Hash) && MdoDtWrite(Path,Raw,Bytes) &&
                MdoDtAdd(&Capture,"legacy-memory.json","legacy_memory",Bytes,Hash,0600u);
            xrtFree(Raw); if (!Copied) goto done;
        }
    } else if (!Session || !MdoDtCaptureSession(&Capture,Project,Session,Owner)) goto done;
    Manifest=xrtValueObject();
    if (!Manifest || !MdoAccountSetUInt(Manifest,"schema_version",1u) || !MdoAccountSetString(Manifest,"kind",Kind) ||
        !MdoAccountSetString(Manifest,"project_id",Project) || !MdoAccountSetString(Manifest,"name",Found?Info.Name:"默认项目") ||
        !MdoAccountSetString(Manifest,"source_workspace",Binding.WorkspaceRoot) ||
        !MdoApiValueSetInt(Manifest,"captured_at",MdoUtcUs()) || !MdoAccountSetUInt(Manifest,"total_bytes",Capture.Bytes) ||
        !xrtValueObjectSetTake(Manifest,XRT_STR_LITERAL("files"),&Capture.Files)) goto done;
    MdoDtTaskPath(Path,false,Id,"manifest.json");
    if (!MdoDtSave(Path,Manifest)) goto done;
    xfile File=MdoHomeOpenRead(Path); char Hash[65]; uint64 Bytes=0;
    Ok=MdoDtHash(File,Hash,&Bytes,Cancel); xrtClose(File);
    if (Ok) Ok=MdoAccountSetUInt(Task,"manifest_bytes",Bytes) && MdoAccountSetString(Task,"manifest_sha256",Hash) &&
        MdoAccountSetUInt(Task,"total_bytes",Capture.Bytes) && MdoAccountSetUInt(Task,"file_count",Capture.Index) &&
        MdoDtTaskState(Task,"connecting","") && MdoDtTaskSave(false,Task);
done:
    xrtDirClose(Sessions); xrtRootClose(Workspace); xrtRootClose(Memory); xrtFree(MemoryNative);
    bool CapturedProject=Owner!=NULL && Full;
    MdoProjectLeaseRelease(Owner); xrtValueRelease(Capture.Files); xrtValueRelease(Manifest);
    if (CapturedProject) { (void)MdoSessionCatalogChanged(); MdoApiLiveChanged(NULL); }
    if (!Ok && Error->eCode==XWORK_ERROR_NONE) { Error->eCode=XWORK_ERROR_IO; snprintf(Error->sMessage,sizeof(Error->sMessage),"cannot capture a stable complete project; check filenames, space and active writers"); }
    return Ok;
}
