# 单会话恢复协调与发布合同

2026-10-03。`app/include/mdo/session_restore.h` 新增持久 Accept 与 owning
Execute/Discard；旧 `MdoSessionRestorePublish` 共用执行核心并保留兼容。
生产审核/apply/query/cancel HTTP 与有界 worker 已接入 requested 事务，
详见 [worker 合同](session-restore-worker-api.md)。正式导入页面仍需接入。
`restore_ready:false` 保持，不能把本阶段当成完整导入体验已经交付。

## 调用方与输入

入口必须在有界 worker 执行，不能在网络回调中材料化、解析图片或回放模型。
调用方在返回前保有不可变 decoded v2 backup 与 cancel 对象。请求包含显式
reviewed `MdoProjectBinding`、新 32 位小写 hex session ID 和正 UTC 微秒时间。
源 metadata 的 workspace 只作为来源，不会被自动选为写入位置。

Request/Result 使用精确 Size。Result Size 错误不改写其任何字节；一般失败
清空验证/恢复事实，仅保留 Size 与真实 Home 重启要求。初始复核在任何 Home
恢复写入之前读取当前项目并比较版本、项目重建时间、canonical workspace 和
目录物理身份。早期复核不能替代最终发布前的同一项检查。

一个最多 30 秒的协作 deadline 覆盖准备、磁盘 Stage、模型/UI/像素检查、
再次读回和最终短回调；调用方可提供更小预算或取消。native I/O 不能被该
deadline 强制中断。生命周期所有者必须先停止接纳并排空调用，再 Unit
session manager、项目和 Home；生产 API Unit 已落实该顺序。

## ID 与数据预留

`restore_reservation.inc.c` 保持在 session manager 的私有边界中，不暴露
任意 Home 事务指针。每个 reservation 拥有当前 shared project lease 的强
引用及 `MDO_SESSION_DATA_CAPTURE` 排他租约。最多八个 pending identity，
Home journal 仍只有一个当前事务。预留自身不创建 Home、目标目录或 Agent，
不更新 catalog generation。

Create/Fork 的 absent 检查和 native mkdir 现在都在 manager 锁内，与 pending
ID 检查串行。Open 在读 metadata 和登记运行态之前复核；Load 同样拒绝
pending 目标。ASCII 大小写与末尾点的保守别名共享预留，防止 Windows 同一
native 目录通过另一种 ID 拼写进入。路径本身不会被改写成这些别名。

预留采用两段锁边界：

1. 当前 project owner 已取得；manager 锁内检查容量、已有目录、活动运行态，
   将 ID 放入 pending 集合。该阶段不调用 data registry。
2. 解锁 manager 后取得 data capture lease，再在 manager 锁内重复目录检查。
   两段之间完成的 sidecar 写入会被第二次检查发现，保留其数据并拒绝恢复。
   取得 lease 之后，草稿、队列、图片等已登记写入不能进入；无关 session 的
   写入继续可用。

registry mutex 仅保护计数，不跨越 I/O 或 manager 边界。不存在目标时，先
预留 ID 再取得 data lease，也避免对已经存在的会话施加无必要的 capture。
这里只协调进程内已登记操作；外部程序直接修改文件仍由 identity/目录清单
和无覆盖 rename 在提交时拒绝，不能被应用层租约排除。

未开始 Home 事务的旧 reservation 可以在 manager Unit/重新 Init 后释放；
旧地址不属于新 pending 集合，旧 data registry pin 也不能解锁新 registry。
已开始事务的调用必须排空；违反排空合同的 stale active handle 保留诊断，
不冒充已经释放。生产 worker 必须按顺序排空，不支持非法并发 manager Unit。

## 准备与一次发布

入口组合已有的 `PrepareRestore` 与 Home journal、Stage：投影修复、输入待
确认转换、身份/workspace/产物重绑定和来源记录，随后独立材料化与实际模型/
UI/图片像素校验。再次 StageCheck 之后保存 verified directory identity，
关闭 caller parent 与所有 Stage anchors。失败也先关闭 anchors，再让当前
Home journal 消费私有材料；未知文件或清理失败由 Home 隔离写入。

