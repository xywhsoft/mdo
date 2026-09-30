# 计划任务存储与执行

MDO-7E 把产品层计划任务定义保存在 Home 的 `schedules/` 下，并把时间计算、misfire、overlap 和统一 task 状态交给 xwork scheduler。mdo 不复制 recurrence 算法；它负责可读存储、并发控制、Agent 选择、审计、执行驱动和结果历史。

## 目录

```text
mdo-home/
└─ schedules/
   ├─ <schedule-id>.json
   ├─ <schedule-id>.json.bak
   ├─ audit.jsonl
   ├─ .writer.lock
   └─ history/
      └─ <schedule-id>.jsonl
```

外部 `schedules/` 不存在时，manager 只发布空 catalog，不创建 Home。自动执行器也先检查是否存在到期定义；空闲轮询不会获取 writer lock，因此无窗口的单文件首次启动保持零外部写入。Windows 原生 GUI 的 WebView2 缓存则在首次开窗时进入 Home。

每个定义文件使用严格 schema version 1。未知、缺失、重复或类型不匹配的字段使该条目进入 diagnostics，不会隐藏其他有效定义。主要字段分为四组：

- 产品身份：`id`、`label`、`project_id`、`agent_id`、`model_id`、`protocol`、`reasoning_effort`、`workspace_root` 和 `input`；
- recurrence：`frequency`、`interval`、`start_at_us`、`weekday_mask`、`timezone`、`utc_offset_seconds` 和 fold policy；
- 执行策略：misfire policy、grace、catch-up 上限、overlap policy、并发上限和 enabled；
- 恢复游标：runtime generation、下一次 occurrence、最近 claim、claim/misfire 计数、产品 revision 和更新时间。

标识、文本、输入、定义文件、catalog、diagnostics、审计、历史和活动执行数都有硬上限。输入最大 64 KiB；单个运行结果最大 64 KiB；最多加载 128 个定义。xwork 继续执行更细的 interval、时区偏移、catch-up 和并发策略校验。

## 一致性与恢复

所有产品 mutation 先取得进程内 mutex 和 `schedules/.writer.lock`，再核对调用方给出的 revision。创建、启停、认领推进和删除都会先追加不含原始输入的 prepared audit；audit 只保存输入字节数和 SHA-256。定义随后用 Home 原子替换接口发布。写入失败后 manager 进入 fail-stop persistence fault，拒绝继续推进游标，防止内存状态与磁盘状态持续分叉。

定义创建/替换/启停/删除还在 writer/audit/store 变化前取得项目共享租约。由于定义可迁到不同项目，按 schedule ID 修改时先在只读 manager 锁内解析项目，释放锁后取得租约，再在写入锁内重新核对归属；归属变化返回冲突。替换同时保留原项目和目标项目，直到成功发布或失败补偿结束。独占冲突不会创建 Home 或修改定义/审计，其他项目的独立定义操作仍可执行。

每次启动枚举定义并用 `xworkRuntimeRestoreSchedule` 一次恢复完整 cursor。单个损坏文件形成结构化 diagnostic。合法定义即使暂时不能恢复进 runtime，也仍在 catalog 中可见，但 `Runnable=false`。

认领使用调用方提供的 Unix 微秒时间。manager 先只读检查最早 occurrence；没有到期项时直接返回 next wake，不创建外部文件。存在到期项时才获取 writer lock 并调用 xwork。xwork 因 `skip` misfire 或 overlap 推进但没有返回 task 时，mdo 仍比较并持久化所有变化的 cursor，写入 `advance` audit，避免重启后重复处理已经跳过的 occurrence。

全局 `settings.agent.schedules=false` 会保留并展示定义，但将它们以 disabled 状态恢复到 xwork。此时 catalog 的 `Runnable` 为 false，claim 保持空闲。

全局开关重载先保留整个有界 catalog 的关联项目，再在 manager 锁内核对 generation。
任一项目被独占或 catalog 在取得租约期间变化时，在 runtime 修改前返回冲突；已取得的
租约全部释放。租约覆盖批量更新、失败回滚和回滚失败后的兜底停用。开关没有变化时
是只读空操作，不额外取得租约。

