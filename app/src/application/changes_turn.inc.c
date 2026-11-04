/* Immutable per-execution review. Kept outside the user's repository and
 * never placed in model context. Capture failures must not fail Agent work. */
static bool SdkGitTurnPath(const mdo_sdk_context_v1* C,uint64 Event,char Out[512]){
    return Event&&SdkIdValid(C->ProjectId)&&SdkIdValid(C->SessionId)&&
        snprintf(Out,512,"workbench-baselines/%s/sessions/%s/turns/%llu.json",C->ProjectId,C->SessionId,(unsigned long long)Event)<512;
}
static xvalue* SdkGitTurnRead(const mdo_sdk_context_v1* C,const xvalue* P,mdo_error_v1* E){
    uint64 Event=0;char Path[512];
    if(!SdkUInt(SdkGet(P,"turn"),&Event)||!SdkGitTurnPath(C,Event,Path))return SdkGitFail(E,"git_turn_invalid","Select a reply with a stable start event",400),NULL;
    bool Missing=false;char* Text=MdoExtensionRead(Path,true,16384,&Missing);xvalue* V=SdkParse(Text);xrtFree(Text);
    if(!V)return SdkGitFail(E,Missing?"git_turn_missing":"git_turn_corrupt",Missing?"This reply has no completed change receipt":"The reply change receipt is unreadable",Missing?404:503),NULL;
    uint64 Version=0,Recorded=0;
    if(!SdkUInt(SdkGet(V,"schema_version"),&Version)||Version!=1||!SdkUInt(SdkGet(V,"turn"),&Recorded)||Recorded!=Event||
        !SdkField(V,"project")||strcmp(SdkField(V,"project"),C->ProjectId)||!SdkField(V,"session")||strcmp(SdkField(V,"session"),C->SessionId)||
        !MdoPathField(V,"workspace",true)||!MdoPathField(V,"repository",true)||!SdkGitSameDirectory(SdkField(V,"workspace"),C->Workspace)){
        xrtValueRelease(V);return SdkGitFail(E,"git_turn_context_changed","Reply changes do not belong to this conversation and workspace",409),NULL;
    }
    bool Available=false;xrtValueGetBool(SdkGet(V,"available"),&Available);
    if(!Available){
        cstr Reason=SdkField(V,"reason");bool NoRepository=Reason&&!strcmp(Reason,"git_not_repository"),NoGit=Reason&&!strcmp(Reason,"git_unavailable");
        /* Earlier completion retried an empty base/root and overwrote the
         * not-a-repository result. Confirm that case without backfilling or
         * changing its immutable historical comparison. Real Git failures
         * and captures with a repository keep their original failure. */
        cstr Root=SdkField(V,"repository"),Base=SdkField(V,"base");
        if(!NoRepository&&!NoGit&&Reason&&(!strcmp(Reason,"git_revision_invalid")||!strcmp(Reason,"sdk_native_failed"))&&(!Root||!Root[0])&&(!Base||!Base[0])){
            xvalue* P=xrtValueObject();SdkPut(P,"repository",C->Workspace);mdo_error_v1 Check={0};char* Current=SdkGitRoot(C,P,&Check);
            NoRepository=!Current&&!strcmp(Check.Code,"git_not_repository");NoGit=!Current&&!strcmp(Check.Code,"git_unavailable");xrtFree(Current);xrtValueRelease(P);
        }
        SdkGitFail(E,NoRepository?"git_not_repository":NoGit?"git_unavailable":"git_turn_unavailable","File changes could not be captured for this reply",409);xrtValueRelease(V);return NULL;
    }
    if(!SdkGitOid(SdkField(V,"base"))||!SdkGitOid(SdkField(V,"current_tree"))){xrtValueRelease(V);return SdkGitFail(E,"git_turn_corrupt","Reply snapshot identities are invalid",503),NULL;}
    /* Import/truncate/recovery must not attach an old receipt to a new event
     * with the same numeric ID. Validate the immutable journal anchor too. */
    uint64 Native=0,Lower=0,Upper=0;SdkUInt(SdkGet(V,"native_run"),&Native);SdkUInt(SdkGet(V,"started_after_us"),&Lower);SdkUInt(SdkGet(V,"completed_at_us"),&Upper);
    xwork_error Error;MdoSessionEventSnapshot* Events=MdoSessionEventReplay(C->ProjectId,C->SessionId,Event-1,1,&Error);
    MdoSessionEventInfo Info={0};Info.Size=sizeof(Info);
    bool Matches=Events&&MdoSessionEventSnapshotAt(Events,0,&Info)&&Info.EventId==Event&&Info.Kind==XWORK_EVENT_AGENT_START&&!Info.AgentDepth&&Info.RunId==Native&&Info.OccurredAt>=(int64)Lower&&Info.OccurredAt<=(int64)Upper;
    MdoSessionEventSnapshotRelease(Events);
    if(!Matches){xrtValueRelease(V);return SdkGitFail(E,"git_turn_context_changed","The original reply anchor is no longer available",409),NULL;}
    return V;
}
xvalue* MdoApplicationTurnChangesBegin(const mdo_sdk_context_v1* Given,uint64 NativeRun){
    if(!SdkGitBaselineLock||!Given->SessionId[0])return NULL;
    xerror* Saved=xrtTakeError();mdo_sdk_context_v1 C=*Given;uint64 End=MdoDeadlineAfterUs(5000000);if(!C.Deadline||C.Deadline>End)C.Deadline=End;mdo_error_v1 E={0};
    xvalue* V=xrtValueObject();if(!V)goto done;
    SdkPutUInt(V,"schema_version",1);SdkPut(V,"project",C.ProjectId);SdkPut(V,"session",C.SessionId);SdkPut(V,"workspace",C.Workspace);
    SdkPutUInt(V,"native_run",NativeRun);SdkPutUInt(V,"started_after_us",(uint64)MdoUtcUs());
    xwork_error Error;MdoSessionEventSnapshot* Events=MdoSessionEventReplay(C.ProjectId,C.SessionId,0,1,&Error);
    if(!Events){xrtValueRelease(V);V=NULL;goto done;}
    SdkPutUInt(V,"after_event",MdoSessionEventSnapshotLatestId(Events));MdoSessionEventSnapshotRelease(Events);
    if(SdkGitBaselineEnter(&C,&E)){
        xvalue* P=xrtValueObject();SdkPut(P,"repository",C.Workspace);char* Root=SdkGitRoot(&C,P,&E);xrtValueRelease(P);
        char* Repo=SdkGitSnapshotRepo(C.ProjectId),*Tree=NULL;char Ref[160];snprintf(Ref,sizeof(Ref),"refs/mdo-turns/%s/before",C.RunId);
        if(Root&&Repo)Tree=SdkGitSnapshot(&C,Root,Repo,Ref,&E);
        SdkPut(V,"repository",Root);SdkPut(V,"base",Tree);SdkPut(V,"reason",Tree?"":E.Code[0]?E.Code:"git_snapshot_failed");
        xrtFree(Tree);xrtFree(Repo);xrtFree(Root);xrtMutexUnlock(SdkGitBaselineLock);
    }else SdkPut(V,"reason",E.Code);
done:xrtClearError();if(Saved)xrtSetErrorTake(Saved);return V;
}
void MdoApplicationTurnChangesFinish(const mdo_sdk_context_v1* Given,xvalue* V,bool Started){
    if(!V||!Started)return;
    xerror* Saved=xrtTakeError();mdo_sdk_context_v1 C=*Given;C.Deadline=MdoDeadlineAfterUs(5000000);mdo_error_v1 E={0};
    uint64 After=0,Native=0,Lower=0,Event=0;SdkUInt(SdkGet(V,"after_event"),&After);SdkUInt(SdkGet(V,"native_run"),&Native);SdkUInt(SdkGet(V,"started_after_us"),&Lower);
    xwork_error Error;MdoSessionEventSnapshot* Events=MdoSessionEventReplay(C.ProjectId,C.SessionId,After,256,&Error);
    for(size_t i=0;Events&&i<MdoSessionEventSnapshotCount(Events);i++){
        MdoSessionEventInfo Info={0};Info.Size=sizeof(Info);
        if(MdoSessionEventSnapshotAt(Events,i,&Info)&&Info.Kind==XWORK_EVENT_AGENT_START&&!Info.AgentDepth&&Info.RunId==Native&&Info.OccurredAt>=(int64)Lower){Event=Info.EventId;break;}
    }MdoSessionEventSnapshotRelease(Events);char Path[512];if(!SdkGitTurnPath(&C,Event,Path))goto done;
    bool Available=false;
    if(SdkGitOid(SdkField(V,"base"))&&SdkGitBaselineEnter(&C,&E)){
        char* Repo=SdkGitSnapshotRepo(C.ProjectId);char Ref[160];snprintf(Ref,sizeof(Ref),"refs/mdo-turns/%s/after",C.RunId);
        char* Tree=Repo?SdkGitSnapshot(&C,SdkField(V,"repository"),Repo,Ref,&E):NULL;
        Available=Tree!=NULL;SdkPut(V,"current_tree",Tree);if(!Available)SdkPut(V,"reason",E.Code);
        xrtFree(Tree);xrtFree(Repo);xrtMutexUnlock(SdkGitBaselineLock);
    }
    SdkPutUInt(V,"turn",Event);SdkPutUInt(V,"completed_at_us",(uint64)MdoUtcUs());
    xrtValueObjectSetNew(V,XRT_STR_LITERAL("available"),xrtValueBool(Available));
    if(MdoPathField(V,"workspace",false)&&MdoPathField(V,"repository",false)){
        char* Json=SdkJson(V);if(Json)(void)MdoHomeAtomicWrite(Path,Json,strlen(Json),false);xrtFree(Json);
    }
done:xrtClearError();if(Saved)xrtSetErrorTake(Saved);
}
