#include "mdo/sdk.h"
static mdo_result Echo(void* U,const mdo_sdk_context_v1* C,
    const xvalue* P,xvalue** R,mdo_error_v1* E) {
    (void)U;(void)E;
    *R=P?C->Api->Values->Clone(P):C->Api->Values->Object();
    return *R?MDO_RESULT_OK:MDO_RESULT_ERROR;
}
static mdo_result Register(const mdo_host_services_v1* H,
    const mdo_registrar_v1* R,void** D,char* E,size_t N) {
    (void)H;(void)D;
    mdo_service_v1 Service={sizeof(Service),1,"example.plugin.echo",
        "Echo","{}","{}",MDO_TOOL_EFFECT_READ,NULL,Echo};
    return R->AddService(R->Context,&Service,E,N);
}
static const mdo_module_v1 Module={MDO_V1_HEADER(mdo_module_v1),
    MDO_PLUGIN_ID,"Native example","Owned service example",MDO_PLUGIN_VERSION,
    MDO_CAPABILITY_APPLICATION,NULL,0,Register,NULL};
const mdo_module_v1* mdoModuleEntry(void){return &Module;}
