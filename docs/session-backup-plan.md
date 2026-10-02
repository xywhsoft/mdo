# 会话 JSON 完整备份与恢复实施记录

状态：专用下载/上传、离线解码、模型/UI 关系、静态图片像素检查及生产异步
预览 API 已接入。独立 staging 的材料化/读回检查已实现，身份及投影转换、
原子发布与正式页面入口待实现。2026-10-02 已完成
checkpoint 与有界读取共用排他运行窗口、统一捕获边界、v2 捕获/编码层和
有界 HTTP/TLS 传输。现有页面仍使用 `export_schema:1`，只有 meta 和模型 snapshot。
格式/传输验证通过不表示正式页面已经导出完整备份，或恢复事务已经完成。
旧版核心功能是 Markdown 导出，图片携带已经恢复；这里补齐新版现有 JSON
备份入口，不将它冒充旧版已有的导入能力。

## 内容与完成判据

备份须能在另一份全新 Home 中恢复可追溯的会话。需要覆盖：

- 模型账本的 snapshot 和 journal，以及当前 meta；
- UI 事件、时间、用量、工具结果、历史编辑/截断记录和消息附件引用；
- 图片原字节、元数据和完整名称；artifact 文件及其引用；
- 会话自己的 todo、反馈、草稿和队列。队列恢复为待用户确认的状态，不能因为
  导入自动发起模型调用、shell 或其他工具；
- 格式版本、原项目/会话身份、各文件长度与内容校验。运行锁、临时文件、缓存、
  服务凭据和其他项目数据不作为可恢复会话内容。

源 workspace 的绝对路径只是来源信息，不能作为导入时的自动写入目标。
已清理或被 UI retention 淘汰的内容不能重建；文件中必须声明实际保留范围。
超过预算或缺少被引用的数据时明确失败或生成明确标为不完整的导出，不能给出
完整备份成功提示。不能通过改名关闭当前内容缺口。

## 一致性边界

`MdoAgentSessionWithCheckpoint()` 仅保护模型账本。捕获函数同步执行，不能
重入运行/账本维护，不得等待网络或用户输入。它不能单独冻结整个会话目录。

完整捕获前必须建立并证明覆盖下列写入方的边界：

| 内容 | 当前写入方/保护边界 | 接入要求 |
| --- | --- | --- |
| meta、profile、清空/截断/分叉 | session handle 锁、session manager 锁 | 验证 revision，阻止其他 handle 修改 |
| snapshot、journal | Agent run claim、runtime lease | checkpoint 与复制在同一 claim 内 |
| UI 事件、待办 | event bridge、todo 存储 | 证明末尾事件落盘；统一捕获边界 |
| 图片 | API attachment 锁、过期清理、附件剪枝 | 覆盖 GET 触发的清理，原图与元数据同时稳定 |
| 草稿 | API draft 锁 | 同时保存 revision 和附件引用 |
| 队列与回执 | API queue 锁、GET 核对/清理 | 捕获 pending 与 receipt，恢复不重复派发 |
| artifact | xwork registry 与文件写入 | 证明文件已完成，捕获引用与文件匹配 |

HTTP write admission 只能覆盖登记的 HTTP 写入。GET 的清理、后台回调、直接
manager 调用或库通过 native path 写入不能被它自动覆盖。不得以“GET 是只读”
或“已拿到 Home 锁”作为全目录一致性的证明。捕获入口应集中这些边界并固定
锁顺序，发生冲突立即返回可重试错误；不等待正在运行的任务完成。先用确定性
小用例证明边界，再组合，不新增压力测试。

## 落地顺序

1. **已完成**：模型 checkpoint 与有界读取同一个 run claim；metadata 校验与
   读取串行；失败释放引用/claim。现有 v1 envelope 保持兼容。
2. **已完成**：实际文件清单、统一捕获边界和确定性
   小型冲突用例，见下节。后续完整格式必须从这个入口复制文件，不能绕过。
3. **已完成**：版本化 manifest 与专用有界下载/分段上传。
   当前普通 API 请求上限 256 KiB、旧 v1 下载上限 33 MiB，不能直接塞入图片
   和全部日志。采用专用上传/下载边界并
   分别限制文件数、单文件、总字节、JSON/base64 膨胀和传输时间；定额失败需要
   明确反馈。导出完成即释放捕获锁，下载速度不能占用运行窗口。
4. **进行中**：离线拥有解码、清单及 metadata/UI/todo、draft/queue/receipt/
   feedback/消息绑定 schema、模型 snapshot/journal schema/CRC、checkpoint 后记录
   连号、保留 UI 的侧车关系、独立模型上下文重放和模型/UI 关系检查已接入。
   新模型 writer 已用 v4 保存完整消息 parts/native；旧文件的已丢失内容
   不能修复。助手 thinking/text 与签名转换已接入；其他 native 块未映射，
   已增加静态 PNG/JPEG/WebP 的实际像素解码；URL/空图片引用单独统计为
   未验证，动画 PNG/WebP 明确拒绝。生产单 worker 预览已接入，支持粗粒度
   进度、取消、失败重试、过期回收与退出时 join；全部检查复用同一个 30 秒
   协作预算。staging 中的 UI 投影恢复和正式页面证明仍在步骤 5–6 中完成。
   先实现离线验证与预览，
   再做恢复事务。验证所有 schema、路径、ID、内容
   校验和引用；拒绝链接、绝对路径、`..`、重复文件和大小声明失真。v1 只能
   预览为模型快照，不能误报为完整带图备份。未知模型/Agent 的会话可保留
   原数据供查看，继续运行前重新选择并验证有效 profile。
5. **进行中**：已实现独立 staging 的排他材料化、完整磁盘读回及实际无绑定
   xllm/UI 关系和像素检查；它仍保留来源身份及队列原字节，不能发布。
   继续完成身份/产物重绑定、投影修复和待确认转换。恢复到独立 staging，
   验证实际 xllm 恢复与 UI replay 后，以不覆盖的原子
   目录发布方式创建新的会话。保留原身份作为来源，明确处理 ID 冲突；失败
   只清理由本次事务拥有的文件。实际发布后才进入 catalog，导入不得立即
   调用模型或恢复执行队列。复用 Home 锚定和事务原则，但不要直接借用会
   替换整份 Home 的导入操作。文件系统不支持所需原子发布时明确拒绝。
6. 接入现有会话菜单和恢复预览，补齐三语进度、冲突/失败恢复与键盘/移动
   布局。从正式单文件页面导出到全新 Home 再恢复，逐项读回模型历史、UI
   记录、原图字节/名称、反馈/todo/草稿/队列/artifact，并确认没有自动运行。
   Windows、Linux 有界门禁和确定性打包后提交；原生下载和实体手机另记证据。

每项提交更新 [迁移记录](frontend-migration.md) 与
[完成审计](frontend-completion-audit.md)。基础代码的回归不能代替完整备份的
正式导出/恢复验证，也不能代替原生及实体设备验收。

## 模型快照格式与原字节 CRC 校验

下文保存各阶段当时的边界；当前模型重放增量见末尾“独立模型上下文重放”。

2026-10-02 新增独立的 `backup_snapshot.c`，在共用的文件检查入口接入，
捕获后的 v2 编码与拥有离线解码复用已解析的 JSON tree，不再二次解析快照。
检查 xllm-session 1–3 的格式/字段、消息序号严格递增且小于 next_sequence、
turn 不超过 current_turn、角色/flags 和 nullable 文本、工具调用字段类型，
文件账本的名称/序号数组类型、长度和序号范围。未知字段和嵌入 NUL 拒绝；
源路径及工具参数只是记录，不打开或执行。空 provider tool ID 和原始错误参数
可合法保留，不能为了 schema 校验重写为“正确”内容。

配置数值在窄化之前检查整数类型与位宽，压力阈值只接收数值，枚举按当前 ABI
检查。未安装模型的身份保持来源数据；配置校验复用没有 client、路径、hook
的内存 `xllmSessionCreate`，只验证库实际支持的配置，立即释放。不调用模型、
shell、queue 或 catalog，不写 Home。v1/2 允许省略新增字段/配置，使用库默认；
v3 快照须带配置及原字节校验和。

v3 CRC-32/ISO-HDLC 核对原文件末尾 checksum 之前的精确字节，保持库的格式，
不重新序列化来验 CRC。固定只读 nibble 表，每 64 KiB 检查取消/截止时间；
消息、工具调用和文件账本之间也检查。解析器和一次文本检查仍是有界同步操作，
不把协作截止时间当作可中断任何原生指令的硬超时。

拥有解码探针使用真实 idle 会话快照，追加合法 v1/2、Unicode 消息/推理及
原始错误工具参数；177 个小错误输入中包含非法配置、消息/文件序号、字段类型
和重新计算外层 SHA-256 后的内部 CRC 损坏。分块 CRC 过程中取消返回空结果，
失败后能重试，Home 原字节不变。首次 CRC 错误用例没有匹配初始 turn 值，
已改为必定修改现存 checksum 字节；完整门禁另发现手写 session 探针漏掉新
模块，已同步其 include 与复制清单，没有放宽产品校验。

这完成快照格式边界；后续 journal 格式接入见下节，实际 xllm 重放及模型/UI
关系仍需接入。图片实际解码、生产预览 worker、新 ID/原子 staging 恢复和正式菜单
也未完成。manifest 保持 `restore_ready:false`，本阶段不开放导入或自动续行。

