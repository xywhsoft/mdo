#include "../../include/mdo/time.h"
#include "internal.h"

void MdoDtTargetSession(cstr Transfer,cstr Source,char Target[33]) {
    char Text[96],Hash[65]; snprintf(Text,sizeof(Text),"%s:%s",Transfer,Source);
    MdoDtHex(Text,strlen(Text),Hash); memcpy(Target,Hash,32u); Target[32]=0;
    if (!strcmp(Target,Source)) Target[0]=Target[0]=='a'?'b':'a';
}
static bool MdoDtApplyMode(xroot Root,cstr Path,uint32 Mode) {
#if defined(_WIN32) || defined(_WIN64)
    /* Keep the destination's inherited ACL. xrtRootSetMode deliberately
     * rejects POSIX permissions on Windows; it cannot represent that ACL. */
    (void)Root; (void)Path; (void)Mode;
    return true;
#else
    return xrtRootSetMode(Root,Path,false,Mode);
#endif
}
static bool MdoDtParents(xroot Root,cstr Path) {
    char Copy[2049]; if (!MdoDtPath(Path)) return false;
    strcpy(Copy,Path);
    for (char* p=Copy;*p;p++) if (*p=='/') {
        *p=0; xfileinfo Info;
        if (!xrtRootStat(Root,Copy,false,&Info)) {
            xrtClearError(); if (!xrtRootDirCreate(Root,Copy,0700u)) return false;
        } else if (Info.Type!=XFILE_TYPE_DIRECTORY) return false;
        *p='/';
    }
    return true;
}
static bool MdoDtCopyReceived(xroot Root,cstr Target,cstr Payload,const xvalue* Row,xcancel* Cancel) {
    uint64 Expected=0,Total=0; cstr Digest=MdoAccountText(Row,"sha256",64u); char Hash[65];
    if (!Digest || !MdoAccountGetUInt(xrtValueObjectGet(Row,XRT_STR_LITERAL("bytes")),&Expected) || !MdoDtParents(Root,Target)) return false;
    xfileinfo Info; bool Exists=xrtRootStat(Root,Target,false,&Info);
    if (Exists) {
        xfileoptions Read; xrtFileOptionsInit(&Read); Read.Flags=XFILE_READ|XFILE_NOFOLLOW;
        xfile File=xrtRootFileOpen(Root,Target,&Read);
        bool Same=File && MdoDtHash(File,Hash,&Total,Cancel) && Total==Expected && !strcmp(Hash,Digest);
        xrtClose(File); return Same;
    }
    xrtClearError(); xfile Input=MdoHomeOpenRead(Payload),Output=NULL; bool Ok=false;
    char Temporary[2100]; snprintf(Temporary,sizeof(Temporary),"%s.mdo-transfer-part",Target);
    xfileoptions Options; xrtFileOptionsInit(&Options); Options.Flags=XFILE_WRITE|XFILE_CREATE|XFILE_TRUNCATE|XFILE_NOFOLLOW; Options.Mode=0600u;
    Output=xrtRootFileOpen(Root,Temporary,&Options);
    uint8 Buffer[65536];
    while (Input && Output && !xrtCancelRequested(Cancel)) {
        size_t Read=0,Written=0;
        if (!xrtRead(Input,Buffer,sizeof(Buffer),&Read)) goto done;
        if (!Read) break;
        if (!xrtWrite(Output,Buffer,Read,&Written) || Written!=Read) goto done;
        MdoDtKeepAwake();
    }
    Ok=Input && Output && !xrtCancelRequested(Cancel) && xrtFlush(Output);
done:
    xrtClose(Input); xrtClose(Output);
    if (Ok) {
        Options.Flags=XFILE_READ|XFILE_NOFOLLOW; Output=xrtRootFileOpen(Root,Temporary,&Options);
        Ok=Output && MdoDtHash(Output,Hash,&Total,Cancel) && Total==Expected && !strcmp(Hash,Digest);
        xrtClose(Output);
        uint64 Mode=0600u; MdoAccountGetUInt(xrtValueObjectGet(Row,XRT_STR_LITERAL("mode")),&Mode);
        if (Ok) Ok=MdoDtApplyMode(Root,Temporary,(uint32)Mode) && xsRootRenameNoReplace(Root,Temporary,Target);
    }
    return Ok;
}
static bool MdoDtMark(cstr Project,cstr Session,bool Pending,cstr Id) {
    char Path[256]; snprintf(Path,sizeof(Path),MDO_DT_BASE "/pending/%s/%s",Project,Session?Session:"project");
    return Pending?MdoDtWrite(Path,Id,strlen(Id)):MdoHomeRemove(Path,false);
}
static bool MdoDtVerifyPublished(cstr Path,const xvalue* Receipt,xcancel* Cancel) {
    str Native=MdoHomeExternalPath(Path); xroot Root=Native?xrtRootOpen(Native):NULL; xrtFree(Native);
    xfileinfo Info; uint64 Device=0,Identity=0;
    const xvalue* Files=xrtValueObjectGet(Receipt,XRT_STR_LITERAL("files")); bool Ok=false;
    if (!Root || !MdoAccountGetUInt(xrtValueObjectGet(Receipt,XRT_STR_LITERAL("device")),&Device) ||
        !MdoAccountGetUInt(xrtValueObjectGet(Receipt,XRT_STR_LITERAL("identity")),&Identity) ||
        !xrtRootStat(Root,".",false,&Info) || Info.Device!=Device || Info.Identity!=Identity) goto done;
    for (size_t i=0;i<xrtValueCount(Files);i++) {
        const xvalue* Row=xrtValueArrayGet(Files,i); cstr Relative=MdoAccountText(Row,"path",256u);
        cstr Digest=MdoAccountText(Row,"sha256",64u); uint64 Expected=0,Total=0; char Hash[65];
        xfileoptions Options; xrtFileOptionsInit(&Options); Options.Flags=XFILE_READ|XFILE_NOFOLLOW;
        if (!Relative || !Digest || !MdoAccountGetUInt(xrtValueObjectGet(Row,XRT_STR_LITERAL("bytes")),&Expected)) goto done;
        xfile File=xrtRootFileOpen(Root,Relative,&Options);
        bool Match=MdoDtHash(File,Hash,&Total,Cancel) && Total==Expected && !strcmp(Hash,Digest);
        xrtClose(File); if (!Match) goto done;
    }
    Ok=true;
done:
    xrtRootClose(Root); return Ok;
}
static bool MdoDtCommitSession(xvalue* Task,const xvalue* Manifest,size_t Index,cstr Workspace,xcancel* Cancel,xwork_error* Error) {
    cstr Id=MdoAccountText(Task,"id",32u),Project=MdoAccountText(Task,"destination_project_id",64u);
    const xvalue* Row=xrtValueArrayGet(xrtValueObjectGet(Manifest,XRT_STR_LITERAL("files")),Index);
    cstr Logical=MdoAccountText(Row,"path",2048u); char Source[33],Target[33],Parent[33]="",Path[256],Leaf[96],Destination[256];
    size_t SourceSize=strlen(Logical)-14u;
    memcpy(Source,Logical+9u,SourceSize); Source[SourceSize]=0; MdoDtTargetSession(Id,Source,Target);
    snprintf(Destination,sizeof(Destination),"sessions/%s/%s",Project,Target);
    snprintf(Leaf,sizeof(Leaf),"prepared/%s.json",Target); MdoDtTaskPath(Path,true,Id,Leaf);
    xvalue* Receipt=MdoDtRead(Path,256u*1024u); bool Ok=false;
    if (!MdoDtMark(Project,Target,true,Id)) goto done;
    if (!Receipt) {
        size_t Bytes=0; snprintf(Leaf,sizeof(Leaf),"files/%08u",(unsigned)Index); MdoDtTaskPath(Path,true,Id,Leaf);
        char* Json=MdoDtBytes(Path,MDO_SESSION_BACKUP_MAX_DOCUMENT_BYTES,&Bytes);
        MdoSessionBackup* Backup=Json?MdoSessionBackupDecode(Json,Bytes,NULL,Cancel,Error):NULL; xrtFree(Json);
        MdoSessionBackup* Prepared=NULL; MdoSessionBackupPreview Preview={0}; Preview.Size=sizeof(Preview);
        MdoSessionBackupRestoreInfo Facts={0}; Facts.Size=sizeof(Facts);
        MdoSessionBackupStage* Stage=NULL; MdoSessionBackupStageInfo Info={0}; Info.Size=sizeof(Info);
        if (!Backup || !MdoSessionBackupPreviewGet(Backup,&Preview)) { MdoSessionBackupRelease(Backup); goto done; }
        if (Preview.Info.ParentSessionId[0]) {
            char ParentPath[64]; snprintf(ParentPath,sizeof(ParentPath),"sessions/%s.json",Preview.Info.ParentSessionId);
            const xvalue* Files=xrtValueObjectGet(Manifest,XRT_STR_LITERAL("files"));
            for (size_t i=0;i<xrtValueCount(Files);i++) {
                cstr Name=MdoAccountText(xrtValueArrayGet(Files,i),"path",2048u);
                if (Name && !strcmp(Name,ParentPath)) MdoDtTargetSession(Id,Preview.Info.ParentSessionId,Parent);
            }
        }
        int64 RestoredAt=0; (void)xrtValueGetInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("created_at")),&RestoredAt);
        MdoSessionBackupRestoreTarget Options={0}; Options.Size=sizeof(Options); Options.ProjectId=Project;
        Options.SessionId=Target; Options.WorkspaceRoot=Workspace; Options.RestoredAt=RestoredAt;
        Options.PreserveLifecycle=true; Options.ParentSessionId=Parent;
        Prepared=MdoSessionBackupPrepareRestore(Backup,&Options,NULL,Cancel,&Facts,Error); MdoSessionBackupRelease(Backup);
        MdoDtTaskPath(Path,true,Id,"stage"); xroot Anchor=MdoHomeOpenStorageDirectory(Path);
        bool Staged=Prepared && Anchor && MdoSessionBackupStagePrepare(Prepared,Anchor,NULL,Cancel,&Stage,Error) &&
            MdoSessionBackupStageCheck(Stage,NULL,Cancel,Error) && MdoSessionBackupStageInfoGet(Stage,&Info);
        xrtRootClose(Anchor);
        if (!Staged) {
            if (Stage) (void)MdoSessionBackupStageDiscard(&Stage,NULL);
            MdoSessionBackupRelease(Prepared); goto done;
        }
        Receipt=xrtValueObject(); xvalue* Files=xrtValueArray();
        for (size_t i=0;Files && i<MdoSessionBackupFileCount(Prepared);i++) {
            MdoSessionBackupFile File; char Hash[65]; xvalue* Item=xrtValueObject();
            if (!MdoSessionBackupFileGet(Prepared,i,&File) || !MdoDtHex(File.Data,File.Bytes,Hash) ||
                !MdoAccountSetString(Item,"path",File.Path) || !MdoAccountSetUInt(Item,"bytes",File.Bytes) ||
                !MdoAccountSetString(Item,"sha256",Hash) || !xrtValueArrayAppendTake(Files,&Item)) {
                xrtValueRelease(Item); xrtValueRelease(Files); Files=NULL; break;
            }
        }
        char Staging[256]; snprintf(Staging,sizeof(Staging),MDO_DT_BASE "/incoming/%s/stage/%s",Id,Info.DirectoryName);
        snprintf(Leaf,sizeof(Leaf),"prepared/%s.json",Target); MdoDtTaskPath(Path,true,Id,Leaf);
        bool Saved=Receipt && Files && MdoAccountSetString(Receipt,"staging",Staging) &&
            MdoAccountSetUInt(Receipt,"device",Info.DirectoryIdentity.Device) && MdoAccountSetUInt(Receipt,"identity",Info.DirectoryIdentity.Identity) &&
            xrtValueObjectSetTake(Receipt,XRT_STR_LITERAL("files"),&Files) && MdoDtSave(Path,Receipt);
        xrtValueRelease(Files); MdoSessionBackupRelease(Prepared);
        if (!Saved) { (void)MdoSessionBackupStageDiscard(&Stage,NULL); goto done; }
        if (!MdoSessionBackupStageRelease(&Stage,Error)) goto done;
    }
    bool Exists=false;
    if (!MdoHomeExternalStat(Destination,&Exists,NULL)) goto done;
    if (!Exists) {
        char ParentPath[128]; snprintf(ParentPath,sizeof(ParentPath),"sessions/%s",Project);
        xroot ParentRoot=MdoHomeOpenStorageDirectory(ParentPath); xrtRootClose(ParentRoot);
        cstr Staging=MdoAccountText(Receipt,"staging",255u);
        if (!Staging || !MdoDtVerifyPublished(Staging,Receipt,Cancel) || !MdoHomeRenameNoReplace(Staging,Destination)) goto done;
    }
    Ok=MdoDtVerifyPublished(Destination,Receipt,Cancel) && MdoAccountSetString(Task,"destination_session_id",Target);
