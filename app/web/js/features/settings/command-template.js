export function newCommand(id = "my-command") {
  return `/* mdo-command
${JSON.stringify({ id, description: "自定义命令", effects: ["ui"], timeout_ms: 30000 }, null, 2)}
*/
#include "mdo/command.h"

MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,
    const xvalue* Arguments, xvalue** Result)
{
    (void)Arguments;
    return mdoCommandCall(C, "ui.help", NULL, Result);
}
`;
}