Windows/Linux 完整有界门禁通过 114 Python、251 Node、90 模块、严格 C11、
36 运行探针、独立 A/B 与三项 packed；Windows 另通过便携 WebView2 Home
和打包启动 20 秒。当前 Ubuntu WSL 可用，Linux 在新 ext4 源拷贝中从同一
锁定 SDK 新构建宿主，跳过 GUI。根目录程序与 Windows A/B SHA-256 为
`cdfd1363200177be4b58bebcd0639b4a4a1d7fefc94231d1faeedbaa4975f55c`，
Linux A/B 为
`d769d8a556a94b0037550297b47f7ef67e40c6698fdecf7f533f44dfc23da785`。
日志 `.build/qa-backup-snapshot-{final,linux-final}.log`；没有压力或高负载测试。

## 模型 journal 格式与 checkpoint 连号检查

2026-10-02 将快照校验模块整理为 `backup_model.c`，snapshot 与 journal 共用
消息字段、整数/文本及原字节 CRC 检查。共同入口先解析快照并取得 checkpoint，
再逐行解析 journal，一次只持有一行的 JSON tree；两份文件不再经过通用扫描
二次解析。其他侧车和 UI 继续使用各自读取器，来源路径和原始工具参数保持
记录，不作为打开文件、恢复路径或执行工具的指令。

v1/2 使用 `journal_sequence`/`operation`，v3 使用 `sequence`/`type`。七类
记录 begin_turn/add_message/compact/ledger/truncate/rewind/clear 分别检查已知
字段及必需值，拒绝错类型、未知/重复字段、NUL 和数值窄化。保留库重放所
支持的可省略 generation/usage/after_sequence，以及 rewind 的零边界。
v3 CRC 对换行前精确字节校验，CRLF 仅移除 framing 的 CR；空行和未终止尾
记录拒绝，离线检查不截短文件或修补损坏内容。

记录编号必须严格递增；snapshot 已覆盖的前缀可从中间开始或缺少已覆盖号，
但仍核对每一行的 schema/CRC。超出 checkpoint 的第一条必须是 checkpoint+1，
后续不能跳号。每条记录及 CRC 的每 64 KiB 保留取消/截止时间检查。此处检查
格式和记录编号，尚不证明消息与 turn 的完整关系、工具配对、实际压缩摘要
质量或 rewind 后的模型上下文。这些必须以 xllm-session 实际重放验证，不能
用相同外观的手写状态机替代。

有界 HTTP/TLS 探针新增合法旧记录、旧 v3 可省略字段、七种记录 schema、
covered/tail 编号和 CRLF，检查字段/CRC 损坏、缺号/乱序、重复/空/残缺行及
首条读取后取消。另通过锁定库实际写出 begin_turn、add_message、ledger、
rewind、clear，将精确行字节纳入离线解码。该夹具只使用隔离 Home 的自有
临时文件，在零写入比较前移除；解码、重编码和错误重试期间 Home 原字节
保持，没有模型/shell/queue 执行。其余两类使用 schema 样例，尚未冒充实际
compaction/truncate 重放证据。

本阶段继续保持 `restore_ready:false`，正式页面菜单和恢复事务未开放。

Windows/Linux 完整有界门禁通过 114 Python、251 Node、90 模块、严格 C11、
36 运行探针、独立 A/B 与三项 packed；Windows 另通过便携 WebView2 Home
和 20 秒打包启动。Linux 使用新 ext4 源拷贝和同一锁定 SDK 新构建宿主，跳过
GUI。根目录程序与 Windows A/B SHA-256 为
`dd9b148ce35f07c1bd3d8b180a79b71d9b37ac66a17703bd72d8850981e5519f`，
Linux A/B 为
`d70032d9bc70cf02a50a13a2f7e7814c15b7cb8639ff310d198282e362d56fbb`。
日志 `.build/qa-backup-journal-{final,linux-final}.log`；没有压力或高负载测试。

## 统一捕获边界与实际文件清单

会话目录为 `sessions/<project>/<session>/`，当前可恢复文件如下：

| 相对路径 | 内容与写入边界 |
| --- | --- |
| `meta.json` | 当前 schema/revision/profile/来源；所有 handle 的提交共用 session manager 锁 |
| `snapshot.json`、`journal.jsonl` | xllm-session 通过 native path 写入；root run claim 固定模型账本 |
| `ui-events.jsonl` | event bridge 同步追加/retention/维护；bridge 锁和 session data 写租约 |
| `todo.json` | bridge 投影、截断恢复，以及直接 TodoProject/Reset；session data 写租约 |
| `attachments/<image-id>.bin`、`attachments/<image-id>.json` | 图片原字节/元数据；API attachment 锁；直接分叉复制另持 source/target data 写租约 |
| `attachments/events/<event-id>.json`、旧 `attachments/runs/<run-id>.json` | 消息图片引用；事件写入、剪枝和分叉/失败回收持 data 写租约 |
| `draft.json` | 文本、图片引用、revision、提交意图/profile；API draft 锁 |
| `queue.json`、`queue-receipts/<item-id>.json` | pending/派发身份/回执/图片回收记录；API queue 锁，覆盖 GET 核对及直接回执 helper |
| `feedback.json` | 消息反馈及截断后的修复；API feedback 锁，包含全局 GET 清单触发的直接修复 |
| `artifacts/run-<run-id>/<artifact-id>-<source>.txt` | xwork native 原子文件写入及引用；必须没有运行中/待执行的本会话后台任务或存活子 Agent |

`.runtime.lock`、临时文件、缓存与服务凭据不导出；`.bak` 是实现侧回退文件，
未来 manifest 仅收录当前逻辑内容。全局/项目新任务草稿不属于这个会话。

API 捕获按 attachment → draft → queue → feedback 顺序尝试取得现有存储锁，
失败立即逆序释放并返回 `409 session_capture_busy`，不等待某个文件操作。
这些锁是现有全局锁，其他会话在写相同子系统时也可能使一次捕获需要重试。
它们同时覆盖图片 GET 清理、队列 GET 修复和非路由 helper。

随后 `MdoSessionWithCapture()` 尝试 session handle → manager 锁，验证最新
metadata，取得 Agent 的 root run claim。全局 manager 锁保留到复制结束，
其他 metadata handle 不能在途中提交。checkpoint 完成后再取得本会话 data
排他租约和 bridge 锁，同步调用只读、有界 reader；先 claim 后 data 排他，
避免另一个刚启动的运行被捕获租约误拒绝首条消息。冲突可以发生在 checkpoint
之后，但不会产生备份输出，且全部已取得的边界都释放。

后台边界先在 root claim 内读取稳定 task snapshot，拒绝 `sOwnerSession`
属于当前 snapshot path 的 pending/running 任务，再检查共享 callback owner
是否仅剩 product session 与 root Agent 两个基础引用。待启动直接子任务在
root claim 释放前已发布 task；子/孙 Agent 保留 owner 到文件写入与末尾回调
完成。因此 task → owner 检查顺序覆盖 queued child 到 live child 的交接和
已结束父任务仍有存活后代的情况。普通 checkpoint 的行为保持兼容。

data 写租约覆盖直接 Todo、事件、附件引用、剪枝、分叉图片复制和回收；它
允许嵌套共享写入，捕获不等待 writer，键按保守大小写/末尾点规则防护 native
别名。租约 registry 关闭后由旧 lease 保留到最终释放；重新初始化后释放旧
lease 不会解除新 registry 的排他状态。租约不会创建 Home 或磁盘锁。

reader 只从 Home 锚定入口有界复制当前目录，不得调用 manager/运行/网络 API。
API guard 和所有 session 边界在网络下载之前释放；完整格式及恢复事务仍按
步骤 3–6 实现。进程外直接修改 Home 不属于这份应用内捕获合同。

本阶段确定性探针通过：四类 API 锁由另一线程占用时均拒绝捕获、逆序释放并
成功重试；data 租约覆盖别名/嵌套 writer/不同会话/旧 registry 最终释放；
reader 中直接待办、附件、剪枝、分叉及回收拒绝，另一 handle/thread 无法
进入 metadata 边界。带错误/无错误失败后 claim 与全部租约释放，原 v1 导出
及陈旧 revision 拒绝保持。真实后台 child 在模型回调中用信号量暂停：捕获
不调用 reader，child 完成后重试成功；另覆盖 pending/running/terminal task
及 terminal 父任务仍有后代 owner 引用的拒绝。没有压力或高负载测试。

Windows/Linux 后端全发布门禁通过 114 Python、240 Node、90 模块解析、严格
C11、32 运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和打包
崩溃/20 秒启动。新增占用提示的三语映射之后，两平台重新通过 contract、
完整前端检查、A/B 打包、packed Home lease/队列恢复；Windows 最终包另通过
便携窗口/20 秒启动。最终 Windows SHA-256 为
`b2a3e7db3a2a63d6735c071bcba2cfd40d7276cb6efa38a6d18ecae278647ddd`，
Linux 为 `195ffc4b7d5ecf19203f59bbfe0b040659c5900ac2cbebd0f52dc6da3b910bad`。
日志为 `.build/qa-session-capture-{release,linux-release,final,final-linux}.log`。
根目录 `mdo.exe` 已更新到最终 Windows 验证包。

## v2 内部文件格式与所有权

`MdoSessionBackupCapture()` 仅通过统一捕获入口复制当前文件；HTTP 调用方
与 API 存储 manager 共用进程的调用方（包括非 HTTP 线程）还必须持有四类
API guard。返回不可变、独立拥有字节的对象，释放 API guard
和 session 后才调用 `MdoSessionBackupEncode()`。编码、JSON 检查、hash 和
base64 均在捕获边界之外，不再次读取 Home。路径按 session 目录相对路径
排序，每项保存 `path`、`bytes`、`sha256`、`encoding:base64` 和精确原字节。

