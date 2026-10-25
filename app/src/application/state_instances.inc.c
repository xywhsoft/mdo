#include "../../include/mdo/bootstrap.h"
#include "../../include/mdo/agents.h"
#include "../../include/mdo/sessions.h"

/* Durable member identity and inbox are independent of in-process operation
 * handles. Each explicit step claims one turn; ambiguous turns never replay. */
static xvalue* SdkInstanceValue(xvalue* Root,cstr Key){return (xvalue*)SdkGet(SdkGet(SdkGet(Root,"records"),Key),"value");}
static bool SdkInstanceStore(xvalue* Root,cstr Key,xvalue* V,mdo_error_v1* E){
    uint64 Revision=SdkStateRevision(Root);const xvalue* Records=SdkGet(Root,"records");
    if(Revision==UINT64_MAX||(!SdkGet(Records,Key)&&xrtValueCount(Records)>=SDK_STATE_RECORDS))return MdoApplicationError(E,"sdk_instance_limit","Member namespace retained-record budget exhausted","instance",413,false);
    return SdkMailboxStore(Root,Key,V,Revision+1,E);
}
static int SdkInstanceActive(cstr Identity);
static xvalue* SdkInstanceInfo(const xvalue* V){
    xvalue* R=xrtValueClone(V);cstr State=SdkField(V,"state"),Epoch=SdkField(V,"epoch");
    cstr Identity=SdkField(V,"runtime_identity");
    if(State&&!strcmp(State,"running")&&(!Epoch||strcmp(Epoch,SdkState.Epoch)||!Identity||SdkInstanceActive(Identity)<0))SdkPut(R,"state","requires_review");
    return R;
}
static bool SdkInstancePaths(cstr StatePath,cstr Id,char Identity[65],char Directory[160]){
    char* Text=xrtFormat("%s\n%s",StatePath,Id);bool Ok=Text&&MdoExtensionHash(Text,Identity);xrtFree(Text);
    if(Ok)snprintf(Directory,160,"data/application/instances/%s",Identity);return Ok;
}
static int SdkInstanceActive(cstr Identity){for(int I=0;I<8;I++)if(SdkState.Members[I].Cancel&&!strcmp(SdkState.Members[I].Identity,Identity))return I;return -1;}
static bool SdkInstanceNotify(const mdo_sdk_context_v1* C,const xvalue* V){MdoApplicationEvent("instance.changed",C,V);return true;}
static bool SdkInstanceStep(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Id=SdkField(P,"id");char Key[128],Path[160],Identity[65],Directory[160];snprintf(Key,sizeof(Key),"sys.instance.%s",Id);
    MdoModuleCatalog* Modules=MdoModuleCatalogSnapshot();
    if(!Modules)return MdoApplicationError(E,"sdk_instance_role","Role catalog is unavailable","instance",503,false);
    xcancel* Cancel=xrtCancelCreate();xvalue *Config=NULL,*Message=NULL,*MailParams=xrtValueObject(),*Root=NULL;int Slot=-1;bool Ok=false,Claimed=false;
    if(!Cancel||!MailParams)goto done;
    xrtMutexLock(SdkState.Lock);Root=SdkStateLoad(C,P,Path,E);xvalue* Member=Root?SdkInstanceValue(Root,Key):NULL;
    if(!Member){MdoApplicationError(E,"sdk_instance_missing","Member is unavailable","instance",404,false);goto unlock;}
    if(!SdkField(Member,"state")||strcmp(SdkField(Member,"state"),"idle")){MdoApplicationError(E,"sdk_instance_not_idle","Member is running, closed or requires explicit recovery review","instance",409,false);goto unlock;}
    if(SdkState.Stopping){MdoApplicationError(E,"sdk_stopping","Member service is stopping","instance",503,false);goto unlock;}
    for(int I=0;I<8;I++)if(!SdkState.Members[I].Cancel){Slot=I;break;}
    if(Slot<0){MdoApplicationError(E,"sdk_instance_busy","Eight member turns are already active; wait for idle notification","instance",429,false);goto unlock;}
    if(!SdkInstancePaths(Path,Id,Identity,Directory))goto unlock;
    SdkPut(MailParams,"mailbox",SdkField(Member,"mailbox"));SdkPutUInt(MailParams,"limit",1);xvalue* Page=NULL;bool Changed=false;
    if(!SdkMailboxApply(C,"mailbox.read",MailParams,Root,&Page,E,&Changed)){xrtValueRelease(Page);goto unlock;}
    const xvalue* Next=xrtValueArrayGet(SdkGet(Page,"items"),0);
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
    bool Exists=false;xfileinfo Stat={0};Ready=Ready&&DirectoryReady&&MdoHomeExternalStat(Snapshot,&Exists,&Stat);
    char *NativeSnapshot=MdoHomeExternalPath(Snapshot),*NativeJournal=MdoHomeExternalPath(Journal);
    Ready=Ready&&NativeSnapshot&&NativeJournal;
    if(!Ready&&Error.eCode==XWORK_ERROR_NONE){Error.eCode=XWORK_ERROR_IO;snprintf(Error.sMessage,sizeof(Error.sMessage),"Cannot validate member parent or private snapshot directory");}
    Options.SessionPath=NativeSnapshot;Options.JournalPath=NativeJournal;Options.ArtifactDirectory=Directory;Options.Recover=Exists;
    cstr Stage="session_open";
    if(Ready&&NativeSnapshot&&NativeJournal)Session=MdoAgentSessionCreateWithRuntime(MdoBootstrapRuntime(),&Options,&Error);
    if(Session)Stage="recovery_check";
    bool Recovery=false;Ready=Session&&MdoAgentSessionRecoveryRequired(Session,&Recovery,&Error)&&!Recovery;
    if(Ready){MdoAgentRunOptions Turn;MdoAgentRunOptionsInit(&Turn);Turn.Prompt=SdkField(SdkGet(Message,"body"),"prompt");Turn.Cancel=Cancel;Turn.Deadline=Options.Deadline;
        Stage="run_create";Run=MdoAgentRunCreate(Session,&Turn,&Error);if(Run)Stage="run_start";Ready=Run&&MdoAgentRunStart(Run,&Error);}
    xwork_result Status=XWORK_RESULT_ERROR;xwork_run_result Result={0};
    if(Ready){
        Stage="run_wait";
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
    SdkPut(Outcome,"stage",Stage);SdkPutUInt(Outcome,"error_code",Error.eCode);xrtValueObjectSetNew(Outcome,XRT_STR_LITERAL("checkpointed"),xrtValueBool(Checkpoint));xrtValueObjectSetNew(Outcome,XRT_STR_LITERAL("recovery_required"),xrtValueBool(Recovery));
    SdkPutUInt(Outcome,"model_calls",Result.uModelCalls);SdkPutUInt(Outcome,"tool_calls",Result.uToolCalls);
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
static bool SdkInstanceCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Id=SdkField(P,"id");bool List=!strcmp(Method,"instance.list"),Create=!strcmp(Method,"instance.create");
    if(!List&&(!SdkStateKey(Id)||strlen(Id)>64))return MdoApplicationError(E,"sdk_instance_arguments","A portable member id of at most 64 characters is required","instance",400,false);
    if(!strcmp(Method,"instance.step"))return SdkInstanceStep(C,P,R,E);
    MdoModuleCatalog* Modules=Create?MdoModuleCatalogSnapshot():NULL;MdoModuleAgentInfo Role={0};Role.Size=sizeof(Role);
    if(Create&&!Modules)return MdoApplicationError(E,"sdk_instance_role","Role catalog is unavailable","instance",503,false);
    char Key[128],Path[160];snprintf(Key,sizeof(Key),"sys.instance.%s",Id?Id:"");bool Ok=false,Changed=false;
    xrtMutexLock(SdkState.Lock);xvalue* Root=SdkStateLoad(C,P,Path,E);xvalue* Member=Root?SdkInstanceValue(Root,Key):NULL;
    if(!Root)goto done;
    if(List){*R=xrtValueArray();const xvalue* Records=SdkGet(Root,"records");for(size_t I=0;I<xrtValueCount(Records);I++){cstr K=SdkKey(Records,I);if(!strncmp(K,"sys.instance.",13))xrtValueArrayAppendNew(*R,SdkInstanceInfo(SdkInstanceValue(Root,K)));}Ok=*R!=NULL;goto done;}
    if(Create){
        cstr Agent=SdkField(P,"agent_id"),Model=SdkField(P,"model_id"),Permission=SdkField(P,"permission_profile");if(!Agent)Agent="mdo.default";if(!Permission)Permission="balanced";
        if(!MdoModuleCatalogAgentFind(Modules,Agent,&Role)||!(Role.Flags&(MDO_AGENT_MAIN|MDO_AGENT_SUBAGENT))||
            (strcmp(Permission,"read-only")&&strcmp(Permission,"balanced")&&strcmp(Permission,"full-access"))){MdoApplicationError(E,"sdk_instance_role","Select an available role and permission profile","instance",400,false);goto done;}
        xvalue* Intent=xrtValueObject();SdkPut(Intent,"id",Id);SdkPut(Intent,"agent_id",Agent);SdkPut(Intent,"model_id",Model);SdkPut(Intent,"permission_profile",Permission);
        SdkPut(Intent,"workspace",C->Workspace);SdkPut(Intent,"parent_session",C->SessionId);SdkPut(Intent,"owner",C->ExtensionId);char Hash[65];SdkMailboxHash(Intent,Hash);
        if(Member){Ok=SdkField(Member,"intent_hash")&&!strcmp(SdkField(Member,"intent_hash"),Hash);xrtValueRelease(Intent);if(Ok)*R=SdkInstanceInfo(Member);else MdoApplicationError(E,"sdk_identity_conflict","Member id belongs to different creation intent","instance",409,false);goto done;}
        size_t Count=0;const xvalue* Records=SdkGet(Root,"records");for(size_t I=0;I<xrtValueCount(Records);I++)if(!strncmp(SdkKey(Records,I),"sys.instance.",13))Count++;
        if(Count>=64){xrtValueRelease(Intent);MdoApplicationError(E,"sdk_instance_limit","Namespace retains 64 members; use an execution namespace","instance",413,false);goto done;}
        char Box[96];snprintf(Box,sizeof(Box),"instance.%s",Id);SdkPut(Intent,"mailbox",Box);SdkPut(Intent,"intent_hash",Hash);SdkPut(Intent,"state","idle");SdkPut(Intent,"epoch",SdkState.Epoch);
        Ok=SdkInstanceStore(Root,Key,Intent,E);if(Ok)*R=SdkInstanceInfo(Intent);xrtValueRelease(Intent);Changed=Ok;
    }else if(!Member){MdoApplicationError(E,"sdk_instance_missing","Member is unavailable","instance",404,false);}
    else if(!strcmp(Method,"instance.get")){*R=SdkInstanceInfo(Member);Ok=*R!=NULL;}
    else if(!strcmp(Method,"instance.send")){
        cstr Prompt=SdkField(P,"prompt");if(!Prompt||!Prompt[0]){MdoApplicationError(E,"sdk_instance_arguments","A nonempty prompt is required","instance",400,false);goto done;}
        xvalue* Current=SdkInstanceInfo(Member);bool Open=!strcmp(SdkField(Current,"state"),"idle")||!strcmp(SdkField(Current,"state"),"running");xrtValueRelease(Current);
        if(!Open){MdoApplicationError(E,"sdk_instance_not_open","Member is closed or requires review","instance",409,false);goto done;}
        xvalue* Q=xrtValueObject(),*Body=xrtValueObject();SdkPut(Body,"prompt",Prompt);SdkPut(Q,"mailbox",SdkField(Member,"mailbox"));SdkPut(Q,"message_id",SdkField(P,"message_id"));xrtValueObjectSetNew(Q,XRT_STR_LITERAL("body"),Body);
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
