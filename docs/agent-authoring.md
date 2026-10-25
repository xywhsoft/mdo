# 本地 Agent 与 SubAgent 开发

0.7.0 的主角色和子角色共用配置、C 回调、Application SDK 和执行管线。`mode` 决定角色出现在主角色选择器、委派目录，或者同时出现在两处；主角色拥有用户会话，子角色拥有隔离模型账本和委派任务生命周期。子角色不自动得到额外授权。

## 最快开始

在设置 → Agent 或 SubAgent 新建角色，填写指令后保存即可生效。表单支持工具选择、高级配置和上下文预览，也可以切换源文件编辑。导入支持 Markdown、`mdo.agent.v1` JSON 和原有本地导出文件。

手工安装时，将配置保存为 `mdo-home/agents/<id>.md` 或 `mdo-home/subagents/<id>.md`。JSON 内容同样可以放在这个规范文件中；通过界面导入 `.json` 会自动按规范落盘。刷新对应列表后加载。资源目录之外的系统命令不会自动探测或管理。

`examples/agents/` 提供：

| 文件 | 放置位置 | 用途 |
| --- | --- | --- |
| reviewer.md | agents/reviewer.md 或 subagents/reviewer.md | 两种运行模式均可使用的只读审查员 |
| coordinator.md | agents/coordinator.md | 自己只读，委派实现与审查 |
| worker.md | subagents/worker.md | 执行实现和验证 |
| config-role.json | agents/doc-helper.md，或在界面导入 JSON | 完整 JSON 配置示例 |
| code-reviewer.c | modules/agents/code-reviewer.c | C 回调、实例状态和自定义驱动 |

## 配置式开发

```markdown
---
schema: mdo.agent.v1
name: 审查员
mode: both
extends: mdo.general
tools: [read, ls, glob, grep]
read_only: true
context: {"memory.global":false,"environment":true}
---
审查相关代码，提供可验证的问题和修复建议。
```

最少只需角色指令；名称、说明可省略。`extends` 支持 `mdo.general`、`agent.<id>`、`subagent.<id>` 和 `mdo.default`。`mdo.general` 提供通用基础指令。继承对象逐项合并，数组整体替换，正文默认追加，`prompt_mode: replace` 改为替换；循环或超过 16 层会被拒绝。`prompt_file` 从当前角色分类目录读取相对路径；继承来的上下文文件保留原分类目录。

`tools: inherit` / `all` 表示使用获准目录，`none` 或 v1 配置的 `[]` 表示不暴露任何工具。工具数组支持结尾 `*` 通配符。旧格式的空数组保留原来的继承含义，表单再次保存时会写出明确的 `inherit`。`skills` 选择需要预加载正文的 Skill；未选中的 Skill 仍可按发现策略由模型调用。

`model: inherit` 跟随当前任务或父角色。显式模型 ID、`reasoning_effort` 和 `runtime.output_tokens/context_tokens/input_tokens` 按该角色实际模型校验。子角色可以使用比父角色更大的模型窗口和输出额度，但不能超过自己的模型档案上限。模型调用按实际角色路由，模型 wire 名称相同的不同供应商不会混用。

## 上下文来源

`context` 中每个来源可以写 `false`，或写选项对象：

| 来源 | 内容 |
| --- | --- |
| system | 配置正文、继承正文及 C descriptor 基础指令 |
| skills.preload | `skills` 中预加载的 Skill 正文 |
| user_instructions | 用户全局 Agent 指令 |
| reply_language | 回复语言偏好 |
| environment | 当前工作区、项目、角色和时间；默认关闭 |
| external_tools | 已启用外部工具的程序路径及模型提示词 |
| skills.discovery | 可发现 Skill 的摘要目录 |
| knowledge | 已启用知识库的目录及注入提示词 |
| memory.global / memory.project | 全局和当前项目的记忆索引 |
| file_access | 文件工具路径及权限用法 |
| project_rules | 工作区的 AGENTS.md，或 `file` 指定的工作区相对文件；默认关闭 |
| mcp.instructions | 选中 MCP 连接已缓存的服务说明；默认关闭，不为注入提示词主动连接服务 |
| custom.<id> | 自定义文本、文件或模板 |

