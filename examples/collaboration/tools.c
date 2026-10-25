/* Optional Agent tools. Copy into Home/modules/tools/ only when wanted. */
#include <mdo/instance.h>
#include <mdo/work.h>
#include <stdio.h>
#include <string.h>

static mdo_result Execute(void* User,const mdo_tool_context_v1* Tool,
    const char* Json,mdo_result_writer_v1* Writer,char* Error,size_t Capacity){
    const mdo_sdk_context_v1* C=Tool->Application;
    if(!C||!C->Api){snprintf(Error,Capacity,"Application SDK is unavailable");return MDO_RESULT_ERROR;}
    const mdo_values_v1* V=C->Api->Values;
    xvalue* Input=V->Parse(Json);const char* Action=V->Text(V->Get(Input,"action"));
    bool Member=User!=NULL;const char* const MemberActions[]={"create","get","list","send","run","result","wait","cancel","close","resume","abandon"};
    const char* const WorkActions[]={"create","get","list","claim","complete","release","cancel"};
    const char* const* Actions=Member?MemberActions:WorkActions;
    size_t Count=Member?sizeof(MemberActions)/sizeof(*MemberActions):sizeof(WorkActions)/sizeof(*WorkActions);bool Found=false;
    for(size_t I=0;Action&&I<Count;I++)if(!strcmp(Action,Actions[I]))Found=true;
    const xvalue* Arguments=V->Get(Input,"args");
    if(!Found||(Arguments&&V->Kind(Arguments)!=MDO_VALUE_OBJECT)){V->Release(Input);snprintf(Error,Capacity,"Select a listed action and supply args as an object");return MDO_RESULT_ERROR;}
    xvalue* P=Arguments?V->Clone(Arguments):V->Object();char Namespace[160];
    /* Ordinary user sessions get a private collaboration group. Persistent
     * members already carry this group, so all their SDK calls inherit it. */
    if(!V->Get(P,"namespace")&&(!C->Namespace||!C->Namespace[0])&&C->SessionId&&C->SessionId[0]){
        snprintf(Namespace,sizeof(Namespace),"session.%s",C->SessionId);xvalue* S=V->String(Namespace);V->Set(P,"namespace",S);V->Release(S);
    }
    char Method[64];snprintf(Method,sizeof(Method),"%s.%s",Member?"instance":"work",Member&&!strcmp(Action,"run")?"step":Action);
    xvalue* Result=NULL;mdo_error_v1 Failure={0};mdo_result Code=mdoCall(C,Method,P,&Result,&Failure);
    if(Code!=MDO_RESULT_OK){
        V->Release(Result);Result=V->Object();xvalue* S=V->String(Failure.Code);V->Set(Result,"error",S);V->Release(S);
        S=V->String(Failure.Message);V->Set(Result,"message",S);V->Release(S);
    }
    char* Text=Result?V->Json(Result):NULL;
    bool Ok=Text&&Writer->WriteText(Writer->Context,Text)&&Writer->SetSuccess(Writer->Context,Code==MDO_RESULT_OK);
    V->Free(Text);V->Release(Result);V->Release(P);V->Release(Input);return Ok?MDO_RESULT_OK:MDO_RESULT_ERROR;
}
static mdo_result Register(const mdo_host_services_v1* H,const mdo_registrar_v1* R,void** Data,char* E,size_t N){
    (void)H;(void)Data;
    mdo_tool_v1 T={0};T.Size=sizeof(T);T.AbiVersion=1;T.Id="members";T.Name="Members";
    T.Description="Use a persistent worker: create once with id/agent_id, send a prompt with a unique message_id, then run. Reuse the worker for follow-up turns. Read result by message_id. Resume/abandon only when explicitly intended, using the revision and active_message returned by get.";
    T.ParametersJson="{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"create\",\"get\",\"list\",\"send\",\"run\",\"result\",\"wait\",\"cancel\",\"close\",\"resume\",\"abandon\"]},\"args\":{\"type\":\"object\"}},\"required\":[\"action\"]}";
    T.Effects=MDO_TOOL_EFFECT_AGENT_DELEGATION;T.PermissionResource="collaboration";T.Flags=MDO_TOOL_PARALLEL_SAFE;T.MaxResultBytes=49152;T.Execute=Execute;T.UserData=(void*)1;
    if(R->AddTool(R->Context,&T,E,N)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;
    T.Id="shared_work";T.Name="Shared work";T.UserData=NULL;
    T.Description="Coordinate shared tasks. Create with id/title and optional existing depends_on IDs. List ready tasks. Claim with expected_revision/worker/claim_id before doing work; keep its claim_token. Complete with claim_token/result, or explicitly release to retry. A cancelled or reassigned claim rejects late results.";
    T.ParametersJson="{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"create\",\"get\",\"list\",\"claim\",\"complete\",\"release\",\"cancel\"]},\"args\":{\"type\":\"object\"}},\"required\":[\"action\"]}";
    return R->AddTool(R->Context,&T,E,N);
}
MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void){
    static const mdo_module_v1 M={sizeof(M),1,"example.collaboration","Collaboration tools","Optional members and shared tasks","1",MDO_CAPABILITY_APPLICATION,NULL,0,Register,NULL};return &M;
}