到期认领与显式立即运行在同步 xwork 游标之前也先保留完整 catalog 的项目集合，核对
generation 后重新检查就绪状态。成功认领把对应项目租约转移到 manager 的 claim 记录，
直到 `MdoScheduleFinishTaskWithRun()` 发布结果历史才释放。未完成的 mdo claim 上限为
64，独立于调用者是否释放 runtime task，避免异常调用使拥有者表无界增长；分配及上限
检查发生在推进 xwork 之前。未到期/全局停用的读取仍不取得项目租约、不创建 Home。

同步游标可能推进多个项目。如果同步失败，整个关联项目集合保留到 manager 关闭；
历史写入失败则保留对应 claim，后续重复完成明确返回 persistence fault，不重复追加
历史。正常完成只接受仍有 mdo claim 的任务；调用者不得绕过 manager 先释放该 task。
统一 task 已经 cancelled 时，结果历史记录为 cancelled，避免误报成功。以上是进程内
隔离，没有新增跨重启的结果历史修复事务。

显式“立即运行”要求全局执行开关开启、定义已恢复且调用方持有当前 revision。它使用 xwork 的统一 scheduled task 和并发上限；暂停或周期已结束的定义也可执行。此操作更新最近认领时间、认领计数和 revision，写入 `run-now` audit，却不推进下一次 occurrence 或 catch-up 游标。执行失败仍通过常规 task/历史链记录。

## Agent 执行

bootstrap 创建一个长生命周期 schedule executor。生产默认使用一个轻量 timer thread，每 250 ms 把 `xrtNow()` 显式传给 manager；嵌入端和测试可关闭自动模式并调用 `MdoScheduleExecutorPump()` 注入模拟时钟。每次 pump 最多认领 4 个 occurrence，公开上限为 16，活动 Agent run 总量上限为 64。

每个 claim 创建普通 `MdoAgentSession` 和异步 `MdoAgentRun`。Agent、模型、协议、reasoning、输出上限、项目、workspace 和 prompt 均来自已经持久化的定义。Agent 继续使用同一 Module、Skill、Memory、MCP、Web、permission、effect 和 xwork audit 边界；executor 不提供绕过权限的私有调用路径。

非空 `ProjectId` 使 Agent 回调拥有者独立保留项目租约，不依赖记忆开关。产品 Session
或 Run 已销毁后，直接保留的 runtime Run 等引用仍可延长拥有者的存活期；最后一个
拥有者释放后才允许项目独占。manager 的 claim 租约同时覆盖创建 Agent 前的交接和
关闭时销毁 Run 后、结果历史发布前的清理空窗。

完成后，executor 把 Agent run result 写回原 scheduled task，并向 `history/<id>.jsonl` 追加 task ID、Agent run ID、计划时间、完成时间、result 和有界最终文本。shutdown 先停止 timer，再取消并回收活动 Agent run，最后将对应 scheduled task 记为 cancelled；这发生在 schedule、module、memory 和 runtime manager 释放之前。

任务面板停止现通过 `MdoScheduleExecutorCancelTask` 向实际 Run 的取消令牌发出请求，
不提前把 product-owned task 标成 terminal。重复请求幂等；在执行器锁内只检查终态、
保留 Run 生命周期和发出取消，不等待尚在运行的模型/工具完成。已完成但尚未收割
的 Run 按真实成功/失败结果归档；取消中保持 Agent 与 claim 的项目租约，直到实际
退出、回收和历史发布。API 返回 `stop_requested`，前端列表、详情和对话停靠卡显示
“正在停止…”，退出后才显示“已停止”。停止已接受后的读取失败保留已确认快照。

直接调用 xwork `task_cancel` 的嵌入端/工具仍使用原有立即标记语义；下一次 pump
通过公开 task 快照向对应 Agent Run 传递取消。默认轮询间隔 250 ms。这一兼容路径
不改变 xwork 的 task API/ABI，不应把其提前 terminal 的标记视为 Run 已退出。

## 公开操作

每次执行以统一 task ID 生成独立的 `AskScopeId`，通过原有 `ask_user` 工具注册；
`ProductSessionId` 仍为计划 ID，项目记忆审计关联不变。只增加宿主侧问答路由，
不增加模型工具描述、产品会话或会话目录。同一计划并行运行时，问题与回答互不
可见；派生工具目录中的询问仍属于该执行范围。回答时短暂持有执行器生命周期锁，
防止停止或回收移除范围；询问 manager 再原子检查取消令牌、截止时间和一次性提交。
此保证针对执行器拥有的停止路径；直接 xwork 取消仍按前述 pump 转发语义处理。

