# 本地工具与 Agent 配置

入口均在 **设置 → 扩展能力**，与 Skill、MCP、命令同级。

## 工具管理

工具页只有两个分类：

- **mdo 内建工具**：只读清单，显示用途和可用状态。联网工具显示“会员”胶囊；只有登录且开启“允许联机搜索”时才进入模型可用工具。
- **自定义工具**：新建、导入 `.c`、编辑源文件、导出、启停和删除。一个文件可注册多个工具；启停作用于整个文件。

文件保存于 `mdo-home/tools/<id>.c`。ID 为 1–64 个小写字母、数字、点、短横线或下划线，不允许路径分隔符、连续点和 Windows 保留名。清单保存在源码 `app/include/mdo/tool_catalog.h`，没有另一份用户可改写的内建工具注册表。

点击“新建”会生成可直接编译的 JSON 回显工具。先填写文件 ID，再修改 C 实现；未改过的模板会自动跟随文件 ID 更新工具名。建议工具 ID 使用 `user.<id>`，模块 ID 使用 `user.tools.<id>`。文件 ID 和导出的工具 ID 可以不同，模型和 Agent 工具配置使用导出的工具 ID。内建 ID 及兼容导入别名（如 `Read`、`Bash`）保留，避免导入 Agent 时指向不同工具。

保存通过现有 TCC 与 Module ABI 编译和注册，不要求用户另装编译器，Windows 和 Android 使用相同源文件。单个管理文件最多 128 KiB，JSON 请求另有 256 KiB 上限。C 是进程内可信扩展，并非隔离沙箱；只导入已审阅的代码。

## 命令参数与权限错误

`exec` 和 `spawn` 直接接收 `argv` 字符串数组，不经过 shell。
`argv[0]` 必须是非空的程序名，后续参数可以是空字符串；例如
`["ssh-keygen", "-t", "ed25519", "-N", "", "-f", "id_ed25519"]`
中的空密码会作为一个独立参数传给程序。命令预览将空参数显示为 `""`。
数组最多 256 项，参数不能包含 NUL，权限审核中的命令预览最多 4096 字节。

工具结果区分以下失败原因，单次调用与批量调用使用相同规则：

- `invalid tool arguments: ...`：参数不合法，尚未执行，也不会因此弹出批准请求。
- `tool execution denied by approval policy`：当前权限配置或用户的批准决定拒绝执行。
- `tool execution denied by permission policy: ...`：权限资源规则拒绝，保留具体原因。
- `tool permission preparation failed: ...`：权限描述准备失败，保留内部错误原因。

“完全访问”使用自动批准，合法空参数无需额外批准；普通文件路径可指向工作区外，
`read/ls/glob/grep/write/edit` 和进程工作目录使用相同访问范围。系统文件权限仍然生效。
其他权限模式保留工作区和显式挂载目录的范围；声明为只读的 Agent 不因选择完全访问而获得写入权限或扩大范围。
子任务继承父任务的文件范围，同时保留自身工具权限上限。`@alias/` 路径始终限定在对应挂载目录内。

`read` 对可验证的常见路径格式错误在一次工具调用内纠正：匹配的外围引号/反引号、
首尾空白、换行后附带的工具 XML 尾标记。原路径存在时优先读取原路径；修正后的文件必须真实存在。
缺失的裸记忆文件名和旧 `mdo-home/data/memory/` 路径只有在已挂载记忆中唯一匹配时才恢复，
结果会明确记录原路径和实际路径。真正的缺失、歧义、NUL 和系统权限错误继续准确报告。
分页数值超范围会限幅，`start_line=0` 按第 1 行处理；同时提供行号和字节游标时优先游标，
超过文件末尾的游标返回 EOF。文本按页读取，不再因整个文件大于 64 MiB 被拒绝；图片仍限制 64 MiB。
UTF-8 字节游标落在多字节字符内部时，会在验证字符后最多回退 3 字节，并报告实际游标。
真正的无效编码与含 NUL 的二进制数据分别报告，不再将中文游标问题误报为文件损坏。