来源对象支持 `enabled`、`role: system/user`、`order`、`text`、`append`、`template`、`file`、`required`、`max_bytes`、`refresh: run/request`。普通来源默认每轮读取一次；`request` 来源在本轮后续请求前刷新，其他来源保持原值。`max_bytes: 0` 不单独裁剪，仍受总提示词上限约束。裁剪保持 UTF-8 边界。

`include` / `exclude` 用于 Skill 预加载/发现、知识库和外部工具目录的条目筛选，支持结尾通配符。它们决定提示词内容；真正的工具暴露使用 `tools`，MCP 服务范围使用 `mcp_servers`。外部工具提示词筛选不等于操作系统进程沙箱。

全局关闭记忆时，角色配置不会重新启用记忆注入；停用的 MCP 连接也不会注入旧缓存说明。

模板必须显式写在 `template` 字段中，支持 `{{agent.id}}`、`{{project.id}}`、`{{session.id}}`、`{{workspace.path}}`。未知变量报错；普通指令、用户输入和工具输出不执行模板替换。角色文件路径是所属分类目录内的便携相对路径，禁止绝对路径和 `..`。项目规则文件相对于工作区。

用户当轮输入、附件、显式 Skill 调用、压缩摘要、历史消息、工具调用与结果属于模型账本，不重复塞入系统提示词。`history` 控制其请求投影；C 的 `BEFORE_REQUEST` 能精细处理最终消息、parts 和工具 schema。

## 权限、委派与额度

`permission_profile` 支持 `inherit`、`read-only`、`balanced`、`full-access`。角色配置、工具 effect 和用户实时授权共同决定可执行操作；配置声明完全访问不会绕过用户限制。

`read_only` 只限制角色自己的工具；启用 `allow_delegation` 的协调员可以把任务交给另一个获准写入的角色。`delegation.allow/deny` 选择可委派角色，`allowed_effects` 可进一步限制整个委派树。`allow_background` 决定该角色能否接受后台委派。深度和并行数仍受共享执行资源限制，并行数沿用全局 Agent 设置，不为每个子角色创建无限的新配额。

`delegation.context` 支持 `task`（默认，只传任务）、`summary`、`recent`（最近两次用户输入及之后内容）、`full`。`max_context_bytes` 默认 64 KiB，最大 512 KiB。父账本在发起委派时取不可变快照，以引用文本传给子角色；不会复制未完成的工具协议 ID，也不会把历史当作新授权。

`runtime` 支持 `max_turns`、`max_model_calls`（每次运行）、`max_depth`、`max_final_bytes`、`timeout_ms`（命令超时）、`task_timeout_ms`（接受委派的任务超时）、`context_tokens`、`input_tokens`、`output_tokens`。委派任务 `task_timeout_ms: 0` 表示不额外添加角色时间限制，仍继承宿主任务取消与截止时间。主任务总超时由宿主运行配置负责。工具连续失败仍沿用产品的 10 次策略。

`max_model_calls` 约束角色的普通交互请求；压缩摘要使用已有的独立、有界重试策略。发送前还会检查回调新增消息和工具 schema 的输入开销，超出时尝试一次安全压缩，无法容纳时明确失败，避免发送已知超限的请求。

`require_verification_after_write` 开启修改后的验证要求。`history` 支持 `mode: all/current/recent`、`keep_recent_turns`（recent 默认 2）、`include_summary`、`tool_result_max_bytes`、`images: allow/omit`。投影保留工具账本配对，原始内容仍留在会话中。主动排除摘要/旧消息会减少模型可见上下文，适合明确独立的任务。

`model_options` 支持 `temperature`、`top_p`、`parallel_tool_calls`、`tool_choice`、`extra_body`。`extra_body` 不能覆盖模型、消息、工具和托管额度。配置错误在保存/加载或请求准备阶段给出具体错误，不静默忽略未知的高级选项。

