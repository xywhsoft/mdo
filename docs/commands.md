# C 命令与 Skill

命令由用户输入 `/名称 参数` 执行一个独立 C 文件。Skill 提供对话式的工作流程，手动入口为 `/skill:名称` 和 `$名称`，模型也可以通过已有的 Skill 发现、加载机制使用 Skill。普通命令不会自动成为模型工具，也不会自动向会话发送提示词。

## 文件与发现

| 来源 | 位置 | 优先级 |
| --- | --- | --- |
| 项目 | `<workspace>/.mdo/commands/<id>.c` | 最高 |
| 个人 | `<mdo-home>/commands/<id>.c` | 中 |
| 内置 | `app/default-home/commands/<id>.c` | 最低 |

同名来源被覆盖后，无效或停用的高优先级命令会阻止回退。目录发现只读取元数据和哈希，不编译、不运行 C，也不会为了列表读取创建 Home 或工作区。

文件首先包含 `/* mdo-command { ...JSON... } */`，然后是普通 C 源码。必填字段为 `id`、`description`；`id` 与文件名相同。可选字段为 `name`、`argument_hint`、`requires_session`、`timeout_ms`、`effects`。未识别的字段会被拒绝，避免拼写错误悄悄失效。

`effects` 可描述 `read`、`workspace-write`、`process`、`network`、`external-service`、`secrets`、`ui`、`model`。它是能力说明，不是原生代码的访问控制边界。外部生态包中的 C 命令需要沿用 C 代码的显式信任确认。

“设置 → 命令”编辑个人文件；“项目命令”可新建、编辑、复制内置命令到项目、启停和删除覆盖文件。保存只校验元数据，编译诊断在执行时返回。修改和删除使用内容哈希作为版本前置条件，项目写入有进程内和文件锁。

项目启停文件为 `.mdo/commands.json`：

```json
{"schema_version":1,"disabled":{"example":true}}
```

此设置按命令名称生效，也能阻止同名低优先级命令。删除覆盖文件会保留独立的名称停用设置；如需恢复内置命令，先启用项目命令再删除覆盖文件。

## 参数和 SDK

公开头文件为 `include/mdo/command.h`。入口为：

```c
MDO_EXPORT mdo_result mdoCommandRun(
    const mdo_command_context_v1* Context,
    const xvalue* Arguments,
    xvalue** Result);
```

`Arguments` 始终是数组：

```text
/example "" 'two words' C:\work\file.txt
/example ["", true, 42, {"path":"C:\\work\\file.txt"}]
```

普通语法只产生字符串，支持引号和空参数，保留普通 Windows 路径、UNC 路径；反斜杠可转义空白或引号。完整 JSON 数组采用标准 JSON 类型和转义规则。不执行变量展开、命令替换或 shell。

上下文固定本次调用的 ID、命令 ID、项目、会话、运行、工作区、原始参数、源码哈希和截止时间。程序通过 `Host` 函数表读取值、检查类型、解析和生成 JSON，以及调用宿主。`At`、`Get` 返回借用值；`Parse`、`Call` 返回的值由调用者释放，`Json` 返回的文本用 `Free` 释放。通过 `*Result` 交回的值转移给 Worker，不能把参数中的借用值直接交回。需要复制时可通过 `Json`、`Parse` 构造新值。

设置 `Context->Error` 可提供具体失败原因。返回 `MDO_RESULT_CANCELLED`、`MDO_RESULT_TIMEOUT` 会保留对应状态；编译失败、入口缺失、异常退出也有独立错误码。Worker 的 `stdout`、`stderr` 不作为结果通道；请使用结构化结果。

## 宿主操作

| 方法 | 参数和行为 |
| --- | --- |
| `ui.new` | 打开新建任务界面，保留原有“不提前创建空会话”的行为 |
| `session.create` | 对象，使用本次项目创建真实会话；复用原有会话 API 的校验，返回会话信息 |
| `session.compact` | 压缩当前空闲会话，使用模型、取消和截止时间，保留原始消息记录 |
| `ui.model` | 数组；空数组循环模型，一个字符串选择模型 ID |
| `ui.session` | `{"action":"fork"}`、`clear` 或 `export`；沿用原有对话框和完整 Markdown 导出 |
| `ui.stop` | 停止本浏览器的待执行命令，以及调用时捕获的当前运行；不会误停随后开始的新运行 |
| `ui.settings` / `ui.theme` / `ui.help` | 设置、主题切换、快捷键帮助 |
| `ui.mcp` | 读取 MCP 提示词到草稿；保留旧文本参数形式，也支持 JSON 数组形式 |
| `skill.invoke` | `{"id":"skill-id","arguments":"..."}`；未提供 arguments 时使用命令原始参数 |
| `api.request` | `{"method":"GET","path":"/api/v1/..."}`；访问本机现有 API，写入可选 `body`、`headers` |