普通工具失败交回模型纠正，连续 10 次失败才结束任务；任一工具成功后清零。
成功的重复读取或轮询不触发重复批次保护。一个批次已执行的所有结果先保存，
再判断失败上限，避免续跑时重复执行已完成的调用。参数、IO、工具内部错误、单次
工具超时、结果超限和无效编码不会单独终止整个任务。取消、明确的任务期限、
内存不足、持久化/上下文损坏和未知副作用收据仍保留中断与人工恢复规则。

进程在权限审核通过后仍可能启动、捕获或停止失败。这类结果以
`process_failure: stage=... reason=...` 开头，保留程序、工作目录和原生原因链中的
操作、系统错误码及系统说明，例如 `not_found`、`permission_denied`、`unsupported`。
诊断不附带命令参数、环境变量或原生错误的 Data 字段；没有原生原因时明确报告未知。

`edit` 的替换仍要求精确匹配。零匹配时最多给出三个相似锚点附近的上下文；
多匹配时给出真实匹配的行号，支持跨行文本。候选只用于修正参数，不会自动应用。
提示中制表符显示为 `\t`，行尾空格另行计数，过长行会注明截断。
任意一个编辑失败时，整批不会写入文件。

## Windows Python 项目导入

Python 工具包修订 3 使用 CPython 自身的标准命令行路径初始化：脚本可以导入
同目录模块，`-c`、`-m` 和标准输入可以导入当前目录模块，调用的 `env` 字符串数组
中可以用 `"PYTHONPATH=<目录>"` 指定额外模块目录。打包前移除 embeddable 原始素材里的 `python*._pth`，
其强制隔离会忽略上述目录；标准库 ZIP 和扩展模块保持随包提供。
显式使用 `-I`、`-E`、`-P` 时，仍遵守对应的标准隔离选项。
包清单根据转换后的实际文件重新生成，旧安装需要升级 Python 工具包才能获得修复。
此调整不包含 pip 或第三方依赖。

## 回调约定

完整公共头文件是 `include/mdo/module.h`，编译时可直接 `#include "mdo/module.h"`。注册链只有三步：

1. `mdoModuleEntry()` 返回模块描述符，其中 `Register` 指向注册函数。
2. `Register` 通过 `Registrar->AddTool` 添加工具描述符，声明 ID、说明、JSON 参数 schema、影响范围和 `Execute`。
3. 模型调用时，mdo 自动转发 JSON 参数及调用上下文到 `Execute`，收集结果并回送模型，同时沿用权限审核、取消、结果大小限制和运行记录。

```c
static mdo_result Execute(void* UserData,
    const mdo_tool_context_v1* Context, const char* ArgumentsJson,
    mdo_result_writer_v1* Writer, char* Error, size_t ErrorCapacity)
{
    (void)UserData; (void)Context; (void)Error; (void)ErrorCapacity;
    if (!Writer->WriteText(Writer->Context, ArgumentsJson) ||
        !Writer->SetSuccess(Writer->Context, true)) return MDO_RESULT_ERROR;
    return MDO_RESULT_OK;
}
```

参数按声明的 schema 暴露给模型，回调仍须检查所使用的参数值。结果 writer 会复制内容；参数、上下文和 writer 仅在回调期间有效，不可留给异步线程继续使用。`MaxResultBytes` 控制结果上限，错误写入调用方提供的 `Error` 缓冲区并返回错误状态。

只读工具声明 `MDO_TOOL_EFFECT_READ`。文件修改、进程、网络等工具必须如实声明效果；一个非只读效果可以提供常量 `PermissionResource`，多个效果必须提供无副作用的 `DescribePermissions` 回调。mdo 在执行前完成权限裁决。接口细节见 [Module ABI](module-abi.md)，完整最小示例见 `tests/fixtures/modules/local-tool.c`；页面模板位于 `tool-template.js`。

