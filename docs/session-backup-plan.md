# 会话 JSON 完整备份与恢复实施记录

状态：专用下载/上传已接入，完整校验与恢复入口待实现。2026-10-02 已完成
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
4. 先实现离线验证与预览，再做恢复事务。验证所有 schema、路径、ID、内容
   校验和引用；拒绝链接、绝对路径、`..`、重复文件和大小声明失真。v1 只能
   预览为模型快照，不能误报为完整带图备份。未知模型/Agent 的会话可保留
   原数据供查看，继续运行前重新选择并验证有效 profile。
5. 恢复到独立 staging，验证实际 xllm 恢复与 UI replay 后，以不覆盖的原子
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
