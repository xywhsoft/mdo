/* mdo-command
{
  "id": "clear",
  "description": "清空当前会话历史",
  "effects": [
    "ui"
  ],
  "requires_session": true,
  "timeout_ms": 30000
}
*/
#include "mdo/command.h"
#include <stdio.h>

MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,
    const xvalue* A,xvalue** R)
{
    if(C->Host->Count(A)!=0) {
        snprintf(C->Error,C->ErrorCapacity,"Invalid arguments for /clear");
        return MDO_RESULT_ERROR;
    }
    xvalue* P=C->Host->Parse("{\"action\":\"clear\"}");
    if(!P)return MDO_RESULT_ERROR;
    mdo_result Code=mdoCommandCall(C,"ui.session",P,R);
    C->Host->Release(P);return Code;
}

