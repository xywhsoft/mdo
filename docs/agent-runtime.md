# mdo Agent 会话运行时

本文记录 MDO-6D 的产品层边界。底层执行、工具调度、子 Agent、任务、
MCP、artifact 和事件能力由 xwork 提供；mdo 负责把配置、模型、模块与
Skill 的不可变 generation 组合成一次可追溯的 Agent 会话。

## 对象与生命周期

```text
MdoAgentSession
├─ xwork_agent
├─ xllm_session
├─ model/module/skill catalog snapshots
├─ resolved model routes
└─ acquired module Agent lifecycles

MdoAgentRun
├─ xwork_run
└─ retained MdoAgentSession
```

`MdoAgentSessionCreateWithRuntime()` 接收进程级 `xwork_runtime`，解析并固定
当前 config revision、model generation、module generation、Skill generation
和最终 tool catalog generation。模块或 Skill 在此后 reload，不会改变已经
创建的会话；旧 TCC generation 会一直保留到最后一个使用它的 run 释放。

每个 prompt 使用 `MdoAgentRunCreate()` 创建独立 run。run 会持有 session，
所以调用方可在 run 创建后立即释放自己的 session 句柄。`Start`、`Wait`、
`Cancel` 和 `GetInfo` 直接保留 xwork 的并发与取消语义；`Wait` 超时只结束
本次等待，不隐式取消仍在运行的任务。

通过 `MdoAgentSessionInfo` 与 `MdoAgentRunInfo` 返回的字符串是借用视图，
只在对应 session/run 仍被调用方持有时有效。`xwork_run_result` 由调用方使用
`xworkRunResultUnit()` 释放。

## 选择与继承

主 Agent 默认是不可编辑的内置 `mdo.default`。有效选择顺序如下：

1. 调用方显式指定的 Agent、model、wire protocol、reasoning 和输出上限；
2. Agent descriptor 固定的值；
3. 全局 Agent 配置中的 reasoning、并行工具数、并行子 Agent 数和默认权限；
4. model profile 的默认协议、reasoning 与预算。

模型 profile 是硬上限。Agent 与调用方只能收窄 context、input 和 output
预算。子 Agent 还必须同时服从自身模型上限和父 Agent 的有效上限。未知
model、协议、reasoning、权限档位和任何越权预算都在 session 创建阶段失败。

每条模型 route 由 `(wire model, reasoning)` 唯一选择。两个不同 profile 或
协议若产生无法从 xllm request 区分的 route，session 创建会失败，不会把请求
猜测性地发送给任一 provider。

## Tool、Skill 与子 Agent

Agent descriptor 可以声明 tool allowlist、effect ceiling、Skill 列表和子 Agent
能力。mdo 先让 xwork 构造实际工具目录和子 Agent roster，再按 allowlist 与
effect 删除不可用工具。只读档位保留读取与只读委派能力，写入、进程、网络、
外部服务、secret 和计划任务能力会被移除。

只有 Agent 显式选择的 Skill 才会加载正文。正文总注入上限为 512 KiB；外部
Skill 会附加不可信 reference 提示。Skill 声明的每个必需工具必须同时满足：

- 位于 Agent allowlist，或 Agent 未设置 allowlist；
- 存在于最终有效工具目录；
- effect 没有超过 Agent 或父 Agent 的 ceiling。

任一条件不满足都拒绝创建 session。这样不会出现“提示词要求使用某工具，
运行时却已因权限被删掉”的半有效 Agent。

所有带 `MDO_AGENT_SUBAGENT` 的 descriptor 会组成一次不可变 roster。子 Agent
只能收窄 model budget、tool、Skill、effect、深度与后台执行能力，不能放宽
父 Agent 的权限。主 Agent 未声明 delegation 时发布空 roster。

## 回调所有权

生产路径在创建 session 时固定 model catalog、校验 profile 和 route。首次
实际模型调用时才解析 endpoint 与 credential reference，并为对应 route 创建
共享的 `xllm_client`；并发调用由 route 锁串行初始化。因此，即使运行环境尚未
配置模型服务，也可以创建和持久化任务；模型调用仍会准确报告配置失败。
secret 只在 client factory 内短暂出现并立即清零，不进入公开 catalog、
session info、日志或持久化数据。

`OnModelComplete` 是离线测试和受控宿主的可注入边界。使用它时仍会完整验证
model profile，但跳过 endpoint 和 credential 解析。调用方若传入任何回调状态，
应同时提供 `OnOwnerRetain` 与 `OnOwnerRelease`；mdo 将整组 model、approval、
permission、hook 和 event 状态作为一个 owner 固定到最后一个物理 xwork Agent
销毁。只提供一个 owner 回调会被拒绝。

注入的 `xllm_response` 必须遵守 xllm 的分配器契约，并能由
`xllmResponseDestroy()` 释放。它不能用 xrt 私有堆分配后交给 xllm/CRT 释放。

## 初始化与关闭顺序

bootstrap 顺序保持为：

```text
Home -> Config -> Models -> xwork runtime -> Skills -> Web -> MCP -> Modules -> Sessions
```

创建 session 前以上组件必须全部就绪。关闭时先停止并释放所有 run/session，
再按相反顺序卸载 Sessions、Modules、MCP、Web、Skills、runtime、Models、
Config 和 Home。

## 当前验证范围

Windows 真实 xs/TCC 探针覆盖：自定义主/子 Agent、Skill prompt 组合、Ling
Responses 默认选择、generation reload 后旧会话固定、无效 reasoning、缺失
Skill 工具、调用方提前释放 session、异步 run、实际选择信息和 callback owner
平衡。另有严格 GCC 编译、全量静态合同、相关 manager 回归与隔离目录单文件
启动。测试全部是有界功能测试，没有运行压力或高负载测试。

Ling 3.0 Tiny 的三种线上协议已由 catalog 和 client factory 支持，但真实联网
探针仍需要运行时提供三条显式 URL 与 `MDO_LING_API_KEY`；当前结果不代表线上
接口已经执行。