done:
    xrtValueRelease(Receipt); return Ok;
}
static bool MdoDtCommitTree(xvalue* Task,const xvalue* Manifest,cstr Prefix,xroot Root,xcancel* Cancel) {
    const xvalue* Files=xrtValueObjectGet(Manifest,XRT_STR_LITERAL("files"));
    for (size_t i=0;i<xrtValueCount(Files);i++) {
        const xvalue* Row=xrtValueArrayGet(Files,i); cstr Logical=MdoAccountText(Row,"path",2048u);
        cstr Type=MdoAccountText(Row,"type",16u); size_t Start=strlen(Prefix);
        if (strncmp(Logical,Prefix,Start) || (Logical[Start] && Logical[Start]!='/')) continue;
        cstr Relative=Logical[Start]?Logical+Start+1u:".";
        if (!strcmp(Type,"directory")) {
            if (!strcmp(Relative,".")) continue;
            xfileinfo Info;
            if (!MdoDtParents(Root,Relative)) return false;
            if (xrtRootStat(Root,Relative,false,&Info)) { if (Info.Type!=XFILE_TYPE_DIRECTORY) return false; }
            else { xrtClearError(); if (!xrtRootDirCreate(Root,Relative,0700u)) return false; }
        } else {
            char Path[256],Leaf[40]; snprintf(Leaf,sizeof(Leaf),"files/%08u",(unsigned)i);
            MdoDtTaskPath(Path,true,MdoAccountText(Task,"id",32u),Leaf);
            if (!MdoDtCopyReceived(Root,Relative,Path,Row,Cancel)) return false;
        }
    }
    /* Apply directory permissions only after children are complete. */
    for (size_t i=xrtValueCount(Files);i>0;i--) {
        const xvalue* Row=xrtValueArrayGet(Files,i-1u); cstr Logical=MdoAccountText(Row,"path",2048u);
        cstr Type=MdoAccountText(Row,"type",16u); uint64 Mode=0700u; size_t Start=strlen(Prefix);
        if (!strcmp(Type,"directory") && !strncmp(Logical,Prefix,Start) && (!Logical[Start] || Logical[Start]=='/')) {
            MdoAccountGetUInt(xrtValueObjectGet(Row,XRT_STR_LITERAL("mode")),&Mode);
            if (!MdoDtApplyMode(Root,Logical[Start]?Logical+Start+1u:".",(uint32)Mode)) return false;
        }
    }
    return true;
}
static bool MdoDtFinishVisibility(xvalue* Task,const xvalue* Manifest) {
    cstr Id=MdoAccountText(Task,"id",32u),Project=MdoAccountText(Task,"destination_project_id",64u);
    const xvalue* Files=xrtValueObjectGet(Manifest,XRT_STR_LITERAL("files"));
    for (size_t i=0;i<xrtValueCount(Files);i++) {
        const xvalue* Row=xrtValueArrayGet(Files,i); cstr Type=MdoAccountText(Row,"type",16u);
        if (!strcmp(Type,"session")) {
            char Source[33],Target[33]; cstr Logical=MdoAccountText(Row,"path",2048u); size_t Size=strlen(Logical)-14u;
            memcpy(Source,Logical+9u,Size); Source[Size]=0; MdoDtTargetSession(Id,Source,Target);
            if (!MdoDtMark(Project,Target,false,Id)) return false;
        }
    }
    if (!strcmp(MdoAccountText(Task,"kind",16u),"project") && !MdoDtMark(Project,NULL,false,Id)) return false;
    if (!MdoDtTaskState(Task,"completed","") || !MdoDtTaskSave(true,Task)) return false;
    (void)MdoSessionCatalogChanged(); MdoApiLiveChanged(NULL); return true;
}
typedef struct MdoDtBoundPublication { xvalue* Task; const xvalue* Manifest; } MdoDtBoundPublication;
static bool MdoDtPublishBound(const MdoProjectBinding* Current,void* Data,xwork_error* Error) {
    (void)Current; (void)Error; MdoDtBoundPublication* Publish=Data;
    return MdoDtTaskState(Publish->Task,"committed","") && MdoDtTaskSave(true,Publish->Task) &&
        MdoDtFinishVisibility(Publish->Task,Publish->Manifest);
}
bool MdoDtCommit(xvalue* Task,xcancel* Cancel,xwork_error* Error) {
    cstr Id=MdoAccountText(Task,"id",32u),Project=MdoAccountText(Task,"destination_project_id",64u);
    cstr Kind=MdoAccountText(Task,"kind",16u); bool Full=Kind && !strcmp(Kind,"project"),Ok=false;
    char Path[256],Pending[80],Final[80]; str Workspace=NULL,ParentPath=NULL;
    xvalue* Manifest=NULL; xroot Parent=NULL,Root=NULL; MdoProjectLease* Owner=NULL;
    MdoDtTaskPath(Path,true,Id,"manifest.json"); Manifest=MdoDtRead(Path,MDO_DT_MANIFEST);
    if (!MdoDtManifestValid(Manifest)) goto done;
    cstr State=MdoAccountText(Task,"state",32u);
    if (State && (!strcmp(State,"committed") || !strcmp(State,"completed"))) { Ok=MdoDtFinishVisibility(Task,Manifest); goto done; }
    /* File verification belongs on this worker, never the chunk HTTP callback. */
    const xvalue* Files=xrtValueObjectGet(Manifest,XRT_STR_LITERAL("files"));
    for (size_t i=0;i<xrtValueCount(Files);i++) {
        const xvalue* Row=xrtValueArrayGet(Files,i); cstr Type=MdoAccountText(Row,"type",16u);
        if (!strcmp(Type,"directory")) continue;
        char Leaf[40],Hash[65]; uint64 Bytes=0,Expected=0;
        snprintf(Leaf,sizeof(Leaf),"files/%08u",(unsigned)i); MdoDtTaskPath(Path,true,Id,Leaf);
        xfile File=MdoHomeOpenRead(Path);
        MdoAccountGetUInt(xrtValueObjectGet(Row,XRT_STR_LITERAL("bytes")),&Expected);
        bool Match=MdoDtHash(File,Hash,&Bytes,Cancel) && Bytes==Expected && !strcmp(Hash,MdoAccountText(Row,"sha256",64u));
        xrtClose(File); if (!Match) goto done; MdoDtKeepAwake();
    }
    Owner=MdoProjectLeaseAcquire(Project,MDO_PROJECT_LEASE_SHARED,Error); if (!Owner) goto done;
    MdoProjectBinding Binding={0}; Binding.Size=sizeof(Binding);
    if (Full) {
        if (!MdoDtMark(Project,NULL,true,Id)) goto done;
        cstr Selected=MdoAccountText(Task,"target_directory",2048u);
        if (Selected && Selected[0]) ParentPath=xrtPathAbs(Selected);
        else {
            Root=MdoHomeOpenStorageDirectory("workspace/received-projects"); xrtRootClose(Root); Root=NULL;
            ParentPath=MdoHomeExternalPath("workspace/received-projects");
        }
        Parent=ParentPath?xrtRootOpen(ParentPath):NULL;
        if (!Parent) goto done;
        xfileinfo ParentInfo; uint64 ParentDevice=0,ParentIdentity=0;
        if (!xrtRootStat(Parent,".",false,&ParentInfo)) goto done;
        if (MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("parent_identity")),&ParentIdentity)) {
            MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("parent_device")),&ParentDevice);
            if (ParentDevice!=ParentInfo.Device || ParentIdentity!=ParentInfo.Identity) goto done;
        } else if (!MdoAccountSetUInt(Task,"parent_device",ParentInfo.Device) ||
            !MdoAccountSetUInt(Task,"parent_identity",ParentInfo.Identity) || !MdoDtTaskSave(true,Task)) goto done;
        uint64 Total=0; MdoAccountGetUInt(xrtValueObjectGet(Manifest,XRT_STR_LITERAL("total_bytes")),&Total);
        if (!MdoDtDiskSpace(ParentPath,Total)) goto done;
        snprintf(Pending,sizeof(Pending),".mdo-transfer-%s",Id); snprintf(Final,sizeof(Final),"mdo-%s",Id);
        Workspace=xrtPathJoin(ParentPath,Final); xfileinfo Info;
        bool Published=xrtRootStat(Parent,Final,false,&Info); xrtClearError();
        uint64 Device=0,Identity=0;
        if (Published) {
            if (!MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("workspace_device")),&Device) ||
                !MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("workspace_identity")),&Identity) ||
                Info.Device!=Device || Info.Identity!=Identity) goto done;
            Root=xrtRootOpenIn(Parent,Final);
            if (!Root || !MdoDtCommitTree(Task,Manifest,"workspace",Root,Cancel)) goto done;
            xrtRootClose(Root); Root=NULL;
        } else {
            uint64 StageDevice=0,StageIdentity=0;
            if (!xrtRootStat(Parent,Pending,false,&Info)) {
                xrtClearError();
                if (!xrtRootDirCreate(Parent,Pending,0700u) || !xrtRootStat(Parent,Pending,false,&Info) ||
                    !MdoAccountSetUInt(Task,"stage_workspace_device",Info.Device) || !MdoAccountSetUInt(Task,"stage_workspace_identity",Info.Identity) ||
                    !MdoDtTaskSave(true,Task)) goto done;
            } else if (!MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("stage_workspace_device")),&StageDevice) ||
                !MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("stage_workspace_identity")),&StageIdentity) ||
                Info.Device!=StageDevice || Info.Identity!=StageIdentity) goto done;
            Root=xrtRootOpenIn(Parent,Pending); if (!Root) goto done;
            if (!MdoDtCommitTree(Task,Manifest,"workspace",Root,Cancel) || !xrtRootStat(Root,".",false,&Info)) goto done;
            xrtRootClose(Root); Root=NULL;
            if (!MdoAccountSetUInt(Task,"workspace_device",Info.Device) || !MdoAccountSetUInt(Task,"workspace_identity",Info.Identity) ||
                !MdoAccountSetString(Task,"destination_workspace",Workspace) || !MdoDtTaskSave(true,Task) ||
                !xsRootRenameNoReplace(Parent,Pending,Final)) goto done;
        }
        snprintf(Path,sizeof(Path),"memory/projects/%s",Project); Root=MdoHomeOpenStorageDirectory(Path);
        if (!Root || !MdoDtCommitTree(Task,Manifest,"memory",Root,Cancel)) goto done;
        xrtRootClose(Root); Root=NULL;
    } else {
        if (!MdoProjectBindingPrepare(Project,&Binding,Error)) goto done;
        Workspace=xrtStrDup(Binding.WorkspaceRoot);
        uint64 Reviewed=0,Device=0,Identity=0; int64 Created=0;
        cstr ExpectedWorkspace=MdoAccountText(Task,"target_workspace",2048u);
        if (!MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("target_project_revision")),&Reviewed) || Reviewed!=Binding.Revision ||
            !xrtValueGetInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("target_project_created_at")),&Created) || Created!=Binding.CreatedAt ||
            !MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("target_workspace_device")),&Device) || Device!=Binding.WorkspaceIdentity.Device ||
            !MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("target_workspace_identity")),&Identity) || Identity!=Binding.WorkspaceIdentity.Identity ||
            !ExpectedWorkspace || strcmp(ExpectedWorkspace,Binding.WorkspaceRoot)) goto done;
    }
    if (!Workspace) goto done;
    for (size_t i=0;i<xrtValueCount(Files);i++) {
        const xvalue* Row=xrtValueArrayGet(Files,i); cstr Type=MdoAccountText(Row,"type",16u);
        if (!strcmp(Type,"session") && !MdoDtCommitSession(Task,Manifest,i,Workspace,Cancel,Error)) goto done;
        if (!strcmp(Type,"legacy_memory")) {
            char Source[256],Leaf[40]; snprintf(Leaf,sizeof(Leaf),"files/%08u",(unsigned)i); MdoDtTaskPath(Source,true,Id,Leaf);
            size_t Bytes=0; char* Json=MdoDtBytes(Source,8u*1024u*1024u,&Bytes);
            snprintf(Path,sizeof(Path),"memory/projects/%s.json",Project);
            bool Copied=Json && MdoDtWrite(Path,Json,Bytes); xrtFree(Json); if (!Copied) goto done;
        }
    }
    uint64 Member=0; MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("member_id")),&Member);
    if (xrtCancelRequested(Cancel) || !MdoDtMember(Member) || !MdoDtReceiveAllowed()) goto done;
    if (Full) {
        xroot CurrentParent=xrtRootOpen(ParentPath); xfileinfo CurrentInfo; uint64 Device=0,Identity=0;
        MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("parent_device")),&Device);
        MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("parent_identity")),&Identity);
        bool Same=CurrentParent && xrtRootStat(CurrentParent,".",false,&CurrentInfo) && CurrentInfo.Device==Device && CurrentInfo.Identity==Identity;
        xrtRootClose(CurrentParent); if (!Same) goto done;
        /* The private marker hides a completed definition until the durable
         * transfer result is committed. Existing definitions are never replaced. */
        snprintf(Path,sizeof(Path),"projects/%s.json",Project); bool Exists=false;
        if (!MdoHomeExternalStat(Path,&Exists,NULL)) goto done;
        if (!Exists) {
            MdoProjectCreateOptions Options; MdoProjectCreateOptionsInit(&Options);
            Options.Id=Project; Options.Name=MdoAccountText(Manifest,"name",256u); Options.WorkspaceRoot=Workspace;
            if (!MdoProjectPublishTransfer(&Options,Owner,NULL,Error)) goto done;
        } else {
            xvalue* Definition=MdoDtRead(Path,8192u); cstr Existing=MdoAccountText(Definition,"workspace_root",2048u);
            bool Same=Existing && !strcmp(Existing,Workspace); xrtValueRelease(Definition); if (!Same) goto done;
        }
    }
    if (!MdoAccountSetString(Task,"destination_workspace",Workspace)) goto done;
    if (Full) {
        if (!MdoDtTaskState(Task,"committed","") || !MdoDtTaskSave(true,Task)) goto done;
        Ok=MdoDtFinishVisibility(Task,Manifest);
    } else {
        MdoDtBoundPublication Publication={Task,Manifest};
        Ok=MdoProjectWithBinding(&Binding,Owner,MdoDtPublishBound,&Publication,Error);
    }
done:
    xrtRootClose(Root); xrtRootClose(Parent); xrtFree(Workspace); xrtFree(ParentPath); xrtValueRelease(Manifest); MdoProjectLeaseRelease(Owner);
    if (!Ok && Error->eCode==XWORK_ERROR_NONE) { Error->eCode=XWORK_ERROR_IO; snprintf(Error->sMessage,sizeof(Error->sMessage),"target commit paused: check free space, permissions, project identity and account; received data is retained"); }
    return Ok;
}
