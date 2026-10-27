/* mdo-command
{
  "id": "new",
  "description": "新建会话",
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
        snprintf(C->Error,C->ErrorCapacity,"Invalid arguments for /new");
        return MDO_RESULT_ERROR;
    }
    return mdoCommandCall(C,"ui.new",A,R);
}

