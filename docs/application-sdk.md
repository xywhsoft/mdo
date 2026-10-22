# mdo Application SDK

Application SDK 是命令、C 模块、工具、Agent/SubAgent 和客户端插件共用的应用访问层。它包括可发现的值接口与应用服务、原生产品函数导出、异步操作、流、事件和客户端桥接。版本仍为 mdo 0.2.4；SDK 的初始接口版本为 1。

## 入口与所有权

| 使用位置 | 入口 | 执行方式 |
| --- | --- | --- |
| C 命令 | `Context->Application` | 独立进程，通过 RPC 调用应用服务 |
| C 模块 | `Host->Application` | 进程内调用，注册服务和钩子 |
| C 工具 | `ToolContext->Application` | 带当前工具调用、工作区、取消令牌和截止时间 |
| 原生 Agent/SubAgent | `<mdo/native/agents.h>` 等 | 原生产品 API 和完整 Agent 驱动入口 |
| 客户端 JavaScript | `globalThis.mdo` | 标准 API 适配器、目标设备和窗口桥接 |
| 插件前端 | `activate(sdk)` 的参数 | 插件自己的存储命名空间及当前设备目标 |

普通 C 扩展包含 `<mdo/sdk.h>`。值由 `Api->Values` 创建和释放；`Get/At/Text` 借用，`Set/Append` 保留引用，调用结果归调用方所有。不要跨模块自行 `free()` 宿主分配的对象。上下文字符串和工具结果 writer 只在规定生命周期内有效。

原生模块还可以包含 `<xsbase.h>`、`<xwork.h>`、`<xllm.h>`、`<xllm-session.h>` 和 `<mdo/native/*.h>`。构建会从产品头文件声明自动生成 mdo 函数导出，并将全部公开原生头文件放进编译环境。没有另外维护一份缩减的产品函数白名单，也没有导出私有 manager 全局变量。`sdk.native` 列出函数名和对应头文件。

原生模块是受信任的进程内 C 代码。进程内崩溃不会被 SDK 变成安全隔离；不可信代码不应作为原生模块加载。独立命令进程没有指向主进程对象的原生函数指针，应通过 `mdoCall` 操作产品，使用原生模块开发长期后台实例。

## 服务目录与调用

`sdk.services` 列出服务，`sdk.describe {"id":"..."}` 返回参数契约。产品资源目录从生产路由自动生成，构建测试覆盖每一条路由和每一种方法；原生服务也有参数发现契约。新增产品路由会自动进入 SDK。

```c
#include <mdo/sdk.h>

static mdo_result Inspect(const mdo_sdk_context_v1* C, xvalue** Result) {
    mdo_error_v1 Error = {0};
    return mdoCall(C, "runtime.info", NULL, Result, &Error);
}
```

产品资源服务使用一致的信封：

```json
{
  "path": {"project":"default", "session":"session-id"},
  "query": {"limit":20},
  "body": {"title":"新标题"},
  "revision":"由上次读取返回的修订值"
}
```

返回值为 `status/data/revision/content_type/transport`。常用别名包括 `project.list/create/get/update/remove`、`session.create/get/update/fork/history/clear`、`run.start/get/cancel`、`models.list`、`skills.list`、`mcp.list` 等。精确资源名和占位符以目录为准，例如设置更新使用 `product.settings.by_domain.put`，其 path 字段为 `domain`。

普通操作直接复用生产业务处理函数，保留修订、项目生命周期、更新门禁及数据一致性检查。需要物理 HTTP 连接的备份、附件和异步产品接口沿用原传输路径；开始执行后发生错误不会改走另一条路径重放。

三个客户端入口为 `GET /api/v1/application/services`、`POST /api/v1/application/call` 和 `GET /api/v1/application/events?after=N`。调用接口仍要求当前页面写入令牌，并遵守远程控制权限；只读远程页面继续使用产品 GET 接口。POST 可以携带 `context.project/session/run/extension`，项目 ID 在服务端解析成工作区。已有原生调用上下文不能被嵌套信封重新指定。

## 能力范围

