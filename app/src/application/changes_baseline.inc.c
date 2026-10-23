/* Local-only, immutable session-start snapshots. Git receives raw blobs through
 * fast-import in a private bare repository: no user index, attributes, filters,
 * hooks, checkout, credential helper or user object database is used for writes. */
#define GIT_SNAPSHOT_BYTES (64u*1024u*1024u)
#define GIT_SNAPSHOT_FILE_BYTES (16u*1024u*1024u)
#define GIT_SNAPSHOT_BUFFER (GIT_SNAPSHOT_BYTES+GIT_METADATA_LIMIT*2u)
#define GIT_SNAPSHOT_STORE_KIB (128u*1024u)
static xmutex* SdkGitBaselineLock;
bool MdoApplicationChangesInit(void){SdkGitBaselineLock=xrtMutexCreate();return SdkGitBaselineLock!=NULL;}
void MdoApplicationChangesUnit(void){if(SdkGitBaselineLock)xrtMutexDestroy(SdkGitBaselineLock);SdkGitBaselineLock=NULL;}
static bool SdkGitBaselineEnter(const mdo_sdk_context_v1* C,mdo_error_v1* E){
    if(!SdkGitBaselineLock)return SdkGitFail(E,"git_baseline_unavailable","Baseline service is unavailable",503);
    while(!xrtMutexTryLock(SdkGitBaselineLock)){
        if(SdkCancelled(C))return SdkGitFail(E,"git_snapshot_cancelled","Baseline inspection was cancelled or exceeded its deadline",499);
        xrtSleep(10);
    }return true;
}
typedef struct SdkGitBuffer {char* Data;size_t Size,Capacity;} SdkGitBuffer;
static bool SdkGitAppend(SdkGitBuffer* B,const void* Data,size_t Bytes){
    if(Bytes>GIT_SNAPSHOT_BUFFER-B->Size)return false;
    size_t Need=B->Size+Bytes;if(Need>B->Capacity){size_t N=B->Capacity?B->Capacity:8192;while(N<Need&&N<GIT_SNAPSHOT_BUFFER)N=N>GIT_SNAPSHOT_BUFFER/2?GIT_SNAPSHOT_BUFFER:N*2;
        char* P=xrtRealloc(B->Data,N);if(!P)return false;B->Data=P;B->Capacity=N;}
    if(Bytes)memcpy(B->Data+B->Size,Data,Bytes);B->Size=Need;return true;
}
static bool SdkGitAppendText(SdkGitBuffer* B,cstr S){return SdkGitAppend(B,S,strlen(S));}
static bool SdkGitQuoted(SdkGitBuffer* B,cstr Path){
    if(!SdkGitAppendText(B,"\""))return false;
    for(size_t i=0;Path[i];i++){unsigned char C=(unsigned char)Path[i];char Escaped[5];
        if(C=='"'||C=='\\'||C<32||C==127){snprintf(Escaped,sizeof(Escaped),"\\%03o",C);if(!SdkGitAppendText(B,Escaped))return false;}
        else if(!SdkGitAppend(B,&C,1))return false;
    }return SdkGitAppendText(B,"\"");
}
static bool SdkGitSnapshotPaths(const mdo_sdk_context_v1* C,cstr Root,xprocessresult* R,mdo_error_v1* E){
    const cstr Args[]={"ls-files","--cached","--others","--exclude-standard","-z"};
    return SdkGitRun(C,Root,Args,5,(xbytesview){0},GIT_METADATA_LIMIT,NULL,NULL,R,E)&&SdkGitSuccess(R,E,"git_snapshot_failed");
}
static bool SdkGitStable(const xfileinfo* A,const xfileinfo* B){
    return A->Type==B->Type&&A->Device==B->Device&&A->Identity==B->Identity&&A->Size==B->Size&&A->Modified==B->Modified&&A->Changed==B->Changed&&A->Mode==B->Mode;
}
static char* SdkGitSnapshotRepo(cstr Project){
    char P[384];if(!SdkIdValid(Project)||snprintf(P,sizeof(P),"workbench-baselines/%s/repository",Project)>=(int)sizeof(P))return NULL;
    return MdoHomeExternalPath(P);
}
static bool SdkGitSnapshotStore(const mdo_sdk_context_v1* C,cstr Repo,mdo_error_v1* E){
    char Relative[384];snprintf(Relative,sizeof(Relative),"workbench-baselines/%s/repository",C->ProjectId);
    xroot Storage=MdoHomeOpenStorageDirectory(Relative);if(!Storage)return SdkNativeFailure(E,"git_snapshot");
    xfileinfo Info={0};bool Exists=xrtRootStat(Storage,"HEAD",false,&Info);xrtRootClose(Storage);
    if(!Exists){if(!xrtGetError()||xrtErrorKind(xrtGetError())!=XERR_NOT_FOUND)return SdkNativeFailure(E,"git_snapshot");xrtClearError();}
    if(!Exists){
        const cstr Init[]={"init","--bare","--template=",Repo};xprocessresult Result={0};
        bool Ok=SdkGitRun(C,C->Workspace,Init,4,(xbytesview){0},4096,NULL,NULL,&Result,E)&&SdkGitSuccess(&Result,E,"git_snapshot_failed");xrtProcessResultUnit(&Result);if(!Ok)return false;
    }else if(Info.Type!=XFILE_TYPE_FILE)return SdkGitFail(E,"git_baseline_corrupt","Private baseline repository metadata has an unexpected type",503);
    const cstr Args[]={"count-objects","-v"};xprocessresult Result={0};
    bool Ok=SdkGitRun(C,Repo,Args,2,(xbytesview){0},4096,NULL,NULL,&Result,E)&&SdkGitSuccess(&Result,E,"git_snapshot_failed");
    uint64 Size=0;
    if(Ok){char* Text=xrtStrDupN((cstr)Result.Stdout,Result.StdoutSize);if(!Text)Ok=SdkNativeFailure(E,"git_snapshot");
        for(char* Line=Text;Line&&*Line;){uint64 N=!strncmp(Line,"size: ",6)?SdkGitNumber(Line+6):!strncmp(Line,"size-pack: ",11)?SdkGitNumber(Line+11):0;Size=N>UINT64_MAX-Size?UINT64_MAX:Size+N;char* Next=strchr(Line,'\n');Line=Next?Next+1:NULL;}xrtFree(Text);
        if(Size>GIT_SNAPSHOT_STORE_KIB)Ok=SdkGitFail(E,"git_snapshot_store_limit","Private project baseline store reached 128 MiB; no new snapshot was admitted",413);
    }xrtProcessResultUnit(&Result);return Ok;
}
static char* SdkGitSnapshotLink(const mdo_sdk_context_v1* C,cstr Root,cstr Path,mdo_error_v1* E){
    char* Directory=xrtPathJoin(Root,Path);const cstr PrefixArgs[]={"rev-parse","--show-prefix"},HeadArgs[]={"rev-parse","--verify","HEAD"};
    char* Prefix=Directory?SdkGitText(C,Directory,PrefixArgs,2,false,E):NULL;
    char* Commit=Prefix&&!Prefix[0]?SdkGitText(C,Directory,HeadArgs,3,false,E):NULL;xrtFree(Prefix);xrtFree(Directory);
    if(!SdkGitOid(Commit)){xrtFree(Commit);SdkGitFail(E,"git_snapshot_entry_unsupported","A nested repository has no readable root commit; no partial baseline was published",422);return NULL;}return Commit;
}
/* The object database has no automatic GC. A snapshot admission above 128 MiB
 * is refused; a single admitted raw input is <=64 MiB plus bounded metadata.
 * Existing snapshots remain readable when no further writes can be admitted. */