`api.request` 只接受本机 API 路径，走已有来源、修订、写入和项目保护；写入令牌由宿主提供，不交给 C 命令。返回 `status`、`etag`、`body`，调用者应检查 HTTP 状态。命令、宿主更新及工具包分发相关端点不向此桥开放，防止递归或替换运行中的宿主。网络失败不会自动重放写入；错误会明确提醒结果可能已生效。

界面操作仅在成功后由原浏览器应用，并检查当前上下文。结构化结果支持：

- `message`：完成提示。
- `draft`：不超过 64 KiB 的待发送文本；仅在原输入框为空且上下文未切换时填入。
- `data`、`files` 等字段：以可复制的只读文本结果显示；文件元数据不会自动触发读取或跳转。

命令结果、MCP 提示词、Skill 展开都不会自动发送给模型。用户在命令执行期间修改的草稿不会被覆盖。

## 内置命令

`/new`、`/model`、`/fork`、`/export`、`/clear`、`/stop`、`/settings`、`/theme`、`/help`、`/mcp` 均为独立 C 文件；新增 `/compact`。聊天层不再按命令名硬编码业务分派。

`/compact` 只使用当前会话的模型进行摘要，不制造用户回合。运行中、存在待恢复响应或没有安全压缩前缀的会话会返回具体原因。模型拒绝、质量不合格和取消不会删除原始对话；成功通过现有压缩事件刷新上下文统计。

## 隔离和恢复

Windows、Linux 使用同一原生程序启动私有 Worker；Android 使用 APK 自带的 `libmdo_command_worker.so` 加载同包 `libxs.so`。没有额外运行时依赖，也没有原进程执行外部 C 的降级路径。Worker 没有监听端口，只配置一个网络工作线程。父进程取消、超时或退出后，Worker 和其进程组会被终止。

限制：有效命令最多 256 个，源码最多 128 KiB，元数据最多 8 KiB，参数最多 64 KiB / 128 项，单次结果最多 64 KiB；最多同时执行 4 个命令，最多保留 64 个执行记录。每次调用最多 128 次宿主操作、16 个界面操作。默认超时 30 秒，上限 10 分钟；内置压缩命令为 180 秒。

执行前固定源码及 SDK 快照。客户端先生成调用 ID，服务端保存执行记录，再启动 Worker。响应丢失时只按原 ID 读取状态；不会重发可能有副作用的调用。重启时未完成的记录标记为 `interrupted`；损坏或不可读记录标记为 `uncertain`，不会当作“未执行”。恢复和结果查询不重放 C 程序。临时参数、源码及 IPC 文件在结束或恢复时清理。

旧的个人 `commands/*.md` 首次迁移为一个手动 Skill 和 C 包装命令，仍可用 `/原名称`。原文件保留作备份，迁移记录在 `data/commands/migrations`；后续编辑应改 C 或 Skill。删除新命令后，重启不会从旧备份重新生成。冲突不会覆盖现有 Skill，诊断写入 `data/commands/migration-report.json`。

## HTTP 接口

- `GET /api/v1/commands?project=<id>`：合并后的只读目录。
- `POST /api/v1/command-invocations?project=<id>`：`invocation_id`、`command_id`、`raw_arguments`，可选 `session_id`、`run_id`、`revision`。
- `GET /api/v1/command-invocations/<id>`：执行记录。
- `DELETE /api/v1/command-invocations/<id>`：取消。
- `GET/PUT/DELETE /api/v1/commands/project/<id>?project=<id>`：项目源文件；写入需 `If-Match`。
- `POST /api/v1/commands/project/<id>/enabled?project=<id>`：项目启停；需源文件修订。

接口不接受调用者指定的任意工作区路径，项目 ID 由宿主解析。个人 CRUD 继续复用 `/api/v1/extensions/commands`。
