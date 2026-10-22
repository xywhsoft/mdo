/* mdo-command {"id":"sdk-example","description":"检查 Application SDK，并输出当前上下文","effects":["read"],"timeout_ms":30000} */
#include <mdo/command.h>
#include <mdo/sdk.h>
#include <stdio.h>
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* Args,xvalue** R){
    (void)Args;mdo_error_v1 E={0};mdo_result Code=mdoCall(C->Application,"sdk.context",NULL,R,&E);
    if(Code!=MDO_RESULT_OK)snprintf(C->Error,C->ErrorCapacity,"%s: %s",E.Code,E.Message);
    return Code;
}