static char* SdkGitSnapshot(const mdo_sdk_context_v1* C,cstr Root,cstr Repo,cstr Ref,mdo_error_v1* E){
    xprocessresult Names={0},AfterNames={0},Result={0};SdkGitBuffer Blobs={0},Files={0};xvalue* Seen=xrtValueObject(),*Links=xrtValueObject();xroot Anchor=NULL;
    xfileinfo* Identities=NULL;char** Paths=NULL;size_t Count=0;uint64 ContentBytes=0;char* Tree=NULL;bool Ok=false;
    if(!SdkGitSnapshotPaths(C,Root,&Names,E)||!SdkGitSnapshotStore(C,Repo,E))goto done;
    Anchor=xrtRootOpen(Root);Identities=xrtCalloc(GIT_FILE_LIMIT,sizeof(*Identities));Paths=xrtCalloc(GIT_FILE_LIMIT,sizeof(*Paths));
    if(!Anchor||!Identities||!Paths||!Seen||!Links)goto native;
    size_t At=0;while(At<Names.StdoutSize){
        cstr Input=SdkGitToken(Names.Stdout,Names.StdoutSize,&At);if(!Input||!SdkGitPath(Input))goto unsupported;
        char* Path=xrtStrDup(Input);if(!Path)goto native;size_t Length=strlen(Path);if(Length&&Path[Length-1]=='/')Path[Length-1]=0;
        if(SdkGet(Seen,Path)){xrtFree(Path);continue;}if(Count>=GIT_FILE_LIMIT){xrtFree(Path);goto limit;}
        Paths[Count]=Path;xfileinfo* Before=&Identities[Count++];if(!xrtValueObjectSetNew(Seen,xrtStrView(Path),xrtValueBool(true)))goto native;
        if(SdkCancelled(C))goto cancelled;
        if(!xrtRootStat(Anchor,Path,false,Before)){if(xrtGetError()&&xrtErrorKind(xrtGetError())==XERR_NOT_FOUND){xrtClearError();continue;}goto native;}
        unsigned Mode=100644;char Header[256];unsigned char* Data=NULL;size_t Bytes=0;xfile File=NULL;xfileinfo After={0};char* Link=NULL,*Commit=NULL;
        if(Before->Type==XFILE_TYPE_FILE){
            if(Before->Size>GIT_SNAPSHOT_FILE_BYTES||Before->Size>GIT_SNAPSHOT_BYTES-ContentBytes)goto limit;
            xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_NOFOLLOW;
            File=xrtRootFileOpen(Anchor,Path,&Options);Bytes=(size_t)Before->Size;Data=xrtMalloc(Bytes?Bytes:1);
            bool Read=File&&Data&&xrtReadFull(File,Data,Bytes,NULL)&&xrtFileStat(File,&After);if(File&&!xrtClose(File))Read=false;
            if(!Read){xrtFree(Data);goto native;}if(!SdkGitStable(Before,&After)){xrtFree(Data);goto changed;}
#if !defined(_WIN32) && !defined(_WIN64)
            if(Before->Mode&0111u)Mode=100755;
#endif
        }else if(Before->Type==XFILE_TYPE_LINK){
            Link=xrtRootLinkRead(Anchor,Path);if(!Link)goto native;Bytes=strlen(Link);Data=(unsigned char*)Link;Mode=120000;
        }else if(Before->Type==XFILE_TYPE_DIRECTORY){
            Commit=SdkGitSnapshotLink(C,Root,Path,E);if(!Commit)goto done;
            if(!SdkPut(Links,Path,Commit)){xrtFree(Commit);goto native;}Mode=160000;
        }else goto unsupported;
        bool Fits=true;
        if(!Commit){
            snprintf(Header,sizeof(Header),"blob\nmark :%u\ndata %zu\n",(unsigned)Count,Bytes);
            Fits=Bytes<=GIT_SNAPSHOT_BYTES-ContentBytes&&SdkGitAppendText(&Blobs,Header)&&SdkGitAppend(&Blobs,Data,Bytes)&&SdkGitAppendText(&Blobs,"\n");
            ContentBytes+=Bytes;snprintf(Header,sizeof(Header),"M %u :%u ",Mode,(unsigned)Count);
        }else snprintf(Header,sizeof(Header),"M %u %s ",Mode,Commit);
        xrtFree(Data);xrtFree(Commit);
        if(!Fits||!SdkGitAppendText(&Files,Header)||!SdkGitQuoted(&Files,Path)||!SdkGitAppendText(&Files,"\n"))goto limit;
    }
    /* Validate the complete inventory again, including tracked deletions and
     * entry names. This detects observed concurrent changes without claiming
     * to lock external editors or provide an OS filesystem transaction. */
    if(!SdkGitSnapshotPaths(C,Root,&AfterNames,E))goto done;
    if(Names.StdoutSize!=AfterNames.StdoutSize||(Names.StdoutSize&&memcmp(Names.Stdout,AfterNames.Stdout,Names.StdoutSize)))goto changed;
    for(size_t i=0;i<Count;i++){
        if(SdkCancelled(C))goto cancelled;xfileinfo After={0};bool Exists=xrtRootStat(Anchor,Paths[i],false,&After);
        if(!Exists&&xrtGetError()&&xrtErrorKind(xrtGetError())==XERR_NOT_FOUND){xrtClearError();if(Identities[i].Type==XFILE_TYPE_NONE)continue;goto changed;}
        if(!Exists)goto native;if(!SdkGitStable(&Identities[i],&After))goto changed;
        cstr Original=SdkField(Links,Paths[i]);if(Original){char* Commit=SdkGitSnapshotLink(C,Root,Paths[i],E);bool Same=Commit&&!strcmp(Original,Commit);xrtFree(Commit);if(!Same)goto changed;}
    }
    char Header[768];snprintf(Header,sizeof(Header),"reset %s\n\ncommit %s\ncommitter mdo <baseline@localhost> %lld +0000\ndata 0\n\ndeleteall\n",Ref,Ref,(long long)(MdoUtcUs()/1000000));
    if(!SdkGitAppendText(&Blobs,Header)||!SdkGitAppend(&Blobs,Files.Data,Files.Size)||!SdkGitAppendText(&Blobs,"\ndone\n"))goto limit;
    const cstr Import[]={"fast-import","--quiet","--done","--max-pack-size=67108864"};
    if(!SdkGitRun(C,Repo,Import,4,(xbytesview){(const unsigned char*)Blobs.Data,Blobs.Size},4096,NULL,NULL,&Result,E)||!SdkGitSuccess(&Result,E,"git_snapshot_failed"))goto done;
    char Revision[320];snprintf(Revision,sizeof(Revision),"%s^{tree}",Ref);const cstr Resolve[]={"rev-parse","--verify",Revision};
    Tree=SdkGitText(C,Repo,Resolve,3,false,E);if(!SdkGitOid(Tree)){xrtFree(Tree);Tree=NULL;goto done;}Ok=true;goto done;
cancelled:SdkGitFail(E,"git_snapshot_cancelled","Baseline inspection was cancelled or exceeded its deadline",499);goto done;
changed:SdkGitFail(E,"git_snapshot_changed","Workspace changed while the baseline was captured; no stable comparison was published",409);goto done;
unsupported:SdkGitFail(E,"git_snapshot_entry_unsupported","Workspace contains an unreadable Git entry or unsupported path; no partial baseline was published",422);goto done;
limit:SdkGitFail(E,"git_snapshot_limit","Baseline exceeds 20,000 entries, 16 MiB per file or 64 MiB of content",413);goto done;
native:if(!E->Code[0])SdkNativeFailure(E,"git_snapshot");
done:
    if(Anchor)xrtRootClose(Anchor);for(size_t i=0;i<Count;i++)xrtFree(Paths?Paths[i]:NULL);xrtFree(Paths);xrtFree(Identities);xrtValueRelease(Seen);xrtValueRelease(Links);
    if(Blobs.Data)xrtSecureZero(Blobs.Data,Blobs.Size);xrtFree(Blobs.Data);xrtFree(Files.Data);xrtProcessResultUnit(&Names);xrtProcessResultUnit(&AfterNames);xrtProcessResultUnit(&Result);
    return Ok?Tree:NULL;
}
static bool SdkGitBaselinePath(cstr Project,cstr Session,char Out[512]){
    return SdkIdValid(Project)&&SdkIdValid(Session)&&snprintf(Out,512,"workbench-baselines/%s/sessions/%s.json",Project,Session)<512;
}
static xvalue* SdkGitBaselineRead(const mdo_sdk_context_v1* C,mdo_error_v1* E){
    char Path[512];if(!SdkGitBaselinePath(C->ProjectId,C->SessionId,Path))return SdkGitFail(E,"git_baseline_missing","Select a session that captured a workspace baseline at creation",409),NULL;
    bool Missing=false;char* Text=MdoExtensionRead(Path,true,16384,&Missing);xvalue* V=SdkParse(Text);xrtFree(Text);
    if(!V){SdkGitFail(E,Missing?"git_baseline_missing":"git_baseline_corrupt",Missing?"This session has no captured baseline":"The captured baseline record is unreadable; original data was retained",Missing?409:503);return NULL;}
    uint64 Version=0;bool Available=false;
    if(!SdkUInt(SdkGet(V,"schema_version"),&Version)||Version!=1||!xrtValueGetBool(SdkGet(V,"available"),&Available)||!SdkField(V,"representation")||strcmp(SdkField(V,"representation"),"raw-worktree-v1")||
        !SdkField(V,"project")||strcmp(SdkField(V,"project"),C->ProjectId)||!SdkField(V,"session")||strcmp(SdkField(V,"session"),C->SessionId)||!SdkField(V,"workspace")||!C->Workspace||strcmp(SdkField(V,"workspace"),C->Workspace)){
        xrtValueRelease(V);SdkGitFail(E,"git_baseline_corrupt","Baseline schema or immutable context is invalid; original data was retained",503);return NULL;}
    xwork_error Error;MdoSession* Session=MdoSessionLoad(C->ProjectId,C->SessionId,&Error);MdoSessionInfo Info={0};Info.Size=sizeof(Info);
    bool Valid=Session&&MdoSessionGetInfo(Session,&Info)&&C->Workspace&&!strcmp(Info.WorkspaceRoot,C->Workspace);MdoSessionRelease(Session);
    if(!Valid){xrtValueRelease(V);SdkGitFail(E,"git_baseline_context_changed","The session or its original workspace is unavailable",409);return NULL;}
    return V;
}
void MdoApplicationCaptureChanges(cstr Project,cstr Session,cstr Workspace){
    char Path[512];if(!SdkGitBaselineLock||!SdkGitBaselinePath(Project,Session,Path))return;
    /* A failed new-session transaction must not leave an older attempt's
     * baseline attached if the same requested session ID is retried. */
    if(!Workspace){(void)MdoHomeRemove(Path,false);xrtClearError();return;}
    MdoApplicationInvocation I={0};mdo_sdk_context_v1 C;MdoApplicationContext(&C,&I,"local-client","session-baseline",Project,Session,"",Workspace,MdoDeadlineAfterUs(15000000));
    mdo_error_v1 E={0};xerror* Saved=xrtTakeError();if(!SdkGitBaselineEnter(&C,&E)){if(Saved)xrtSetErrorTake(Saved);return;}
    bool Exists=false;xfileinfo Info={0};if(!MdoHomeExternalStat(Path,&Exists,&Info)||Exists)goto done;
    xvalue* P=xrtValueObject();SdkPut(P,"repository",Workspace);char* Root=SdkGitRoot(&C,P,&E);xrtValueRelease(P);
    char* Repo=SdkGitSnapshotRepo(Project),*Tree=NULL;char Ref[320];snprintf(Ref,sizeof(Ref),"refs/mdo-sessions/%s",Session);
    if(Root&&Repo)Tree=SdkGitSnapshot(&C,Root,Repo,Ref,&E);
    xvalue* V=xrtValueObject();SdkPutUInt(V,"schema_version",1);SdkPut(V,"session",Session);SdkPut(V,"project",Project);SdkPut(V,"workspace",Workspace);SdkPut(V,"repository",Root);SdkPut(V,"tree",Tree);
    SdkPut(V,"representation","raw-worktree-v1");SdkPutUInt(V,"captured_at_us",(uint64)MdoUtcUs());xrtValueObjectSetNew(V,XRT_STR_LITERAL("available"),xrtValueBool(Tree!=NULL));SdkPut(V,"reason",Tree?"":E.Code);SdkPut(V,"message",Tree?"":E.Message);
    char* Json=SdkJson(V);if(Json)(void)MdoHomeAtomicWrite(Path,Json,strlen(Json),false);xrtFree(Json);xrtValueRelease(V);xrtFree(Root);xrtFree(Repo);xrtFree(Tree);
done:xrtMutexUnlock(SdkGitBaselineLock);xrtClearError();if(Saved)xrtSetErrorTake(Saved);
}