清单记录 `format:mdo-session-backup`、`export_schema:2`、原项目/会话 ID、
revision、捕获时间、来源 workspace、文件数量/总字节、当前 UI 首末事件 ID
和记录数，以及缺少的可选侧车文件。checkpoint 后 journal 可以合法不存在，
不会为了导出创建空 journal。retention 明示早期内容可能已经清理，不能从
当前保留文件推导“包含从创建以来的全部历史”。来源 workspace 和事件中
旧 artifact 原生路径只是来源信息，后续恢复必须重写映射，不能直接使用。

默认硬预算为 1024 文件、单文件 32 MiB、原字节合计 64 MiB、编码文档 96 MiB；
另按实际产品文件收紧图片、metadata、UI 日志、引用和侧车限额。遍历最多
4096 节点、三层；只接收已知逻辑文件与目录，未知内容明确失败。Home 锚定
的 no-follow 打开、regular-file/目录类型和前后 identity/size 复核拒绝链接、
特殊文件及替换。只跳过已知逻辑文件的 `.bak`/`.tmp`/`.bak.tmp` 和 regular
`.runtime.lock`；不将未知文件或链接冒充可忽略临时文件。相对路径不接受
绝对路径、父目录、分隔符别名和超出已知路径格式的文件名。

捕获默认五秒、编码默认三十秒的单调截止时间，可降低预算/时间，不能抬高；
在分块读取和各有界操作之间检查。同步 native read/checkpoint 和一次 JSON
解析不能被截止时间强制中断，不能把这写成操作系统级硬超时。最终文档先
估算 base64 膨胀和头部余量，再分配一次输出；不会构造第二份完整 base64
value tree。输出大小失败为零，全部部分文件/缓冲释放，可重新捕获。

编码检查 JSON 语法、无残缺尾的 JSONL、metadata 身份/revision、UI 事件 ID
递增、图片元数据/二进制成对及长度/名称、草稿/队列/消息图片引用和 UI
artifact 文件引用。待回收 `discard_images` 允许对象已经不存在。记录所有
原字节而非运行恢复或队列派发。这里尚未验证所有产品 schema、图片实际
解码、所有 ID 关系及实际 xllm/UI replay，因此 manifest 明确
`validation:json-syntax-and-resource-references`、`restore_ready:false` 和
`queue_restore_policy:require-user-confirmation`。不能据此开放恢复或声称
步骤 4–6 完成；现有页面菜单及 v1 HTTP 保持原行为。

传输接入前查明：xs 使用非阻塞发送队列，xrt 默认 TCP WriteLimit 为 1 MiB；
不能把 96 MiB 文档直接交给现有一次 `StreamSend` 并声称下载支持上限。
专用接口须按 XS_TAKEOVER 生命周期管理有界分块、背压、截止时间、
取消/关闭与脚本代释放，不占用会话捕获锁，也不能放大所有普通请求限额。

确定性小型 C/TCC fixture 不发起模型/shell/队列，独立 Python 读回逐项核对
base64、长度、SHA-256、路径排序、UI 保留范围、图片二进制零字节/完整
Unicode 名称、草稿/队列、反馈/todo、回执及 artifact。捕获后写入新草稿再
编码仍得到旧字节；降低单文件/总字节/文件数/文档预算、截止时间、缺图、
缺 artifact、未知内容、残缺日志均明确失败，清理后成功重试。实际链接
能力可用时另确认拒绝链接；探针侧车部分为格式校验的最小子集，这不是
完整 schema/replay 或真实恢复成功用例。

本子阶段 Windows/Linux 完整有界门禁通过 114 Python、240 Node、90 模块、
严格 C11、32 运行探针、确定性 A/B、packed Home 租约/队列恢复；Windows
另通过便携窗口与打包崩溃/20 秒启动。最后加强不可变字节的独立读回断言
及 API guard 注释后，两平台重跑 session 与 A/B/packed 租约/队列，Windows
最终包另通过便携窗口/20 秒启动；Linux 实际符号链接拒绝断言通过。日志为
`.build/qa-session-backup-format-{release,linux-release,final,final-linux}.log`。
根目录程序已更新，与 Windows 最终 A/B 同为
`1d3059efa6127f2eae7a9ee0a11a8916dbab46c6f33e2a70effaf36be7340709`；Linux
最终包为 `4e5f26d3bc7f406699f883e9ed3730d07601db6bbf3b8a99ec65ecdd63a90582`。
本轮没有更换页面菜单/普通 HTTP 导出，没有执行浏览器下载或真实导入操作。

## 专用 HTTP/TLS 下载与生命周期

新增 `GET/HEAD/OPTIONS /api/v1/projects/{project}/sessions/{session}/backup`。
普通请求、图片和 v1 `/export` 限额与行为保持；只为 v2 单独发送最高 96 MiB
文档。一次进程最多一个备份任务，第二个返回可重试的 `session_backup_busy`；
executor 首次下载才创建一个线程，不排队多个大文档，不写临时下载文件。

任务只复制 request ID、方法码并持有 TCP 或 TLS stream 引用，不保留请求视图。
session 自带的 project lease 覆盖捕获；取得四类 API guard 后复制，立即释放
guard 和 session，再编码和下载。异步捕获不绕过 Home/项目生命周期边界。
响应为 JSON attachment，Content-Length 精确，强 ETag 为最终文档 SHA-256；
HEAD 保留同一编码/预算判定和长度，但不发送正文。成功 drain 后 orderly close，
失败/取消 abort；专用响应声明 `Connection: close`，没有更换 xs event table。

每次至多 16 KiB；TCP 根据 WriteLimit 收紧，处理 AGAIN 的零受理并等 DRAIN，
TLS 使用 off-worker async Send 与 DRAIN Future。每个 Future 的 wait/cancel/
detach 在任务仍持有 stream 时完成。入场起共用 30 秒单调截止时间；捕获仍
最多五秒。时间/取消在有界操作之间检查，同步 native read/checkpoint 或一次
JSON 解析仍不能被强制中断。关闭/超时已发送部分内容时直接断开，不能给出
完整文件成功响应。四类新错误均有中英俄稳定码映射。

xs 的 XS_TAKEOVER 连接持有脚本代；API Unit 停止入场，cancel/wait/destroy
下载 executor 后才释放 API manager 和脚本代码。真实网络等待可由取消唤醒，
包括 Unit 在唯一网络 worker 上执行的情形。Unit 不允许在该 executor 内调用；
同步文件系统操作的协作取消边界仍适用。

`test_backup_download_runtime.py` 在隔离复制源中注入暂停/小预算控制，分别
通过 HTTP/TLS 发送普通 2 MiB artifact，最终文档超过默认 1 MiB 发送队列。
逐文件独立 base64/长度/hash 读回，验证捕获后实际 metadata 修改不影响原
备份且不占用存储锁、单任务拒绝、HEAD、断连、截止时间、无效内容与重试。
单个 socket 的缓冲缩小后，确认实际 pending bytes，再从网络 worker cancel
真实背压等待并释放执行槽；不是压力或高负载测试。`test_packed_backup_download.py`
只在隔离目录放已打包程序及 HTTP 测试配置，源代码全部来自内置 VFS；下载
草稿、待确认队列和原 artifact 后，换程序目录重启同一 Home，再核对原字节。
没有执行模型、shell、队列，也没有进行浏览器下载或恢复。

严格离线验证/预览、实际 xllm/UI replay、新会话原子发布和正式
页面菜单仍待步骤 4–6 完成；`restore_ready:false` 继续保留。此后端下载不能
当作正式页面导出到新 Home 再恢复成功的证据。

本阶段 Windows/Linux 完整有界门禁通过 114 Python、240 Node、90 前端模块、
严格 C11、33 运行探针和 A/B 确定性；Windows 另通过便携窗口与打包崩溃/
20 秒启动。最后映射排版及项目租约/packed 新验证在 Windows 复验完整前端、
API、A/B、三项 packed 和便携窗口；Linux 全门禁包含最终代码/新验证。
日志为 `.build/qa-backup-download-{release,final,linux-release}.log`。
根目录程序与最终 Windows A/B 同为
`6ea1f9b686bc5971cb84b4672136aee8c79d0b5cac1d31db21e98f739282093e`；Linux
包为 `a3dad18d2340d4410dc2366a6896a8646750760b7b6406d0303fa463897b68f6`。

## 专用分段上传与不可变读取

xs 的 RequestProc 在请求体收齐后调用；大文件不能通过简单放大普通请求限额
解决。传输分为下列入口，普通 API 的 256 KiB 限额及图片 8 MiB 限额保持：

| 方法/路径（统一前缀 `/api/v1/session-backups/uploads`） | 合同 |
| --- | --- |
| GET/HEAD 前缀 | 当前可见上传或 `upload:null`，可找回旧页面/丢失创建回复的 ID |
| POST 前缀 | 严格 `{id,bytes,sha256?}`；ID 为客户端先生成的 32 位小写 hex，字节数 1–96 MiB，可选 SHA-256 为 64 位小写 hex |
| GET/HEAD `/{id}` | 接收进度、分段上限、剩余时间、receiving/sealed 和传输校验状态 |
| PUT `/{id}/chunks/{offset}` | application/octet-stream，解码后最多 256 KiB；固定长度/分块 HTTP 均按解码字节核对 |
| POST `/{id}/seal` | 无正文；已收齐则结束 SHA-256，声明不符为 422 并丢弃上传；重复 seal 保持同一校验结果 |
| DELETE `/{id}` | 无正文；移除可见性，释放无人读取的内容；没有会话目录操作 |

