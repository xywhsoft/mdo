# Commands

Each `<id>.c` is an independent, user-invoked `/<id>` command.
The first comment contains static metadata; listing commands never executes C.

```c
/* mdo-command
{"id":"example","description":"Show a result","effects":["read"]}
*/
#include "mdo/command.h"
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,
    const xvalue* Arguments, xvalue** Result)
{
    (void)Arguments;
    *Result = C->Host->Parse("{\"message\":\"Command finished\"}");
    return *Result ? MDO_RESULT_OK : MDO_RESULT_ERROR;
}
```

Search order: project `.mdo/commands`, personal `mdo-home/commands`, then
built-ins. An invalid or disabled override blocks fallback. The settings page
can edit personal commands and project overrides. Source is compiled only when
invoked, in a separate worker process, with a deadline and explicit cancellation.
Trusted native code is required: process isolation is not a security sandbox.

Ordinary arguments become an array of strings, including quoted empty strings
and Windows paths. A complete JSON array passes typed values. There is no shell
expansion. Results may contain `message`, `data`, `files` (display metadata), or
an unsent `draft`. Host operations use `C->Host->Call`; see `docs/commands.md`
in the source distribution and `include/mdo/command.h` for the public ABI.

Skills remain prompt workflows, invoked with `/skill:<id>` or `$<id>`.
Existing personal Markdown commands are migrated once to a Skill plus a C
wrapper. Original `.md` files remain backups; editing or deleting a generated
command does not make its backup active again.