| 领域 | SDK 能力 |
| --- | --- |
| 项目、会话、消息 | 完整资源操作、历史、分支、回收站、彻底删除、恢复 |
| 上下文 | token 状态、请求构造、压缩、检查点、历史与恢复账本 |
| 模型 | 配置与目录、`model.complete`、多模态部分、工具定义、流、用量、凭据续期和现有重连策略 |
| Agent/SubAgent | 定义、生命周期、委派，以及 `MdoAgentSessionDriveNative` 完整驱动入口 |
| 工具 | 注册、发现、`tool.call`，沿用受管会话的实时权限、执行器、结果裁剪与附件规则 |
| Skill、MCP、命令和扩展包 | 现有管理、发现、调用及在线生态资源操作 |
| 草稿、队列、计划、记忆 | 现有产品操作和原生 manager API |
| 账号、权限、询问 | 账号状态、登录/退出、凭据库、权限档案、审批、用户问题 |
| 文件与系统 | `fs.*`、`vfs.*`、`storage.*`、`secret.*`、`process.run`、`network.request`、环境变量、打开文件/URL |
| 客户端 | 目标窗口、动作回执、面板、菜单、快捷键、渲染器、编辑器，以及插件自己的 DOM/JS/CSS |
| 远程与数据 | 设备连接、IPv4/IPv6/直连/中继状态、项目/会话传输、附件、备份和迁移 |
| 宿主 | 更新、工具包、电源管理、诊断、模块重载和宿主源码重载；原生 xs/xrt 网络服务工厂 |

`fs.*` 接受绝对原生路径，或基于上下文工作区的相对路径；也可指定 `space: home/application`。`vfs.*` 使用 `/app/...`。`storage.*` 使用相对路径，持久化到 `data/extensions/<extension-id>/...`。这些面向受信任扩展的访问没有工作区外路径黑名单。模型调用的 `tool.call` 则保留会话实时权限，避免用扩展入口悄悄改变用户选择的权限档案。

文件读取支持 offset/limit，返回 base64，并在完整 UTF-8 文本可用时附带 text。写入接受 text/base64 二选一，默认原子替换。revision 可以检测读取后发生的文件变更；这是原生文件的乐观检查，无法锁住不遵守 mdo 协议的外部编辑器。`process.run` 传递 argv 数组，合法空参数会保留；非零退出码属于结果，包含 stdout/stderr 和退出状态。平台错误保留错误阶段、状态和原生原因链。

## 异步操作、流和错误

```json
{"method":"operation.start","params":{
  "id":"sdk-0123456789abcdef0123456789abcdef",
  "method":"process.run",
  "params":{"program":"python","args":["script.py",""]}
}}
```

用返回的 ID 调用 `operation.get/wait/cancel/result/release`。先持久化准入回执，才开始工作。同一 ID 同一请求返回已有回执；不同请求返回冲突。重启后发现已受理但未完成的回执会标为 interrupted，绝不自动重新执行。

参数和结果不持久化到操作回执，避免将提示词、令牌和凭据落进诊断文件。运行中参数和结果在内存保留，release 或宿主退出后释放；结果丢失后返回 410，准入回执仍保留。超过 16 KiB 的结果通过 `operation.result` 分段读取 UTF-8 JSON，JavaScript 的 `sdk.result(id)` 会组合字节后再解码。原生回执中的状态和结果读取应分别处理，失败不会被包装成成功运行。

`stream.open/read/write/close/release/stat` 提供有界字节流。写满时可以等待；would-block 保证本次没有写入任何字节。close 停止写入，已缓冲内容读完后才 EOF。模型回调运行在引擎线程，不能阻塞；消费者未及时排空模型流时会明确取消并返回溢出错误，不会悄悄丢弃 delta。

资源预算：64 个保留操作、8 个活动操作；单操作结果 16 MiB；64 个流、每流最多 1 MiB、空闲租期 5 分钟；流和结果读取每段最多 16 KiB；文件读取最多 256 KiB/次，写入最多 1 MiB/次；进程 stdout/stderr 各保留前 16 KiB。传输还有命令/RPC/HTTP 自身的消息和时间预算。较大工作应使用分页、分段或流。操作继承调用方截止时间，不能绕过命令执行预算。

`mdo_error_v1` 保留 Code、Message、Stage、Status、Cause、Retryable 和 MayHaveCompleted。结果不确定时先查询回执及目标状态。不要仅因为 Retryable 或网络断开就重放写入。

## 扩展服务、钩子与生命周期

`Registrar->AddService` 接受 ID、说明、输入/输出 schema、effects 和执行回调。产品服务自行验证业务参数；自定义服务应验证自己的契约。重复服务名会拒绝整个候选目录，保留旧目录。覆盖内置服务必须设置 `MDO_SERVICE_REPLACE_BUILTIN`，目录只显示最终生效的提供者。

`AddHook` 支持有确定排序的 `service.before` 和 `service.after`。观察者不能修改值；Transform 钩子可替换输入或结果。回调在目录互斥锁外运行，递归调用最多 32 层；after 钩子失败会标记效果可能已经发生。事件总线还提供 `events.emit/read`，按 epoch/cursor 检测宿主重启和环形日志缺口。`product.changed` 与 `session.event` 是变更提示，详细历史仍从对应产品资源重读；不会将所有会话正文复制到诊断事件里。