## C 开发

使用公开 `mdo/agent.h`，定义 `mdo_agent_v1` 和可选的 `mdo_agent_program_v1`，最后 `MDO_AGENT_MODULE(Role)` 即可；一个文件注册多个角色、工具或服务时继续使用完整模块入口。无需自己编译 DLL。将 `.c` 放进 `mdo-home/modules/agents/` 或 `modules/subagents/` 后刷新加载。界面也可以下载最小 C 模板。

无需配置文件即可运行 C descriptor；需要叠加配置时，在规范配置文件中设置 `code: true`，角色 ID 必须与 C descriptor 一致。C descriptor 的模型、工具、effect、生命周期和基础提示词作为基础，配置覆盖声明的字段。C descriptor 的 `ConfigurationJson` 用于上下文、历史、委派和请求选项；原生模型/执行额度使用 descriptor 对应字段，配置文件中的 `runtime` 可以覆盖它们。

支持以下事件，主角色和子角色完全相同：

| 回调 | 用途 |
| --- | --- |
| CONTEXT | 修改独立来源、模板变量、排序、文本和启停，再统一渲染 |
| BEFORE_REQUEST / AFTER_RESPONSE | 修改最终请求，检查/转换模型响应 |
| BEFORE_TOOL / AFTER_TOOL | 检查工具参数与返回结果 |
| BEFORE_DELEGATE / AFTER_DELEGATE | 检查委派调用与返回结果 |
| INSTANCE_RELEASE | 清理每个实际运行实例的外部资源 |
| Program.Run | 接管运行过程；可调用 `mdoAgentRunDefault` 复用完整托管循环 |

`mdoAgentNative()` 提供当前 xwork/xllm 请求、响应、工具事件、托管 session、取消和 deadline；`Context.Application` 提供完整 Application SDK。只有 CONTEXT 阶段可以修改 `ContextPlan`，其余阶段只读；请求与响应在相应阶段可改，不能保留其借用指针。

`mdoAgentState()` 是当前实际角色实例的私有对象，运行之间保留，实例销毁时释放；它不是持久存储。需要跨重启状态时使用 SDK 存储并明确保存/恢复。模块全局变量可能被多个会话并发访问，不应用作无锁实例状态。Acquire/Release 仍管理共享模块资源。

完整自定义驱动可以通过 xwork/xllm 和 SDK 构造自己的流程；自行替换托管循环时，也必须自行维护消息账本、流式事件及结果。通常只需要使用回调加 `mdoAgentRunDefault`，即可保留现有会话恢复、工具权限、取消和持久化行为。C 是受信任的进程内扩展，具备与内置代码相同的能力；配置权限不构成对恶意原生代码的沙箱。

模块和会话固定 generation；热重载只影响新会话，运行中的父/子角色继续持有旧代码直到释放。子角色检查点与父角色分开，不会覆盖父会话文件。

## 检查接口

`GET /api/v1/extensions/{agents|subagents}/{id}/plan` 检查已加载角色，POST 同一路径并提交 `{ "content": "..." }` 检查未保存配置。可使用 `?project=<项目ID>` 选择工作区。返回来源、顺序、启停、裁剪信息以及最终 system/user 文本，不调用模型、不运行 C 回调，也不创建记忆或角色文件。

配置预览不会执行继承 C 程序。实际运行中 `MdoAgentSessionInspect()` 可以读取最近一次来源计划和请求投影，包括模型、工具、输入估算和输出上限；图片仅给出 parts 数量，不复制二进制。请求预览文本有 1 MiB 上限，包含用户原文和记忆，只应用于本机诊断，不应主动上传日志。

Agent/SubAgent 的商店、创作中心、审核和独立发布规则见 [角色独立生态](agent-ecosystem.md)。

配置覆盖 C 角色时，未明确填写的行为位、模型、推理级别与预算继承 C 描述符；`false` 行为位和 `0` 预算同样保留。只有显式配置的字段才覆盖原声明；`model: inherit`、`permission_profile: inherit` 可明确选择会话或用户配置。