任务详情以旧版询问卡片提供选项与自由回答，轮询不重新挂载编辑器。草稿按 task/ask
保存，切任务或语言不串用；中文输入法确认不会误提交。接受回答后的读取失败保留
“已提交”及只读输入，原位重试只刷新状态。任务列表使用 `pending_questions` 显示
“等待你回答”，实际 Run 和租约保持活动，直到回答后的运行退出。

- `MdoScheduleCreate` 创建定义；ID 为空时生成 XID。
- `MdoScheduleSetEnabled` 使用 revision 启停定义。
- `MdoScheduleRemove` 使用 revision 删除没有活动 run 的定义；历史和审计保留。
- `MdoScheduleClaimDue` 提供确定性单次认领，供 executor 或嵌入宿主使用。
- `MdoScheduleTrigger` 和 `MdoScheduleExecutorRunNow` 按 revision 显式启动一个普通 Agent run，并返回 task/run ID。
- `MdoScheduleFinishTaskWithRun` 对齐 scheduled task 与 Agent run 历史。
- `MdoScheduleHistoryRecent` 有界读取最近 32 条持久完成记录；文本以 UTF-8 边界截断为预览，无历史时不创建 Home。
- `MdoScheduleCatalogSnapshot` 返回引用计数不可变快照和恢复 diagnostics。
- `MdoScheduleExecutorGetSnapshot` 返回活动数、累计 claim/completion/failure 和最近错误。
- `MdoScheduleExecutorCancelTask` 取消持有的 Run，返回是否由执行器处理，供通用 task API 安全回退；`MdoScheduleExecutorTaskCancellationRequested` 提供进行中的请求状态。
- `MdoScheduleExecutorTaskAsks` 读取某次执行的询问；`MdoScheduleExecutorAnswerTaskAsk` 按执行归属一次性回答，供 `/tasks/{task}/asks` 的 GET/HEAD 和单条 PUT 使用。

计划任务的 HTTP 资源和 UI 编辑器已接入；执行历史以有界最近记录页展示。更早历史的分页仍需在后续阶段实现。

## 当前验证范围

2026-09-30 的真实 xs/TCC 探针覆盖定义独占拒绝、创建不落盘、定义及审计字节保持、
其他项目可写、原/目标项目的共同保留、受控发布失败后的补偿，以及解析期间真实
跨项目迁移与 catalog 增量导致的二次校验拒绝。全局更新和回滚被受控失败后，兜底
停用的每个节点仍保留全部关联项目；操作退出后租约全部可重新取得。
检查点仅注入复制的测试源，正式应用不含这些入口。

执行阶段另验证一次认领实际推进两个项目、独占期间认领/立即运行的零修改、空闲
读取、Agent 创建前的 claim 保留、历史发布与重复完成、cancelled 结果，以及第二个
定义发布失败后的全项目隔离和历史失败后的 claim 隔离。关闭记忆工具的执行器探针
验证失败创建/启动清理、模型回调、产品 Run 回收后的直接 runtime 引用保留，以及
关闭时最终 Agent 拥有者已释放、历史尚未写入的空窗仍被 claim 保护。

取消执行探针现验证模型真正收到取消令牌、等待模型退出期间 task 仍为 RUNNING、
项目租约仍保留、重复请求与重复历史保护、直接 xwork 取消的 pump 转发，以及已完成
Run 的真实成功结果。HTTP 探针通过实际 `ask_user` 等待工具验证退出和唯一 cancelled
历史。打包组件夹具在 320×350 验证请求中/退出后的显示、重复点击与焦点。
HTTP 探针另覆盖同一计划两个真实 `ask_user` 执行、反序回答与输出对应、跨 task
拒绝、UTF-8 字节上限、重复/迟到回答、HEAD/OPTIONS、终态空列表及无会话目录。
打包组件夹具覆盖轮询/输入法、语言切换、独立草稿、重复提交和接受后的读取失败。
完整项目清除事务和跨重启恢复仍待实现；待回答的计划运行没有新增持久恢复机制。

本阶段 Windows 有界发布门禁通过 114 项 Python、117 项 Node、80 个前端模块解析、
严格 C11、21 个运行探针与确定性打包，另通过便携 WebView2 Home 和 20 秒打包启动。
WSL 因宿主空间耗尽后的只读/I/O error 尚未恢复，新增链路的 Linux 门禁未完成。
未做压力或高负载测试，macOS 与实体移动端仍待独立验收。
