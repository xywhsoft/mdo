#include "internal.h"
#include "../../include/mdo/sessions.h"
#include "../../include/mdo/approvals.h"
typedef struct SdkToolExecution {const mdo_sdk_context_v1* Caller;const xvalue* Params;xvalue* Result;} SdkToolExecution;
static bool SdkToolDrive(void* User,const MdoAgentNativeContext* Native,xwork_error* Error){
    SdkToolExecution* Execution=User;const mdo_sdk_context_v1* C=Execution->Caller;
    cstr Tool=SdkField(Execution->Params,"tool");char* Args=SdkGet(Execution->Params,"arguments")?SdkJson(SdkGet(Execution->Params,"arguments")):xrtStrDup("{}");
    xllm_tool_call Call={(char*)C->InvocationId,(char*)Tool,Args};xllm_executor_ctx Context={0};MdoApplicationInvocation* I=C->Opaque;Context.pCancel=I?I->Cancel:NULL;Context.uDeadline=C->Deadline;
    xllm_executor_result Result={0};bool Ok=Args&&Native->Executor->pExecute(Native->Executor->pUserData,&Call,&Context,&Result);xrtFree(Args);
    if(Ok){Execution->Result=xrtValueObject();SdkPut(Execution->Result,"text",Result.sContent);xrtValueObjectSetNew(Execution->Result,XRT_STR_LITERAL("success"),xrtValueBool(Result.bSuccess));xrtValueObjectSetNew(Execution->Result,XRT_STR_LITERAL("effect_applied"),xrtValueBool((Result.uFlags&XLLM_EXECUTOR_RESULT_EFFECT_APPLIED)!=0));
        if(Result.pImageBytes){xvalue* Image=xrtValueObject();SdkByteValue(Image,Result.pImageBytes,Result.iImageSize);SdkPut(Image,"media_type",Result.sImageMime);xrtValueObjectSetNew(Execution->Result,XRT_STR_LITERAL("image"),Image);}}
    else {xworkErrorInit(Error);Error->eCode=XWORK_ERROR_TOOL;snprintf(Error->sMessage,sizeof(Error->sMessage),"Managed tool executor could not complete the call");}
    return Ok;
}
static bool SdkToolCall(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Project=SdkField(P,"project"),SessionId=SdkField(P,"session"),Tool=SdkField(P,"tool");if(!Project)Project=C->ProjectId;if(!SessionId)SessionId=C->SessionId;
    if(!Project||!Project[0]||!SessionId||!SessionId[0]||!Tool||!Tool[0])return MdoApplicationError(E,"sdk_arguments_invalid","Select a product project/session and a tool ID","tool",400,false);
    MdoSessionRuntimeOptions Options;MdoSessionRuntimeOptionsInit(&Options);MdoApplicationInvocation* I=C->Opaque;Options.Cancel=I?I->Cancel:NULL;Options.Deadline=C->Deadline;Options.OnPermission=MdoApprovalOnPermission;Options.UseRunPermissionScope=true;
    xwork_error Error;MdoSession* Session=MdoSessionOpen(Project,SessionId,&Options,&Error);MdoAgentSession* Agent=Session?MdoSessionAgentRef(Session):NULL;
    bool Required=false,Ready=Agent&&MdoAgentSessionRecoveryRequired(Agent,&Required,&Error);
    if(Ready&&Required){Ready=false;xworkErrorInit(&Error);Error.eCode=XWORK_ERROR_CONTEXT;snprintf(Error.sMessage,sizeof(Error.sMessage),"Resolve interrupted tool calls before executing another managed tool");}
    SdkToolExecution Execution={C,P,NULL};bool Ok=Ready&&MdoAgentSessionDriveNative(Agent,SdkToolDrive,&Execution,&Error);
    MdoAgentSessionRelease(Agent);MdoSessionRelease(Session);*R=Execution.Result;
    return Ok||MdoApplicationError(E,"sdk_tool_execution_failed",Error.sMessage,"tool",409,Execution.Result!=NULL);
}
static bool SdkCompactCall(const mdo_sdk_context_v1* C,xvalue** R,mdo_error_v1* E){
    MdoSessionRuntimeOptions Options;MdoSessionRuntimeOptionsInit(&Options);MdoApplicationInvocation* I=C->Opaque;Options.Cancel=I?I->Cancel:NULL;Options.Deadline=C->Deadline;
    xwork_error Error;MdoSession* Session=MdoSessionOpen(C->ProjectId,C->SessionId,&Options,&Error);bool Ok=Session&&MdoSessionCompact(Session,&Error);MdoSessionRelease(Session);
    if(!Ok)return MdoApplicationError(E,"sdk_compact_failed",Error.sMessage,"context",SdkCancelled(C)?499:409,false);
    if(I&&I->Actions){xvalue* P=xrtValueObject(),*Result=NULL;SdkPut(P,"action","ui.refresh");bool Found=false;Ok=MdoApplicationUiCall(C,"ui.invoke",P,&Result,E,&Found);xrtValueRelease(Result);xrtValueRelease(P);if(!Ok){E->MayHaveCompleted=true;return false;}}
    *R=xrtValueObject();SdkPut(*R,"message","上下文已压缩，原始会话记录仍可查看。");return true;
}
