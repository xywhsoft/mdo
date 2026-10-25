#ifndef MDO_PUBLIC_AGENT_H
#define MDO_PUBLIC_AGENT_H

#include <xsbase.h>
#include "module.h"
#include "sdk.h"
#include <xwork.h>

/* Optional native conveniences. The standard module ABI itself remains free
 * of xrt/xwork layout dependencies. All returned pointers are callback-local. */
static inline xvalue* mdoAgentState(const mdo_agent_context_v1* Context)
{ return Context ? (xvalue*)Context->State : NULL; }
static inline xvalue* mdoAgentContextPlan(const mdo_agent_context_v1* Context)
{ return Context ? (xvalue*)Context->ContextPlan : NULL; }
static inline xwork_agent_extension_context* mdoAgentNative(const mdo_agent_context_v1* Context)
{ return Context ? (xwork_agent_extension_context*)Context->Native : NULL; }
static inline xvalue* mdoAgentContextSource(const mdo_agent_context_v1* Context,const char* Id)
{
    xvalue* Sources=xrtValueObjectGet(mdoAgentContextPlan(Context),xrtStrView("sources"));
    for(size_t I=0;I<xrtValueCount(Sources);I++){
        xvalue* Source=xrtValueArrayGet(Sources,I);xstrview Name;
        if(xrtValueGetString(xrtValueObjectGet(Source,xrtStrView("id")),&Name) && xrtStrEqual(Name,xrtStrView(Id)))return Source;
    }
    return NULL;
}
static inline bool mdoAgentSetContextText(const mdo_agent_context_v1* Context,const char* Id,const char* Text)
{
    xvalue* Source=mdoAgentContextSource(Context,Id);
    return Source && xrtValueObjectSetNew(Source,xrtStrView("text"),xrtValueString(xrtStrView(Text)));
}
/* Called only inside Program.Run. It reuses the claimed, managed executor,
 * ordinary recovery, tool policy, streaming, persistence and cancellation. */
static inline mdo_result mdoAgentRunDefault(const mdo_agent_context_v1* Context,xwork_error* Error)
{
    xwork_agent_extension_context* Native=mdoAgentNative(Context);
    if(!Native || !Native->DefaultRun)return MDO_RESULT_ERROR;
    return (mdo_result)Native->DefaultRun(Native->pAgent,Native->sPrompt,Native->pUserMessage,
        Native->bResume,Native->pResumeOptions,Native->pResult,Error);
}
static inline const xwork_event* mdoAgentEvent(const mdo_agent_context_v1* Context)
{return Context?(const xwork_event*)Context->Event:NULL;}
static inline const xwork_error* mdoAgentError(const mdo_agent_context_v1* Context)
{return Context?(const xwork_error*)Context->Error:NULL;}
/* A one-file role needs only a descriptor and this export. Use the ordinary
 * module entry instead when registering multiple roles, tools or services. */
#define MDO_AGENT_MODULE(Descriptor) \
    static mdo_result mdo_agent_register(const mdo_host_services_v1* Host,const mdo_registrar_v1* Registrar,void** Data,char* Error,size_t Capacity) { \
        (void)Host;(void)Data;return Registrar->AddAgent(Registrar->Context,&(Descriptor),Error,Capacity); \
    } \
    MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void) { \
        static mdo_module_v1 Module;Module.Size=sizeof(Module);Module.AbiVersion=MDO_MODULE_ABI_VERSION; \
        Module.Id=(Descriptor).Id;Module.Name=(Descriptor).Name;Module.Description=(Descriptor).Description; \
        Module.Version="1.0.0";Module.Register=mdo_agent_register;return &Module; \
    }
#endif