模块及异步操作持有代码代际引用，热重载不会释放仍在使用的代码。`Stop` 在退休/退出时调用一次，用于停止并回收模块自己的后台生产者；它不能释放仍供活动回调使用的数据。`Unregister` 在最后引用释放后回收共享数据。回调不持有模块目录互斥锁；重载事务自身串行化，回调内再次重载会立即返回 busy。不要在回调中调用宿主/manager 的 Unit 销毁流程。后台模块应使用 Stop 打破自己持有会话/任务所形成的生命周期引用，不能只依赖最终 Unregister。

受管子 Agent 在开始运行时继承所有者的项目、产品会话标识；结束运行或释放所有者时清理映射。工具执行和权限发现使用同一运行时身份映射，不通过工作区路径猜测会话。显式 run 事件回调也保留这一桥接。

## 完整的原生 Agent 驱动

`MdoAgentSessionDriveNative(Session, Drive, Data, Error)` 取得该 Agent 的独占空闲运行窗口，向 Drive 借出原生 Runtime、Agent、History、Executor 和受管模型 Complete 回调。扩展可以自行决定：

1. 如何从 History 构造或裁剪请求，以及何时压缩。
2. 何时调用哪一个模型、使用哪些工具定义及流回调。
3. 如何串行或批量执行工具，怎样处理工具级失败与继续循环。
4. 何时将回复、工具调用和工具结果提交到账本，以及何时结束。

Executor 使用现有权限、钩子、串行组和结果处理。Drive 返回后保存检查点，即使 Drive 失败也会保留已经写入的账本，供后续恢复；不会把失败解释为外部效果已回滚。需要在长期实例中使用时，模块自行保留所需的原生对象，并通过 Stop/Unregister 释放。不要将借出的上下文指针保存到 Drive 之后。

## 客户端插件与恢复

`plugin.apply` 接受 `id`、可选的 `frontend: web/...js` 和 1..128 个文件。文件路径相对于 `/app/`，可以新增或覆盖 web、默认资源、模块和核心源码；不局限于固定 UI 插槽。文件发布为不可变 revision，保存大小及 SHA-256，加载前校验。配置指向当前版本，并保留 previous 供 `plugin.rollback` 使用。

`plugin.enable/disable/remove/rollback` 管理挂载。删除保留私有 revision 备份，不对用户项目做递归删除。每文件最多 1 MiB，单插件有效负载最多 4 MiB，最多 32 个插件。目录项和路径会检查跨平台别名，插件不能通过 `..` 写出自己的 revision 目录。

VFS 的挂载发布具有文件级原子性，多个文件不构成全局读事务。修改模块后调用 `runtime.reload {domain:"modules"}`，修改核心 C 源码后显式提交 `runtime.reload {domain:"host"}` 并查询 reload receipt；已打开的 UI 需要重载页面才能替换执行中的 JavaScript。宿主重启时先恢复插件挂载，再由显式宿主重载采用核心源码覆盖。语法错误会拒绝候选模块/宿主；插件文件本身不等于已经通过 C 编译。

恢复启动设置环境变量 `MDO_PLUGINS_DISABLED=1`，会跳过插件挂载及前端激活；坏的 revision 也会跳过挂载并报告错误。配置损坏会进入恢复模式，不覆盖原始配置。退出恢复模式前可保留备份并修正 `config/plugins.json`。核心源码覆盖属于受信任原生改造，需要保留原程序以便恢复。

前端入口导出 `activate(sdk)`，可返回清理函数。`sdk.ui` 提供 registerPanel/registerMenu/registerKeybinding/registerRenderer/openPanel/openEditor，也可以直接使用 DOM 和覆写后的内置文件。渲染器由注册它的插件或其他插件显式挂载，不会自动替换所有聊天消息。UI 插件属于展示窗口的设备；切换到远程会话后，API 调用仍走用户选中的目标设备。

通过 `ui.clients` 找到明确的窗口，再调用 `ui.invoke {client,action,params}` 和 `ui.result`。命令内部未指定 client 的动作会附加到原命令回执。已排队不等于已执行；客户端 ACK 才确定结果。客户端在执行前保存意图，页面中途重载会报告不确定状态，不会重复执行分支、导出等副作用。

可运行的 C/JavaScript 示例见 [examples/application-sdk](../examples/application-sdk)。模型测试采用本机模拟服务，不需要供应商密钥或真实模型额度。
