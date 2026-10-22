#include <mdo/sdk.h>
#include <mdo/native/agents.h>
#include <mdo/native/sessions.h>
#include <string.h>

typedef struct Inspect {const mdo_values_v1* Values;xvalue* Result;} Inspect;
static bool InspectAgent(void* User,const MdoAgentNativeContext* Native,xwork_error* Error){
    (void)Error;Inspect* Data=User;xllm_request Request;xllmRequestInit(&Request);
    bool Ok=Native->Executor->pListTools(Native->Executor->pUserData,&Request);
    if(Ok){Data->Result=Data->Values->Object();xvalue* Count=Data->Values->UInt(Request.iToolCount);Data->Values->Set(Data->Result,"tools",Count);Data->Values->Release(Count);}
    /* A custom loop can use History, Complete, and Executor here before
     * returning. This read-only example performs no model call. */
    xllmRequestUnit(&Request);return Ok;
}
static mdo_result Execute(void* User,const mdo_sdk_context_v1* C,const xvalue* Params,xvalue** R,mdo_error_v1* E){
    (void)User;(void)E;const mdo_values_v1* V=C->Api->Values;
    const char* Project=V->Text(V->Get(Params,"project")),*Id=V->Text(V->Get(Params,"session"));
    if(!Project)Project=C->ProjectId;if(!Id)Id=C->SessionId;
    MdoSessionRuntimeOptions Options;MdoSessionRuntimeOptionsInit(&Options);Options.Deadline=C->Deadline;
    xwork_error Error;MdoSession* Session=MdoSessionOpen(Project,Id,&Options,&Error);MdoAgentSession* Agent=Session?MdoSessionAgentRef(Session):NULL;
    Inspect Data={V,NULL};bool Ok=Agent&&MdoAgentSessionDriveNative(Agent,InspectAgent,&Data,&Error);
    MdoAgentSessionRelease(Agent);MdoSessionRelease(Session);*R=Data.Result;return Ok?MDO_RESULT_OK:MDO_RESULT_ERROR;
}
static mdo_result Register(const mdo_host_services_v1* Host,const mdo_registrar_v1* R,void** Data,char* Error,size_t Capacity){
    (void)Host;(void)Data;mdo_service_v1 Service={0};Service.Size=sizeof(Service);Service.Version=MDO_SDK_VERSION;
    Service.Id="example.agent.inspect";Service.Description="Inspect a session through the native Agent driver";Service.InputSchema="{\"type\":\"object\"}";Service.OutputSchema="{\"type\":\"object\"}";Service.Effects=MDO_TOOL_EFFECT_READ;Service.Execute=Execute;
    return R->AddService(R->Context,&Service,Error,Capacity);
}
static const mdo_module_v1 Module={MDO_V1_HEADER(mdo_module_v1),"example.application","Application SDK example","Native driver example","1",MDO_CAPABILITY_APPLICATION,NULL,0,Register,NULL,NULL};
MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void){return &Module;}
