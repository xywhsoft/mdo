/* mdo-command
{
  "id": "mcp",
  "description": "读取 MCP 提示词并填入输入框",
  "effects": [
    "ui"
  ],
  "requires_session": false,
  "timeout_ms": 30000,
  "argument_hint": "服务器 提示词 {\"参数\":\"值\"}；或 [\"服务器\",\"提示词\",{\"参数\":\"值\"}]"
}
*/
#include "mdo/command.h"
#include <stdio.h>

MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,
    const xvalue* A,xvalue** R)
{
    if(false) {
        snprintf(C->Error,C->ErrorCapacity,"Invalid arguments for /mcp");
        return MDO_RESULT_ERROR;
    }
    return mdoCommandCall(C,"ui.mcp",A,R);
}

