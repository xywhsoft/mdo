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
    if(State&&Identity&&SdkInstanceActive(Identity)>=0&&(!strcmp(State,"idle")||!strcmp(State,"requires_review")))SdkPut(R,"state","running");
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
#include "state_member_turn.inc.c"
static bool SdkInstanceStep(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    bool Resume=!strcmp(Method,"instance.resume"),Abandon=!strcmp(Method,"instance.abandon");
    if(Resume){bool Handled=false;bool Ok=MdoApplicationMemberResumeManaged(C,P,R,E,&Handled);if(Handled)return Ok;}
    xwork_resume_options ResumeOptions;xwork_recovery_decision Decisions[64];
    if(Resume&&!SdkMemberResumeOptions(P,&ResumeOptions,Decisions,E))return false;
    bool Ready=false;xwork_error Error;xworkErrorInit(&Error);
    MdoApplicationMemberTurn* T=MdoApplicationMemberTurnOpen(C,Method,P,NULL,&Ready,&Error,E);
    if(!T){
        if(E&&!strcmp(E->Code,"sdk_instance_no_work")){
            bool Found=false,Ok=MdoApplicationStateCall(C,"instance.get",P,R,E,&Found);
            if(Ok){xrtValueObjectSetNew(*R,XRT_STR_LITERAL("no_work"),xrtValueBool(true));memset(E,0,sizeof(*E));E->Size=sizeof(*E);}return Ok;
        }
        return false;
    }
    MdoAgentSession* Session=T->Session;MdoAgentRun* Run=NULL;xwork_run_result Result={0};xwork_result Status=XWORK_RESULT_ERROR;
    bool Recovery=false;
    if(Ready)Ready=MdoAgentSessionRecoveryRequired(Session,&Recovery,&Error);
    if(Ready&&Resume&&!Recovery&&SdkField(SdkGet(T->Message,"body"),"input_file")&&!SdkMemberRoutedCompleted(T)){
        Ready=false;Error.eCode=XWORK_ERROR_CONTEXT;snprintf(Error.sMessage,sizeof(Error.sMessage),"No completed turn belongs to this routed input. Review the member history and resubmit explicitly; previous replies will not be reused.");
    }
    if(Ready&&Abandon){uint64 Sequence=0,Finished=0;Ready=MdoAgentSessionLastSequence(Session,&Sequence,&Error)&&(!Recovery||MdoAgentSessionFinishInterrupted(Session,Sequence,&Finished,&Error));if(Ready)Status=XWORK_RESULT_CANCELLED;}
    else if(Ready&&Resume&&!Recovery){Ready=MdoAgentSessionDriveNative(Session,SdkMemberFinal,&Result,&Error);if(Ready)Status=XWORK_RESULT_OK;}
    else if(Ready){MdoAgentRunOptions Turn;MdoAgentRunOptionsInit(&Turn);Turn.Prompt=MdoApplicationMemberTurnPrompt(T);Turn.UserMessage=MdoApplicationMemberTurnUserMessage(T);Turn.Cancel=T->Cancel;Turn.Deadline=C->Deadline?C->Deadline:MDO_DEADLINE_NEVER;Turn.Resume=Resume;Turn.ResumeOptions=Resume?&ResumeOptions:NULL;
        Run=MdoAgentRunCreate(Session,&Turn,&Error);Ready=Run&&MdoAgentRunStart(Run,&Error);}
    if(Ready&&Run){
        for(;;){MdoAgentRunInfo Info={0};Info.Size=sizeof(Info);if(!MdoAgentRunGetInfo(Run,&Info))break;
            if(Info.Run.eState!=XWORK_RUN_CREATED&&Info.Run.eState!=XWORK_RUN_RUNNING){Status=MdoAgentRunWait(Run,MDO_DEADLINE_NEVER,&Result,&Error);break;}
            if(SdkCancelled(C))xrtCancelRequest(T->Cancel);xrtSleep(20);
        }
    }
    MdoAgentRunDestroy(Run);
    bool Ok=MdoApplicationMemberTurnFinish(T,Status,&Result,&Error,R,E);
    xworkRunResultUnit(&Result);MdoApplicationMemberTurnRelease(T);return Ok;
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
