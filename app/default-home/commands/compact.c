/* mdo-command
{"id":"compact","description":"压缩当前会话的模型上下文，保留原始会话记录","effects":["model"],"requires_session":true,"timeout_ms":180000}
*/
#include "mdo/command.h"
#include <stdio.h>
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R)
{
    if(C->Host->Count(A)){snprintf(C->Error,C->ErrorCapacity,"/compact does not accept arguments");return MDO_RESULT_ERROR;}
    return mdoCommandCall(C,"session.compact",NULL,R);
}
