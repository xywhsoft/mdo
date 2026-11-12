#include <mdo/agent.h>
#include <stdio.h>

/* Demonstrates private configuration access. It does not contact a vendor. */
static mdo_result Execute(void* User,const mdo_tool_context_v1* C,const char* Args,
    mdo_result_writer_v1* Writer,char* Error,size_t Capacity){
    (void)User;(void)Args;
    const xvalue* Settings=mdoToolSettings(C);
    xstrview Key={0},Endpoint={0},Model={0};bool Enabled=false;
    if(!Settings||!xrtValueGetString(xrtValueObjectGet(Settings,XRT_STR_LITERAL("api_key")),&Key)||
       !xrtValueGetString(xrtValueObjectGet(Settings,XRT_STR_LITERAL("api_url")),&Endpoint)||
       !xrtValueGetString(xrtValueObjectGet(Settings,XRT_STR_LITERAL("model")),&Model)){
        snprintf(Error,Capacity,"Complete this extension's configuration first");return MDO_RESULT_ERROR;
    }
    xrtValueGetBool(xrtValueObjectGet(Settings,XRT_STR_LITERAL("enabled")),&Enabled);
    /* Pass Key to an authenticated HTTP header in your actual image adapter.
     * Do not emit it to the model, logs, errors, URLs or process arguments. */
    bool Ok=Writer->WriteText(Writer->Context,Enabled&&Key.Size&&Endpoint.Size&&Model.Size?
        "Image service configuration is ready.":"Image service is disabled or incomplete.");
    return Ok&&Writer->SetSuccess(Writer->Context,true)?MDO_RESULT_OK:MDO_RESULT_ERROR;
}
static mdo_result Register(const mdo_host_services_v1* Host,const mdo_registrar_v1* R,
    void** Data,char* Error,size_t Capacity){
    (void)Host;(void)Data;mdo_tool_v1 Tool={0};Tool.Size=sizeof(Tool);Tool.AbiVersion=1;
    Tool.Id="example_image_configuration";Tool.Name="Check image API configuration";
    Tool.Description="Check whether this extension's private image API configuration is ready; makes no network request.";
    Tool.ParametersJson="{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}";
    Tool.Effects=MDO_TOOL_EFFECT_READ;Tool.MaxResultBytes=1024;Tool.Execute=Execute;
    return R->AddTool(R->Context,&Tool,Error,Capacity);
}
MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void){
    static const mdo_module_v1 Module={MDO_V1_HEADER(mdo_module_v1),"example.image-configuration",
        "Image configuration","Private dictionary example","1.0.0",MDO_CAPABILITY_APPLICATION,NULL,0,Register,NULL};
    return &Module;
}