最终 `MdoProjectWithBinding` 取得 definition guard 和 native writer lock，
重新读取 reviewed 项目。版本、重建或物理目录身份变化则不调用发布函数。
callback 只检查预算并进入 manager/storage 的短提交；不再解析图片、运行
模型或进行网络请求。工作目录复核证明的是最终检查时的身份，不能禁止
不受应用控制的外部目录重命名。

manager 锁内先拒绝 `UINT64_MAX` generation，再调用 Home 的无覆盖目录
rename。实际 `Committed` 为真时恰好推进一次 catalog generation，即使
后续 journal 清理失败导致 bool false。目标目录在 rename 前不可见；rename
之后完整可见。pending/data pins 最后释放，发布不会打开 Agent、执行工具
或派发恢复队列。导入的未知 Agent/model profile 仍为描述性信息，后续运行
必须通过现有 runtime 校验。

## 提交事实与错误

`Committed` 独立于 bool 成功。提交后的 verified facts、目标 metadata、Stage
身份和 catalog generation 保留；项目 writer close false 或 Home journal
清理失败不能把已提交结果改为未提交，也不能删除已提交目录。Home 被标为
RestartRequired，后续写入暂停；重启继续使用已有 journal 回收机制。

失败不能触发自动另造 ID 重试。调用方必须保存同一目标 ID，核对结果和
重启后的实际目录/来源。当前同步 API 不提供持久 HTTP receipt；返回值丢失
后的可查询结果、worker 状态及断线恢复仍待设计接线。底层现有 journal
覆盖进程中断恢复，不额外宣称目录元数据已经具备断电持久性保证。

## 有界验证

`tests/test_restore_coordinator_runtime.py` 调用真实生产入口；一份复制源码
夹具拥有一个 native thread、backup 深副本和 cancel，HTTP 只启动/读取/
取消/放行。测试夹具退出先 cancel 并 join，再释放对象，所有 pause 和 fault
都不编入生产源。每个协议执行普通事务、关闭失败、GC 失败三份隔离 Home。

覆盖：零 Home 的容量/Size/旧 manager 预留生命周期；真实源模型写入、UI
和 inline PNG；2 MiB 产物；pending 原拼写/别名 Create/Open/Load/data writer
拒绝与无关 writer 允许；两段预留间的真实 sidecar 写入被保留；提前取消、
deadline、最终 callback 取消、generation 耗尽；项目修改、同名 workspace
物理目录替换；坏图片失败与私有 journal 清理；实际成功只增一次 generation、
catalog RuntimeOpen=false、队列 staged、provenance 和模型/产物精确字节；
真实 artifact HTTP/TLS 读回；重复目标不覆盖；提交后关闭/GC 失败仍保留
Commit 与会话读取能力，来源文件始终不变。

首轮夹具遗漏 queue 的必需 first 字段、内部 Clone 的绝对 deadline、错误码
及 generation 注入后的观测基准，已按实际 API 合同修正。Windows canonical
路径带 `\\?\`，测试现同时核对 metadata 精确等于 reviewed binding，并以
samefile 核对物理目录；没有把路径字符串的另一种拼写作为成功依据。初始
日志保留，不放宽生产输入、取消或提交判断。

此阶段没有新增 DOM、原生窗口交互或实体移动端证据，也没有压力/高负载。
此前 Linux queued HEAD reset 与首次大备份响应间歇失败仍独立待排查。

最终 Windows/Linux 完整门禁通过 115 Python、252 Node、90 JS 模块、严格
C11、43 runtime、三项 packed 和独立 A/B；Windows 另通过便携 WebView2
Home/20 秒启动。SDK@9bb75a5 不变，复用已验证 native host；Linux 使用
全新 ext4 staging，十三份改动代码/清单/探针按 LF 核对。根目录更新
6,432,061 字节、SHA-256
`8998f650af73e0398445ce1c9ff2e130cab61ee7f2f6e71fe9badf28e64cf391`；
Linux A/B 6,482,221 字节、SHA-256
`25fa200b1edfa90f3af66568b517903cf7e89bfdc374a1996bc7817ad6171bb8`。
日志 `.build/qa-restore-coordinator-{windows,linux}-final.log`；源码比对
`.build/qa-restore-coordinator-source-equivalence.log`。首轮夹具失败保存在
`.build/qa-restore-coordinator-runtime-{initial,debug,debug2,debug3,debug4,debug5}.log`，
修正后定向结果在 `runtime-final.log`。没有修订成功、取消或文件保留条件。