所有写入仍需当前 write token，并服从 Home 导入/重启和 HTTP 写入 admission。
创建意图在分配前验证字段/类型/预算；相同 ID/字节数/期望 hash 的重试只读
当前进度，不重置内容或截止时间。不同意图明确冲突。偏移必须是规范十进制；
只在当前末尾追加，已经收齐的范围仅允许逐字节相同的重试，不重复累加 hash；
越界、重叠追加、不同内容和 seal 后写入都拒绝。断连前未收齐的单段不会进
handler，因此不会推进已接收字节。错误回复后先 GET 进度，不能假定回滚。

每个 API store 只有一个上传槽，按声明字节数分配一次精确容量，最多 96 MiB；
没有全量 realloc、后台 idle 线程、磁盘暂存、用户路径或 request/stream 借用。
下载的配额独立，不能将 96 MiB 宣称为整个进程内存上限。创建后固定五分钟
单调截止时间，重试/查询不续期；过期内容在任何上传访问时立即不可见，内存
在下一次上传访问、最后一个 pin 或 Unit 回收。无访问时不承诺五分钟整点释放
内存。重启会丢弃上传，客户端须保留原文件重新上传；不在 Home 留半成品。

sealed 只表示字节数与传输 hash，仍是未经格式验证的原始数据，响应保留
`validation:transport-sha256` 和 `restore_ready:false`。不解析大 JSON，不创建
session，不执行模型/shell/队列。未来离线 validator 可 Acquire 不可变字节，
在 store 锁外执行；cancel/expiry 后已有 pin 保留内容及配额，最后 Release 才
释放。store 的 owner/pin 计数保护各自的锁与槽，Unit/Init 后迟到的旧 Release
不能碰新 generation。生产 Unit 前必须停止入场并 join reader workers，TCC
卸载前释放所有调用者；内存引用不能代替代码生命周期。

本阶段正常文件传输发现 xs 的 TLS 全请求模型存在两处衔接缺口：共享 context
只有默认 256 KiB PlainLimit，且保留部分 header/body 时未请求 ReadMore。已在
独立 xs 分支提交 `c840d8c`、`483d753`，mdo 依赖锁升级到后者。HTTP listener
独立 context 保留 policy/其他限额，明文预算为 effective recv_limit 加一条 TLS
record；大小溢出/创建失败拒绝启动。等待保留前缀时按 xrt API 请求增长，失败
abort；没有修改 xrt 核心或放大其他协议/普通 API 限额。listener 保留引用后
释放局部 context，失败路径平衡。xs 独立小探针覆盖默认/大/小窗口，真实
HTTP/TLS 固定/分块请求的字节与 hash、keep-alive 和仅发送头部的超限拒绝。

mdo 的独立 HTTP/TLS 探针把正常 2 MiB artifact 的真实 v2 下载文档再分段上传，
直接从不可变 pin 独立计算原字节 hash；覆盖缺 token、字段/媒体类型/偏移/
预算错误、相同重试、不同内容冲突、未完整单段断连、seal/校验失败重试、
取消时保留 reader、过期和旧 generation Release。上传前后 Home 可读文件
逐字节一致（Windows 排除被宿主独占的 `.mdo.lock`）；没有执行恢复。
packed VFS 探针也增加真实文档的上传/校验/删除，不使用外部 app 源。

下一步在这个 sealed 不可变输入上完成离线严格 schema、路径/身份/hash/引用
验证与预览；然后实际账本/UI 试恢复及独立新会话原子发布，最后切换页面菜单。

本阶段 Windows/Linux 分别重建锁定 xs 宿主并通过独立 HTTP/TLS 窗口探针，
mdo 完整有界门禁通过 114 Python、240 Node、90 模块、严格 C11、34 运行
探针、A/B 和三项 packed；Windows 另通过便携窗口及打包崩溃/20 秒启动。
Linux 为新 ext4 工作树、跳过 GUI。根目录程序与 Windows A/B 同为
`3d2fcd26719706568c216cfaa8250abfabcfd56c3270c6f5ae82d619fa25bc29`；Linux
包为 `13e8487ac69d3bcb3c22c60bf1950885982d2b025980e5c8d7cfd4daee730a54`。
日志为 `.build/qa-backup-upload-{xs-build,xs-receive,runtime,release,linux-host,linux-xs-receive,linux-release}.log`。
没有压力/高负载、浏览器下载或真实恢复测试，完整恢复不由本阶段证明。

## 离线拥有解码与部分 schema 校验

`MdoSessionBackupDecode()` 接受 sealed 上传的不可变字节或其他同步借用输入，
只在内存中返回拥有各文件字节的对象。复用捕获/编码的路径白名单、大小预算、
metadata 与资源引用规则，不接触 Home、catalog、Agent、模型、shell 或队列。
生产调用方须将它放到有界 worker，先 pin 输入，在结束后释放；本轮没有新增
HTTP 预览入口或 worker，不在网络 callback 上解析大文档。

外层 JSON 使用 SAX，只保存小清单和一个进行中的文件，不创建整份 base64
DOM。访问器自己按字段记录拒绝重复键（包括转义后的同名键），因为 xrt 的
visitor 不执行 DOM 重复键策略。每项严格检查路径、字段类型、规范 base64、
声明大小及 SHA-256；拒绝未知字段/文件、重复路径及 Windows 大小写别名、
绝对/父目录/分隔符别名，
事件/运行引用文件名还拒绝不规范的前导零。文件排序后核对总数、总字节、
可选文件缺失声明、metadata 原身份/revision/workspace 和 UI 首末 ID/记录数。
来源 workspace 与 artifact 原生路径仍只作来源，不作为写入目标。

预算仍为 1024 文件、32 MiB 单文件、64 MiB 原字节合计及 96 MiB 文档，允许
调用方降低；路径另受产品限额约束。一次解码拥有至多 64 MiB 原数据，解析器
的当前字符串 token 可额外占用单文件 base64 大小；上传自身及一次产品 JSON
解析的 DOM/节点也占独立预算，不能把 64/96 MiB 宣称为整个进程峰值。每个
visitor 事件、文件和日志记录间检查单调截止时间/取消，不能硬中断单个 token、
hash、codec 或 DOM 解析操作。所有失败释放部分文件和清单；输入不被修改。

metadata 和图片元数据复用已有检查；UI 事件 schema/身份及 todo schema 通过
新增纯校验 seam 复用真实读取器，释放所有解析对象且不投影或写侧车。尚未
完成 draft、queue、queue receipt、消息附件绑定及模型账本的完整 schema/ID
关系、图片实际解码或 xllm/UI 试恢复。`PreviewGet` 只返回已解码对象的复制
事实，成功不是完整恢复资格；manifest/上传的 `restore_ready:false` 保留。

兼容 v1 原始 meta/snapshot JSON 字节，不重序列化 snapshot 的校验内容。
预览明确 `ExportSchema:1`，只有两文件，不能称为完整带图备份，编码器也明确
拒绝把它包装成 v2。未知模型身份保留供查看，不作配置查询或模型调用。

`test_backup_decode_runtime.py` 从真实 idle 会话、正式草稿/待确认队列 API、
事件投影产生的 UI/todo 和普通 2 MiB artifact 开始，经正式下载/分段上传后
独立核对所有原字节的 hash。释放 upload pin/取消原上传后，解码对象仍保有
副本。HTTP/TLS 均覆盖 65 个小型错误输入、重复/转义键、版本/路径/大小/
hash/保留范围/身份/资源错误、UI/todo schema、下调预算、取消/截止时间和
失败后重试；全部验证前后 Home 可读文件逐字节相同。v1 精确字节与禁止
升级为完整格式通过。部分 schema 仍缺，本子阶段不把实施步骤 4–6 写成完成，
没有浏览器下载、真实恢复、压力或高负载测试。

本子阶段 Windows/Linux 完整有界门禁通过 114 Python、240 Node、90 模块、
严格 C11、35 运行探针、确定性 A/B 和三项 packed；Windows 另通过便携
WebView2 Home 及打包崩溃/20 秒启动。Linux 使用新 ext4 独立源拷贝，复用
上一阶段已重建/验证的相同锁定 xs 宿主，跳过 GUI。根目录程序已更新到
Windows A/B 同一包，SHA-256 为
`b2a3cbc20bf1b0112c13aedffa503b2e8024c3ba937c51f78e3fa773f02d4ab7`；Linux
包为 `b1780ce13c14da5013579941872743ecd1e09233f34f7a842105913d2cab2e78`。
最终日志为 `.build/qa-backup-decode-{verified,linux-verified}.log`。

初轮日志保留了三处探针问题：清除探针在 printf 只有半行时便终止进程；
下载探针的 250ms 截止时间从入场开始，慢编码时未到达发送暂停点；会话探针
手动复制源时缺少新增的内部头文件。前两处已提交 `6a51def`，分别等待 Unit 后
完整完成标记、在拥有该 context 的 executor 上于发送检查点开始短 deadline。
最后一处已补齐依赖复制。业务/零写入/真实背压/截止时间断言保持，未放宽
产品限额或跳过失败；最终两平台完整门禁包含全部修正。

## 共享侧车解析与离线 schema 校验

