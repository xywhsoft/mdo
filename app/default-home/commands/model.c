/* mdo-command
{
  "id": "model",
  "description": "切换模型；可选模型 ID",
  "effects": [
    "ui"
  ],
  "requires_session": false,
  "timeout_ms": 30000,
  "argument_hint": "[模型 ID]"
}
*/
#include "mdo/command.h"
#include <stdio.h>

MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,
    const xvalue* A,xvalue** R)
{
    if(C->Host->Count(A)>1) {
        snprintf(C->Error,C->ErrorCapacity,"Invalid arguments for /model");
        return MDO_RESULT_ERROR;
    }
    return mdoCommandCall(C,"ui.model",A,R);
}

