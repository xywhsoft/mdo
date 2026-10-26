#include "../../include/mdo/runs.h"

struct MdoApplicationRunRoute {
    mdo_sdk_context_v1 Context;
    MdoApplicationInvocation Invocation;
    MdoModuleCatalog* Modules;
    MdoApplicationMemberTurn* Member;
    xvalue* Facts;
    char Project[65],Session[129],Run[49],Workspace[2049];
};
void MdoApplicationRunRouteRelease(MdoApplicationRunRoute* Route){
    if(!Route)return;MdoApplicationMemberTurnRelease(Route->Member);MdoModuleCatalogRelease(Route->Modules);xrtValueRelease(Route->Facts);xrtFree(Route);
}
MdoApplicationMemberTurn* MdoApplicationRunRouteMember(MdoApplicationRunRoute* Route){return Route?Route->Member:NULL;}
bool MdoApplicationRunRoutePrepare(const MdoRunStartOptions* O,cstr Id,MdoSession* View,xcancel* Cancel,uint64 Deadline,
    MdoApplicationRunRoute** Out,xwork_error* Error){
    *Out=NULL;MdoSessionInfo Info={0};Info.Size=sizeof(Info);if(!MdoSessionGetInfo(View,&Info))return false;
    MdoApplicationRunRoute* R=xrtCalloc(1,sizeof(*R));if(!R)return false;
    snprintf(R->Project,sizeof(R->Project),"%s",O->ProjectId);snprintf(R->Session,sizeof(R->Session),"%s",O->SessionId);
    snprintf(R->Run,sizeof(R->Run),"%s",Id);snprintf(R->Workspace,sizeof(R->Workspace),"%s",Info.WorkspaceRoot);
    R->Modules=MdoModuleCatalogSnapshot();R->Invocation.Catalog=R->Modules;R->Invocation.Cancel=Cancel;
    MdoApplicationContext(&R->Context,&R->Invocation,"run-router",R->Run,R->Project,R->Session,R->Run,R->Workspace,Deadline);R->Context.Origin=O->Origin;
    R->Facts=xrtValueObject();SdkPut(R->Facts,"run_id",Id);SdkPut(R->Facts,"agent_id",Info.AgentId);SdkPut(R->Facts,"prompt",O->Prompt);
    xrtValueObjectSetNew(R->Facts,XRT_STR_LITERAL("resume"),xrtValueBool(O->Resume));
    *Out=R;mdo_error_v1 E={0};bool Ok=R->Modules&&R->Facts;
    if(Ok&&O->MemberRoute){
        xrtValueObjectSet(R->Facts,XRT_STR_LITERAL("route"),O->MemberRoute);
        xrtValueObjectSetNew(R->Facts,XRT_STR_LITERAL("external_settlement"),xrtValueBool(true));
    } else if(Ok)Ok=MdoApplicationHooks(&R->Context,"run.route",&R->Facts,&E);
    if(!Ok)goto failed;
    const xvalue* Route=SdkGet(R->Facts,"route");
    if(!Route){MdoApplicationRunRouteRelease(R);*Out=NULL;return true;}
    if(O->Resume&&!O->MemberRoute){MdoApplicationError(&E,"sdk_route_recovery","Use the member's explicit recovery decisions before sending another main input","run",409,false);goto failed;}
    cstr Namespace=SdkField(Route,"namespace"),Member=SdkField(Route,"id"),Message=SdkField(Route,"message_id");
    if(!SdkIdValid(Namespace)||strlen(Namespace)>128||!SdkStateKey(Member)||strlen(Member)>64||!SdkStateKey(Message)||strlen(Message)>128){
        MdoApplicationError(&E,"sdk_route_invalid","A route needs a namespace, persistent member and unique message identity","run",400,false);goto failed;}
    xvalue* P=xrtValueClone(Route);
    if(!O->Resume){xvalue *Input=xrtValueObject(),*Attachments=xrtValueArray();
        for(size_t I=0;I<O->AttachmentCount;I++)xrtValueArrayAppendNew(Attachments,SdkString(O->AttachmentIds[I]));
        SdkPut(Input,"prompt",O->Prompt);xrtValueObjectSetNew(Input,XRT_STR_LITERAL("attachments"),Attachments);xrtValueObjectSetNew(P,XRT_STR_LITERAL("_main_input"),Input);}
    bool Ready=false;R->Member=MdoApplicationMemberTurnOpen(&R->Context,O->Resume?"instance.resume":"instance.route",P,View,&Ready,Error,&E);xrtValueRelease(P);
    if(!R->Member||!Ready)goto failed;
    return true;
failed:
    if(Error&&Error->eCode==XWORK_ERROR_NONE){Error->eCode=E.Status==429?XWORK_ERROR_LIMIT:XWORK_ERROR_POLICY;
        snprintf(Error->sMessage,sizeof(Error->sMessage),"%s",E.Message[0]?E.Message:"Cannot prepare the interactive member route");}
    return false;
}
bool MdoApplicationRunRouteFinish(MdoApplicationRunRoute* R,xwork_result Status,const xwork_run_result* Result,
    const xwork_error* Error,bool Started){
    if(!R)return true;mdo_error_v1 E={0};xvalue* Member=NULL;bool Settled=true;
    if(R->Member)Settled=MdoApplicationMemberTurnFinish(R->Member,Status,Result,Error,&Member,&E);
    /* Completion must settle durable work even after Stop/deadline. It cannot
     * grant a new user decision; the original provenance is retained. */
    R->Context.Deadline=0;R->Invocation.Cancel=NULL;
    SdkPut(R->Facts,"state",Status==XWORK_RESULT_OK?"succeeded":Status==XWORK_RESULT_CANCELLED?"cancelled":Status==XWORK_RESULT_TIMEOUT?"timed_out":"failed");
    xrtValueObjectSetNew(R->Facts,XRT_STR_LITERAL("started"),xrtValueBool(Started));
    xrtValueObjectSetNew(R->Facts,XRT_STR_LITERAL("settled"),xrtValueBool(Settled));
    if(Member)xrtValueObjectSet(R->Facts,XRT_STR_LITERAL("member"),Member);
    SdkPut(R->Facts,"error",Error?Error->sMessage:"");
    mdo_error_v1 Hook={0};bool Notified=MdoApplicationHooks(&R->Context,"run.finished",&R->Facts,&Hook);
    xrtValueRelease(Member);return Settled&&Notified;
}
bool MdoApplicationMemberResumeManaged(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E,bool* Handled){
    *Handled=false;cstr Id=SdkField(P,"id");if(!Id)return false;
    char Key[128],Path[160];snprintf(Key,sizeof(Key),"sys.instance.%s",Id);
    xrtMutexLock(SdkState.Lock);xvalue* Root=SdkStateLoad(C,P,Path,E),*Member=Root?SdkInstanceValue(Root,Key):NULL;
    xvalue* Message=NULL;bool Changed=false;bool Ok=Root&&Member;
    if(Ok){xvalue* Q=xrtValueObject();SdkPut(Q,"mailbox",SdkField(Member,"mailbox"));SdkPut(Q,"message_id",SdkField(Member,"active_message"));
        Ok=SdkMailboxApply(C,"mailbox.get",Q,Root,&Message,E,&Changed);xrtValueRelease(Q);}
    bool Routed=Ok&&SdkField(SdkGet(Message,"body"),"input_file")!=NULL;
    xrtValueRelease(Message);xrtValueRelease(Root);xrtMutexUnlock(SdkState.Lock);
    if(!Routed)return true;
    *Handled=true;xvalue* View=NULL;if(!SdkMemberRecovery(C,P,&View,E))return false;
    bool Required=false;xrtValueGetBool(SdkGet(View,"resume_required"),&Required);xrtValueRelease(View);
    if(!Required){*Handled=false;return true;}
    cstr Token=SdkField(P,"expected_recovery_token");if(!Token||strlen(Token)!=64)return MdoApplicationError(E,"sdk_instance_recovery_token","Inspect this main speaker's recovery and supply its accepted token","instance",400,false);
    xwork_resume_options Resume;xwork_recovery_decision Decisions[64];if(!SdkMemberResumeOptions(P,&Resume,Decisions,E))return false;
    MdoRunStartOptions O;MdoRunStartOptionsInit(&O);O.ProjectId=C->ProjectId;O.SessionId=C->SessionId;O.Resume=true;O.RecoveryToken=Token;
    O.MemberRoute=P;O.ResumeOptions=&Resume;O.Origin=mdoCallOrigin(C);
    MdoRunInfo Info={0};Info.Size=sizeof(Info);xwork_error Error;xworkErrorInit(&Error);bool MayHaveExecuted=false;
    if(!MdoRunStartWithOutcome(&O,&Info,&Error,&MayHaveExecuted))return MdoApplicationError(E,"sdk_instance_main_run",Error.sMessage,"instance",Error.eCode==XWORK_ERROR_LIMIT?429:409,MayHaveExecuted);
    for(;;){
        if(SdkCancelled(C))MdoRunCancel(Info.Id,NULL,&Error);
        MdoRunInfo Current={0};Current.Size=sizeof(Current);
        if(!MdoRunGetInfo(Info.Id,&Current))return MdoApplicationError(E,"sdk_instance_main_run","Managed recovery receipt is unavailable; inspect the member before repeating","instance",410,true);
        if(Current.Terminal)break;xrtSleep(20);
    }
    bool Found=false;Ok=MdoApplicationStateCall(C,"instance.get",P,R,E,&Found);if(Ok)SdkPut(*R,"managed_run_id",Info.Id);return Ok;
}