新增 `sessions/sidecars/{profile,binding,draft,queue,feedback}.{h,c}`，只消费
内存 JSON/值，拥有 DTO 的文本与提交分配。头文件明确 Parse 输出与 Unit/
Release 的生命周期；失败释放部分拥有对象，完整解析后不保留输入或 DOM
视图。session 层不引用 api 层。旧 HTTP profile 类型保留为核心 DTO 的别名，
原有构建响应和写文件仍在 api 中；原生附件绑定读取也复用纯 binding parser。

草稿 scope 显式区分 session/global/project；完整 profile 与项目部分选择
共享规则，保存的未知 model/reasoning 不查询 catalog。保持 session 草稿
1–7、队列 1–7、回执 1–3、反馈 1 和附件绑定 1 的格式。新解码还检查回执
内容 ID 与文件名一致、run 绑定与文件名一致，事件/运行数字文件名不能溢出
uint64。日常队列读取仍先解析，再按原规则读取/修复回执并协调 sending 状态；
离线仅检查格式，不能从已准备回执推导运行成功、写新回执或自动投递。

修正 JSON MaxValues：反馈的根 object/version/array 也占三个节点，512 项
需要 `512*3+3`；队列预算覆盖 20 项带四图片/完整 profile 和 256 个清理 ID
同时存在。文件大小、文本/图片数量、清理项和上传配额不变。队列清理 ID
可以指向已删除图片，仍不作为必须恢复资源。

扩展 owning decoder 探针保留真实会话/正式下载上传基础，追加侧车历史格式、
profile/字段/类型/身份/重复键和 ID、部分分配失败、文件名溢出及声明上限。
总计 115 个小错误输入，HTTP/TLS 都检查失败后可重试和 Home 零写入。
满队列的图片数据只是 pair/schema fixture，不宣称通过真实图片解码。
五个核心文件可分别按 C11/Wall/Wextra/Werror 编译；原 API 和恢复测试继续
覆盖日常读取的锁、回执晋级、编辑器草稿与重启恢复。

步骤 4 仍在进行：模型 snapshot/journal 严格 schema 与校验和/重放、图片实际
解码、保留 UI 的反馈/绑定/queue receipt 关系及 production worker 尚缺。
需要兼容合法裁剪后缺历史证据的侧车，不能简单要求所有旧 ID 都在 retained
UI 中。随后按步骤 5 实际试恢复、原子发布新会话，再接步骤 6 页面体验；
当前 schema 成功和 `PreviewGet` 事实仍不授权完整恢复。

Windows/Linux 完整有界门禁通过 114 Python、240 Node、90 模块、严格 C11、
35 运行探针、A/B 和三项 packed；Windows 另通过便携 WebView2 Home 与打包
崩溃/20 秒启动。Linux 是新 ext4 源拷贝，复用相同锁定 SDK/宿主并跳过 GUI。
Windows A/B 与根目录程序 SHA-256 同为
`d2d9f2bf24b1c176832f9283869ceaacd3d5be370b665ef1965a1c0f3f4633a2`；Linux
包为 `33530a65c903438bd4c3a013bb761fc788f73e6adf1cfcb8b936fa713cf850d0`。
日志 `.build/qa-sidecars-{verified,linux-verified}.log`；独立编译与 API/session/
run manager 的定向检查也通过。没有压力或高负载测试，未执行真实恢复。

## 保留历史的关系与未知引用

`backup_relations.c` 以 owning UI 原字节为输入，使用 live event visitor 构造
排序的数值事实及原字节 offset；callback 中的字符串不逃逸。索引及删除区间
数组各自不超过 UI 文件字节预算，receipt 数由 1024 文件预算约束。先严格
解析每条事件，按 ID 查找反馈/todo/绑定，删除区间排序合并后查找；不创建
完整日志 DOM，不向 Home/catalog/runtime 查询或写入。文件、事件、引用和
队列项间检查 deadline/cancel；单个 codec/JSON/qsort 等操作仍为合作取消。

保留范围内的正证据必须一致：反馈目标是成功 MODEL_DONE；todo 的 event
必须是成功主 Agent 的 mdo.todo，非截断文本可解析且 items 与侧车相等；
event 图片绑定必须是相同 run 的主 Agent START。UI 中队列绑定 START 必须
有同 ID 的 durable receipt；prepared receipt 的 Agent run 必须匹配，重复
START 身份拒绝。纯 `MdoQueueReceiptApply` 复用实时发送状态一致性规则；
只有已有 accepted receipt，或匹配的 prepared/start 证据，才能和 sending
项中非空 run_id 一致。验证不会提升回执、启动队列或改写输入。

前缀 retention 使旧引用不可再核实；这不构成被删证明。明确 marker 的
`[source_event_id,event_id)` 是被删证明，不能同时存在仍保留的事件。
缺少保留范围内或未来目标、非法 marker/重复 start 及 event ID UINT64_MAX
拒绝；删除区间内的侧车留作后续 staging 协调。预览分别报告未知和已删除
侧车引用次数，而非唯一 ID 数量；没有 UI 时保持未知，不伪造来源事实。
旧 source=0 marker 的 todo reset 规则与 live reconciler 相同，不替反馈/
附件凭空创造删除范围。

旧 run 数字可跨重启复用，`attachments/events` 的当前绑定覆盖 legacy runs；
不强行比较两个列表，legacy run 记录报告未知。缺少 surviving START 的
prepared/accepted receipt 保留为未知；schema 2 claim 也不能证明 accepted
运行身份。UI/账本的 sequence 对应、真实图片解码及 xllm replay 仍需后续
验证，零未知/删除引用数不表示完整恢复资格。

公开 preview Size 不匹配现在在任何清零之前返回，避免旧调用者的小缓冲区
被新结构长度覆盖；其他失败才清零已知大小对象。探针用真实 8 字节分配和
guard 验证成功/失败解码后都不越界写。扩展 corpus 共 134 个小错误输入，
另有 prepared/claimed/accepted、prefix/显式删除、数字 run 重用及缺失目标
的独立正反用例；在第一个严格 UI callback 之后注入取消，证明索引的失败
清理。HTTP/TLS 校验前后 Home 可读文件逐字节相同。首次探针仅因同名测试
变量覆盖失败，保留日志并改名后重跑。

Windows/Linux ext4 全部门禁通过：114 项 Python、240 项 Node、90 个前端
模块、严格 C11、35 个运行探针、3 个打包探针及独立 A/B 包一致；Windows
另通过便携 WebView2 Home 和打包启动 20 秒检查。
Windows 与根目录程序 SHA-256 为
`770a5ba8d41ff4485b9c69c149c0bb2704e8348f93327894f9218a72d19f4065`，
Linux 为 `5bc37b6bc5762d58f6be4202f39d90ebbb7cb007bc9bc91c05ccd6bc7917873b`。
日志 `.build/qa-relations-{verified,linux-verified}.log`。
没有真实恢复、压力或高负载测试。额外普通图片读回超时已登记为独立待修项，
这些通过结果不能证明全部附件传输和页面操作已恢复。

上述普通 PNG 读回超时已在后续图片发送阶段修复；图片与备份共用有界传输
函数，采用独立额度和生命周期。HTTP/TLS 及打包 VFS 的 GET/HEAD、备份带图
与移动 exe 重启读回已增加覆盖。这不替代离线备份中的实际图片解码、完整
恢复与页面验收；详见 `frontend-migration.md` 的普通图片附件发送记录。

## 独立模型上下文重放

2026-10-02 新增 `MdoSessionBackupReplayModel()`，只接受成功 owning decode
的对象，在新的最多 30 秒预算内调用库的 `xllmSessionRestore()`。默认预算
不复用 decode 已消耗的 deadline；文件数、单文件/总字节和取消先检查，模型
解析保留 262144 值/深度 32，并由库限制 journal 条数。返回对象独立拥有，
释放上传 pin、原文和整个 backup 后仍能渲染。错误只丢弃未发布的上下文，
原始文件不变；语义错误与可检查的历史格式分开报告。

库 API 从借用字节恢复，不访问文件、不继承路径、client、driver、hooks
或 cancel，也不发起模型/工具调用。使用与 Load/Recover 相同的 snapshot
loader 和七类 operation replay；v1/v2 兼容默认、工具配对、summary 质量、
checkpoint 和 rewind/clear 不另写状态机。journal 必须完整换行，残尾不
修补；CRC 使用原字节，CRLF 的换行 CR 不属于校验正文。解析预算、OOM、
取消和过期 deadline 各有错误，失败销毁整个 partial context。取消仍是
合作式，单次 native JSON 解析不能被强制打断。

源头在隔离 xrt worktree 的 `codex/mdo-session-restore` 分支，提交
`cb6c05f67e7f471e14b53d33208ef380bf0fbfbd`。该 extlib 基于原锁定库版本
`c88a4259`，xrt 单头仍单独锁定 `6040abda`，没有混入主工作树的其他修改。
同时将此前 SDK 的 pinned system prompt getter 纳入源头；xs 提交
`064952339ae425de3fd74d20b4b714ec5b3d30a2` 同步 15 个生产文件并更新
UPSTREAM/公开 TCC 符号。逐字节校验和 SDK 35 项门禁通过（Windows 跳过一
项 Linux 专用 build-plan），mdo deps.lock 同步新源 revision 与 tree hash。