编译、注册或依赖校验失败时，保存返回具体错误并恢复旧文件和旧目录；编辑器保留草稿。任务中的旧回调保有其代码代际，新任务使用新目录。停用或删除被 Agent 显式引用的工具会被拒绝；先调整相应 Agent 的勾选范围。手工复制文件后可点“刷新”编译，文件内容有误时请修正或移除后再启动。

## Agent 与 SubAgent

普通配置分别保存在 `agents/<id>.md` 和 `subagents/<id>.md`。默认 Agent 对应 `agents/default.md`、运行 ID `mdo.default`；其他主 Agent 为 `agent.<id>`，子 Agent 为 `subagent.<id>`。

两种编辑页共用工具勾选清单，包含内建工具和已注册的 C 工具，可筛选且不会丢失隐藏项的选择。默认继承全部可用工具；取消继承后至少选择一个工具。`tools: []` 保留原有继承语义。最终范围始终与登录状态、功能开关、任务权限相交；SubAgent 还受父任务的工具与权限约束。

MCP 的实际工具按需发现。这里选择 `tool_search`、`tool_load`；具体服务端工具范围在 MCP 配置中设置，不把可能在重启后消失的临时工具名称保存为 Agent 白名单。

```yaml
---
name: Researcher
description: Read a project and answer questions.
model: inherit
tools: [read, grep, ask_user]
allow_delegation: false
code: false
---
```

主 Agent 指令留空时按系统默认规则生成；普通模式下填写指令则使用所填的基础提示词。语言偏好、工作区、记忆等运行时规则继续由现有提示词组合器加入。SubAgent 需要填写任务指令。

通用 Agent 默认不强制在写文件后追加命令验证，联网调研、保存报告和维护记忆可直接交付。
编码主 Agent 可在 frontmatter 中设置 `require_verification_after_write: true`，要求在
最后一次工作区修改之后成功执行验证命令才能结束；应在交付正文前完成适当验证。
这项完成策略不改变工具权限；子 Agent 继承主 Agent 的完成策略。C 主 Agent 对应
`MDO_AGENT_REQUIRE_VERIFICATION_AFTER_WRITE` 标志。

默认 Agent 可以编辑和导出，不能停用；保存的外部版本可“恢复默认”。只查看默认配置不会创建 `mdo-home/agents`。

### 可选 C 能力

已有 `modules/agents/*.c` 和 `modules/subagents/*.c` 仍受支持。高级主 Agent 可以用同 ID 的 C 实现并启用配置 `code: true`：C 的 `Register` 可计算完整基础提示词，通过 `AddAgent` 发布，并提供 `Acquire` / `Release` 生命周期回调。普通工具勾选配置继续生效，C 的效果与深度上限不能被配置放大。代码生成发生在模块注册时，刷新后供新建任务使用，不是每条输入的提示词钩子。

不启用 C 模式时无需编写 C；即使存在同 ID 的 C Agent，基础提示词按普通配置生成。同 ID 的 C 实现缺失、重复或角色错误时保存失败并回退。共享的描述符、执行器、目录和代际持有机制没有增加第二套 Agent 运行框架。

## 本地接口

- `GET /api/v1/tools`：内建清单及当前注册的 C 工具元数据，包含 `source`、`member_only`、`available`、`availability`。不联网、不连接 MCP、不编译代码，不把完整参数 schema 重复发给管理列表。
- `GET /api/v1/extensions/tools`、`GET /api/v1/extensions/tools/<id>`：C 文件列表／源文件及导出工具信息。
- `PUT /api/v1/extensions/tools/<id>`：`{"content":"...C source..."}`，写入并注册。
- `POST .../<id>/enabled`：`{"enabled":true}`；`DELETE .../<id>` 删除自定义文件。
- Agent、SubAgent 使用相同的 `/extensions/agents`、`/extensions/subagents` 文件接口。

写操作沿用当前目标设备的写入准入与权限校验。`If-Match` 必填；创建为 `"new"`，编辑、启停、删除为当前 `revision`。并发修改返回 412，保存失败不会悄悄覆盖新版本。
