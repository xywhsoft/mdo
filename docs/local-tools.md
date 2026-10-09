# 本地工具与 Agent 配置

入口均在 **设置 → 扩展能力**，与 Skill、MCP、命令同级。

## 工具管理

工具页只有两个分类：

- **mdo 内建工具**：只读清单，显示用途和可用状态。联网工具显示“会员”胶囊；只有登录且开启“允许联机搜索”时才进入模型可用工具。
- **自定义工具**：新建、导入 `.c`、编辑源文件、导出、启停和删除。一个文件可注册多个工具；启停作用于整个文件。

文件保存于 `mdo-home/tools/<id>.c`。ID 为 1–64 个小写字母、数字、点、短横线或下划线，不允许路径分隔符、连续点和 Windows 保留名。清单保存在源码 `app/include/mdo/tool_catalog.h`，没有另一份用户可改写的内建工具注册表。

点击“新建”会生成可直接编译的 JSON 回显工具。先填写文件 ID，再修改 C 实现；未改过的模板会自动跟随文件 ID 更新工具名。建议工具 ID 使用 `user.<id>`，模块 ID 使用 `user.tools.<id>`。文件 ID 和导出的工具 ID 可以不同，模型和 Agent 工具配置使用导出的工具 ID。内建 ID 及兼容导入别名（如 `Read`、`Bash`）保留，避免导入 Agent 时指向不同工具。

保存通过现有 TCC 与 Module ABI 编译和注册，不要求用户另装编译器，Windows 和 Android 使用相同源文件。单个管理文件最多 128 KiB，JSON 请求另有 256 KiB 上限。C 是进程内可信扩展，并非隔离沙箱；只导入已审阅的代码。

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