库的 Windows/Linux 有界回归覆盖实际 snapshot+tail/journal-only、工具与
文件 ledger、v1/v2 fixture、撤回与清空、深所有权、逐分配点 OOM 和首条
重放后取消；byte restore 没有进入任何已武装的存储故障钩子。mdo HTTP/TLS
探针覆盖 Unicode/reasoning、旧格式/CRLF、covered checkpoint 去重、配对
工具和 ledger、压缩质量、截断、真实库 writer 的 rewind/clear，以及错误
turn、重复 call、孤立 result、deadline/cancel/budget、NULL error 和释放
backup 后渲染。失败后同一 backup 的文件 SHA-256 不变；237 种 malformed
输入继续通过，整个检查前后 Home inventory 逐字节相同。夹具的孤立源文件
清单与类型/字段接线在首次门禁发现后修正，失败日志保留。

步骤 4 尚缺模型/UI 的消息与删除边界对应、图片实际解码、生产预览 worker。
步骤 5 的 staging、新 ID/来源、修正已删除投影、非覆盖原子发布与 catalog
仍缺；步骤 6 正式菜单、三语进度/错误、完整页面导出恢复及原生/实体设备
也未验收。保持 `restore_ready:false`，内存上下文可重放不授权发布、队列
续行或调用模型。本阶段没有真实产品恢复、压力或高负载测试。

两平台最终有界门禁通过 114 Python、251 Node、90 模块、严格 C11、36
运行探针、三项 packed 与独立 A/B；新模块另有两平台独立严格 C11 检查。
Windows 另通过便携 WebView2 Home 与 20 秒启动。宿主本轮从新 SDK 重建，
清单修正后的门禁复用同一宿主；Linux 使用新 ext4 源拷贝并跳过 GUI。
根目录程序与 Windows A/B SHA-256 为
`a571f1ef2e080b050c589375cbc69368ae4e8b6ffaadb7dd8de30b0524164016`；Linux 为
`14ed2a06d4494bf4727dbce1ca09eaf09bfd20a9cba3a6d2341e15f929d64e12`。
日志 `.build/qa-session-memory-{final,linux-final}.log`，库的 bounded gate 在
`.build/qa-session-memory-{windows,linux}.log`。完整备份恢复和页面验收仍待后续。

## 保留模型账本与 UI 关系检查

2026-10-02 新增显式的 `MdoSessionBackupCheckModelHistory()`。只接受成功
解码的拥有对象，以新预算先重放独立模型，再用原始保留账本核对主 Agent
事件。不使用 `BuildRequest()` 的渲染结果建立身份索引，因为压缩/剪枝会隐藏
仍然保留的原始消息。解码、模型重放和关系检查是三个独立结果，关系失败
不得销毁可检查的原始备份。

xllm-session 增加 `xllmSessionEntryCount/EntryAt`：只读零分配的原始 entry
视图携带 sequence、turn、flags 及借用 message。只供单写入线程在稳定状态
下检查；任意修改/销毁使视图失效，不能当作跨线程快照。hook 内或瞬态状态
拒绝返回借用指针，size 不匹配不修改输出，其他失败清空。库的有界测试
覆盖分配故障下仍可读取、watermark 隐藏但原 entry 可见、撤回/清空、hook/
瞬态状态和短结构体。源头提交 `e2b990f290f212c7cb2352806219faf5f91ad8f5`；
xs 提交 `34fba960d1b2525a6bf64524358e6d9c32c4592e` 逐字节同步 15 个生产文件，
TCC 增加公开入口，deps.lock 锁定同一来源。xrt 核心版本保持 `6040abda`。

关系检查按以下规则处理保留范围：

- 用户事件的非零 `user_message_sequence` 必须对应同 turn 的普通 user
  entry；角色、turn、文字或重复投影冲突明确失败。没有序号的旧记录/续行
  只报告无法核对，不凭运行 ID 猜测 user 身份。
- 成功 MODEL_DONE 对应同 turn 的唯一 assistant；核对文字与重复投影。
  同 turn 多条旧 assistant 没有唯一依据，报告无法核对。失败模型事件不
  伪造 assistant；子 Agent 有独立账本，不与根账本混用。
- 工具按 turn 和 call ID 联合索引，核对 name 及 TOOL_START 的原参数。
  UI 的显示结果和模型的结果包装本来不同，不能直接逐字节比较。TOOL_DONE/
  RECOVERY_RESOLVED 核对实际持久化 result；TOOL_DONE 先于 result append
  的真实崩溃窗口标为不完整证据，不修改原数据或重新执行工具。
- 被明确截断的文字只核对已保存的前缀，同时记为无法完整核对。没有对应
  UI 的保留模型 entry 统计为投影缺口；system 和 synthetic/pinned 内容
  不要求与用户可见消息一一对应。已删除历史的侧车关系仍由既有 removal
  marker 校验检查，恢复时须在 staging 中处理已删除投影。

`MatchedUiRecords` 是已匹配身份的记录数；部分证据可以同时计入
`UnverifiedUiRecords`。`UnprojectedModelMessages` 是缺少 UI 投影的普通
模型消息数。返回 true 只表示没有证明矛盾，不能将这些计数当作完整恢复
资格。v1 没有 UI，仍可检查模型；缺口不会被补造。输出 Size 合同、预算/
取消/截止时间及失败清理与模型重放一致；解析/排序是有界协作操作，不是
能打断任意原生指令的硬超时。没有 Home/catalog、存储、模型、工具或队列
调用，始终保持 `restore_ready:false`。

HTTP/TLS 隔离探针通过真实产品运行生成两轮 user/assistant、模型快照与
UI 文件，再从正常备份入口捕获，得到四项匹配且无未核对/投影缺口。缩短
UI 保留前缀后明确报告两条模型消息缺口。测试源运行使用进程内固定响应，
不访问线上模型；它在零写入 inventory 基线之前执行，不能混淆源数据
生成与后续离线检查的零写入合同。其余小用例检查角色/序号/turn/文字/
工具身份/参数冲突、旧记录、子 Agent、截断、恢复事件、待持久化工具结果、
旧多模态、deadline/budget/cancel、NULL error、失败后重试、备份原字节保持。

本阶段发现一个需先修复的持久化缺口：当前 `xllm_session__write_entry`
没有写出 `pParts/iPartCount`，journal 的 add_message 共用同一个 writer。
mdo 带图请求的文字在 TEXT part 中而非 sContent，重新加载后图片与该文字
均可能缺失。已有有效图片绑定、但模型 entry 无 parts 的旧备份明确记为
无法完整核对；非空模型文字的明确冲突仍然拒绝。不能凭 UI 文字补写模型
账本或声称可完整恢复。后续必须在源库设计有版本、字节预算、二进制编码
及旧格式兼容的多模态持久化，并证明保存/重放/重启/备份恢复的实际 parts。
随后再完成图片实际解码、生产 worker、staging 发布、正式菜单和设备验收。

最终 Windows/Linux 有界门禁通过 114 Python、252 Node、90 模块、严格
C11、36 运行探针、三项 packed 和独立 A/B；Windows 另通过便携 Home
覆盖/搬移/重启与 20 秒启动。Linux 在新 ext4 源副本中从同一 SDK 重建
原生宿主，跳过 GUI；没有压力或高负载测试。根目录程序与 Windows A/B
SHA-256 为 `2d76be9b8f9aa88a20de852755c3799fecf7fc49a45847c7af70d4e67be43542`；
Linux 为 `4e74f65bfcb567070900c45867e976a9dca677c76b4b599adc34eafccaeadedb`。
日志 `.build/qa-model-history-{windows,linux}-final.log`，源库 bounded 回归见
`.build/qa-model-history-library-windows.log`、`.build/qa-model-history-linux.log`。
最终 size canary 使用分配器对齐的短存储，两平台 HTTP/TLS 定向补验通过，
日志 `.build/qa-model-history-probe-aligned-{windows,linux}.log`。生产包字节未变。

## 完整消息的多模态持久化

2026-10-02 的 xllm-session `799124f928dad085a045a1049b1d083b5ab1d194`
修复上一阶段发现的 writer 缺口。snapshot 与 journal 新写入版本均为 4，
沿用 v3 的 CRC、checkpoint 和记录序号。公开宏
`XLLM_SESSION_PERSISTENCE_SCHEMA_VERSION` 声明当前格式，mdo 构建核对
该宏与 deps.lock。读取兼容 v1–3；这些旧 entry 不得带 v4 字段，防止
错误降级后静默忽略消息内容。版本 5 及未来未知格式明确拒绝。

每个 v4 entry 要求 nullable `native` 与 `parts` 数组。普通消息使用空
数组；非空数组要求 `content:null`，避免 xllm parts 与文字 fast path
互相竞争。每个 part 必须完整携带以下八个字段：

| 字段 | 合同 |
| --- | --- |
| kind | 公共 enum 0–5；TEXT、REASONING、IMAGE、AUDIO、FILE、NATIVE |
| text、native_type、media_type、source_url、detail | nullable 字符串，不含 NUL；顺序和原文保留 |
| data_bytes | 非负整数，恢复时检查 size_t 范围及实际解码长度 |
| data | 零字节为 null；非零为规范带 padding 的标准 base64，包括原始 NUL/高位字节 |

message native 须为有效 JSON；native part 的 opaque text 不要求是 JSON。
恢复临时借用 DOM 字符串，AddPart 深复制后立即释放临时二进制；失败销毁
整个未发布上下文，不能返回部分恢复。URL/FILE 元数据只是原消息内容，
不会触发网络、文件、模型或工具访问。上传总字节预算约束编码数据，解码
前校验尺寸，取消/截止时间在 parts 之间及完成时检查；单次 codec 操作仍
是协作式有界检查，不冒充原生指令可中断的硬超时。

