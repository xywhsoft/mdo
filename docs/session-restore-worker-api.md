# 会话恢复审核、持久接受与后台执行

2026-10-03。生产路由和拥有句柄已接通；正式导入页面继续实施。
现有备份 manifest 的 `restore_ready:false` 保持，不能将后台探针当成
完整页面操作、原生 WebView 或实体移动端验收。

## 调用流程

先分块上传并封存，再由已有异步 preview 完成 schema/model/UI/images
预检。只有成功、未撤销且未过期的 decoded v2 才能取得拥有引用。
旧 v1 部分导出不能恢复成完整会话。

| 方法与路径 | 输入与结果 |
| --- | --- |
| `POST /api/v1/session-backups/previews/{preview}/restore-review` | JSON 仅含 `project_id`；固定源 pin/hash、目标项目版本/重建时间、物理 workspace、新请求/会话 ID 和恢复时间；201，重复同一未过期审核为 200 |
| `GET/HEAD /api/v1/session-backups/restores/{request}` | 优先查询 Home 的不可变接受/终态证据，再附加当前审核或 worker 的小型事实；不读取当前项目、源上传或目标会话 |
| `POST /api/v1/session-backups/restores/{request}/apply` | 空 body；使用审核保存的目标，不能在确认时重新捕获已变化的项目；接受持久化后才提交后台任务，202；已有持久请求直接 200 返回原事实 |
| `DELETE /api/v1/session-backups/restores/{request}` | 取消 active token 或释放未接受审核/已结束内存记录；幂等 200 acknowledgement，随后 GET 取得结果；不能撤销已经提交的会话 |

写路由沿用 `X-Mdo-Write-Token` 和写入 admission。审核未接受前不创建
Home 恢复 journal/回执，不执行模型、解析图片或写目标目录。目标项目须
可用且 workspace 是已有物理目录；允许未登记的虚拟 default 使用 revision
及 created_at 为零。源 workspace 只作为来源信息，不能自动成为目标。

审核返回稳定的新 ID、目标 workspace 和版本、源身份/hash、title/profile、
文件/字节数量及已知未验证历史引用。页面应先显示这些事实，让用户确认后
再 apply；`restore-review` 本身不是用户批准。相同 source/project 的重复
审核沿用原绑定，项目变化不会偷偷刷新审核；应明确丢弃后重新审核。

## 接受与生命周期

`MdoSessionRestoreAccept` 复制 request/budget，取得 shared project owner、
目标 identity/data reservation，并调用 requested Home Begin。immutable
owner flush 成功后才返回 owning operation；只做有界 admission，不做深
复制或 semantic replay。源 SHA 来自已封存上传对应的 preview pin，不能
由 HTTP apply body 覆盖。原始 decoded backup 借用至 operation 被消费；
worker 通过 owning pin 保持它的寿命。

`MdoSessionRestoreExecute` 在有界 worker 内再次复核目标，复用准备、Stage
材料化与磁盘读回、模型/UI/像素检查和最终短 binding callback。单个最多
30 秒的协作预算从 Accept 开始，包括等待；native I/O/codec 不可强行抢占。
最终无覆盖目录移动是唯一 commit，实际提交只推进一次 catalog generation。
不打开 Agent/driver/runtime，恢复的队列保持 staged，草稿和队列不自动运行。

`MdoSessionRestoreDiscard` 消费已接受但被取消、未运行或提交失败的 operation，
先关闭 parent，再消费 Home journal 并释放 data/project ownership，留下
aborted 结果。Execute/Discard 都使用精确 Result.Size；错误输出不改写内存
或消费 handle。正常失败也消费 operation，Commit 独立于 bool success；
非法提前退役 manager 时保留 handle，不能再次 Execute，只能诊断/收尾。
旧同步 `MdoSessionRestorePublish` 保留为兼容入口，共享执行/清理逻辑。

生产 manager 最多一个审核和一个 worker，pool 为 1 thread / 1 queued task。
active 先于 Submit 发布，Submit 在非递归状态 mutex 外进行。预取消任务
可以在 Submit 内直接 Drop；拒绝提交时调用同一 Drop。Drop 必须消费所有
accepted ownership，然后在状态锁外释放 pin，最后解除 admission。成功
Submit 之后不再访问可能已经被并发 DELETE 回收的 Job 指针。

审核/内存诊断寿命五分钟，过期不回收 active job；已接受的永久 Home 结果
不随内存 TTL 删除。清理 preview 大字节在状态锁外且持有 admission fence。
预览 DELETE/TTL 不撤销旧 pin，旧 pin 仍占 preview 配额直到释放。

