# 离线恢复的目标身份准备

2026-10-02：`MdoSessionBackupPrepareRestore` 返回独立拥有的 v2 副本。
一次调用按顺序完成历史投影修复、输入待确认转换、metadata/UI 身份重建
和产物显示路径重绑定，随后重新进行共用 schema、CRC、引用及历史关系
检查。原备份保持不可变，没有 Home/catalog、模型调用、工具或实时队列
操作。副本可交给私有 Stage 做实际模型/UI 重放关系和图片像素检查。

生产恢复 worker、持久事务所有权、异常退出回收、非覆盖原子发布、catalog
及正式菜单仍待实现，`restore_ready:false` 保持。这个内存 API 和私有
Stage 都不代替发布事务。

## 明确的目标与身份

调用方传入 Size 初始化的 target：项目 ID、预留的新 32 位小写 hex 会话
ID、明确的绝对 workspace 和正数 UTC 微秒恢复时间。没有从源 workspace
或来源文件推断目标的默认行为。缺失字段、无效 live metadata schema、
非绝对路径、控制字符、源 ID 或历史 source/target ID 重用均失败。
绝对路径语法兼容 POSIX、Windows drive 与 UNC，跨平台离线读取不要求
这些描述性路径在当前机器存在。

调用方仍必须在真实事务中验证并预留目标项目、会话 ID 与 canonical
workspace，检查 Home/catalog 冲突和目标文件系统。此纯函数不能证明
目录存在、项目授权或出版能力，也不打开源/目标路径。

新 metadata 的 revision 是 1，创建/更新时间使用明确的恢复时间；状态
active、previous active，取消 pin、旧 fork 父链接和 fork sequence，清空
旧 config/model/module/skill generation。原 title、Agent/model/protocol、
reasoning、权限 profile 与输出上限保持描述性字段；未安装的 Agent/model
也可以保留供查看，实际继续运行前仍需验证有效 profile。

## 历史与便携产物

保留 UI 的 event ID、时间、turn/run/task/tool 身份、用量、text、模型字段
及其他内容，仅更新顶层 project/session identity。原 schema 1–5 不升级，
JSON 行会重新编码，保持字段值而不承诺 UI 原排版的字节稳定。
模型 snapshot/journal、图片、artifact 和不需要修复的侧车原字节保持。
历史 run/task ID 是历史记录，不能据此恢复实时执行。

每个非空 `artifact_path` 用共用尾路径解析器匹配真实 manifest 文件，
处理两平台分隔符并拒绝歧义，改为 `artifacts/run-…/…txt`。消息 text 和
工具参数中的路径不改写。历史读取器根据当前项目、会话和 event ID 在
Home 下定位文件，不依赖源绝对路径。准备阶段另外检查最终历史读取路径
的 256 字节上限；长项目 ID 与长文件名组合超限时明确失败。

## 持久来源文件

副本总是新增/追加 `restore-origin.json`，即使没有 UI 或输入侧车。
schema 1 的 `imports` 条目包含捕获/恢复时间、精确 UTF-8 的 source/target
metadata、各自的 bytes/SHA、UI 是否存在及源 UI 指纹，以及按 event ID
排序的旧/新 artifact path 映射。原始 fork、pin、generation、workspace
和 profile 可从源 metadata 追溯；每次转换保留之前条目的语义、源字节
及校验和。外层 provenance JSON 会重新编码。

共用捕获白名单、编码/拥有解码、私有 Stage 写入/读回/清理都携带该文件。
旧 v2 的 absent 清单仍兼容。每个 metadata 记录复用真实 reader 和内层
SHA 校验；重算外层备份 SHA 不能隐藏 schema、长度或身份转换矛盾。
目标 metadata 的生命周期/profile 转换、ID 唯一性和 artifact 映射均验证。

来源信息是被动描述，不作为执行、路径打开或发布授权。SHA 不认证来源；
源 UI 只存指纹和变更路径，未另存完整旧 UI 字节，历史指纹只能检查格式。
不能用它认证旧 UI，也不能把来源 metadata 当成实时的 target 授权。
后续用户已删除的历史产物不要求为来源记录重新获取。

历史最多 16 条、来源文件最多 8 MiB，artifact 映射最多 32,768 项，同时
受 caller 较小的文件数/单文件/总字节预算约束。达到上限整体失败，不
截断历史。UI 新分配按结果文件/总字节预算封顶，完整分配后才转移所有权。
metadata、UI、输入和来源任何一步失败均释放整份副本，清空 Facts，仅
保留 Size；错误的 Facts.Size 不接触调用方内存。源对象仍可重试。

同一个 30 秒协作截止时间覆盖所有阶段；取消在文件、UI 行、来源条目、
映射及哈希操作前后检查，单次 native JSON/SHA 操作不能被抢占。生产需
使用有界 worker，不能从网络回调调用此 API。

## 有界验证与剩余证据

HTTP/TLS 探针使用真实三轮本地模型/UI 账本和固定普通 2 MiB artifact。
产物来源事件是受控夹具。核对目标 metadata、逐条 UI 字段、源/target
metadata 原字节/SHA、便携路径与实际历史读取器的路径计算；释放上传/
原备份/准备副本后，Stage 仍独立读回、原生 encode/decode 与重新检查。
真实前端控制器使用实际 target key 刷新/reconcile/pump，只有 GET。

覆盖重复导入、历史 ID 冲突、未安装 Agent/model 身份的保留、不存在/空 UI、
POSIX/drive/UNC、长读取路径、输入转换和来源新增的文件数/字节配额，
UI 行后与来源 SHA 后取消、较小 Size、内层损坏和重算 SHA 后的转换矛盾、
16 条追加失败/17 条拒绝。源 Home 全文件库存和源编码前后相同。

初始前端探针假定 draft 一定有 submission，遇到只有 composer 的有效
草稿报 TypeError；已修正测试适配器，使用真实 metadata target key，并
分别验证有/无 submission 时不自动派发。初始日志保留。没有压力或高
负载测试。后台逻辑和路径计算不替代正式恢复页面、目标 Home 的实际
HTTP 产物读回、原生点击、实体移动端或 Linux queued HEAD reset 验收。

最终 Windows/Linux 有界门禁通过 115 Python、252 Node、90 JS 模块、严格
C11、40 runtime、三项 packed 及独立 A/B；Windows 另通过便携 WebView2
Home 和 20 秒打包启动。SDK 未变化，复用已验证 native host。Linux 使用
新的 ext4 staging，十三份代码/清单/探针输入与 Windows 按 LF 归一化
核对。根目录 `mdo.exe` 更新为 6,414,192 字节、SHA-256
`03e14cac3b5936c405f34729a199ed894518a2ddce7f3d02f036c856d856d228`；
Linux A/B 为 6,464,352 字节、SHA-256
`6ee02ce391ffe121f3754f53ea17dfa68d9441cf43bc064335a4702095d37f31`。
日志 `.build/qa-restore-target-{windows,linux}-final.log`，源码核对记录
`.build/qa-restore-target-source-equivalence.log`。发布时无运行的根目录窗口。