库的 Windows/Linux bounded gate 已验证真实 file load/recover、
snapshot+tail、journal-only、六类 parts 与 URL 图片、message native、
原字节和字段顺序、释放原始消息/文件缓冲后仍可用、取消及 session 分配
失败清理。有效 CRC 下的非法 enum、错误尺寸/非规范 pad bits、漏 parts、
版本降级/未知版本均拒绝。日志 `.build/qa-parts-library-windows-final.log`
与 `.build/qa-parts-linux.log`，没有压力或高负载测试。

xs `455b70fb2f9ec92a5d2e72f0d523d0bb85ba2525` 同步源库 16 个生产文件，
tree SHA-256 为 `10dc791e36074531609f606f6f2b8f72294ef8ab681970f4d3f6148712f232e6`；
94 个公开 TCC 入口保持，SDK 门禁通过。mdo 的模型文件 schema 检查同时
接受 v1–4；实际独立重放仍是另一项语义结果，不能以格式通过替代恢复。
模型/UI 文字核对无分配地拼接有序 TEXT parts，忽略 REASONING/NATIVE，
分段 Unicode、空段、跨段截断、颠倒顺序/混入推理/缺失文字均有回归。

HTTP/TLS 使用隔离副本和进程内响应，从正常产品入口运行三轮：首轮含
中文与真实 1×1 PNG，第三轮之前释放并重新打开会话。三次实际模型请求
均见原文和原图；正常备份捕获后独立重放核对完整图片 SHA-256。模型/UI
关系六项匹配且无未核对/缺口，缩短 UI 前缀后报告两条模型缺口。原 Ling
配置保持，测试模型仅在隔离副本声明图片能力。首次能力配置错误已修正，
没有放宽产品能力验证。257 个格式错误输入、失败重试、预算/取消、原始
备份字节及 Home inventory 继续保持。源会话生成发生在零写入基线之前。

尚需完成助手响应到消息的保真转换：目前 session 添加 assistant 时没有
保留 provider 的推理签名；xllmMessageFromResponse 添加 native part 会
清掉先前 content，直接改用它会引入丢字。须在 xllm 源头修复文字/签名
共存和响应转换，再在 session 中接入并做实际响应回归。v4 可保留已经
传入消息的这些字段，并不自动补全上游没有传递的响应内容。

已有旧备份丢失 parts 时继续报告无法完整核对，不从 UI 或附件倒推模型
消息。图片实际解码、生产预览 worker、独立 staging、来源/新身份、原子
非覆盖发布、catalog 与正式菜单仍待实现；`restore_ready:false` 保持。
本阶段不增加原生点击或实体设备证据。

最终 Windows/Linux 有界门禁均通过 114 Python、252 Node、90 模块、严格
C11、36 运行探针、三项 packed 与独立 A/B；Windows 另通过便携 Home
覆盖/搬移/重启与 20 秒启动。两端宿主本轮均由 SDK `455b70f` 重建，
Windows 完整门禁复用已建宿主；Linux 使用新的 ext4 源副本并跳过 GUI。
根目录程序与 Windows A/B SHA-256 为
`ea490f94e346925572b8d820d7d13d9fc8fa11868f3aae9c0da9f177fcf33e7c`；
Linux 为 `ae17c3a32d4f1c41a157bd9380a3e1bef565fd0416194112586d0b2965e3f51f`。
日志 `.build/qa-parts-{windows,linux}-final.log`。没有压力或高负载测试。

## 助手响应签名与正文保真转换