宿主先 drain HTTP callback，`MdoApiUnit` 再停止/取消/等待恢复 pool；所有
operation/Drop 完成后才 Unit preview/uploads、session/project/Home 和 TCC。
pin/operation 不保留 TCC 可执行代码。非法退役下不能丢弃 live ownership
并继续卸载；本 API 的消费顺序避免该程序生命周期错误。

## 丢响应与中断

SessionId 同时就是不可变 request ID。apply、取消或网络失败后始终保留
这个 ID，GET 查询它；404/503/响应丢失均不是生成新 ID 重试的许可。
POST replay 优先查持久结果，不要求当前 preview/project/target 还存在；
冻结 Home 时仍可 replay，新的 mutation 由 Home 拒绝。

GET 的 `accepted/state/committed/terminal/cleanup_pending` 来自存储证据，
`phase/worker_finished/cancel_requested/error_code/message` 是驻留诊断。
commit 后的取消不能把 outcome 改成 aborted；回执完成前 Commit 已有位置
证明时保留 `committed:true`，不能推断 rollback。错误/损坏/冲突证据返回
503，不将它当成 missing。当前 Home 的重启要求也返回给查询方。

接受或终结可以在首次 Home 查询与内存快照之间完成，接口会补读持久结果，
避免把已经 accepted 的工作误标为未接受审核。failed acceptance 也可能已
产生 aborted/pending 证据；响应中的同一 ID 继续用于结果核对。详细存储
规则、容量及进程中断后的启动回收见 [持久结果合同](session-restore-receipts.md)。

永久终态只证明历史提交或中止，不保证目标今天仍存在，不提供自动幂等重做。
没有宣称 power-loss 目录 metadata durability 或外部程序写文件隔离。

## 有界验证

`tests/test_restore_worker_runtime.py` 在 HTTP/TLS 各用普通事务和三个隔离
中断场景。生产路由操作真实 v2 模型/UI/inline PNG、待审草稿/队列和 2 MiB
artifact；所有 pause/fault/control 只注入测试拷贝，没有生产测试端点。

验证未接受/过期审核、write token、空 apply body、同审核稳定 ID、virtual
default、Size 不消费、取消跳过 Run、pool 拒绝、deadline、中途项目版本及
同路径物理 workspace 替换、Unit cancel/join、preview/upload 删除后的旧
pin、一次 catalog 同步、源文件不变、队列 staged 和 runtime_open=false。
固定 ID replay、内存 TTL/重建、目标及项目删除、损坏回执拒绝均通过。

独立 helper 的强制进程中断用于证明 Windows/Linux 的启动 journal 回收，
只终止该测试 helper；commit 后 GC 失败/冻结 Home 保留提交结果，重启不
重跑 worker，提交后取消也不改 commit。原同步 coordinator 探针同时保留。
不进行压力或高负载测试；这不是 DOM、原生点击或实体设备交互证据。

`tests/test_packed_restore.py` 已加入标准发布门禁。独立目录只有打包程序
和隔离服务配置，来源是 exe 内嵌 TCC/VFS，没有外部 app/source overlay，
没有 fault injection 或 live provider。真实 idle 会话、普通 PNG、2 MiB
artifact、草稿/队列从 export/upload/preview/review/apply 完成恢复；模型
与资源字节、来源不变和 staged/runtime 状态核对后，移动程序并使用同一
Home 重启，query/apply replay 返回永久结果且目标字节不变。

首轮 Windows 全门禁捕获旧 coordinator 夹具的快照竞态：先读取临时
MAX generation 再读取 Done，能组合两个时刻。夹具现在先 acquire Done
再读取 generation，不改生产实现或原断言；修正后的定向探针通过。
首轮日志和单文件夹具 keyword-argument 修正记录保留。

最终 Windows/Linux 通过 115 Python、262 Node、93 JS、严格 C11、46
runtime、四项 packed 及独立 A/B；Windows 另过便携 Home/20 秒启动。
SDK@9bb75a5 不变，复用锁定 native host；14 份代码/探针按 LF 核对。
根目录发布 6,453,443 字节，SHA-256
`d5e9d16545d4925834c1f50d12af0bac4fc729d6a8da2e19e54af946cbeb8332`；
Linux A/B 6,503,603 字节，SHA-256
`00aa612a1ae9887a592c22289b5b91a866157b608f7750843f8acdfcf99808b1`。
日志 `.build/qa-restore-worker-{windows,linux}-final.log`、`source-equivalence.log`
和 `root-publication.json`；没有因通过后台门禁而升级正式 UI/设备验收。
