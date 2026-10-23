/* mdo-command {"id":"workbench-plan","description":"创建 C/JS 共用的计划资源；可选参数为明确的 UI client ID","effects":["workspace-write","ui"],"timeout_ms":30000} */
#include <mdo/command.h>
#include <mdo/workbench.h>
#include <stdio.h>

MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* Command,
    const xvalue* Args,xvalue** Result)
{
    const mdo_sdk_context_v1* C=Command->Application;
    const mdo_values_v1* V=C->Api->Values;
    mdo_error_v1 Error={0};xvalue *Params=NULL,*Resource=NULL,*Panel=NULL,*Type=NULL;
    Params=V->Parse("{\"revision\":0,\"kind\":\"plan\",\"title\":\"C 扩展创建的计划\",\"value\":{\"items\":[{\"text\":\"C 创建资源\",\"done\":true},{\"text\":\"JS 展示和编辑同一份资源\",\"done\":false}]}}");
    mdo_result Code=mdoResourcePut(C,Params,&Resource,&Error);V->Release(Params);
    if(Code!=MDO_RESULT_OK){snprintf(Command->Error,Command->ErrorCapacity,"%s: %s",Error.Code,Error.Message);return Code;}
    *Result=V->Object();V->Set(*Result,"resource",Resource);
    const char* Client=V->Text(V->At(Args,0));
    if(Client&&Client[0]){
        Params=V->Parse("{\"id\":\"example.workbench-plan\",\"title\":\"C 扩展计划\",\"renderer\":\"plan\",\"scope\":\"session\"}");
        Code=mdoPanelRegister(C,Params,&Type,&Error);V->Release(Params);V->Release(Type);
        if(Code==MDO_RESULT_OK){
            Params=V->Parse("{\"type\":\"example.workbench-plan\",\"key\":\"main\",\"reveal\":\"activate\"}");
            xvalue* ClientValue=V->String(Client);V->Set(Params,"client",ClientValue);V->Release(ClientValue);
            xvalue* Ref=V->Object();V->Set(Ref,"id",V->Get(Resource,"id"));V->Set(Params,"resource",Ref);V->Release(Ref);
            Code=mdoPanelOpen(C,Params,&Panel,&Error);V->Release(Params);
            if(Code==MDO_RESULT_OK)V->Set(*Result,"panel",Panel);V->Release(Panel);
        }
        if(Code!=MDO_RESULT_OK){xvalue* Message=V->String(Error.Message);V->Set(*Result,"presentation_error",Message);V->Release(Message);}
    }
    V->Release(Resource);
    /* A missing viewer never invalidates the successfully saved resource. */
    return MDO_RESULT_OK;
}
