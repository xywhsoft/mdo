#include "../../include/mdo/bootstrap.h"
#include "../../include/mdo/agents.h"
#include "../../include/mdo/sessions.h"

/* Durable member identity and inbox are independent of in-process operation
 * handles. Each explicit step claims one turn; ambiguous turns never replay. */
static xvalue* SdkInstanceValue(xvalue* Root,cstr Key){return (xvalue*)SdkGet(SdkGet(SdkGet(Root,"records"),Key),"value");}
static bool SdkInstanceStore(xvalue* Root,cstr Key,xvalue* V,mdo_error_v1* E){
    uint64 Revision=SdkStateRevision(Root);const xvalue* Records=SdkGet(Root,"records");
    if(Revision==UINT64_MAX||(!SdkGet(Records,Key)&&xrtValueCount(Records)>=SDK_STATE_RECORDS))return MdoApplicationError(E,"sdk_instance_limit","Member namespace retained-record budget exhausted","instance",413,false);
    return SdkPutUInt(V,"revision",Revision+1)&&SdkMailboxStore(Root,Key,V,Revision+1,E);
}
static int SdkInstanceActive(cstr Identity);
static xvalue* SdkInstanceInfo(const xvalue* V){
    xvalue* R=xrtValueClone(V);cstr State=SdkField(V,"state"),Epoch=SdkField(V,"epoch");
    cstr Identity=SdkField(V,"runtime_identity");
    if(State&&!strcmp(State,"running")&&(!Epoch||strcmp(Epoch,SdkState.Epoch)||!Identity||SdkInstanceActive(Identity)<0))SdkPut(R,"state","requires_review");
    return R;
}
static xvalue* SdkInstanceListInfo(const xvalue* V){
    xvalue* Current=SdkInstanceInfo(V),*R=xrtValueObject();const cstr Fields[]={"id","agent_id","model_id","state","active_message"};
    for(size_t I=0;I<sizeof(Fields)/sizeof(*Fields);I++)if(SdkGet(Current,Fields[I]))SdkPut(R,Fields[I],SdkField(Current,Fields[I]));
    SdkPutUInt(R,"revision",SdkStateRevision(Current));cstr Outcome=SdkField(SdkGet(Current,"last_outcome"),"outcome");if(Outcome)SdkPut(R,"outcome",Outcome);
    xrtValueRelease(Current);return R;
}
static bool SdkInstancePaths(cstr StatePath,cstr Id,char Identity[65],char Directory[160]){
    char* Text=xrtFormat("%s\n%s",StatePath,Id);bool Ok=Text&&MdoExtensionHash(Text,Identity);xrtFree(Text);
    if(Ok)snprintf(Directory,160,"data/application/instances/%s",Identity);return Ok;
}
static int SdkInstanceActive(cstr Identity){for(int I=0;I<8;I++)if(SdkState.Members[I].Cancel&&!strcmp(SdkState.Members[I].Identity,Identity))return I;return -1;}
static bool SdkInstanceNotify(const mdo_sdk_context_v1* C,const xvalue* V){MdoApplicationEvent("instance.changed",C,V);return true;}
#include "state_history.inc.c"
#include "state_recovery.inc.c"
static bool SdkMemberFinal(void* U,const MdoAgentNativeContext* Native,xwork_error* E){
    xwork_run_result* R=U;size_t Count=xllmSessionEntryCount(Native->History);(void)E;
    if(Count){xllm_session_entry_view Last={0};Last.iSize=sizeof(Last);if(!xllmSessionEntryAt(Native->History,Count-1,&Last))return false;
        if(Last.pMessage&&Last.pMessage->eRole==XLLM_ROLE_ASSISTANT)R->sFinalText=xrtStrDup(Last.pMessage->sContent?Last.pMessage->sContent:"");}
    return true;
}
static bool SdkMemberResumeOptions(const xvalue* P,xwork_resume_options* O,xwork_recovery_decision Decisions[64],mdo_error_v1* E){
    const xvalue* A=SdkGet(P,"decisions");xworkResumeOptionsInit(O);
    if(A&&(xrtValueType(A)!=XVALUE_ARRAY||xrtValueCount(A)>64))return MdoApplicationError(E,"sdk_instance_arguments","Supply at most 64 explicit recovery decisions","instance",400,false);
    O->pDecisions=Decisions;O->iDecisionCount=xrtValueCount(A);
    for(size_t I=0;I<O->iDecisionCount;I++){
        const xvalue* V=xrtValueArrayGet(A,I);cstr Id=SdkField(V,"tool_call_id"),Action=SdkField(V,"action");
        if(!Id||!Id[0]||strlen(Id)>256||!Action||(strcmp(Action,"retry")&&strcmp(Action,"record_uncertain")))return MdoApplicationError(E,"sdk_instance_arguments","Each recovery decision needs a tool_call_id and retry/record_uncertain action","instance",400,false);
        xworkRecoveryDecisionInit(&Decisions[I]);Decisions[I].sToolCallId=Id;Decisions[I].eAction=!strcmp(Action,"retry")?XWORK_RECOVERY_RETRY:XWORK_RECOVERY_RECORD_UNCERTAIN;
    }return true;
}
static bool SdkInstanceStep(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    bool Resume=!strcmp(Method,"instance.resume"),Abandon=!strcmp(Method,"instance.abandon"),Recover=Resume||Abandon;
    xwork_resume_options ResumeOptions;xwork_recovery_decision Decisions[64];
    if(Resume&&!SdkMemberResumeOptions(P,&ResumeOptions,Decisions,E))return false;
    cstr Id=SdkField(P,"id");char Key[128],Path[160],Identity[65],Directory[160];snprintf(Key,sizeof(Key),"sys.instance.%s",Id);
    MdoModuleCatalog* Modules=MdoModuleCatalogSnapshot();
    if(!Modules)return MdoApplicationError(E,"sdk_instance_role","Role catalog is unavailable","instance",503,false);
    xcancel* Cancel=xrtCancelCreate();xvalue *Config=NULL,*Message=NULL,*MailParams=xrtValueObject(),*Root=NULL;int Slot=-1;bool Ok=false,Claimed=false;
    if(!Cancel||!MailParams)goto done;
    xrtMutexLock(SdkState.Lock);Root=SdkStateLoad(C,P,Path,E);xvalue* Member=Root?SdkInstanceValue(Root,Key):NULL;
    if(!Root)goto unlock;
    if(!Member){MdoApplicationError(E,"sdk_instance_missing","Member is unavailable","instance",404,false);goto unlock;}
    xvalue* Current=SdkInstanceInfo(Member);cstr CurrentState=SdkField(Current,"state");
    bool Allowed=CurrentState&&!strcmp(CurrentState,Recover?"requires_review":"idle");xrtValueRelease(Current);
    if(!Allowed){MdoApplicationError(E,"sdk_instance_not_idle","Member is running, closed or needs an explicit resume/abandon decision","instance",409,false);goto unlock;}
    if(Recover){uint64 Expected=0,Revision=0;SdkUInt(SdkGet(Member,"revision"),&Revision);cstr MessageId=SdkField(P,"message_id"),Active=SdkField(Member,"active_message");
        if(!SdkUInt(SdkGet(P,"expected_revision"),&Expected)||Expected!=Revision||!Active||!MessageId||strcmp(MessageId,Active)){
            MdoApplicationError(E,"sdk_instance_conflict","Recovery requires the current member revision and active_message","instance",409,false);goto unlock;}}
    if(SdkState.Stopping){MdoApplicationError(E,"sdk_stopping","Member service is stopping","instance",503,false);goto unlock;}
    for(int I=0;I<8;I++)if(!SdkState.Members[I].Cancel){Slot=I;break;}
    if(Slot<0){MdoApplicationError(E,"sdk_instance_busy","Eight member turns are already active; wait for idle notification","instance",429,false);goto unlock;}
    if(!SdkInstancePaths(Path,Id,Identity,Directory))goto unlock;
    if(SdkInstanceActive(Identity)>=0){MdoApplicationError(E,"sdk_instance_busy","Member is being inspected or run by another operation","instance",409,false);goto unlock;}
    SdkPut(MailParams,"mailbox",SdkField(Member,"mailbox"));SdkPutUInt(MailParams,"limit",1);xvalue* Page=NULL;bool Changed=false;
    if(Recover)SdkPut(MailParams,"message_id",SdkField(Member,"active_message"));
    if(!SdkMailboxApply(C,Recover?"mailbox.get":"mailbox.read",MailParams,Root,&Page,E,&Changed)){xrtValueRelease(Page);goto unlock;}
    const xvalue* Next=Recover?Page:xrtValueArrayGet(SdkGet(Page,"items"),0);
    if(!Next){*R=SdkInstanceInfo(Member);xrtValueObjectSetNew(*R,XRT_STR_LITERAL("no_work"),xrtValueBool(true));Ok=*R!=NULL;xrtValueRelease(Page);goto unlock;}
    Message=xrtValueClone(Next);xrtValueRelease(Page);Config=xrtValueClone(Member);
    SdkPut(Member,"state","running");SdkPut(Member,"epoch",SdkState.Epoch);SdkPut(Member,"runtime_identity",Identity);SdkPut(Member,"active_message",SdkField(Message,"message_id"));
    Claimed=SdkInstanceStore(Root,Key,Member,E)&&SdkStateSave(Path,Root,E);
    if(Claimed){snprintf(SdkState.Members[Slot].Identity,65,"%s",Identity);SdkState.Members[Slot].Cancel=Cancel;}
unlock:
    xrtValueRelease(Root);Root=NULL;xrtMutexUnlock(SdkState.Lock);if(!Claimed)goto done;
    /* No state/catalog mutex is held while creating or executing C/model code. */
    xwork_error Error;xworkErrorInit(&Error);MdoAgentSession* Session=NULL;MdoAgentRun* Run=NULL;MdoApprovalScope* Scope=NULL;
    MdoAgentSessionOptions Options;MdoAgentSessionOptionsInit(&Options);
    Options.AgentId=SdkField(Config,"agent_id");Options.ModelId=SdkField(Config,"model_id");Options.ProjectId=C->ProjectId;
    Options.Namespace=SdkField(Config,"namespace");Options.MemberId=Id;
    char* DefaultWorkspace=C->ProjectId&&!strcmp(C->ProjectId,"default")?MdoHomeDefaultWorkspacePath(true):NULL;
    char QuestionScope[33];memcpy(QuestionScope,Identity,32);QuestionScope[32]=0;
    Options.ProductSessionId=SdkField(Config,"parent_session");Options.AskScopeId=Options.ProductSessionId&&Options.ProductSessionId[0]?Options.ProductSessionId:QuestionScope;Options.WorkspaceRoot=DefaultWorkspace?DefaultWorkspace:SdkField(Config,"workspace");
    Options.PermissionProfile=SdkField(Config,"permission_profile");Options.AllowSubagentDefinition=true;Options.ModuleCatalog=Modules;
    Options.Cancel=Cancel;Options.Deadline=C->Deadline?C->Deadline:MDO_DEADLINE_NEVER;Options.UseRunPermissionScope=true;Options.OnPermission=MdoApprovalOnPermission;
    cstr Parent=Options.ProductSessionId;MdoSessionInfo ParentInfo={0};ParentInfo.Size=sizeof(ParentInfo);
    bool Ready=true;
    if(Parent&&Parent[0]){
        MdoSession* Owner=MdoSessionLoad(C->ProjectId,Parent,&Error);Ready=Owner&&MdoSessionGetInfo(Owner,&ParentInfo)&&ParentInfo.Status==MDO_SESSION_ACTIVE;MdoSessionRelease(Owner);
        if(Ready){Options.PermissionProfile=ParentInfo.PermissionProfile;Scope=MdoSessionPermissionScopeAcquire(C->ProjectId,Parent,&Error);Options.LivePermissionScope=Scope;Ready=Scope!=NULL;}
    }
    char Snapshot[192],Journal[192];snprintf(Snapshot,sizeof(Snapshot),"%s/snapshot.json",Directory);snprintf(Journal,sizeof(Journal),"%s/journal.jsonl",Directory);
    bool DirectoryExists=false; xfileinfo DirectoryStat={0};
    bool DirectoryReady=MdoHomeCreateDirectory(Directory)||(MdoHomeExternalStat(Directory,&DirectoryExists,&DirectoryStat)&&DirectoryExists&&DirectoryStat.Type==XFILE_TYPE_DIRECTORY);
    bool Exists=false,JournalExists=false;xfileinfo Stat={0};Ready=Ready&&DirectoryReady&&MdoHomeExternalStat(Snapshot,&Exists,&Stat)&&MdoHomeExternalStat(Journal,&JournalExists,&Stat);
    char *NativeSnapshot=MdoHomeExternalPath(Snapshot),*NativeJournal=MdoHomeExternalPath(Journal);
    Ready=Ready&&NativeSnapshot&&NativeJournal;
    if(!Ready&&Error.eCode==XWORK_ERROR_NONE){Error.eCode=XWORK_ERROR_IO;snprintf(Error.sMessage,sizeof(Error.sMessage),"Cannot validate member parent or private snapshot directory");}
    Options.SessionPath=NativeSnapshot;Options.JournalPath=NativeJournal;Options.ArtifactDirectory=Directory;Options.Recover=Exists||JournalExists;
    Options.PreserveRecoveryLedger=Recover;
    SdkMemberHistoryWriter* History=NULL;
    if(Ready&&!Abandon){
        History=SdkMemberHistoryCreate(Directory,Message);Ready=History!=NULL;
        if(Ready){Options.OnEvent=SdkMemberHistoryEvent;Options.EventUserData=History;
            Options.OwnerUserData=History;Options.OnOwnerRetain=SdkMemberHistoryRetain;Options.OnOwnerRelease=SdkMemberHistoryRelease;}
        else {Error.eCode=XWORK_ERROR_IO;snprintf(Error.sMessage,sizeof(Error.sMessage),"Cannot open the member's visible conversation journal");}
    }
    if(Ready&&NativeSnapshot&&NativeJournal)Session=MdoAgentSessionCreateWithRuntime(MdoBootstrapRuntime(),&Options,&Error);
    /* Callback ownership follows the native session, including any pinned
     * descendants, rather than borrowing this step's stack frame. */
    SdkMemberHistoryRelease(History);
    bool Recovery=false;Ready=Session&&MdoAgentSessionRecoveryRequired(Session,&Recovery,&Error);
    cstr ExpectedToken=SdkField(P,"expected_recovery_token");
    if(Ready&&Resume&&ExpectedToken){
        uint64 Sequence=0;xwork_recovery_snapshot* Pending=MdoAgentSessionRecoverySnapshot(Session,&Error);
        char Token[MDO_AGENT_RECOVERY_TOKEN_CAPACITY],Epoch[65];
        Ready=(Pending||Error.eCode==XWORK_ERROR_NONE)&&SdkMemberRecoveryEpoch(Config,Epoch)&&MdoAgentSessionLastSequence(Session,&Sequence,&Error)&&MdoAgentRecoverySnapshotToken(Pending,Recovery,Sequence,Epoch,Token)&&!strcmp(ExpectedToken,Token);
        xworkRecoverySnapshotRelease(Pending);
        if(!Ready){Error.eCode=XWORK_ERROR_RECOVERY_REQUIRED;snprintf(Error.sMessage,sizeof(Error.sMessage),"Recovery details changed; inspect the native calls again before deciding");}
    }
    if(Ready&&!Recover&&Recovery)Ready=false;
    /* An initial checkpoint makes even the first model turn recoverable. */
    if(Ready)Ready=MdoAgentSessionCheckpoint(Session,&Error);
    xwork_result Status=XWORK_RESULT_ERROR;xwork_run_result Result={0};
    if(Ready&&Abandon){uint64 Sequence=0,Finished=0;Ready=MdoAgentSessionLastSequence(Session,&Sequence,&Error)&&(!Recovery||MdoAgentSessionFinishInterrupted(Session,Sequence,&Finished,&Error));if(Ready)Status=XWORK_RESULT_CANCELLED;}
    else if(Ready&&Resume&&!Recovery){Ready=MdoAgentSessionDriveNative(Session,SdkMemberFinal,&Result,&Error);if(Ready)Status=XWORK_RESULT_OK;}
    else if(Ready){MdoAgentRunOptions Turn;MdoAgentRunOptionsInit(&Turn);Turn.Prompt=SdkField(SdkGet(Message,"body"),"prompt");Turn.Cancel=Cancel;Turn.Deadline=Options.Deadline;Turn.Resume=Resume;Turn.ResumeOptions=Resume?&ResumeOptions:NULL;
        Run=MdoAgentRunCreate(Session,&Turn,&Error);Ready=Run&&MdoAgentRunStart(Run,&Error);}
    if(Ready&&Run){
        for(;;){MdoAgentRunInfo Info={0};Info.Size=sizeof(Info);if(!MdoAgentRunGetInfo(Run,&Info))break;
            if(Info.Run.eState!=XWORK_RUN_CREATED&&Info.Run.eState!=XWORK_RUN_RUNNING){Status=MdoAgentRunWait(Run,MDO_DEADLINE_NEVER,&Result,&Error);break;}
            if(SdkCancelled(C))xrtCancelRequest(Cancel);xrtSleep(20);
        }
    }
    MdoAgentRunDestroy(Run); /* Releases run claim before checkpoint/idle publication. */
    xwork_error CheckpointError;xworkErrorInit(&CheckpointError);
    bool Checkpoint=Session&&MdoAgentSessionWithQuiescentCheckpoint(Session,NULL,NULL,&CheckpointError);
    if(Checkpoint&&!MdoAgentSessionRecoveryRequired(Session,&Recovery,&CheckpointError))Checkpoint=false;
    if(!Checkpoint&&Error.eCode==XWORK_ERROR_NONE)Error=CheckpointError;
    xvalue* Outcome=xrtValueObject();cstr State=Status==XWORK_RESULT_OK?"succeeded":Status==XWORK_RESULT_CANCELLED?"cancelled":Status==XWORK_RESULT_TIMEOUT?"timed_out":Status==XWORK_RESULT_LIMIT?"limited":"failed";
    if(!Checkpoint||Recovery)State="requires_review";
    SdkPut(Outcome,"outcome",State);SdkPut(Outcome,"message_id",SdkField(Message,"message_id"));SdkPut(Outcome,"error",Status==XWORK_RESULT_OK?"":Error.sMessage);
    SdkPutUInt(Outcome,"error_code",Error.eCode);
    if(Recovery&&Session){xwork_recovery_snapshot* Pending=MdoAgentSessionRecoverySnapshot(Session,&CheckpointError);xvalue* Calls=xrtValueArray();
        for(size_t I=0;Pending&&I<xworkRecoverySnapshotCount(Pending)&&I<64;I++){xwork_recovery_call_info Call;xworkRecoveryCallInfoInit(&Call);if(xworkRecoverySnapshotAt(Pending,I,&Call)){
            xvalue* V=xrtValueObject();SdkPut(V,"tool_call_id",Call.sToolCallId);SdkPut(V,"tool",Call.sToolName);xrtValueObjectSetNew(V,XRT_STR_LITERAL("automatic_retry_safe"),xrtValueBool(Call.bAutomaticRetrySafe));xrtValueArrayAppendNew(Calls,V);}}
        xworkRecoverySnapshotRelease(Pending);xrtValueObjectSetNew(Outcome,XRT_STR_LITERAL("pending_tools"),Calls);}
    size_t Bytes=Result.sFinalText?strlen(Result.sFinalText):0,Keep=Bytes>8192?8192:Bytes;while(Keep<Bytes&&Keep&&(Result.sFinalText[Keep]&0xc0)==0x80)Keep--;
    xrtValueObjectSetNew(Outcome,XRT_STR_LITERAL("text"),xrtValueString(xrtStrViewN(Result.sFinalText?Result.sFinalText:"",Keep)));
    xrtValueObjectSetNew(Outcome,XRT_STR_LITERAL("text_truncated"),xrtValueBool(Bytes>Keep));
    xworkRunResultUnit(&Result);MdoAgentSessionRelease(Session);MdoSessionPermissionScopeRelease(C->ProjectId,Parent,Scope);xrtFree(NativeSnapshot);xrtFree(NativeJournal);xrtFree(DefaultWorkspace);
    xrtMutexLock(SdkState.Lock);Root=SdkStateLoad(C,P,Path,E);Member=Root?SdkInstanceValue(Root,Key):NULL;
    if(Member){
        xvalue* Ack=xrtValueObject();SdkPut(Ack,"mailbox",SdkField(Config,"mailbox"));SdkPut(Ack,"message_id",SdkField(Message,"message_id"));xvalue* Receipt=NULL;bool Changed=false;
        Ok=SdkMailboxApply(C,"mailbox.ack",Ack,Root,&Receipt,E,&Changed);xrtValueRelease(Receipt);xrtValueRelease(Ack);
        if(Ok){char MailKey[128];SdkPut(MailParams,"message_id",SdkField(Message,"message_id"));SdkMailboxIdentity(MailParams,MailKey);xvalue* Mail=SdkInstanceValue(Root,MailKey);
            Ok=xrtValueObjectSet(Mail,XRT_STR_LITERAL("outcome"),Outcome)&&SdkPut(Member,"state",!strcmp(State,"requires_review")?"requires_review":"idle")&&
                xrtValueObjectSet(Member,XRT_STR_LITERAL("last_outcome"),Outcome)&&SdkInstanceStore(Root,Key,Member,E)&&SdkStateSave(Path,Root,E);}
        if(Ok)*R=SdkInstanceInfo(Member);
    }else Ok=false;
    xrtValueRelease(Root);Root=NULL;SdkState.Members[Slot].Cancel=NULL;SdkState.Members[Slot].Identity[0]=0;xrtCondBroadcast(SdkState.Drained);xrtMutexUnlock(SdkState.Lock);
    xrtValueRelease(Outcome);if(Ok)SdkInstanceNotify(C,*R);
done:
    xrtCancelDestroy(Cancel);MdoModuleCatalogRelease(Modules);xrtValueRelease(Config);xrtValueRelease(Message);xrtValueRelease(MailParams);return Ok;
}
static bool SdkInstanceWait(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    uint64 Timeout=0;if((SdkGet(P,"timeout_ms")&&!SdkUInt(SdkGet(P,"timeout_ms"),&Timeout))||Timeout>60000)return MdoApplicationError(E,"sdk_instance_arguments","timeout_ms must be 0..60000","instance",400,false);
    uint64 End=MdoDeadlineAfterUs(Timeout*1000);
    for(;;){
        xvalue* Events=MdoApplicationEvents(NULL,E);if(!Events)return false;xvalue* Wait=xrtValueObject();
        xrtValueObjectSet(Wait,XRT_STR_LITERAL("after"),SdkGet(Events,"latest"));xrtValueObjectSet(Wait,XRT_STR_LITERAL("epoch"),SdkGet(Events,"epoch"));SdkPut(Wait,"topic","instance.changed");xrtValueRelease(Events);
        bool Found=false,Done=false;bool Ok=MdoApplicationStateCall(C,"instance.result",P,R,E,&Found);xrtValueGetBool(SdkGet(*R,"completed"),&Done);
        if(!Ok||Done||!Timeout||MdoDeadlineExpired(End)){xrtValueRelease(Wait);return Ok;}
        xrtValueRelease(*R);*R=NULL;uint64 Ms=(MdoDeadlineRemainingUs(End)+999)/1000;if(Ms>1000)Ms=1000;SdkPutUInt(Wait,"timeout_ms",Ms);
        Events=MdoApplicationEventsWait(C,Wait,E);xrtValueRelease(Wait);if(!Events)return false;xrtValueRelease(Events);
    }
}
static bool SdkInstanceCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Id=SdkField(P,"id");bool List=!strcmp(Method,"instance.list"),Create=!strcmp(Method,"instance.create");
    if(!List&&(!SdkStateKey(Id)||strlen(Id)>64))return MdoApplicationError(E,"sdk_instance_arguments","A portable member id of at most 64 characters is required","instance",400,false);
    if(!strcmp(Method,"instance.history"))return SdkMemberHistory(C,P,R,E);
    if(!strcmp(Method,"instance.recovery"))return SdkMemberRecovery(C,P,R,E);
    if(!strcmp(Method,"instance.wait"))return SdkInstanceWait(C,P,R,E);
    if(!strcmp(Method,"instance.step")||!strcmp(Method,"instance.resume")||!strcmp(Method,"instance.abandon"))return SdkInstanceStep(C,Method,P,R,E);
    MdoModuleCatalog* Modules=Create?MdoModuleCatalogSnapshot():NULL;MdoModuleAgentInfo Role={0};Role.Size=sizeof(Role);
    if(Create&&!Modules)return MdoApplicationError(E,"sdk_instance_role","Role catalog is unavailable","instance",503,false);
    char Key[128],Path[160];snprintf(Key,sizeof(Key),"sys.instance.%s",Id?Id:"");bool Ok=false,Changed=false;
    xrtMutexLock(SdkState.Lock);xvalue* Root=SdkStateLoad(C,P,Path,E);xvalue* Member=Root?SdkInstanceValue(Root,Key):NULL;
    if(!Root)goto done;
    if(List){*R=xrtValueArray();const xvalue* Records=SdkGet(Root,"records");for(size_t I=0;I<xrtValueCount(Records);I++){cstr K=SdkKey(Records,I);if(!strncmp(K,"sys.instance.",13))xrtValueArrayAppendNew(*R,SdkInstanceListInfo(SdkInstanceValue(Root,K)));}Ok=*R!=NULL;goto done;}
    if(Create){
        cstr Agent=SdkField(P,"agent_id"),Model=SdkField(P,"model_id"),Permission=SdkField(P,"permission_profile");if(!Agent)Agent="mdo.default";if(!Permission)Permission="balanced";
        if(Model&&Model[0]&&(!SdkStateKey(Model)||strlen(Model)>128)){MdoApplicationError(E,"sdk_instance_arguments","model_id must be a portable model identity","instance",400,false);goto done;}
        if(!MdoModuleCatalogAgentFind(Modules,Agent,&Role)||!(Role.Flags&(MDO_AGENT_MAIN|MDO_AGENT_SUBAGENT))||
            (strcmp(Permission,"read-only")&&strcmp(Permission,"balanced")&&strcmp(Permission,"full-access"))){MdoApplicationError(E,"sdk_instance_role","Select an available role and permission profile","instance",400,false);goto done;}
        xvalue* Intent=xrtValueObject();SdkPut(Intent,"id",Id);SdkPut(Intent,"agent_id",Agent);SdkPut(Intent,"model_id",Model);SdkPut(Intent,"permission_profile",Permission);
        SdkPut(Intent,"workspace",C->Workspace);SdkPut(Intent,"parent_session",C->SessionId);SdkPut(Intent,"owner",C->ExtensionId);char Hash[65];SdkMailboxHash(Intent,Hash);
        if(Member){Ok=SdkField(Member,"intent_hash")&&!strcmp(SdkField(Member,"intent_hash"),Hash);xrtValueRelease(Intent);if(Ok)*R=SdkInstanceInfo(Member);else MdoApplicationError(E,"sdk_identity_conflict","Member id belongs to different creation intent","instance",409,false);goto done;}
        size_t Count=0;const xvalue* Records=SdkGet(Root,"records");for(size_t I=0;I<xrtValueCount(Records);I++)if(!strncmp(SdkKey(Records,I),"sys.instance.",13))Count++;
        if(Count>=64){xrtValueRelease(Intent);MdoApplicationError(E,"sdk_instance_limit","Namespace retains 64 members; use an execution namespace","instance",413,false);goto done;}
        char Box[96];snprintf(Box,sizeof(Box),"instance.%s",Id);SdkPut(Intent,"mailbox",Box);SdkPut(Intent,"namespace",SdkField(Root,"namespace"));SdkPut(Intent,"intent_hash",Hash);SdkPut(Intent,"state","idle");SdkPut(Intent,"epoch",SdkState.Epoch);SdkPut(Intent,"history_format","mdo.instance.history.v1");
        Ok=SdkInstanceStore(Root,Key,Intent,E);if(Ok)*R=SdkInstanceInfo(Intent);xrtValueRelease(Intent);Changed=Ok;
    }else if(!Member){MdoApplicationError(E,"sdk_instance_missing","Member is unavailable","instance",404,false);}
    else if(!strcmp(Method,"instance.get")){*R=SdkInstanceInfo(Member);Ok=*R!=NULL;}
    else if(!strcmp(Method,"instance.result")){
        xvalue* Q=xrtValueObject();SdkPut(Q,"mailbox",SdkField(Member,"mailbox"));SdkPut(Q,"message_id",SdkField(P,"message_id"));char MailKey[128];
        xvalue* Mail=SdkMailboxIdentity(Q,MailKey)?SdkInstanceValue(Root,MailKey):NULL;xrtValueRelease(Q);
        if(!Mail){MdoApplicationError(E,"sdk_message_missing","Member message is unavailable","instance",404,false);goto done;}
        const xvalue* Outcome=SdkGet(Mail,"outcome");xvalue* Fallback=NULL,*Current=SdkInstanceInfo(Member);cstr State=SdkField(Current,"state");
        if(!Outcome&&State&&(!strcmp(State,"closed")||!strcmp(State,"requires_review"))){Fallback=xrtValueObject();SdkPut(Fallback,"outcome",!strcmp(State,"closed")?"cancelled":"requires_review");SdkPut(Fallback,"message_id",SdkField(P,"message_id"));SdkPut(Fallback,"text","");Outcome=Fallback;}
        *R=xrtValueObject();SdkPut(*R,"message_id",SdkField(P,"message_id"));SdkPut(*R,"state",State);
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("completed"),xrtValueBool(Outcome!=NULL));
        if(Outcome)xrtValueObjectSet(*R,XRT_STR_LITERAL("result"),Outcome);xrtValueRelease(Fallback);xrtValueRelease(Current);Ok=*R!=NULL;
    }
    else if(!strcmp(Method,"instance.send")){
        cstr Prompt=SdkField(P,"prompt");if(!Prompt||!Prompt[0]){MdoApplicationError(E,"sdk_instance_arguments","A nonempty prompt is required","instance",400,false);goto done;}
        xvalue* Current=SdkInstanceInfo(Member);bool Open=!strcmp(SdkField(Current,"state"),"idle")||!strcmp(SdkField(Current,"state"),"running");xrtValueRelease(Current);
        if(!Open){MdoApplicationError(E,"sdk_instance_not_open","Member is closed or requires review","instance",409,false);goto done;}
        xvalue* Q=xrtValueObject(),*Body=xrtValueObject();SdkPut(Body,"prompt",Prompt);
        SdkPut(Body,"origin",mdoCallOrigin(C)==MDO_ORIGIN_USER?"user":"agent");
        SdkPut(Q,"mailbox",SdkField(Member,"mailbox"));SdkPut(Q,"message_id",SdkField(P,"message_id"));xrtValueObjectSetNew(Q,XRT_STR_LITERAL("body"),Body);
        Ok=SdkMailboxApply(C,"mailbox.send",Q,Root,R,E,&Changed);xrtValueRelease(Q);
    }else{
        char Identity[65],Directory[160];SdkInstancePaths(Path,Id,Identity,Directory);int Active=SdkInstanceActive(Identity);
        if(!strcmp(Method,"instance.cancel")){
            if(Active>=0)xrtCancelRequest(SdkState.Members[Active].Cancel);*R=SdkInstanceInfo(Member);xrtValueObjectSetNew(*R,XRT_STR_LITERAL("cancel_requested"),xrtValueBool(Active>=0));Ok=*R!=NULL;
        }else if(Active>=0){MdoApplicationError(E,"sdk_instance_busy","Cancel and await the active turn before closing","instance",409,false);}
        else {Ok=SdkPut(Member,"state","closed")&&SdkInstanceStore(Root,Key,Member,E);Changed=Ok;if(Ok)*R=SdkInstanceInfo(Member);}
    }
    if(Ok&&Changed)Ok=SdkStateSave(Path,Root,E);
done:
    xrtValueRelease(Root);xrtMutexUnlock(SdkState.Lock);MdoModuleCatalogRelease(Modules);
    if(!Ok){xrtValueRelease(*R);*R=NULL;}if(Ok&&Changed)SdkInstanceNotify(C,*R);return Ok;
}
