# 会话 JSON 完整备份与恢复实施记录

状态：待实现完整格式与恢复入口。2026-10-01 已完成 checkpoint 与有界读取
共用排他运行窗口，以及统一捕获边界；`export_schema:1` 的内容仍只有 meta
和模型 snapshot。捕获基础通过验证不表示完整备份格式已经完成。
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
3. 定义版本化 manifest 与专用有界传输。当前普通 API 请求上限 256 KiB、
   下载上限 33 MiB，不能直接塞入图片和全部日志。应采用专用上传/下载边界并
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