2026-10-02 源库提交 `5d16ece2b049fd73eee1d8e79551d6d70c6be216`，修复
上一阶段记录的响应转换缺口。Anthropic 要求回传 thinking 块时保留原文
和签名，签名是 opaque 字段，不能解释或拼接成另一块。
参见 [官方 thinking 文档](https://platform.claude.com/docs/en/build-with-claude/thinking)。

`xllmMessageFromResponse` 在存在签名时使用原有 parts 模型：按响应的
TEXT/REASONING 块顺序复制，每段 REASONING 后跟对应的 NATIVE part，
native_type 为 `thinking_signature`、text 为签名。TEXT parts 保留原始
正文，content 保持 null，空推理也有自己的 part。无 TEXT block 的
自定义响应可从 joined content 保存正文；普通 unsigned 响应继续保持
紧凑的 content 表示。输出深复制，不借用响应缓冲；分配失败释放整个
输出。count 非零却缺少 block/tool 数组的参数明确拒绝。

Anthropic encoder 优先用签名前的原始 REASONING part，既有 native-only
消息继续使用旧 joined reasoning，但不声称能重建旧分块。空推理仍编码
thinking 与签名。其他 dialect 跳过这个专有 signature part，原账本保留；
GLM reasoning_content 与可见正文分开。Completions 使用共用的 JSON
字符串转义生成分段正文，修复原先直接在引号中拼接文字导致的非法 JSON；
Anthropic 普通助手正文补闭合对象；Responses 的零可见 parts 使用空文字。
没有增加 public ABI、导出入口或持久化版本。

`xllmSessionAddAssistantResponse` 共用该转换，仍在成功追加后反馈原 usage。
这使正常运行保存的 assistant、history 容器、file/byte restore 与后续
请求都保留相同 parts。工具仍是独立数组，只保证数组本身的顺序，不映射
工具与正文的任意交错；redacted thinking 及其他 provider-native block
仍未接入。不能把本次 signed thinking 的修复写成全部 provider 回放完成。
v1–3 与已丢失内容的旧消息仍按前述不完整证据处理，不能补造数据。

xs 提交 `ef0e00608827655818424af6731cac919200f552`，逐字节同步 xllm
18 个生产文件、xllm-session 16 个生产文件。对应 tree SHA-256 为：

- xllm：`605bcc5d36bc66308a47325d115d713dfb7d1ddb7641319c57c00e67fcb29d66`
- xllm-session：`b75eca97d9859232fb43ba87164cdef7d3a0b42c368c1ab59b4cb4e37fd44230`

deps.lock、UPSTREAM 与公开目录保持同步，SDK 35 项门禁通过（Windows
跳过一项 Linux build-plan）。库 Windows/Linux bounded gate 覆盖已解析
Anthropic SSE、thinking/正文/工具共存、多签名和空推理、四种 dialect、
Unicode/引号/反斜杠/控制字符、跨协议过滤、原缓冲修改、全部转换分配点
失败、缺失数组、plain assistant 和 custom fallback；session 的真实
snapshot、journal-only、snapshot+tail、file Load/Recover 后再次编码均
保留原 signed thinking、正文和工具对。日志
`.build/qa-signed-{xllm,session}-windows-complete.log` 与
`.build/qa-signed-linux.log`。初次 Linux staging 脚本的前缀替换误改了
Path.parts，已修正 helper，未改变源码或放宽任何门禁。

mdo HTTP/TLS 隔离探针在三轮实际运行中返回两段正文、有字/空推理和
两项 opaque 签名；在第三轮前释放并重新打开源会话，下一次实际模型
请求仍有原 parts。正常捕获→独立重放核对全部 assistant parts 和
native_type，原中文/PNG 的 SHA-256、六项模型/UI 关系及缩短范围后的
投影缺口继续通过。固定响应仅用于测试自己的模型；没有线上真实签名
验证或 shell 调用。备份检查前后源字节和 Home inventory 不变，257 个
格式错误输入继续覆盖。生产预览 worker、图片实际解码、staging 发布和
正式完整备份菜单仍待完成，`restore_ready:false` 保持。

最终 Windows/Linux 有界门禁均通过 114 Python、252 Node、90 模块、严格
C11、36 运行探针、三项 packed 与独立 A/B；Windows 另通过便携 Home
覆盖/搬移/重启与 20 秒启动。两端宿主本轮均从 SDK `ef0e006` 重建，
Windows 完整门禁复用已建宿主；Linux 使用新的 ext4 源副本并跳过 GUI。
根目录程序与 Windows A/B SHA-256 为
`94b2468c5fe81fd387ee6a9aeca544a96718031860aea6a452de122f242fcbf1`；
Linux 为 `6d95c732bc8d52f0abb755e412026530bff83aff504bbd81b481abd2c85e550a`。
日志 `.build/qa-signed-{windows,linux}-final.log`。未做压力或高负载测试，
本阶段不增加原生点击或实体设备证据。


## 原生图片像素检查

步骤 4 增加 `MdoSessionBackupCheckImages`，与拥有解码、模型重放、模型/UI
关系检查保持独立结果。它遍历 attachment 原数据及重放后的所有保留 IMAGE
parts，通过 xs 可选 `image` 扩展逐张解码 PNG/JPEG/静态 WebP。只复制统计
事实，不保留 RGBA，不读 Home/网络、修改源字节或执行工具。URL/空引用
单列未验证，动画 PNG/WebP 目前拒绝；不得把这些情况改写成完整图片通过。

编码、尺寸、像素、全部 codec 动态内存、聚合操作/字节和 cancellation /
deadline 预算见公开头与迁移记录。取消不能抢占纯 CPU 解码循环，后续生产
worker 必须拥有输入、输出和取消生命周期，先释放网络 upload pin 后再做
检查，采用单个图片执行槽处理 WebP busy，并在发布前保留所有检查结果。
实际像素可解码不保证来源/视觉正确性，也不取代 SHA-256 或 ledger/UI 核对。

源依赖、许可证和适配器随 `deps.lock` 精确锁定，构建不下载图片运行依赖。
原生两平台检查包含精确 PNG/无损 WebP 像素 hash、JPEG/有损 WebP、极小
超限头、截断、CRC、错误压缩流、重入、取消、每个实际分配点的失败/重试。
产品探针检查附件及内嵌/URL，不在生产网络回调中解码。旧 1×1 PNG 夹具
的错误 IDAT CRC 已修正，原字节保真与可解码仍是不同检查。

这完成静态图片检查子项；生产预览 worker、staging、发布、菜单和设备
验收仍未完成，保持 `restore_ready:false`，不得自动调用模型或派发队列。

## 生产异步预览

步骤 4 的生产 worker 已完成：seal 上传后 POST 获得预览 ID，查询三个完成
门禁；共享从接受开始的 30 秒协作预算。单 worker/槽覆盖输入 pin、拥有
decode、模型/UI 与像素门禁、结果保留及清理，不能并行展开第二份大备份。
上传引用在 decode 后释放；失败、预取消跳过 Run 和退出均由 Drop 清理，
Unit 取消/join 后才退休上传 store 和 TCC code。同步 Submit/Drop 的重入已
用所有权预发布和非递归状态锁外提交处理，不在成功提交之后触碰 job；
提交拒绝由调用方走同一清理路径并保留小终态回执。

成功结果保持五分钟，过期在访问/退出时回收；DELETE 取消或丢弃 payload，
保留小回执供查询。回收不持状态锁，也占有 admission。相同上传 ID+SHA-256
重试复用，ID 改内容拒绝复用；失败可重新检查。没有 Home/catalog 写入、
模型调用、工具执行或派发队列。已确认 schema/模型/UI/图片的事实与未验证
缺口分别返回，v1 明确标为 partial，仍不授予完整恢复资格。

有界 HTTP/TLS 探针通过实际后台接口，验证 2 MiB artifact、草稿、待确认
队列、小 PNG、上传 pin 释放后删除/定额复用、HEAD/OPTIONS/token/body、
语义/像素错误、deadline/取消/重试、终态过期和 Unit 后再初始化，比较整个
Home 的原文件字节。没有压力或高负载测试。协议细节见
[预览 API](session-backup-preview-api.md)。当前只完成步骤 4 的后台预览部分；
staging UI 重放、投影修复、原子非覆盖发布及正式页面仍按步骤 5–6 实施。

最终 Windows/Linux 有界门禁通过 115 Python、252 Node、90 模块、严格 C11、
37 runtime、三项 packed 与独立 A/B；Windows 另通过便携 WebView2 Home
和 20 秒启动。packed 探针直接使用内置 API 完成下载/上传/预览，并在移动
程序后重启再检查，确认 source/原图统计/文件数量及 Home 原字节，队列不
派发。根目录程序为 6,385,507 字节、SHA-256
`bf135701deb7e04c4c1bad17a593c31a73ec505e15c71c96998954878c425271`；
Linux A/B 为 `6f7b14a017bcc3b2915bbc778ea4184b815ba6e284c3a9839032ffe0ea8f1324`。
日志 `.build/qa-preview-{windows,linux}-final.log`。没有压力/高负载测试，
没有本轮原生点击或实体设备验收；既有 Linux queued HEAD reset 仍未定因。

## 恢复后历史工具输出保护

步骤 5 的前置修复已完成：xwork runtime 编号会在重启后重置，旧 atomic
write 可能覆盖同路径产物。xwork 3.7.1 通过锚定目录、排他临时文件、完整
flush/close 和 `xrtRootRenameNoReplace` 发布，碰撞换编号，预算/ID 耗尽时
失败并保留原内容。registry 配额跨重试预约，所有元数据在发布前准备，
成功文件不进入失败删除路径。目录格式/ABI/事件 schema 保持，xrt 核心不改。

两平台库夹具及 native xs/TCC 三次真实异步 read run 核对旧文件原字节、
SHA-256、不同编号和临时清理通过，无网络模型或用户数据，无压力/高负载
测试。Windows link 创建权限不可用有明确 skip，Linux link 父目录拒绝通过。
源库 `c91c563e`、xs `ed87394` 已提交，mdo 锁定相应 20 个生产文件。
详见 [发布合同和界限](xwork-artifact-publication.md)。这个前置修复不是
staging 导入完成证明，完整恢复仍保持 `restore_ready:false`。
审查发现 Home 在项目 workspace 外时，当前 xwork 普通文件路径策略会拒绝
mdo 指定的产物目录；后续必须为宿主产物存储设独立锚定边界并验证真实
MdoAgentSession，不能通过放宽模型文件工具的 workspace 范围来修复。

本轮最终 Windows/Linux 有界门禁通过 115 Python、252 Node、90 模块、严格
C11、38 runtime、三项 packed 和独立 A/B；Windows 另通过便携 WebView2
及 20 秒启动，native xs/xsw 与根目录 mdo.exe 已更新。Windows 程序为
6,387,552 字节、SHA-256
`0758820636dfb04107c0d92e5db369df658b7fa85559acc51a9563a5fcd098a1`；
Linux 为 `da4cc36f9650a885a5064cba3cd76a3ce20d0bed889308e2685e43bf44fa0761`。
日志 `.build/qa-artifact-{windows,linux}-final.log`；没有压力/高负载测试，
既有 Linux queued HEAD reset 和原生/实体设备证据仍单独待处理。

## 外置 Home 与只读产物的接入修复

步骤 5 的另一个前置缺口已修复：xwork 3.8.0 / ABI 7 引入显式宿主 store，
引用保留已经锚定的目录，registry 读取也相对该根进行，显示路径不参与
IO。mdo 从 Home 根创建 store，内部输出保存与项目写权限分离；只读会话
保留完整输出，ephemeral Home 禁止保存。普通文件工具仍受 workspace
范围限制，不能通过 artifact store 取得项目外文件权限。

真实 mdo/TCC 小探针三次启动外置 Home 的同一只读会话，核对实际 Agent
run、事件桥、历史 HTTP API、重启与关闭后 Home 移动，保存每轮完整字节
和 SHA-256，项目外读取/项目内写入拒绝。源库夹具另核对活句柄移动、旧
位置替代和 Agent 销毁后读取。宿主须在 import/purge/Unit 前停止原生写入。
源库 `0f597d26`、xs `9bb75a5` 及 mdo 依赖均可追溯，详见
[宿主产物存储](host-artifact-store.md)。前节的外置 Home 缺口是修复前记录。

最终两平台有界门禁通过 115 Python、252 Node、90 模块、严格 C11、39
runtime、三项 packed 和独立 A/B；Windows 另通过便携 WebView2 及 20 秒
启动。root mdo.exe 和 native xs/xsw 已更新，Windows SHA-256
`c4a2e154553b7e8c244d08a606c919cfd0e35335d3a55416001e30ec005f1c27`；
Linux `2c7bfe3a2511a6013e653816b65fe69df60409740d895aec1cd0f4fee4ed1be1`。
日志 `.build/qa-store-{windows,linux}-final.log`。没有压力/高负载或新增原生/
实体设备验收；Linux queued HEAD reset 仍待排查。staging UI 重放、投影
修复、原子发布及正式菜单仍按步骤 5–6 实施，`restore_ready:false` 保持。

## 独立 staging 材料化与磁盘读回

步骤 5 已新增私有锚定目录的材料化入口。排他创建并分块写入/flush/close，
重新打开文件核对身份、长度及每块原字节；inventory 只接受拥有索引中的
对象。磁盘读回字节独立拥有，再通过共用 schema/CRC/引用、实际保留 UI
reader、xllm-session 模型/UI 关系及图片像素门禁。所有阶段共享 30 秒预算。
释放原 decoded backup/上传或关闭 caller root 后仍可重新检查与清理。

Windows 长期持有子目录句柄会阻止祖先移动，已改为只长期保留父锚定根，
每次操作按身份取得子根并及时关闭。原生根内父目录移动/旧位置替代通过。
清理仅删除已跟踪且身份匹配的文件和空目录；外来对象会阻止清理，非 NULL
handle 留给 caller 重试，不通过扫描递归删除，也不因取消而停止清理。

本 Stage 保留来源 metadata、UI 身份、artifact 路径和 queue 原字节；后续
新 ID/provenance、投影修复及待确认转换仍需完成，不能直接发布到 catalog。
强制终止后的 staging ownership/回收、原子目录发布和正式页面也未完成。
详见 [暂存合同](session-backup-staging.md)，保持 `restore_ready:false`。

HTTP/TLS 小型探针核对真实三轮 run、inline PNG、草稿/队列、2 MiB artifact、
每份落盘原字节和源 Home 不变。覆盖预算、预取消及第一块 artifact 后取消、
内容/身份篡改、外来文件及清理重试、模型/UI 矛盾和损坏图片失败；v1 拒绝。
最终 Windows/Linux 有界门禁通过 115 Python、252 Node、90 模块、严格 C11、
40 runtime、三项 packed 和 A/B；Windows 另通过便携 WebView2 及 20 秒
启动。SDK 不变且 native host 复用已验证版本，Linux 用新 ext4 源 staging。
根目录程序已更新，Windows SHA-256
`3c2c970924292a406394c376aecbe4b87bad5e3fda4eb9f358054327e2064eae`；
Linux `8958084d30e58f5725df3b15dabdf2fbb608f7555bb788e1dc2df9619aba0e82`。
日志 `.build/qa-staging-{windows,linux}-final.log`。没有压力/高负载或新增
原生/实体设备验收；Linux queued HEAD reset 仍待排查。
