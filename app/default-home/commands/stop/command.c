/* mdo-command
{
  "id": "stop",
  "description": "停止运行中的任务或命令",
  "effects": [
    "ui"
  ],
  "requires_session": false,
  "timeout_ms": 30000
}
*/
#include "mdo/command.h"
#include <stdio.h>

MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,
    const xvalue* A,xvalue** R)
{
    if(C->Host->Count(A)!=0) {
        snprintf(C->Error,C->ErrorCapacity,"Invalid arguments for /stop");
        return MDO_RESULT_ERROR;
    }
    return mdoCommandCall(C,"ui.stop",A,R);
}

