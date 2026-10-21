#include "../../include/mdo/time.h"
#include "internal.h"

static int MdoDtPathCompare(const void* A,const void* B) { return strcmp(*(char* const*)A,*(char* const*)B); }
bool MdoDtManifestValid(const xvalue* Manifest) {
    static const char* const Keys[]={"schema_version","kind","project_id","name","source_workspace","captured_at","total_bytes","files"};
    static const char* const FileKeys[]={"path","type","sha256","bytes","mode"};
    uint64 Version=0,Total=0,Declared=0; cstr Kind=MdoAccountText(Manifest,"kind",16u);
    const xvalue* Files=xrtValueObjectGet(Manifest,XRT_STR_LITERAL("files")); size_t Count=xrtValueCount(Files);
    if (!MdoDtKeys(Manifest,Keys,sizeof(Keys)/sizeof(*Keys)) || xrtValueCount(Manifest)!=8u || !Kind || (strcmp(Kind,"session") && strcmp(Kind,"project")) ||
        !MdoAccountGetUInt(xrtValueObjectGet(Manifest,XRT_STR_LITERAL("schema_version")),&Version) || Version!=1u ||
        !MdoAccountGetUInt(xrtValueObjectGet(Manifest,XRT_STR_LITERAL("total_bytes")),&Declared) ||
        !MdoAccountText(Manifest,"name",256u) || xrtValueType(Files)!=XVALUE_ARRAY || !Count || Count>MDO_DT_FILES) return false;
    char** Names=xrtCalloc(Count,sizeof(*Names)); if (!Names) return false;
    bool Ok=false; size_t Sessions=0;
    for (size_t i=0;i<Count;i++) {
        const xvalue* Row=xrtValueArrayGet(Files,i); cstr Path=MdoAccountText(Row,"path",2048u);
        cstr Type=MdoAccountText(Row,"type",16u),Hash=MdoAccountText(Row,"sha256",64u); uint64 Bytes=0,Mode=0;
        if (!MdoDtKeys(Row,FileKeys,sizeof(FileKeys)/sizeof(*FileKeys)) || xrtValueCount(Row)!=5u || !MdoDtPath(Path) || strstr(Path,".mdo-transfer-part") || !Type || !Hash ||
            !MdoAccountGetUInt(xrtValueObjectGet(Row,XRT_STR_LITERAL("bytes")),&Bytes) || Bytes>UINT64_C(9007199254740991)-Total ||
            !MdoAccountGetUInt(xrtValueObjectGet(Row,XRT_STR_LITERAL("mode")),&Mode) || Mode>0777u) goto done;
        bool Directory=!strcmp(Type,"directory"),Session=!strcmp(Type,"session"),Legacy=!strcmp(Type,"legacy_memory");
        bool Workspace=!strcmp(Path,"workspace") || !strncmp(Path,"workspace/",10u);
        bool Memory=!strcmp(Path,"memory") || !strncmp(Path,"memory/",7u);
        if (Directory) { if (Bytes || Hash[0] || (!Workspace && !Memory)) goto done; }
        else {
            if (!MdoAccountHex(Hash,64u)) goto done;
            if (!Bytes && strcmp(Hash,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) goto done;
            if (Session) {
                char Id[33]; size_t Length=strlen(Path);
                if (Length<15u || Length>46u || strncmp(Path,"sessions/",9u) || strcmp(Path+Length-5u,".json") || Bytes>MDO_SESSION_BACKUP_MAX_DOCUMENT_BYTES) goto done;
                memcpy(Id,Path+9,Length-14u); Id[Length-14u]=0; if (!MdoDtSessionId(Id)) goto done; Sessions++;
            } else if (Legacy) { if (strcmp(Path,"legacy-memory.json") || Bytes>8u*1024u*1024u) goto done; }
            else if (strcmp(Type,"file") || (!Workspace && !Memory)) goto done;
            if (!strcmp(Kind,"session") && !Session) goto done;
        }
        Names[i]=xrtStrDup(Path); if (!Names[i]) goto done;
        for (char* p=Names[i];*p;p++) if (*p>='A' && *p<='Z') *p=(char)(*p+'a'-'A');
        Total+=Bytes;
    }
    qsort(Names,Count,sizeof(*Names),MdoDtPathCompare);
    for (size_t i=1;i<Count;i++) if (!strcmp(Names[i-1],Names[i])) goto done;
    /* A regular file cannot also be the parent of another entry. */
    for (size_t i=0;i<Count;i++) {
        const xvalue* Row=xrtValueArrayGet(Files,i); cstr Type=MdoAccountText(Row,"type",16u);
        if (!strcmp(Type,"directory")) continue;
        char Folded[2049]; strcpy(Folded,MdoAccountText(Row,"path",2048u));
        for (char* p=Folded;*p;p++) if (*p>='A' && *p<='Z') *p=(char)(*p+'a'-'A');
        char* Key=Folded; char** Found=bsearch(&Key,Names,Count,sizeof(*Names),MdoDtPathCompare);
        size_t Length=strlen(Folded);
        if (Found && Found+1<Names+Count && !strncmp(Found[1],Folded,Length) && Found[1][Length]=='/') goto done;
    }
    Ok=Total==Declared && (strcmp(Kind,"session") || (Count==1u && Sessions==1u));
done:
    for (size_t i=0;i<Count;i++) xrtFree(Names[i]);
    xrtFree(Names); return Ok;
}
static uint64 MdoDtFileSize(cstr Path) {
    bool Exists=false; xfileinfo Info;
    return MdoHomeExternalStat(Path,&Exists,&Info) && Exists && Info.Type==XFILE_TYPE_FILE?Info.Size:0u;
}
static bool MdoDtIncomingCurrent(xvalue* Task,const xvalue* Manifest) {
    uint64 Index=0; char Path[256],Leaf[40]; bool Changed=false;
    const xvalue* Files=xrtValueObjectGet(Manifest,XRT_STR_LITERAL("files"));
    (void)MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("next_file")),&Index);
    while (Index<xrtValueCount(Files)) {
        const xvalue* Row=xrtValueArrayGet(Files,(size_t)Index); cstr Type=MdoAccountText(Row,"type",16u);
        uint64 EmptySize=1u;
        (void)MdoAccountGetUInt(xrtValueObjectGet(Row,XRT_STR_LITERAL("bytes")),&EmptySize);
        if (Type && !strcmp(Type,"directory")) { Index++; Changed=true; continue; }
        snprintf(Leaf,sizeof(Leaf),"files/%08u",(unsigned)Index);
        MdoDtTaskPath(Path,true,MdoAccountText(Task,"id",32u),Leaf);
        if (EmptySize==0u && !MdoDtWrite(Path,"",0u)) return false;
        bool Exists=false;
        if (!MdoHomeExternalStat(Path,&Exists,NULL)) return false;
        if (!Exists) break;
        Index++; Changed=true;
    }
    if (Changed && (!MdoAccountSetUInt(Task,"next_file",Index) || !MdoDtTaskSave(true,Task))) return false;
    snprintf(Leaf,sizeof(Leaf),"files/%08u.part",(unsigned)Index);
    MdoDtTaskPath(Path,true,MdoAccountText(Task,"id",32u),Leaf);
    return MdoAccountSetUInt(Task,"offset",MdoDtFileSize(Path));
}
static bool MdoDtReceiveChunk(MdoApiContext* Context,cstr Path,uint64 Offset,uint64 Total,cstr ExpectedHash,uint16* HttpStatus,cstr* Code) {
    char* Data=NULL; size_t Bytes=0; MdoApiBodyStatus Status=MdoApiBinaryBodyRead(Context,MDO_DT_CHUNK,&Data,&Bytes);
    *HttpStatus=400u; *Code="transfer_chunk_invalid";
    if (Status!=MDO_API_BODY_OK && !(Status==MDO_API_BODY_MISSING && Offset==Total)) return false;
    xfile File=NULL; bool Ok=false; size_t Written=0;
    *HttpStatus=409u; *Code="transfer_offset_conflict";
    if (Offset>Total || Bytes>Total-Offset || MdoDtFileSize(Path)!=Offset) goto done;
    MdoHomeSnapshot Home={0}; Home.Size=sizeof(Home);
    *HttpStatus=507u; *Code="transfer_space_unavailable";
    if (!MdoHomeGetSnapshot(&Home) || !MdoDtDiskSpace(Home.Path,Bytes)) goto done;
    *HttpStatus=500u; *Code="transfer_storage_error";
    File=MdoHomeOpenWrite(Path,XFILE_READ|XFILE_WRITE|XFILE_CREATE);
    if (!File || !xrtSeek(File,(int64)Offset,XSEEK_START,NULL) || (Bytes && (!xrtWrite(File,Data,Bytes,&Written) || Written!=Bytes)) || !xrtFlush(File)) goto done;
    if (ExpectedHash && Offset+Bytes==Total) {
        char Hash[65]; uint64 Size=0;
        *HttpStatus=422u; *Code="transfer_checksum_mismatch";
        if (!MdoDtHash(File,Hash,&Size,NULL) || Size!=Total || strcmp(Hash,ExpectedHash)) goto done;
    }
    *HttpStatus=500u; *Code="transfer_storage_error";
    Ok=true;
done:
    xrtClose(File); xrtFree(Data);
    if (!Ok && *HttpStatus==422u) (void)MdoHomeRemove(Path,false);
    return Ok;
}
static bool MdoDtSealPayload(cstr Source,cstr Target,uint16* Status,cstr* Code) {
    if (MdoHomeRenameNoReplace(Source,Target)) return true;
    xerror* Error=xrtTakeError(); xerrkind Kind=xrtErrorKind(Error);
    *Status=Kind==XERR_UNSUPPORTED?422u:Kind==XERR_PERMISSION?403u:500u;
    *Code=Kind==XERR_UNSUPPORTED?"transfer_storage_unsupported":"transfer_storage_error";
    xrtErrorFree(Error); return false;
}
static bool MdoDtAdmissionStorage(cstr Id,cstr Directory) {
    char Source[256],Target[256]; bool Ok=false; xfile File=NULL;
    MdoDtTaskPath(Source,true,Id,"admission.part"); MdoDtTaskPath(Target,true,Id,"admission.ready");
    File=MdoHomeOpenWrite(Source,XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE);
    if (!File) return false;
    bool Closed=xrtClose(File);
    if (Closed && MdoHomeRenameNoReplace(Source,Target)) { (void)MdoHomeRemove(Target,false); Ok=true; }
    else { xerror* Error=xrtTakeError(); (void)MdoHomeRemove(Source,false); xrtSetErrorTake(Error); return false; }
    if (!Directory || !*Directory) return Ok;
    xroot Root=xrtRootOpen(Directory); xfileoptions Options; xrtFileOptionsInit(&Options);
    Options.Flags=XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE|XFILE_NOFOLLOW; Options.Mode=0600u;
    snprintf(Source,sizeof(Source),".mdo-transfer-%s-probe.part",Id); snprintf(Target,sizeof(Target),".mdo-transfer-%s-probe.ready",Id);
    File=Root?xrtRootFileOpen(Root,Source,&Options):NULL;
    if (File) {
        Closed=xrtClose(File); Ok=Closed && xsRootRenameNoReplace(Root,Source,Target);
        xerror* Error=Ok?NULL:xrtTakeError(); (void)xrtRootRemove(Root,Ok?Target:Source); if (Error) xrtSetErrorTake(Error);
    } else Ok=false;
    xrtRootClose(Root); return Ok;
}
static bool MdoDtIncomingBegin(MdoApiContext* Context,cstr Id) {
    MdoApiJsonBody Body; MdoApiBodyStatus Status=MdoApiJsonBodyRead(Context,&Body);
    if (Status!=MDO_API_BODY_OK) return MdoApiReplyBodyError(Context,Status);
    uint64 Member=0,Bytes=0; char Hash[65]; size_t EncodedBytes=0;
    cstr Digest=MdoAccountText(Body.Value,"manifest_sha256",64u),Kind=MdoAccountText(Body.Value,"kind",16u);
    cstr Target=MdoAccountText(Body.Value,"target_project_id",64u),Directory=MdoAccountText(Body.Value,"target_directory",2048u);
    cstr SourceDevice=MdoAccountText(Body.Value,"source_device",32u);
    str Encoded=xrtJsonStringify(Body.Value,false,&EncodedBytes);
    static const char* const Keys[]={"member_id","manifest_bytes","manifest_sha256","total_bytes","kind","target_project_id","target_directory","source_device"};
    bool Valid=MdoDtKeys(Body.Value,Keys,sizeof(Keys)/sizeof(*Keys)) && xrtValueCount(Body.Value)==8u && Kind &&
        (!strcmp(Kind,"session") || !strcmp(Kind,"project")) && Digest && MdoAccountHex(Digest,64u) &&
        MdoAccountGetUInt(xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("member_id")),&Member) && MdoDtMember(Member) &&
        MdoAccountGetUInt(xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("manifest_bytes")),&Bytes) && Bytes && Bytes<=MDO_DT_MANIFEST &&
        SourceDevice && (!*SourceDevice || MdoDtId(SourceDevice)) && Target && MdoDtPath(Target) && !strchr(Target,'/') && Directory && (!*Directory || xrtPathIsAbs(Directory)) &&
        Encoded && MdoDtHex(Encoded,EncodedBytes,Hash);
    xrtFree(Encoded);
    if (!Valid) { MdoApiJsonBodyUnit(&Body); return MdoApiReplyError(Context,400,"transfer_invalid_offer","Invalid transfer metadata or account",NULL); }
    xvalue* Task=MdoDtTaskRead(true,Id);
    if (Task) {
        cstr Existing=MdoAccountText(Task,"offer_sha256",64u);
        bool Same=Existing && !strcmp(Existing,Hash);
        if (Same) {
            cstr State=MdoAccountText(Task,"state",32u);
            if (State && strcmp(State,"completed") && strcmp(State,"cancelled")) Same=MdoDtQueueChange(true,Id,true);
        }
        xrtValueRelease(Task); MdoApiJsonBodyUnit(&Body);
        return Same?MdoApiReplySuccessTake(Context,200u,MdoDtTaskRead(true,Id),NULL):
            MdoApiReplyError(Context,409,"transfer_identity_conflict","This transfer ID belongs to different data",NULL);
    }
    xvalue* Queue=MdoDtQueue(true); bool Full=xrtValueCount(Queue)>=MDO_DT_TASKS; xrtValueRelease(Queue);
    if (Full) { MdoApiJsonBodyUnit(&Body); return MdoApiReplyError(Context,409,"transfer_queue_full","Finish or cancel an existing incoming transfer before starting another",NULL); }
    Task=xrtValueClone(Body.Value);
    char ReceiverId[33];
    if (!MdoDtReceiverId(ReceiverId) || !MdoAccountSetString(Task,"receiver_id",ReceiverId)) {
        xrtValueRelease(Task); MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context,503,"transfer_identity_unavailable","Receiver storage identity is unavailable",NULL);
    }
    uint64 TotalBytes=0; MdoHomeSnapshot Home={0}; Home.Size=sizeof(Home);
    if (*Directory) {
        xroot Parent=xrtRootOpen(Directory); xfileinfo Info;
        bool Available=Parent && xrtRootStat(Parent,".",false,&Info) && Info.Type==XFILE_TYPE_DIRECTORY;
        xrtRootClose(Parent);
        if (!Available) {
            xrtValueRelease(Task); MdoApiJsonBodyUnit(&Body);
            return MdoApiReplyError(Context,422,"transfer_target_path_unavailable","Destination parent folder does not exist or cannot be accessed",NULL);
        }
    }
    if (!MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("total_bytes")),&TotalBytes) || TotalBytes>UINT64_C(9007199254740991) ||
        !MdoHomeGetSnapshot(&Home) || !MdoDtDiskSpace(Home.Path,TotalBytes>UINT64_MAX/2u?UINT64_MAX:TotalBytes*2u) ||
        (*Directory && !MdoDtDiskSpace(Directory,TotalBytes))) {
        xrtValueRelease(Task); MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context,507,"transfer_space_unavailable","Not enough free space in the destination Home or project folder",NULL);
    }
    if (!MdoDtAdmissionStorage(Id,!strcmp(Kind,"project")?Directory:NULL)) {
        xerror* Error=xrtTakeError(); bool Unsupported=xrtErrorKind(Error)==XERR_UNSUPPORTED; xrtErrorFree(Error);
        xrtValueRelease(Task); MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context,422,"transfer_storage_unavailable",Unsupported?
            "Destination filesystem does not support atomic exclusive publication; choose a native filesystem for Home and the project folder":
            "Destination storage cannot create and exclusively publish files; check access permissions and available space",NULL);
    }
    if (!strcmp(Kind,"session")) {
        MdoProjectBinding Binding={0}; Binding.Size=sizeof(Binding); xwork_error Error;
        if (!MdoProjectBindingPrepare(Target,&Binding,&Error) ||
            !MdoAccountSetUInt(Task,"target_project_revision",Binding.Revision) ||
            !MdoApiValueSetInt(Task,"target_project_created_at",Binding.CreatedAt) ||
            !MdoAccountSetString(Task,"target_workspace",Binding.WorkspaceRoot) ||
            !MdoAccountSetUInt(Task,"target_workspace_device",Binding.WorkspaceIdentity.Device) ||
            !MdoAccountSetUInt(Task,"target_workspace_identity",Binding.WorkspaceIdentity.Identity)) {
            xrtValueRelease(Task); MdoApiJsonBodyUnit(&Body);
            return MdoApiReplyError(Context,409,"transfer_target_changed","Target project is unavailable; select it again",NULL);
        }
    }
    char NewProject[65]; snprintf(NewProject,sizeof(NewProject),"received-%s",Id);
    Valid=Task && MdoApiValueSetInt(Task,"created_at",MdoUtcUs()) &&
        MdoAccountSetString(Task,"id",Id) && MdoAccountSetString(Task,"offer_sha256",Hash) &&
        MdoAccountSetString(Task,"destination_project_id",!strcmp(Kind,"project")?NewProject:Target) &&
        MdoAccountSetUInt(Task,"next_file",0u) && MdoDtTaskState(Task,"receiving_manifest","") && MdoDtTaskSave(true,Task) && MdoDtQueueChange(true,Id,true);
    xrtValueRelease(Task); MdoApiJsonBodyUnit(&Body);
    return Valid?MdoApiReplySuccessTake(Context,201u,MdoDtTaskRead(true,Id),NULL):
        MdoApiReplyError(Context,503,"transfer_storage_unavailable","Unable to persist transfer admission",NULL);
}
bool MdoDtIncomingRoute(MdoApiContext* Context) {
    char Id[33],Path[256],Leaf[64];
    uint16 ReplyStatus=409u; cstr ReplyCode="transfer_state_conflict";
    if (Context->ParamCount<1u || Context->Params[0].Size!=32u) return MdoApiReplyError(Context,400,"transfer_invalid_id","Invalid transfer ID",NULL);
    memcpy(Id,Context->Params[0].Data,32); Id[32]=0;
    if (!MdoDtId(Id)) return MdoApiReplyError(Context,400,"transfer_invalid_id","Invalid transfer ID",NULL);
    bool Read=Context->Request->head->MethodCode==XHTTP_METHOD_GET;
    if (!Read && !MdoDtReceiveAllowed()) return MdoApiReplyError(Context,403,"transfer_receive_disabled","Enable receiving on the target device",NULL);
    if (!Read && Context->ParamCount==1u && Context->Request->head->MethodCode==XHTTP_METHOD_POST) return MdoDtIncomingBegin(Context,Id);
    xvalue* Task=MdoDtTaskRead(true,Id); uint64 Member=0;
    if (!Task) return MdoApiReplyError(Context,404,"transfer_not_found","Transfer not found",NULL);
    MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("member_id")),&Member);
    if (!MdoDtMember(Member)) { xrtValueRelease(Task); return MdoApiReplyError(Context,403,"transfer_account_changed","Transfer belongs to another account",NULL); }
    cstr State=MdoAccountText(Task,"state",32u);
    if (!State) { xrtValueRelease(Task); return false; }
    bool Receiving=!strcmp(State,"receiving_manifest") || !strcmp(State,"receiving_files");
    MdoDtTaskPath(Path,true,Id,"manifest.json");
    if (strcmp(g_MdoDt.IncomingManifestId,Id)) {
        xrtValueRelease(g_MdoDt.IncomingManifest); g_MdoDt.IncomingManifest=NULL;
        strcpy(g_MdoDt.IncomingManifestId,Id);
    }
    if (!g_MdoDt.IncomingManifest) g_MdoDt.IncomingManifest=MdoDtRead(Path,MDO_DT_MANIFEST);
    xvalue* Manifest=xrtValueRetain(g_MdoDt.IncomingManifest);
    if (Manifest && !strcmp(State,"receiving_manifest")) {
        MdoDtTaskState(Task,"receiving_files",""); MdoDtTaskSave(true,Task); State=MdoAccountText(Task,"state",32u);
    }
    if (Read) {
        if (Manifest && Receiving) (void)MdoDtIncomingCurrent(Task,Manifest);
        MdoDtTaskPath(Path,true,Id,"manifest.part"); MdoAccountSetUInt(Task,"manifest_offset",MdoDtFileSize(Path));
        xrtValueRelease(Manifest); return MdoApiReplySuccessTake(Context,200u,Task,NULL);
    }
    if (Context->ParamCount==2u) {
        xstrview Command=Context->Params[1];
        bool Resume=Command.Size==6u && !memcmp(Command.Data,"resume",6u);
        bool Cancel=Command.Size==6u && !memcmp(Command.Data,"cancel",6u);
        bool CommitStarted=!strcmp(State,"committing") || !strcmp(State,"committed") || !strcmp(State,"completed") ||
            !strcmp(g_MdoDt.ActiveIncoming,Id);
        bool Published=false; (void)xrtValueGetBool(xrtValueObjectGet(Task,XRT_STR_LITERAL("commit_started")),&Published);
        if (Resume && !strcmp(State,"paused") && !CommitStarted) {
            MdoDtTaskState(Task,Manifest?"queued_commit":"receiving_manifest","");
            bool Saved=MdoDtTaskSave(true,Task) && MdoDtQueueChange(true,Id,true); xrtValueRelease(Manifest);
            if (Saved) return MdoApiReplySuccessTake(Context,202,Task,NULL);
            xrtValueRelease(Task); return MdoApiReplyError(Context,503,"transfer_storage_unavailable","Unable to save transfer state",NULL);
        }
        if (Cancel && !CommitStarted && !Published) {
            MdoDtTaskState(Task,"cancelled",""); MdoDtTaskSave(true,Task); xrtValueRelease(Manifest);
            (void)MdoDtQueueChange(true,Id,true);
            return MdoApiReplySuccessTake(Context,200,Task,NULL);
        }
        if (Cancel && (CommitStarted || Published) && strcmp(State,"completed")) {
            xrtValueRelease(Manifest); xrtValueRelease(Task);
            return MdoApiReplyError(Context,409,"transfer_commit_in_progress","Wait for the target commit result before removing its data",NULL);
        }
    }
    if (!Receiving) {
        xrtValueRelease(Manifest); xrtValueRelease(Task);
        return MdoApiReplySuccessTake(Context,200,MdoDtTaskRead(true,Id),NULL);
    }
    if (Context->ParamCount<2u) goto invalid;
    xstrview Action=Context->Params[1]; bool Ok=false;
    if (Action.Size==8u && !memcmp(Action.Data,"manifest",8u) && Context->ParamCount==3u) {
        uint64 Offset=0,Bytes=0;
        if (!MdoDtUnsigned(Context->Params[2],UINT64_MAX,&Offset)) goto invalid;
        MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("manifest_bytes")),&Bytes);
        MdoDtTaskPath(Path,true,Id,"manifest.part");
        Ok=MdoDtReceiveChunk(Context,Path,Offset,Bytes,MdoAccountText(Task,"manifest_sha256",64u),&ReplyStatus,&ReplyCode);
        if (!Ok) goto invalid;
        if (MdoDtFileSize(Path)==Bytes) {
            xvalue* Parsed=MdoDtRead(Path,MDO_DT_MANIFEST);
            uint64 Declared=0,Expected=0;
            MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("total_bytes")),&Expected);
            cstr ParsedKind=MdoAccountText(Parsed,"kind",16u),OfferedKind=MdoAccountText(Task,"kind",16u);
            if (MdoDtManifestValid(Parsed) && ParsedKind && OfferedKind && !strcmp(ParsedKind,OfferedKind) &&
                MdoAccountGetUInt(xrtValueObjectGet(Parsed,XRT_STR_LITERAL("total_bytes")),&Declared) && Declared==Expected) {
                char TargetPath[256]; MdoDtTaskPath(TargetPath,true,Id,"manifest.json");
                if (MdoDtSealPayload(Path,TargetPath,&ReplyStatus,&ReplyCode)) {
                    MdoDtTaskState(Task,"receiving_files",""); MdoDtTaskSave(true,Task);
                    xrtValueRelease(g_MdoDt.IncomingManifest); g_MdoDt.IncomingManifest=xrtValueRetain(Parsed);
                } else Ok=false;
            } else { Ok=false; ReplyStatus=422u; ReplyCode="transfer_manifest_invalid"; }
            xrtValueRelease(Parsed);
        }
    } else if (Action.Size==5u && !memcmp(Action.Data,"files",5u) && Context->ParamCount==4u && Manifest) {
        uint64 Index=0,Current=0,Offset=0,Bytes=0;
        if (!MdoDtUnsigned(Context->Params[2],MDO_DT_FILES,&Index) || !MdoDtUnsigned(Context->Params[3],UINT64_MAX,&Offset) ||
            !MdoDtIncomingCurrent(Task,Manifest)) goto invalid;
        MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("next_file")),&Current);
        if (Index!=Current) goto invalid;
        const xvalue* Row=xrtValueArrayGet(xrtValueObjectGet(Manifest,XRT_STR_LITERAL("files")),(size_t)Index);
        cstr Hash=MdoAccountText(Row,"sha256",64u);
        if (!Hash || !MdoAccountGetUInt(xrtValueObjectGet(Row,XRT_STR_LITERAL("bytes")),&Bytes)) goto invalid;
        snprintf(Leaf,sizeof(Leaf),"files/%08u.part",(unsigned)Index); MdoDtTaskPath(Path,true,Id,Leaf);
        Ok=MdoDtReceiveChunk(Context,Path,Offset,Bytes,NULL,&ReplyStatus,&ReplyCode);
        if (!Ok) goto invalid;
        if (MdoDtFileSize(Path)==Bytes) {
            char TargetPath[256]; snprintf(Leaf,sizeof(Leaf),"files/%08u",(unsigned)Index); MdoDtTaskPath(TargetPath,true,Id,Leaf);
            if (MdoDtSealPayload(Path,TargetPath,&ReplyStatus,&ReplyCode)) {
                MdoAccountSetUInt(Task,"next_file",Index+1u); MdoDtTaskSave(true,Task);
            } else Ok=false;
        }
    } else if (Action.Size==6u && !memcmp(Action.Data,"commit",6u) && Manifest && MdoDtIncomingCurrent(Task,Manifest)) {
        uint64 Index=0; MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("next_file")),&Index);
        if (Index!=xrtValueCount(xrtValueObjectGet(Manifest,XRT_STR_LITERAL("files")))) goto invalid;
        MdoDtTaskState(Task,"queued_commit","");
        if (!MdoDtTaskSave(true,Task) || !MdoDtQueueChange(true,Id,true)) goto invalid;
        Ok=true;
    } else goto invalid;
    if (!Ok) goto invalid;
    MdoApiValueSetInt(Task,"updated_at",MdoUtcUs()); MdoDtTaskSave(true,Task);
    xrtValueRelease(Manifest);
    return MdoApiReplySuccessTake(Context,200u,Task,NULL);
invalid:
    if (ReplyStatus==422u) {
        char Partial[256]; MdoDtTaskPath(Partial,true,Id,"manifest.part");
        (void)MdoHomeRemove(Partial,false);
    }
    xrtValueRelease(Manifest); xrtValueRelease(Task);
    return MdoApiReplyError(Context,ReplyStatus,ReplyCode,
        ReplyStatus==507u?"Destination disk space is insufficient; free space and resume this transfer":
        !strcmp(ReplyCode,"transfer_storage_unsupported")?"This filesystem does not support atomic exclusive publication; use a native filesystem for the destination Home":
        ReplyStatus==422u?"Transfer checksum or manifest validation failed; data has not been published":
        ReplyStatus==500u?"Destination could not write the transfer data; check filesystem permissions and health":
        "Query the current transfer state and offset before continuing",NULL);
}
