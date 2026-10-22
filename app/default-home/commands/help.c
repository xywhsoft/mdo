/* mdo-command
{
  "id": "help",
  "description": "查看命令和快捷键",
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
        snprintf(C->Error,C->ErrorCapacity,"Invalid arguments for /help");
        return MDO_RESULT_ERROR;
    }
    return mdoCommandCall(C,"ui.help",A,R);
}

