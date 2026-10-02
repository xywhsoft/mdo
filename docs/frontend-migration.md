# 前端交互迁移记录

目标是保留 `app_bak/wwwroot` 经验证的操作逻辑，逐步迁到 `app/web` 的模块化实现。页面布局可改进；输入、运行、审批、消息操作必须以服务端状态为准。原生 HTML、CSS、JavaScript 保持无包管理器构建，随 `mdo.exe` 一起打包。

完成判据及尚缺的打包页证据见 [前端操作体验完成审计](frontend-completion-audit.md)。本文件继续记录每次实现和验证的细节。

## 2026-10-02：离线恢复的目标身份准备

新增 `MdoSessionBackupPrepareRestore`，用同一个协作预算依次修复投影、
把输入转为待确认、重建明确的目标 metadata/UI 身份并重绑产物路径。
新 revision/时间/状态、pin/fork 和 generation 均按新会话生命周期处理，
原 profile 与历史内容保持。来源不用于推断 workspace 或执行授权。
`restore-origin.json` 保留 source/target metadata 精确字节、UI 指纹和
旧/新产物路径，随备份及 Stage 保存，历史 ID 重用、schema/映射矛盾、
配额和取消整体失败。

真实模型/UI 账本和资源原字节、实际落盘副本/释放后重查及原生编码重放
核对通过，实际历史读取器的路径计算与便携引用一致。真实前端控制器
以新身份刷新仅 GET。初始探针对 composer-only 草稿的 submission 假设
已修正，失败日志保留。详见 [准备合同](session-backup-restore-preparation.md)。
生产 worker/异常回收、原子发布/catalog、正式页面及目标 Home 的实际
HTTP 读回仍继续实施，`restore_ready:false` 保持。没有压力/高负载或新增
原生/实体设备证据；Linux queued HEAD reset 仍待处理。

本轮最终 Windows/Linux 通过 115 Python、252 Node、90 JS 模块、严格 C11、
40 runtime、三项 packed 和独立 A/B；Windows 另通过便携 WebView2 Home
及 20 秒打包启动。SDK 未变化，native host 复用已验证版本；Linux 新
ext4 staging 的十三份代码/清单/探针输入与 Windows 按 LF 归一化核对。
根目录 `mdo.exe` 已更新为 6,414,192 字节、SHA-256
`03e14cac3b5936c405f34729a199ed894518a2ddce7f3d02f036c856d856d228`；
Linux A/B 为 6,464,352 字节、SHA-256
`6ee02ce391ffe121f3754f53ea17dfa68d9441cf43bc064335a4702095d37f31`。
日志 `.build/qa-restore-target-{windows,linux}-final.log`，源码核对记录
`.build/qa-restore-target-source-equivalence.log`。发布时无运行的根目录窗口。

## 2026-10-02：恢复输入的来源记录随副本保存

输入转换现在自动新增/追加 `restore-inputs.json`，保留原 metadata/queue/
draft 精确 UTF-8 字节、内层 SHA 和 source/review 映射。普通备份的共用
白名单/验证器及私有 Stage 均携带该文件，旧 v2 的 absent 清单仍兼容。
真实 codec、源长度/哈希和映射关系拒绝矛盾；历史受理描述不参与当前
执行授权。二次转换保留来源并继承尚未确认输入的不确定性，新 ID 避开
历史映射。16 条/8 MiB 及 caller 较小预算超限整体失败，不丢旧记录。

独立落盘、释放源对象后重查、原生 encode/decode 原字节、二次转换、
内层损坏/codec/映射拒绝、hash 后取消及配额失败/重试探针通过，源 Home
不变。初始夹具同步发送大响应遇到超时，已改为进程内核对/返回小结果，
生产传输门禁未改变。详见 [来源合同](session-backup-input-provenance.md)。
身份/workspace/artifact 重绑定、worker/异常回收、原子发布/catalog 和
正式页面仍继续实施，`restore_ready:false` 保持。没有压力/高负载或新增
原生/实体设备验收；Linux queued HEAD reset 仍待处理。

首次两平台门禁在旧会话探针报来源验证器未定义，已补齐其手写 TCC
清单并拆出共享纯函数。生成 unity 清单刷新后的定向探针通过；初始失败
日志保留，最终两平台结果在下文记录。

最终 Windows/Linux 通过 115 Python、252 Node、90 JS 模块、严格 C11、40
runtime、三项 packed 和独立 A/B；Windows 另通过便携 WebView2 Home 及
20 秒打包启动。SDK 未变化，native host 复用已验证版本；Linux 使用新的
ext4 staging，十二份代码/清单/探针输入与 Windows 按 LF 归一化核对。
根目录 `mdo.exe` 已更新为 6,407,289 字节、SHA-256
`d909eac42fb3f7c6457dcf64457af89e715f1a3616431902c4c71e80c9473403`；
Linux A/B 为 6,457,449 字节、SHA-256
`c8de9cc2f786c3354a1d03709f91c5ff203b8485705554dc74d0c1d1889928af`。
日志 `.build/qa-input-origin-{windows,linux}-final.log`，源码核对记录
`.build/qa-input-origin-source-equivalence.log`。发布时没有运行的根目录窗口。
完整门禁后仅移除新纯函数文件的一行末尾空行，两平台再通过严格 C11、
独立 A/B 和三项 packed，Windows 另重过便携 Home 及 20 秒启动。
上面的最终字节来自 `.build/qa-input-origin-{windows,linux}-post-format.log`；
完整行为门禁的日志保留，不将格式修整后的打包校验冒充重跑全部探针。

## 2026-10-02：恢复输入转成待确认

新增独立 `backup_submissions.c` 和 filesystem-free
`MdoSessionBackupReviewInputs`。复用历史索引及真实 queue/draft codec，
有受理证据的旧输入不再入队；其他队列项用新 ID 转 staged，草稿意图用
新 ID 转 rejected 审核状态，匹配副本合并，矛盾 payload 整体失败。
保留 profile、图片、原文本及顺序，清空旧 discard/直接运行关联。原回执/
模型/UI/资源文件不变，Facts 记录来源映射及不确定受理。后续事务必须
保存源意图及 Facts 为 provenance；此副本单独不能发布。

源备份前后编码一致、源 Home 不变、独立磁盘读回及实际前端控制器刷新
只读探针通过。覆盖取消/截止/碰撞/增长失败与重试、固定 20+20 容量、
Unicode trim 和未知起动状态。初始字段夹具修正及自身碰撞误判修复后
重新验证，初始日志保留。详见 [输入转换合同](session-backup-inputs.md)。
新身份、artifact 路径、持久 provenance/worker、原子发布、catalog 和
正式页面仍待完成；`restore_ready:false` 保持。没有压力/高负载或新增
原生/实体设备验收；Linux queued HEAD reset 仍待排查。

最终 Windows/Linux 有界门禁通过 115 Python、252 Node、90 JS 模块、严格
C11、40 runtime、三项 packed 和独立 A/B。新增断言及实际前端控制器检查
扩展已有 runtime 探针，未增加探针数量。Windows 另通过便携 WebView2
Home 和 20 秒启动。SDK 不变、native host 复用已验证版本；Linux 新 ext4
源 staging 的十份代码/清单/探针输入与 Windows 按 LF 归一化核对。
根目录 `mdo.exe` 已更新为 6,402,466 字节、SHA-256
`c2c76096fd0973a1d2ac371ae3e8074cf96fd9fa2c2b714585b30084ac6d261f`；
Linux A/B 为 6,452,626 字节、SHA-256
`49a769bbb80dd8a9d4d9ec4b93431b271bb669e339acbcdd9a946698371d6f41`。
日志 `.build/qa-inputs-{windows,linux}-final.log`。

## 2026-10-02：恢复前的历史侧车投影修复

新增 filesystem-free `MdoSessionBackupReconcileHistory`，返回独立拥有的 v2
副本，只处理确有证据的侧车变化。复用 decoder 的历史索引和删改区间：
移除已删除事件的图片绑定/反馈，重建落后、缺失或来源失效的 todo；未知
retention 前缀和旧 run 绑定保留原字节及未验证报告。旧零来源 clear 仅
作用于 todo，并丢弃 clear 前的候选，不能扩大成图片/反馈删除授权。

未改的模型/UI 账本、metadata、图片原数据、artifact、draft/queue/receipt
保持原字节。Stage 读回及投影复制共用取消感知的 owning clone，修复结果
重新跑 schema/CRC/引用和历史关系检查。新增 todo 或编码增长仍受输出
预算约束；失败只释放副本、清空 Facts，原备份不变。全部步骤共享 30 秒
协作预算，不调用模型、工具、队列或 catalog。

扩展现有 HTTP/TLS 小探针，核对三类删改投影、保留项、未知前缀原字节/
计数、clear 限定作用、todo 落后/缺失、增长预算、末条坏 todo、取消及
失败重试。原备份前后编码一致，原 Home 字节不变；释放修复副本后 Stage
还能重查模型/UI 关系与静态像素。旧 todo 样例没有 turn/工具 ID，仍明确
报告 model 未验证，不能算作完整工具运行证据。详见
[投影合同](session-backup-projections.md)。

新身份/provenance、artifact 重绑定、队列待确认转换、生产恢复 worker、
原子发布、catalog 通知和正式页面仍待完成，`restore_ready:false` 保持。
没有新增原生点击/实体设备证据或压力/高负载测试。

最终 Windows/Linux 有界门禁通过 115 Python、252 Node、90 JS 模块、严格
C11、40 runtime、三项 packed 和独立 A/B；Windows 另通过便携 WebView2
Home 与 20 秒启动。SDK 未变化，复用已验证 native host；Linux 使用新的
ext4 源 staging，七份生产/头文件/探针输入与 Windows 按 LF 归一化核对。
根目录 `mdo.exe` 已更新为 6,397,671 字节、SHA-256
`e05cb327c3da1b1b400cf4cdd37980a92a8127a580db04d9bfccd319474e23e1`；
Linux A/B 为 6,447,831 字节、SHA-256
`85f745d6d131f780fee766807ea388106c41375e3aed4e09c384778e01c53615`。
日志 `.build/qa-projection-{windows,linux}-final.log`。Linux queued HEAD
reset 及实体设备验收仍未闭环，本轮通过不改变其状态。

## 2026-10-02：完整备份的独立暂存材料化

新增 `MdoSessionBackupStagePrepare/Check/InfoGet/Discard`，将拥有解码的
v2 备份排他写入 caller 提供的私有目录锚定根，再从磁盘独立读回。文件
身份、长度和原字节匹配，inventory 拒绝未知对象；真实保留 UI reader、
xllm-session 模型/UI 关系及静态像素门禁检查读回数据。所有阶段共享 30
秒协作预算，64 KiB IO 分块；没有模型、工具、队列或 catalog 调用。
返回后可以释放原备份/上传和 caller root，Stage 保留自己的字节及父根。

清理仅处理已登记且身份匹配的 owned 文件和空目录。发现外来文件或替换
身份时留下该对象及 handle，由 caller 处理障碍后重试；取消不阻止清理。
Windows 夹具发现长期打开子目录会阻止父目录移动，已改为只保留父锚定
根，操作时按身份打开子根、返回前关闭，原生根内移动/旧路径替代后检查
与清理通过。没有修改基础库或 ABI，也未增加默认 Agent 工具。

HTTP/TLS 小型探针使用真实三轮 mdo run 的模型/UI/inline PNG、草稿、
待确认队列和普通 2 MiB artifact，逐文件核对落盘原字节、全部 quota、
预取消/第一块 artifact 后取消、同身份篡改/同字节外来身份、外来文件
拒绝及清理重试。实际模型/UI 矛盾及损坏像素在对应门禁拒绝；旧 v1
不能暂存，失败后重试可用，整个源 Home 原字节不变。初始夹具修正了
queue 必需字段、array 断言和 v1 原字节 CRC framing；生产目录句柄
生命周期修正后才通过移动探针。构建清单断言补上新模块，初始日志保留。

细节见 [暂存合同](session-backup-staging.md)。本阶段保留来源 metadata、
UI 身份、artifact 显示路径和 queue/receipt 原字节，只是后续恢复事务的
输入。新身份/provenance、投影修复、队列待确认转换、产品 UI replay、
原子发布、catalog 通知和正式页面仍未完成，`restore_ready:false` 保持。
没有新增原生点击/实体设备证据或压力/高负载测试。

最终 Windows/Linux 门禁通过 115 Python、252 Node、90 JS 模块、严格 C11、
40 runtime、三项 packed 及独立 A/B；Windows 另通过便携 WebView2 Home
与 20 秒启动。基础库/SDK 版本不变，复用上一阶段已经从锁定 SDK 新构建
并验证的 native host；Linux 使用新的 ext4 源 staging，六个变更源/构建
清单/探针文件与工作区内容一致。根目录 `mdo.exe` 已更新为 6,395,308 字节，
SHA-256 `3c2c970924292a406394c376aecbe4b87bad5e3fda4eb9f358054327e2064eae`；
Linux A/B `8958084d30e58f5725df3b15dabdf2fbb608f7555bb788e1dc2df9619aba0e82`。
日志 `.build/qa-staging-{windows,linux}-final.log`；初始清单断言失败日志
保留为 `.build/qa-staging-{windows,linux}-unit-initial.log`。既有 Linux
queued HEAD reset 仍未定因，不能以本轮通过替代该项的原因及设备验收。

## 2026-10-02：外置 Home 与只读会话的完整工具输出

上一阶段记录的 Home 在 workspace 外时产物保存失败，本阶段已修复。xwork
3.8.0 / ABI 7 增加宿主显式传入的引用计数 artifact store，克隆已经锚定的
xrt 目录，写入及 registry 读取都使用目录句柄和受控相对路径。显示路径仅
用于追溯，目录移动或旧路径替换不会改变 registry IO。沿用非覆盖发布、
碰撞预算与失败清理；普通模型文件工具的 workspace 范围不扩大。

mdo 从 Home 锚定根创建每个会话的 store，拒绝 Home 外参数、非法路径、
文件叶及重启冻结时新 store 获取。只读 profile 也保留 Home 内完整输出，
项目写权限和写后验证仍由 profile 决定；ephemeral Home 继续关闭产物保存。
宿主在 import/purge/Unit 前须先停止原生写入者。详见
[宿主产物存储合同](host-artifact-store.md)。源库 `0f597d26`、xs `9bb75a5`
已提交，20 个生产文件及树 SHA 锁定，TCC 注入 172 个 xwork 公开函数。
ABI 升级要求调用方重编译，mdo 模块 ABI 1 和事件 schema 3 保持。

新增真实 mdo xs/TCC 探针，连续三次启动同一只读会话、项目外 Home 和
本地模拟模型：超过 inline 阈值的 read 经产品 event bridge 保存；项目外
读取和项目内写入被拒绝。重启、关闭后移动 Home 再启动均保留每轮原字节，
以持久 event ID 经真实历史产物 HTTP API 读回长度、SHA-256 和内容。库
夹具另验证 caller root 关闭、目录移动/旧位置替代及 Agent 销毁后读取。
Windows/Linux 库测试均通过；初始显示路径相对/绝对断言失败已修正并重跑，
失败日志保留。Windows link 创建权限限制的既有夹具明确跳过。

最终两平台门禁通过 115 Python、252 Node、90 模块解析、严格 C11、39
runtime、三项 packed 与独立 A/B。Windows 另通过便携 WebView2 Home
覆盖/移动重启和 20 秒启动。两平台均从锁定 SDK 重建 native host，八个
变更源文件/锁/探针的 Linux staging 与 Windows 工作区内容核对一致。
根目录 `mdo.exe` 已更新为 6,390,227 字节，与 Windows A/B 一致，SHA-256
`c4a2e154553b7e8c244d08a606c919cfd0e35335d3a55416001e30ec005f1c27`；
Linux 为 `2c7bfe3a2511a6013e653816b65fe69df60409740d895aec1cd0f4fee4ed1be1`。
日志 `.build/qa-store-{library-windows,library-linux,windows-final,linux-final}.log`。

便携优先：WebView2 缓存仍在 `mdo-home/data/cache/webview2`，首次窗口
启动允许创建。正式导出/恢复菜单、staging 发布及完整恢复仍未完成，
`restore_ready:false` 保持；既有 Linux queued HEAD reset 仍待排查。
本阶段没有压力/高负载测试或原生点击/实体设备验收增量。


## 2026-10-02：重启后工具产物的非覆盖发布

恢复发布前发现 xwork 的 runtime run/artifact 编号会重置，而旧产物写入调用
`xrtFileWriteAtomic` 会替换已有同名文件。恢复后的会话或普通程序重启都有
覆盖旧工具输出的风险。xwork 3.7.1 改为根内排他临时文件、完整写入/flush/
close 后使用现有 `xrtRootRenameNoReplace`；碰撞只换编号，128 次或编号耗尽
明确失败，其他 I/O 不重试。失败只清理本次拥有的临时文件和配额；全部
登记内存在发布前准备，最后改名和 registry 登记共用锁，已发布目标不进入
失败删除路径。目录格式、ABI 6、事件 schema 3 不变，没有修改 xrt 核心。

源库提交 `c91c563e`，xs 同步 `ed87394`，20 个生产文件与上游一致，依赖及
生产树 SHA 已锁定。Windows/Linux 库小型夹具核对独立 runtime、重启原字节、
rename 瞬间的竞争目标、成功编号/单次事件、碰撞预算、I/O 立即失败、ID
耗尽、空文件和目录目标。Linux link 父目录拒绝通过；Windows 创建 link 的
权限不可用，明确跳过。两平台均没有临时文件/预约泄漏，没有压力测试。

新增 xs/TCC 运行探针驱动本地模拟模型和一次真实异步 read run，每个平台
连续启动三次，沿用 workspace/run 目录并改变输入；核对三份独立产物的 ID、
SHA-256、每轮旧字节和无残余临时文件。没有网络模型或用户 Home 数据参与。
首次夹具错误地用直接 executor 预期异步 run 号，已改为真实 run 并通过；
锁版本断言同步为 3.7.1。原始失败日志保留，没有绕过产品检查。

细节和取舍见 [工具产物发布](xwork-artifact-publication.md)。本轮是 staging
恢复前的数据保护修复，正式导出/恢复菜单及完整恢复仍未完成，
`restore_ready:false` 保持。便携 WebView2 路径仍是
`mdo-home/data/cache/webview2`，按用户选择允许首次启动创建该目录。

审查时还记录一个既有接入缺口：Home 在项目 workspace 外时，mdo 指定的
产物目录会被 xwork 的普通 workspace 路径策略拒绝。后续要设计宿主产物
存储的独立锚定边界并通过真实 MdoAgentSession 验证，不能放宽所有文件
工具路径。本轮 native xwork 的同工作区探针不作为该缺口已修复的证据。

最终 Windows/Linux 门禁通过 115 Python、252 Node、90 模块解析、严格 C11、
38 runtime、三项 packed 与独立 A/B。Windows 另通过便携 WebView2 Home
覆盖/移动重启和 20 秒启动。根目录 `mdo.exe` 已核对为 6,387,552 字节，与
Windows A/B 完全相同，SHA-256
`0758820636dfb04107c0d92e5db369df658b7fa85559acc51a9563a5fcd098a1`；
Linux 为 `da4cc36f9650a885a5064cba3cd76a3ce20d0bed889308e2685e43bf44fa0761`。
两平台重新生成 native host，xs/xsw 已更新到 mdo 工作区；Linux 完整重跑
复用同一已验证新宿主，不沿用旧版本。日志
`.build/qa-artifact-{library-windows,library-linux,windows-final,linux-final}.log`。
既有 Linux queued HEAD reset 仍未定因，不因本轮通过而关闭该项。
没有压力/高负载测试或原生点击/实体设备验收增量。

## 2026-10-02：生产离线备份预览 worker

已 seal 上传可以通过生产 API 启动单 worker 预览，立即获得独立 ID，并查询
decode → model/UI → images 三个门禁进度。检查共享 30 秒协作预算；查询和
响应不等待完整解析、重放或解码。网络回调只取得 pin/复制摘要，解码后即
释放上传 pin，后续持有独立备份；结果只保留文件字节和已复制事实，不保留
模型、RGBA、连接或 Home/runtime。成功摘要报告旧 v1、未验证引用、无 UI
投影和远程图片缺口，不将它们伪装为完整恢复。

同一上传 ID 与 SHA-256 的 POST 重试复用现有预览；同 ID 改内容不复用旧
成功。一个活动或保留结果占有槽，DELETE 协作取消/释放，终态小回执继续
可查；失败可重新开始，结果五分钟过期后访问回收。大对象释放不持状态锁，
回收期间仍阻止新解码。Unit 先取消/join 再关闭上传存储及允许 TCC 卸载。
预取消探针发现 Submit 可以同步调用 Drop，已改为提交前发布 ownership，
在非递归状态锁之外提交，成功后不再访问 job；未执行 Run 也释放 pin，
拒绝提交走相同清理路径保留失败回执，取消在发布前赢过成功。

HTTP/TLS 确定性小探针通过：真实 2 MiB artifact、草稿、待确认队列及小 PNG
经过生产路由；传输 pin 释放/上传删除/定额复用后检查仍成功，Home 全文件
原字节不变。覆盖 token/body/HEAD/OPTIONS、重复/冲突、解码/模型/像素阶段
失败、deadline、三个阶段取消、预取消跳过 Run、Unit join/重新初始化和
成功/过期清理；真实关闭池导致的提交拒绝、全部状态锁无递归失败及清理后
成功重试。没有压力或高负载测试。

接口合同见 [离线预览 API](session-backup-preview-api.md)。独立 staging、
来源/新身份、原子非覆盖发布、catalog 与正式页面仍待实现，
`restore_ready:false` 保持；本轮没有原生点击或实体设备验收。

最终 Windows/Linux 有界门禁均通过 115 Python、252 Node、90 模块解析、
严格 C11、37 运行探针、三项 packed 与独立 A/B；Windows 另通过便携
WebView2 Home 覆盖/移动重启及 20 秒启动。新增 packed 证明直接使用单文件
内置 API 完成下载/分段上传/预览，核对原图像素统计、源身份与精确文件数，
移动程序后重启再做同一检查，期间 Home 原文件与待确认队列保持。

根目录 `mdo.exe` 已更新为 6,385,507 字节（约 6.09 MiB），与 Windows A/B
SHA-256 一致：
`bf135701deb7e04c4c1bad17a593c31a73ec505e15c71c96998954878c425271`。
Linux A/B 为 `6f7b14a017bcc3b2915bbc778ea4184b815ba6e284c3a9839032ffe0ea8f1324`，
使用新 ext4 源目录 `/home/ubuntu/.cache/mdo-linux-preview-l0ned97r`，复用
首轮新构建且已完整验证的同一锁定 SDK `0c8c533` 原生宿主；关键源文件
11 项与 Windows 工作区 LF 归一后逐字节相同。日志
`.build/qa-preview-{windows,linux}-final.log`，首轮成功日志保留为
`.build/qa-preview-{windows,linux}-first-complete.log`。既有
[Linux queued HEAD reset](linux-image-queued-head-reset.md) 仍为待排查项，
本轮门禁通过不将其自动关闭。未做压力或高负载测试。

## 2026-10-02：备份图片的原生像素检查

xs 增加可选 `image` 原生扩展，只向 TCC VFS 开放 `<xs-image.h>` 和三个
`xsImage*` 函数。stb_image 和 libwebp 解码源、许可证、版本和精确 commit
均随源码锁定；只构建 PNG/JPEG 与标量 WebP 解码，不引入外部 DLL、图片
程序、前端包管理器或 Agent 工具目录。SDK 功能提交 `bc61a55`，直接 TCC
探针头文件修正提交 `a98e8af`；干净 checkout 的原生测试构建目录修正提交
`0c8c533`，后者进入 mdo 依赖锁。锁还校验适配器、
配置、全部 vendor 源及许可证字节，忽略 Windows/Linux 的换行差异。
二进制内的 `/licenses/native-image.txt` 保存相关通知。

`MdoSessionBackupCheckImages` 是独立离线检查：先重放未绑定模型，再逐张
解码附件与原始保留账本的 IMAGE parts，包含被压缩/裁剪隐藏的旧条目。
MIME 与内容必须相符；PNG 验证 chunk CRC，JPEG 验证 marker/EOI，WebP
验证 RIFF/块边界，然后实际解码所有静态像素。RGBA 和临时内存随即释放，
只返回附件/内嵌/未验证数量、RGBA 总字节及峰值内存。URL 或空引用不访问
网络，单独报告未验证；动画 PNG/WebP 暂不支持。可解码不证明视觉内容、
来源或所有有损熵损坏都无误，也不能代替模型/UI 关系或正式恢复资格。

单张上限为 8 MiB 编码、单边 16384、16M 像素、128 MiB 解码堆；聚合
上限为 1024 次图片操作、64 MiB 编码与 256 MiB RGBA，调用方仅可降低。
逐张复用内存，线程局部适配器统计所有动态分配，包括对齐头和 resize
期间的新/旧缓冲。WebP 使用非等待 gate 防止标量 DSP 初始化并发；重入
或并行 WebP 返回 busy。取消/30 秒 deadline 在容器、分配和操作边界检查，
不能抢占 codec 的纯 CPU 循环，生产入口须置于有界 worker。失败清理后
仍可重试；输出 Size 不匹配时不触碰调用对象，其他失败清空结果事实。

两平台原生探针均通过 173 项实际像素、边界、取消及逐处分配失败/重试
检查；小 PNG 与无损 WebP 另核对精确像素 hash。声明超出像素预算的测试
仅使用极小文件头，没有分配大图或进行压力/高负载测试。注册表 36 项通过
（Windows 一项 Linux 专属 skip）；直接/嵌套 TCC 和缺失扩展的头/符号门禁
已接入探针。mdo HTTP/TLS 覆盖实际源运行、附件四种编码、内嵌图片、URL、
截断、取消、deadline、低预算、错误后重试及 Home/备份原字节保持。

实际解码发现之前 68 字节 1×1 PNG 的 IDAT CRC 错误；该夹具曾用于原字节
保真，不能证明其可解码。本次只修正夹具 CRC，保留独立“字节保真/像素
解码”结果。初轮 SDK 探针缺少直接 include、错误码断言把 LIMIT 写成其他
编号，已分别修正；Windows 发布曾遇自身运行探针占用，待其退出后使用同
批已编译对象完成 GUI 链接和发布。首次 Linux 门禁失败与 Windows 发布
失败日志保留，最终门禁另行记录。

生产预览 worker、独立 staging、新身份/来源、原子非覆盖发布、catalog 与
正式完整备份菜单仍待完成。manifest 保持 `restore_ready:false`；没有本轮
原生点击或实体设备证据，不把此子阶段宣称为完整前端体验恢复。


最终 Windows/Linux 有界门禁均通过 115 Python、252 Node、90 模块、严格
C11、36 运行探针、三项 packed 与独立 A/B；Windows 另通过便携 WebView2
Home 覆盖/移动重启及 20 秒启动。Windows 宿主为 5,598,720 字节，较上一
阶段增加 246.5 KiB；完整程序为 6,379,380 字节（约 6.08 MiB）。
Windows 根目录更新 SHA-256：
`f94dade17344fd4ef159f2fc442ed055232dcc06f227a99e009aa5cf1762d034`；
Linux：`ce772b36a7ce9f50702cc2690a4e8326816a8a04f6b2dc7d5c93bc0f42eb04cc`。
最终记录为 `.build/qa-image-{windows,linux}-final.log`，native/TCC 分别保存
在 `.build/qa-image-{codec,tcc}-{windows,linux}-final.log`。

最终 Linux 门禁之前，普通图片 queued Unit 的第五个连接发送 HEAD 时出现
一次 reset；未放宽测试，也未据此修改底层库。相同源/宿主的两次独立普通
HTTP/TLS 验证及随后完整门禁均通过，但仍没有确定原因。该项保持待排查，
见 [连接重置记录](linux-image-queued-head-reset.md)，不能把重跑通过写成
根因已修复。本阶段没有压力/高负载或新增原生点击/实体设备测试。

## 2026-10-02：助手正文与逐块推理签名共同保存

修复 xllmMessageFromResponse 在追加签名 part 时清掉 content 的问题。
带签名的响应改为有序 TEXT/REASONING parts，每个原始推理后紧跟自己的
thinking_signature NATIVE part；空推理也保留签名。普通无签名响应保持
文字表示，工具在独立数组中保持顺序。xllm-session 共用这一转换，继续
反馈原用量，因此运行、会话重启和备份重放使用同一组完整消息。

Anthropic 按相邻原始推理回放签名，旧 native-only 消息仍使用原 joined
reasoning fallback；不会从旧数据推测缺失的分块。Completions/GLM/Responses
过滤 Anthropic 专有签名，保留原账本，让切换协议后的正文可编码。GLM
推理只进入 reasoning_content，不混入 content。同时修复 Completions
分段文字直接拼入引号而未转义，以及 Anthropic 普通助手正文漏闭合括号。
零可见 parts 的 Responses 消息使用空文字。未增加工具目录或前端选项。

源库 `5d16ece2`，xs `ef0e006` 已分别提交；xllm 18、xllm-session 16 个
生产文件逐字节同步，公开入口及持久化 schema 4 保持，deps.lock 同步。
库 Windows/Linux bounded gate 覆盖已解析 Anthropic SSE、原文/工具共存、
多段/空推理、分段 Unicode/引号/反斜杠/控制字符、四种 dialect、原始
缓冲修改、所有转换分配点失败、正常文字与自定义 joined text fallback，
以及真正的 file Load/Recover、journal-only 和 snapshot+tail 后再次编码。

mdo 隔离 HTTP/TLS 探针的三轮实际运行响应包含两段正文、一段有字推理、
一段空推理及两项 opaque 签名。在第三轮前释放并重新打开会话，后续
实际模型请求仍有原 parts；正常捕获后独立重放核对所有 assistant parts、
signature 类型、中文/PNG 与六项 UI 关系。固定响应只在隔离测试模型中
执行，不联网；后续备份检查保持 Home inventory 和原备份字节不变。
257 个格式错误输入继续覆盖。

本阶段只保证现有 response thinking/text 块及工具数组的转换。工具与
正文的任意交错、redacted thinking 和其他 provider-native block 尚未映射；
旧文件已丢失的内容不能补造。完整备份恢复仍需图片实际解码、生产 worker、
staging 原子非覆盖发布及正式菜单；原生/实体设备交互验收继续保留。
详见 [签名实施记录](session-backup-plan.md#助手响应签名与正文保真转换)。

最终 Windows/Linux 有界发布门禁均通过 114 Python、252 Node、90 模块、
严格 C11、36 运行探针、三项 packed 与独立 A/B；Windows 另通过便携
WebView2 Home 覆盖/搬移/重启及 20 秒启动。Windows xs/xsw 本轮从
SDK `ef0e006` 重建，完整门禁复用该宿主；Linux 使用新 ext4 副本
`/home/ubuntu/.cache/mdo-linux-signed-6mkvaaw8` 重建原生宿主并跳过 GUI。
根目录程序已更新为 Windows A/B 的同一字节：
`94b2468c5fe81fd387ee6a9aeca544a96718031860aea6a452de122f242fcbf1`；
Linux 为 `6d95c732bc8d52f0abb755e412026530bff83aff504bbd81b481abd2c85e550a`。
日志 `.build/qa-signed-{windows,linux}-final.log`。未做压力或高负载测试，
本阶段不增加原生点击或实体设备验收等级。

## 2026-10-02：完整消息的多模态持久化

xllm-session 的新 snapshot/journal 使用 v4，保存所有有序 parts 的类型、
文字、来源 URL、媒体类型、detail、native 类型及原始二进制字节，也保存
message native。普通文字仍使用原有 content；含 parts 的消息以 parts 为准，
拒绝同时出现竞争 content。二进制使用规范化的带 padding base64，恢复前
核对声明大小；深复制后不再依赖 JSON 或上传对象，不获取 URL 或外部文件。
读取继续兼容 v1–3；这些旧文件已经丢失的内容不能自动补回。旧程序无法
读取 v4，回退库版本前须保留原始文件。

源库提交 `799124f9`，xs 提交 `455b70f`。16 个生产文件逐字节同步，94 个
公开 TCC 入口保持；deps.lock 锁定源 revision、tree hash 和 schema 4，构建
时额外核对公开 schema 宏。Windows/Linux 的有界库回归覆盖六类 parts、
含 NUL/高位字节、URL、native、实际 file load/recover、snapshot+tail、
journal-only、有效 CRC 下的非法字段/版本/编码、取消、分配失败和源释放。

mdo 离线备份检查接入 v4。模型/UI 核对按有序 TEXT parts 拼接，排除推理
与 native；分段 Unicode、空段、跨段截断及明确冲突均有用例。HTTP/TLS
探针从正常产品入口运行三轮，首轮含中文与真实 1×1 PNG，在第三轮前释放
并重新打开会话，三次实际模型请求都保留原图与原文。正常捕获→独立重放
再核对完整图片 SHA-256，关系检查得到六项匹配且无缺口；缩短 UI 前缀后
明确报告两条缺口。原 Ling 配置未改，测试模型仅在隔离副本声明图片能力，
使用进程内固定响应。首次探针因图片能力前置条件失败，已修正测试模型和
能力名称，没有放宽正式模型检查。257 个格式错误输入及 Home 零写入合同
继续通过。

本阶段解决已有消息的 parts/native 存储。另发现助手响应到消息的转换
尚未完整传递 provider 推理签名；现有 xllmMessageFromResponse 添加 native
part 时可能丢失 content，不能直接替换 session 的转换调用。后续须在
xllm 源头修复并验证助手文字与签名共存。图片实际解码、生产预览 worker、
staging 原子非覆盖发布、正式完整备份菜单及原生/实体设备仍待完成，
manifest 保持 `restore_ready:false`。详见
[多模态持久化记录](session-backup-plan.md#完整消息的多模态持久化)。

最终 Windows/Linux 有界发布门禁均通过 114 Python、252 Node、90 模块、
严格 C11、36 运行探针、三项 packed 与独立 A/B；Windows 另通过便携
WebView2 Home 覆盖/搬移/重启和 20 秒启动。Windows 复用本轮从 SDK
`455b70f` 重建的 xs/xsw；Linux 在新 ext4 副本
`/home/ubuntu/.cache/mdo-linux-parts-opnuds93` 重建原生宿主并跳过 GUI。
根目录程序已更新为 Windows A/B 的同一字节：
`ea490f94e346925572b8d820d7d13d9fc8fa11868f3aae9c0da9f177fcf33e7c`；
Linux 为 `ae17c3a32d4f1c41a157bd9380a3e1bef565fd0416194112586d0b2965e3f51f`。
日志 `.build/qa-parts-{windows,linux}-final.log`。没有压力或高负载测试，
本阶段不增加原生点击或实体设备交互验收等级。

## 2026-10-02：会话备份的模型与 UI 关系检查

新增独立的 `MdoSessionBackupCheckModelHistory()`，在拥有离线备份和独立
模型重放之后，核对根 Agent 的用户 sequence、assistant turn/文字及工具
turn/call ID/name/原参数。明确冲突失败；旧序号缺失、模糊 assistant、截断
内容与工具完成早于结果持久化的窗口报告无法完整核对。UI 保留范围缩短
时报告模型投影缺口，不重建被淘汰的数据。结果不修改备份、不写 Home，
不会执行模型/工具/队列，manifest 保持 `restore_ready:false`。

xllm-session 新增稳定状态下原始 retained ledger 的只读视图；包含被
watermark 隐藏的 entry，避免把渲染后的 request 当作原始身份。单写线程、
借用所有权、size 校验和 hook/瞬态拒绝均有有界库回归。源头 `e2b990f2`、
xs `34fba96` 已提交，15 个生产文件逐字节同步、TCC 公共入口及 deps.lock
已同步。Windows xs/xsw 与 Linux 宿主均从此 SDK 构建。

HTTP/TLS 探针新增实际产品两轮运行→正式捕获→离线关系检查，四条消息
匹配、没有缺口；缩短 UI 前缀后报告两条模型缺口。源运行使用进程内固定
响应，仅发生在测试自己的隔离 Home；零写入基线之后只进行离线检查。
237 个既有格式错误输入继续覆盖，新用例检查明确冲突、旧记录、截断、
子 Agent、工具/恢复事件、取消清理、失败重试、NULL error 和旧多模态缺口。
首次有界库 fixture 在 TURN_COMPLETE 状态下调用 BeginModelCall，已改为
先打开新回合；首次真实保留范围 fixture 假定首 turn 为 1，已改为取实际
事件边界。两处只修正测试前置条件。新增模块的两个静态名字与 unity 中的
旧模块重名，也已修正；失败日志保留，没有放宽编译/库合同。

检查发现现有 xllm-session snapshot/journal writer 没有保存多模态 parts，
带图请求的文字也可能丢失。这类旧备份只报告无法完整核对，不能报成完整
可恢复。多模态持久化、离线图片解码、生产预览 worker、独立 staging 与
非覆盖原子发布、正式菜单和原生/实体设备仍待完成。详见
[实施记录](session-backup-plan.md#保留模型账本与-ui-关系检查)。

最终 Windows/Linux 有界门禁通过 114 Python、252 Node、90 模块、严格
C11、36 运行探针、三项 packed 和独立 A/B。Windows 另通过便携 WebView2
Home 的覆盖/搬移/重启与 20 秒启动；Linux 使用新的 ext4 源副本
`/home/ubuntu/.cache/mdo-linux-model-history-6p8oazb_` 重建宿主，跳过 GUI。
根目录程序已更新为 Windows A/B 的同一字节：
`2d76be9b8f9aa88a20de852755c3799fecf7fc49a45847c7af70d4e67be43542`；
Linux 为 `4e74f65bfcb567070900c45867e976a9dca677c76b4b599adc34eafccaeadedb`。
日志 `.build/qa-model-history-{windows,linux}-final.log`；库 bounded 回归为
`.build/qa-model-history-library-windows.log` 和 `.build/qa-model-history-linux.log`。
没有压力或高负载测试，本阶段不增加原生点击或实体设备交互验收等级。
最终短结构体 canary 改用分配器对齐的存储，避免假定 uint32 栈数组满足
报告结构体的对齐。生产字节未变，两平台 HTTP/TLS 定向补验均通过，日志
`.build/qa-model-history-probe-aligned-{windows,linux}.log`。

## 2026-10-02：图片预览加载反馈与重试

打包页复现了图片 HTTP 503 后预览仅显示损坏图片和文件名的情况。正式
预览现在区分加载、成功和失败：加载期间显示状态，失败时保留原始文件名并
提供“重新载入”。重试使用原 URL，不追加查询参数或额外复制图片字节。
重试期间按钮保持可见并用 aria-disabled 阻止重复请求；成功后先将焦点移到
关闭按钮再隐藏重试按钮。关闭、Esc、导航和原缩略图重绘后的焦点返回合同
保持，异步失败不抢焦点。中文、英语、俄语均新增提示词。

每个预览/重试请求拥有新的图片 DOM 节点，回调同时校验预览身份、节点身份、
dialog 状态和加载阶段。关闭释放 src；旧 load/error 和旧名称不能完成新
预览。Node 事件宿主验证连续失败、同 URL 重试、重复点击、迟到成功/失败、
无解码像素、语言切换和导航焦点；真正的浏览器解码另由组件夹具验证，不能
把模拟事件当作实际解码证据。

Windows 候选单文件 `.build/mdo-image-preview-load.exe` 的隔离 Home 为
`.build/mdo-packed-docks-omih7gtv`。组件导入该包内的正式 JS/CSS；HTTP 503、
再次失败、恢复资源后重试成功、Esc 和旧节点事件隔离通过，成功图片实际
naturalWidth > 0。320×350 中文、280×250 英文提示及重试按钮在视口内，
布局稳定后没有横向溢出。生命周期夹具验证重绘后的名称、快速重开、外部
关闭和焦点；320×350 的模拟 visualViewport 夹具验证 250px 高度、平移和
恢复。这是浏览器模拟，未作为实体手机软键盘证据。

同一 Home 的实际工作台粘贴两张图片、通过正式附件 API 上传后快速切换
预览，第二张成功解码，204 字符原名保留，关闭按钮持有焦点，错误与重试
控件正确隐藏。原生操作工具两次初始化都失败于内核资源路径不存在，本轮
没有原生窗口点击证据；自动原生启动门禁也不代替这一项。

最终 Windows/Linux 有界发布门禁均通过：114 Python、252 Node、90 模块、
严格 C11、36 运行探针、三项 packed 和独立 A/B。Windows 另通过便携
WebView2 Home、覆盖/搬移/重启和 20 秒启动检查。SDK 仍为 `0649523`，复用
上阶段从该源码重建的宿主；Linux 使用新的 ext4 源副本
`/home/ubuntu/.cache/mdo-linux-image-preview-load-4gdzoy2y`，跳过 GUI。
候选包与最终 A/B 包逐字节 hash 一致，根目录程序更新为
`f19dcd78daeee27da2d9360b5b40618551a182aaf6a51c0274d76e74a60fa8a6`；
Linux 包为 `732234960dc58abc47b41541583c7adcea24583c63077204168596557062c65a`。
日志分别为 `.build/qa-image-preview-load-{windows,linux}-final.log`。
没有压力或高负载测试，完整备份恢复等既有缺口保持未完成。

## 2026-10-02：离线模型上下文重放

xllm-session 新增 `xllmSessionRestore()`，以借用的 snapshot/journal 字节为
输入，复用文件恢复的 loader 和 operation replay，返回独立拥有的内存上下文。
不继承 snapshot/journal 路径、client、driver、hooks 或 cancel，不访问原文件，
不启动模型、工具或队列。空 snapshot 可按调用方配置创建，配置中的持久化路径
也会移除；残尾明确拒绝。旧文件 `Load/Recover` 的崩溃修补行为保持原合同。
同一源码提供 newest pinned system prompt getter，修正此前 SDK 额外实现未进入
上游版本标记的问题。源头提交 `cb6c05f6`，xs 同步提交 `0649523`；vendored
15 个文件逐字节验证，TCC 公共入口覆盖门禁通过。

恢复选项有独立 Size/ABI，默认 snapshot/journal 32/64 MiB、每条 JSON 262144
值、65536 条记录、深度 64（最大 256）。covered 记录也消耗预算；解析值/深度
预算、OOM、取消和 deadline 分别报错。CRC 分块和拷贝/记录间检查取消；失败
销毁整个未发布对象。库的 Windows/Linux 有界回归检查实际工具回合、ledger、
v1/v2 fixture、rewind/clear、源字节保持、输入销毁后的深所有权、逐点分配失败，
以及所有存储故障钩子在 byte restore 中均未被调用。

mdo 的 `MdoSessionBackupReplayModel()` 只接收已成功 owning decode 的对象，
启动新的最多 30 秒预算。模型语义检查独立于格式检查：历史中的空 provider ID
或错误工具参数仍可检查/保存；重放失败单独返回，不能删掉或修补原始证据。
HTTP/TLS 探针释放上传 pin 后重放，释放整个 backup 后仍可渲染；工具配对、
checkpoint 去重、旧格式/CRLF、压缩质量门、截断与真实库写出的 rewind/clear
均验证。错误 turn、重复 call、孤立 result、预算、取消和 deadline 都明确失败，
相同 backup 的各文件 SHA-256 保持，Home inventory 逐字节不变。

第一次集成门禁发现测试夹具的类型名错误及新模块未加入 unity 构建清单；
已修正类型/工具参数字段和清单，并保留初次失败日志。语义错误映射到当前
`XWORK_ERROR_IO`（6），修正测试误用的错误码。定向探针通过后才
重新执行完整门禁。此阶段仍没有生产预览路由、完整恢复事务或菜单入口；
模型/UI 对应、图片实际解码、staging/新身份/来源及非覆盖发布继续按
[备份实施记录](session-backup-plan.md#独立模型上下文重放) 完成，manifest 保持
`restore_ready:false`。原生与实体设备体验验收仍独立登记。

两平台完整有界门禁通过 114 Python、251 Node、90 模块、严格 C11、36 运行
探针、三项 packed 与独立 A/B；新 replay 模块也分别通过独立严格 C11。
Windows 另通过便携 WebView2 Home 移动/覆盖优先级及 20 秒启动。xs/xsw 已
从新 SDK 构建；最终门禁复用同一新宿主，不为仅修正 app 清单再次编译宿主。
Linux 最终使用新 ext4 源拷贝，复用本轮从 SDK@0649523 新构建的原生宿主
（SHA-256 `d304dfd00f84ba8d6410db1049a525755d27c6e888901dceb7596105ab79c5d7`），
跳过 GUI。测试 Node 仍只在隔离目录，官方归档 SHA 已复核。
日志 `.build/qa-session-memory-{final,linux-final}.log`；库日志
`.build/qa-session-memory-{windows,linux}.log`。根目录 `mdo.exe` 已从验证候选
覆盖，与 Windows A/B SHA-256 同为
`a571f1ef2e080b050c589375cbc69368ae4e8b6ffaadb7dd8de30b0524164016`；
Linux A/B 为 `14ed2a06d4494bf4727dbce1ca09eaf09bfd20a9cba3a6d2341e15f929d64e12`。
没有压力或高负载测试，完整产品恢复与实体/原生交互仍未验收。

## 2026-10-02：模型 journal 格式校验

快照模块整理为 `backup_model.c`，两种模型文件共用字段、整数/文本和 CRC
校验。先检查 snapshot 并取得 checkpoint，再逐行检查 journal，一次只持有
一行 JSON tree，不重复解析两份文件。v1/2 与 v3 使用各自的序号/操作字段，
七种记录检查各自已知字段和必需值；保留库实际支持的旧可省略字段与 rewind
零边界。换行前精确字节参与 CRC，支持 CRLF；空/残缺行拒绝，不修补文件。
covered 前缀仍检查 schema/CRC，记录严格递增，checkpoint 后必须连续编号。

HTTP/TLS 拥有解码探针通过 237 种小错误输入，新增旧记录、合法 CRLF/covered
前缀及首条读取后的取消。锁定库实际写出 begin_turn/add_message/ledger/
rewind/clear，并将精确字节纳入检查；只写隔离 Home 的自有夹具文件，零写入
对比前已移除。其他两种操作为 schema 样例，不冒充压缩/截断真实重放。
新模块也通过独立严格 C11 编译。详见
[journal 边界](session-backup-plan.md#模型-journal-格式与-checkpoint-连号检查)。

Windows/Linux 完整有界门禁通过：114 Python、251 Node、90 模块、严格 C11、
36 运行探针、独立 A/B 和三项 packed。Windows 另通过便携 WebView2 Home
与 20 秒打包启动；Linux 使用新 ext4 源拷贝、同一锁定 SDK 新构建宿主，
跳过 GUI。测试 Node 从之前隔离目录的官方归档取得，复核原 SHA-256 后仅
解压到本轮目录，不作为生产构建依赖。日志
`.build/qa-backup-journal-{final,linux-final}.log`。根目录程序与 Windows A/B
SHA-256 为
`dd9b148ce35f07c1bd3d8b180a79b71d9b37ac66a17703bd72d8850981e5519f`，
Linux A/B 为
`d70032d9bc70cf02a50a13a2f7e7814c15b7cb8639ff310d198282e362d56fbb`。
没有压力或高负载测试。

字段/CRC/记录连号不能证明上下文可实际恢复。模型 turn 与消息关系、工具
配对、摘要质量和 rewind 后上下文仍需库实际重放，图片实际解码、生产预览
worker、原子恢复及正式菜单也未完成。manifest 保持 `restore_ready:false`，
完整恢复和原生/实体设备证据不升级为已完成。

## 2026-10-02：备份模型快照格式与原字节校验

`backup_snapshot.c` 在捕获后编码与离线拥有解码的共同入口检查模型快照。
支持 xllm-session v1–3，核对格式、字段类型、消息序号/turn/角色/flags、
文件账本的数组与序号范围，以及配置的位宽和库实际支持的范围。v3 对原始
字节核对 CRC-32/ISO-HDLC；每 64 KiB 及消息/工具调用/文件引用之间检查
取消和截止时间。配置只创建立即释放的无 client/path/hook 内存 session，
原始工具参数及来源路径不执行、不打开，没有写入 Home 或调用模型。

拥有解码的 HTTP/TLS 探针保留合法旧格式、Unicode/推理和错误工具参数，
177 种错误输入包含非法快照配置/序号/字段及重算外层 SHA-256 后的内部 CRC
损坏；分块 CRC 中取消、失败后重试、Home 原字节保持通过。首次 CRC 用例
修改了不存在的 turn 字面量，现改为必定修改现存 checksum；手写 session
运行夹具也补入新模块。产品校验没有放宽。详见
[快照边界](session-backup-plan.md#模型快照格式与原字节-crc-校验)。

Windows/Linux 完整有界门禁通过：114 Python、251 Node、90 前端模块、
严格 C11、36 运行探针、独立 A/B 和三项 packed。Windows 另通过便携
WebView2 Home 与 20 秒打包启动。当前 Ubuntu WSL 可用，Linux 使用新 ext4
源拷贝与同一锁定 SDK 新构建宿主，跳过 GUI；测试专用 Node 放入该隔离目录，
不成为生产构建依赖。日志 `.build/qa-backup-snapshot-{final,linux-final}.log`。
根目录程序与 Windows A/B SHA-256 为
`cdfd1363200177be4b58bebcd0639b4a4a1d7fefc94231d1faeedbaa4975f55c`，
Linux A/B 为
`d769d8a556a94b0037550297b47f7ef67e40c6698fdecf7f533f44dfc23da785`。
此前待办/询问及图片改动也随本轮完整 Linux 门禁复验；没有压力/高负载测试。

这完成快照格式边界，journal 校验、实际账本/UI 重放、图片解码、生产预览
worker、原子恢复和正式菜单仍待完成。`restore_ready:false` 保持，完整恢复
与原生/实体设备操作证据不升级为已完成。

## 2026-10-02：待办临时读取失败的有界恢复

待办原先只对侧车落后于工具事件的情况重试；一次网络/503 失败后，已观察
相同工具事件不再触发读取，计划会停在错误状态。复用已有退避，网络错误、
408/429/5xx 与投影延迟共用最多四次重试（120/240/480/960ms）。永久拒绝和
无效计划数据保留错误，不自动重试；预算用尽后由重新选择会话或新工具事件
触发新的读取。成功清除错误并重置预算，没有加入持续轮询。

新 todo 工具事件使旧读取失效；切换会话/历史清空取消计时器。即使旧回调已
排入事件循环，也先检查 generation，不会改写新选择的 timer 或预算。空 data
按无效计划响应报告。六项回归与七项既有历史回放检查通过，覆盖混合延迟/
读取失败的共同额度、持续失败上限、不可重试错误、旧失败与已排队取消回调。

`tests/fixtures/todo-refresh-browser.html` 经隔离打包实例读取生产模块，todo.js
与源文件逐字节一致，API 响应由夹具控制。浏览器实测保留错误期间的原计划，
一次重试后显示 1/2 进度，第一行节点、展开状态和标题焦点保持；迟到失败不
覆盖新计划，历史清空取消待重试读取且焦点回主输入框。整个流程七次读取，
结果及截图 `.build/qa-todo-refresh-browser.{json,jpg}`。临时页面、实例和 Home
已清理，没有使用用户 Home/线上模型；仍不是原生窗口或物理输入法验收。

增量门禁通过 114 项 Python 契约、251 项 Node 测试、90 个前端模块、三项
打包探针、便携 WebView2 Home 和打包启动 20 秒检查，C/API 的完整 Windows
运行门禁沿用 `e53625d`。独立 A/B 包与根目录程序 SHA-256 为
`2720de5b89074a7dc66da251fcd9745643d1a9ff7032ab24bf21891415ced3a4`，
日志 `.build/qa-todo-refresh-{before,after,build,verified}.log`。当前环境没有
WSL 发行版，本次 Linux 未执行；没有压力或高负载测试。

## 2026-10-02：询问刷新恢复与乱序隔离

同一会话的询问读取原先只以切换会话为 generation 边界：较早的成功响应会
重新显示新读取中已经消失的问题，较早的失败也会覆盖新读取的健康状态。
另外，失败后收到相同 items 时被内容去重跳过，错误状态无法清除。五项
定向回归在修改前复现三项失败，修改后全部通过；切换/清空的迟到读取和
健康相同内容不发布也受覆盖。每次有效读取现在取得新 generation；成功的
相同内容仅在需要恢复状态时发布，避免正常轮询反复扰动回答编辑器。

`tests/fixtures/ask-refresh-browser.html` 经隔离打包实例的 loopback 桥接读取
生产模块（asks.js 与源文件逐字节一致），夹具只控制两次读取的响应顺序。
浏览器输入“中文回答草稿”并移动光标后，失败/恢复及迟到失败保留同一编辑器、
草稿、光标 5/5 和焦点。挂起旧快照，用生产询问框按 Enter 提交一次，再释放
旧快照，卡片不重新出现且焦点回主输入框。结果及截图登记在
`.build/qa-asks-refresh-browser.{json,jpg}`。这是生产组件和状态模块的浏览器
验证，API 使用夹具响应；不替代真实模型续行、原生 WebView2 或物理输入法。
临时页面、实例和 Home 已关闭清理，未使用用户 Home 或调用线上模型。

增量门禁通过 114 项 Python 契约、245 项 Node 测试、90 个前端模块、三项
打包探针、便携 WebView2 Home 和打包启动 20 秒检查；C/API 未修改，运行探针
沿用 `e53625d` 的完整 Windows 门禁。本次锁定 SDK 验证通过，独立 A/B 包与
根目录程序 SHA-256 为
`7b81aa6978a5e096aaabb1363afdf3e992971fe24220a096ebe656c668d850c8`。
日志 `.build/qa-asks-refresh-{before,after,build,verified}.log`。当前环境没有
WSL 发行版，本次 Linux 未执行；没有压力或高负载测试。

## 2026-10-02：窄屏图片名称与提示的间距

上一阶段浏览器实测发现手机布局的 `.composer-image` 固定 64px 高度，
但真实内容包含 68px 图片和 20px 名称；兼容提示被排到同一行高度范围。
去掉 tile 固定高度，并把该断点的图片设为与 tile 一致的 64×64，让名称
参与正常排版，40px 移除按钮和预览/关闭交互保持原来的操作方式。

更新的隔离打包实例在 390×844 和 320×844 下实测：四张有效 PNG 正常显示，
320px 下第四张正常换行；最后一个预览按钮 bottom=704.20，提示 top=716.20，
保持 12px 间距，页面 scrollWidth 等于 viewport 宽度，没有横向溢出。
第四张预览和 Escape 关闭恢复触发按钮焦点通过。浏览器视口已 reset，临时
页面与便携 Home 服务已关闭清理。截图 `.build/qa-images-mobile-{fixed,320}.jpg`，
测量 `.build/qa-images-mobile-browser.json`。只证明浏览器响应式显示与此条
交互；实体手机键盘/触控、原生 WebView2 交互及 JPEG/WebP 解码仍需另行验收。

本次仅样式修改，C/API 延续上一提交 `e53625d` 已通过的 Windows 完整门禁。
增量检查通过 114 项 Python 契约、240 项 Node 测试、90 个前端模块及扩展
打包图片/备份/移动路径探针；独立 A/B 包与根目录程序 SHA-256 为
`8d76168f75a72bd8e519a39bdcaad5e08bda70a4762e4d9bdd4416c00d2f3ef2`。
日志 `.build/qa-images-mobile-{build,verified}.log`。当前环境无 WSL 发行版，
本次 Linux 验证未执行，不做压力或高负载测试。

## 2026-10-02：普通图片附件的有界发送

修复上一阶段额外发现的图片读回超时：原路径写出 200 和完整 Content-Length，
随后一次提交整张图片。有效 2,361,103 字节 PNG 的 HTTP 正文未发出，TLS
只收到部分正文。图片 GET/HEAD 现在复制已验证的 ID、方法及请求 ID，并持有
连接引用，通过独立执行池沿用备份的 16 KiB 分块、TCP drain/TLS async 发送。
每个请求从受理起有 30 秒截止时间；成功关闭连接，未完成则 abort。

图片执行池按需创建，4 个读取槽位和 16 个排队槽位，不占备份唯一捕获槽位。
排队时只有固定大小请求及连接引用；执行时每张图片最多 8 MiB，四张图片正文
合计最多 32 MiB。工作线程重新取得 project lease，在附件锁下读入拥有的数据，
发送前释放锁和 lease。没有借用路由参数、HTTP head/body、文件或会话对象。
API Unit 先取消并 join 全部已接受任务，再释放附件/Home 管理器和 TCC 代码。

HTTP/TLS 定向检查通过：有效 PNG 字节/hash、HEAD、四张编辑器缩略图、独立
备份额度、真实 transport wait 取消、断连、deadline、网络线程上的 Unit、
active/queued 释放、删除后已有响应保持原字节及重试。Home 读检查逐字节不变。
第一轮取消探针没有阻塞到真实发送：在 TCP 握手后才缩小接收缓冲，窗口协商
仍可容纳整个图片。调整隔离客户端为握手前设置后通过；没有放宽产品规则。
打包探针追加上传、GET/HEAD、备份带图与移动 exe 路径重启读回，仅用内置 VFS。
这验证传输，不表示图片实际解码和原生 GUI/移动端看图已经验收。

Windows 完整门禁通过：114 项 Python 契约、240 项 Node 测试、90 个前端
模块、严格 C11、36 个运行探针、3 个打包探针，以及便携 WebView2 Home/
打包启动 20 秒检查。独立 A/B 包及根目录程序 SHA-256 为
`4b6a7c707c9eb3919b692ace373c0dc3db5f10dfe3747dc8b58977d8e69c1557`，
日志 `.build/qa-images-verified.log`。本轮 Linux 未执行：当前环境的 WSL
没有可用发行版（`WSL_E_DISTRO_NOT_FOUND`），记录于
`.build/qa-images-linux-launch.log`；上一阶段结果不替代这次产品的 Linux 验证。
不做压力或高负载测试。

隔离打包实例经浏览器实测，四张图均 complete 且 naturalWidth/Height 为
768/1024；桌面及 390×844 窄屏可打开预览、关闭，Escape 恢复原触发按钮
焦点，刷新后四图与草稿文字恢复。未调用模型、未使用用户 Home。
截图 `.build/qa-images-browser-{composer,preview,mobile}.jpg`。窄屏没有横向
溢出，但发现容器固定高度导致名称和兼容提示重叠（按钮 bottom=728.20，
提示 top=716.20）；作为紧接本阶段的样式修复记录，不把这张截图当成布局
全部通过。这里仍不是原生 WebView2/实体手机触控或上传操作的全面验收。

## 2026-10-02：保留历史的侧车关系校验

离线解码新增只保留标量事实与原字节偏移的历史索引，复用实时事件解析器。
核对保留事件中的反馈是否指向成功的模型完成，todo 是否对应成功的主 Agent
待办工具和相同 items，消息附件是否对应相同运行的主 Agent start；队列回执
核对 durable start 的队列 ID/Agent run，以及 sending 项的已接受运行身份。
明确拒绝缺失/矛盾的保留证据、重复派发身份和不可能继续增长的事件 ID。

前缀淘汰不等于删除。旧引用缺少 surviving UI 证据时报告
`UnverifiedHistoryReferences`；被明确历史删除范围覆盖的侧车报告
`RemovedHistoryReferences`，保留输入原字节供后续 staging 协调，不在检查时
修复文件。旧数字 run 绑定可能跨重启复用，不能与新 event 绑定强行比较；
prepared 回执没有匹配 start 时也不被提升。两项计数按侧车引用计，不是唯一 ID
数量。零来源旧 marker 对 todo 的特殊 reset 语义保持。

预览结构新增计数时同时修正 Size 不匹配仍按新结构长度清零的问题：先检查
尺寸，旧/小调用者的对象保持不动。一个真实小分配的 guard 用例覆盖此边界。
历史索引和删除区间分配各受 UI 字节预算约束，每个文件/记录/引用之间检查
取消与 deadline；原字节、上传、token/DOM 和索引都是独立内存开销。

HTTP/TLS 探针追加 134 个小错误输入的全集和裁剪/重用/中断回执的正反样例；
索引开始后取消也通过，Home 保持逐字节不变。首次定向运行遇到测试变量覆盖
（文档与历史回执使用同名变量），已改名后完整重跑，没有放宽产品规则。
模型账本/真实图片解码及实际 replay、预览 worker、原子恢复与正式页面仍缺，
不把本阶段当作完整恢复完成。

Windows 与 Linux ext4 均通过 114 项 Python 契约、240 项 Node 测试、90 个
前端模块语法检查、严格 C11 编译、35 个运行探针及 3 个打包探针。Windows
另通过便携 WebView2 Home 和打包启动 20 秒检查。两平台独立 A/B 包一致；
Windows 与根目录程序 SHA-256 为
`770a5ba8d41ff4485b9c69c149c0bb2704e8348f93327894f9218a72d19f4065`，
Linux 为 `5bc37b6bc5762d58f6be4202f39d90ebbb7cb007bc9bc91c05ccd6bc7917873b`。
日志 `.build/qa-relations-{verified,linux-verified}.log`；不做压力或高负载测试。

额外普通附件检查发现：一张有效的 2,361,103 字节 PNG 上传成功后，HTTP 与
TLS 读回都超时。既有小图片探针未覆盖这个边界；日志
`.build/qa-normal-image-reply-baseline.log`。下一阶段优先修复图片传输，不据
上述门禁宣布附件体验完成。

## 2026-10-02：共享侧车解析边界

草稿、队列、回执、反馈和消息附件绑定的解析移到
`app/src/sessions/sidecars/`，不依赖 HTTP、Home 或运行管理器。HTTP 层保留
原有文件读取、锁和回执修复；离线解码只解析拥有的字节，不查询运行、提升
prepared 回执或恢复执行队列。完整/部分 composer profile 共用一个解析器，
未知模型和思考强度保留为数据，权限值仍严格限定。失败释放草稿提交和队列
文本的部分分配；新模块可独立按严格 C11 编译，不依赖 unity 的隐式声明。

离线验证增加上述 schema、回执文件名身份、绑定 run 文件名身份和 uint64
文件名范围。保留历史 session draft/queue 1–7 和 receipt 1–3；项目专用
draft 8 不作为 session 数据。修正反馈 512 项和队列满项/图片/清理清单组合
的 JSON 节点预算，没有增加文件/队列/正文限额。

扩展 HTTP/TLS 探针覆盖 115 个错误输入、部分拥有对象失败后重试、历史格式、
未知 profile 和声明上限组合；所有校验前后 Home 逐字节一致。图片边界样例
只验证 pair/schema，不代表图片可实际解码。保留历史的跨文件 ID 关系、模型
账本、真实图片解码和回放仍需完成，生产预览 worker、原子恢复及正式页面
菜单也未接入。本子阶段继续属于步骤 4，不把完整恢复写成完成。

Windows/Linux 完整有界门禁通过 114 Python、240 Node、90 模块、严格 C11、
35 运行探针、确定性 A/B 和三项 packed；Windows 另通过便携 WebView2 Home
和打包崩溃/20 秒启动。Linux 使用新 ext4 源拷贝和相同锁定 SDK/宿主，跳过
GUI。根目录程序已更新为 Windows 验证包，SHA-256 为
`d2d9f2bf24b1c176832f9283869ceaacd3d5be370b665ef1965a1c0f3f4633a2`；Linux
包为 `33530a65c903438bd4c3a013bb761fc788f73e6adf1cfcb8b936fa713cf850d0`。
日志 `.build/qa-sidecars-{verified,linux-verified}.log`，未运行压力/高负载测试。

## 2026-10-02：备份离线解码与部分 schema 校验

新增只在内存中工作的拥有解码与预览事实 API。外层 SAX 不构造完整 base64
DOM，逐项核对清单、路径、base64/长度/hash、重复字段/文件、metadata 身份/
workspace 和实际保留范围；解码结果独立于上传生命周期。预算及合作取消/
截止时间覆盖解析事件和文件，失败释放部分对象。UI/todo 复用现有纯读取器
进行 schema 检查，不投影或写数据。v1 精确保留原 meta/snapshot 字节，明确
仅模型快照，不能升级为完整 v2。

HTTP/TLS 运行探针由真实会话草稿/待确认队列、真实 UI/todo 投影及普通
2 MiB artifact 开始，独立核对 decoded 原字节；释放 upload 后副本保持。
65 个小型错误输入（含跨平台大小写路径别名）、降低预算、取消/截止时间、
失败重试及 Home 零写入通过。
没有生产 HTTP 预览 worker/菜单，draft/queue/receipt/消息绑定/模型账本完整
schema 和关系、图片实际解码及真正回放仍缺；这只是步骤 4 的子阶段，
完整 JSON 恢复尚未完成，原生/实体设备证据不由后端探针替代。详见
[实施记录](session-backup-plan.md#离线拥有解码与部分-schema-校验)。

Windows/Linux 完整有界门禁通过 114 Python、240 Node、90 模块、严格 C11、
35 运行探针、A/B 与三项 packed；Windows 另经便携窗口及打包崩溃/20 秒启动。
Linux 为新 ext4 源拷贝、相同锁定宿主，跳过 GUI。根目录程序已更新到
`b2a3cbc20bf1b0112c13aedffa503b2e8024c3ba937c51f78e3fa773f02d4ab7`；Linux
包为 `b1780ce13c14da5013579941872743ecd1e09233f34f7a842105913d2cab2e78`。
日志 `.build/qa-backup-decode-{verified,linux-verified}.log`；初轮日志保留。
门禁同时修正半行日志终止、短 deadline 检查点和隔离探针缺头文件问题，
保留原业务断言，前两项已提交 `6a51def`。便携缓存位置与取舍继续保持。

下一步继续抽取 draft/queue/receipt/消息绑定及模型账本的纯 schema 读取边界，
补全跨引用和图片实际解码，再接入预览 worker、独立恢复事务与正式页面。
完整恢复及原生/实体设备交互仍未由本阶段证明，没有压力/高负载测试。

## 2026-10-02：完整会话备份的分段上传

新增独立上传 store 与查询/创建/分段/seal/取消 API。一个槽最多 96 MiB，
每段最多 256 KiB，沿用 write token 与 Home 写入 admission；固定五分钟
过期，不续期。相同创建意图、相同已接收字节的重试不重复累加，状态/偏移/
内容冲突明确拒绝；当前槽查询可以找回旧页面的 ID。未经格式验证的原始
文档只留内存，不写会话、凭据或 staging，不发起任何运行。后续 validator
可 pin sealed 不可变字节，取消/过期后仍保留读者的引用和配额，旧 store
Release 不会误释放新 generation；入场/worker/TCC teardown 合同已注明。

真实 HTTP/TLS 探针从 2 MiB artifact 的 v2 文档开始，分段收齐并从 pin
独立读回 hash。覆盖无 token、类型/预算/偏移、相同重试/异字节冲突、
未收齐单段断连、seal/校验失败、取消/过期、跨 generation 释放和 Home
零侧车写入。packed 内置 VFS 探针也加入上传/seal/删除。八类新错误已
映射中英俄；页面仍是 v1 菜单，上传 checksum 不等于 schema/replay 校验。

这条普通文件传输同时定位并修正 xs 的 TLS 请求停滞：HTTP 保留完整请求
之前必须请求继续解密，TLS PlainLimit 也必须覆盖有效接收窗口。xs 已提交
`c840d8c`、`483d753`，依赖锁升至后者。新增独立 HTTP/TLS 默认/大/小窗口、
固定/分块、hash、keep-alive 与超限拒绝探针；不改 xrt 核心，不增加全局
普通 API 配额，不运行压力/高负载测试。

下一阶段按 [实施记录](session-backup-plan.md#专用分段上传与不可变读取)
实现离线格式/引用校验与预览，再做实际账本/UI replay 和原子新会话恢复。
没有用传输成功代替正式页面导出/恢复、原生下载或实体移动端验收。

Windows/Linux 重新构建锁定 xs 宿主，独立 xs HTTP/TLS 小探针均通过。
mdo 完整有界门禁在两平台通过 114 Python、240 Node、90 模块解析、严格
C11、34 运行探针、A/B 确定性与三项 packed 探针；Windows 另通过便携
WebView2 Home 和打包崩溃/20 秒启动，Linux 使用新 ext4 独立源拷贝、跳过
GUI。日志为 `.build/qa-backup-upload-{xs-build,xs-receive,runtime,release,linux-host,linux-xs-receive,linux-release}.log`。
根目录程序已用最终 Windows A/B 更新，SHA-256 为
`3d2fcd26719706568c216cfaa8250abfabcfd56c3270c6f5ae82d619fa25bc29`；Linux
包为 `13e8487ac69d3bcb3c22c60bf1950885982d2b025980e5c8d7cfd4daee730a54`。
缓存继续位于 `mdo-home/data/cache/webview2`；没有压力或高负载测试。

## 2026-10-01：完整会话备份的专用下载

新增 v2 `/backup` 的 GET/HEAD/OPTIONS，使用惰性单任务 executor 和
XS_TAKEOVER 连接生命周期。只在捕获阶段持有 session/项目租约及 API
存储 guard，之后独立编码并按至多 16 KiB 分块发送；TCP 处理队列满与
DRAIN，TLS 使用异步 Send/DRAIN。整个任务共用 30 秒截止时间，连接
关闭或取消释放任务；Unit 先取消并 join，再释放 manager/TCC 代码。
普通 API、图片及旧 v1 导出限额不变，缓存继续位于便携 Home。

真实 HTTP/TLS 小探针发送正常 2 MiB 文件，返回文档超过默认发送队列；
独立读回 hash/字节、HEAD、捕获后 metadata 修改、错误/重试、断连、
超时、占用拒绝和 shutdown 均通过。缩小单个 socket 缓冲并确认实际
pending 后，从唯一网络 worker 取消真实背压等待，执行槽可再次使用。
新 packed 探针完全从内置 VFS/TCC 加载代码，核对草稿/待确认队列/
artifact；换程序目录重启同一 Home 后，原内容逐字节一致且没有派发。
项目独占期间 GET/HEAD 下载也拒绝，OPTIONS 保持可用。没有模型、
shell/队列执行或压力/高负载测试。

四类备份错误已有三语提示。页面仍使用 v1 菜单，v2 保留
`restore_ready:false`；上传、离线完整 schema/引用验证、实际账本/UI
恢复、原子新会话发布及正式页面导出/恢复继续按
[实施记录](session-backup-plan.md#专用-httptls-下载与生命周期) 推进。
后端 HTTP 接收与内置 VFS 核对不能代替浏览器下载或实体设备验收。

Windows/Linux 完整有界门禁通过 114 Python、240 Node、90 模块解析、严格
C11、33 运行探针及确定性 A/B；Windows 另经便携 WebView2 Home 与打包
崩溃/20 秒启动。最后整理映射缩进并加入项目租约/内置 VFS 验证后，
Windows 重新通过完整前端检查、API、A/B、三项 packed 探针和便携窗口；
Linux 完整门禁直接包含最终代码及新探针。日志为
`.build/qa-backup-download-{release,final,linux-release}.log`。
根目录 `mdo.exe` 已更新到最终 A/B 包，SHA-256 为
`6ea1f9b686bc5971cb84b4672136aee8c79d0b5cac1d31db21e98f739282093e`；Linux
包为 `a3dad18d2340d4410dc2366a6896a8646750760b7b6406d0303fa463897b68f6`。

## 2026-10-01：完整会话备份的内部文件格式

新增独立 `session_backup.h` 和 `sessions/backup.c`。统一捕获窗口内仅复制
当前逻辑文件，之后用独立拥有的字节检查引用和编码；网络、hash/base64 和
JSON 解析不进入捕获锁。v2 manifest 保存文件相对路径、原字节、长度与
SHA-256，覆盖 snapshot/journal、UI、图片及名称、todo/反馈/草稿/队列、
回执及 artifact；声明来源、缺失的可选文件和实际保留范围。预算、锚定
no-follow、identity 复核、已知路径白名单和失败释放均显式执行。

这是 [备份实施记录](session-backup-plan.md) 第三阶段的格式子阶段。
manifest 明确 `restore_ready:false`；所有产品 schema、实际账本/UI 恢复
和专用传输仍待完成，页面现有 v1 菜单不改。排查发现 xs 的非阻塞发送队列
不能一次受理上限文档，后续必须处理分块、背压及 XS_TAKEOVER 生命周期，
不会仅扩大普通 API/下载限额。格式测试不代替正式页导出/新 Home 恢复。

小型 fixture 验证捕获之后的草稿修改不改变备份，单文件/总字节/文件数/
文档大小/时间预算、未知文件、缺失图/artifact、残缺日志的失败与重试。
独立 Python 逐文件校验实际 base64、长度和 SHA-256，以及 Unicode 名称、
原图含零字节、UI 范围与全部侧车内容；有链接权限的平台另测实际链接拒绝。
fixture 不执行模型、shell 或队列，不是完整 schema/replay 恢复证明。

Windows/Linux 完整有界门禁通过 114 Python、240 Node、90 模块解析、严格
C11、32 运行探针、A/B 确定性打包和 packed Home 租约/队列恢复；Windows
另通过便携 WebView2 Home 和打包崩溃/20 秒启动。最终加强不可变字节读回
断言及 API guard 注释后，两平台重跑 session 探针、A/B、packed 租约/队列，
Windows 最终包再通过便携窗口/20 秒启动。Linux 明确验证实际符号链接拒绝。
日志为 `.build/qa-session-backup-format-{release,linux-release,final,final-linux}.log`。
根目录 `mdo.exe` 已更新，与最终 Windows A/B 包一致，SHA-256 为
`1d3059efa6127f2eae7a9ee0a11a8916dbab46c6f33e2a70effaf36be7340709`；
Linux 最终包为 `4e5f26d3bc7f406699f883e9ed3730d07601db6bbf3b8a99ec65ecdd63a90582`。
便携 WebView2 缓存继续位于 `mdo-home/data/cache/webview2`，首次原生启动
创建 Home 的选择保持。未做压力或高负载测试。

## 2026-10-01：会话备份统一捕获边界

完整备份按 [实施记录](session-backup-plan.md) 完成第二阶段。新增
`MdoSessionWithCapture()`，在最新 metadata、root run claim、底层 data 排他
租约与 event bridge 的固定边界内同步读取。API 入口另按固定顺序尝试取得
attachment/draft/queue/feedback 存储锁，覆盖 GET 清理、直接 helper 和回执
修复；遇到占用立即释放并重试，不把网络下载速度带进排他窗口。新增冲突
提示已有中文/英文/俄文映射。原 v1 JSON 只含 meta/snapshot，保持兼容，
没有误称已完成带图完整备份或恢复。

模型捕获另拒绝本会话 pending/running 后台任务和未释放的 child owner；
task → owner 检查顺序覆盖已发布但尚未启动的直接子任务与存活后代。事件、
待办、附件引用、剪枝、分叉图片复制及回收都登记底层写租约，其他 metadata
handle 由全局 manager 锁固定。租约关闭后的最终释放安全，重新 Init 不会被
旧 owner 解锁；不创建新的磁盘目录/锁。checkpoint 后才能安全取得 data
排他，避免碰撞中的新运行被误拒绝首条消息。

真实 xs/TCC 探针覆盖四类 API 锁的跨线程占用、部分取得后的逆序释放、嵌套
拒绝保留外层锁、成功重试；底层别名、嵌套 writer、不同会话和关闭后释放；
reader 内直接侧车写入/剪枝/分叉拒绝、其他 handle/thread 排他、reader 错误
保留与 claim 释放。真实后台 child 用信号量暂停模型回调，确认捕获拒绝而
没有调用 reader，结束后成功捕获；pending/running/terminal 任务和 retained
后代引用也覆盖。普通 checkpoint 与既有前端交互保持。

Windows/Linux 后端全门禁通过 114 Python、240 Node、90 前端模块、严格 C11、
32 运行探针、确定性打包；Windows 另通过便携 WebView2 Home、打包崩溃/20 秒
启动。三语映射最后补齐后，两平台再通过 contract、全部前端检查、A/B、
packed Home lease 与队列恢复，Windows 最终包另通过便携窗口/20 秒启动。
日志为 `.build/qa-session-capture-{release,linux-release,final,final-linux}.log`。
根目录 `mdo.exe` 已更新，与 Windows 最终 A/B 包一致，SHA-256 为
`b2a3e7db3a2a63d6735c071bcba2cfd40d7276cb6efa38a6d18ecae278647ddd`；Linux
原生文件系统最终包为
`195ffc4b7d5ecf19203f59bbfe0b040659c5900ac2cbebd0f52dc6da3b910bad`。

本阶段不代替完整会话导出/恢复、原生下载或实体手机验收。下一步是版本化
manifest、有界传输、离线验证与新会话原子恢复。未做压力或高负载测试。

## 2026-10-01：外部历史修改同步时间线和待办

正式打包复现表明，事件轮询把 `history_truncated` 作为普通追加记录，外部清空/截断后旧消息卡仍留在缓存，待办观察器又只允许事件 ID 增长，无法恢复更早的计划或清空计划。现在按服务端标记的 `[source_event_id, event_id)` 移除已载入的事件；保留前缀、边界及之后的新回合，模型账本 sequence 复用不影响判断。只接受有效正整数范围，旧式或自定义无范围说明仍按原文显示；记录上限和每轮分页上限保持有界。重叠轮询只发布最新读取，迟到响应不能复活已删除缓存或退回 cursor。

待办收到有效历史边界后使旧读取失效、重置原单调事件下限，并读取服务端修复后的投影；保留计划可以回退到较早事件 ID，清空可以回到 0。响应仍落在已删除范围时按原有有界退避复查，不重新显示旧计划。编辑框保持已输入文字；关闭时原按钮已移除，则通过所属会话校验后的回退直接聚焦输入框。其他路由没有回退焦点，迟到关闭仍不能影响新编辑框。

最终 Windows 单文件 Home `.build/mdo-packed-docks-hsnpym5_` 从页面完成 `TODO UI` 与 `TODO UPDATE UI`，计划为 2/2。在第二条编辑框输入 `MY RETAINED EDIT UI` 后，本地 API 模拟另一客户端从 sequence 5 截断，原第二条来源 ID 为 10，新的范围标记为 19。已有页面无需刷新即只剩第一轮和截断说明，待办由事件 14 回到事件 5、显示 1/2；编辑文字及输入焦点不变。点击取消后焦点为 `prompt`。先前无回退的候选 Home `.build/mdo-packed-docks-g8ivvpxe` 对此操作会落到 BODY，正式候选已纠正。

同一最终页在 320×350 输入下一条 `DRAFT AFTER HISTORY CLEAR UI`，再由本地 API 模拟另一客户端清空。页面自动移除旧用户/助手卡和计划，只保留标记 20；待办服务投影为事件 0、空列表，输入文字及 `prompt` 焦点保留，文档宽 320px，浏览器错误日志为空。截图为 `.build/qa-live-history-edit-retained.png` 与 `.build/qa-live-history-clear-mobile.png`，服务端记录为 `.build/qa-live-history-truncate.json` 与 `.build/qa-live-history-clear.json`。本轮没有在图形页执行永久清空，外部客户端由本地隔离 API 模拟。

七项新 Node 用例覆盖范围合并、不改原数据、无范围说明、明确清空与 gap、反序轮询、旧待办读取隔离、保留计划回退/新计划更新、删除范围内的旧响应重试；编辑用例补验来源移除后的安全焦点回退和路由变化拒绝。Windows 打包操作及两平台有界门禁之外，原生窗口/实体设备与阅读保留前缀时的滚动位置仍需独立验收。

最终 Windows/Linux 有界发布门禁均通过 114 项 Python、240 项 Node、90 个模块解析、严格 C11 编译、32 个运行探针、Home 租约/队列恢复及 A/B 确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒单文件启动。根目录 `mdo.exe` 已更新，SHA-256 与实际操作的候选及 A/B 包同为 `fa3832965af75569998e4dbe2d33cb1f9855d7d358ed2db1fe7955aac5808149`；Linux 原生文件系统包为 `b6bca2581973f5e676765234232dfba496679a3b9e4014e4c1bb257f7fc244a4`。日志为 `.build/qa-live-history-focus-release.log` 与 `.build/qa-live-history-focus-linux-release.log`。未做压力或高负载测试。

## 2026-10-01：消息编辑与重试绑定原始事件

旧消息的编辑或重试原先只核对模型账本 sequence，再获取最新 ETag 截断。另一客户端清空并重新运行后，sequence 会复用；这让旧操作能够撤回另一客户端的新消息。修复前根目录包的隔离 Home `.build/mdo-packed-docks-55kglozd` 中，原用户消息的事件 ID 为 1，替换后为 7，两者 sequence 均为 2；按当时前端正文提交序列单字段请求返回 200，新消息被移除。原编辑窗口保持打开，最终破坏性请求通过本地 API 复现。

前端现在把原始顶层 `agent_start` 事件 ID 随编辑与回复重试传递；后端在维护准备阶段核对该事件仍拥有 `through_sequence + 1`，拒绝来源不匹配并返回 `409 session_message_changed`。通用截断 API 保持兼容。编辑弹窗等异步提交成功后才关闭；失败时文字和附件参数保留、错误获焦，提交中阻止重复激活和取消。已有 Enter/Shift+Enter/输入法行为保持，短屏错误状态缩减辅助说明，保留可编辑正文与操作按钮。

最终 Windows 单文件 Home `.build/mdo-packed-docks-3paog76y` 从页面发送 `STALE ORIGINAL UI`，打开编辑框并输入 `MY UNSENT EDIT UI`，再用本地 API 模拟另一客户端替换为 `UPDATED BY OTHER CLIENT UI`。新旧事件仍为 1/7，sequence 仍为 2；点击“保存并重新发送”后出现明确中文冲突，弹窗不关闭、文字不变，焦点在错误提示，Tab 到取消按钮。会话目录全部文件的前后 SHA-256 相同，新消息仍在且没有第三次运行。320×250 中正文 y=89–133、错误 y=139–174、两按钮 y=185–225，文档宽 320px，浏览器错误日志为空。截图为 `.build/qa-stale-message-windows.png` 和 `.build/qa-stale-message-mobile.png`；记录为 `.build/qa-stale-message-after.json`。

Node 用例覆盖来源传递、复用 sequence 后的拒绝、无来源拒绝、提交等待/重复激活、失败保留与迟到响应隔离；真实 C/TCC 探针覆盖同 sequence 的新旧事件，API 探针覆盖合法来源、错误事件种类/边界、非法字段、已移除来源以及拒绝时文件不变。此证据不代替原生 WebView/实体手机验收。另观察到外部清空后已有页面仍暂留旧消息卡；本次保护其操作不误撤回新消息，后续仍需修补实时历史边界投影。

Windows/Linux 有界发布门禁均通过 114 项 Python、232 项 Node、90 个前端模块解析、严格 C11 编译、32 个运行探针、Home 租约/队列恢复与 A/B 确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒单文件启动。Windows 包 SHA-256 为 `09e13914b6729971737bd95ec7b2487ea1179eb56f8fc3029603d2af0cb54107`，Linux 原生文件系统包为 `4098ef53a3d4e2d762c4d682242bfde8102ba1b4dec48d7938835a8efa6b8cb7`。门禁日志为 `.build/qa-stale-message-release.log` 与 `.build/qa-stale-message-linux-release.log`；未做压力或高负载测试。

根目录 `mdo.exe` 已从 Windows 验证包更新；其 SHA-256 与 A/B 两包及上述图形候选一致。

## 2026-10-01：JSON 导出保留 checkpoint 读取排他权

完整备份审阅发现现有 `MdoSessionExportJson()` 在 checkpoint 返回时已释放
Agent run claim，随后才读取 snapshot；持有 session handle 锁不能阻止已经
取得 Agent 引用的其他调用者启动下一次运行。本轮增加同步有界捕获 API
`MdoAgentSessionWithCheckpoint()`，使 checkpoint、读取和 owned 输出复制都
位于同一排他运行窗口。它持有 Session 引用，checkpoint 失败不调用读函数，
读函数失败保留错误，没有提供错误时补充 I/O 错误；窗口和引用在成功或失败
时均释放。嵌套捕获拒绝不能释放外层已有的 claim。普通 checkpoint 共用实现。

导出 callback 另在 manager 锁下核对 meta revision 并读取元数据，防止其他
handle 修改导致不一致。陈旧或未打开的 handle 明确拒绝；失败时输出 size 为
零。现有 `export_schema:1`、下载入口和前端操作保持兼容，未增添构建依赖。
这是导出一致性修复，不能据此声称整个会话目录已经获得原子快照。

真实 xs/TCC 小型探针在 memory 开启/关闭两种配置下验证：读函数执行期间再次
申请窗口被拒绝，checkpoint 文件已经写好，成功/带错误失败/无错误失败后可
访问账本，已占用窗口时不执行读函数且不误放 claim，空 error 和无 snapshot
路径正确处理。会话探针验证未打开 handle、另一 handle 重命名后的陈旧导出、
注入 metadata 读失败后输出清零、claim 释放及重新导出成功。没有依赖压力或
高负载测试来复现竞态，也没有把此窗口问题归因为此前的 xrt future 崩溃。

Windows 与独立 Linux ext4 镜像的完整有界门禁均通过 114 Python、227 Node、
90 个前端模块解析、严格 C11、32 个运行探针、确定性 A/B、打包 Home 租约及
队列恢复。Windows 另通过便携 WebView2 Home 和 20 秒单文件启动。日志为
`.build/qa-checkpoint-capture-release.log` 与
`.build/qa-checkpoint-capture-linux-release.log`。Windows 两次包及更新后的根目录
`mdo.exe` SHA-256：
`8e9b65dc0264254ad101b2ad0d44c799662b1920459c369f6e86f7f51ad0f79a`；
Linux 包：`6817af1a0e7a63ba6e11146abf981b93f341b1dac127c6236bfcdad76ce3119e`。
本轮未修改前端交互，不重复宣称上一阶段浏览器下载及图片 Blob 核对为本轮证据。

新增 [完整备份实施记录](session-backup-plan.md)，列明模型账本、UI 日志、图片、
artifact、todo、反馈、草稿和队列的写入边界，当前 256 KiB 请求/33 MiB 下载
限制，以及独立 staging、校验、原子发布和不自动执行队列的恢复要求。JSON v1
内容仍不完整，新的格式、恢复入口和两份 Home 间读回验证尚待实现；原生/实体
设备及之前审计边界仍未关闭，长期目标保持进行中。

## 2026-10-01：Markdown 导出携带原图与原文件名

上一阶段已确认真实下载完成，但新版 Markdown 仅输出附件 ID，无法像旧版一样离开原 Home 读取图片。本轮新增独立 `session-export-images.js`：从最终时间线投影选出实际导出的附件，按原项目/会话读取元数据和原图。重复编辑/重试引用只读取一次。元数据身份、版本、图片类型、名称和大小不一致时不内嵌；正文流按元数据大小有界读取，过长、过短或响应类型错误均拒绝，关闭未完成流。每次导出最多 16 个唯一附件、32 MiB 原始图片字节和 30 秒；失败下载也预留并消耗读取预算。没有新增构建依赖或请求外部图片。

格式化器输出标准 Markdown 图片引用，在文件末尾为每个 ID 写一份 data URL 定义。这样保留原始图片和完整文件名，同一图片在多条消息中重复出现时不会复制大段 base64；整个文件可脱离 Home 保存。原始名称只作图片说明，对 Markdown 标点转义，不作为路径或 HTML。旧 v1 无文件名附件用 ID 追溯。缺失、读失败或达到预算的图片仍保留附件 ID，文件内明确提示需要原 Home；中英俄提示已齐全。完整 CommonMark 增强仍是独立范围，本轮不扩张现有聊天渲染器。

新增五项 Node 回归，覆盖纯图片空正文、原图字节与名称、重复引用只读/内嵌一次、缺失图片后继续读取、旧 v1 兼容、错误身份/名称/大小/流拒绝、图片数量和传输预算（含失败预留），以及截止时间取消后不继续读下一图。三语导出用例核对不完整提示；既有长正文补读与明确原文测试继续通过。边界测试使用小图和缩小的预算，不做压力或高负载测试。

最终 Windows 单文件 Home `.build/mdo-packed-docks-shw1kweb` 与 Linux ext4 Home `/home/ubuntu/.cache/mdo-linux-qa-markdown-images/.build/mdo-packed-docks-w93b93f6` 各实际空文字发送两张合成 PNG，等待成功后从正式标题栏导出。两端都得到 1253 字节的 Markdown Blob；各有两份内嵌定义，解码后均为 68 字节且与输入 PNG 的全部 base64 完全一致。15 字符名称 `截图 "1" 100%.png` 和 204 字符长名称完整保留，无 ID 占位、无不完整提示。Linux 实际工作台与文档宽度为 320×350；刷新工作台后再次导出，两端内容结果保持。Windows 首次下载 GUID `780af51c-7768-4fdb-aeb7-d6eb4ba89fcc`、Linux `7087df01-fc1a-4b92-ba4c-2a5c22fea5bd` 都报告 1253/1253 字节 `completed`。详情在 `.build/qa-markdown-images-*-events.json`、`*-captures.json`；截图 `.build/qa-markdown-images-windows.png`、`qa-markdown-images-linux.png`。内嵌浏览器没有可独立读回的磁盘路径，不把 Blob/传输核对写成磁盘文件验收，也不代替实体设备或原生 WebView 下载验证。

Windows 最终有界门禁通过 114 Python、227 Node、90 个模块、严格 C11、32 个运行探针、确定性 A/B、打包 Home 租约/队列恢复、便携 WebView2 Home 和 20 秒启动。最初门禁期间代码审阅补上流关闭与名称空格保留，前后包不同而被 A/B 拦截；该失败日志保留为 `.build/qa-markdown-images-release.log`，源码稳定后的整轮重跑及最终包通过，日志为 `.build/qa-markdown-images-release-final.log`。Linux 的 C 源码未变；独立 ext4 镜像完成相同有界门禁及最终 A/B，并在同步这两项最终 JS 修正后另跑完整前端门禁，见 `.build/qa-markdown-images-linux-release.log`、`qa-markdown-images-linux-final-web.log`。Windows 包及根目录 `mdo.exe` SHA-256 为 `3ab8a2d6ced06f7c85d94261525d37206d0b947d2c8cd1d845414fd2b78ef1fb`；Linux 为 `94b3e7fc82c5d41a2d6f99cf80785b53749de9e5109ce832ca6b84827f14dc41`。

本轮 iframe 观察页的两平台各记录两条无栈 `MutationObserver.observe` 同文错误；本轮没有取得它们的调用栈，不能仅凭文字归为上一阶段已定位的 Electron 注入错误，更不能宣称整个夹具无错误。另打开两平台独立正式会话页并点击同一导出入口，错误日志均为空、消息与图片名称及操作按钮正常。上述下载完成和 Blob 字节证据独立成立。

本轮关闭 Markdown 的图片内容缺口。JSON v1 仍仅提供 meta 与运行快照，没有 UI 事件和附件字节，完整可携带备份及恢复验证继续待实现；没有靠改名隐藏这一缺口。原生端、实体手机和此前审计边界仍待验收，长期目标保持进行中。

## 2026-10-01：导出下载完成事件与图片可携带性核对

上一阶段的内嵌浏览器下载等待接口超时，不能据此确定 `mdo` 下载失败。本轮从正式导出按钮和菜单观察 `Page.downloadWillBegin`/`Page.downloadProgress`，并新增 `tests/fixtures/packed-export-download-browser.html`。QA 代理仅在显式 `--export-download-fixture` 下提供该页面；页面在自己持有的工作台 iframe 中读取原始 Blob，仍调用原生 `createObjectURL`、保留原生 URL 和正式下载动作，关闭页面后观察器消失。产品源码及打包资源没有修改。JSON API 的 MIME 为 `application/octet-stream`，观察器按备份文档形状识别它，不能只按 `application/json` 判断是否已经导出。

Windows 隔离 Home `.build/mdo-packed-docks-edralh3s` 通过实际输入完成普通文字回合，标题栏导出收到 GUID `1342499e-9077-459a-b448-a3fc314a6b20` 的 297/297 字节 `completed`。另一个 Home `.build/mdo-packed-docks-9eus21i9` 实际完成文字与两张合成 PNG 的纯图片回合：文字 Markdown 300 字节完成；带图 Markdown 555 字节完成，JSON 2903 字节完成且正式 Blob 可以解析为含 `export_schema`、`meta`、`snapshot` 的对象。下载详情及 Blob 元数据保存在 `.build/qa-export-download-windows-*-events.json`、`*-captures.json`；截图为 `.build/qa-export-download-windows.png`。这些文件只含隔离 QA 数据。

Linux 独立 ext4 镜像 `/home/ubuntu/.cache/mdo-linux-qa-export-download` 的单文件包在 Home `.build/mdo-packed-docks-fahf_2h0` 完成一次实际纯图片回合。父页面 320×460，实际工作台及文档宽度为 320×350；标题栏 Markdown 下载 GUID `68cf89da-6eea-4778-8e5b-cc2b2cbe7c5f` 为 278/278 字节完成，菜单 JSON GUID `2dfa7a71-6390-4777-a8a8-8e66de9761a5` 为 2500/2500 字节完成，两者与正式 Blob 大小一致。证据在 `.build/qa-export-download-linux-*-events.json`、`*-captures.json`，截图为 `.build/qa-export-download-linux.png`。当前内嵌浏览器没有返回可独立读回的磁盘文件路径，故仅确认浏览器传输完成和交付内容，不把它写成磁盘文件读回验收，也不据此推断实体手机或原生 WebView 的下载通过。

内容核对同时发现下一项实质缺口：两平台的 Markdown 均只输出两个附件 ID，内嵌图片数为零、合成 PNG 字节不在文件中；旧版 `app_bak/wwwroot/src/chrome.js` 会写入图片 data URL。因此旧版可独立携带图片的 Markdown 体验尚未恢复。JSON `export_schema:1` 根据现有 `MdoSessionExportJson` 合同只导出 meta 和 checkpointed xllm snapshot，本轮含图备份同样不包含 PNG 字节及 UI 日志；它不能当作完整带图会话备份。这两项进入后续导出修复范围，不能用下载完成关闭内容缺口。没有对未证实的对象 URL 回收时序作补丁。

Windows 与 Linux 重新构建的程序字节分别保持上一阶段 SHA-256：`53d4228fde4ccea8b89512ac5117b8d029d0317bf6f4923cef400f947f037eab` 和 `e02d3a2a1f2fcce34481069be2d1380952d55155f36a923986305d939b040321`。根目录程序与新 Windows 包一致。本轮通过 114 项 Python、222 项 Node、89 个前端模块解析及两平台 QA 脚本编译；上一阶段对相同产品字节的严格 C11、32 个运行探针、确定性 A/B 和便携窗口门禁仍是上一阶段证据，本轮没有重复运行或重复计数。未做压力或高负载测试。

## 2026-10-01：纯图片消息保留实际空原文

基线 `e3d3f81` 的打包页在纯图片编辑重发后显示 `[Image attachment]`，
编辑和重试还会把它当作正文。旧版 assembler 从 content 分别提取文字和
图片，ui 仅在文字存在时渲染气泡；新版已具备空气泡隐藏样式，差异来自
xwork 诊断启动事件的占位文字进入了 mdo 的持久 UI 会话记录。

mdo 在启动前把实际空 Prompt 的意图与图片引用、运行 ID 一起登记；持有
既有 bridge 锁时，只对匹配运行的顶层图片启动事件复制并还原空文字，
写入持久 UI 记录。成功或撤销后清理该意图，其他运行及恢复不继承。没有
按文本值猜测；用户真的输入 `[Image attachment]` 会完整保留。xwork
原始诊断事件、模型消息与会话格式不变，现有文字无法可靠追溯是否为占位
符，保持原样；用户手动清空后重发可写入正确空原文。

验证：

- 扩展真实 xs/TCC 图片运行探针，覆盖空 Prompt、普通图文及明确输入
  占位同名文字，API text/original_text_bytes 与输入一致；图片仍进入
  模型请求。进程重启后文字和引用回放，旧 run-ID 引用兼容、重用运行 ID
  的新文字/图文不被误清；分叉首条纯图片消息保留空文字及元数据。两项
  Node 用例覆盖纯图片时间线/重试引用和明确文字的投影。
- Windows 最终 A 包 Home `.build/mdo-packed-docks-j5ipb_u1`，源会话
  `V-ORlDbUWNlOJRpScU4NYEL9I18TpgcU`。合成粘贴两 PNG 后空文字发送；
  1280×720 正式页用户正文为空且 display:none，两图及完整名称保留。
  编辑原文为空、输入获焦且允许空文本，直接 Enter 重发，再实际重试后
  分叉 `V-ORlEgxPJAkV0MPwS38oruh9PD9JQpc`。分支仍空原文，编辑填入
  `[Image attachment]` 后重发及刷新，明确文字与两图保留；返回源会话
  仍为纯图片。截图 `.build/qa-image-only-text-windows.png`。
- Linux 最终 A 包 Home
  `/home/ubuntu/.cache/mdo-linux-qa-image-only-text/.build/mdo-packed-docks-_ehxb0fa`，
  源会话 `V-ORlDbO7NImJzigB8q4vft_LJISFBnN`，320×350 正式页实际空
  文字发送、空原文编辑/Enter、重试及回复分叉均通过。分支
  `V-ORlFZueUsLdnxaVKk1LWXje4Tlr8Pz` 的原文仍空，明确输入占位同名
  文字后重发和刷新完整保留，两图已加载、名称 15/204 字符，无横向溢出。
  截图 `.build/qa-image-only-text-linux-edit.png`、
  `.build/qa-image-only-text-linux-literal.png`。
- 两端 API 核对源为空文字/0 字节、分支为明确文字/18 字节，附件 ID 与
  源一致、两图各 68 字节且字节/名称相同；明确文字修改前另有分支空原文
  核对。结果 `.build/qa-image-only-text-{windows,linux}-images.json`。
  两端各四轮 succeeded、未取消、活动数零，
  `.build/qa-image-only-text-runs.json`；独立正式页错误日志为空。
- 从两端实际 API 事件调用正式 Markdown 格式化函数，空文字和明确文字
  各生成 395/471 字节文件，用户条目、两附件引用及正文有无均符合输入。
  这是 API/格式化验证，不是浏览器实际下载通过：Windows 的导出按钮
  点击后下载事件等待 10 秒超时，页面无脚本错误，尚未取得本轮下载文件。
- 通过只读 CDP 捕获本次 iframe MutationObserver 非 Node 异常的精确
  堆栈：script 4，零基行 193、列 6482，Yv/oc 调用链在 require("electron")
  包装的注入标注脚本中对 documentElement 执行 observe。该脚本含
  data-codex-browser-design-group 标记，属于 Codex 内置浏览器注入代码，
  不在 mdo 前端源码中。证据
  `.build/qa-image-only-text-injected-error.json`、
  `.build/qa-image-only-text-injected-source.txt`。未修改注入代码；以前没有
  堆栈的同文错误记录继续保留其原有事实，不推定所有平台的相似错误同源。
- Windows/Linux 完整发布门禁均退出 0，各通过 114 Python、222 Node、
  89 模块、严格 C11、32 运行探针、Home lease、队列启动恢复及确定性 A/B；
  Windows 另通过便携 WebView2/20 秒启动。Linux 全新 ext4 快照复用已
  验证宿主，依赖锁照常验证。日志 `.build/qa-image-only-text-release.log`、
  `.build/qa-image-only-text-linux-release.log`。
- Windows A/B 和根目录 SHA-256：
  `53d4228fde4ccea8b89512ac5117b8d029d0317bf6f4923cef400f947f037eab`；
  Linux A/B：
  `e02d3a2a1f2fcce34481069be2d1380952d55155f36a923986305d939b040321`。

测试图片能力仅由隔离 QA 配置开启，模型通信使用本地服务。合成粘贴和
浏览器视口不代表系统剪贴板、原生窗口或实体软键盘验收；未做压力/高负载
测试，长期目标继续。浏览器导出下载超时留作后续独立排查。

## 2026-10-01：消息编辑隔离迟到关闭事件及带图历史操作补验

基线 `ef692e9` 的正式编辑控制器配合原生 dialog，先取消上一条编辑，
在同一任务内立即打开下一条带图消息。旧 dialog.close() 排队的 close
事件随后把新编辑也取消：reopened/focused 为 false，第二条返回 null。
这是组件夹具生成的快速序列，未将它描述成用户在完整工作台上的手动点击。

close 回调现在仅在 dialog 已关闭时处理当前 pending。新编辑已经打开时，
忽略上一轮迟到事件，保留内容、图片允许空文字的状态和焦点；外部关闭当前
dialog 仍取消并返回当前入口。两项新增 Node 用例覆盖这两个边界。

验证：

- 正式组件源码夹具确认新关闭条件实际加载后，快速取消/重开、空文字带图
  提交、焦点返回均通过；既有 keyCode 229、isComposing 与组合状态保护
  继续通过。初次 reload 后的结果仍失败，未计为通过；加入模块版本与
  已加载条件诊断后，新标签确认通过。尚未证明最初 reload 的失败原因。
- 最终 Windows/Linux A 包的 `/__qa/message-edit-enter` 在 320×250 均
  通过上述快速切换及合成 IME 序列。这一路由读取最终包的正式控制器，
  图片引用用于校验编辑状态，不代表实际上传；真正上传由下一项补验。
- Windows 最终包 Home `.build/mdo-packed-docks-9y6x0duu`，源会话
  `V-ORl9L64d6GYXqoBcpotiEm4G7YtgG3`。合成剪贴板交给真实输入区两张
  PNG，`@alpha` 用 ArrowDown/Tab 选中 `@src/alpha-test.c `，焦点保留。
  实际图文发送后进入完整工作台，历史预览/Esc 返回正确缩略图；编辑取消
  返回编辑入口、原内容和两图保留；再开编辑清空文字并 Enter 重发，图片
  保留且输入获焦。实际重试后仍两图，从回复分叉得到
  `V-ORlA1uZfFS74JZ0dlbg2xRgA5NtTfl`，分支加载两图并聚焦输入。
- Linux ext4 最终包 Home
  `/home/ubuntu/.cache/mdo-linux-qa-edit-reopen/.build/mdo-packed-docks-xgociult`，
  源会话 `V-ORl9O_W1d59B-KopmDap6BVaEi3d-U`。合成粘贴后正式页面在
  320×350 恢复两图草稿，键盘文件补全/图文发送、编辑取消、空文字带图
  Enter 重发、实际重试均通过。回复分叉得到
  `V-ORlAodKYX3dfaDGkYzemFI-l4sl0IB`；填写
  `LINUX FORK FOLLOW-UP DRAFT` 后刷新，草稿、两图、完整名称保持。
  独立正式页历史预览/Esc 返回正确缩略图，草稿不变且无横向溢出。
- 两端只读 API 核对源/分支各一条有效 agent_start，图片占位文字为
  `[Image attachment]`，两附件 ID 保留；源/分支的文件名分别 15/204
  字符，图片各 68 字节且 SHA-256 一致。核对结果保存在
  `.build/qa-edit-reopen-windows-images.json`、
  `.build/qa-edit-reopen-linux-images.json`。两端各三轮 succeeded、
  cancel_requested=false、活动数零，`.build/qa-edit-reopen-runs.json`。
- 合成粘贴 iframe 标签各记到一次 MutationObserver 非 Node 参数错误，
  来源仍未确认；正式前端没有 MutationObserver 调用。另开独立正式页后，
  Windows 分支编辑/取消及 Linux 短屏历史预览/Esc 的错误日志均为空。
  不将本次关闭竞态修复宣称为该错误的根因。截图
  `.build/qa-edit-reopen-windows-final.png`、
  `.build/qa-edit-reopen-linux-mobile.png`。
- Windows/Linux 完整门禁均退出 0，各通过 114 Python、220 Node、89
  模块、严格 C11、32 运行探针、Home lease、队列启动恢复和确定性 A/B；
  Windows 另通过便携 WebView2/20 秒启动。日志
  `.build/qa-edit-reopen-release.log`、`.build/qa-edit-reopen-linux-release.log`。
  Linux 全新 ext4 快照复用已验证宿主，依赖锁照常检查；没有重建宿主。
- Windows A/B 和根目录 SHA-256：
  `e34babd9ce3b0b6e045a27fd7cbc82560df07821c6c02ddabf23d5c2323b13eb`；
  Linux A/B：
  `54ddb12e64451252c28a09047316cdd85a585f22bdefbb92abcd8b9875f427bb`。

所有模型通信使用隔离 Home 的本地测试服务，图片能力仅由 QA 配置打开，
不据此宣称生产 Ling 的图片能力或图片理解质量。合成 IME/剪贴板及浏览器
视口不替代原生 GUI、实体软键盘、系统拖放验收；长期目标继续，未做压力
或高负载测试。

## 2026-10-01：历史编辑和重试后退出旧查询并恢复输入

基线 `38e9626` 单文件 Home `.build/mdo-packed-docks-h01hm2m1` 于
1280×720 搜索 `SEARCH ORIGINAL PROMPT`，从命中消息编辑成
`REPLACED PROMPT WITHOUT ORIGINAL WORDS` 并重发。服务端两轮均成功，
当前日志已有新提示与回复，但页面仍保留旧查询、显示零匹配与空时间线，
输入已启用却无输入焦点。截图 `.build/qa-search-replacement-before.png`。
旧版与新版均按查询隐藏未命中行；本轮保留该搜索模式，补齐从历史操作
回到新回合的路径。

编辑和重试共用的 onTruncated 回调现在于历史替换确认成功、且仍在原
会话时关闭搜索并跟随新回合。取消弹窗、前置校验拒绝和截断失败不执行
此回调，原查询保留；异步过程中切会话也不操作新页面。输入聚焦移到
消息操作 finally 中、setRun 解除禁用之后，仅当前路由且焦点落在 body
或断开节点时恢复，保留用户后续选择的控件。

验证：

- 新增两项 Node 边界用例验证截断拒绝及过期消息边界不通知可见提交；
  既有原会话绑定、前置校验、截断中导航和后台失败隔离继续通过。这些是
  回调合同用例，不单独宣称已验证完整搜索页面。
- Windows 最终 A 包 Home `.build/mdo-packed-docks-w9rozypi`，会话
  `V-ORkl5v6nmPH-vqqi6sHHboisaTBb0R`。1280×720 实际搜索、打开并取消
  编辑后查询与命中保留，焦点返回 user-1/edit；填写 `UNSENT DRAFT KEEP`
  后编辑被明确拒绝，草稿/查询不变。清除草稿后改成不匹配旧查询的文本并
  重发，搜索关闭、完整新提示与回复可见，prompt 启用并获焦。截图
  `.build/qa-search-replacement-windows-edit.png`。
- Windows 通过 1200ms 有界历史读取延迟，从回复搜索命中重试；期间打开
  检查器，新回合显示后 toggle-inspector 焦点保留。再次重试校验期间
  Ctrl+N 切新任务，填写 `NEW TASK DRAFT AFTER ABORT`，旧请求不截断、
  不新增运行，新任务路由/草稿/焦点保持，刷新仍保留。返回源会话后，
  仍只有正确的替换提示与回复，源草稿为空。一次过早的编辑弹窗等待未
  命中，后续页面就绪后正式取消链通过；未计入门禁通过数。
- Linux 全新 ext4 快照最终 A 包 Home
  `/home/ubuntu/.cache/mdo-linux-qa-search-replacement/.build/mdo-packed-docks-knryrq9v`，
  会话 `V-ORkl5tbVWGAv1bcU0ytze5k5Lwbrsj`。320×350 从
  `LINUX SEARCH ORIGINAL` 搜索命中取消编辑，查询与原按钮焦点保留；
  实际改为 `LINUX REPLACED TEXT WITHOUT MATCH` 并重发，随后从回复
  搜索命中重试，两次均关闭搜索并显示新回合，输入启用且获焦。
  `LINUX FOLLOW-UP DRAFT` 刷新保留；截图
  `.build/qa-search-replacement-linux-mobile.png`。
- 最终服务端两端各三轮 succeeded/cancel_requested=false；源会话当前
  提示均为所提交的替换原文，Windows 代理总计三次运行 POST、零取消，
  切路由的校验不新增运行。两端页面无脚本错误，正式夹具退出码为 0。
- Windows/Linux 完整门禁各通过 114 Python、218 Node、89 模块、严格
  C11、32 个运行探针、Home 租约、队列启动恢复及确定性 A/B；Windows
  另通过便携 WebView2 Home/20 秒启动。日志
  `.build/qa-search-replacement-release.log`、
  `.build/qa-search-replacement-linux-release.log`，两端退出码为 0。
- Windows A/B 及更新后的根目录 SHA-256：
  `e50fd79e34d1e9600c4878dc17b77f37637c7c6eb50637baf7f3acfaa5ff4723`；
  Linux A/B SHA-256：
  `580d73c6700775a4d66fabfedce9cb54cc5964d441b14ef5fd3808454b818c53`。

临时浏览器页和视口覆盖已清理。Linux 图形证据仍来自 Windows 浏览器
访问真实 Linux 服务，原生 WebView/输入法/系统拖放及实体移动端缺口
保留；长期任务继续，便携 Home 策略不变，未做压力或高负载测试。

## 2026-10-01：搜索期间收起待决覆盖层并露出匹配正文

基线 `80b0779` 单文件 Home `.build/mdo-packed-docks-qqlcfeee` 在
320×250 生成长询问及真实审批后复现：展开待决卡片再按 Ctrl+F，搜索框
本身可点击，但匹配消息被覆盖；关闭搜索后，prompt 虽获焦，中心仍被
卡片遮挡。审批搜索的消息中心命中 SECTION.conversation-dock，截图
`.build/qa-search-expanded-approval-before.png`；没有把它误报为搜索框遮挡。

搜索新增关闭生命周期，工作台在打开搜索前收起决策覆盖层并隐藏悬浮
停靠卡片；卡片节点、待决状态和未提交回答继续保留。搜索期间的新待决
正常更新，但不触发焦点/滚动揭示。关闭搜索先恢复普通卡片，再聚焦输入；
切会话也解除隐藏，不聚焦已离开的会话。停靠控制器返回明确的
setSearchActive/destroy 接口；仓库现有调用未使用旧的销毁返回值。

首版修复的 Windows/Linux 门禁均通过 114 Python、213 Node，但最终
单文件页又发现极短屏的独立定位缺陷：卡片已隐藏，搜索仍对齐角色/时间，
正文 y=124.39 落入输入区。时间线现在按实际行高和可用空间判断：空间
不足以同时容纳标题和第一行正文时，优先露出正文；宽屏保留原定位。
初版组件只用静态段落，未发现此问题；最终夹具改用正式时间线渲染组件，
不把简化夹具当作完整页面验收。

验证：

- 六项新增 Node 回归覆盖搜索关闭顺序、不可用/重复开关、切会话、IME
  及 229 保护，以及正常/临界/短屏/放大行高的正文定位。
- 正式组件夹具 `conversation-search-docks-browser.html` 于 320×250
  展开询问、输入自由回答、Ctrl+F 搜索；正文首行可点击，卡片隐藏，回答
  保留。250ms 有界新审批更新不抢搜索焦点；切换英语/俄语、关闭搜索和
  切会话后返回均保留回答，Esc 返回可点 prompt，停止次数为零。
  最终实际时间线夹具的正文 y=100.39；俄语关闭后回答保留、两卡恢复。
- 既有搜索 IME、决策展开到达、询问键盘视口三项 DOM 夹具通过。初次
  两个自动夹具在页面加载后才设视口，记录到旧高度；改为加载前设置
  280×250 / 390×700 后通过。新夹具最初导入名及语言挂载路径错误已修正。
  桌面移动栏隐藏时一次点搜索按钮未命中，随后改用 Ctrl+F；这些夹具
  启动/操作失败未计为生产通过。
- Windows 最终 A 包 Home `.build/mdo-packed-docks-18rrinpr`，会话
  `V-ORkh7zgkyWcbUmUKYNhgqU4qYgpK5c`。320×250 展开真实 `LONG ASK UI`，
  自由回答 `Windows final answer retained`，主草稿 `WINDOWS NEXT DRAFT`。
  搜索后正文 y=100.39、首行命中真实正文，回答/草稿不变；Esc 返回可点
  prompt，卡片以普通状态恢复，随后展开并提交回答完成。真实审批同样
  展开→搜索→Esc→拒绝，草稿保留。刷新回放两轮历史与
  `WINDOWS DRAFT AFTER APPROVAL`。独立 1280×720 页的角色/时间及正文
  均可见，未因短屏策略丢失标题。截图
  `.build/qa-search-expanded-final-windows-mobile.png`、
  `.build/qa-search-expanded-final-windows-desktop.png`。
- Linux 全新 ext4 快照最终 A 包 Home
  `/home/ubuntu/.cache/mdo-linux-qa-search-expanded-final/.build/mdo-packed-docks-lari634h`，
  会话 `V-ORkh7yVz2HYXSagrmtMUJlQEoLBVG5`。同样验证询问回答
  `Linux final answer retained`、正文首行、Esc 可点输入和实际回答提交。
  两轮真实审批均拒绝完成；首轮在渲染前读取到 bodyHit=false，第二轮
  等结果计数后核对 y=100.39、bodyHit=true，未将瞬时旧布局报作最终失败。
  刷新回放三轮历史与 `LINUX DRAFT AFTER APPROVAL`。截图
  `.build/qa-search-expanded-final-linux-mobile.png`。
- 服务端最终核对 Windows 两轮、Linux 三轮均 succeeded 且
  cancel_requested=false；待审批数为零，询问工具结果为所保留的回答，
  exec 结果为主动拒绝；两端队列已排空，页面脚本错误为空。一次 API
  读取使用超过允许上限的 limit=64 返回 400，改为 32 后核对通过。
- 最终两端完整门禁各通过 114 Python、216 Node、89 模块、严格 C11、
  32 个运行探针、Home 租约、队列启动恢复和确定性 A/B；Windows 另通过
  便携 WebView2 Home/20 秒启动。日志
  `.build/qa-search-expanded-final-release.log`、
  `.build/qa-search-expanded-final-linux-release.log`，两端退出码均为 0。
  首版日志 `.build/qa-search-expanded-release.log` 和
  `.build/qa-search-expanded-linux-release.log` 保留，不作为最终字节证据。
- Windows 最终 A/B 及更新后的根目录 SHA-256：
  `0cb5d5aeae187ab067c3bf8c03d88f6019fd9abe863c7567bff694300bc2848d`；
  Linux 最终 A/B SHA-256：
  `45533a9fb4d28ba304275121411f9b706e2bd3a8884618d41d30ff3bd3725d0a`。

两端正式夹具正常退出，临时浏览器页已关闭，视口覆盖已清理。Linux 页面
仍由 Windows 浏览器访问真实 Linux 服务，不等于 Linux 原生 WebView
验收。用户选择的 WebView2 便携 Home 策略继续保留；原生点击、输入法、
系统拖放及实体移动端等缺口仍待验收，长期任务继续。未做压力或高负载测试。

## 2026-10-01：新任务首次发送后恢复输入焦点

上一阶段 Linux 最终单文件页在首次懒创建并完成回复后，prompt 已启用，
但焦点仍落在 body；等待 3 秒也未恢复。新任务 pump 保存及迁移草稿时
短暂禁用输入，浏览器丢失原输入框焦点，原 onMigrated 无法再判断归属。

新增独立的 new-task-composer-focus 模块：仅在空白新任务的已聚焦输入框
即将禁用时记录项目与焦点意图；确认目标会话后，等待该会话输入可用，
再一次性恢复焦点。记录仅在内存中，不把重启恢复或后台页面当作用户
正在输入。项目/路由变化、另一控件获焦、点击非输入内容、窗口失焦、
页面隐藏、显式新任务及准备失败均取消记录。原准备和迁移期间的输入
禁用规则保留；恢复使用 preventScroll，不改草稿、运行或队列协议。

验证：

- 新增七项 Node 回归覆盖禁用/迁移/启用顺序、恢复一次、后台/已有会话
  不捕获、后续焦点和非可聚焦内容点击、项目/路由变化、窗口失焦/隐藏、
  错误目标及显式取消。真实 DOM 夹具从按钮启动，通过保留、另一控件、
  内容点击、离开后返回、后台五种情况，使用正式模块及真实 textarea。
  初版夹具假定禁用一定移焦 body，首例实际仍保留 disabled textarea；
  最终夹具记录两种浏览器结果，但严格要求仅保留场景最终聚焦 prompt。
- Windows 最终 A 包 Home `.build/mdo-packed-docks-27cr37rz`，原会话
  `V-ORkVmfIZJWFMfvzwxrOeblE9XDL7MC`。320×350 键盘新建并发送
  `FIRST TASK FOCUS WINDOWS`，创建 `023786106aefb3cde91203f34ed41cd7`，
  回复完成后 prompt 启用且获焦；直接输入下一条草稿，刷新仍保留文本
  和焦点。磁盘核对原草稿、下一条草稿独立保存，队列为空；截图
  `.build/qa-new-task-focus-windows-mobile.png`。
- Windows 另一个最终 A 包 Home `.build/mdo-packed-docks-a46ss426`，
  创建 POST 有界延迟 5 秒。960×600 创建期间打开检查器，迁移完成至
  `73e29a32f41f66cc6107762cb8ebade0` 后焦点仍为 tasks-tab；之后正常
  新建 `df9906052da78cf9ef2ae9e70461a95e`，回复后 prompt 恢复焦点。
  两轮实际运行均 succeeded/cancel_requested=false，页面错误为空。
- Linux 全新 ext4 快照最终 A 包 Home
  `/home/ubuntu/.cache/mdo-linux-qa-new-task-focus/.build/mdo-packed-docks-x1e6cxv1`，
  同样有界延迟创建 5 秒。320×350 首条创建
  `8c6def2f6907a9aa3b572c3f9430a446`，回复后 prompt 启用并获焦，
  下一条草稿刷新保留；截图 `.build/qa-new-task-focus-linux-mobile.png`。
  创建期间打开检查器后，`eda1dc6c9a054035a49bd08e5e481e10` 的回复
  完成仍聚焦 tasks-tab。另在创建期间切回原会话
  `V-ORkX-JpdnnjCwAidSVaDGJ46q1QPXF`，创建完成不把路由切回，原草稿
  保留；随后选择 `a7f3318a6359bbc468f2931e98fde4c5` 才继续其待发项，
  该会话正常回复并获焦。最终四轮运行均成功且未请求取消，页面无错误。
- 两次尝试等待短暂的 disabled 状态未命中；后续状态核对均确认已完成
  并获焦，未将等待超时记作输入失败。一次通配 URL 等待也超时，最终
  核对实际路由与检查器焦点确认通过。Linux 首次非交互 helper 因 stdin
  EOF 退出 1，改用 PTY 后正式夹具正常运行；这些不计入门禁通过数。
- Windows/Linux 完整门禁各通过 114 Python、210 Node、89 模块、严格
  C11、32 个运行探针、Home 租约、队列启动恢复及确定性 A/B；Windows
  另通过便携 WebView2 Home/20 秒启动。日志
  `.build/qa-new-task-focus-release.log`、
  `.build/qa-new-task-focus-linux-release.log`，两端最终进程退出码均为 0。
- Windows A/B 及更新后的根目录 SHA-256：
  `949b8b57f2586d4c51d023e017ff759d01b2d76aa7bfeeae1c4036a1dc8c4cf0`；
  Linux A/B SHA-256：
  `f0a9f45d3572c2a656e186e589ea27ef549790f1089bb990d3c55f4803680925`。

首次懒创建的输入焦点缺口关闭。两端正式夹具正常退出，临时浏览器页
已关闭。Linux 图形证据仍来自 Windows 浏览器
访问真实 Linux 服务，不等于 Linux 原生 WebView 验收。原生窗口点击、
系统拖放/输入法、实体移动端与其余审计缺口保留；长期任务继续，未做
压力或高负载测试。

## 2026-10-01：新任务入口收起覆盖输入区的检查器

基线 57cff72 的 Home `.build/mdo-packed-docks-01vmkboh`，会话
`V-ORkPVW174btfrXhk4vmbOamIAP7kiJ`：320×350 打开检查器，从任务
标签 Ctrl+K 后路由已变为新任务、prompt 已获焦，但检查器仍打开，
输入框中心命中“暂无后台任务”。960×600 从侧栏配置创建会话
`V-ORkPtFid7-cH4h5Gg2w1P-3RxiSOkC` 后，也仍留下覆盖输入区的检查器。
基线截图 `.build/qa-new-task-drawer-before.png` 保留。

普通新任务和配置创建成功两个入口现在均调用既有 closeDrawers，与
项目切换、已有会话选择保持一致；关闭手机覆盖面板及中等屏检查器，
宽桌面常驻面板不受影响。配置创建仍等待正确会话详情后恢复输入焦点，
懒创建、草稿迁移、权限与模型配置事务保持原路径。

验证：

- Windows 最终 A 包 Home `.build/mdo-packed-docks-31ppfo5b`，原会话
  `V-ORkQJJieehH3ki--HM64-55OkuK3bW`。完成一轮真实回复并写入未发送
  草稿后，320×350 从检查器 Ctrl+K 进入可操作新任务，命中与焦点均
  为 prompt、两个抽屉收起。填写新任务草稿，再从检查器 Ctrl+N，文本
  不丢失；重复打开时服务端仍仅原会话目录。手机侧栏用 Enter 选回
  原会话恢复 `Keep original session draft`，再 Ctrl+K 恢复
  `NEW TASK RETAINED DRAFT`，刷新保留后者。截图
  `.build/qa-new-task-drawer-windows-mobile.png`。
- 发送新任务首条后创建 `36a12d21ffac898f091ef962aef2e66d`，实际得到
  回复。960×600 从轨迹 Ctrl+N 先收起检查器，侧栏保留；再次打开
  检查器后配置创建 `V-ORkQtf3HjzQIsJzh82EKSe0LA6lzGQ`，输入框可见、
  获焦，所选 Ling/高推理/只读正确显示。发送后第三个回合实际使用
  high，三个运行均 succeeded/cancel_requested=false。1280×720 从
  轨迹 Ctrl+K 后新任务获焦、两侧常驻面板仍打开；页面错误日志为空。
- Linux 最终 A 包 Home
  `/home/ubuntu/.cache/mdo-linux-qa-new-task-drawer/.build/mdo-packed-docks-rx52hmx2`，
  原会话 `V-ORkR0SgVyFqyyrR34T11sfamfwxxBj`。320×350 从检查器
  Ctrl+N、从侧栏 Ctrl+K 均收起覆盖面板，焦点及中心命中为 prompt；
  原会话和新任务的两份草稿经切换及刷新分别保留，文档宽 320px。
  截图 `.build/qa-new-task-drawer-linux-mobile.png`。新任务首条创建
  `50ebf9a557fbf1740317c3bcef21b048`，完成一轮回复、队列归零。
  960×600 覆盖检查器下配置创建 `V-ORkRhbbLdBma7QduA4zpOOKxxAnrFY`，
  成功后检查器收起、prompt 可见并获焦。页面错误日志为空。
- 两端全门禁通过 114 项 Python、203 项 Node、88 个模块、严格 C11、
  32 个运行探针、Home 租约/队列启动恢复及确定性 A/B；Windows 另通过
  便携 WebView2 Home/20 秒启动。Linux 使用全新 ext4 快照。日志
  `.build/qa-new-task-drawer-release.log`、
  `.build/qa-new-task-drawer-linux-release.log`。
- Windows A/B 及更新后的根目录 SHA-256：
  `646e8cc9fbd6c7ef962270f94f63317df1e8d90967f8fff8670d978eb65689b6`；
  Linux A/B SHA-256：
  `c46d6b9521209286638f79e485750c420138414ae5af38bdf34ddfb9bcd206e1`。

本轮另发现独立缺口：新任务首条发送后 Windows 曾显示空焦点；Linux
在回复完成、prompt 已启用时仍无焦点，等待 3 秒没有回到 prompt。
该证据不是配置创建后的焦点失败，也不是抽屉遮挡；新任务准备阶段会
暂时禁用输入，焦点归属的恢复仍需修复。下一阶段先补此项，不把首条
得到回复当作完整输入链验收通过。

两端夹具正常退出，临时浏览器页关闭。Linux 图形证据仍来自 Windows
浏览器连接 Linux 服务；原生窗口、输入法与实体设备缺口保留。内置
Ling 配置不变，未做压力/高负载测试，长期目标继续。

## 2026-10-01：会话搜索先收起覆盖抽屉

基线 a8f10f6 的单文件 Home `.build/mdo-packed-docks-4p_7eu5v`，会话
`V-ORkLm8IGskTHvXdQBD9_7knUaHz7-g`：完成一轮真实回复后，320×350
从侧栏搜索框按 Ctrl+F，底层会话搜索虽获焦，侧栏和遮罩仍打开；搜索
框中心的命中元素属于侧栏，按 Tab 被抽屉的焦点循环送回。960×600
从轨迹标签按 Ctrl+F 同样留下检查器，搜索框中心命中轨迹标题。
无障碍树中的搜索框 active 并不能证明控件可操作。基线截图
`.build/qa-search-drawer-before-mobile.png` 保留。

搜索模块增加可选打开回调，由应用接入既有 closeDrawers：先收起覆盖
对话的手机侧栏或中等屏检查器，再显示并聚焦搜索。宽桌面常驻面板
仍保留，关闭沿用临时布局路径。再次打开保留查询，Esc 返回输入框，
不改变消息过滤范围、草稿或询问/停止的键盘规则。

验证：

- Windows 最终 A 包 Home `.build/mdo-packed-docks-b4fknpf0`，会话
  `V-ORkMy1ffn2BL9VGY_kv1gkaCUslwJQ`。320×350 从侧栏 Ctrl+F 后
  两抽屉收起，搜索框中心命中自身，输入匹配一条用户消息；Tab 到
  关闭搜索按钮，Esc 回 prompt，未发送文本逐字保留。960×600 从轨迹
  Ctrl+F 收起检查器，重复打开保留查询；1280×720 的两个常驻面板均
  保持打开。截图 `.build/qa-search-drawer-windows-mobile.png`。
- 同一 Windows 会话再触发真实 ask_user，填写自由回答草稿，从手机
  检查器 Ctrl+F 并 Esc。回答草稿保留，焦点回 prompt，运行 API 仍为
  running/cancel_requested=false。随后实际提交回答，两个回合均为
  succeeded、未请求取消，第二轮 tool_calls=1；刷新回放两轮历史并
  恢复后续输入草稿 `Keep final search draft`。脚本错误日志为空。
- Linux 最终 A 包 Home
  `/home/ubuntu/.cache/mdo-linux-qa-search-drawer/.build/mdo-packed-docks-dbfssc2f`，
  会话 `V-ORkO5xYSnp4A1MT8_hZhfa22Bwxn47`。320×350 从侧栏及检查器
  Ctrl+F 均收起覆盖面板并聚焦搜索；命中自身、实际查询、Tab/Esc
  通过。刷新显示完整用户/Agent 回合、输入焦点与
  `Linux retained search draft`；磁盘队列为空，草稿未确认标记为 false，
  文档宽度 320px，页面脚本错误为空。截图
  `.build/qa-search-drawer-linux-mobile.png`。Linux 图形证据仍是 Windows
  浏览器连接 Linux 服务，不代替原生 Linux WebView。
- 既有真实 DOM 输入法夹具 `conversation-search-ime-browser.html` 通过：
  输入法组字及 229 候选键不关闭搜索，第一次 Esc 只关闭搜索，第二次
  Esc 才触发停止。此次没有增加镜像实现的单元测试。
- Windows/Linux 全门禁均通过 114 项 Python、203 项 Node、88 个模块、
  严格 C11、32 个运行探针、Home 租约/队列启动恢复及确定性 A/B；
  Windows 另通过便携 WebView2 Home 和 20 秒启动。日志
  `.build/qa-search-drawer-release.log`、
  `.build/qa-search-drawer-linux-release.log`。Linux 使用全新 ext4 快照，
  首次准备的 mkdir 命令无法执行时门禁尚未启动；改用 Python 文件
  API 完成快照准备，没有削减门禁或放宽原子文件系统断言。
- Windows A/B 及更新后的根目录 SHA-256：
  `790ddae18eac007071a5245ba917fd3956537d0d66873ba9cede0f5a425fc309`；
  Linux A/B SHA-256：
  `c6ad9d4457a4bf654ecda9d5c49af1a0e34dba347f52dc90a2305bc3c06fa409`。

两端夹具已正常退出，浏览器临时页关闭。原生窗口、系统输入法与实体
设备的验收缺口保留。内置 Ling 配置不变，未做压力或高负载测试，
长期目标继续。

## 2026-10-01：引导模式的排队与中断顺序补验

本轮只补实际验收，没有改变程序行为。载入阶段曾显示权限选项禁用；
待输入启用后核对 DOM，选择器及三个选项均未禁用。通过原生选择器
切到只读，收到“会话配置已更新”，磁盘元数据保存 read-only。不能把
初始无障碍快照的禁用状态当作稳定缺陷。

Windows 单文件 Home `.build/mdo-packed-docks-86c1u2xg`，会话
`V-ORkIB5ycJxZVVcqdrO1jYT89rts8fv`；Linux 单文件 Home
`/home/ubuntu/.cache/mdo-linux-qa-settings-invalid-panel-final/.build/mdo-packed-docks-8z8ok7hp`，
会话 `V-ORkJOjmsjUS4B3Vhq8JIhNMzJH132R`。两端通过常规设置预览、
应用引导模式，再返回原会话，执行相同有界流程：

1. 输入区选择高推理、只读，Enter 发送带 SLOW UI 标记的首轮；本地
   模型延迟上限 12 秒。收到运行中状态后仍能切换输入区配置。
2. 切无思考，Ctrl+Enter 提交普通待发项；卡片明确冻结无思考和只读。
3. 切低推理，Enter 提交优先项；普通项的冻结配置仍为无思考，不被
   此后输入区的选择覆盖。
4. 真实运行列表依次为 agent_run_id 1/high/cancelled/cancel_requested、
   2/low/succeeded、3/none/succeeded。持久日志的用户正文依次为首轮、
   Enter 优先项、Ctrl+Enter 普通项；队列及未确认提交均为空。
5. 刷新回放保留“已停止”、优先回复和普通回复，输入框为空并聚焦。
   输入区保留最后手动选择的低推理及“下次任务生效”，草稿保存该
   后续配置；末个队列回合使用无思考，这是两种不同的归属。

Windows 桌面截图 `.build/qa-guide-mode-windows.png`；Linux 320×350
截图 `.build/qa-guide-mode-linux-short.png`，文档宽度 320px。两端新页
脚本错误日志为空。设置返回时队列读取尚在途，配置可能先存为后续
草稿而非立即修改会话元数据，属既有保护；首次提交仍使用该选择。
本轮不把没有出现直接更新 toast 的情况当作更新失败。

两端重新打包通过，SHA-256 与上一轮完整门禁的 A/B 包逐字节相同：
Windows 及根目录为
`6e13407237a1494ef7a40345f77cf7cbbcec4fc8afe4d87856ad3fdf3c5b075f`，
Linux 为
`71396c7c52b5037a33034016f437a9a31c9b7699a29070d2a93690f7ffce6cfa`。
构建日志 `.build/qa-guide-mode-build.log`、
`.build/qa-guide-mode-linux-build.log`。生产源码及测试未修改，完整门禁
沿用上一轮的 114 Python、203 Node、88 模块、严格 C11、32 运行探针和
便携窗口/20 秒启动结果，没有把本轮重打包说成重新运行全门禁。

两端夹具正常关闭。Linux 图形证据仍是 Windows 浏览器连接 Linux 服务，
不代替原生 WebView、Command 键、输入法或实体手机。内置 Ling 配置不变，
未做压力/高负载测试，长期目标继续。

## 2026-10-01：跨设置分区校验定位到错误字段

旧包 Home `.build/mdo-packed-docks-c90dhtgx`（基线 7bb103c）复现：
Agent 并行工具数填 0 后切到常规，点击预览仍留在常规和预览按钮，提示
仍为“有尚未预览的更改”，错误字段隐藏。原生整表 reportValidity 无法
把隐藏分区中的字段显示给用户，操作看起来没有反应。

预览和应用现在共同查找首个参与原生校验的无效字段，先显示其设置分区，
保留全部未应用输入，再给出三语提示、聚焦字段并调用该字段的原生校验。
移动端沿用已有可见区域滚动，错误字段不会落到固定分类栏或操作栏下。
不会跳过其他分区的校验，也不把无效值发给 API；预览、ETag 和服务端
合并事务保持不变。三语词典各增加一键，现各 1346 键。

验证：

- 新夹具 `tests/fixtures/settings-invalid-panel-browser.html` 使用完整
  正式表单、约束、CSS 和设置模块。桌面及 320×350 实际操作验证数值
  下限、必填空值、多个错误依次定位，以及英语/俄语 URL 提示。字段
  可见并获焦、未应用系统指令逐字保留、文档无溢出，错误输入预览请求
  数为 0；全部修正后恰发一次预览，页面无脚本错误。
- Windows 最终单文件 Home `.build/mdo-packed-docks-bum78aka`，会话
  `V-ORkGCJjdXmFVR3a27JbrAE9iBOD-yR`。320×350 下并行数 0 自动切回
  Agent，字段 y=139.453～179.453、焦点正确、revision 保持 1。联网
  超时 100 和无效 URL 依次在英语、俄语界面被定位，系统指令仍为
  `Windows settings preservation QA`。修正后预览、应用使 revision=2；
  刷新保留俄语、工具数 8、超时 45000、合成搜索 URL 和指令。另一次
  设置往返等待输入启用后确认原会话路由、未发送草稿及 prompt 焦点，
  无脚本错误。截图 `.build/qa-settings-invalid-panel-windows-short.png`。
- Linux 最终单文件 Home
  `/home/ubuntu/.cache/mdo-linux-qa-settings-invalid-panel-final/.build/mdo-packed-docks-qun4p73z`，
  会话 `V-ORkGZKi1QukLL2EJtFeUyRf5J1SGdF`。320×350 的并行数 65 自动
  切回 Agent，原生提示上限 64；搜索 URL 为空时从英语常规切到联网
  字段。修正后预览/应用及刷新保存工具数 6、合成 URL 和指令
  `Linux settings preservation QA`；等待语言包载入后英语 revision 2
  与提示正常，页面宽度 320px、错误日志为空。截图
  `.build/qa-settings-invalid-panel-linux-short.png`。两端停服后核对
  `mdo-home/config/settings.json`，只有上述有效差量，没有错误值。
- 首轮两端门禁因旧源码契约硬编码 form.reportValidity 调用而失败；
  更新为字段的原生 reportValidity 断言，操作行为由浏览器验证。初次
  日志保留在 `.build/qa-settings-invalid-panel-linux-release-initial.log`
  及 `.build/qa-settings-invalid-panel-release-initial.log`。
  最终 Windows/Linux 全门禁通过 114 项 Python、203 项 Node、88 个模块、
  严格 C11、32 个运行探针及确定性 A/B 打包；Windows 另通过便携
  WebView2/20 秒启动。最终日志
  `.build/qa-settings-invalid-panel-release.log`、
  `.build/qa-settings-invalid-panel-linux-release.log`。
- Windows A/B 及根目录 SHA-256：
  `6e13407237a1494ef7a40345f77cf7cbbcec4fc8afe4d87856ad3fdf3c5b075f`；
  Linux A/B SHA-256：
  `71396c7c52b5037a33034016f437a9a31c9b7699a29070d2a93690f7ffce6cfa`。

Linux 图形证据来自 Windows 浏览器访问 Linux 服务，不代替原生 WebView
或实体设备。内置 Ling 配置不变，未做压力/高负载测试，长期目标继续。

## 2026-10-01：停靠卡片为历史消息保留滚动空间

跨平台复核发现一个实际操作缺陷：320×350 下，即使收起计划并滚到历史
末尾，悬浮停靠卡片仍遮住最后的消息按钮。定位器报告点击成功却没有
复制提示或反馈变化，不能视作通过。Linux 旧包中用户复制按钮的中心
y=119.109 落在卡片覆盖区；不是剪贴板或会话接口错误。

现在复用停靠区的 ResizeObserver，计算其对历史区的实际覆盖高度，并
通过既有时间线尾部 sentinel 保留滚动空间；scroll-padding 使键盘聚焦
能够避开覆盖区，回到底部入口随覆盖高度上移。只在原本紧贴末尾时继续
跟随；阅读历史时保持位置，卡片隐藏及模块销毁时清理预留。没有缩小
历史内容盒，也没有新增观察器或协议。极短屏展开计划时仍可先收起计划
阅读历史，不承诺在同一屏同时显示完整历史、计划和输入区。

验证：

- 新增正式 CSS/模块浏览器夹具
  `tests/fixtures/conversation-history-overlap-browser.html`。同一夹具配
  基线 5542cc1 的两份文件先复现 actionFits=false：按钮 y=121.125～
  161.125，可见历史下缘 138.406。修复后同样收起计划、滚到末尾，按钮
  y=53.125～93.125，预留 68px，actionFits=true；实际点击计数为 1。
- 阅读位置测试在计划变化前后保持 scrollTop=80；隐藏计划预留归零，
  恢复并收起后为 68px。390×600 展开计划及桌面也可到达末条按钮，草稿
  保持、文档无溢出、新页面无脚本错误。既有软件键盘询问夹具在 390×600
  重新载入后通过提交、展开/收起及焦点检查。
- 最终 Windows 单文件 Home `.build/mdo-packed-docks-a5jiwe4s`，源会话
  `V-ORkBBhuCCWFpDuuYvlg4OSChNhGlI4`、分支
  `V-ORkBbmJUMv2UC4slR5UbZtCQK0qO7M`。实际生成待办及两轮回复，在
  320×350 收起计划后点击用户复制，浏览器剪贴板逐字为
  `WINDOWS DOCK FINAL MESSAGE`；末条点赞刷新保持，末条分叉立即创建
  两轮分支并聚焦输入，源会话不变。页面宽度 320px、预留 68px、错误
  日志为空，截图 `.build/qa-history-overlap-windows-short.png`。
- 最终 Linux 原生 ext4 单文件 Home
  `/home/ubuntu/.cache/mdo-linux-qa-conversation-history-overlap/.build/mdo-packed-docks-cxasq0j9`，
  源会话 `V-ORkC0ih9XS5XGM0CYtgAGSSVN8DwDU`、分支
  `V-ORkCzmyAoTOeGzy_2OqEZyaMR3kWNl`。同样以真实待办和两轮回复验证
  320×350 复制原文 `LINUX DOCK FINAL MESSAGE`、点赞刷新保持、末条
  分叉两轮历史与输入焦点。再复制末条助手成功，按钮 y=65.125～105.125
  小于卡片上缘 119；截图 `.build/qa-history-overlap-linux-short.png`。
  文档宽度 320px、预留 68px、错误日志为空。两端正常停服后读磁盘日志，
  源/分支各有对应两条用户正文，源反馈均为 event_id=13、value=good。
- 本轮修复前另在 Linux 旧包 Home
  `/home/ubuntu/.cache/mdo-linux-qa-timeline-history-locale/.build/mdo-packed-docks-9cn07ry7`
  完成五轮有界运行：12 秒慢回复、待办、自由询问、询问期间排队并自动
  续发、一次审批执行无害 Python 命令（exit_code=0）。询问草稿在排队
  更新时保持，Enter 提交后聚焦输入；刷新回放四轮历史及待办，审批后
  结果持久化。`@QA` 补全带空格路径后实际 Ctrl+Z/Ctrl+Shift+Z 撤销/
  重做并刷新保留草稿。这些补足 Linux 服务及浏览器交互证据，旧包的
  复制/点赞因覆盖没有通过，不能与最终修复的证据混为一谈。
- Windows/Linux 新原生文件系统全门禁通过 114 项 Python、203 项 Node、
  88 个模块、严格 C11、32 个运行探针及确定性 A/B 打包；Windows 另通过
  便携 WebView2/20 秒启动。日志为
  `.build/qa-conversation-history-overlap-release.log`、
  `.build/qa-conversation-history-overlap-linux-release.log`；Linux 快照为
  `/home/ubuntu/.cache/mdo-linux-qa-conversation-history-overlap`。
- Windows A/B 及根目录 SHA-256：
  `25ec0204fa75477d3ec733e224e3f1285476041e89344ffc4768b6579faadf95`；
  Linux A/B SHA-256：
  `6ed23c2b2fc23987fb767b2a603c0d9d2443d4a3c6c11db449f8fda4e494237a`。

Linux 页面由 Windows 浏览器连接真实 Linux 服务，不能替代 Linux 原生
WebView、系统剪贴板、输入法及实体触控验收。未增加词典键（各 1345 键），
未做压力/高负载测试，内置 Ling 生产配置不变，长期目标继续。

## 2026-10-01：历史截断与清空提示随界面语言显示

上一轮俄语工作台的历史边界仍显示中文。服务端持久化日志使用同一种
history_truncated 事件表示截断和清空，以两条固定文案区分操作；投影原先
优先显示 event.text，已有译文因此没有使用。本阶段仅在该事件类型中
映射服务端的两条原始固定提示，空说明仍按既有截断回退；未知/导入说明
保持原文。用户、模型、工具、错误和未知事件中的相同中文不会被替换。
不改变日志格式、服务端 API 或事件正文，三语词典各 1345 键。

验证：

- 新增三项 Node 回归覆盖三语切换与再次回切、截断/清空/缺省提示、原事件
  不变、未知说明与普通正文隔离，以及显式边界不重复显示缺口通知。
- 浏览器组件页先复现英语角色已翻译但两条边界仍为中文。修复后桌面及
  320×350 三语 translated/originals 均为 true，未发送草稿保持，无溢出。
  初次夹具漏传 feedbackStore，补齐后才完成复现；新标签脚本错误为空。
  QA 模块查询参数和语言包 no-store 避免沿用测试期间的旧缓存，正式页面
  不依赖这些控制。
- 最终单文件 Home `.build/mdo-packed-docks-ucv4d8mq`，会话
  `V-ORk2zRgsU2elmWw0YhLzG_TW1rKjs4`。普通发送后，通过实际编辑/Enter
  重发生成历史边界；新用户正文含“会话历史已截断”和第二行 QA 标记。
  英语只翻译系统行；常规设置预览/应用英语、俄语（revision 1→2→3），
  每次返回原会话并刷新，边界、角色和用量按对应语言显示，用户正文逐字
  保留。俄语 320×350 页面宽度 320px，历史提示和输入操作可见；桌面与
  短屏截图为 `.build/qa-timeline-history-locale-packed-ru.png`、
  `.build/qa-timeline-history-locale-packed-ru-short.png`，脚本错误日志为空。
  共 1 次队列 POST、2 次运行 POST。磁盘日志核对确认历史提示及用户原文
  仍为原始 UTF-8；临时页面和服务正常关闭。
- 清空提示的显示由正式模块组件及 Node 用例验证；本轮没有在工作台执行
  清空操作。两端完整门禁包含既有真实 HTTP 清空/截断日志回归。
- Windows/Linux 新原生文件系统全门禁通过 114 项 Python、203 项 Node、
  88 个模块、严格 C11、32 个运行探针及确定性 A/B 打包；Windows 另通过
  便携 WebView2/20 秒启动。日志为
  `.build/qa-timeline-history-locale-release.log`、
  `.build/qa-timeline-history-locale-linux-release.log`；Linux 快照为
  `/home/ubuntu/.cache/mdo-linux-qa-timeline-history-locale`。
- Windows A/B 及根目录 SHA-256：
  `e4871318f9846e9dfe2e94197ee6596b01177a58113fff0b98ea201ffe2b8d00`；
  Linux A/B SHA-256：
  `16c608c34240d6c52fca9096d2c70735c9d091c5962b15b3943ccb174848cb21`。

本项关闭上一轮确认的历史边界语言缺口，不代表其他服务端错误和说明已
全部本地化。内置 Ling 生产配置不变；未做压力/高负载测试，原生 GUI、
输入法及实体设备验收缺口保留，长期目标继续。

## 2026-10-01：消息编辑恢复 Enter 保存重发

旧版 `chrome.js` 的 promptModal 以 Enter 确认消息编辑，新版 textarea 只
能通过按钮确认。真实 DOM 夹具先复现 Enter 仅插入换行、弹窗仍打开；
现在无修饰键的 Enter 走与按钮相同的 requestSubmit 和验证，Shift+Enter
保留换行。共享输入法跟踪阻止候选确认、keyCode 229 和组合输入触发重发，
输入框或窗口失焦释放组合状态。禁用、只读及已处理的按键不再次提交。

编辑框关联可访问的键盘提示，中英俄词典各 1344 键。提示属于可滚动内容
区，按钮保持可操作；中文/英文短屏可直接看到提示，俄语长标题和说明在
320×350 需要滚动查看。调整提示段落默认边距，避免额外空白遮挡。

验证：

- 新增五项 Node 用例覆盖单次提交及焦点、修饰键、输入法及失焦释放、
  空白验证/纯图片编辑，以及禁用/只读/已处理按键。正式模块浏览器夹具
  验证实际 Enter 和 Shift+Enter，三种合成输入法候选均被阻止，结束组合
  后可提交原文并恢复焦点。
- 首次重载夹具仍读到浏览器缓存中的旧模块；检查导出的函数确定 Enter
  处理器未加载。仅给 QA 夹具模块 URL 增加查询参数后，加载新实现并通过；
  正式模块及路由不依赖这个参数。
- 初版包隔离 Home `.build/mdo-packed-docks-ktzm13ch`，会话
  `V-ORjvfRzHbPeCRlwiBhB8P_RoaS4wG_`。两次普通运行后，桌面 Enter 编辑
  重发第二条，刷新保留第一条及新多行文本、移除原第二条；320×250 验证
  空白 Enter 留在原弹窗并提示，修正后仍可重发并刷新。共 2 次队列 POST、
  4 次运行 POST，未因键盘提交重复运行。
- 最终包隔离 Home `.build/mdo-packed-docks-g6hgk53d`，会话
  `V-ORjxubt3sEDOkphcGREWdxBuUlqehP`。正式工作台实际 Shift+Enter 换行、
  Enter 将历史消息改为 `EDIT ENTER FINAL UPDATED UI` 加“最终包第二行”；
  1 次队列 POST、2 次运行 POST。中文/英语提示保留输入；通过设置预览/
  应用事务保存俄语（revision 2），返回原会话，滚动显示完整俄语提示，
  取消恢复对应编辑按钮焦点。刷新仍为俄语且保留新多行历史，320px 无横向
  溢出，浏览器错误日志为空。最终包组件另通过三种合成候选保护。
- 截图 `.build/qa-message-edit-enter-packed-final.png`、
  `.build/qa-message-edit-enter-packed-ru.png`。临时页面与服务均正常关闭。
- 最终 Windows/Linux 原生文件系统全门禁均通过 114 项 Python、200 项
  Node、88 个前端模块、严格 C11、32 个运行探针及确定性 A/B 打包；Windows
  另通过便携 WebView2/20 秒启动。日志为
  `.build/qa-message-edit-enter-release-final.log` 和
  `.build/qa-message-edit-enter-linux-release-final.log`；Linux 快照为
  `/home/ubuntu/.cache/mdo-linux-qa-message-edit-enter-final`。
  提示边距调整前的两端通过日志也保留，最终验收使用调整后的完整结果。
- Windows A/B 及根目录 SHA-256：
  `752e88b88563b86f928e561969a9b6cc01d2d8ffde95f205c86891114e69ac8f`；
  Linux A/B SHA-256：
  `b92da9aaf3704168e0ee49d2cc6f983eae12e729db17475c97de269483ab8e40`。

俄语工作台的历史边界仍显示服务端中文“会话历史已截断”，已确认是下一项
语言缺口。内置 Ling 生产配置不变，未做压力/高负载测试；合成输入法事件
和浏览器视口不代表原生输入法或实体设备通过，其他原生 GUI 缺口保留。
长期目标继续。

## 2026-10-01：图片预览关闭与重绘保持状态

真实 DOM 夹具稳定复现旧实现的三项缺陷：名称请求完成前原缩略图被重绘，
已打开预览无法补上名称；同一事件任务中关闭第一张再打开第二张，迟到的
原生 close 事件清空新预览；导航关闭预览后，旧 close 回调抢走新输入焦点。
旧夹具的 nameAfterRerender、reopened、navigationFocus 均为 false。

现在预览捕获缩略图已绑定的会话范围名称 Promise，独立等待名称，只有
仍属同一次打开才更新。WeakMap 不延长缩略图生命周期，没有额外元数据
请求，也不再使用 MutationObserver。关闭同步清理当前预览，迟到 close
事件不清理已重开的对话框；普通关闭和 Esc 恢复原图或其重绘替代按钮的
焦点，导航关闭不再异步抢焦点。外部直接调用原生 dialog.close 仍可清理。

验证：

- 两项新增 Node 用例覆盖原图脱离后的名称完成，以及重用节点时新旧预览
  各自持有名称读取。既有七项名称用例继续通过。
- 新 `image-preview-lifecycle-browser.html` 直接使用正式模块，桌面与
  320×350 验证重绘后名称、替代缩略图焦点、快速重开、导航焦点、外部
  关闭、旧名称隔离、当前名称完成，全部通过，无溢出或夹具脚本错误。
  QA 代理另提供同一夹具从最终包读取模块的路由，两种视口同样通过。
- 既有 `image-names-browser.html` 桌面与短屏继续通过迟到名称、单次共享
  读取、队列选择和卡片保持、三语焦点；短屏移除按钮宽 40px。既有软件
  键盘预览夹具验证三种可视视口位置、恢复和关闭焦点，全部通过。
- 最终包隔离 Home `.build/mdo-packed-docks-lcfrhe3l`，会话
  `V-ORjoxf_TlYO_iaV_Isfmh3MM8MIvQW`。合成粘贴通过正式上传与草稿保存
  两张 PNG；QA 控制在同一任务内关闭/重开，桌面和 320×350 均保留第二张
  名称及关闭按钮焦点。控制首次失败是点击了重绘后脱离的第二个节点；
  诊断证实打开预览时两旧节点已脱离，改为定位当前同 ID 卡片后通过。
  正式输入区、上传和预览模块未为控制替换实现。
- 普通打包页面恢复两图和未发送文字；Esc 清空预览资源、回到正确缩略图，
  草稿保持。一次实际图文运行完成后刷新恢复两图和名称；历史预览关闭后
  可继续编辑，320×350 无溢出。截图
  `.build/qa-image-preview-lifecycle-packed.png` 和
  `.build/qa-image-preview-lifecycle-workbench.png`。
- 复用标签累计两次 MutationObserver 非 Node 参数错误；其来源仍未确认，
  本轮没有复现原报告的精确堆栈，不能将三项稳定缺陷视作该错误的根因。
  当前正式前端已无 MutationObserver 调用；新标签桌面和短屏实际历史
  预览/Esc/草稿验证的错误日志为空。保留这项未确认现象，不宣称所有
  原生窗口已验收。
- Windows 与新的 Linux 原生文件系统快照完整门禁均通过 114 项 Python、
  195 项 Node、88 个前端模块、严格 C11、32 个运行探针及确定性 A/B 打包；
  Windows 另通过便携 WebView2/20 秒启动。日志为
  `.build/qa-image-preview-lifecycle-release.log` 和
  `.build/qa-image-preview-lifecycle-linux-release.log`；Linux 快照为
  `/home/ubuntu/.cache/mdo-linux-qa-image-preview-lifecycle`。
- Windows A/B 及根目录 SHA-256：
  `67b51533081d4977bf20fd6b108ba6fd28c809b97667be8ff7939f353418258b`；
  Linux A/B SHA-256：
  `e01c2e576b3337bdb6d3908c56c24e24ffe167b33d00a1e56f5e4985a0d1d5a1`。

内置 Ling 生产配置未改，图片能力只在隔离 QA 开启。未做压力/高负载测试；
合成粘贴、浏览器 Esc 与视口夹具不代表系统原生拖放、输入法或实体设备
通过。原生 GUI/其他系统等缺口保留，长期目标继续。

## 2026-10-01：附件原始文件名贯穿草稿、队列、历史与分叉

对照旧版 `attach-name`，新版附件只有图片序号，上传元数据没有保存名称。
本阶段补齐持久化和显示链路，布局与既有添加、移除、发送、预览操作保持。
名称通过百分号编码请求头随原二进制上传；具名附件使用 v2 JSON，旧 v1
继续读取。名称只作显示，最多 1024 UTF-8 字节；JSON 读取和分叉复制共同
使用 4096 字节上限。新增会话范围的只读元数据路由，见
[图片附件约束](image-attachment-implementation.md#原始文件名与元数据2026-10-01)。

草稿、队列和用户历史均显示名称，长名称省略、完整值保留在提示及预览；
预览/移除标签含序号和文件名，三语切换保留焦点。名称缓存限定 128 项，
按项目/会话/附件隔离并共享请求；失败退避，旧记录保留通用标签。已有
缓存同步参与卡片比较，避免队列轮询丢失文字选择。迟到响应不修改脱离
文档或引用已变的缩略图；已打开预览随其原图的迟到名称更新。

验证：

- 新增七项 Node 用例验证 UTF-8 字节边界、控制/路径/坏 UTF-16 拒绝、
  上传头及原二进制保持、缓存范围/请求合并/旧元数据/退避/淘汰、字面
  文本及延迟引用隔离；稳定错误码补三语回归。
- 扩展真实 HTTP/xs/TCC 图片探针，覆盖坏编码、无效 UTF-8、重复请求头、
  超长名称、无新增原件、v1/v2 GET/HEAD/OPTIONS、损坏元数据拒绝、重启、
  超过 512 字节的合法名称分叉，以及源原件移除后子会话仍可独立读取。
  Windows 和 Linux 完整门禁均包含该探针。
- `image-names-browser.html` 在桌面及 320×350 全部通过：一次名称请求
  共享给草稿/队列，先打开通用名称预览后补上迟到名称，关闭回到原按钮；
  队列轮询保留原卡片和文字选择，英语/俄语重绘保留移除按钮焦点，草稿
  保持、无横向溢出、短屏移除按钮 40px。既有附件可访问性及软件键盘
  预览夹具仍通过。
- 最终包隔离 Home `.build/mdo-packed-docks-rfw4nplh`，源会话
  `V-ORjhXbD6aGZVDPaPz0lm7i5vcBXnLc`，通过合成粘贴与正式 API 保存
  `截图 "1" 100%.png` 和 204 字符的长中文名称。两个原始侧车为 160、
  743 字节；普通页面刷新保留文本、两图和名称，图片解码成功，320×350
  无横向溢出。短屏预览关闭恢复缩略图焦点，截图
  `.build/qa-attachment-names-packed.png`。
- 仅 QA 代理增加“发送并排队图片任务”控制，调用未修改工作台的输入和
  发送逻辑；正式队列两个标签逐字核对完整文件名，页面控制返回
  “正式待发队列已保存两个完整文件名（15 / 204 字符）”。证据截图
  `.build/qa-attachment-names-queue.png`。真实图文运行完成后草稿清空；
  历史刷新恢复具名图片。整次隔离页面验证共 5 次 queue POST、5 次 run
  POST，结束时队列为空、六个历史图片标签恢复完整名称；206 次 queue
  GET 来自有限验证期间的正常页面轮询，验证结束已关闭页面与服务。
- 从首轮回复实际分叉到 `V-ORjjJAyar721k9KQSJdHjgpR1Gb26b`，保留两图
  和两个名称；两个 `.bin` 和 `.json` 与源逐字节一致，包括 743 字节
  侧车。新页面 320×350 长名预览关闭回到该历史缩略图，可继续编辑而不
  改变历史，最终新页面错误日志为空；截图
  `.build/qa-attachment-names-fork.png`。此前复用标签累计出现一次
  MutationObserver 非 Node 参数错误，尚未确认来源；新页面正式预览及
  两种组件预览均未复现，未据此宣称所有原生窗口已验收。
- 首次 Windows 全门禁在 Home 导入崩溃恢复探针返回 `import_init=0`。
  该探针单独复验和完整门禁重跑均通过；未确认首次失败原因，保留原始
  `.build/qa-attachment-names-release.log` 与
  `.build/qa-attachment-names-home-recheck.log`。
- Windows 与新的 Linux 原生文件系统快照完整门禁均通过 114 项 Python、
  193 项 Node、88 个前端模块、严格 C11、32 个运行探针及确定性 A/B 打包；
  Windows 另通过便携 WebView2/20 秒启动。日志为
  `.build/qa-attachment-names-release-recheck.log` 和
  `.build/qa-attachment-names-linux-release.log`，Linux 快照为
  `/home/ubuntu/.cache/mdo-linux-qa-attachment-names`。
- Windows A/B 及根目录 SHA-256：
  `52ea587c830940cfe0444812a2462ce8b166b77326e51bfab3a611fa896cf85d`；
  Linux A/B SHA-256：
  `0964acc6fc3b44c2681b102323d335b9c18d73bfde0d25c514d8ad92e92e77e1`。

内置免费 Ling 的生产配置未改，图片能力与固定回复只用于隔离 QA 配置。
未做压力/高负载测试；合成粘贴不代表原生文件拖放或实体设备通过，其他
原生 GUI/平台缺口保留，长期目标继续。

## 2026-10-01：多图粘贴保留完整文件列表

粘贴和拖放现在共用文件读取函数。原粘贴逻辑只要从 clipboard.items 读出
一张图片，就不再检查 files。组件页复现：完整 FileList 有 first.png 和
second.png，但第二个 item 的 getAsFile 返回 null 时，只上传了第一张。

现在以可用 FileList 的完整顺序为准，items 只补充未声明的 MIME；没有
FileList 时再读取 items。两种视图同时存在不会重复上传，也不按文件名、
大小等元数据合并用户分别选择的文件。显式 MIME 仍优先于扩展名及 item
提示；仅有 getAsFile 的既有剪贴板视图继续兼容。粘贴过滤图片，普通文本
继续由浏览器插入；拖放保留非图片条目以显示原有类型反馈。

验证：

- 新增五项 Node 用例覆盖部分可读的 items、两视图去重、相同文件元数据、
  文件及 item MIME、items-only 和纯文本。两个既有文件 MIME 用例仍通过。
- `composer-transfer-files-browser.html` 先在旧实现实际显示 passed:false、
  uploads:1、images:1；新实现上传 first.png 和 second.png 各一次，草稿
  保持，错误为空，320×350 文档宽度等于视口。
- 现有 `composer-upload-session-browser.html` 十个用例全部通过：跨会话
  上传隔离、新任务上传锁、空 MIME、仅 items、仅 files、双视图、拖放、
  无可读文件以及模型兼容切换。该证据来自浏览器组件，不代表系统原生
  文件拖放已验收。
- 新的 `--image-transfer-fixture` 仅在隔离 QA 代理提供控制页；iframe 加载
  未修改的单文件工作台，控制页生成第二个 item 不可读的合成粘贴事件。
  图片上传、草稿保存及图文运行均走正式生产模块和真实本地 API，没有
  模拟上传响应。`--image-capable` 只给隔离测试配置启用图片，本次未改
  产品内置 Ling 3.0 Tiny 的配置。
- 最终单文件 Home `.build/mdo-packed-docks-we0zy1we` 实际添加两张有效
  PNG；磁盘 draft revision 1 保存原文“保留这段草稿，并检查两张图片”和
  两个不同附件 ID。打开普通打包页后，320×350 仍有两张图片和原草稿，
  移除按钮均为 40×40px，两张图片都完成解码。390×600 截图为
  `.build/qa-image-transfer-packed.png`。
- 实际粘贴纯文本“，继续正文”保持两张图片，再发送图文任务，正常回复、
  输入焦点回到 prompt、草稿及待发附件清空。320×350 刷新回放仍显示两张
  已解码的历史图片和原回复，文档无横向溢出，脚本错误为空。结束截图为
  `.build/qa-image-transfer-packed-complete.png`。包内 composer-images.js
  与源码逐字节一致。
- Windows 完整有界门禁通过 114 项 Python、186 项 Node、87 个模块、
  严格 C11、32 个运行探针、确定性 A/B 打包及便携窗口/20 秒启动。Linux
  本轮只复验 87 个前端模块及 186 项 Node 用例，没有宣称完整运行门禁或
  新 Linux A/B 包通过；C 盘可用空间本轮曾降至约 267MB，结合上一阶段
  WSL 只读/I/O 故障，本次没有再创建完整原生发布快照。没有压力或高负载
  测试，原生系统剪贴板多文件、文件拖放和实体设备仍待验收。

根目录 `mdo.exe` 使用已验证的 Windows 包，SHA-256 为
`e137aca4fc0e0db62a92c7e1e25e780808e9808da183f916e9a8e4a2470fa7d7`。
日志为 `.build/qa-image-transfer-release.log`、
`.build/qa-image-transfer-linux-frontend.log`。可用
`python tests/manual_packed_docks_qa.py --packed-path mdo.exe --image-capable --image-transfer-fixture`
启动新隔离 Home，打开 READY 地址同源的 `/__qa/image-transfer`，在工作台
输入草稿，再点击“粘贴两张图片”重放。候选事件由夹具合成，不能把这条
链路写作原生操作系统剪贴板或实体手机已通过。长期目标仍未完成。

## 2026-10-01：询问 Escape 与快捷键遵守输入法状态

询问卡展开时，原来的 Escape 处理没有检查中文候选状态，会提前收起卡片。
全局快捷键使用单个 composition 布尔值；输入框被移除或未发出
compositionend 就失焦时，其他输入框的新任务快捷键也会被持续拦截。

现在公共输入法跟踪器用 WeakSet 记录实际编辑目标，在捕获阶段监听
compositionstart、compositionend 和不冒泡的 blur。输入框失焦只释放自己的
状态，窗口失焦释放全部状态，组件销毁移除监听。询问卡与全局快捷键都结合
该目标的状态、事件 isComposing 和 keyCode 229 判断候选按键。普通 Escape
仍先收起展开的决策，不改变既有停止运行或关闭菜单的操作顺序。

验证：

- 新增六项 Node 用例，覆盖不同输入框、未结束输入、输入框/窗口失焦和
  监听销毁；其中两个全局快捷键用例先在原实现复现失败。捕获监听的移除
  使用明确的 capture 选项，两端测试运行时保持同一语义。
- `decision-escape-ime-browser.html` 原实现在真实浏览器中出现四项失败：
  候选跟踪、isComposing、229 的 Escape 均提前收起，移除编辑器后 Ctrl+K
  不响应。新实现五项检查全部通过，普通 Escape 收起而不触发停止；320×350
  复验通过，文档宽度为 320px。
- 已有询问输入法 Enter 夹具仍只提交一次“你好”；会话搜索候选 Escape
  不关闭搜索，随后普通 Escape 依次关闭搜索、停止运行。上述候选事件由
  夹具合成，没有将它们写作操作系统原生中文输入法或实体手机验收。
- 最终单文件 Home `.build/mdo-packed-docks-5gkbkqwh` 经本机固定模型返回
  长询问。在 320×250 页面输入“先保留草稿，再检查本地结果”，展开后实际
  按 Escape，卡片收起、原回答与焦点保持、运行仍在等待。再次展开并按
  Enter 正常提交，工具结果包含原回答，询问卡消失，焦点回到 `prompt`。
  390×600 页面显示最终回复和消息操作，脚本错误为空，文档宽度等于视口。
  截图为 `.build/qa-decision-ime-packed.png`。从该包读取三个修改的模块，
  与源码字节一致。
- Windows 完整有界门禁和 Linux 原生文件系统复验均通过 114 项 Python、
  181 项 Node、87 个模块解析、严格 C11、32 个运行探针及确定性 A/B 打包；
  Windows 另通过便携 WebView2 Home 和 20 秒启动。Windows 日志为
  `.build/qa-decision-ime-release.log`，没有压力或高负载测试。

根目录 `mdo.exe` 使用已验证的 Windows 包，SHA-256 为
`149b25071342fb56a6fb1582ca5eda21ca8b3a969546706058a93caf8d341508`。

本轮首次 Linux 门禁未通过：新原生快照
`/home/ubuntu/.cache/mdo-linux-qa-decision-ime` 已完成单元/前端检查及部分运行
探针，在项目清除意图探针中遭遇只读文件系统；随后 `findmnt`、`df`、
`dmesg` 程序均报 I/O 错误。Windows 检查时 C 盘仅余 18,239,488 字节，
Ubuntu 的 `ext4.vhdx` 位于 C 盘，文件大小 50,931,433,472 字节。磁盘空间
与 WSL 故障同时出现，尚未取得内核日志确认因果。失败日志保留在
`.build/qa-decision-ime-linux-native-release.log`。失败后停止该快照的写入，
没有重置 WSL 或删除其数据。

随后只读复核显示 C 盘可用空间回到约 3.7GB，WSL 根目录为 rw。在全新的
`/home/ubuntu/.cache/mdo-linux-qa-decision-ime-recheck` 快照中单独重跑完整
门禁，全部通过；Linux A/B 包 SHA-256 为
`e069eea04773b24105c0e829c266c14ecfb5ae1c71c8bccf0a60d74afe892b66`，
日志为 `.build/qa-decision-ime-linux-native-recheck-release.log`。没有把部分
通过或环境自动恢复当成完整复验，也没有把该结果外推到 `/mnt/d` 的原子
文件系统能力或 Linux 原生 GUI。原生窗口完整点击、系统文件拖放、其他
图形平台及实体设备的缺口仍保留，长期目标未完成。

## 2026-10-01：文件补全进入输入撤销记录

文件补全原先用 `setRangeText` 修改 textarea。实际键盘验证表明，补全后
按 Ctrl+Z 会收到 `historyUndo`，文字却不回退。现在公共文本编辑模块优先
使用浏览器编辑命令，使完整引用成为一次可撤销的编辑。已有空格或换行
一并放入该次编辑，保留原分隔符及后续文字，重做后的光标仍在分隔符之后。
浏览器不支持命令时保留既有 `setRangeText` 回退，不新增工具链依赖。

公共模块只补发未由原生编辑发出的 input 通知，避免重复保存草稿与估算
token。返回失败但实际已经编辑的命令不会重复插入；`beforeinput` 被取消
时保留原文及原光标，禁用/只读输入不会被旧选项修改。选择本来就完整的
引用仅移动光标，不重置撤销历史。

验证：

- 六项 Node 用例覆盖原生通知、编辑后返回失败、缺失/拒绝的命令、取消、
  无内容变化及只读输入。真正的原生撤销由浏览器键盘验证，不以模拟命令
  测试代替。
- `composer-mention-undo-browser.html` 原实现实际补全 `keep this draft @QA`
  后 Ctrl+Z 无文字变化；新实现一次撤销回到原查询，Ctrl+Shift+Z 恢复
  `@"notes/QA notes.txt"`。带后续 `tail` 的补全及重做均保留后续文字，
  光标仍在 `tail` 之前。
- 320×350 夹具页用中文多行草稿、鼠标选择路径验证换行保留及撤销/重做；
  文档宽度 320px，重做光标位于下一行“后面保留”之前。截图为
  `.build/qa-mention-undo-browser-mobile.png`。该证据来自浏览器组件，不代表
  原生移动设备或所有 WebView 通过；旧环境回退没有新增原生撤销保证。

- 最终单文件 Home `.build/mdo-packed-docks-hxv656rf` 从真实工作区选择
  `notes/QA notes.txt`。磁盘草稿已保存后，桌面 Ctrl+Z/Ctrl+Shift+Z 仍分别
  回到 `@QA` 和完整引用，保留后续 `tail` 及光标。Ctrl+K 进入新任务后
  Ctrl+Z 不能带回旧输入；返回原会话仍保留其草稿。320×350 中实际用
  鼠标选择路径、多行中文草稿验证撤销与重做，磁盘 revision 8 保留原换行；
  刷新读回相同内容，文档宽度 320px，浏览器脚本错误为空。390×600 截图
  为 `.build/qa-mention-undo-packed.png`。
- 从最终包读取公共文本编辑模块，SHA-256 与源码字节一致。Windows/Linux
  原生文件系统有界门禁均通过 114 项 Python、175 项 Node、86 个模块、
  严格 C11、32 个运行探针与确定性 A/B 打包；Windows 另通过便携窗口及
  20 秒启动。没有压力或高负载测试，没有将 Linux 服务验证写作原生 GUI
  或实体手机键盘的验收。

根目录 `mdo.exe` 使用通过门禁的 Windows 包更新，SHA-256 为
`ccda933dc6383c01f4c331cdf088416a888086a2d1345d5a1fca44e585342428`。
Linux A/B 包 SHA-256 为
`391f7ec8d997a67fc89fb5c3210789fef7caecbf42d9536d946c1adaa81c389a`。
日志为 `.build/qa-mention-undo-release.log` 与
`.build/qa-mention-undo-linux-native-release.log`。独立组件页可用
`python -m http.server 38925 --bind 127.0.0.1` 打开
`/tests/fixtures/composer-mention-undo-browser.html`，键入 `keep @QA`，选择
候选后依次按 Ctrl+Z、Ctrl+Shift+Z 重放；光标放在多行中间同样可验证。

## 2026-10-01：兼容复制保留文字选择

缺少异步剪贴板 API 的 WebView 使用临时 textarea 复制。原实现只恢复
按钮焦点，临时选择会清除读者选中的回复文字。现在公共复制模块在进入
兼容路径时保存 DOM 范围、原始端点及方向，复制结束后恢复原端点仍在
文档中的范围；Range 的克隆也会在节点删除后移动到父节点，不能将这种
移动误判为原选择仍然存在。多个范围和不支持方向 API 的环境使用 Range
回退。当前焦点在输入框时，保留
选区、方向和内部滚动位置。快照在异步 API 返回失败之后才获取，避免把
等待期间已经转移的焦点带回旧按钮；同步复制事件移除的节点不被恢复。
成功的异步路径和既有复制失败提示保持原行为。

验证：

- 新增五项 Node 用例，覆盖反向选择、多范围回退、复制抛错、复制事件移除
  节点且活 Range 移到父节点，以及等待异步拒绝时继续编辑另一草稿；复制
  模块共八项用例通过。原端点保护用例在中间打包模块上实际失败，修复后
  通过，日志为 `.build/qa-clipboard-live-range-before.log`。
- `clipboard-selection-browser.html` 用正式公共模块模拟缺少异步接口。
  原实现实际点击后 `before` 为完整选中文字、`after` 为空；修复后选择
  文字、反向端点和按钮焦点均保持。桌面及 320×350 页面通过，短屏文档
  宽度 320px。该夹具验证选择恢复与兼容调用结果，不是原生系统剪贴板
  内容或其他平台原生窗口的验收。

- 最终包 Home `.build/mdo-packed-docks-hpp9kfns` 完成一轮本机合成 Markdown
  回复。桌面实际复制代码和整条回复，读回分别为完整 C 代码与原 Markdown；
  320×350 再复制整条回复，未发送中文草稿保持，刷新仍保留，文档宽度为
  320px，脚本错误为空。390×600 截图为
  `.build/qa-clipboard-selection-final-packed.png`。
- 从该最终包读取 `/js/utils/clipboard.js`，字节 SHA-256 与源码一致；以
  提取的模块重复桌面及短屏夹具验证，选择、方向、焦点均保持。兼容模式
  仍仅验证调用结果与选择恢复，不将其写为原生系统剪贴板内容验收。
- Windows/Linux 原生文件系统有界全门禁通过 114 项 Python、169 项 Node、
  85 个前端模块、严格 C11、32 个运行探针与确定性 A/B 打包。Windows 另
  通过便携 WebView2 Home 和 20 秒启动；没有压力或高负载测试。Linux 的
  服务门禁不代表 Linux 原生 GUI、macOS 或实体触控设备通过。

根目录 `mdo.exe` 使用通过门禁的 Windows 包更新，SHA-256 为
`654ebb2b85e4864665cd05f7c27dfab39ac6fce72207c0f6392838485b14f8ea`。
Linux A/B 包 SHA-256 为
`4e6e200dc7fe7cfbdca44ca469b119ebc0df2f616ed245a23786ab661d6198ee`。
日志：`.build/qa-clipboard-selection-final-release.log` 和
`.build/qa-clipboard-selection-final-linux-native-release.log`。独立夹具可用
`python -m http.server 38921 --bind 127.0.0.1` 打开
`/tests/fixtures/clipboard-selection-browser.html` 后点击验证按钮重放。

## 2026-10-01：旧数据导入在写入前检查文件系统能力

当前 WSL 挂载盘拒绝 `RENAME_NOREPLACE`，原来的缓存 Home 导入却先写了
owner 和 payload，再在退休事务时失败，导致事务残留及写入冻结。现在
`MdoHomeImportBegin` 在既有 Home 锁及进程租约中，先移动一个空事务目录、
清理后重新创建记录，再给转换器暂存根。能力缺失时仅移除空目录；这个
准备阶段的两个空目录位置都能由现有启动恢复处理，不新增读取/启动写入。
未知清理内容仍保留且冻结 Home，未把外部内容当作可删除缓存。

迁移结果增加明确的失败种类，在资源释放及清零后仍保留；API 不解析错误
文字，缺失能力返回 `409 migration_storage_unsupported`。前端按稳定码显示
三语存储限制提示，用户无需误以为重启即可完成导入。未修改 xrt、未弱化
无覆盖改名保证、未自动移动用户的便携目录。

验证：

- Home 事务探针新增五种模式：能力拒绝、空目录已移动后返回错误、移动前/
  后进程退出，以及清理目录混入未知文件。前四种保留缓存且无事务残留；
  最后一种保留未知字节并拒绝启动。原有搬迁、回滚、提交与恢复用例仍通过。
- 新 HTTP 探针在复制源码中注入两个相同消息、不同 xrt 种类，验证只有
  `XERR_UNSUPPORTED` 走新错误码；两次请求均保留来源/缓存、无事务记录、
  不要求重启，之后正式项目写入成功。三语错误映射由已有 i18n 测试覆盖。
- 真实 Linux 单文件 Home `/mnt/d/GIT/mdo/.build/mdo-packed-migration-7xfc26sl`
  在桌面及 320×350 页面实际确认导入，得到中文限制提示，宽度无溢出，
  脚本错误为空。后台实际状态 `restart_required=false`、
  `import_in_progress=false`，来源/缓存原字节保留，两个事务目录不存在。
- 最终 Windows 包 Home `.build/mdo-packed-migration-h41gdy3q` 在 320×350
  完成正常导入，结果卡获焦；重启读回迁入深色主题，事务退场，旧来源与
  缓存保持。测试助手打印链接修正为实际的 `/settings/diagnostics` 路由。
- Windows/Linux 原生文件系统均通过 114 项 Python、164 项 Node、85 个模块、
  严格 C11、32 个运行探针和确定性打包；Windows 另通过便携 WebView2 Home
  及 20 秒启动检查。日志为 `.build/qa-import-preflight-release.log` 和
  `.build/qa-import-preflight-linux-native-release.log`。

根目录与 Windows A/B/UI 包 SHA-256 为
`6a422378d44d1ae3eb6205789cb2679295a7eb85d519e2aefd5e3b77090ca127`；Linux
A/B/真实挂载盘 UI 包为
`f59b0548e5665e55dbed27a7721234b99c691fedd1a11e0e34f40355e8ddc0b5`。
挂载盘仍不支持原子不覆盖移动，本轮证明的是缓存 Home 导入提前拒绝，不能
报告该路径的完整事务兼容或全门禁通过。实体设备、原生窗口完整操作及
macOS 仍待验收；长期目标未完成，未做压力或高负载测试。

## 2026-10-01：恢复决定绑定原会话与恢复快照

恢复面板原先只用 `tool_call_id` 保存选择，并以一个全局布尔值标记提交中。
同一个调用 ID 出现在新恢复快照中时，会沿用旧的“重新执行”；会话 A 请求
未返回时，会话 B 的选择也被禁用。更严重的是，迟到的 A 回调会直接接管当前
运行监视器、清掉当前输入错误，或解除当前会话的队列阻塞。

新增独立的 `recovery-decisions.js` 控制器：选择绑定项目、会话和恢复令牌，
相同快照刷新保留选择，新的快照需要重新核对；工具变为不可用时移除重试
选择。每个会话有独立的提交锁，请求开始时冻结自己的选择副本；旧响应不会
清掉另一会话的选择，也不能释放同会话的新请求。失败保留当前快照的选择。
恢复和结束中断轮次的回调都携带原拥有者；只有原会话仍在当前工作区时才
接管运行监视、清除输入错误或派发队列，后台完成仍刷新运行及任务清单。
没有修改 HTTP 协议、工具重试语义或权限规则。

验证证据：

- 七项新增 Node 用例覆盖令牌变化、项目/会话隔离、工具不可用、独立在途
  请求、迟到/重复完成、失败保留及工作区切换。
- 正式生产模块的浏览器夹具 `tests/fixtures/recovery-context-browser.html`
  先在旧实现复现新令牌沿用选择、A 请求阻塞 B；新实现能分别提交 A 的重试
  与 B 的不确定记录，A 先返回时保留 B 的选择和提交锁，两次回调分别归属
  A、B。这是合成 HTTP 响应证据，没有实际重复执行写入工具。
- 最终 Windows 单文件 Home `.build/mdo-packed-docks-90ilydhp` 实际完成
  `SLOW UI` → 停止 → 打开恢复决定 → 继续恢复 → 审核一次只读合成文件检查
  → `exit_code: 0` → 正常结束。未发送草稿和输入焦点保留，决策计数归零，
  脚本错误为空；390×600 页面无横向溢出，保存了桌面及手机视口截图。
  另一个 Home `.build/mdo-packed-docks-2c8lwljn` 确认无验证的合成模型触发
  现有完成保护后显示失败，不能把它计为正常完成。
- Windows 和 Linux 原生文件系统均通过 114 项 Python、164 项 Node、85 个
  前端模块解析、严格 C11、31 个运行探针和确定性打包；Windows 另通过便携
  WebView2 Home 与 20 秒打包启动检查。Linux 宿主从锁定的 xs `5f1e31a`
  重新构建，原生快照位于 `/home/ubuntu/.cache/mdo-linux-qa-recovery-context`。

根目录 `mdo.exe`、Windows A/B 包及上述 UI 包的 SHA-256 均为
`a0c93b4716af874d18cc8d46f9ee9d1030170482b1b68518f6c7c0e45307c2f0`；Linux
A/B 包为 `ab4d3f4b13c83d5a32d6f19fdb9bd4379b4457c621cedd4737fbeab6d2df9ffd`。
日志分别是 `.build/qa-recovery-context-release.log` 和
`.build/qa-recovery-context-linux-native-release.log`。

WSL 环境已能写入临时文件；Linux 服务层复验不再因只读环境搁置。不过同一
门禁在 `/mnt/d` 的 Home 导入回滚处失败，独立小目录验证该挂载盘的
`renameat2(RENAME_NOREPLACE)` 返回 `EINVAL`，原生 `/tmp` 成功。这项限制及
后续修复约束见 [Linux 文件系统原子操作记录](linux-filesystem-atomic-rename.md)。
没有跳过断言或改用可覆盖目标的重命名。实体手机、真实软键盘、macOS 和原生
窗口完整操作仍需验收；长期目标未完成，未做压力或高负载测试。

## 2026-10-01：运行失败结束部分回复与思考卡

原时间线只在 `agent_done` 中收敛尚未结束的卡片；xwork 的取消使用
`agent_done.success=false`，致命失败则单独发出 `error`。因而模型先输出文字/
思考、再失败时，页面虽显示运行错误，回复和思考仍保持“运行中”。工具或
循环保护失败也可能让最后一张回复显示为正常完成。协议已核对当前锁定的
xwork 源码，不把所有 `success=false` 当作失败，也不按错误文字猜取消。

时间线现在把 `error` 作为对应执行的终态，结束它的尚未完成回复、思考和工具
卡，并将该执行的最后回复标记为失败。回复头使用已有三语“失败”文案；
主动取消仍显示“已停止”。范围严格绑定 run ID 与启动事件 epoch，不改变
并行 Agent 或复用 ID 的上一轮。已有文字、复制片段、重试输入与完成工具的
真实状态保留；缺少 `model_done` 时不凭失败事件生成 Token 用量或 tok/s。

新增五项 Node 用例先在原实现复现三项失败，再全部通过：部分流终态、并行/
复用 ID 隔离、完成模型后的工具失败、没有输出的启动失败及主动取消。既有
八项交错时间线用例继续通过。

旧根目录单文件 Home `.build/mdo-packed-docks-h2yqm5kb` 使用本机 Chat
Completions 模型，发送 `STREAM FAIL UI`：两段真实文字/思考 delta 后附损坏
JSON，正式运行库发出 error；页面的部分回复和思考仍为 running。新最终包
Home `.build/mdo-packed-docks-y11tavvt` 重复同一操作，二者变为 failed，原错误
详情保持，复制实际读回 `Hello world`。320×350 刷新后仍为 failed，没有统计
速度标签，三个回复操作按钮高 40px，文档宽度等于 320px，脚本错误日志为空。
390×600 另保存可读失败界面；没有把浏览器视口当作实体手机。截图：
`.build/qa-terminal-error-before.png`、`.build/qa-terminal-error-desktop.png`、
`.build/qa-terminal-error-mobile.png`。模型、Home 与程序均为隔离夹具，未调用
公网模型或改变日常会话；测试服务已正常退出。

Windows 门禁通过 114 项 Python、157 项 Node、84 个模块解析、严格 C11、
31 个运行探针、确定性打包及便携 WebView2 Home/20 秒启动。根目录程序、
两次打包及新 UI 验收包 SHA-256 均为
`4a13b8c3cb46ae6f55ddf78da532e120d69cc7cafc590f92c30deec9633cec72`。
本阶段没有修改 xrt/xs 或事件存储格式。Linux/macOS、原生完整操作及实体设备
未新增验收；不做压力或高负载测试，长期前端恢复目标继续保持未完成。

## 2026-10-01：项目清除的确认执行与提交后收敛

项目清单现可进入独立确认弹窗：默认聚焦取消，输入原项目 ID 才能提交，打开、
刷新和 Esc 均不保存或执行。提交先检查在途动作，并保存全部已加载脏草稿；
失败时没有新编号。随后暂停写入、保存一个便携原意图，执行前重新读取意图
及原结果。恢复卡可重新审阅尚未接受的同一绑定；版本变化或同名重建不能
沿用旧确认。丢失保存/执行响应只核对原编号，不自动重试或生成替代编号。

提交后新增“确认完成并重新载入”：先排除在途动作和未保存草稿、再核对原
终态，确认意图后仍保持本地暂停直到页面重新载入。受影响的返回导航清除，
当前工作区属于该项目时回到项目设置。旧内存、pagehide、自动派发均不能
重新生成已移除引用；永久结果凭据保留。要求宿主恢复的结果明确提示重启
程序，页面刷新不等于宿主重启。未保存内容提供复制与明确重新载入，不自动
丢弃。三语词典各 1339 键；模块与协议见 [前端操作说明](project-purge-frontend.md)。

新增 11 项 Node 用例及一个正式前端模块/真实 HTTP 探针。后者在七个小 Home
验证成功、丢准备/执行/确认响应、回滚、取消和排空失败；四个提交路径逐项
检查被移除根确实不存在，其余项目、共享记录和工作区字节保持。旧页面写入
拒绝与确认后继续暂停也经同一前端 API 写入门验证，不调用外部模型。

确认弹窗点击使用复制 C 夹具的“执行前零目标 abort”模式：任何点击都不能
移除预置数据。桌面核对默认取消、输入错误禁用提交、Esc 返回原清单按钮，
且取消后无意图文件；最终夹具 `.build/mdo-packed-purge-rs2gshz7` 在 320×350
显式提交后显示同一原编号的 abort，焦点落到结果标题，确认移除后回到刷新
按钮，预置字节均保持。最初同步聚焦被原生 dialog 关闭后的焦点恢复覆盖，
现延后一帧聚焦结果标题；最终夹具实测该修复。按钮高 40px、弹窗内部滚动，
无横向溢出、脚本错误为空。这是前端执行及 abort 路径证据，不是生产 GUI
清除成功证据；截图 `.build/qa-purge-confirm-mobile.png`、
`.build/qa-purge-confirm-aborted.png`。

未修改的最终包 Home `.build/mdo-packed-purge-cc3urzzg` 在打开页面之前用正式
API 清除自有合成项目。320×350 打包页核对原提交事实，点击完成确认并实际
重新载入后恢复卡隐藏、项目列表为空。status 验证意图消失、永久原凭据保留、
已清除根未被重建，其余预置字节保持；页面脚本错误为空。截图
`.build/qa-purge-confirm-committed.png`、`.build/qa-purge-confirm-completed.png`。

Windows 最终门禁通过 114 项 Python、152 项 Node、84 个模块解析、严格 C11、
31 个运行探针、确定性打包、便携 WebView2 Home 和 20 秒启动。根目录程序、
两次打包及未修改的 UI 验收包 SHA-256 均为
`2f733c9da7d8999bb190b43c51b3192691ec1f1de427eef2cddec92d3c8d1c46`。
没有修改 xrt/xs 宿主。Linux/macOS、原生窗口完整清除点击、实体手机未新增
验收；只做有界验证，未做压力或高负载测试。实际产品清除流程已开放，长期
前端恢复目标继续保持未完成。便携优先继续使用
`mdo-home/data/cache/webview2`，首次打开原生窗口允许创建 Home。

## 2026-10-01：HTTP 写入准入与失效页面草稿恢复

新增独立的 `src/api/write_admission.c`，普通 HTTP 变更及原始图片上传在正文
处理前核对固定页面令牌，并持有准入计数至处理/回复结束。清除独占准入不
等待已有写入；全新执行在任何数据移动前增加代，原结果重放不增加代，宿主
重新启动更换随机部分。迟到写入即使在意图确认移除或同名项目重建后仍被
拒绝。读取、停止既有 Run 及按原绑定取消/确认恢复保持可用，具体例外及
HTTP 客户端兼容要求见 [接口合同](http-write-admission.md)。

前端只在启动时取得令牌；后续查询不静默替换。空意图的不同代或变更拒绝
也会暂停草稿/派发，核对成功和 abort 确认不能解锁失效页。恢复卡提供明确
复制草稿和重新载入入口，不自动刷新丢弃修改。复制涵盖已加载的图片 ID、
配置与待发快照及运行接纳不明标志，独立对象不会改变原草稿；不是图片文件
导出，也不是所有未加载草稿的完整备份。

Windows 隔离 xs/TCC 探针使用三个小 Home，覆盖缺失/重复/失效头、冷读零
创建、迟到全局/项目草稿、新项目/会话/选择及上传、同名重建、重放/确认与
宿主重启。两客户端/一个夹具线程在复制源中延迟处理完成后的准入释放，
验证写入先接纳阻止清除，以及独占未释放拒绝普通写入；不在监听回调阻塞
等第二连接，不保留请求指针，不代表高并发性能验收。

最终单文件 Home `.build/mdo-packed-purge-vjgf0zi1` 先恢复原项目新任务草稿，
只重启该夹具宿主，再输入「宿主重启后的未保存输入」。迟到自动保存收到
412，输入保持且发送/配置/附件暂停；提示直达项目恢复卡。桌面及 320×350
实际点击复制并读回剪贴板，原正文与未保存新增文字均在 JSON 快照中。
再次核对空意图仍保持暂停，按钮高 40px、无横向溢出，核对完成焦点保留。
明确重新载入后恢复卡消失，重进原项目恢复服务端原草稿且可编辑，未覆盖
原草稿。夹具 status 核对预置项目/会话/记忆/计划/工作区字节保持；页面脚本
错误为空。截图：`.build/qa-write-admission-desktop.png`、
`.build/qa-write-admission-mobile.png`。

发布门禁通过 114 项 Python、141 项 Node、82 个模块解析、严格 C11、30 个
运行探针、确定性打包、便携 WebView2 Home 和 20 秒启动。根目录更新包与
两次确定性包及 UI 验收包 SHA-256 均为
`45fc3c987a16659aa80b0031be533a6d9c447c32201d3b54e1889ab1aafa5aff`。
没有修改 xrt/xs 宿主。Linux/macOS、原生完整点击及实体设备未新增验收；
不做压力或高负载测试。

实际清除确认/执行、前端在途操作排空及提交后草稿/队列/导航收敛仍待完成，
未开放产品清除入口，长期前端恢复目标继续保持未完成。便携优先策略继续
使用唯一的 `mdo-home/data/cache/webview2`；原生首次开窗可创建此目录。

## 2026-10-01：清除请求的前端恢复与取消

新增独立 `project-purge-recovery.js` 和稳定 DOM 的恢复面板。启动先读取便携
意图，再允许自动保存、全局草稿迁移和任务派发；已有请求使用原编号、项目、
核对版本及定义创建时间查询。没有结果不当作已取消，损坏字段/ETag、数字精度
不足或绑定不符均保留记录并暂停当前页面写入。查询有 8 秒时限，重复操作合并。

设置的项目页可重新核对、持久取消原请求，并在已验证 `aborted` 后明确确认移除
意图；确认响应丢失时读取意图和原结果收敛。取消的 HTTP 错误也按 `error.details`
中的提交事实显示，已知提交事实不会被后续矛盾结果抹去。另一页面移除意图时，
本页已知的 pending/committed/未知请求继续保留；只在原 abort 已验证后交接到
后续意图。不会自动执行清除、生成新编号或确认移除已提交请求。

全局提示可直达恢复卡。当前页面的 JSON 写入与直接图片上传均受写入门控制，
读取与停止既有 Run 保持可用。草稿暂停保存时保留文本、附件和待发快照，不
重新安排失败重试或 pagehide 写入；正在保存的一次请求结束后不继续后续脏迭代。
取消并确认后恢复保存和正常草稿/任务核对。按钮等待响应后恢复键盘焦点，不
抢走其他控件的新焦点。中英俄提供相同状态和操作提示。

新增 19 项异步 Node 用例验证丢取消/确认响应、错误中的提交事实、旧页交接、
矛盾绑定/字段/统计、数字精度、重复点击、超时、写入门与原始上传，以及暂停
中/在途保存和任务恢复。最终单文件 Home `.build/mdo-packed-purge-5aweedz0`
在桌面和 320×350 验证提示跳转、原编号取消、刷新保留 abort、明确确认、保留
全局草稿、焦点返回“重新核对”/“刷新”；按钮高 40px、无横向溢出，脚本错误为空，
预置项目/会话/计划/记忆/引用及工作区字节保持。另一 Home
`.build/mdo-packed-purge-35x1hdim` 预先用正式 API 清除夹具自有合成项目，页面在
主定义消失后仍显示原名称和 committed，仅提供核对、不提供取消或确认移除；
检查 Home 中只留下共享审计/锁与意图/凭据，没有重建已移除草稿或项目数据。

Windows 最终门禁通过 114 项 Python、136 项 Node、82 个模块解析、严格 C11、
29 个运行探针、确定性打包、便携 WebView2 Home 和 20 秒启动；UI 验收包与
门禁包逐字节一致。根目录 `mdo.exe` 已更新，SHA-256 为
`39daa1bd1d4f78acb746cc8ef5d3bd15e7ad6c8d039d23e09d923638aac633fb`。
早期门禁运行中修改了前端，两个快照的哈希不同；冻结本阶段代码后完整重跑
通过，未将不同快照算作确定性通过。未做压力/高负载测试。

本阶段的门只约束当前页面，不是服务端全局写入隔离协议；在途请求也不被
撤销。清除确认/执行按钮、准备新意图、受影响项目的在途操作排空、提交后
草稿/队列/导航收敛与其他页面迟到写入保护仍待完成，因此未开放实际清除。
Linux/macOS、原生完整点击及实体设备没有新增验收。长期任务继续，用户选择的
便携优先策略保持：原生首次开窗可以创建唯一的 `mdo-home/data/cache/webview2`。

## 2026-10-01：项目清除意图的便携保存与终态确认

新增 `src/api/project_purge_intent.c`，一项未核对意图保存在
`mdo-home/data/project-purge-intent.json`，不使用浏览器私有存储或备份。
GET/HEAD 仅读外部 Home，精确限制 JSON、大小、字段与绑定，并核对路径及
打开句柄的读取前后身份/大小；损坏或目录不当作空状态。保存、确认移除及
正式执行/取消共享意图锁，另一绑定不能抢占已有意图，原结果查询保持独立。

`POST projects/{project}/purge-intent` 复用执行接口的强 ETag/创建时间解析器，
新意图取得共享项目租约、校验主定义后保存服务端名称。相同绑定只重放原意图，
不重新读取已删除/重建的定义；发布/关闭错误以精确读回确认。已接受的 ID 不能
在确认后重建意图，避免迟到保存复活旧流程。`DELETE project-purge-intent`
需要意图强 ETag 及同绑定终态，不把“无结果”当作可丢弃；pending/损坏/矛盾
证据保留，取消仍须使用原 ID 的持久取消。确认响应丢失后可重放；旧 ETag 不能
移除后续新意图。冻结时可读或重放相符意图，新写入/移除继续要求重启恢复。

十三个隔离 Home 的真实 Windows xs/TCC/HTTP 验证空读零创建、版本/创建时间/
名称、两个 HTTP 客户端争用、丢失执行/确认响应、项目版本变化与同名重建、
迟到保存与旧确认、冻结/重启、发布/移除失败与关闭错误、保存/移除后退出、
损坏 JSON/路径类型和矛盾凭据。项目/引用/工作区字节与目录代按实际操作核对；
故障仅在复制源，没有模型调用或日常数据清除。新增三个意图错误码的中英俄
行动提示，并由既有三语覆盖测试核对。具体协议见 [便携意图](project-purge-intent.md)。

Windows 最终门禁通过 114 项 Python、117 项 Node、80 个前端模块解析、严格
C11、29 个运行探针、确定性打包、便携 WebView2 Home 和 20 秒启动。根目录
`mdo.exe` 已更新，SHA-256 为
`06849c92187aa15127cd727aeac48c2db7db49f51778d4312ba9aefe9c6035f8`。
本轮没有新增页面交互或 UI 验收；前端确认、意图恢复面板、自动保存/派发暂停
及草稿/队列/导航收敛仍待接线，不能把持久 API 通过记作旧版清除操作已恢复。
Linux/macOS、原生完整点击和实体设备没有新增验收；未做压力或高负载测试，
长期任务仍在进行，便携优先策略保持。

## 2026-10-01：请求取消阻止迟到的项目清除

新增存储 `MdoHomePurgeRequestCancel` 与正式 `POST projects/{project}/purge-cancel`。
同一 Home 锁内将尚未接受的原 ID 预留为不可变、全零的 `aborted` 终态，不扫描
或搬迁项目数据，不创建缺失的 Home。已存在且绑定相同的终态原样返回，包括
已有扫描统计的回滚或已提交结果；取消不能撤销提交。pending、损坏及绑定冲突
均保留证据并拒绝新预留。接受/发布沿用空准备日志的恢复，失败无法收敛则冻结。

协调器在扫描后遭遇存储 ID 冲突时，先释放全部保护器再核对原凭据。取消若先
接受，迟到执行仅重放原 abort 与零统计，不返回放弃扫描的数量，也不推进目录代
或修改计划缓存。新 HTTP 使用原强 ETag/创建时间，失败响应仍携带原提交事实；
冻结时仅允许核对相符记录，新 ID 和普通写入继续拒绝，导入隔离不变。

新增真实 Windows xs/TCC/HTTP 探针，在十三个隔离 Home 核对单文件零创建、
参数/绑定/容量、终态重放、同名重建、冻结与重启、零目标真实清除和畸形零统计
拒绝。两个受控本机线程分别验证取消先接受与清除先接受；接受后、部分结果、
完整临时结果和发布后退出，重复重启始终保留原 abort，项目/引用/工作区字节
保持。实际发布后报告关闭错误也由凭据确认结果。故障只注入复制源；没有模型
调用或日常 Home 清除，不做压力/高负载测试，也不宣称断电保证。

Windows 完整门禁通过 114 项 Python、117 项 Node、80 个前端模块解析、严格
C11、28 个运行探针、确定性打包、便携 WebView2 Home 及 20 秒启动。
根目录 `mdo.exe` 已更新，SHA-256 为
`fe2c7827226feb0eb235bb52aed466d2c6819ab963ece86e1be66480dc6c0416`。
本阶段没有修改页面或新增 UI 验收；前端确认、便携客户端意图、断开响应后的
原 ID 核对及草稿/队列/导航恢复仍待接入。用户便携优先选择继续生效：原生首次
开窗可创建唯一 `mdo-home/data/cache/webview2`，无窗口只读启动按需创建。
Linux/macOS、原生完整点击和实体移动端没有新增验收；长期任务仍在进行。

## 2026-10-01：项目清除 HTTP 执行查询与条件引用预览

新增独立 `src/api/project_purge.c`，`POST projects/{project}/purge` 只接收持久
请求 ID、已核对的定义创建时间和强 ETag，由 Requested 协调器取得独占并重扫。
不先读取可能已被删除/重建的定义，不在外层持有共享门妨碍独占。
`GET/HEAD project-purges/{request}` 查询原结果，不依赖当前定义或项目门。
错误响应在 `error.details` 保留提交事实、绑定、统计和恢复状态；Home 冻结时
专用 POST 仍可报告既有结果，所有普通写入及新存储修改保持拒绝，导入隔离不变。

共享预览按“选择 → 草稿”锁序完整检查全局记录，只把当前精确归属的两个引用
加入排序候选、文件数和字节数；返回定义创建时间供最终前置条件使用。预览仍为
瞬时建议，执行须重新校验。项目弹层新增两个引用行，清除错误码补齐中/英/俄。

新增真实 HTTP/TCC 探针，在七个隔离 Home 验证 GET/HEAD/OPTIONS/405、弱/
重复 ETag 和错误参数、过期版本/创建时间零接受、条件引用路径和物理统计，
空闲会话拥有者下预览可读而执行拒绝；完整发送后关闭连接且不读取响应，查询
原 ID 得到提交结果，重放不改数据/目录代，同名重建不受旧请求影响。补偿、
未完成补偿、缓存/清理/结果发布失败经正式路由携带正确事实；冻结时查询和
同 ID 重放可用，普通写入拒绝，重启后原结果收敛。无归属/其他项目引用保持，
损坏引用或结果不当作缺失。故障仅在复制源，没有联网模型或日常数据清除。

最终单文件 Home `.build/mdo-packed-purge-no6ps_ri` 中，桌面及 320×350 预览
显示两个引用“存在”、13 个排序根、15 个文件、3 个目录和 5449 字节；小屏按钮
均高 40px、y=293–333，文档宽 320px。Esc 关闭后焦点回原核对按钮，脚本错误
为空；夹具检查所有预置项目/全局引用和工作区哨兵字节保持，没有执行清除。
截图见同目录 `references-desktop.png`、`references-mobile.png`。夹具使用已拒绝
的待建任务，避免页面恢复控制器自动创建会话改变只读预览的基线。

Windows 最终门禁通过 114 项 Python、117 项 Node、80 个前端模块解析、严格
C11、27 个运行探针、确定性打包、便携 WebView2 Home 和 20 秒启动。根目录
`mdo.exe` 已更新，SHA-256 为
`f1ad1b03726ecb38af827efbff5e13c1a05a1f44f0949d52fecfa16d1a78663d`。
产品清除确认、持久客户端意图、响应丢失核对 UI 与导航恢复仍须接入，长期
任务保持进行中。Linux/macOS、原生窗口完整点击和实体移动端未新增验收；
没有压力或高负载测试，便携优先策略继续生效。

## 2026-10-01：项目清除的持久请求与结果恢复

新增 `MdoProjectPurgeExecuteRequested`，请求 ID 绑定项目、revision 和定义创建
时间。相符终态重放原结果，不再次操作存储/缓存或推进目录代；删除后重建的
同名项目不受旧请求影响。存储锁内再次检查 ID，关闭不同项目并发接受的窗口。
不带请求的兼容 C 入口仍无凭据，产品 HTTP 接线不能使用它。

持久请求在首次搬迁前发布，v2 就绪清单再次绑定其元数据，兼容恢复旧 v1 日志。
完整补偿或提交事实验证后，先发布 `data/project-purges/<request-id>.json`，
再退役日志并清理。单项最多 2048 字节，目录最多 1024 项；不自动过期或复用 ID。
普通 Home 接口保留该命名空间，普通改名还保护 `data` 父目录。

结果发布失败保留日志并冻结 Home，重启先恢复结果再装载管理器。已接受而
终态待发布时不能重新执行；有有效提交标记的查询仍报告已提交，避免把发布
失败误判成未执行。临时结果仅允许规范 JSON 的完整内容或前缀；矛盾请求、
清单、标记或终态在首次补偿/清理前拒绝，保留原证据。

新增真实 xs/TCC 探针，在十七个隔离 Home 验证接受、提交、回滚、重放、同名
重建、过期前置条件零接受、两个受控本机线程争用同一 ID、结果发布失败、
部分/完整临时结果及接受后/发布后退出、重复重启、实际发布后的错误返回，
以及畸形/超限结果、六类矛盾日志和复制源的两项容量上限。旧存储与业务探针
同时通过；故障控制只存在于复制源，没有模型调用或日常 Home 清除。

Windows 最终门禁通过 114 项 Python、117 项 Node、80 个前端模块解析、严格
C11、26 个运行探针、确定性打包、便携 WebView2 Home 和 20 秒启动。根目录
`mdo.exe` 已用验证候选更新，SHA-256 为
`50d881a09b62e33bf4b7d6f9ccb960d02eed247bd9da110690c7db3c3883695a`。
本阶段没有改动前端页面，清除 HTTP 执行/查询、预览条件引用、两步确认及
响应丢失核对/导航恢复仍待完成；长期任务保持进行中。Linux/macOS 和实体
设备未复验，未做压力或高负载测试。用户确认的便携优先策略继续生效：原生
窗口首次启动允许创建 `mdo-home/data/cache/webview2`，不回退 AppData。

## 2026-10-01：项目清除业务协调与计划缓存收敛

新增 `MdoProjectPurgeExecute`，将项目独占、当前定义版本/运行态复查、全量
新清单、全局引用保护器、计划缓存保护器及 Home 存储事务组合成一个 C 入口。
预检错误不移动数据；计划缓存核对扫描 generation、实际原生注册、定义代、
启停状态和活动执行数。停用计划也随项目撤下，快照独立拥有旧数据。

存储的 `Committed` 决定后续行为，不能根据清理返回值猜测是否执行成功。
提交后撤下全部关联计划缓存并通知受影响的会话/记忆目录代；某项原生撤下
失败仍处理其他项，冻结 Home/计划认领并保留独占至关闭。未完成补偿也保留
隔离和日志，重启先恢复存储再装载管理器。首次清理失败、第二次恢复完成时，
仍报告已成功提交；需要重启的错误则单独携带提交事实。

新增真实 xs/TCC 探针，在十个隔离 Home 验证过期版本、会话占用、原生运行及
缓存配置/注册不一致、错误拥有者/代、畸形全局记录的零移动拒绝，正常回滚、
条件引用保留、完整清除、空项目、代更新和旧快照保持。故障用例覆盖未完成
补偿、第二个原生撤下失败、持续/一次性提交后清理失败、存储提交后缓存同步
前退出及重启收敛。其他项目、共享审计/报告、便携浏览器缓存及工作区字节
不变，迟到全局引用请求被拒绝。故障控制只存在于复制源，没有清除日常 Home。

Windows 最终门禁通过 114 项 Python、117 项 Node、80 个前端模块解析、严格
C11、25 个运行探针、确定性打包、便携 WebView2 Home 和 20 秒启动。根目录
`mdo.exe` 已重建更新，SHA-256 为
`77d3922151c10482068992ad1273ed04df8226e90295a473a61d9c0857c51ecc`。
本阶段没有改动前端页面，不能作为产品清除 UI 的验收。持久请求/结果凭据、
预览条件引用、HTTP 执行、确认及导航/响应丢失恢复仍待完成；长期任务保持
进行中。Linux/macOS 和实体设备本轮未复验，未做压力或高负载测试。

用户确认的便携优先策略继续生效：原生窗口首次启动允许创建
`mdo-home/data/cache/webview2`，不回退 AppData；无窗口只读启动按需创建 Home。

## 2026-10-01：项目清除的全局草稿与上次会话引用保护

为完整清除协调器补齐引用保护组件：当前独占租约的拥有者取得选择和草稿锁，
复用现有严格解析并核对读前/读后的文件身份。只有当前记录精确指向目标项目
时，才把全局草稿或上次会话选择加入同一 Home 事务；无归属输入和其他项目
内容保留，损坏记录拒绝整次收集。保护器独立保留租约引用，事务结束后按相反
顺序释放。Home 清单白名单仅新增两个固定条件文件，不接收客户端路径。

普通选择 GET/PUT 使用同一锁，PUT 仍保持会话租约直到发布。全局草稿 PUT
先保护引入或保留的项目再进入最终草稿锁；部分更新重新核对归属和 revision。
默认空工作区及由已有会话/计划形成的未注册项目继续可用，未把“注册定义文件
存在”错误地当作所有输入的前提。无法验证项目时保留已有草稿并反馈错误。

隔离真实 HTTP/TCC 探针验证两个条件目标、错误拥有者、零 Home 创建、畸形
记录及失败后的锁释放；实际全局草稿写入点始终阻止独占。另一项目的草稿和
选择 writer 分别与一个本机保护线程交错，观察到实际等待最终锁，并在释放后
正确保存。复制源的提交前失败使项目及两个引用一起恢复，成功提交后普通 GET
读回空引用，迟到的被清除项目引用被拒绝；其他项目和无归属输入字节保持。
存储探针另验证十三项小清单在条件引用搬迁后及提交后进程中断的重复恢复。
故障控制只在测试副本；没有对日常 Home 做清除，也没有开放产品执行入口。

Windows 最终门禁通过 114 项 Python、117 项 Node、80 个前端模块解析、严格
C11、24 个运行探针、确定性打包、便携 WebView2 Home 与 20 秒启动。根目录
`mdo.exe` 已更新，SHA-256 为
`f73af306b2e52ce97253bd2e37bd656a37b2a1b6bb975b7343951b77d61a2c93`。
本阶段没有改动前端页面；计划缓存同步、独占内业务校验的组合、持久请求/结果
凭据及前端确认/导航恢复仍待完成，长期任务保持进行中。Linux/macOS 未复验；
未做压力或高负载测试。用户确认便携性优先，原生窗口首次启动允许创建
`mdo-home/data/cache/webview2`，无窗口只读启动仍按需创建 Home。

## 2026-09-30：项目清除的存储搬迁、回滚与启动恢复

新增独立 Home 存储事务，将项目固定数据根和合法计划命名空间搬到私有槽位。
预检完整通过后才写日志；就绪清单记录根、日志目录和 payload 的身份及文件
大小，不可变标记经刷新和无覆盖改名发布。移动前后核对身份，提交前错误逆序
补偿，提交后整体退役日志再清理。启动在业务管理器初始化前恢复；歧义位置、
身份替换、错误标记和陌生数据保留证据并拒绝启动，不猜测或覆盖冲突对象。

回滚或清理无法完成，以及运行中发现待恢复日志时，Home 冻结普通写入并要求
重启。结果单独报告是否已提交，即使提交后的清理失败也不能当作未执行重试。
日志路径及 Windows 大小写/尾随点空格别名对普通 Home 接口保留。项目候选清单
复用存储目标类型与容量；旧导入和清除日志同时存在时停止恢复。全局引用和
计划缓存不由此原语擅自改写，业务执行入口尚未开放。

新增真实 xs/TCC 有界探针，以十一项小型合成数据根验证各搬迁节点失败与进程
中断、移动后错误、准备/临时标记/提交/退役/清理中断及重复恢复；另覆盖回滚/
清理失败冻结、Windows 文件句柄阻止改名、改名成功却报告关闭错误、无 Home
零创建、非法/重复/过期目标、节点限额、源与 payload 内的 junction、文件/目录/
日志身份替换、大小变化、歧义位置、清单和标记损坏、过早提交及旧导入冲突。
其他项目、配置、缓存、共享记录及全局状态字节保持。节点上限用复制源四节点
限额验证，测试控制不进入应用；现有最小运行夹具也补齐新增存储依赖。

Windows 最终门禁通过 114 项 Python、117 项 Node、80 个前端模块解析、严格
C11、23 个运行探针、确定性打包、便携 WebView2 Home 与 20 秒启动。根目录
`mdo.exe` SHA-256 为 `afed8036c856e90c915f8ed19be671a975a851adbc087898bc36a8e7896668f6`。
本轮没有修改前端操作，未新增打包页点击证据；详细边界见
[项目 Home 存储事务](home-purge-transaction.md)。仅验收进程中断恢复，未承诺
断电持久性；Linux/macOS 未复验。下一阶段接入业务独占、全局引用协调、计划
缓存同步与前端确认/结果恢复，不能把本阶段记作产品项目清除已完成。未做压力
或高负载测试。

## 2026-09-30：项目清除候选路径与会话选择的写入保护

项目清除预览原只显示存在性和业务数量，遗漏新任务草稿、计划备份/历史，无法
核对实际路径。现由独立 C 模块只读构造完整候选根，遍历项目会话桶和迁移侧车，
统计文件、目录与字节数；界面列出所有候选路径并明确保留共享记录及工作区源
文件。三语词典各 1284 键。扫描失败不返回部分清单，界面清空旧清单并允许重试。

扫描限制路径/节点/深度/计划文件大小，检查真实文件类型、身份、链接及计划
磁盘定义与缓存的一致性。HTTP handler 全程持有项目共享租约；内部 API 接受
当前独占拥有者重扫，拒绝错误项目、共享及旧注册表租约。保存“上次会话”原在
最终写入前释放加载会话，现延长项目租约到写入结束，避免独占与陈旧选择竞态。

真实 xs/TCC 探针覆盖路径/物理统计、草稿备份、停用计划及改派备份归属、只读
字节保持、孤立计划、磁盘归属变化、深度/节点上限、Windows junction 和错误
拥有者。节点限额采用复制源四节点故障，不做大规模文件或负载测试。测试保留
旧导入证据并重启独立的空 Home，避免用要求重启的缓存代继续跑后续请求。

最终单文件夹具 `.build/mdo-packed-purge-vj6mjiz4` 的桌面及 320×350 页面显示
11 个候选根、13 个文件和 3 个目录，新增草稿和计划历史可读。小屏可滚动至
末尾路径，操作按钮高 40px、初始 y=293–333；Esc 返回原核对按钮。模拟孤立
计划文件后失败清单为空，移除夹具故障后 Shift+Tab/Enter 刷新恢复 11 项且保持
刷新焦点。无横向溢出或浏览器脚本错误，既有项目和工作区哨兵字节保持。截图
见同夹具目录 `preview-desktop.png`、`preview-paths-mobile.png` 和失败反馈图片。

Windows 最终门禁通过 114 项 Python、117 项 Node、80 个前端模块解析、严格
C11、22 个运行探针、确定性打包、便携 WebView2 Home 与 20 秒启动。根目录
`mdo.exe` SHA-256 为 `97003868f8eee5ef96ace944002bd87a311112ee98688e08b4d5382b4f681dbc`。
详细接口和剩余步骤见 [项目清除事务边界](project-purge-transaction.md)。本轮未
复验 Linux/macOS、原生窗口内完整操作或实体移动端；实际搬迁、回滚、启动
恢复和全局引用重置尚未实现，此阶段不开放清除执行入口。未做压力或高负载测试。

## 2026-09-30：保留便携浏览器缓存的旧数据导入

按用户选择，原生窗口浏览器数据留在 `mdo-home/data/cache/webview2`。旧迁移要求
Home 不存在，使首次打开窗口后的默认 GUI 无法导入。现允许只有宿主锁及浏览器
缓存的已挂载 Home，仍拒绝任何用户数据、未知项、链接父目录和来源/目标相互包含。
缓存不遍历、不移动；预览显示保留提示，令牌绑定目标模式，沿用预览和二次确认。

新增私有 Home 导入事务：持有现有进程租约，转换期间冻结普通写入；固定七个数据根
经当前解析器校验后，记录设备/目录 ID，发布就绪清单并逐项锚定无覆盖搬迁，最后
发布不可变提交标记。成功后 HTTP 写入要求重启，Home 直接写入口也拒绝，避免旧
manager 修改新批次。启动在所有业务 manager 之前恢复，提交前回滚，提交后保留；
先整体退役事务再清理、所有者标记最后删除。身份变化和歧义状态保留记录并拒绝
启动。标记文件刷新；本阶段保证进程中断恢复，未承诺断电目录元数据持久性。

真实 xs/TCC 探针覆盖七个搬迁节点、报告/提交/回滚故障、准备至清理的进程中断、
重复启动、目录替换和标记损坏。缓存文件在发布期间保持打开，旧来源及缓存字节
始终保留。补充缓存三个父级及暂存树的链接拒绝；Windows 权限不支持 symlink 时
使用测试目录内的 junction，外部哨兵不变，不修改系统权限。HTTP 覆盖仅缓存 Home
的失败清理、成功后的 503 写入隔离、重启读入设置及包含来源的目标拒绝。

最终单文件夹具 `.build/mdo-packed-migration-4a5s717g` 使用合成缓存和旧配置，不
创建产品会话。进入设置和只读预览后 Home 仍只有锁和缓存。320×350 中展开确认
聚焦取消、确认按钮高 40px 且位于 y=181–221，无横向溢出；键盘取消返回原按钮，
再次展开后 Tab/Enter 完成导入，焦点移到重启反馈。正常重启后设置页显示深色且
实际主题为 dark，事务目录消失，浏览器脚本错误为空。截图在同夹具目录内。

Windows 最终有界门禁通过 114 项 Python、117 项 Node、80 个前端模块解析、严格
C11、22 个运行探针、确定性打包、便携 WebView2 Home 及 20 秒启动。根目录
`mdo.exe` SHA-256 为 `01dae3be1e1e168edb34d697790696b7cf4415451f9b52a383d378ed2dc5f78b`。
详细契约见 [Home 导入事务](home-import-transaction.md)。本轮未复验 Linux/macOS
和实体移动端，也未在原生窗口内完成导入点击；Windows 原生缓存路径由独立门禁
覆盖。完整项目清除/恢复仍缺；未做压力或高负载测试。

## 2026-09-30：旧数据迁移的项目生命周期保护

旧迁移原在生成目标项目映射之前创建暂存目录，发布期间也没有项目租约。现先只读
规划全部映射，包含非法旧 ID 的哈希桶和隐式 tasks 桶，再在首次暂存写入前保留
全部项目。任一项目被独占时返回原 `409 migration_conflict`，部分取得的租约释放，
不创建 Home 或暂存目录。成功发布和失败临时目录清理结束后才释放全部租约。
三语冲突反馈已补充“项目正忙”，导入仍沿用预览、再次确认与成功后重启的操作。

真实 xs/TCC HTTP 探针独占大小写/尾随点别名、tasks 和哈希项目，验证源字节不变、
零新写入、部分取得失败释放及空 Error 的直接 C 入口。复制源注入报告写入和最终
rename 失败，检查准备、文件写入、发布及清理节点均保留全部项目；失败清理后与
成功发布后可重新独占。测试控制不进入正式应用，未修改 xrt/xs API。

Windows 最终有界门禁通过 114 项 Python、117 项 Node、80 个前端模块解析、严格
C11、21 个运行探针、确定性打包、便携 WebView2 Home 与 20 秒启动。根目录
`mdo.exe` SHA-256 为 `3ebead2e0ebb3a859d9590118c995e8a848cfff36eb8ad713ff0084c6f9b2704`。
WSL 环境故障尚未恢复，本轮没有新增 Linux/macOS 验收；未做压力或高负载测试。

同时确认一个真实入口缺口：便携原生窗口先创建浏览器缓存 Home，而迁移目前要求
Home 不存在，即使用户数据为空也不能从该默认 GUI 导入。下一阶段须设计保留缓存
的发布事务与启动恢复，不删除活动缓存，也不直接合并已有用户 Home。完整项目
清除及崩溃恢复仍未实现，此阶段不代表清除入口已可开放。

## 2026-09-30：在任务详情回答计划 Agent 的询问

计划执行原以计划 ID 绑定 `ask_user`，但没有可供会话 API 读取的产品会话，用户只能
停止等待。现每次执行拥有独立 `AskScopeId`，经 `/tasks/{task}/asks` 读取和回答，
保持项目记忆的原计划关联，不创建额外会话目录或增加模型工具。任务快照增加
`pending_questions` 与对应 ETag，任务列表和详情显示三语“等待你回答”。

问答卡片与普通对话共享原有实现：选项、自由输入、UTF-8 1024 字节上限、中文输入法
保护和一次性提交。任务轮询保留编辑器节点；切任务保存独立草稿，切语言保持焦点
和选择。已接受回答后的读取失败保留只读卡片及“已提交”，原位重试只读状态。
停止与回答通过执行器生命周期锁串行，询问 manager 继续原子检查令牌与截止时间。

真实 xs/TCC HTTP 探针启动同一计划的两个 `ask_user` 执行，验证独立 ID、反序回答
与输出对应、跨 task 拒绝、字节上限、重复和迟到回答、终态空列表、HEAD/OPTIONS
及没有会话目录。最终单文件 Home `.build/mdo-packed-docks-nqmgbx77` 在 320×350 的
真实任务抽屉输入“先检查，再继续”，输入框 y=181–221，选项/提交按钮高 40px，
无横向溢出；提交后仅写一条 succeeded 历史、询问归零。打包生产组件夹具通过轮询
与输入法、语言切换、独立草稿、重复请求、接受后的 503 读取失败及重试收敛。
原普通对话软键盘视口夹具和计划停止夹具保持通过，浏览器脚本错误为空。

Windows 最终有界门禁通过 114 项 Python、117 项 Node、80 个前端模块解析、严格
C11、21 个运行探针、确定性打包、便携 WebView2 Home 和 20 秒启动检查。根目录
`mdo.exe` SHA-256 为 `3c608e95fb4e70d1d90a17c7abb1d1013832b7ff8e84ae1ddf1d99f107ba8e86`。
WSL 的只读/I/O error 尚未恢复，新增链路 Linux 复验未完成；macOS 和实体移动端
仍待独立验收。待回答计划没有新增跨重启恢复功能。未做压力或高负载测试。

## 2026-09-30：计划任务停止接到实际 Agent Run

原任务面板调用 xwork 的通用 task 取消，只改变 scheduled task 的状态，未取消执行器
持有的 Agent Run。现优先由执行器处理自己的 task ID，并把取消传给模型和工具。
取消请求与收割在同一个生命周期锁下串行；仍运行的 Run 不等待退出、不提前标记
terminal，已完成的 Run 按真实结果归档。直接 xwork 取消仍由下一次 pump 转发。

task API 新增 `stop_requested`，强 ETag 同时反映该位。列表、详情与对话停靠卡在
等待退出时显示三语“正在停止…”，保留按钮焦点并拦截重复点击；实际退出后才显示
“已停止”。停止响应的确认快照先写入前端状态，随后的 GET 失败不会丢失已接受状态。
没有修改 xrt、xs 或 xwork 的 API/ABI。

真实 xs/TCC 运行探针覆盖模型取消令牌、等待实际退出期间的 RUNNING 和项目租约、
重复停止、直接 task_cancel 转发、取消历史唯一性，以及完成后停止保留成功结果。
HTTP 探针另外让实际 `ask_user` 工具处于等待中，验证 DELETE 使工具退出、待询问
清空，并写唯一 cancelled 历史。新增两项 Node 测试验证确认后读取失败和明确拒绝。

隔离候选单文件 Home `.build/mdo-packed-docks-ojmxka25` 的真实任务面板停止了正在
等待回答的 `cancel-packed-ask`：先显示“正在停止…”，随后活动数归零，历史中只有
task/run 3 的 cancelled 结果，文本为 `user question was cancelled`，浏览器脚本
错误为空。生产组件夹具 `tests/fixtures/task-cancellation-browser.html` 使用相同
打包 CSS/JS，在 320×350 验证重复激活只发送一次 DELETE、确认后持续停止提示、
轮询重绘保留按钮焦点，以及模拟实际退出后移除停靠卡并显示已停止。该夹具的
退出阶段是模拟状态，不代替前述真实模型/工具的执行证据。

最终根目录单文件包在另一隔离 Home `.build/mdo-packed-docks-xc9fw1y_` 复核相同
320×350 夹具，确认新的停止响应快照合并后五项断言及模拟退出均通过，脚本错误为空。

本轮 Linux 全量复验遇到宿主环境故障：C 盘只剩约 44 MiB，WSL 写入失败后文件系统
变为只读，后续 `/bin/bash` 启动报 I/O error。故不能报告 Linux 门禁通过；须恢复
环境后重跑。Windows 编译临时目录切到 `.build/compiler-temp` 后，完整有界门禁
通过 114 项 Python、115 项 Node、77 个前端模块解析、严格 C11、21 个运行探针、
确定性打包、便携 WebView2 Home 与 20 秒启动。根目录 `mdo.exe` SHA-256 为
`34406ef03a73a2fe2d98f2224373d5e199300e8172579d9d8e9ba1beca948a4a`。
实体移动端和 macOS 仍待验，未做压力或高负载测试。

## 2026-09-30：计划认领与运行拥有者保持连续租约

到期和显式认领现在在游标同步前保留完整 catalog 的项目集合，再核对 generation 与就绪状态；空闲读取仍无写入。认领成功把项目租约转移到 manager，直到结果历史发布才释放，不留下 Agent 创建前的交接空窗。未完成 mdo claim 上限为 64，提前分配与检查，不依赖 runtime task 是否被调用者释放。Finish 只处理仍有 mdo claim 的任务，重复完成不会追加历史；统一 task 已 cancelled 时按 cancelled 记录。

Agent 回调拥有者在非空 ProjectId 下独立保留租约，直到最后一个 runtime 引用释放，关闭记忆工具也一样。它和 manager 的 claim 租约共同覆盖启动失败、回收、关闭时 Run 已销毁但结果尚未写入的清理边界。若游标同步中途失败，保留整个关联项目集合到 manager 关闭；若结果历史写入失败，保留对应 claim 并明确拒绝重复完成。这是进程内隔离，没有新增跨重启结果修复协议。

真实 xs/TCC 探针让 Beta 的过期计划先推进、再认领 Alpha，实际验证两次发布均保留两项目。独占期间认领/立即运行保持定义和审计字节不变；返回 claim 后、Agent 尚未创建时仍不可独占。受控第二次发布失败后，Beta 已为 revision 2、Alpha 仍为 revision 1，两项目保持隔离至关闭；历史失败也保留项目到关闭。关闭记忆工具的执行器探针覆盖零落盘的 Agent 创建拒绝、失败创建与启动清理、模型回调、产品 Run 回收后保留的未启动 xwork_run 引用，以及关闭时外部回调拥有者已释放、历史尚未落盘的空窗。检查点仅注入复制的源，正式应用不含这些控制。

Windows/Linux 有界发布门禁各通过 114 项 Python、113 项 Node、77 个前端模块解析、严格 C11、21 个运行探针与确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` 已更新，SHA-256 为 `3624fc94711ce07caf3b4897073e4998940e0cc4bc4410ea33334af62f872af0`；Linux 包为 `2e4ff9f4627c0cb6a6f03858a01e859f277092f211d248a6640e71a0579fcd0f`。未做压力或高负载测试。

本阶段发现另一个实际操作缺口：任务面板取消 scheduled task 只调用 xwork 的 task 取消，尚未贯通执行器持有的 Agent Run；历史中的 cancelled 状态不能证明 Agent 已停止。下一阶段先补齐该停止链，再继续旧版迁移的生命周期接入、完整项目清除和启动恢复。macOS 与实体移动端仍待验收。

## 2026-09-30：计划定义与全局启停接入项目租约

计划创建在首次 writer/audit/store 操作前取得项目共享租约。启停、删除和替换先在只读 manager 锁内解析当前项目，释放该锁后取得租约，再在写入锁内核对归属；解析期间被另一 writer 迁到不同项目或删除的定义会返回冲突，不使用未经保留的身份。替换同时保留当前项目与目标项目，保护发布和失败补偿，目标被独占时释放先取得的源租约。其他项目的独立定义操作仍可执行。

全局 schedules 开关会修改整个 catalog，现先保留有界 catalog 的全部关联项目，再检查 generation；任一项目被独占或 catalog 在取得租约期间变化时，在 runtime 修改前拒绝。成功更新、失败回滚和回滚失败后的兜底停用全程保持租约。开关没有变化时仍是只读空操作，不受独占影响。

真实 xs/TCC 探针在无 Home 时验证创建被拒绝且不落盘；已有定义的独占拒绝保持定义与审计字节不变。复制源中的检查点用真实 Replace 插入归属迁移、用真实 Create 插入 catalog 增量，验证二次校验拒绝陈旧身份并释放租约。受控 store 发布失败后，原定义补偿期间源/目标均不可独占；批量启停更新与回滚均受控失败后，兜底停用的每个节点仍保留全部项目，退出后租约可重新取得。正式应用不含测试检查点。

Windows/Linux 有界发布门禁各通过 114 项 Python、113 项 Node、77 个前端模块解析、严格 C11、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` 已更新，SHA-256 为 `7d87cf99c3b574facb18b22876a9db0441f24de07b5956ef62ebdf9d806ff0be`；Linux 包为 `e8d170e9645481b2b58b583d67c75e068bd1d981bbadd2f896606d585903e44c`。计划 claim/finish 和运行拥有者、旧版迁移、完整清除及恢复事务仍待接入；macOS 与实体移动端仍待验收。未做压力或高负载测试。

## 2026-09-30：记忆与直接侧车写入接入项目租约

项目记忆 Upsert/Remove 在记忆锁与首次 Home 写入前取得共享租约。整目录导入在完成来源校验后、取得记忆锁和 writer 文件锁前，先保留全部关联项目；任一项目被独占时释放已取得的租约，不发布全局或项目记忆，也不创建 Home。成功发布或既有失败回滚结束后才释放全部租约。Agent 记忆工具绑定也持有租约，解绑后已保留的工具目录仍阻止独占，直到最后一个工具引用释放。三个项目记忆 HTTP 路由同时覆盖整个 handler；全局记忆保持独立作用域。

待办投影/重置、图片事件写入/修剪、分叉图片回滚、跨项目图片复制、七个队列凭据及准备/认领助手、上传过期清理均在独立 C 入口取得租约。图片复制先保留源项目和目标项目；目标取得失败会释放源租约。队列附件引用查询可触发启动凭据修补，因此也按写入边界保护。分叉调用者继续在完整发布及回滚期间保持自身租约，不能把单个助手误当成跨调用事务。

真实 xs/TCC 探针验证独占期间直接写入被拒绝，纯单文件 Home 不创建，已存侧车逐字节与目录清单不变；图片回滚没有删除预置文件。记忆探针覆盖多项目取得中途失败的清理、实际发布时保留全部租约、最后一个 store 受控写入失败后删除已发布文件并释放租约、公开目录导入拒绝，以及 Agent 解绑后工具目录引用仍保留租约。测试检查点只注入复制源，正式应用不含控制入口。这些证据覆盖同步发布与既有回滚，未新增记忆导入的崩溃恢复事务。

Windows/Linux 有界发布门禁各通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` 已更新，SHA-256 为 `8418ab36e73df710c149699b827017d7c42fadee22592f3df57229461296784c`；Linux 包为 `75751796a6add8b65eb1a9385bfad749b4aa674cff4dcb7b9854c96ac07ad5f7`。Windows 原生窗口按便携优先设置写入 `mdo-home/data/cache/webview2`，首次开窗允许创建 Home；无窗口只读启动仍按需创建。计划与旧版迁移的生命周期接入、完整清除及启动恢复仍待实现，macOS 和实体移动端仍待验收；未做压力或高负载测试。

## 2026-09-30：项目定义与侧车请求保持生命周期租约

草稿、队列、附件等 HTTP handler 原先在检查会话后立即释放句柄，随后才访问侧车；项目新任务草稿则不经过会话句柄。路由现显式声明需要租约的项目范围，在 handler 前取得共享租约，直到其响应与清理完成才释放，覆盖项目定义端点、项目/会话草稿、提交意图、队列、反馈、附件、待办及关联会话操作。OPTIONS、405 与无效标识保留原合同；全局草稿和其他项目不受同一项目的独占影响。

项目 Create/Replace/Unregister 的直接 C API 也在首次 Home 操作与 writer 锁前取得共享租约；修改/取消注册新增 BUSY 结果，包括调用者不接收 Error 时。全局反馈列表会独立修补侧车，其 Reconcile 入口已自行持有租约。HTTP 冲突采用 `409 project_busy`，中英俄提示说明本次操作未执行，不误报项目标识重复。

真实 HTTP/TCC 探针在纯单文件和已存项目两种状态下持有独占，验证侧车及关联操作被拒绝、文件逐字节与目录清单不变；前者没有创建 Home。复制应用的测试专用检查点证明草稿释放会话句柄之后、项目定义发布之前仍不能取得独占。正常保存、无效正文和版本冲突后的独占重取也通过；另在 Windows/Linux 直接调用 C writer，触发重复创建以及已取得租约后的过期 revision 修改/取消注册，确认独占随后可取得。正式打包应用不含测试控制或检查点。此轮没有新增可执行清除入口；底层图片/待办/队列助手、记忆、计划和迁移仍待核对，完整事务及共享记录策略仍缺。

Windows/Linux 有界发布门禁各通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` 已更新，SHA-256 为 `bd0f3a38ff2fc4516aef6f32cc8a70f30aacaa5c1cee88624ffc0bf5a5459cc5`；Linux 包为 `fb69ad90758ae0732d5731257970dfb2ebf50eb8f7a35176db6aa08c7c72ac5b`。macOS 和实体移动端仍待独立验收，未做压力或高负载测试。

## 2026-09-30：项目生命周期门接入会话与回调拥有者

恢复旧版项目清除需要先阻止其数据仍在使用时发生搬迁。新增 `project_lifecycle.h` 与独立的 `src/projects/lifecycle.c`，提供纯内存、非等待式的共享/独占租约；bootstrap 在 Home 后初始化该服务。会话创建、打开和加载在首个文件操作及管理器锁之前取得共享租约，分叉继承源租约，失败准备与回滚全程保持租约。会话句柄和事件桥各持有引用，直到最终 Agent 回调拥有者释放后才解除排他阻挡。

有界 TCC 探针验证共享引用保持、不同项目独占、大小写/尾随点别名、受控单线程跨线程拒绝，以及服务关闭/重新初始化后释放旧租约不会影响新注册表。会话探针还验证独占期间创建/打开/加载均返回上下文冲突且没有创建目录；只关闭会话句柄但保留 AgentSession 时独占仍被阻止；加载句柄和失败创建清理的租约边界也通过。交互运行、会话回放、归档/回收站、分叉及队列恢复的既有探针通过。

这只是清除事务的会话隔离阶段。项目定义、直接侧车 API、记忆导入、计划和旧版迁移还须接入；锁内完整清单、失败隔离、搬迁与启动恢复也仍待实现。具体接入矩阵见 [项目清除事务边界](project-purge-transaction.md#生命周期门接入进度)，项目清除入口保持“未开放”。macOS 和实体移动端仍待独立验收。

Windows/Linux 有界发布门禁各通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` 已从门禁包更新，SHA-256 为 `ab4ce18bccd4fb8a7b3f534efc317aa01773474a27b5def94ddc9c712a6183a0`；Linux 包为 `019547afb37585bb9da24f586a788bfe99c789b80e9ffcf8778efd9d4293a281`。未做压力或高负载测试。

## 2026-09-30：项目清单包含备份与迁移侧车

项目设置的只读清单原只报告有效会话、计划与记忆主文件，遗漏仍需清除的定义/记忆备份以及旧版迁移侧车。现在 `purge-preview` 补报项目定义 `.bak`、项目记忆 `.bak`、会话目录和 `migration/session-prompts/<id>` 的存在性。查询使用 Home 锚定 stat；预期文件变成目录等异常类型时返回 `purge_preview_unavailable`，不将其误报为“无”。前端提供中英俄文案，旧接口未返回字段时显示“未知”。

隔离单文件 Home `.build/mdo-packed-docks-pecx47a4` 验证了主记忆文件缺失、备份仍在以及旧提示目录存在的清单；280×250 页面可滚动，刷新与关闭按钮始终可见且高 40px，页面宽 280px。刷新后清单保持正确，Esc 关闭后焦点回原清单入口，脚本错误为空。API/TCC 探针覆盖不存在/存在的四个字段，以及把备份路径替换成目录时的明确失败。实体手机触控和原生移动端仍待验收；清除事务尚未实现，入口仍如实标明只读核对。

同时修正事务设计的并发前提：外部 Home 已由 `.mdo.lock` 跨进程独占；当前缺口是进程内项目生命周期读写门、锁内完整清单、搬迁事务和启动恢复。证据和后续合同见 [项目清除事务边界](project-purge-transaction.md)。

Windows/Linux 有界门禁各通过 114 项 Python、113 项 Node、77 个模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` 已更新，与图形实测候选及门禁包一致，SHA-256 为 `39e72ed4a78f1b49ca2a47e471bd15358706af6ff726032e098da5410b5d160d`；Linux 包为 `355b081af15d086e8420363b1fbff0cfb7f0e5e4a5368ec6192518491fbb8294`。未做压力或高负载测试。

## 2026-09-30：附件图片预览跟随可见视口

附件缩略图在输入时可打开图片预览。此前软键盘只缩小 `visualViewport` 而布局视口仍为 700px 时，400×300 图片的预览弹窗仍居中于原页面：图片下缘 y=474，关闭按钮下缘 y=262，均超过 250px 可见区域。现把图片预览纳入顶层弹窗的可见视口定位，并按可见高度限制图片尺寸。

浏览器夹具使用正式预览模块与样式，在 320×700 布局、250px 模拟可见高度下验证弹窗 y=8–234、关闭按钮 y=13–53、图片高 168px 且下缘 y=225；可见视口上移 20px、移至布局底部及恢复后位置均正确，关闭后焦点回原缩略图。候选单文件 Home 启用图片能力，在 280×250 实际选择隔离的 `fixture.png` 并打开预览：关闭按钮 y=101–141，点击后焦点回“查看图片 1”，附件仍留在未发送输入区。页面无横向溢出或脚本错误。实体手机键盘、系统文件选择和其他原生 WebView 仍待验收，未做压力或高负载测试。

Windows 有界门禁通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针、确定性打包、便携 WebView2 Home 与 20 秒打包启动；Linux 独立 ext4 工作树通过同一单元、模块、运行探针及确定性打包（跳过 GUI）。根目录 `mdo.exe` 已从通过门禁的候选更新，SHA-256 为 `2459e7294873a253ca2c1837f0868fbaba76e5555df46a972327e40b525982b6`；Linux 包为 `dc2a80fc94becea4280f851d5e0d1712cc3dbae7344488f5986fe814ac0feead`。

## 2026-09-30：快捷键帮助避开软键盘

输入区执行 `/help` 可直接打开“快捷键与命令”，但它未进入顶层弹窗的可见视口定位规则。浏览器夹具在 320×700 布局视口中模拟软键盘把可见高度缩至 250px，旧样式仍将弹窗置于 y=49–651，“知道了”按钮底部 y=650，被键盘遮住。现在该弹窗复用新任务等弹窗的可见视口定位规则，出现键盘时位于 y=8–242，操作栏底部 y=243；帮助内容区高 95px、内容高 567px，可内部滚动。可见视口上移 20px、移至布局底部以及恢复原高度后位置均正确。

候选单文件页在 280×250 从输入框执行 `/help`，弹窗位于 y=16–234，操作栏 y=178–235，内容区 100px/659px；滚动到末尾 `scrollTop=559` 后，“知道了”仍可点击，关闭后焦点回到主输入框。页面无横向溢出或脚本错误。这些是浏览器窄视口和模拟可见视口证据，实体软键盘与其他原生 WebView 尚待验收。未做压力或高负载测试。

Windows 有界门禁通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针、确定性打包、便携 WebView2 Home 与 20 秒打包启动；Linux 独立 ext4 工作树通过同一单元、模块、运行探针及确定性打包（跳过 GUI）。根目录 `mdo.exe` 已从通过门禁的候选更新，SHA-256 为 `18522df756328f675808e8f86d819b7ca4ba42de88ae3b4602447373e4c44158`；Linux 包为 `cd5384413cafb22a7e92af207bc728b066f865089f54f1b3846abedd8c8af7b5`。

## 2026-09-30：极短屏记忆编辑与保存焦点

旧单文件包在 320×250 打开“全局记忆”时，标题操作占 115px，列表至少占 95px，编辑表单被压到约 35px 高；保存按钮位于 y=749–789，无法在弹窗可视区操作。记忆弹窗现于极短屏压缩标题，并让列表与表单共用同一滚动内容区。点击“新建”或选择条目时，聚焦的字段会随滚动进入可见区。软键盘只缩小 `visualViewport` 时，弹窗也按可见区域定位，不依赖布局高度媒体查询。

最终候选单文件页在 280×250 打开记忆弹窗，弹窗位于 y=8–242，内容区高 103px、可滚动内容高 679px，页面无横向溢出。点击“新建”后标识输入框位于 y=169–209；填写并保存隔离 Home 的测试记忆后，列表显示 1 条、revision 1，保存按钮位于 y=185–225 且重新获得焦点。此前保存时禁用按钮会把焦点丢给页面根节点，现仅在用户未转移焦点时恢复。该隔离记录刷新后仍在，选中可编辑；确认删除后列表归零、revision 2、焦点回“新建”，没有留下失焦的隐藏确认按钮。未保存状态下“打开文件夹”“放弃并关闭”“关闭”三按钮各至少 40px 宽。390×500 仍使用原移动端列表/表单双区布局。

浏览器夹具在 320×700 布局视口模拟软键盘将可见高度缩至 250px：弹窗位于 y=8–242，内容区高 103px、可滚动内容高 604px；可见视口上移 20px、移至布局底部、恢复原高度均跟随正确位置。打包页脚本错误日志为空。实体手机键盘、触控及其他原生 WebView 尚待验收，未做压力或高负载测试。

Windows 有界门禁通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针、确定性打包、便携 WebView2 Home 和 20 秒打包启动；Linux 独立 ext4 工作树通过同一单元、模块、运行探针及确定性打包（跳过 GUI）。根目录 `mdo.exe` 已从通过门禁的候选更新，SHA-256 为 `321269f849f6316ca937767fa3021ec8bfcdcf16c4c5ffb39bde07426f1457e5`；Linux 包为 `46543002019455b87565d95b04562022fcd78ff19353c316f9e6fbd74a213123`。

## 2026-09-30：项目弹窗跟随移动端软键盘可见视口

项目弹窗虽已适配 280×250 的布局视口，但移动端软键盘可能只缩小 `visualViewport`，不触发高度媒体查询。原规则只调整新任务、会话操作和消息编辑弹窗，漏掉项目弹窗。在 320×700 布局视口、250px 可见视口的浏览器夹具中，修复前项目弹窗仍居中于原布局，位于 y=139–561，提交栏完全被键盘遮挡。

现把项目弹窗纳入已有可见视口定位规则。新增浏览器夹具复用正式 CSS 与 `trackMobileViewport`，验证键盘出现时弹窗位于 y=8–242、操作栏底部 y=243；字段区高 111px、内容高 298px，可独立滚动。可见视口上移 20px、移至布局底部以及恢复原高度后，弹窗均跟随正确位置；消息编辑弹窗的原有键盘夹具也继续通过。候选单文件页在 280×250 打开“添加项目”，弹窗位于 y=16–234、提交栏 y=186–235，无横向溢出或脚本错误。这里模拟了软键盘引起的可见视口变化，实体手机键盘和其他原生 WebView 仍待验收。未做压力或高负载测试。

Windows 有界门禁通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针、确定性打包、便携 WebView2 Home 与 20 秒打包启动；Linux 独立 ext4 工作树通过同一单元、模块、运行探针和确定性打包（跳过 GUI）。根目录 `mdo.exe` 已从通过门禁的候选更新，SHA-256 为 `993bda852a75820f29483b0abe4d23b176c1e57510b33ac531b80b97ca8b6115`；Linux 包为 `84f772583d420cecd34d3c8dcfc6dcf2386150118a05e78e78f4f9fe55870b0f`。

## 2026-09-30：极短屏项目弹窗保留操作与错误焦点

项目创建/编辑弹窗曾与其他任务弹窗使用相同结构，却没有对应的固定标题、滚动字段区和固定操作栏布局。旧单文件页在 320×350 打开“添加项目”时，提交按钮位于 y=456–496，超出 y=17–333 的弹窗与视口；整张表单需滚动到底才能提交。重复项目标识被服务端拒绝后，错误虽出现在弹窗内，键盘焦点却落到页面根节点。

项目弹窗现在沿用其他任务弹窗的布局：标题与取消/提交栏固定，仅字段区滚动；高度不超过 300px 时收紧标题和内边距。错误容器可由程序聚焦，提交失败时把焦点交给错误说明，保留原字段供修改。

候选单文件 Home `.build/mdo-packed-docks-w_jswruw` 在 280×250 下，提交按钮为 y=191–231，弹窗底边 y=234；字段区高 114px、内容高 274px，可内部滚动。实际把隔离工作区 `src` 注册成项目后进入该项目的新任务页，输入框获焦，刷新仍选中 `src`。再次以相同标识提交，错误说明位于 y=147–182 且获焦，Tab 到“取消”，取消后焦点回“添加项目”；编辑已有项目时保存按钮同样位于视口内。页面宽度为 280px，无横向溢出或浏览器脚本错误。实体触控和软键盘仍待验收，未做压力或高负载测试。

Windows 有界门禁通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针、确定性打包、便携 WebView2 Home 与 20 秒打包启动；Linux 独立 ext4 工作树通过同一单元、模块、运行探针和确定性打包（跳过 GUI）。根目录 `mdo.exe` 已更新，SHA-256 为 `394b1bf63f3693660a39286ea08949b66bbbaf7f9460834f6bcb82be9161a50a`；Linux 包为 `5e140d73b99f0ab0750d7acdbc640088b4f5fb9c4e55a4cb5475cc5c6cdb620f`。

## 2026-09-30：失败工具卡同时显示状态和历时

前一阶段把工具卡的时间标为“总历时”后，拒绝 `exec` 审批的 320×350 单文件页暴露了状态遮蔽：折叠卡只显示“总历时 7.9 秒”，无法从摘要看出工具没有执行。工具卡现在对失败或取消同时显示状态和总历时；成功卡仍保持简洁的总历时。未改动审批策略或工具结果。

候选单文件 Home `.build/mdo-packed-docks-jh_emp40` 重新运行 `APPROVAL UI` 并点击“拒绝”，折叠卡显示“失败 · 总历时 12.0 秒”，展开后错误输出仍是 `tool execution denied by approval policy`，最终回复显示工具和等待合计 12.0 秒。320×350 页面宽度等于视口宽度，状态文字完整位于视口内，浏览器错误日志为空。实体触控和其他原生 WebView 仍待验收，未做压力或高负载测试。

Windows 有界门禁通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针、确定性打包、便携 WebView2 Home 与 20 秒打包启动；Linux 独立 ext4 工作树通过同一单元、模块、运行探针及确定性打包（跳过 GUI）。根目录 `mdo.exe` 已从验证候选更新，SHA-256 为 `2132f28389668291f3d301c88850c79ad67d654a51580e3f34488b76a0a92da8`；Linux 包为 `87d511971194241dee58405a5442255743d1301cfe4a90215a1fc6805348ecdf`。

## 2026-09-30：工具阶段耗时明确包含等待

`xwork` 在询问和权限审批之前发出 `tool_start`，在用户回答、执行或拒绝之后才发出 `tool_done`。此前时间线把两者间隔标作“工具时间”，会把人的决策等待误写成命令运行耗时。时间线现在以“总历时”标记每个工具卡，以“工具和等待合计”标记回复中的累计阶段时间；成功和失败的已结束阶段均计入，缺失开始事件的阶段不臆造时长。执行器自身耗时仍由具体工具结果报告，不能由通用事件间隔推断。

隔离模型夹具的 280×250 候选单文件页走通询问 `Inspect`、审批“允许一次”、`exec` 成功和最终回复。工具结果报告命令 `duration_ms: 42`，页面分别显示询问“总历时 8.0 秒”、命令“总历时 8.9 秒”、回复“工具和等待合计 16.9 秒”，准确表明这些数字包含等待。最终输入框获焦，页面宽度与 280px 视口相同，浏览器错误日志为空。该检查是浏览器窄视口，不代表实体触控或其他原生 WebView 已验收。

用户选择便携优先后，核对现有 `app/xs.json` 已把 WebView2 profile 指向 `mdo-home/data/cache/webview2`。发布门禁以“首次启动只在可执行文件旁新增 `mdo-home`”为准，并验证搬迁可执行文件和 Home 后 profile 可继续使用；启动时创建 Home 属于预期行为，不再将“零写入”作为窗口启动判据。未做压力或高负载测试。

最终源码的 Windows 有界门禁通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针、确定性打包、便携 WebView2 Home 与 20 秒打包启动；Linux 独立 ext4 工作树通过同一单元、模块、运行探针及确定性打包（跳过 GUI）。根目录 `mdo.exe` 已更新，SHA-256 为 `c1c6f6792274580fde1ab6dd32e54e69e5231088bca2dd317b58abed5b73a4d0`；Linux 包为 `328cedec87ff276432cb3e2aff488c45f09f98fc9cea052d3c0cea1172ba639c`。

## 2026-09-30：Agent 工具清单摘要区分继承与空模块

扩展页原把 `agent.tools: []` 显示成“0 个工具”。运行时空工具清单表示 Agent 没有额外的工具白名单，仍按 effect 上限与会话权限筛选实际可用工具；“0 个工具”会误导自定义 Agent 的排查。Agent 卡片现在明确显示“未设工具白名单 · 仍受权限约束”，显式工具清单仍显示其数量。Module 卡片的 0/1 工具数是模块实际注册量，保持原样。

候选单文件 Home `.build/mdo-packed-docks-41e4bh1x` 启用了额外的只读 QA Agent：280×250 扩展页同时显示该 Agent 与内置 Default，二者的空白名单均按新语义说明；模块卡仍分别显示 0 或 1 个实际工具。中文、英文、俄语预览均显示对应说明，文档无横向溢出，浏览器无脚本错误。该检查只覆盖打包页摘要，不据此推断某一会话的有效工具总数；有效目录仍由运行时按权限计算。未做压力或高负载测试。

Windows 有界门禁通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针、确定性打包、便携 WebView2 Home 和 20 秒打包启动；Linux 独立 ext4 工作树通过同一单元、模块、运行探针与确定性打包（跳过 GUI）。根目录 `mdo.exe` 已更新，SHA-256 为 `1cfb8a25c92a3ab542f1bf6cc3f60955fb422f3bd9325643c036449140525cbf`；Linux 包为 `c6270927a8858a3302c81c0c558a5bdaf9aeb9c71f4924afe8093c429c0211fe`。

## 2026-09-30：极短屏消息操作避开回到底部按钮

在 280×250 的旧单文件页发送约千字的用户消息后，滚动到消息末尾时，右侧“回到底部”浮层覆盖“编辑此消息并重新发送”按钮。点击编辑实际触发回到底部，弹窗没有打开。移动布局现将该浮层靠左放置，避开右对齐的消息操作栏，保留原有滚动入口。

候选单文件 Home `.build/mdo-packed-docks-wd39il17` 中，浮层位于 x=12–92，编辑按钮位于 x=217–257；点击编辑后，280×250 弹窗完整落在 y=16–234，保存按钮在视口内，文档无横向溢出。取消后焦点回到原编辑按钮；再次编辑并重新发送后，服务端历史显示截断边界、新用户消息及新回复，刷新后仍保留。浏览器错误日志为空。这只验证了浏览器模拟窄视口，实体触摸和其他原生 WebView 仍需验收。未做压力或高负载测试。

Windows 有界门禁通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针、确定性打包、便携 WebView2 Home 和 20 秒打包启动；Linux 独立 ext4 工作树通过同一单元、模块、运行探针与确定性打包（跳过 GUI）。根目录 `mdo.exe` 已从通过门禁的候选更新，SHA-256 为 `9e84f176cd31b223c7c8140e4b7a5b42ff34a1e378e373c23e8573d6bb0196d6`；Linux 包为 `9065fa04c0b9fc5a1769915ff4f0427768ceab2bc207db862ec98d4996e41e06`。

## 2026-09-30：模型编辑区重绘后保留键盘位置

模型与 Provider 使用同一双栏编辑区。旧单文件 Home `.build/mdo-packed-docks-431719st` 在 280×250 新建 Provider 时，填写标识和名称后点“放弃修改”，表单重绘移除原按钮，焦点落到页面根节点；切换模型/Provider 及选择列表项也会移除获焦按钮。另在候选 Home `.build/mdo-packed-docks-40dtomky` 复现“刷新”成功后同样失焦。键盘用户必须重新从页面入口定位到编辑区。

现在分类切换后聚焦当前分类按钮，列表选择后聚焦选中资源；放弃新建草稿后回到“新增模型”或“新增 Provider”，放弃已有资源的编辑后回到原列表项。候选单文件 Home `.build/mdo-packed-docks-40dtomky` 在 280×250 逐项验证这些焦点链。另在隔离 Home 新建仅指向 `example.invalid` 的测试 Provider，修改名称后放弃，表单恢复已保存名称、焦点落到原 Provider 列表项。最终候选 Home `.build/mdo-packed-docks-esz2uo2h` 验证刷新后焦点回新“刷新”按钮；刷新后的焦点恢复只在当前焦点退回页面根节点、编辑区仍可见时执行。页面宽 280px，浏览器脚本错误为空。测试 Provider 未用于模型请求。其他设置分区与实体触控仍需继续验收。未做压力或高负载测试。

最终源码的 Windows 有界门禁通过 114 项 Python、113 项 Node、77 个模块解析、21 个运行探针、确定性打包、便携 WebView2 Home 和 20 秒启动；Linux 在 WSL 原生文件系统复用已锁定宿主，通过相同单元、模块、运行探针及确定性打包（跳过 GUI）。根目录 `mdo.exe` 已更新，与 Windows 发布候选 SHA-256 同为 `a07fbe509d07a47adf93169a603d91522d6c9754fb897fe599d03abd3c4fbdf6`；Linux 包为 `8ef612e3a7bcc6803dca12e38a6b26cebdb97499d57a2aa2021ace66bdaccba3`。

## 2026-09-30：视口恢复后退出极短屏决策浮层

真实单文件 Home `.build/mdo-packed-docks-74paobil` 在 280×250 收到 `ASK UI` 询问并点“展开决策”后，把窗口放大到 320×350：会话区已有 177px、不再符合拥挤条件，但浮层仍占 288px，按钮继续显示“收起决策”并获焦。原因是高度同步只隐藏了拥挤标记，没有清除展开状态；展开状态本身又让按钮保持可见。

现在高度恢复、无需极短屏浮层时自动收起，并在展开按钮失去可见性前把焦点转给同卡标题。候选单文件 Home `.build/mdo-packed-docks-6zkh2ol7` 用真实询问复测：280×250 可展开；320×350 自动收起、按钮隐藏、标题获焦且页面宽 320px；缩回 280×250 后按钮恢复，可再次展开并选择 `Fast`。运行完成后待决清空、主输入获焦、回复及用量正常，浏览器脚本错误为空。最终根目录包的多决策展开夹具在 280×250 仍为 `passed=true`，覆盖新增询问与审批时的答案、焦点和相对位置。此项验证浏览器视口变化与正式打包资源，实体手机软键盘及其他原生 WebView 仍需验收。未做压力或高负载测试。

Windows 有界门禁通过 114 项 Python、113 项 Node、77 个模块解析、21 个运行探针、确定性打包、便携 WebView2 Home 和 20 秒启动；Linux 在 WSL 原生文件系统的独立拷贝通过相同单元、模块、运行探针与确定性打包（跳过 GUI）。根目录 `mdo.exe` 已更新，与 Windows 发布候选 SHA-256 同为 `945685e8a60749720700114c45d598ea82e8d19120797eb0fb6261b52bab45c3`；Linux 包为 `354149695b696bf137582591a8d35bb2f394e6e9d63ea89269a0bd07f612b0c2`。

## 2026-09-30：打包资源下补验极短屏多决策到达

新增 `tests/fixtures/decision-expand-arrival-browser.html`，在 280×250 合成一张长询问卡，展开后输入答案，再插入第二张询问和一张审批卡。夹具检查展开按钮状态一致、原答案和焦点保留、编辑器相对停靠区位置不跳动、Esc 收起后继续编辑，以及清空待决项后主输入获焦且页面不横向溢出。源码直载的浏览器夹具通过；隔离代理 `tests/manual_packed_docks_qa.py --packed-path .\mdo.exe --decision-expand-arrival-fixture` 只提供测试文档，CSS 和 JS 请求仍转发到单文件包内置 VFS，同尺寸结果 `passed=true`，浏览器脚本错误为空。

这验证了正式打包资源在合成多卡状态下的交互；并不代表真实运行服务已产生同时待决的多张卡，也不替代实体手机软键盘、触摸或 Linux 原生 WebView。当前 WSL 无 DISPLAY、Wayland 或可用图形宿主，故本轮未取得 Linux 原生 GUI 证据。产品代码与包字节未变；重建候选包与根目录 `mdo.exe` SHA-256 均为 `414628269fd8a32f5f0dc9ec47229d4cb191b6719e507b2ae51994e3fe97a6cb`。未做压力或高负载测试。

## 2026-09-30：便携 WebView2 门禁等待临时配置释放

有界发布门禁的 Python、前端、运行探针和确定性打包已经通过后，Windows `TemporaryDirectory` 在删除测试专用 WebView2 Home 时偶发遇到 `BrowserMetrics/*.pma` 的短暂文件占用，导致整项门禁报 `WinError 5`。测试进程已正常结束，随后检查没有残留的测试进程。门禁现仅对自身在系统临时目录下创建、名称前缀匹配的绝对目录做最多 5 秒的删除重试；目录持续被占用仍明确失败，不掩盖便携配置错误，也不触碰其他目录。重跑 Windows 有界门禁后，便携 Home 搬移、环境变量与 CLI 优先级、20 秒打包启动及确定性打包均通过。此项只修复测试收尾，不改变产品包行为。

## 2026-09-30：极短屏待决卡可展开阅读和操作

原单文件 Home `.build/mdo-packed-docks-_p23byrd` 在 280×250 视口收到长询问时，停靠面板可滚动正文约 378px，但只露出 69px，首屏仅有标题和问题开头，选项需反复滚动才能到达。主输入中 18 行下一条草稿及焦点保持，页面未溢出；缺口是待决操作难以到达。现仅当移动端决策面板高度低于 120px 且内容溢出时，在询问或审批标题旁显示 40px 高的“展开决策”。点开后面板占用可视区、保留内部滚动和标题处的“收起决策”；Esc 也可收起。待决项解决后自动复位，正常高度不显示额外控件。语言文字加入中英俄，审批卡重绘保持按钮焦点。

候选单文件 Home `.build/mdo-packed-docks-o1wk0127` 的 280×250 长俄文询问从 69px 展至 190px，滚动后长选项和收起按钮同屏，点击首项后待决卡清空、输入焦点返回，页面宽度 280px 且无脚本错误。相同包的审批卡也可展开与 Esc 收起；320×350 时入口隐藏。最终候选 Home `.build/mdo-packed-docks-go3p4qd4` 复核收起后从 280×250 改到 320×350：隐藏按钮将焦点交给审批标题；允许一次后待决清空、输入框获焦、宽度保持 320px，脚本错误为空。展开模式对实体触摸与真实软键盘的行为仍待设备验收。

Windows/Linux 有界发布门禁均通过 114 项 Python、113 项 Node、77 个模块解析、21 个运行探针与确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒打包启动。根目录 `mdo.exe` SHA-256 为 `414628269fd8a32f5f0dc9ec47229d4cb191b6719e507b2ae51994e3fe97a6cb`，Linux 包为 `0e6dc89c509fd8dd2bcf48a209754c58ccc33c36b70c38df2a1bf265fbd73f70`。没有运行压力或高负载测试。

## 2026-09-30：记录启动停留阶段以定位偶发空白页

此前隔离浏览器偶发出现静态资源返回 200、会话 URL 保留、却没有初始化 API 请求的空白启动。本轮先用原根目录单文件包在隔离 Home `.build/mdo-packed-docks-xkoailpd` 做两次有界刷新：两次都恢复 “Packed docks QA”，无网络加载失败或脚本异常，不能据此排除偶发故障，也不能将其归因于 mdo。

页面根节点现在记录 `data-mdo-startup-stage`：`import` 表示入口模块及静态依赖仍未完成导入；`setup` 表示已进入应用初始化；`resources` 表示正在等待启动所需的资源；`navigation` 表示正在选择启动路由；`ready` 表示启动完成。20 秒观察上限的控制台提示会带上当时阶段，失败时保留最后阶段。这一标记不改变启动路由、超时恢复或消息操作。下次复现时，可同时读取 `data-mdo-startup` 和该阶段，收集模块网络瀑布，再与原生 WebView2 对照。

候选单文件包在 `.build/mdo-packed-docks-m0ro98xg` 延迟首个入口模块 30 秒，载入期间观察到 `import`，请求完成后恢复原会话与输入焦点；在 `.build/mdo-packed-docks-tcf01y6p` 延迟首次会话及模型目录读取 10 秒，无 hash 的页面观察到 `resources`，完成后变为 `ready` 并恢复上次会话。最终同字节包在独立 Home `.build/mdo-packed-docks-fx_mp7oo` 连续刷新明确会话 URL 三次，均为 `ready`、标题和输入焦点正确，CDP 未记录网络加载失败或脚本异常。未捕获真实偶发空白页，故根因仍待故障当次证据。

Windows/Linux 有界发布门禁各通过 114 项 Python 检查、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒打包启动。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `6e94e6a541da398aa161df2cb0756acfcbddfe0dc096d9d0de60742b0932f313`，Linux 包为 `c674dcb252a108b71cd54e6490fc1ef2f6c8d507bbc452ff488aff5efd237530`。未做压力或高负载测试。

## 2026-09-30：启动询问弹窗随界面语言显示

选择“启动时询问是否继续上次任务”后，弹窗过去写死中文；英文和俄文设置下，标题、说明和两个按钮仍是中文。现将这些文字接入三语词典，并在无明确路由且确实采用询问模式时等待语言包就绪，再打开弹窗。明确会话 URL 的恢复仍不等待语言包。

候选单文件包在隔离 Home `.build/mdo-packed-docks-no84k8nc` 将英文语言包响应延迟 5 秒：载入期间显示启动遮罩，弹窗出现时已是英文；继续上次任务后回到原会话，输入框获焦。320×350 页面再次打开弹窗，无横向溢出；选择“新任务”进入新任务页并聚焦输入框。随后切换俄文，同尺寸弹窗标题、说明和按钮均为俄文，继续按钮获焦，文档宽度仍为 320px。此为浏览器窄屏模拟，实体移动端和其他原生 WebView 尚待验收。

Windows/Linux 有界发布门禁各通过 114 项 Python 检查、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 及 20 秒打包启动。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `31e2d62f90ac104a6d93955be0576112e34ec3e46c5a83ebb9aa92f5128a4771`，Linux 包为 `136b3637d503f7c458a4fba08cd064758c66eb3f36ecdb4b7a02173f51ed1b57`。未做压力或高负载测试。

## 2026-09-30：首次载入不再露出可操作的新任务空壳

静态 HTML 过去在入口模块尚未执行时就显示“新任务”欢迎页和输入控件；明确会话 URL 的短暂载入因此看起来像错误地跳回新任务，按钮也能在初始化前获得焦点。现在页面初始显示独立的“正在初始化 Agent 工作台…”状态，并暂时禁用底层工作区及跳转链接。模块成功后才开放工作区并恢复输入焦点；失败或 20 秒超时时，加载层让位于已有的可聚焦重载入口。迟到的模块成功时，超时层自行收起并恢复原路由。

最终候选单文件包在隔离 Home `.build/mdo-packed-docks-k5c2ch49` 延迟首次入口模块 5 秒：载入期间可访问树仅有加载说明，随后回到“Packed docks QA”且输入框获焦。另一 Home `.build/mdo-packed-docks-2r3vj79b` 首次拒绝模块：320×350 页面只显示启动失败说明和“重新载入”，无横向溢出；点击后恢复原会话、输入焦点和可操作工作区。第三个 Home `.build/mdo-packed-docks-vvd2wo_s` 延迟模块 60 秒：20 秒时显示超时说明、焦点落在重载按钮且工作区仍不可操作；模块返回后不需点击即恢复会话和焦点。首次拒绝时控制台有预期的导入错误，成功与迟到恢复页没有脚本错误。

Windows/Linux 有界门禁各通过 114 项 Python 合同检查、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒打包启动。根目录 `mdo.exe` 的目标 SHA-256 为 `4d5267b756a7af1e5a6ccc9e4735a4f57c8b9c6275b1a959ef5346d69183fb48`，Linux 包为 `7fc00e1850eb99abe5c93200545b21a6a3d04f5534ba8c607ce387cd42b4e502`。这一改动消除了载入期间误导性的可操作空壳，不解释此前一次“静态资源均返回 200 但长期无初始化 API”的偶发故障；其根因仍待故障当次证据。未做压力或高负载测试。

## 2026-09-30：单文件多后台任务结束时保留另一项选区

隔离模型夹具新增一次性 `TASK SECOND UI` 标记，允许在第一项后台任务仍运行时提交第二项；`--task-ms` 的有界上限扩至 60 秒，以便在真实打包页观察两个任务交错完成。此改动只涉及手动测试夹具。

根目录 `mdo.exe` 在独立 Home `.build/mdo-packed-docks-welj26pu` 依次运行 `TASK UI`、`TASK SECOND UI`，两次审批均选择“允许一次”。对话停靠卡同时显示两项“进行中”；选中第二项任务名称的一段文字后，第一项完成并被移出卡片，第二项仍为“进行中”，选区内容保持，所在行由第二行变为第一行。第二项完成后，任务检查器显示 0 活动、2 最近、0 异常；第二项详情的退出码为 0，标准输出为 `task UI fixture second`。浏览器脚本错误为空。

这补齐真实单文件页的“移除前一任务而保留后一任务阅读状态”证据；服务端逆序重排、实体触控及其他系统原生 WebView 仍待验收。产品包字节未变，`mdo.exe` SHA-256 仍为 `8ff890b26f234dd54e88d9dceb0b75eace6acd23e5965fe574962a462eebb1c6`。未做压力或高负载测试。

## 2026-09-30：打包页跨页面待办更新保留选区

隔离模型夹具新增 `TODO UPDATE UI` 和 `TODO REORDER UI` 两个一次性标记，分别提交 1/2→2/2 的完整待办快照，以及把两项交换顺序的完整快照。夹具脚本通过 Python 语法检查；正式单文件程序内容没有改动。

根目录 `mdo.exe` 在独立 Home `.build/mdo-packed-docks-n2i48p5w` 打开同一会话的两个页面。阅读页先通过真实 `mdo.todo` 工具建立 1/2 计划，再选中“Verify refresh”；更新页提交进度快照后，阅读页的服务端轮询显示 2/2、第二项已完成，原文字选区仍是“Verify refresh”。更新页再提交重排快照，阅读页显示“Verify refresh”移到第一项，选区依然保留。刷新阅读页后，同一 2/2 顺序从持久会话恢复、输入框获焦。另一页面在 320×350 视口下文档宽 320px，待办与输入区可见，两个页面的浏览器脚本错误均为空。

此项补齐真实单文件页的跨页面进度更新和两项逆序，仍不代表任意重复正文条目的身份匹配、实体设备触控或其他系统原生 WebView 已验收。产品包字节未变，`mdo.exe` SHA-256 仍为 `8ff890b26f234dd54e88d9dceb0b75eace6acd23e5965fe574962a462eebb1c6`。未做压力或高负载测试。

## 2026-09-30：单文件待办与后台任务轮询补验

用根目录同一打包字节，在独立 Home `.build/mdo-packed-docks-7enlv0uj` 依次发送 `TODO UI` 和 `TASK UI`。真实工具调用先产生 1/2 的待办卡，随后审批卡请求运行有界的 30 秒本地进程；允许一次后，后台任务卡显示“进行中”。鼠标选中待办的“Verify refresh”，等待任务结束及后台任务卡移除，待办卡仍显示 1/2，原文字选区和页面焦点没有被轮询打断。320×350 视口下选区仍在，文档宽度为 320px，输入区可见，浏览器脚本错误为空。

第二个独立 Home `.build/mdo-packed-docks-hupcdty1` 直接运行 `TASK UI`，在后台任务卡上实际选中了任务名称；30 秒进程结束后，该任务卡按现有规则消失，选区随被移除的节点清除，页面没有脚本错误。这验证了真实单文件页中任务卡出现、轮询和完成移除，以及任务移除时存活待办选区的行为；多任务逆序、待办进度变化和实体触控仍只由生产模块夹具覆盖，不能据此宣布全部阅读状态场景完成。

本阶段未改产品源码或打包字节，`mdo.exe` SHA-256 仍为 `8ff890b26f234dd54e88d9dceb0b75eace6acd23e5965fe574962a462eebb1c6`。未做压力或高负载测试。

## 2026-09-30：列表重排时保留正在阅读的行

上一阶段已经复用待办和后台任务的行节点，但服务端更新列表顺序时，逐项排序仍可能把正在选中的行从 DOM 中移走再插回，浏览器因而清空文字选区。现在协调列表时，先保护当前获焦的卡片或编辑器；没有获焦行时，如果文字选区完整位于某个存活行内，以该行为锚点移动其他节点。待办与后台任务共用这一规则，排序结果仍以服务端列表为准。

`tests/fixtures/conversation-todo-card-browser.html` 在旧实现复现条目重排后 `reorderKept=false`，修复后为 `true`；`conversation-task-card-browser.html` 的三项后台任务逆序也保留第二项选区，`passed=true`。320×350 的 `conversation-dock-scroll-browser.html` 和 `ask-arrival-while-editing-browser.html` 同时通过，确认新决策到达时仍优先保护询问输入及其滚动位置。这些是加载生产模块的浏览器夹具；真实单文件页面的列表逆序和实体触控尚未复测。

Windows/Linux 有界门禁均通过 114 项 Python 测试、113 项 Node 测试、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒启动。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `8ff890b26f234dd54e88d9dceb0b75eace6acd23e5965fe574962a462eebb1c6`，Linux 包为 `1a9e34581c9508efefb2a78aceb4cb4c29a606dd3fbbb319519c1701748de3fa`。未做压力或高负载测试。

## 2026-09-30：待办进度更新时保留未变化条目

待办卡过去以整份计划快照为键：任一条标记完成、添加条目或折叠展开，都会创建新的卡片和按钮。阅读后续待办条目时，前一条完成便清空文字选区；折叠按钮也失去原节点。现在同一会话保留待办卡和折叠按钮，按条目正文及重复序号匹配存活行，仅同步完成标记、当前项样式和进度计数。服务端计划快照没有条目 ID；修改正文或无法区分的同名条目仍可能更换对应行，不能承诺跨任意重排保留阅读状态。

生产模块浏览器夹具 `tests/fixtures/conversation-todo-card-browser.html` 在旧实现复现 `progressKept=false`、`insertionKept=false`、折叠操作原按钮失效；修复后进度变化、前部插入和折叠再展开都保持未变化的条目节点及按钮焦点，`passed=true`。320×350 的既有 `conversation-dock-scroll-browser.html` 与 320px `conversation-docks-mobile-browser.html` 也通过；前者改为核对真实内容变化后节点保留且显示更新。以上属于浏览器生产模块证据，尚未在真实待办运行的单文件页面或实体触控设备上复测。

Windows/Linux 有界门禁均通过 114 项 Python 测试、113 项 Node 测试、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒启动。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `f6a507f41d012c0b252b7dc6d5f8efb5417b25543a85d7e986609928700c5866`，Linux 包为 `4c30f0633a5e956dbb5cdf4d0623d400da458d12e19c56281393e73837f95502`。未做压力或高负载测试。

## 2026-09-30：后台任务卡更新时保留阅读位置

后台任务状态轮询过去按状态、标签和数量重建整张对话停靠卡。用户正在阅读任务名称时，状态从“待执行”变为“运行中”或新任务加入会清掉文字选区，详情按钮也被换成新节点。现在按任务 ID 保留已显示的任务行，同一会话保留卡片和“查看任务详情”按钮，只同步变化的状态、标签和数量；移除已结束任务时，仍显示的后续任务节点保持原位。

生产模块浏览器夹具 `tests/fixtures/conversation-task-card-browser.html` 先在旧实现复现 `sameCard=false`、`sameLabel=false`、`sameButton=false`、`selectionKept=false`；修复后任务状态更新、第二项加入和队首移除均保留存活节点及正在阅读的文字选区，任务标签变化时详情按钮的焦点也保持，`passed=true`。这是浏览器模块交互证据，尚未在真实后台任务的单文件页面和实体触控设备上复测。

Windows/Linux 有界门禁均通过 114 项 Python 测试、113 项 Node 测试、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒启动。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `8b5d42e815cc452f569a45dd8bc01b9d90c935c3b3132446e07abc6519f480ce`，Linux 包为 `8f15db1a4ff54af4f622bba26f9026592d4c806af4047d5d516ae408ea71941a`。未做压力或高负载测试。

## 2026-09-30：拖放对象不可读取时明确提示

上一阶段支持 item-only 图片拖放后，仍有一个 WebView 边界：拖放事件报告文件，但 `files` 为空且文件 item 的 `getAsFile()` 返回空。此时浏览器导航已被阻止，页面原先既不上传也不提示，用户可能误以为图片已经加入草稿。现在清除拖放高亮后在输入区显示明确的“无法读取拖放的文件，请使用‘添加图片’选择”，并提供中英俄三语文案；没有文件对象时不发上传请求。

生产模块夹具 `tests/fixtures/composer-upload-session-browser.html` 在旧实现复现 `unreadableHandled=true`、错误列表为空；修复后出现 `image.dropUnavailable` 错误及中文提示。九项既有上传、剪贴板、item-only 拖放和模型兼容场景继续通过，合计十项，浏览器脚本错误为空。该测试使用合成 DataTransfer，不代替操作系统原生拖放或实体设备验收。

Windows/Linux 有界门禁均通过 114 项 Python 测试、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒启动。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `ae1daf5fc62cee94f2e960c9932faaf9b642eee307a7e626e61ed3d64592b710`，Linux 包为 `d8f0cd96f1a94de6d97b78e43a9a833c450ce6ac5dd98cb7c1725beebafe5e60`。未做压力或高负载测试。

## 2026-09-30：拖放图片可从 item-only 数据读取

输入区原先用 `DataTransfer.items` 识别文件拖放，却只从 `DataTransfer.files` 取上传内容。有些 WebView 在放下时仍只提供带 `getAsFile()` 的 item；旧逻辑会阻止页面导航并清除拖放提示，但既不上传也不报错。现在优先读取 `files`，为空时从文件 item 提取图片，并在文件自身未给出类型时使用 item 提供的图片 MIME；两种集合同时存在时只上传一次。

生产模块浏览器夹具 `tests/fixtures/composer-upload-session-browser.html` 新增 item-only 拖放：旧代码下 `dropHandled=true`、上传请求为零，修复后同一场景产生一次 PNG 上传并加入附件。夹具原有跨会话并行上传的清理回调已与当前产品接口对齐；会话切换、图片优先创建、系统空 MIME、三种剪贴板视图、拖放和模型兼容性共九项均通过，浏览器脚本错误为空。该验证是合成 WebView 数据视图，操作系统原生文件拖放仍需实体窗口验收。

Windows/Linux 有界门禁均通过 114 项 Python 测试、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒启动。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `9975083f809ddff576d0139f6e26950639d9e842e8ffed28674c1ced635ce75d`，Linux 包为 `fe7194e30ece68ba66d253f5c408f7dc4afb4ac06bcb41a044899d6704dd4adf`。未做压力或高负载测试。

## 2026-09-30：诊断重新检测保留焦点与原位重试

320×350 单文件页的“诊断与存储”中，旧版点击“重新检测”会在迁移目录读取后的重绘中把焦点丢到页面根部。现在扩展目录与诊断目录共用稳定身份的操作按钮：请求期间新绘制的按钮保持 `aria-disabled`、重复触发被同一进行中集合挡住，重绘后焦点回原按钮。迁移目录读取失败时，错误显示在迁移分组，按钮可原位重试；存储或运行诊断读取失败也不再遮掉迁移检测入口。

旧单文件 Home `.build/mdo-packed-docks-1on15xha` 复现检测后失焦。候选单文件 Home `.build/mdo-packed-docks-ruev38id` 在 320×350 下正常检测后焦点保持；受控阻断一次 `/api/v1/migrations/legacy` GET 时，进行中按钮保持焦点和不可重复状态，失败后出现分组内错误，第二次检测恢复两张来源卡。扩展页的 Skill 刷新在重构后仍保持焦点，两个页面均无横向溢出，浏览器脚本错误为空。验证只使用隔离 Home 和本机服务。

Windows/Linux 有界门禁均通过 114 项 Python 测试、77 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过便携 WebView2 Home 及 20 秒启动。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `06d51492d785c47ecea0fc899ef7bad7f7d7c72da9762626d2ec1397e47f28c6`，Linux 包为 `fec23d58443ec9c5b9f892768c7071979466ad0d64750b3062a2e007110990ae`。实体触控和其他原生 WebView 仍待验收；未做压力或高负载测试。

## 2026-09-30：扩展目录刷新保留操作位置

设置页的 Skill、Module、MCP 操作在目录读取后会重建整个扩展列表。旧单文件包中点击 Skill“刷新”后，焦点落到页面根部；操作期间被重建的新按钮也不继承原按钮的禁用状态。现在给每个扩展操作稳定身份，重绘后把焦点还给同一按钮，用跨重绘的进行中集合拒绝重复触发。MCP 服务器操作消失时，焦点回到 MCP 分组的重载入口。单个目录读取失败只在本分组显示错误，其余分组及重试入口仍保留；读取失败不再显示刷新成功提示。

独立单文件 Home `.build/mdo-packed-docks-ow69bdtn` 验证 Skill 刷新、Module 重新编译及 MCP 重载后原按钮保持焦点。对 Skill GET 注入一次本地网络失败时，进行中的刷新按钮保持可聚焦但不可重复操作；失败后错误留在 Skill 分组、按钮可重试，第二次读取恢复 Project Explorer，浏览器脚本错误为空。此前明确会话 URL 三次有界刷新均正常恢复，未捕获偶发启动空壳，其根因继续待查。用户已确认便携优先；原生 WebView2 首次开窗写入 `mdo-home/data/cache/webview2`，发布门禁按此验收。

Windows/Linux 有界门禁均通过 114 项 Python 测试、77 个前端模块解析、21 个运行探针和确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒启动。根目录 `mdo.exe` 与 Windows 候选包 SHA-256 为 `dc4937aa4b4f76d29c1974cca6ec5e1980d2f4ab7db489e33f0e7aab242c9d7a`，Linux 包为 `7296cbfd7389b78edcbb208bf068e7fab808836d1d12808b0f328c3a1c474f99`。实体触控、其他原生 WebView 与偶发空壳故障当次证据仍待验收；未做压力或高负载测试。

## 2026-09-30：已有询问重排时保留输入焦点

上一阶段让询问卡按资源列表排序，但当两张已有卡交换位置时，协调逻辑会移动正在编辑的卡片本身。浏览器随即把焦点送回主输入框；答案文字虽在原卡，用户已无法连续输入。现在如果存活卡片内有焦点，就以该卡为锚点移动其他卡完成排序，避免把活动输入框或按钮从 DOM 摘下。

320×350 的 `tests/fixtures/ask-arrival-while-editing-browser.html` 在旧协调逻辑下复现 `existingOrderPreservesEditor=false`、焦点跳回主输入；修复后为 `true`，活动卡顺序正确，输入值、焦点和屏内位置保留。先前新增询问插入、审批到达/移除、主动滚动检查继续通过；同尺寸 `tests/fixtures/conversation-dock-scroll-browser.html` 全部通过，待办、审批与任务卡仍可读且文档不横向溢出。候选单文件 Home `.build/mdo-packed-docks-k9rrun0d` 完成 `ASK UI` 提交、工具结果与回复，浏览器脚本错误为空。已有卡交换的特定时序由浏览器夹具验证；打包页验证常规询问链未回归。

Windows/Linux 有界发布门禁均通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒启动。根目录 `mdo.exe` 与 Windows 候选包 SHA-256 为 `37b30dc60ec2497ee6a5e3e20eadf81b160904756ee4fc88e222f408cac9aa73`，Linux 包为 `60f552861d18a3f13b9734804df71d1b072101242024732df77a09afbb0bb9c3`。真实服务中的已有询问重排与实体触控仍待单独验收；未做压力或高负载测试。

## 2026-09-30：多询问按服务端顺序显示

多条待回答询问并存时，服务端返回的顺序可能因旧槽位复用而变化。此前前端只把新询问追加到末尾，导致页面顺序与服务端列表不一致；直接重建列表又会清空用户正在填写的答案。现在按服务端顺序协调卡片节点，只插入或移动确有必要的卡片，存活的询问输入框保持原节点。

320×350 的 `tests/fixtures/ask-arrival-while-editing-browser.html` 先用旧实现复现新询问落在末尾（`orderedArrivalPreservesEditor=false`），修复后新卡位于首位、原输入值与焦点保持，输入框在短屏停靠区中的位置变化不足 1px；已有的审批插入/移除、询问到达和主动滚动断言同样通过。候选单文件 Home `.build/mdo-packed-docks-n_m3g6ci` 走通 `SEQUENTIAL DECISIONS UI`：回答询问后出现审批，允许一次后本地无害命令得到 `exit_code: 0`，最终回复与用量显示正常，浏览器错误日志为空。服务端顺序变化的特定时序由浏览器夹具覆盖；这条打包链验证常规决策操作未回归。

Windows/Linux 有界发布门禁均通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒启动。根目录 `mdo.exe` 与 Windows 候选包 SHA-256 为 `949b93c51ea5bd4fc405c4ab79342efdddf52854041c263322f73dcb7ea848c4`，Linux 包为 `5b16c6eb871f3fe6476bec22a9f6ff2e0e943f3bbabb65608116127ae7a444ea`。实体触控、其他原生 WebView 及真实多询问交错仍需验收；未做压力或高负载测试。

## 2026-09-30：移除待办卡时保留后续任务卡

对话停靠区同时显示待办和后台任务时，待办列表清空会使前面的待办卡消失。旧协调逻辑在移除末尾旧节点之前，先把后面的任务卡插到前面；即使任务内容未变，卡片也被重挂，用户选中的任务文字随之消失。现先移除失效卡片，再补齐顺序；仍存在的任务卡保持原节点。渲染后只在原焦点已离开停靠区时恢复焦点，不再对仍获焦的控件重复调用 `focus()`。

320×350 的 `tests/fixtures/conversation-dock-scroll-browser.html` 在旧实现复现 `taskSelectionAfterTodoRemoved=false`，新实现为 `true`，其余停靠区断言及无横向溢出检查通过。`tests/fixtures/ask-arrival-while-editing-browser.html` 同尺寸复核询问编辑、审批到达和移除时的输入与滚动保持，全部通过。候选单文件 Home `.build/mdo-packed-docks-q9dlv16b` 走通 `TODO UI`、`ASK UI` 和选择 `Fast`：待办与询问同时显示，回答后待办仍在，询问结果及回复进入时间线，浏览器错误日志为空。真实页面链验证卡片共存与提交；任务文字选区的特定边界由浏览器夹具验证。

Windows/Linux 有界发布门禁均通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒启动。根目录 `mdo.exe` 与 Windows 候选包 SHA-256 为 `66d9b4a556bec195221f57b6e835a17e95af34e7eba7f392934526f01b881c9f`，Linux 包为 `94795db27cb73a0ef3a0662e84525d584ebb035f94ef2d45417c39f403295665`。按便携优先约定，原生 WebView2 首次开窗会在 `mdo-home/data/cache/webview2` 写入；实体触控和其他原生 WebView 仍需验收，未做压力或高负载测试。

## 2026-09-30：新增审批不打断已有参数阅读

审批检查器原先只在倒计时变化时复用卡片；新增另一条审批时，整张列表会 `replaceChildren`，把未变化的卡片也摘下再挂回，用户正在阅读的调用参数选区随之清空。另一条边界是前一张卡的参数发生变化时，后面未变化的卡也被移动，导致同样的中断。现在先移除真正失效的卡，再按服务端顺序补入新卡；保留的卡片和焦点不再无故重置。确实变化的卡片仍重新构建，以显示新的参数。

`tests/fixtures/decision-panel-stability-browser.html` 增加两条浏览器断言：选中第一张卡参数后追加审批，以及选中第二张卡参数后替换第一张卡。两项在旧实现均复现选区消失，在独立本地来源上用新实现通过；倒计时更新保留选区、内容变化时替换原卡的既有断言仍通过。候选单文件 Home `.build/mdo-packed-docks-3hloeonl` 走通真实审批入口：检查器展开参数后点“允许一次”，隔离本地打印命令返回 `exit_code: 0`，待审批数从 1 降到 0，回复和工具结果留在时间线，浏览器脚本错误为空。打包页这条操作链验证提交结果；多卡选区由独立浏览器夹具验证。

Windows/Linux 有界发布门禁均通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒启动。根目录 `mdo.exe` 与 Windows 候选包 SHA-256 为 `f28fc0e58d0e53a4f3791311ee5ab479a3c35db73d976fd60de1219eff4e418b`，Linux 包为 `dc48189c6ad36232764f040187d7b32acaca2297f0b18840207010fcf1e92418`。实体触控和其他原生 WebView 仍需验收；未做压力或高负载测试。

## 2026-09-30：计划运行轮询保留卡片选择与焦点

运行中的计划每秒读取一次列表。此前每次结果都清空整个列表并重建卡片：在单文件页选中“仅一次 · tasks · mdo.default”后，自动刷新会立即清掉文字选择，即使该行内容没有变化。现在按计划 ID 复用卡片和按钮节点，只更新确实变化的文本、徽标、禁用状态和错误提示；列表增删及排序仍与服务端结果同步。删除失败提示保持在原卡片内，刷新只隐藏旧提示，不移走卡片结构。

旧包的隔离 Home `.build/mdo-packed-docks-0alrrb6n` 复现了运行中选择丢失。新包 Home `.build/mdo-packed-docks-g_ynejxi` 使用本地 12 秒模型夹具运行同一计划：轮询 2.5 秒后及运行结束后，文字选择均保留，卡片仍获焦；280×250 下文档宽度等于视口，按钮可见且无横向溢出。另一个隔离 Home `.build/mdo-packed-docks-lg4h5yp8` 让首次合成删除返回 503：错误在原卡片显示且“删除”获焦；点击刷新后提示立即消失且焦点留在刷新，第二次删除得到空列表并聚焦刷新，浏览器脚本错误为空。合成删除只作用于代理视图。

Windows/Linux 有界发布门禁均通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒启动。根目录 `mdo.exe` 与 Windows 候选包 SHA-256 均为 `78de005ab03ff52e0536d59678a6ec9b78274d54187f3f674a47cbad90c05aaa`，Linux 包为 `959dd57d563fca995e7b4340ff0460aba55b36f50457218836878b53d1921573`。用户再次确认便携优先：原生 WebView2 首次开窗会创建 `mdo-home/data/cache/webview2`，现有配置及发布门禁已按此规则运行。实体触控和其他原生 WebView 尚需设备验收；未做压力或高负载测试。

## 2026-09-30：极短屏计划历史与启动空壳复核

根目录现有单文件 `mdo.exe` 在隔离 Home `.build/mdo-packed-docks-2idq5xbd` 的 280×250 页面中创建长名称计划，历史 GET 第一次由本地代理返回 503。弹层长标题自动换行，关闭按钮与错误说明、获焦的“重试读取”仍在可视区；重试后显示空历史并将焦点转给关闭按钮，关闭后焦点回到原计划“历史”。随后在同一隔离 Home 点一次“立即运行”，本地夹具完成该计划；再次打开历史得到一条真实的“已完成”记录，滚动弹层可读完整任务、运行标识和回复，关闭后焦点仍回原入口。浏览器脚本错误为空。空历史的长标题、关闭与焦点另在 `.build/mdo-packed-docks-5ehqgk0u` 复核。两次均只使用隔离 Home 和本机模型夹具，没有压力或高负载测试；实体触控仍需设备验收。

同一包的明确会话 URL 在 `.build/mdo-packed-docks-5ehqgk0u` 中做三次有界刷新，均进入 `data-mdo-startup=ready`、恢复原会话和可用输入；CDP 采集到 `/js/` 模块均为 200、没有网络加载失败或脚本异常。这几次正常刷新无法解释此前偶发的“资源 200、无初始化 API、停在新任务外壳”，因此空壳根因保持待查，不以本次未复现宣称修复。产品源码和打包字节未变，根目录 `mdo.exe` SHA-256 仍为 `ecec83e46649ddb42ed264cd92435e6fe658bc43e6fdf2caf3e355e10d5bc113`；该字节在上一阶段通过 Windows/Linux 有界门禁、确定性打包、便携 WebView2 Home 与 20 秒启动。

## 2026-09-30：极短屏计划操作错误留在原卡片

280×250 的旧单文件包 `.build/mdo-packed-docks-lcv14ou9` 在模拟删除失败后虽然聚焦原“删除”，常驻错误却在卡片上方、视口之外，底部临时提示还遮住按钮。现在列表操作错误出现在对应卡片的操作按钮之前；卡片不存在时仍在列表上方显示。删除失败不再叠加遮挡按钮的临时提示，重试或刷新会清除旧错误。暂停、启用和立即运行在请求期间由卡片承接焦点；完成后优先回到原操作按钮，若按钮已禁用则回到卡片，若刷新未确认结果则回到“刷新”。表单保存开始时也会清除过期的列表错误。

候选 Windows 单文件 Home `.build/mdo-packed-docks-0wtw_djd` 用 280×250 视口和只在代理视图生效的合成 DELETE 验证：第一次 503 后错误与原按钮同屏、按钮获焦；从它重试后列表为空、刷新获焦，浏览器脚本错误为空，代理没有向上游转发 DELETE。最终字节 Home `.build/mdo-packed-docks-99atxcep` 再次验证 280×250 删除失败后错误与原按钮同屏，点击刷新即清除旧错误，焦点留在刷新且脚本错误为空。此前候选包 `.build/mdo-packed-docks-6yq4n1qh` 还验证了暂停→启用后焦点仍停在同一卡片的切换按钮。极短屏长名称删除确认、取消回原按钮、删除相邻项后 Tab 到编辑及删空回刷新也在隔离包 `.build/mdo-packed-docks-snjo3c1e` 验证。Windows/Linux 有界门禁通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒打包启动。Windows 包 SHA-256 为 `ecec83e46649ddb42ed264cd92435e6fe658bc43e6fdf2caf3e355e10d5bc113`，Linux 包为 `ffd5482bfd63e80ffcd9d00631da75fa4c5b2a646408a864a937470d8ce14176`。实体触控仍待验收；未做压力或高负载测试。

## 2026-09-30：计划删除后保留可操作位置

计划列表在删除确认后会重绘。原流程把被删项的按钮移除后，没有指定新的焦点；请求失败时只把错误写在下方的长表单旁，用户需重新寻找原卡片。现在列表操作错误直接显示在列表上方：删除失败返回原卡片的“删除”按钮；成功后聚焦相邻计划卡，Tab 可到“编辑”；删除最后一项时聚焦“刷新”。请求进行中用可命名的计划卡承接焦点。列表重新读取失败时保留旧卡片、显示读取错误并恢复按钮可用状态，焦点交给“刷新”，避免卡片永久禁用或把旧数据误当作已经同步。

隔离候选包 `.build/mdo-packed-docks-1lr3d_yr` 用 `--simulate-schedule-delete --reject-first-simulated-schedule-delete` 验证取消、第一次 DELETE 返回 503、从原按钮重试、成功后跳到另一卡及删空回到刷新。代理没有转发任何计划 DELETE；上游 API 在测试结束前仍有原来的两项。第二个隔离包 `.build/mdo-packed-docks-3ejpn3q9` 在模拟删除成功后让第一次列表 GET 返回 503：页面显示读取错误和可操作的旧卡片，刷新按钮获焦；点击刷新后代理视图收敛为空。实际计划 DELETE 的 revision 和持久化语义由既有 API/运行探针覆盖，本次打包交互测试只验证前端焦点与错误恢复。Windows/Linux 有界门禁各通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` SHA-256 为 `a277d3d832e5e0f4a540528de868a921828ccc0e635a31fcbc57d325620901ea`，Linux 包为 `00506648daffd2465f71a57321a6d6dc11efa7a77d621d469f88f7c5bb3c3e70`。实体触控和极短屏的删除确认仍需验收；未进行压力或高负载测试。

## 2026-09-30：计划历史读取失败后原位重试

计划任务的历史弹层原先能显示空态和已有记录，关闭时焦点也能返回原卡片，但读取失败后只留下一行错误，用户必须关闭再打开才能重试。现在失败时在弹层内显示“重试读取”并聚焦；重试期间保留按钮位置、用 `aria-disabled` 防止重复发起，成功后显示记录、收起重试入口并将焦点转到关闭按钮。关闭弹层会使尚未完成的旧请求失效，避免它覆盖下一次打开的结果。新增文案随中英俄语言包提供。

隔离打包候选 `.build/mdo-packed-docks-zhosu__k` 用 `--fail-first-schedule-history` 将第一次历史 GET 返回 503：页面显示错误、重试按钮获焦；点击后第二次 GET 读取预置的 `任务 #7 · 运行 #107` 记录，状态变为“1 条记录”、焦点在关闭按钮。空历史与三条已有记录在修复前的打包页也正常显示，关闭后焦点回到原“历史”按钮。Windows/Linux 有界发布门禁各通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒打包启动。根目录 `mdo.exe` 已更新，SHA-256 为 `88a73b22172b5852489cfdfec94829c62f9b416be93648a121fb5896ca015e2a`，Linux 包为 `012322d90305bbd5001ad000c984bffebcbaabc3b0ead780b5f527f1f3d14aa3`。极短屏历史弹层、计划删除后的焦点和实体触控仍需分别验收；未进行压力或高负载测试。

## 2026-09-30：计划创建与编辑后回到结果卡片

移动设置页的计划表单很长，保存按钮位于列表下方。旧单文件 Home `.build/mdo-packed-docks-l1x92e7y` 在 320×350 创建和编辑计划均成功，但视口留在表单底部，结果卡片位于 y=-924～-734，焦点落到页面主体；用户看不到刚保存的计划，也失去键盘操作位置。现在仅在创建或编辑成功、当前计划页仍可见时，把已保存计划卡移入视口并聚焦。卡片具有可读的名称和可见焦点轮廓；随后的列表重绘保持卡片焦点，Tab 顺序直接进入“编辑”。若列表刷新后找不到该卡片，焦点落到刷新入口，避免无反馈地回到页面主体。

与最终包字节相同的候选包在隔离 Home `.build/mdo-packed-docks-wlz8cwe4` 实际创建远期计划、编辑名称：两次保存后 320×350 的结果卡片均位于 y=108～298，焦点在对应卡片，Tab 到“编辑”；280×250 再创建第二项，卡片标题从 y=100 可见，Tab 后“编辑”按钮位于 y=188～228、高 40px。两个尺寸无横向溢出，浏览器脚本错误为空。Windows/Linux 有界门禁各通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针和确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` 已更新，SHA-256 为 `750b1f8e0ef981910c8efbc917c1d9973f9bf38634d96ed99f026e1df27bb9a9`，Linux 包为 `fc240d51d0fd199bc1ba447bbbffc5312dbe51729ad5381525a6f9f02536edf8`。计划删除、历史弹层和实体设备触控仍需单独验收；未进行压力或高负载测试。

## 2026-09-30：慢详情重试后立即恢复待发派发

直接打开已有会话时，初次会话详情 GET 与运行状态、恢复状态和待发队列一起持有派发闸门。若详情 GET 很慢，用户通过“重试读取”取得当前详情后，旧 GET 仍可能占住初次选择的闸门：旧单文件 Home `.build/mdo-packed-docks-ziu2lj_5` 将前两次详情 GET 延迟 60 秒，重试成功后消息在单调时间 1727329.109 入队，运行 POST 直到 1727345.515 才发出，相隔约 16.4 秒。现在初次选择观察资源仓库的**当前**详情状态，仍须等待运行/恢复/待发核对，但不等待已被重试取代的网络请求；路由离开时解除观察，旧请求完成也不能覆盖较新详情。重试进行中按钮保持可见且获焦，显示“正在重试…”并阻止重复触发；再次超时或失败后仍可重试。

与最终发布字节相同的候选包在隔离 Home `.build/mdo-packed-docks-mvb8fml1` 使用同样的 60 秒延迟：第三次详情 GET 成功后，`GATE UI` 的队列 POST 于 1727652.890、唯一运行 POST 于 1727652.984 发出；两条旧 GET 到 1727666.046/1727666.109 才返回。回复、用量和可用输入均在页面出现，浏览器脚本错误为空。另一个候选 Home `.build/mdo-packed-docks-4wi9pqju` 验证第三次详情 GET 也被延迟时，按钮仍可见、`aria-disabled=true` 且保持焦点。定向用例覆盖旧请求迟到、路由离开、重复重试和失败后的焦点。Windows/Linux 有界门禁各通过 114 项 Python、113 项 Node、77 个前端模块解析、21 个运行探针和确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` 已更新，SHA-256 为 `f5b15c9ee846f7e62f30a2ffd68e593ec9b52de0c181fe6617bbaeb43c52348a`，Linux 包为 `8471b3a17a24e77da0b7071740d1e342896b56691188790e679110e35bf45e2a`。未进行压力或高负载测试；此前静态资源均返回 200 的偶发空壳仍缺少故障当次证据。

## 2026-09-30：明确会话读取失败时显示恢复入口

直接打开已有会话时，前端原先会先将启动状态设为 `ready`，但会话详情请求仍在进行。旧单文件包让最初两次详情 GET 返回 503 后，标题仍是“新任务”、欢迎页仍可见、输入框禁用，只在输入区留下原始错误；用户无法从页面重试，看起来像载入了空壳。现在明确会话的详情尚未就绪时，在对话区显示载入状态并隐藏欢迎页；读取失败时给出三语说明和“重试读取”，超过 20 秒时也提供重试入口。重试保持当前会话路由和草稿，成功后恢复原会话标题、输入能力与焦点；失败后焦点留在重试按钮。详情返回不属于当前路由时显示错误，不把错误数据当作已载入会话。

最终 Windows 单文件包在隔离 Home `.build/mdo-packed-docks-doze6jm9` 中拒绝前两次详情 GET：页面处于 `error`，显示恢复卡片；第三次读取成功后回到“Packed docks QA”，输入框启用并获焦，浏览器脚本错误为空。另在 320×350、连续失败的候选包验证卡片可见、重试按钮高 40px、文档宽度 320px，重试再次失败时按钮仍获焦；30 秒延迟夹具观察到 `loading` 与 `delayed` 状态。定向用例验证状态、超时、路由切换和焦点。Windows/Linux 有界门禁各通过 114 项 Python、111 项 Node、76 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒启动观察。根目录 `mdo.exe` 已更新，SHA-256 为 `f38f3c00e7f3a6389663dfdf6e0236606f7dc3c76785b8ee900d3263e91a9956`，Linux 包为 `43502df789fb6623259cc0977ce0aafefac44a42ed01137c00ea8efa900f95ca`。此项只修复受控会话详情失败的反馈与恢复；此前静态资源均返回 200 的偶发空壳仍缺少故障当次证据。未进行压力或高负载测试。

## 2026-09-30：工具回合统计覆盖全部模型调用

旧版回复区可直接看到回合与速度信息。新时间线在工具调用前的模型响应没有正文时不会创建回复卡，之前因此只把工具调用后的最后一次 `model_done` 的用量写到最终回复：真实单文件夹具两次模型调用各返回 7 输入、3 输出 tokens，页面却只显示 7/3。现在从同一 `agent_start` 到 `agent_done` 累加各次模型调用的用量和有真实起止事件的模型耗时，在最终回复标明调用次数，并以总输出量除以总模型耗时计算 tok/s。单次调用仍保持简洁的原有显示；只有完成事件携带正文、没有流式增量时，也能拿到可证实的用量。缺少运行起点时不冒充本轮合计；任一模型计时不完整时不展示与累计用量口径混杂的 LLM 耗时和速度。

最终 Windows 单文件 Home `.build/mdo-packed-docks-by_32g2b` 发送 `ARTIFACT UI`，一次只读 `read` 引发两次模型调用，页面显示“本轮 2 次模型调用 · 14 输入 / 6 输出 tokens · LLM 0.6 秒 · 工具 <0.1 秒 · 10.4 tok/s”；刷新后保持一致。320×350 视口的文档宽度为 320px，七个消息按钮均高 40px，浏览器脚本错误为空。定向用例覆盖工具调用前无正文、复用运行 ID、缺失运行起点、缺失计时起点及仅有完成正文的情况。Windows/Linux 有界门禁均通过 114 项 Python、75 个前端模块解析、21 个运行探针和确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` 已更新，SHA-256 为 `839015b29b2c14ccd9b4f86cff479b56b33576792b9e322f43365a26c775b170`，Linux 包为 `4f099b3f7cbd76f52a2ed0d98e84d8761b7fda4ae4bc64af5c43426b28e5df01`。实体移动端和其他原生 WebView 仍待验收；未进行压力或高负载测试。

## 2026-09-30：回复操作栏恢复工具耗时

旧版回复统计会汇总工具耗时；新时间线虽然在各工具卡上有起止时间，却没有把它放回回复操作栏。现在按运行及启动轮次累加有真实 `tool_start`/`tool_done` 配对且成功的工具耗时，在 `agent_done` 时只附到该轮最后一条助手回复。失败工具、缺失起点的截断事件和后续复用同一运行 ID 的轮次均不混入统计。LLM 与工具耗时共用三语格式；小于 0.1 秒显示为“<0.1 秒”。

Windows 候选单文件 Home `.build/mdo-packed-docks-g40sqaul` 用本地模型触发一次只读 `read`，最终回复显示“7 输入 / 3 输出 tokens · LLM 0.3 秒 · 工具 <0.1 秒 · 11.3 tok/s”。320×350 视口文档宽度为 320px，浏览器脚本错误为空。定向用例验证失败工具、孤立完成事件及同一运行 ID 的下一轮不会被误计。Windows/Linux 有界门禁各通过 114 项 Python、105 项 Node、75 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` 已更新，SHA-256 为 `be09ee12f4d6a97636c39ae860067c5ac3cdfab32776462ff3de336159f8be74`，Linux 包为 `94bc51e7bcd409bcfe0570e2207506db66bc16bf34ac8187ee5aef6a352be1ca`。实体移动端和其他原生 WebView 仍待验收；未进行压力或高负载测试。

## 2026-09-30：回复统计恢复 LLM 耗时

旧版助手回复把 LLM 耗时与 token 用量、生成速度放在同一处；新时间线已有 `model_start` 和 `model_done` 的时间戳，却只用它计算 token/s。现在有完整起止事件时，回复操作栏一并显示 LLM 耗时。事件窗口缺少 `model_start` 时不凭首条可见文本推造耗时；原有 token/s 回退计算保持可用。低于 0.1 秒的真实耗时显示为“<0.1 秒”，避免四舍五入后的“0.0 秒”。

Windows 候选单文件 Home `.build/mdo-packed-docks-t31lqomo` 使用本地夹具的 250 毫秒模型延迟，页面显示“7 输入 / 3 输出 tokens · LLM 0.3 秒 · 10.7 tok/s”；320×350 视口文档宽度仍为 320px，统计文字留在卡片内，浏览器脚本错误为空。模块用例覆盖交错事件的精确耗时和截断事件窗口不显示虚构耗时。Windows/Linux 有界门禁各通过 114 项 Python、104 项 Node、75 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒打包启动。根目录 `mdo.exe` 已更新，SHA-256 为 `749522d7997866a9d9827334b8c3d6447d5694a0f552dd25b3cd8c05924c3246`，Linux 包为 `d3b4bb5b3e66bae9a43c75dfe912a7238de7aac239cdd579b2892891f08be2c5`。实体移动端和其他原生 WebView 仍待验收；未进行压力或高负载测试。

## 2026-09-30：启动失败层在语言模块不可用时仍可读

入口模块导入失败时，语言模块同样不能执行。旧单文件页虽会显示可重试错误层，但标题、说明和按钮始终是中文；即使语言包后来载入，失败层也没有翻译标记。现在内联启动脚本自带中、英、俄三种最小恢复文案：已完成设置语言读取时优先用配置语言，否则按浏览器首选语言回退，不依赖任何模块或 API。失败层的静态元素同时接入语言键，使超时期间迟到的语言包仍可更新文案。错误层显示时，窗口标题也与其语言一致；重新载入仍保留当前 hash 路由。

最终 Windows 单文件包在隔离 Home 首次拒绝 `/js/main.js` 后显示中文错误层。随后只在测试标签页阻断该模块，通过浏览器语言覆盖分别模拟 `en-US` 与 `ru-RU`；英语显示 “Mdo could not start” / “Reload”，俄语显示 “Не удалось запустить Mdo” / “Перезагрузить”，两者窗口标题均为 `Mdo`。解除阻断并点击俄语重试后，页面回到原 “Packed docks QA” 会话，`data-mdo-startup=ready`、输入框可用。此项验证的是入口请求明确失败与重试；先前静态资源均返回 200 的偶发空壳仍未获得故障当次根因。

Windows/Linux 有界发布门禁各通过 114 项 Python、103 项 Node、75 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` 已更新，SHA-256 为 `6849e065f0fe603b0aeed4bed1ead33d92691b52a63ab5177a1edb898fe8d1b7`，Linux 包为 `4af65bd1eeba9dfdd8004b80add3635df4cb0a9275efc7b947afdb70ede9957c`。未进行压力或高负载测试。

## 2026-09-30：慢侧栏下的双轮待发复核

上一阶段移除了从设置返回时侧栏会话列表对队列闸门的等待。本轮进一步检查“第一轮结束后，第二条待发是否会被运行轮询中的列表刷新拖住”。单文件包用 `--subsequent-sessions-delay-ms 30000 --slow-ms 12000` 打开明确会话，先发送 `SLOW UI`，运行中再排入 `FOLLOWUP UI`。第一次运行 POST 为单调时间 1721445.296，第二条队列 POST 为 1721445.500；第一次运行 GET 捕获成功后，第三次 `/sessions` GET 于 1721458.140 开始等待，但第二次运行 POST 已在 1721458.187 发出。页面最终留下两轮完整消息和各自的 7 输入 / 3 输出用量、token/s，代理总计两次队列和两次运行 POST，浏览器脚本错误为空。

这项结果否定了“运行轮询中的侧栏等待必然拖慢下一条”的怀疑；全局运行状态订阅另有派发路径。没有为此删除轮询里的刷新，也没有改变产品代码。320×350 的打包欢迎页与常规设置页还用实际布局测量了视口内启用控件，未发现小于 40×40px 的目标；实体触控、软键盘及其他设置分区仍按审计清单保留。此轮使用与上一阶段相同的程序字节，Windows/Linux 发布门禁结果仍见上一节；未进行压力或高负载测试。

## 2026-09-30：返回会话不等待侧栏列表

从设置返回原会话会重新读取会话详情、待发队列和侧栏列表。侧栏列表只用于导航展示，却在同会话刷新分支中被 `await`，令队列闸门一直保持到它返回。隔离旧单文件包把首次之后的 `/sessions` GET 各延迟 30 秒：设置返回触发的第三次 GET 于单调时间 1720857.609 开始，消息于 1720860.765 入队，运行 POST 直到该 GET 在 1720887.609 返回后才于 1720887.671 出现。

现在仍读取会话详情并核对持久待发状态，同时在后台刷新侧栏列表。相同代理下，候选包的第三次列表 GET 于 1720993.218 开始；消息在 1720996.328 入队、1720996.421 启动运行，未等待列表返回。页面显示固定回复、7 输入 / 3 输出 tokens 与 token/s，浏览器脚本错误为空，代理记录一次队列与一次运行提交。该验证覆盖返回同一会话且侧栏响应缓慢的路径，不代替其他路由或网络失败的验收。

Windows/Linux 有界发布门禁各通过 114 项 Python、103 项 Node、75 个前端模块解析、21 个运行探针和确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` 已更新，SHA-256 为 `0f6b3bb98d167a7a257244e3a4e3e26cf7c04848506afcecacc3a224f5b616c2`，Linux 包为 `c29e4ec733a09ad3a400aa88baef7b90628de6ece9cd99c022a4cb7b43ce3ec3`。未进行压力或高负载测试。

## 2026-09-30：运行列表首次失败时等待核实

慢读取闸门修复后，另一条失败路径仍会提前派发：`createResourceStore.load()` 把全局 `/runs` 的 503 记为 `error` 并返回，启动流程因此正常释放运行核对闸门；待发派发器又把仓库的初始空数组当成已核实的空运行列表。旧打包版受控复现中，首次 GET 于单调时间 1718341.390 返回 503，队列 POST 为 1718346.265，运行 POST 为 1718346.359，早于第二次运行列表 GET 的 1718349.421 返回。

派发器现在要求运行列表状态为 `ready`，才允许从待发队列启动运行。失败时消息继续持久入队，输入区提示将等待自动重试；现有 8 秒运行状态轮询在成功后重新触发派发。候选单文件包把前两次 GET 各延迟 30 秒，并让首次返回 503：队列 POST 为 1719890.453，首次 GET 于 1719904.437 失败，第二次 GET 于 1719942.453 成功，运行 POST 才在 1719942.500 出现。最终只有一次队列和一次运行提交，页面显示固定回复、7 输入 / 3 输出 tokens 和 token/s，浏览器脚本错误为空。这个实验验证了首次读取失败后的等待与恢复，不把它当作所有网络异常场景的证明。

Windows/Linux 有界发布门禁各通过 114 项 Python、103 项 Node、75 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒打包启动。根目录 `mdo.exe` 已由 Windows 门禁包更新，SHA-256 为 `84cf9ffc3eeba77ee800b95caf909820d0c0cc93e2feaf0ca002fbd7028d90bb`，Linux 包为 `6b0fc358fc8f4d993bec53ba8c00d5c0b10e7509fe13a7f65fcb7c1666b92d90`。未进行压力或高负载测试。

## 2026-09-29：Markdown 导出补齐长事件正文

Markdown 导出原先直接使用每条最多 4,096 字节的事件列表，即使会话日志保留了完整正文，导出也会留下截断片段并提示“日志截短”。导出现在先分页读取最多 4,096 条事件，再按精确事件 ID 补读需要写入 Markdown 的截断正文；最多补读 64 条、累计 2 MiB。取回的事件必须与原 ID 和类型一致且明确未截断。失败、源日志已裁剪、单事件超过 64 KiB 或导出预算用尽时，仍保留可见前缀并在文件开头提示“部分事件正文未能完整导出”。思考片段本来不进入 Markdown，因而不消耗补读预算，也不单独触发该提示。普通分页事件接口的 4 KiB 上限没有改变。

Node 用例验证长用户消息和长模型正文补齐后再排版，以及服务端返回相邻事件时拒绝误用并保留不完整警告。最终 Windows 单文件 Home `.build/mdo-packed-docks-yezqykt2` 从页面发送 4,217 字符合成消息，点击顶栏“导出 Markdown”时，浏览器网络记录同时出现普通事件页和 `full_text=1` 精确事件请求，页面显示“Markdown 已开始下载”。同一打包服务的数据经前端导出模块独立生成 4,413 字节 Markdown，包含完整原文、无不完整警告。内嵌浏览器未提供可读回的下载文件路径；本轮不把磁盘下载正文列为已验证，既有短消息的 Edge 落盘验收仍见此前记录。

Windows/Linux 有界门禁各通过 114 项 Python、101 项 Node、75 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒打包启动。根目录 `mdo.exe` SHA-256 为 `87cdf41f354b5b1228fdfce5e726c4ebe41bf3d6ac6dfc6bd4e590e0281640c1`，Linux 包为 `3660c95692d80e03068fbfe22c5f803442afd17343688176caa0d998ca2aea08`。实体移动端与其他系统原生 WebView 的下载行为仍待验收；未做压力或高负载测试。

## 2026-09-29：长消息恢复编辑与重试

上一阶段的单事件全文读取已让截断消息可以复制完整正文，但时间线仍隐藏这些用户消息的“编辑”和对应模型回复的“重试”：直接拿 4 KiB 可见前缀去执行历史截断会丢失原消息尾部。现在两项入口重新显示，点击时按用户 `agent_start` 的精确事件 ID 取回全文；无法取得、仍被截断或事件身份不匹配时拒绝操作并提示历史未改变。点击时先记录会话路由版本，在异步读取后再次验证，避免切走又返回时对旧点击执行操作。编辑仍在打开弹窗前检查草稿、队列和运行状态；提交与重试继续使用原有的强 ETag 历史边界与二次检查。

Node 定向用例验证长用户消息与其回复共享正确的取回片段、全文可用于编辑和重试、取回失败不会交出可提交的文本。Windows 最终单文件 Home `.build/mdo-packed-docks-492b0130` 从页面发送 4,217 字符的 UTF-8 用户消息：编辑弹窗读回的内容与原文逐字相同，取消后历史保持；点击回复的重试后，时间线只有一条用户消息，复制所得仍与原文相同；再编辑添加 ` EDITED` 并提交，刷新后复制得到完整修改文本。浏览器脚本错误为空。320×350 视口下页面宽度 320px、用户消息复制和编辑按钮均为 40×40px。此证据覆盖有界本地模型夹具的单轮长消息；取回失败的服务端路径由定向用例与 API 合同覆盖，尚未在单文件页面注入失败。

Windows/Linux 有界发布门禁各通过 114 项 Python、99 项 Node、75 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒打包启动回归。根目录 `mdo.exe` SHA-256 为 `2192151dab287bee27244b19ac6b8a15e749b89ef2d0a862058edc32156c3965`，Linux 包为 `70668330c11a4d025fea2dec254a10c8eac2ca633560838d871ac4c80e2c2d89`。实体手机与其他系统原生 WebView 仍待验收；未做压力或高负载测试。

## 2026-09-29：长消息按需复制完整事件正文

前一阶段让截断消息如实提示“仅复制可见部分”，但用户仍须导出会话才能取得完整原文。会话事件列表继续对每条正文保留 4,096 字节上限；仅 `after=<event_id-1>&limit=1&full_text=1` 可按需读取一个事件，正文上限 65,536 字节。全局运行事件不接受此选项，缺少精确游标、批量请求、重复或非法参数均返回 400。前端要求返回的事件 ID、类型与目标一致且服务端明确未截断，否则回退为可见部分，避免日志裁剪后误复制相邻事件。

时间线记录被截断的用户事件和模型正文分片在拼接文本中的位置。复制时只补读这些分片，最多 16 条、结果最多 1 MiB；若任一读取失败、仍截断或前缀不符，整次复制退回原有可见文本并显示“已复制可见部分”。提示现说明会尝试取回全文，普通短消息仍走原复制路径。编辑和重试截断用户消息的限制没有改变，不能因为复制成功就把可见前缀当成完整可编辑原文。

真实 API 探针以跨越 4 KiB 边界的 UTF-8 用户消息验证列表截断、单事件全文、完整 ID 与非法查询拒绝；Node 用例覆盖用户消息、交错模型分片、失败回退和总复制长度上限。Windows 候选的隔离单文件 Home `.build/mdo-packed-docks-y52zmarl` 从页面发送 4,217 字符合成消息，时间线仍显示截断提示，点击“复制消息”后剪贴板读回完整原文并显示“消息已复制”；随后只补上总长度上限分支，最终包与该候选的复制逻辑相同。Windows/Linux 原生文件系统有界门禁各通过 114 项 Python、98 项 Node、75 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒打包启动。根目录 `mdo.exe` SHA-256 为 `c8f83ec3ccc02b6e45a1708aaf1f93b735313e3c5369195951267c0dd9ff453e`，Linux 包为 `25c565274f7059635e9ec9fc77f265f8642b4231db4243ba825ed30225c7b683`。超过单事件上限或原始日志已被裁剪时仍只能复制可见部分；实体移动端和其他系统原生 WebView 尚未验收。未做压力或高负载测试。

## 2026-09-29：截断消息的复制语义与可见提示

时间线事件 API 每条最多返回 4,096 字节文本；隔离旧单文件 Home `.build/mdo-packed-docks-ae4p6iqq` 发送 4,219 字节的合成消息后，服务端事件明确给出 `text_truncated=true`、`original_text_bytes=4219`，但页面只显示 4,096 字节且仍把按钮标作“复制消息”，点击后提示“消息已复制”。会话 JSON 导出中仍有完整原文。这会让用户误以为复制到了整条消息。

时间线现对截断的用户消息和模型文本分片保留不完整标记，在正文后显示“消息内容有截断；复制仅包含可见部分。”，并把复制按钮和成功提示改为“复制可见部分／已复制可见部分”；完整消息的复制文案不变。三语词典同步更新。Node 用例覆盖用户消息和模型回复分片的截断投影；原有 Clipboard API 与回退路径保持不变。

候选单文件 Home `.build/mdo-packed-docks-aaxvknbt` 中，同样的长消息显示 4,096 字节、截断提示及“复制可见部分”按钮；点击后出现“已复制可见部分”，普通 Agent 回复仍为“复制消息”。320px 视口下按钮 40×40px、文档宽度 320px、浏览器脚本错误为空。最终文案从“只显示消息开头”进一步改为“消息内容有截断”，以准确覆盖模型文本中间分片被截断的情况；发布门禁生成物将包含该文案。此项明确复制范围，没有把 4,096 字节预览扩为完整消息读取。

最终单文件 Home `.build/mdo-packed-docks-0pijbqhy` 重验长消息显示最终文案、“复制可见部分”按钮及点击反馈；普通回复仍为“复制消息”，320px 页面宽度 320px、按钮 40×40px、脚本错误为空。Windows/Linux 有界门禁各通过 114 项 Python、96 项 Node、75 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒打包启动回归。根目录 `mdo.exe` SHA-256 为 `e3987a8f6c653227b281b5c4e89bb2f9c4cfb56acdb6ef06edade945a6219e5c`，Linux 包为 `565211ae47404723a8cf7b87d8d2adfdd2909482af353de7e5625b67fb94db94`。未做压力或高负载测试。

## 2026-09-29：极短屏设置分区与未应用配置回访

最新 Windows 单文件包在隔离 Home `.build/mdo-packed-docks-x0oh3ttx` 中，使用 280×250 浏览器视口依次打开常规、Agent、联网与搜索、项目、计划任务、模型、反馈、扩展与 MCP、权限、诊断与存储十个设置分区。每个分区的 URL 与焦点均落在所选分类，文档宽度始终为 280px；从诊断页返回原会话后，输入框重新获焦，浏览器脚本错误日志为空。

390×600 视口进一步在 Agent 页把默认推理强度从“中”改为“高”，未应用时切到模型资源页，页面显示“返回未应用的设置”；点击该入口回到 Agent 后选择仍为“高”，放弃更改又恢复“中”，配置 revision 保持 1。这项验证覆盖短屏分类导航和跨资源页的未应用表单状态；没有提交配置，也不代替各分区的完整增删改操作或实体手机软键盘验收。

## 2026-09-29：配置创建任务跟随目标项目默认模型

“配置后创建任务”弹窗原先允许直接编辑项目标识，但切到另一个已登记项目时，模型仍沿用打开弹窗前输入区的选择。隔离旧包中，`beta` 项目的默认模型设为 Ling Text QA；从默认项目打开弹窗并把项目改成 `beta` 后，模型仍为 Ling 3.0 Tiny，继续创建会覆盖目标项目默认值。

项目字段现在使用原生 `datalist` 提示已登记的项目 ID。输入完整已知 ID 时，或离开任意项目 ID 字段时，弹窗按所选 Agent 的显式声明优先、再按目标项目默认模型及内置默认值更新模型、思考和权限；用户随后仍可手动改选。目录、默认值和输入事件已从 `app.js` 抽到 `features/chat/new-session-profile.js`，Node 回归覆盖项目联动、Agent 优先级、未知项目回退和手动选择。

隔离单文件 Home `.build/mdo-packed-docks-pltbs_pt` 中，从默认项目改选 `beta` 后弹窗立即显示 Ling Text QA；创建出的会话归属 `beta`，服务端模型为 `ling-3.0-tiny-text-qa`。同一 Home 再手动选择 Ling 3.0 Tiny 创建第二会话，服务端保留手选模型。模块化后的最终候选 Home `.build/mdo-packed-docks-wxyhazys` 重验项目联动和两个原生项目建议；320×350 弹窗宽 286px、创建按钮完整可达，文档宽 320px，脚本错误为空。

Windows/Linux 有界发布门禁分别通过 114 项 Python、95 项 Node、75 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过便携 WebView2 Home 的原生窗口检查和 20 秒打包启动回归。根目录 `mdo.exe` 已更新，SHA-256 为 `d3590b87e5dbc73074c55209672637a6867c16c8ddede4787d8546e76353f55f`；Linux 包为 `ff7f3ed35fb9633f3c57de6c60575622121570a28d2bf859c21d84ae8931b38d`。未做压力或高负载测试。

## 2026-09-29：新任务配置可恢复项目默认值

上阶段让模型、思考和权限的显式覆盖随项目草稿保存，但页面没有办法删除这些选择。即使后来调整了项目或默认 Agent，用户也只能编辑 Home 文件才能重新继承默认值。现在新任务输入区在存在任一显式覆盖时显示“跟随项目默认配置”按钮；清除项目草稿中的 `composer_profile` 成功后，三个控件重新使用当前默认值。按钮在会话页隐藏，保存或创建期间不可操作。清除失败时保留先前选择与可重试入口，仍显示草稿保存错误。成功后焦点移到模型选择器。

Node 回归验证跨项目覆盖隔离、清除后随 Agent 默认值变更、刷新后继续继承，以及焦点交接。隔离单文件 Home `.build/mdo-packed-docks-jhp_5f77` 在新任务页选只读后立即清除，界面回到询问权限且模型选择器获焦；刷新后仍为默认权限，按钮不再出现，浏览器脚本错误为空。最终候选 Home `.build/mdo-packed-docks-xnfu2hnd` 在 320×350 视口验证按钮 40×40 可见、清除后模型选择器获焦、文档无横向溢出且脚本错误为空。

Windows/Linux 有界发布门禁分别通过 114 项 Python、93 项 Node、74 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过便携 WebView2 Home 的 20 秒原生窗口检查和打包崩溃回归。最初并行运行浏览器夹具时，Windows 窗口自动关闭检查曾超时；停掉夹具后独立重试以及完整门禁均通过，没有修改产品代码。根目录 `mdo.exe` 已更新，SHA-256 为 `0c802dfbc15cf58f4aa406f80bf78e9129f9fd1a97db1c83a0bd6be2458b9399`；Linux 包为 `b3322bcf1ec45ad4f40e13e2c353bae6a44acb47fc8aa7a0b36ac3ebb6a72d92`。未做压力或高负载测试。

## 2026-09-29：未发送的新任务配置随项目草稿恢复

新任务的模型、思考强度和权限原先只保存在前端内存。旧单文件页选择第二模型、高思考、只读后刷新，三项全部回到 Ling 3.0 Tiny、中思考、询问，虽然同项目文字草稿可恢复。项目草稿现用 `composer_profile` 保存三个字段的**显式覆盖**：空字符串表示继续跟随默认 Agent；用户只改权限时，不会把当时的默认模型和思考强度暗中冻结。项目草稿文件升至 schema 8，继续读取 schema 7；全局创建日志、会话草稿与提交快照仍为 schema 7，且继续要求完整配置。保存中和失败状态在输入区显示三语提示。

候选单文件 Home `.build/mdo-packed-docks-lcivat99` 将第二模型、高思考、只读写入 `data/project-drafts/default.json`，磁盘文件为 schema 8。刷新同一个新任务 URL 后三项均恢复，输入框获焦，页面无横向溢出或浏览器脚本错误。Node 回归另验证只选权限时模型仍随默认 Agent 改变、两个项目的覆盖互不串用；真实 HTTP/TCC 探针验证部分配置往返、清除、非法值拒绝、旧 schema 7 项目草稿可读，以及全局/会话草稿仍拒绝不完整快照。

320×350 候选单文件 Home `.build/mdo-packed-docks-yb2tphjk` 只把权限改为只读：保存期间输入区显示“正在保存新任务配置…”，完成后刷新仍为只读；模型和思考保持默认 Ling 3.0 Tiny / 中，输入框获焦，页面无横向溢出或浏览器脚本错误。Windows 有界发布门禁通过 114 项 Python、93 项 Node、74 个前端模块解析、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 与 20 秒单文件启动；根目录 `mdo.exe` SHA-256 为 `efdcb617f4ebac6c498e8a89dff6f9dee3d8482823acf0746fdfe907c0d654ab`。Linux 在 WSL 原生文件系统的独立拷贝通过相同有界门禁（跳过 GUI），包 SHA-256 为 `2d27d53c85ca726aca38023864411e5d7460292833c402115f52a0b501582d53`。未做压力或高负载测试。

## 2026-09-29：显式会话不等待上次选择读取

即使 URL 已明确指向一个会话，启动导航原先仍先等待 `GET /api/v1/workspace-state`。若这条仅供“继续上次任务”使用的请求很慢，原会话的输入区会停在 `loading`。现在先订阅会话选择并启动读取；显式 URL 直接继续恢复会话，读取完成后才比较和保存最后选择。没有显式路由时仍等待读取，以保留“上次任务 / 新任务 / 询问”的启动选择语义。

隔离代理延迟首次工作区状态 GET 30 秒：旧包在明确会话 URL 下保持 `loading`，候选包在请求未返回时已进入 “Packed docks QA”，状态为 `ready`，输入框可用且获焦。请求返回后 `data/workspace-state.json` 记录了该会话，界面与焦点保持，浏览器错误日志为空。Node 回归验证启动先于慢 GET 完成，且读取旧选择后仅保存当前选择。代理的连接等待队列也由默认 5 调到 128，避免它在大量并发模块请求时制造连接拒绝。

排查此前偶发刷新空壳时，CDP 在旧队列大小的代理上抓到 `/js/state/resources.js` 的 `net::ERR_CONNECTION_REFUSED`；入口动态导入失败，现有启动层显示 `failed`。增大代理队列后，两次有界刷新均恢复原会话、无脚本错误。该证据说明代理可产生相似现象，不证明此前静态资源均返回 200 的那一次同源；原生 WebView2 对照和失败当次完整瀑布仍需继续。

Windows 有界发布门禁通过 114 项 Python、92 项 Node、74 个前端模块解析、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 与 20 秒单文件启动；根目录 `mdo.exe` SHA-256 为 `169099a7e9a797e5f20de943eeb5ce0ca50f1ce2f78289aeb946b6998bccfc63`。Linux 在 WSL 原生文件系统的独立拷贝通过相同有界门禁（跳过 GUI），包 SHA-256 为 `92e90e7378a3f847777dd42b840938a1ec0133685ebca106283c377764eca745`。未做压力或高负载测试。

## 2026-09-29：会话恢复不等待任务与管理资源

启动时原先把任务列表、审批列表和设置管理资源与会话目录、模型、设置及恢复状态放进同一个 `Promise.allSettled`。任何一个无关请求慢下来，明确的会话 URL 都要等它结束才能完成启动。现在仍并发加载这些资源、由各自订阅视图更新，但只等待恢复会话所需的资源后就执行工作区导航；任一初始资源真正拒绝时，原有的部分载入提示只显示一次。

隔离单文件代理只延迟首次 `GET /api/v1/tasks` 30 秒。旧包打开明确会话 URL 后仍是 `data-mdo-startup=loading`；候选包在请求尚未结束时已是 `ready`，标题恢复为 “Packed docks QA”，输入框可用且获焦。慢请求结束后会话和焦点保持，浏览器错误日志为空。测试没有触发任何创建、队列提交或运行 POST。这个修复针对无关 API 拖慢正常启动；此前静态资源均返回 200 的偶发空壳仍须捕获失败当次模块执行状态，并与原生 WebView2 对照。

Windows 有界发布门禁通过 114 项 Python、91 项 Node、74 个前端模块解析、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 与 20 秒单文件启动；根目录 `mdo.exe` 已更新，SHA-256 为 `4a632bcca0e79d8e7795686a25b282ecf5475818a684b9f1d523367ffd482f65`。Linux 在 WSL 原生文件系统的独立拷贝通过同一有界门禁（跳过 GUI），包 SHA-256 为 `5a427d4aaa3a0b6b177ff3c8ae50e2d008f8509bf1b80c9c7a93df7c6ee52f41`。未做压力或高负载测试。

## 2026-09-29：启动恢复计时器独立于模块执行

启动超时和入口失败恢复原先由内联 `type="module"` 脚本安装。若浏览器尚未执行这段模块脚本，页面会停在可见但尚未初始化的“新任务”外壳，恢复计时器也不存在。启动代码现在是页面末尾的普通脚本，仅通过动态 `import()` 载入应用模块；它在模块请求前安装 20 秒计时器，并将 `loading`、`timeout`、`failed`、`ready` 写入根元素的 `data-mdo-startup`，便于下一次故障定位。此改动增强恢复路径，不把此前静态资源返回 200 的偶发空壳认定为已找到根因。

隔离单文件页首次拒绝 `/js/main.js` 时立即显示启动错误，重载按钮获焦；点击后同一会话 URL 恢复标题、Ling 3.0 Tiny 配置和输入焦点，状态为 `ready`。另将首次模块请求延迟 60 秒：约 20 秒后状态为 `timeout`，恢复层显示且按钮获焦；点击后原会话恢复，状态回到 `ready`。30 秒延迟的独立页面也在请求完成后自行恢复。此前偶发的资源 200 空壳仍需在原生 WebView2 捕获完整加载与执行状态。

Windows 有界发布门禁通过 114 项 Python、完整 Node 契约检查、74 个前端模块解析、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 和 20 秒单文件启动；根目录 `mdo.exe` SHA-256 为 `d66d67edf44c294f5dbc274cc94751c8942f086cc9ae0f45df5fc95de42e69e7`。Linux 在 WSL 原生文件系统独立拷贝通过同一有界门禁（跳过 GUI），包 SHA-256 为 `f5f15c949718d409e57d5b2f18c2f418998ee7b014af78db2d25d54b89a028fc`。未做压力或高负载测试。

## 2026-09-29：创建明确被拒后刷新仍等待手动重试

创建会话收到明确 4xx 时，旧前端只在当前页面内存里标记“已拒绝”；刷新后持久草稿仍是 `creating`，启动协调器会再次发送创建请求。全局新任务日志现增加可持久化的 `rejected` 阶段：明确拒绝后先写入 Home，刷新只恢复待发卡和手动重试入口，不自动 POST。用户点击重试时按当前模型、思考和权限生成新会话 ID，并将所有仍待发的输入快照移交新会话。旧 `creating`、`copying` 日志继续可读；结果不明的创建仍保留原 ID 供人工核对。刷新后的提示不再重复附加同一句说明。

隔离打包夹具首次创建返回 422 后，`data/draft.json` 的阶段为 `rejected`。刷新后页面仍在原项目的新任务路由，有一条待发送项和“使用当前选项重试创建”；停止夹具时创建 POST 总数为 1，队列与运行 POST 均为 0，浏览器脚本错误为空。另一隔离 Home 在刷新后手动点击重试，最终只产生一轮用户与 Agent 消息，创建 POST 总数为 2，队列与运行 POST 各 1。API 探针验证 `rejected` 往返，Node 测试模拟新页面实例并确认不会自动创建、可修改配置后重试。

Windows 有界发布门禁通过 114 项 Python、74 个前端模块解析、完整 Node 契约检查、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 和 20 秒单文件启动；根目录 `mdo.exe` SHA-256 为 `b11d0bde091020d01a7f52622d8a3d281aaba1fd52b1c5c7276ca7b143661752`。Linux 在 WSL 原生文件系统独立拷贝通过相同的有界门禁（跳过 GUI），包 SHA-256 为 `cbe5aa19bb88cc3d646406f9f4c7419c7f32c58418c26bb047fe157a93d3197e`。未做压力或高负载测试。

## 2026-09-29：项目级新任务草稿接入输入区

新任务输入区现在按项目选择 `data/project-drafts/{project}.json`。尚未发送的文本随项目切换和页面刷新独立恢复；首次发送前先落盘项目草稿，再写入全局创建日志。全局 `data/draft.json` 只在创建会话与移交首次提交时承载任务 ID、待发队列和恢复状态。只有确认全局日志持久化后才清理已提交的项目文本，避免崩溃窗口把唯一副本删除。附件先创建会话也沿用同一移交流程。已有旧版全局未发送文本会先复制并确认保存到所属项目，再清除全局文本；若目标已有不同草稿，两份都保留并提示冲突。

隔离打包页分别在默认项目与 `mobile-b` 输入不同文本，切换、刷新后各自恢复；320×350 视口下输入焦点仍在输入框，页面没有横向溢出或脚本错误。另一打包夹具完成 A/B 项目发送隔离：A 仅生成一轮消息并清空其项目草稿及全局日志，B 的草稿保持原样。旧全局文本夹具验证先迁移到默认项目；首次创建返回 422 后可用页面上的重试入口完成一次队列提交，最终两个草稿均清空。九项定向 Node 回归覆盖项目隔离、旧草稿迁移、冲突保留、附件先创建及创建失败恢复。创建明确失败后的刷新行为见上方独立阶段。

Windows 有界发布门禁通过 114 项 Python、74 个前端模块解析、完整 Node 契约检查、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 与 20 秒单文件启动；根目录 `mdo.exe` 已更新，SHA-256 为 `51cf623ff304d21d9845cf1dd79691d38bc010498ec176293c944825cce8f64a`。Linux 在 WSL 原生文件系统的独立拷贝通过同一有界检查（跳过 GUI），包 SHA-256 为 `f176ae67badc9236bb828209f5dcbb591c537a5257648a4d1bf16fc51884d74f`。未做压力或高负载测试；实体移动端和其他系统原生 WebView 仍待验收。

## 2026-09-29：新任务配置继承所选 Agent 的默认值

自定义 Agent 已在目录接口中声明模型、思考强度和权限，但新建会话弹窗切换 Agent 时仍沿用输入框上一个配置，提交后覆盖了 Agent 的默认值。现在切换 Agent 或重新打开弹窗会将其声明的三项默认值填入控件；未声明的字段沿用输入框当前选择，之后仍可手动修改。空白新任务输入框在用户尚未手选配置时也跟随内置默认 Agent；用户选择权限或模型后，后续目录刷新不会覆盖该选择。Node 回归覆盖目录迟到、手动覆盖、自定义 Agent 切换和未知模型 ID 的可见性。

Windows/Linux 有界发布门禁均通过 114 项 Python、84 项 Node、72 个前端模块解析、21 个运行探针与确定性打包；Windows 还通过单文件零旁路写入及 20 秒启动检查。根目录 `mdo.exe` 已重建并与 Windows 发布包 SHA-256 `083eff884584b4a8edce4f3431348060b9f2cd7631ffc4e3be23fdd9719c0626` 一致，Linux 包 SHA-256 为 `2dc13ec23c1172db256e956c1e9673333dcd4b8c55f58a14d8b3428ad0b08754`。最终单文件包的实际弹窗切换与创建回放仍待单独验收；便携 WebView 配置与实体设备验收另见下文。未运行压力或高负载测试。

## 2026-09-29：打包页自定义 Agent 创建链补验

在隔离 Home 加入测试用 C Agent，目录按 ID 排序时 `qa.profile` 位于 `mdo.default` 前。前一包 `.build/mdo-packed-docks-1ynazdpb` 的“配置后创建任务”弹窗因此初始选中了 QA Agent；这会令用户无意间改用另一 Agent。现在目录初载或原选择已移除时明确选择 `mdo.default`，目录刷新则保留用户手动选中的 Agent。Node 回归覆盖三个时序。

最终单文件 Home `.build/mdo-packed-docks-8xf736ky` 初开弹窗显示 Default、Ling 3.0 Tiny、中思考、询问权限；手动切到 QA Profile 后立即显示 Ling Text QA、高思考、只读。页面创建 `Agent profile packed QA` 后，服务端会话持久字段为 `agent_id=qa.profile`、`model_id=ling-3.0-tiny-text-qa`、`reasoning_effort=high`、`permission_profile=read-only`。从该页面发送 `PROFILE UI`，有界本地模型返回 `UI fixture completed.`，显示 7 输入 / 3 输出 tokens；刷新后 Agent、配置、两张消息卡与用量仍在，浏览器脚本错误为空。自定义 C Agent 夹具只写入隔离 Home，未加入产品内置目录。

最终代码的 Windows/Linux 有界门禁均通过 114 项 Python、85 项 Node、72 个前端模块解析、21 个运行探针与确定性打包；Windows 单文件零旁路写入及 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `1bffe7953bdfea4b45c3e0d15f0c2b35a87ac9ff765f5991802d8eab82e646f1`，Linux 包为 `d44556f53fb3954b5e36dba3895b76adabf2f4c132e13c6e83fa4a83e10a3339`。实体触控、原生 Linux WebView 和 macOS 仍待验收；未做压力或高负载测试。

## 2026-09-29：手机短屏自定义 Agent 创建与运行

复用相同根目录单文件包，在隔离 Home `.build/mdo-packed-docks-gz8hn8f7` 的 320×350 页面打开手机侧栏、“配置后创建任务”弹窗。默认显示内置 Agent；切到 QA Profile 后，模型、思考强度和权限分别变为 Ling Text QA、高、只读。弹窗内部可滚动到最后一项权限，创建按钮始终位于视口内。页面创建 `Mobile Agent profile QA` 后，服务端字段为 `qa.profile`、`ling-3.0-tiny-text-qa`、`high`、`read-only`；随后发送 `MOBILE PROFILE UI`，待发卡显示冻结配置，本地模型返回完整回复和 7 输入 / 3 输出 tokens，输入焦点恢复。文档宽 320px，发送按钮位于 x=261–301、y=292–332，脚本错误为空。

同一包在 280×250 页面重新打开配置弹窗，Agent 和权限控件可通过内部滚动到达，取消后焦点回到“配置后创建任务”入口；文档宽仍为 280px。此阶段没有修改应用代码，根目录 `mdo.exe` 的 SHA-256 仍为 `1bffe7953bdfea4b45c3e0d15f0c2b35a87ac9ff765f5991802d8eab82e646f1`，沿用上节已通过的 Windows/Linux 有界门禁。浏览器视口模拟不能代替实体手机触控、输入法和软键盘验收；未做压力或高负载测试。

## 2026-09-28：无明确会话 URL 时优先恢复正在运行的任务

旧版走查记录指出，刷新后会自动选中仍在运行的会话并接续显示结果。新版启动时只按便携 Home 保存的上次会话选择，另一会话有活跃运行也会留在空闲任务页。现在仅在“继续上次任务”启动模式且 URL 没有明确选择时，从已载入的运行列表中选择最近的、仍非终态且会话处于进行中的任务；否则沿用上次会话。用户明确打开的会话 URL、“打开新任务”与“每次询问”保持原选择。Node 回归覆盖空闲上次会话、活跃任务、终态/不存在的运行、明确 URL 和其他两种启动模式。

Windows 单文件 Home `.build/mdo-packed-docks-5nkjpkzj` 中，服务端上次会话指向 `Idle saved QA`，另一个 `Packed docks QA` 正执行 20 秒有界本地模型回复。从无 hash 的根 URL 打开，页面实际跳转到运行中的 `Packed docks QA`，显示用户消息与“运行中”；同一页随后收到 `UI fixture completed.`、7 输入 / 3 输出 tokens 和“就绪”，浏览器脚本错误为空。此验收使用本地模型夹具，未覆盖不同系统原生 WebView 或实体移动端。

Windows/Linux 有界门禁通过 114 项 Python、82 项 Node、72 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 门禁包 SHA-256 均为 `65e2e0efed4bb9adb44c88ed8939ce5ad554fecb078c0c53f0b88453c001cefa`，Linux 包为 `124c5a1d7badca6aedd440f20023b00e3694b0bd7a3bf0a107f32447d02a8f86`。未运行压力或高负载测试。

## 2026-09-28：图片清理失败提示准确反映草稿状态

草稿图片移除时若清理标记 POST 失败，之前中文界面会直接显示服务端英文错误；用户重试成功后，这条失败提示也可能留在输入区。现在客户端把该操作的失败归一为“清理记录未能保存，图片仍在草稿中，请重试”，清理待办达到上限时给出单独提示；两者均有中英俄文案。后续图片成功移除时，只有当前会话的这两类过期提示会被清除。三语词典与错误码映射通过 Node 用例。

Windows 单文件 Home `.build/mdo-packed-docks-v42ofjxb` 用隔离代理把首次清理标记 POST 合成为 503。页面先显示中文保留说明和图片；再次点击后图片与错误提示一同消失，服务端草稿 revision 2、附件引用为空，`queue.json` 清理标记为空，附件目录无文件。代理累计两次标记 POST、一次附件 DELETE，浏览器脚本错误为空。该测试未覆盖原生 WebView 或实体触控。

Windows/Linux 有界门禁通过 114 项 Python、81 项 Node、72 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 门禁包 SHA-256 均为 `e102718f8ad9f47dba5c4e5386fafbe545bc35cfe522fa524f64749a1cc95160`，Linux 包为 `7dc9cd9822a9e73865085fad40321c9e07c2c8008b6feab26013c16cbbcaa19a`。未运行压力或高负载测试。

## 2026-09-28：草稿图片清理意图跨重启恢复

上一阶段的删除重试只保存在页面内存中，进程在第一次 DELETE 失败后退出仍会留下孤立文件。现在图片移除先确认当前草稿引用已保存，再通过仅接收有效会话和格式正确的 32 位十六进制图片 ID 的空体 POST，把清理意图写入该会话现有的 `queue.json/discard_images`；随后才从草稿移除图片并执行 DELETE。服务端仍逐项检查草稿、队列、运行及历史引用，未去引用前的删除返回 409；删除成功或文件已不存在时清除持久标记。队列标记最多 256 项，重复登记幂等，记录失败时图片保持在草稿。API 探针覆盖空体约束、重复登记、仍被引用时拒删、去引用后删除和标记清除。

Windows 单文件 Home `.build/mdo-packed-docks-44gvokmd` 中，两次图片移除经过 3 秒延迟 DELETE，首次请求被合成 503 拒绝后由清理器重试成功；代理共收到三次 DELETE，草稿 revision 4 无附件、`queue.json` 标记为空、附件目录无文件，浏览器脚本错误为空。另在同一 Home 通过真实 HTTP 留下“草稿无引用、队列有清理标记、附件文件仍在”的中断状态，停止并重启打包进程；重新打开原会话后，页面读取持久标记，附件 `.bin` 消失且标记清空。这个重启验收从 API 构造精确中断窗口，页面负责恢复清理；尚未在实体触控或原生 WebView 中模拟进程被强制终止。弃用上传在取得服务端图片 ID 前的崩溃窗口仍需单独审计。

另一隔离 Home `.build/mdo-packed-docks-5f_kulu7` 把第一次清理标记 POST 合成为 503：打包页仍显示图片和错误提示，服务端草稿 revision 保持 1 且引用未变，`queue.json` 未创建，附件文件保留，DELETE 次数为零。再次点击后标记写入成功，草稿 revision 2、引用为空，附件与标记均清空；代理累计两次标记 POST、一次 DELETE，浏览器脚本错误为空。

Windows/Linux 有界门禁通过 114 项 Python、81 项 Node、72 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `898bd80954e25e02cee8412ed63fdd77783d24feb68083572b6e3deb8ba63441`，Linux 包为 `aa930f490240e4694a76de7a9518e2ac30f21808f3c6325551ae4ca2a84114cd`。未运行压力或高负载测试。

## 2026-09-28：图片从草稿移除后重试清理孤立附件

图片移除先保存不再引用该附件的草稿，再向服务端删除文件。此前若 DELETE 暂时失败，页面仍完成移除，但只显示错误，没有把孤立附件交给现有的重试清理器。旧单文件 Home `.build/mdo-packed-docks-owgpfb2e` 用一次合成 503 复现：草稿 `attachments` 已为空，代理仅一次 DELETE，便携 Home 的附件 `.bin/.json` 仍在。现在失败后按原会话和图片 ID 交给队列、弃用上传共用的清理器；`attachment_not_found` 仍视为已清理。草稿保存失败时仍恢复图片，不安排删除。

修复后的 Windows 单文件 Home `.build/mdo-packed-docks-2qw_rivh` 使用相同夹具：上传一次、点击移除后代理记录两次 DELETE（第一次拒绝、第二次成功），草稿 revision 2 的附件数组为空，磁盘附件目录无文件，界面没有残留图片或误导性的失败提示，浏览器脚本错误为空。当时重试器只在页面存活期间保有暂存清理项；此风险已在上节通过持久标记和重启验收补齐。该阶段 Windows/Linux 有界门禁通过 114 项 Python、81 项 Node、72 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `f5825acbedfff32c00d3348de169e0b07f4f978f3369cf20e74620e1129c8f9d`，Linux 包为 `bed5ccf9d0c8af07c6bc4fe71801e27b636c2193ebaa316dae994303f5b0915b`。未运行压力或高负载测试。

## 2026-09-28：运行创建失败后跨会话保留人工核对

上一阶段将导航加载与派发失败的阻断分离，本阶段用单文件页面复验人工核对链。隔离 Home `.build/mdo-packed-docks-gpatqxoj` 的代理对首次运行 POST 返回合成 503：页面保留一条“发送状态待确认”待发消息及“确认未发送后重试”入口。切到新任务再返回后，复核提示和消息仍在，代理没有自动产生第二次运行 POST。只有实际点击重试入口才产生第二次 POST，随后得到一轮回复、队列清空且输入恢复；全程一条队列 POST、两条运行 POST（第一次被拒、第二次人工重试），浏览器脚本错误为空。此检查使用本地模型夹具，不涉及外部端点；真实网络中途断开的其他时序仍需分别核对。

同一包还在 320×350 的“联网与搜索”长表单中检查底部“代理绕过”字段：滚动后字段位于 y=163–203、操作栏位于 y=267–350；将视口高度模拟缩至 250px 时，已聚焦字段仍位于 y=155–195。该模拟不能替代实体手机软键盘。此阶段没有修改程序代码；根目录 `mdo.exe` 仍是上一阶段通过 Windows/Linux 有界门禁的单文件包，SHA-256 为 `77c83f40e796f8a8712b9806565d6c3114ef025306c26d779bc2e0f235139a99`。未运行压力或高负载测试。

## 2026-09-28：同一会话重叠加载不再提前放行待发队列

快速离开又返回同一会话时，两次导航加载可能同时在途。原先只有一个 `Set` 标记：旧加载的 `finally` 会删除新加载仍在使用的标记；导航完成也可能清除派发失败后等待用户复核的阻断。现在队列闸门分别记录明确复核阻断和每个会话最新一次加载的持有者，旧加载只能释放自己的持有权。待发队列和优先中断都在最新加载完成、且无复核阻断后才继续。Node 用例覆盖旧加载先结束、复核阻断与加载交错、不同会话互不干扰。

Windows 单文件 Home `.build/mdo-packed-docks-eqs41vij` 将队列 GET 延迟 1.5 秒，在会话和新任务间快速往返后，发送本地夹具消息得到一次队列 POST、一次运行 POST、完整回复和用量；页面无脚本错误。这条浏览器检查验证普通操作未受影响；重叠加载的精确放行顺序由 Node 用例验证，尚无打包页直接观测派发窗口的证据。Windows/Linux 有界门禁通过 114 项 Python、81 项 Node、72 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `77c83f40e796f8a8712b9806565d6c3114ef025306c26d779bc2e0f235139a99`，Linux 包为 `efa152ead7f351b99c7fb28fb7913d6b759c6113a42cade730edb6449442633d`。原生 WebView 与实体移动端待验收；未运行压力或高负载测试。

## 2026-09-28：切换会话后忽略旧导航加载错误

会话导航会异步读取详情、运行态和待发队列。旧单文件 Home `.build/mdo-packed-docks-x2t1ctck` 将首条队列 GET 延迟 5 秒并返回合成 503：从已有会话立即切到新任务后，旧请求的 `Synthetic queue read failure` 出现在新任务输入框。原因是导航回调在 `await` 后没有核对路由归属，错误分支直接显示到当前输入区。现在同一回调核对路由版本、项目、会话和草稿归属；旧加载不再显示错误或继续派发当前会话的队列。加载失败时也始终释放原会话的队列阻塞标记，避免后续重试被残留状态挡住。

修复后的 Windows 单文件 Home `.build/mdo-packed-docks-2tz8gg2f` 使用相同的 5 秒延迟和一次 503：切到新任务后保持“就绪”，输入框没有旧错误，代理仅一次队列 GET，浏览器脚本错误为空。独立 Home `.build/mdo-packed-docks-s0my701y` 在仍停留原会话时注入一次 700 毫秒延迟失败，错误正常显示；刷新后的下一次读取成功，提示消失，输入恢复。两项均只用隔离本地夹具，没有外部模型请求；原生 WebView 和实体移动端仍待验收。

Windows/Linux 有界门禁通过 114 项 Python、79 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `40848fafb734456d09728e510df866bd1fe3f459a0a29e11a6dc05104ccf78e0`，Linux 包为 `6c8ba5704ef6ad8e93c670e8bf62e84e7fd3b1f5a51579ced557c01685cdc4fe`。未运行压力或高负载测试。

## 2026-09-28：未完成新任务始终显示在所属项目

便携 Home 当前只保存一条跨会话的新任务意图。旧单文件 Home `.build/mdo-packed-docks-1qi35csb` 中，默认项目创建请求延迟 5 秒且返回配置错误后，切到 `workspace-b` 的新任务页会同时显示默认项目的待发卡和重试入口，顶栏却声称任务将建在 `workspace-b`。这可能使用户用另一个项目的配置重试原任务。现在导航在新任务入口核对持久意图的 `project_id`：有未完成意图时，将其他项目的新任务路由替换为所属项目并提示先完成恢复；已有会话仍可正常进入。意图处理完成后，项目切换恢复。刷新或直接打开其他项目的新任务 URL 时，也在读取便携草稿后重新核对。

修复候选 Home `.build/mdo-packed-docks-o4aabpn0` 实测了真实第二项目、延迟创建失败、侧栏与输入区项目选择器两条切换路径：两者都留在默认项目，重试后恰有两次创建 POST、一次队列 POST、一次运行 POST，得到一轮回复；随后 `workspace-b` 新任务页可进入。该轮发现重试按钮消失后焦点落到页面正文，于是补上原按钮获焦时的输入焦点转移。Home `.build/mdo-packed-docks-os2trja_` 实测重试成功后输入框获焦、回复可见且无脚本错误。最终包 Home `.build/mdo-packed-docks-6e7smrpi` 又在失败意图持久化后直接打开其他项目的新任务 URL，页面返回默认项目，待发卡和重试入口仍可见，脚本错误为空；此最后一条 URL 测试未创建第二项目，仅验证路由归属。Node 用例覆盖新任务入口、已有会话入口、直接 URL 重核对及意图解决后恢复切换。

Windows/Linux 有界门禁通过 114 项 Python、79 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `e1422c6dcf218f5d8d005376413456931d07e3fb03c72aa6b1f9aa2cb0d044f5`，Linux 包为 `a1ba55aea49fd8fc0bdf069dd997e986ea64234af90434cca7ad8d2d6a136f02`。这仍是一条全局待恢复新任务的设计，不支持同时保留多个项目的新任务意图；实体移动端和原生 WebView 待验收，未做压力或高负载测试。

## 2026-09-28：运行轮询迟到响应不再更新其他会话

运行轮询发出 GET 后，清除定时器不能取消已经在途的请求。旧回调原先会在切换会话或新任务后继续调用 `setRun`，失败时还会把旧请求的错误写入当前输入区。现在每次轮询固定发起时的路由版本、会话和运行 ID；响应到达后以及后续异步刷新之间都核对归属。若只是进入设置又返回同一运行，则续接轮询，避免丢失完成状态。

Windows 单文件 Home `.build/mdo-packed-docks-8153orhd` 用有界代理先捕获第一条 `running` GET，再延迟 4 秒并返回合成 503；页面在这期间切到新任务，迟到错误没有出现在新任务输入区，状态保持“就绪”、停止按钮隐藏、焦点留在输入框，脚本错误为空。最终逻辑的单文件 Home `.build/mdo-packed-docks-z21g1bxr` 将每次 GET 延迟 3 秒：运行中进入设置、等待首条旧响应后返回会话，轮询继续到 `succeeded`，回复、token 用量和“就绪”状态出现，脚本错误为空。测试使用本地合成慢回复，没有外部模型请求；原生 WebView 与实体移动端仍待验收。

Windows/Linux 有界门禁通过 114 项 Python、78 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `9350676893e9e60935bfb90223b7ef3445a21bb966da5306b1a63fc3aac52982`，Linux 包为 `4ce1c856d3c6840f366bd96ad6959999b5ad4469d6a0ada3df5f84edd5f404c1`。未运行压力或高负载测试。

## 2026-09-28：移动决策完成后返回对话且运行结束不抢焦点

390×500 的 Windows 单文件 Home `.build/mdo-packed-docks-u_nidcw3` 走通 `SEQUENTIAL DECISIONS UI`：回答询问后，在移动检查器“决策”页允许最后一项 `exec`。工具执行成功、服务端待审批归零，但检查器仍停在空白决策页；运行完成又将焦点送到面板遮挡的主输入框。现在决策面板在成功刷新并确认待办数为零后通知外壳；只有手机检查器仍显示决策页、焦点仍在该页标签时才关闭抽屉并聚焦输入。运行轮询结束时不再无条件聚焦输入；若焦点原本在随后隐藏的停止按钮，才在没有弹层或抽屉时恢复输入焦点。

最终单文件 Home `.build/mdo-packed-docks-vgckmsog` 在 390×500 重走询问→审批→回复：检查器提交“允许一次”后审批卡归零、面板关闭、焦点到输入框，工具结果 `exit_code: 0` 与最终回复可见。另一条 4 秒有界 `SLOW UI` 运行中打开移动检查器并停在“决策”标签，运行结束后检查器保持打开、标签仍获焦；页面宽度 390px、浏览器脚本错误为空。若还有审批待处理或用户已切换标签，完成单项决策不会强制关闭面板。实体触控及原生 WebView 尚待验收。

Windows/Linux 有界门禁通过 114 项 Python、78 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `475d7bad923938113d2b2348565b0442ac3f589f4759eee3b8a2860c374ae560`，Linux 包为 `e566e621273b231a78b44a061765979e7c84cb8c4f903d27293506b410993a32`。未运行压力或高负载测试。

## 2026-09-28：短屏长询问先显示问题开头

320×350 的长 `ask_user` 询问到达时，旧滚动策略优先露出第一个选项，却把标题和问题开头裁掉。旧单文件 Home `.build/mdo-packed-docks-v3be5cop` 中，询问停靠区为 y=52–221，自动滚动 67px；标题位于 y=-2–17，问题从 y=24 开始，用户必须反向滚动才能读懂选项。现在当标题、完整问题和首个选项无法同时放入停靠区时，从标题开始呈现；空间足够时仍优先把选项露出。

修复后的 Windows 单文件 Home `.build/mdo-packed-docks-9buh8ki9` 在 320×350 下打开同一俄语长询问，停靠区 y=52–221、滚动量为 0；标题 y=65–84、问题 y=91–199 均完整可见，首个选项从 y=207 开始提示还能下滚。向下滚动后选项 y=72–149，可点击并得到回复，输入焦点回到编辑框；页面宽度 320px、脚本错误为空。独立 Home `.build/mdo-packed-docks-pm16utpy` 在 390×500 下标题、问题和首个选项均完整处于停靠区内，维持原有直接阅读与选择路径。测试用本地合成模型，没有外部请求；实体手机软键盘、触控和原生 WebView 仍待验收。

Windows/Linux 有界门禁通过 114 项 Python、78 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `fbfac3e2e564a2c414b94ae7d1fcca577c6e214456733251e11385054ce6762e`，Linux 包为 `d566c3ff809c3bc8d00c8122acfc5553820441ec0112af0e4c10661d5176dd50`。未运行压力或高负载测试。

## 2026-09-28：图片上传期间保持斜杠命令可执行

图片正在保存时，输入完整 `/help` 再按 Enter 会被上传状态提前拦截，发送按钮也保持禁用；从补全菜单直接选命令却能执行。这让同一命令的键盘和指针路径不一致。现在先识别并执行完整斜杠命令，再检查图片上传状态；发送按钮只在输入恰好是完整命令时允许操作。普通消息仍等图片保存完成，执行命令只清除命令文本，不消费未发送的图片草稿。

Windows 单文件 Home `.build/mdo-packed-docks-25ciy4kz` 将附件 POST 延迟 5 秒：上传第一张时点击发送执行 `/help`，上传第二张时按 Enter 执行 `/help`，两次均打开帮助且没有队列或运行 POST；刷新后两张图片仍在且输入框获焦。上传第三张时输入普通消息，发送按钮在保存期间禁用，保存完成后恢复；三张图片与消息草稿均保留，脚本错误为空。测试只用本地合成 PNG，没有调用外部模型。实体移动端粘贴、触控和原生 WebView 仍待验收。

Windows/Linux 有界门禁通过 114 项 Python、78 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `e25ece20d601c078733c60c08aafc94ad32605093a7893a8a271b4405f576945`，Linux 包为 `5d3e6abf2433b8184befc2cacb40b665768a1ef019ff70cb11c1a259a6d0649e`。未运行压力或高负载测试。

## 2026-09-28：明确会话 URL 刷新后恢复输入焦点

打包页在 390×500 完成 `TODO UI` 后保留 1/2 待办，再发送 `LONG ASK UI` 并在询问到达前打开移动检查器。到达时检查器自动收起，询问标题获焦，长问题从 y=65 开始可读，首个长选项完整处于 y=171–227；回答后输入框重新获焦，待办 1/2 与两轮回复保留，刷新后账本中的回答和待办仍在，页面宽度 390px、脚本错误为空。此组合本身没有发现缺陷，隔离 Home 为 `.build/mdo-packed-docks-cb2a8jof`。

刷新该会话的明确 `#/projects/default/sessions/...` URL 时又发现输入焦点停在页面正文。根因是启动导航仅对自动打开上次任务设置了加载完成后的焦点，遇到明确 hash 路由便直接返回。现在明确会话 URL 在对应会话详情准备好后聚焦输入；明确新任务 URL 在输入可写时也聚焦。用户在加载过程中主动转到其他控件时不抢焦点，设置等非工作区 URL 不触发。Node 回归覆盖详情延迟、用户改选焦点和明确新任务路径。

最终单文件 Home `.build/mdo-packed-docks-hwqay5oq` 验证明确会话 URL 首次打开、桌面和 390×500 刷新，以及 390×500 新任务 URL 刷新后均在页面加载完成时聚焦输入框；窄屏文档宽 390px，浏览器脚本错误为空。Windows/Linux 有界门禁通过 114 项 Python、78 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `e86dd10c13d99182bf49a9d4674fc908ca746d2f6017d11a21eee787ae5e113a`，Linux 包为 `fab17628e46b86bdfb3593ce568e46b23452bd2c79c834f307f1a60974864f15`。实体手机触控、软键盘与原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：切会话后附件删除失败的重试

上传请求返回时如果输入区已切到其他任务，原实现会发起一次附件 DELETE，却直接忽略删除失败；临时网络或存储错误会留下没有草稿引用的附件。现在这类过期上传交给待发队列已经使用的附件清理器：它按原项目和会话保存待清理 ID，运行仍占用会话时等待，暂时性删除失败按有界退避重试。新任务和原任务都不接收过期图片。

手工打包夹具新增 `--fail-first-attachment-delete`。Windows 单文件 Home `.build/mdo-packed-docks-dbmbgbcu` 将附件 POST 延迟 5 秒，上传时立即切到新任务；首次 DELETE 收到夹具的 503，页面仍开启时自动发出第二次 DELETE。代理记录一次附件 POST、两次 DELETE；原会话附件目录最终为 0 个文件，新旧任务输入区均无图片，浏览器脚本错误为空。该测试使用合成 PNG 和本地服务，没有外部模型调用。页面关闭前尚未到达重试时的跨重启恢复不属于此项；原生文件拖放、实体手机粘贴及 WebView 仍待验收。

Windows/Linux 有界门禁均通过 114 项 Python、77 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `d594980c70204b11c41fe2e88a770fc0afcc4e3d908ac2c557a444b31f4032ad`，Linux 包为 `8b1c9d246daef7a64c20f00be33843184f13a27e7e38e46ebf0022d319ed7842`。未运行压力或高负载测试。

## 2026-09-28：连续图片上传的失败和切会话边界

手工打包夹具新增 `--fail-first-attachment`，只让第一条附件 POST 返回 503，并保留已有的 0–5 秒延迟。它用于检查连续图片添加中某张失败后，后续图片是否继续处理，而不涉及外部模型或高负载。单文件 Home `.build/mdo-packed-docks-glbb2wj_` 在 1.5 秒延迟下连续粘贴两张合成 PNG：首张失败时页面显示 `Synthetic attachment failure`，第二张继续保存，提示中的等待数量消失；刷新后成功的图片仍在，浏览器脚本错误为空。代理记录两次附件 POST，失败没有把整条队列中断。

另一隔离 Home `.build/mdo-packed-docks-g3bq8jzh` 把附件 POST 延迟 5 秒。在请求尚未返回时切到新任务，返回后新任务没有图片，切回原会话也没有误附图片，原会话附件目录内文件数为 0，页面脚本错误为空。这条切会话路径同时完成了归属隔离和返回后清理的核验。原生系统拖放、实体移动端粘贴和 WebView 仍待验收。

Windows/Linux 有界门禁均通过 114 项 Python、77 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 已重建，SHA-256 仍为 `e7fe48292e340ef90150ee2825e22ef02dced4c908d075732261e78d2c4041b5`，Linux 包为 `359cd0b91d00d514fdc8c2287b4e7173ce59e97d3db970e6893ba2f7392c1ba1`。未运行压力或高负载测试。

## 2026-09-28：连续图片粘贴自动排队并修复新任务附件创建

上一阶段让上传期间再次粘贴不再静默失败，但仍要求用户等候并重试。现在附件入口把同一会话的连续粘贴、选择或拖入保留为逐张上传的有界队列；界面显示待添加数量，四张上限同时计算已保存、上传中与等待中的图片。切换会话后不把待添加图片送到新会话；创建图片新任务期间，临时不可写状态不再拒绝同一批后续图片。图片新任务还有一处独立故障：创建会话时没有传入输入区所选模型、思考强度和权限，直接触发读取 `model_id` 的脚本错误；现在按当时的选择传入完整配置。

最终 Windows 单文件 Home `.build/mdo-packed-docks-nqfdsgxh` 通过各 2 秒的会话创建和附件上传延迟验证：新任务连续粘贴先显示 1 张待添加，随后只创建一次会话、顺序保存两张图片；服务端记录 Ling 3.0 Tiny／高思考／只读，刷新后两张草稿图片仍在。切走另一张正在创建图片的空白任务后，原会话没有混入附件，代理未记录该次附件 POST。已有会话在 320×350 下连续粘贴同样显示等待数量并最终得到两张，页面宽度保持 320px、脚本错误为空。另一候选 `.build/mdo-packed-docks-9910absz` 验证第五次粘贴被四张上限拦截，刷新后恰有四张。Windows/Linux 有界门禁通过 114 项 Python、77 项 Node、71 个模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `e7fe48292e340ef90150ee2825e22ef02dced4c908d075732261e78d2c4041b5`，Linux 根目录 `mdo` 为 `359cd0b91d00d514fdc8c2287b4e7173ce59e97d3db970e6893ba2f7392c1ba1`。原生系统拖放与实体手机 WebView 粘贴仍待验收；未运行压力或高负载测试。

## 2026-09-28：图片上传期间再次粘贴给出明确反馈

旧版附件在本地读取后加入草稿；新版先把图片写入会话附件存储。上传或移除尚未结束时，输入框仍能收到粘贴和拖放事件，但 `addFiles` 过去直接返回，既不接收文件，也不提示用户。现在这类操作显示“请等待当前图片操作完成后再添加”，保留已有草稿；完成后再次粘贴会清除提示并正常上传。附件按钮原本已在忙碌期间禁用。

此处记录上一阶段的行为；上传期间的再次添加现由上节的有界队列接收，等待提示只用于图片移除期间。

隔离打包 Home `.build/mdo-packed-docks-qrpw3w0x` 用最多 5 秒的附件 POST 延迟复现连续粘贴：首张图片保存中，第二次粘贴立即显示提示，首轮只有一张图片；完成后重试得到两张，刷新仍保留两张。320×350 下再次连续粘贴，提示完整可见、页面宽度保持 320px、没有脚本错误；代理仅记录实际接收的三次附件 POST。Windows/Linux 有界门禁通过 114 项 Python、77 项 Node、71 个模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `cb84a8a32ad3e995ba893682f8b4b53075bb769f03651dfd6ea8c734536d023c`，Linux 根目录 `mdo` 为 `2f8883471968f0a08989ee778280b1850105cc721559159f71e09649da782791`。浏览器调试接口不支持注入原生系统文件拖放，实体系统的拖放与手机 WebView 粘贴仍待验收；未运行压力或高负载测试。

## 2026-09-28：内置资源说明随界面语言切换

扩展页此前已翻译界面和资源状态，但五项随程序发布的 Agent、Skill、Module 说明仍直接显示英文。现在仅在资源 ID、内置来源和原始说明同时匹配时，将默认 Agent、Project Explorer、默认 Agent 模块、Echo 工具模块和 Plan 工具模块的说明接入中英俄词典。外部资源以及改写过说明的内置资源继续显示作者原文，资源名称与 ID 不改动。Node 用例覆盖三语和上述保留边界。

重新生成的 Windows 单文件 Home `.build/mdo-packed-docks-_o12_vzo` 在中文扩展页显示五项中文说明，切换俄语预览后五项同步显示俄语，浏览器脚本错误为空。Windows/Linux 有界门禁通过 114 项 Python、77 项 Node、71 个模块解析、21 个运行探针和确定性打包；Windows 另通过单文件零旁路写入及 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `838a5a082ab96b610507f6b8082eb612759d14784fb4555ea4472cfdd0bf3b94`，Linux 根目录 `mdo` 为 `e8eb6a57aceef35431e91b5daa9bc59e231f3ad075c6cfbdda990e87ae4d9545`。其他服务端原样返回的错误与资源说明仍需逐项本地化；未运行压力或高负载测试。

## 2026-09-28：顶层操作弹窗跟随软键盘可视区

原有 `visualViewport` 处理只缩短 `.app-shell`，但编辑消息、新任务和会话操作使用 HTML 顶层 `<dialog>`，不继承容器高度。生产样式夹具在 390×700 布局、250px 可见区复现：编辑消息弹窗仍位于 y≈161–539，保存按钮下沿 y≈538，被键盘遮住。现把可视区顶部与高度同步到文档根节点，三个操作弹窗在该范围内定位并限制高度，字段区继续独立滚动。可视区被浏览器平移到底部时，聊天容器仍可保持原高度，弹窗则单独跟随平移。

修复后的浏览器夹具在 390px/320px 宽下验证编辑弹窗位于 y=8–242，保存按钮下沿约 243px；可视区顶部平移 20px 后弹窗在 y=28–262；平移至页面底部后弹窗在 y=458–692，且聊天容器未错误缩短。键盘收起后弹窗恢复居中与原高度，两种宽度无横向溢出或脚本错误。Windows 单文件 Home `.build/mdo-packed-docks-6dlgs58s` 在真实 320×250 短窗口打开新任务弹窗，底部创建按钮下沿约 235px，取消可执行且脚本错误为空。Windows/Linux 有界门禁通过 114 项 Python、77 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `a5dbefa14fff26463413a43ec90a64e3be615d28e5023610101495df314bb66c`，Linux 根目录 `mdo` 为 `1b88b668bd3256785ef0a9ad586bdbebc3e9fb2c063c1217aabe532a4add95b6`。可视区收缩和平移为浏览器夹具模拟；实体手机软键盘与原生 WebView 仍待验收，未运行压力或高负载测试。

## 2026-09-28：软键盘下的设置操作栏让位给表单

聊天输入区修复后，同样检查设置页的 `visualViewport` 缩小而布局视口不变的路径。390×700 布局、250px 可见区的生产样式夹具中，空闲且四个操作都不可用的底栏仍占约 83px，设置布局只剩 115px；仅可恢复默认时仍显示四个按钮。现在这一路径沿用既有极短屏设置规则：空闲收起底栏，恢复状态只显示“重置”，有更改时保留状态与预览、放弃、应用，并将自定义指令输入框压至可滚动的 80px。键盘收起后撤销紧凑规则。

修复后夹具在 390px 和 280px 宽下，空闲设置布局高 206px；有更改时底栏约 69px 且下沿正好在可见区 250px；仅恢复时底栏 49px 且只显示一个按钮。恢复 700px 可视高度后四个按钮和普通底栏回归，280px 文档无横向溢出。Windows 单文件 Home `.build/mdo-packed-docks-kq3knaqz` 在真实 280×250 短窗口中复核空闲布局高 206px，修改 Agent 推理强度后可预览并放弃，放弃后焦点回当前分类，浏览器脚本错误为空。Windows/Linux 有界门禁通过 114 项 Python、77 项 Node、71 个模块解析、21 个运行探针与确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `5ee343c7de0e254e01ff9d0ee314108d95e74c5ed705955f5d816c92f7ae2447`，Linux 根目录 `mdo` 为 `fc6a9abf6f5dfdc1f67098e0b81d4afd60e5e288423977979a9a7b10f086839f`。可视区收缩由浏览器夹具模拟，实体手机软键盘和原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：软键盘压缩可视区时保留补全菜单

某些移动 WebView 在软键盘弹出后只缩小 `visualViewport`，CSS 的高度媒体查询仍看到原来的布局视口。生产样式夹具以 390×700 布局、250px 可见区和 20 行草稿复现：发送按钮还在屏内，但 `/` 候选菜单仅高 12px，无法完整阅读单条候选。现由移动视口跟踪器在可见高度不超过 300px 时标记输入区，沿用极短屏的单行工具栏与可滚动草稿；键盘收起、非移动布局或缩放时会撤销标记。

修复后的浏览器夹具在 390px 和 320px 宽下均将菜单高度扩至 78px、顶端保持在手机栏下方 61px，发送按钮底边为 232px，20 行草稿继续留在输入框内滚动，320px 文档无横向溢出。`tests/test_mobile_viewport.mjs` 验证标记随可视区恢复清除；Windows 单文件包的普通移动宽度会话可打开 `/help` 补全并执行命令。Windows/Linux 有界门禁通过 114 项 Python、77 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `601a08721e65970e531c06c653f46abb03a9f520c7b8f9c15806cbcabd84a1be`，Linux 根目录 `mdo` 为 `98d0b0c0ce4e943279d0bd006180ae19c83025aedaf7d9a3bac4b716d2f05fe7`。可视区缩小是浏览器夹具模拟，实体设备软键盘和原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：极短屏设置页让位给内容

280×250 视口下，设置页固定操作栏原本即使四个按钮全部不可用，也占约 83px；扣除标题和分类栏后，内容可见高度只剩约 67px。现在空闲且无可用操作时收起栏；已有用户设置而仅“恢复默认”可用时，保留单按钮紧凑栏；有未预览的更改、预览结果或错误时，继续显示状态和操作，但收紧短屏间距。收起操作栏后，放弃更改或异步操作不会将焦点交给隐藏状态文本，而是落在当前分类按钮。

Windows 单文件候选 `.build/settings-short-debug.exe` 在 280×250 的空闲内容区可见高度为 166px，四按钮操作栏不占位；更改主题后，预览、放弃和应用仍可达，放弃后焦点回“常规”。保存主题并刷新后，紧凑的“重置”入口高 40px，确认层完整落在视口内；取消返回该按钮，确认恢复后 revision 增加且主题回到系统默认。10 个设置分区的选中标签均可见，页面宽度均为 280px；最终重新打包候选又复核空闲内容区和零脚本错误。有界代理新增 `--packed-path`，可在根目录程序正在运行时独立检查新包。Windows/Linux 有界门禁通过 114 项 Python、77 项 Node、71 个前端模块解析、21 个运行探针和确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。关闭占用窗口后重新生成的根目录 `mdo.exe` 与已验证候选逐字节一致，SHA-256 为 `2026b2d52defc399829d22b31e4c2286ca9f195d96b3c8f9da78f231b4aa22c7`；Linux 根目录 `mdo` 为 `4a967982b5b89dccd4608d7234c8c9d206d7aefbff9fa705ff84c7169252761e`。实体触控、软键盘和原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：新任务创建被拒后的键盘恢复验证

用 Windows 单文件 Home `.build/mdo-packed-docks-pf9igxvj` 和有界代理 `--fail-first-create --create-delay-ms 3000` 验证上一阶段共用的核对逻辑：首次创建收到 422 后，页面留在新任务、显示“使用当前选项重试创建”，输入已保存；点按重试后，3 秒等待期间焦点保持在按钮、`aria-disabled=true` 且原生 `disabled=false`。第二次创建成功后进入新会话，焦点回到输入框，原消息只入队一次；代理记录创建 POST 两次（首次拒绝、重试成功）、队列 POST 一次。320px 页面无横向溢出，浏览器脚本错误为空。此阶段只补打包页证据，代码和根目录单文件包沿用提交 `a703586`。原生 WebView 和实体触控仍待验收；未运行压力或高负载测试。

## 2026-09-28：提交状态核对保留操作与键盘焦点

打包版队列 POST 已被服务端接受但响应断开时，聊天框需要让用户核对队列，不能直接重发。旧实现把核对按钮原生禁用，异步读取时会失去键盘焦点；后台队列刷新若同时读失败，还会把待核对提示覆盖成普通错误，使核对入口消失。现在核对期间用 `aria-disabled` 和一次性执行锁保留按钮焦点并挡住重复激活；同一未确认提交的后台读失败保留待核对入口。核对失败后更新错误说明并把焦点交给新的核对按钮，成功后在按钮消失时交给输入框。新任务核对入口同样使用这一焦点和重复激活逻辑，迟到结果不会覆盖已经切换的页面。

有界代理新增 `--queue-read-delay-ms`（最多 5 秒）和 `--queue-read-failures`（最多 8 次），仅用于隔离环境。最终 Windows 单文件 Home `.build/mdo-packed-docks-yiv2j4h6` 将首次队列 POST 响应丢弃，再延迟并拒绝队列读取：核对入口持续可见；点击后按钮在异步期间仍有焦点、`aria-disabled=true` 而原生 `disabled=false`；失败后新核对按钮重新获焦，允许键盘重试。重复点击未产生第二次队列 POST，代理总计一次。320px 页面无横向溢出，浏览器脚本错误为空。Windows/Linux 有界门禁均通过 114 项 Python、77 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `202e48017211ad0719f60b3a4fa358ce7ed034c1d572ea688a53cddfeecbe71b`，Linux 根目录 `mdo` 为 `68166a13832e02b090342a3fecca457bde3b09ff729e937d8f7ef7a5d6c5e347`。原生 WebView 和实体触控仍待验收；未运行压力或高负载测试。

## 2026-09-28：聊天框停止请求期间保留焦点并隔离会话

旧单文件 Home `.build/mdo-packed-docks-r3fcc_m1` 在 320×350 点击“停止当前任务”后立即原生禁用按钮，5 秒有界代理延迟内焦点掉到页面根节点。现在停止按钮在请求期间保持可聚焦，以 `aria-disabled` 和运行 ID 锁防止重复提交；停止完成且该按钮消失时，焦点返回输入框。迟到响应仅在原会话仍被选中且运行 ID 未变时更新当前运行状态，切到另一会话不会覆盖其状态或错误区。

Windows 单文件 Home `.build/mdo-packed-docks-rff829k0` 的 5 秒延迟中确认按钮保持焦点、第二次按 Enter 没有重复停止请求，结束后焦点回输入框。Home `.build/mdo-packed-docks-hturksik` 先创建并完成第二会话，再从第一会话发起停止并立即切换；响应返回后第二会话仍“就绪”、保留自己的回复、没有旧会话错误，返回第一会话可见“已停止”和恢复入口，代理只记录一次运行 DELETE。最终重新打包 Home `.build/mdo-packed-docks-moipjuuc` 又在 320px 复核等待期 `aria-disabled=true` 而原生 `disabled=false`、停止后输入焦点、无横向溢出或脚本错误，代理仍只有一次 DELETE。夹具新增 `--run-cancel-delay-ms`（上限 5 秒），只作用于隔离环境的运行停止请求。

Windows/Linux 有界门禁均通过 114 项 Python、77 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `5081f39876d2316ff1e590a7c8f33efd3aef2185e7accdaa35c2dc5621c1284d`，Linux 根目录 `mdo` 为 `182b5eb44c73a9fa94aa32fc0f6c1813971de5338125a61c7db5dcac95403eea`。原生 WebView 和实体触控仍待验收；未运行压力或高负载测试。

## 2026-09-28：资源设置页收起空表单间距

320×350 单文件设置页切到“权限”等资源分区时，常规/Agent/联网表单的子分区虽已隐藏，表单自身仍保留 70px 内边距，使分类栏到标题出现约 92px 空白。现在切换分区时一并隐藏不包含当前分区的表单；返回表单分区时原节点与未应用选择保持。修复后的 Windows 单文件 Home `.build/mdo-packed-docks-5xqivsih` 实测“权限”和“计划任务”标题距分类栏 22px，文档宽度 320px；从“常规”选浅色但不应用，经“权限”“计划任务”返回后仍保持草稿，“Agent”表单也正常恢复，浏览器脚本错误为空。Linux 单文件 Home `/home/ubuntu/.cache/mdo-linux-qa-73d848b/mdo-current/.build/mdo-packed-docks-ae_op9b7` 在同尺寸复核“诊断与存储”标题间距 22px、无横向溢出和脚本错误。

Windows/Linux 有界门禁均通过 114 项 Python、77 项 Node、71 个前端模块解析、21 个运行探针和确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `1895df5c5c2dd94ee5bbb4b109afe26b15aeb0084f0645538d193f50c292a072`，Linux 根目录 `mdo` 为 `6b5af67459218b0df968de3c118ce56a012c7252f5a7a52da3c87594a38e47f4`。其他设置分区与实体触控仍待验收；未运行压力或高负载测试。

## 2026-09-28：单文件打包页验证交错 Chat Completions 流

扩展有界本地模型夹具 `tests/manual_packed_docks_qa.py --interleaved-chat-stream`，令真实打包进程通过 Chat Completions 协议接收两段同时含正文与思考字段的 SSE：`Hello ` / `Thinking `，接着 `world` / `again`，最后返回用量 7 输入、3 输出。Windows 单文件 Home `.build/mdo-packed-docks-molh0fln` 的持久事件依次为正文、思考、正文、思考和完成事件；页面只显示一张 `Hello world` 回复卡和一张 `Thinking again` 思考卡，用量与 token/s 落在该回复上，刷新后仍一致。320×350 页面无横向溢出或脚本错误。第二个单文件 Home `.build/mdo-packed-docks-jymrn_94` 验证点赞状态刷新后保留。两次夹具均确认交错流实际由模型端点送出。这覆盖打包进程到时间线和消息操作的协议路径；线上 Ling 服务和其他系统原生 WebView、实体触控仍待验收。

本阶段 Windows/Linux 有界门禁通过 114 项 Python、77 项 Node、71 个前端模块解析、21 个运行探针和确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。重新生成的根目录 `mdo.exe` SHA-256 为 `ee25ae6c5147259c881db5ed73c69d9e97891f095e4dc7f3f55297d1dbfa9cca`，Linux 根目录 `mdo` 为 `2f6c4ae4c95a994cf290a38ce1acbd7fabc532d080f9d6b86fb464ca6494fff9`。未运行压力或高负载测试。

## 2026-09-28：交错流式事件保持同轮消息完整

时间线原来只把相邻的同类流片段接在一张卡上。`xllm` 的 Chat Completions 解析器可从同一增量依次发出正文和思考；正文、思考与后台事件一旦交错，同一模型回合就可能出现多张相同键的回复或思考卡，回复末尾的反馈与 token 用量只落在最后一张。现在按流键归并整个模型回合的同类片段，保留首个片段的位置、完整文本和单一操作卡；每次时间线重放重新建立映射，不跨运行或模型回合共享。

新增 Node 回归先复现交错事件产生 7 张卡而只有 4 个唯一键，修复后验证一张用户、一张回复、一张思考和一张后台任务卡，正文 `Hello world`、思考 `Think again`、反馈事件和用量均归属同一回复。生产模块浏览器夹具又验证增量更新后复制按钮焦点返回，最终只有一张回复卡且点赞/点踩、统计可见；320px 时间线无横向溢出，脚本错误为空。最终单文件 Home `.build/mdo-packed-docks-pjgt3fph` 用本地确定性 Markdown 回复验证普通路径仍显示复制、分叉、重试、反馈及 token/s，320px 无横向溢出和脚本错误。交错 Chat Completions 流的后续单文件实测见上一节。Windows/Linux 有界门禁通过 114 项 Python、77 项 Node、71 个前端模块解析、21 个运行探针和确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `ee25ae6c5147259c881db5ed73c69d9e97891f095e4dc7f3f55297d1dbfa9cca`，Linux 根目录 `mdo` 为 `2f6c4ae4c95a994cf290a38ce1acbd7fabc532d080f9d6b86fb464ca6494fff9`。其他系统原生 WebView 与实体触控仍待验收；未运行压力或高负载测试。

## 2026-09-28：并行决策到达时保留询问输入位置

用户在一张询问卡输入自由回答时，另一张询问或审批可能从后台任务到达。旧停靠区无条件滚到新决策；110px 高的生产样式夹具中，第二张询问让滚动位置从 67px 立即跳到 186px，随后回到 128px。审批卡插入在询问前面时，即使跳过主动滚动，也会把原输入框从停靠区顶端 61px 推到 226px。现在以当前询问输入框作为滚动锚点，保留它的草稿、焦点和可见位置；新决策仍加入视图并触发到达通知。用户没有在编辑询问时，既有的新决策自动露出行为保持。

浏览器夹具验证了第二张询问到达时滚动固定 67px、审批出现和移除时输入框相对位置维持约 61px、草稿与焦点保持，空闲时第三张询问仍自动露出；输入法与跨会话询问夹具也通过。最终单文件 Home `.build/mdo-packed-docks-37cxxj7f` 在 320×350 顺序完成询问提交、审批拒绝和回合回复，页面无横向溢出或脚本错误。单文件夹具尚未制造“正在输入时并行到达”的事件，该边界证据来自生产模块浏览器夹具。Windows/Linux 有界门禁通过 114 项 Python、76 项 Node、71 个前端模块解析、21 个运行探针和确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `8596e43102d5e231f3dfeffcb8fdd6a2de7605eabea6c6ae940bb2d9e34ea036`，Linux 根目录 `mdo` 为 `4fef777c6f08afde76b6087c58daff430d4ed55a0ff5932669eee77a2245cbe7`。其他系统原生 WebView 和实体触控仍待验收；未运行压力或高负载测试。

## 2026-09-28：队列刷新保留正在阅读的消息

运行中的队列轮询原本每次清空队列容器并重建卡片；既有滚动锚点和按钮焦点恢复无法保留文本选区。现在按队列项 ID 复用内容未变化的卡片和列表节点，状态或内容改变时仍更新对应项。前项被移除时先删除失效节点，再原位更新后项序号和移除按钮的可访问标签，避免移动正在阅读的后项。跨会话切换仍建立独立列表。

浏览器夹具验证重复渲染、服务端刷新、新增后项、移除前项后选区与节点保持，同时保留中段/底部滚动、移除当前项后的锚点及跨会话滚动复位。最终单文件 Home `.build/mdo-packed-docks-d7n4rljn` 在 30 秒有界慢回复中排入长消息，文本选区经过 3.2 秒运行中刷新仍在；320×350 下队列内部和页面均无横向溢出，脚本错误为空。前项移除时的选区仅由浏览器夹具验证，未在单文件页单独复测。Windows/Linux 有界门禁通过 114 项 Python、76 项 Node、71 个前端模块解析、21 个运行探针和确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `15fba37dbf7b2831140b2137f714585a57ee2b40c7e90cfcba74579a74ff3fee`，Linux 根目录 `mdo` 为 `ace3216e2235234d681271b77c1f0e9c503ed6f3a15436c4abe4e7f080c3f275`。其他系统原生 WebView 和实体触控仍待验收；未运行压力或高负载测试。

## 2026-09-28：顶栏会话菜单刷新时保留阅读位置

顶栏与手机顶栏的“更多会话操作”使用独立菜单组件。旧实现每次收到会话元数据通知都会清空菜单，即使只更新 revision 或时间戳；320×350 生产样式夹具在聚焦最后一项后得到 `headerPollStable=false`。现在菜单按会话标识和实际可见操作集合复用节点，元数据刷新不会打断已聚焦的操作或内部滚动；会话状态、语言或可用操作真正改变时仍重新生成菜单，关闭后再次打开从顶部开始。操作提交继续在点击时读取当前会话对象。

修复后的浏览器夹具在 320×350 验证原节点、焦点与滚动跨元数据更新保持；模拟归档后操作项更新，恢复后首尾键盘循环、输入法候选 Esc、普通 Esc 焦点返回以及重新打开从顶部开始均通过。单文件 Home `.build/mdo-packed-docks-pzqb_26j` 在 320×350 运行 30 秒有界慢回复，菜单末项与 122px 滚动经过约 9.5 秒保持，页面无横向溢出和脚本错误；回复完成时应用把焦点送回输入框，菜单随之关闭，此阶段不改变这一既有焦点行为。Windows/Linux 有界门禁通过 114 项 Python、76 项 Node、71 个前端模块解析、21 个运行探针和确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `6dd21fa4c2143e12cc11cf110292d81aab0849985190957322e70088435b2696`，Linux 根目录 `mdo` 为 `ae3dba8c4bb7bbe2472275287c789f54877c88981edd05e5721a5eb1aaa55dad`。其他系统原生 WebView 与实体触控仍待验收；未运行压力或高负载测试。

## 2026-09-28：会话列表轮询不再重建未变化条目

运行期间列表资源会重复进入刷新状态，原侧栏每次都清空并重建全部条目与浮层菜单。320×350 生产样式夹具在仅刷新时间戳时得到 `titleSelectionStable=false`；持续阅读长操作菜单也会丢失节点和内部滚动。现在会话列表按实际显示字段判断是否需要重绘；仅修订号、时间戳或资源的刷新状态变化时，原位更新时间而保留条目和菜单。行点击和菜单提交在执行时读取最新会话对象，避免 DOM 复用后使用过期修订号。标题、排序、状态等确实改变时仍完整更新列表。

最终浏览器夹具在 320×350 验证标题选区、时间标签、菜单末项焦点与滚动位置跨无关刷新保持，并确认菜单动作收到新 revision 2；既有菜单定位、输入法候选 Esc、Home/End 和首尾循环检查全部通过，脚本错误为空。最终单文件 Home `.build/mdo-packed-docks-3hk5843e` 在 320×350 验证长菜单可滚至末项（内部滚动 225px）、Esc 返回原入口，文档无横向溢出或脚本错误；该打包页操作没有单独注入刷新，刷新稳定性证据来自生产模块夹具。Windows/Linux 有界门禁通过 114 项 Python、76 项 Node、71 个前端模块解析、21 个运行探针和确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `3080c1dd49734c44684ee5b921de9b25919b25099da8c8e825edf6e77540ae66`，Linux 根目录 `mdo` 为 `f88120dc1c75ced50237461ebbdf160f5d9172c73a41938c9237e8847562fbaf`。实体设备触控与原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：待办与后台任务轮询保留文本选区

停靠区曾在任意状态刷新时清空并重建待办、错误和后台任务卡。旧单文件 Home `.build/mdo-packed-docks-__vf4iek` 中选中“Inspect UI”后，约 3.8 秒空闲轮询便清掉选区。现在按会话和实际显示字段复用待办、任务卡，只在文字、完成状态、任务状态或语言改变时更新；卡片增删时逐节点调整，保留未变化的相邻卡片。审批卡也采用相同的逐节点调整，避免多张审批卡互相打断阅读。

生产样式 320×350 浏览器夹具验证了无关轮询及元数据变化时选区保留、实际内容变化时卡片更新，以及待办与任务卡交叉变化时另一张卡的选区不丢失。最终单文件 Home `.build/mdo-packed-docks-j__mu890` 中，两卡并列时待办选区经过约 3.8 秒轮询和后台任务完成、任务卡移除后仍在；先前构建的 Home `.build/mdo-packed-docks-bg050e8t`、`.build/mdo-packed-docks-qjmnm209` 分别验证待办和任务文字在独立轮询中的选区。320px 页面无横向溢出，脚本错误为空。Windows/Linux 有界发布门禁通过 114 项 Python、76 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `49b2ed01eedfe67a5bfd683cefbcd976565a18cef1a1bf3681264b35e89e8ee2`，Linux 根目录 `mdo` 为 `8a06cee00874022f7112424752335ca20ef01356c4d89ee90ace38702c8c4d5e`。其他系统原生 WebView 和实体触控仍待验收；未运行压力或高负载测试。

## 2026-09-28：审批轮询期间保留命令选区

审批待决时每秒刷新剩余时间。旧单文件 Home `.build/mdo-packed-docks-fzm443ae` 中，在对话停靠卡展开调用参数并选中文字，约 2.4 秒后选择消失：轮询每次都重建整张卡。检查器“决策”页也采用同样的整卡重建。现在两处审批视图按审批 ID 复用内容未变化的卡片，只更新倒计时和按钮的等待状态；调用参数、资源或风险真的变化时才换节点，切换语言时重新本地化。脱离文档的旧参数折叠项不再回写展开状态。

生产样式浏览器夹具在 320×350 验证停靠卡倒计时变化时卡片和参数节点不变、选区保留，调用参数变化时节点更新；独立检查器夹具验证相同两条路径，均无脚本错误。单文件 Home `.build/mdo-packed-docks-izlo7nvx` 中停靠卡选择完整 JSON 后经过 2.4 秒轮询仍保持选区；最终 Home `.build/mdo-packed-docks-3tqfzuki` 中检查器参数选区经过 2.5 秒仍在，点击“允许一次”后无害工具返回 `exit_code: 0`、待审批清空、输入重新可用，脚本错误为空。Windows/Linux 有界门禁通过 114 项 Python、76 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `c1764185c5578b4987db14da9c86940891871c90d7421eb97f78dc1934818227`，Linux 根目录 `mdo` 为 `32d667a4b54910680a449ce6efec75fd856f5fa89d6368185d16b7c2dc4d7e8e`。其他系统原生 WebView 和实体触控仍待验收；未运行压力或高负载测试。

## 2026-09-28：极短屏新审批先显示决策标题

隔离单文件 Home `.build/mdo-packed-docks-ilhh5wjg` 在 280–320×250 下先显示待办，再由同一模型回合依次触发询问和审批。回答询问后，停靠区将新审批自动滚到“查看调用参数”：320px 时审批标题顶端为 -37px，按钮顶端为 127px，分别落在停靠位 52–121px 的两侧，首屏只显示卡片中段。新逻辑根据停靠位是否容得下标题与一个 40px 操作按钮：极短时先展示审批标题及风险，保留内部滚动到资源与操作按钮；正常高度仍按原定位方式展示更多内容。

最终单文件 Home `.build/mdo-packed-docks-7uxlx__z` 在 280×250 重放待办→询问→审批：新审批标题位于 y=65–84px、停靠位 y=52–121px、文档宽 280px；从标题向下分段滚动能访问并点击“允许一次”，无害命令结束。随后在 320×350 再触发独立审批，标题、风险、命令和参数入口保持可见，按钮可滚动点击，回复完成；浏览器脚本错误为空。Windows/Linux 有界门禁通过 114 项 Python、76 项 Node、71 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `998c27da276c6508933c624920ef29293aae0cd74b09502df6d4e44cb9dd16bb`，Linux 根目录 `mdo` 为 `42f6257e5b95f9d768085082d9d6f4a8d7668205d01a0a451926c107eb6dd524`。实体触控、软键盘和原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：停止任务等待期间保留键盘焦点

前一阶段用原生 `disabled` 同步停止按钮时，隔离单文件 Home `.build/mdo-packed-docks-77_5mbse` 中按 Enter 停止详情任务，焦点立即落到页面 `<body>`，键盘用户失去当前位置。现在两个入口在请求期间使用 `aria-disabled` 保持可聚焦，`cancel()` 仍按任务 ID 拦截重复激活；视觉上显示等待状态，轮询重绘也能恢复原按钮焦点。任务真正停止、按钮消失时，详情焦点转到“返回任务列表”。

最终单文件 Home `.build/mdo-packed-docks-_nou4wc8` 在 5 秒有界 DELETE 延迟期间，按 Enter 后及 1.7 秒轮询后焦点都保持在详情“停止”，两处按钮均报告 `aria-disabled=true`；请求完成后任务显示“已停止”、“返回任务列表”获焦。代理仅记录一次任务 DELETE，页面脚本错误为空。Windows/Linux 有界门禁通过 114 项 Python、76 项 Node、71 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过零旁路写入及 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `14e47972fba8b8fd2bda614c7735ccc065fdebbef52e5f6940f2395d29ed7aff`，Linux 根目录 `mdo` 为 `f462793b7d8cdadd57387b034f490ecb1c635c5fcb040872b200ceb153e4e1fb`。实体触控及原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：任务停止请求在列表与详情之间去重

后台任务的列表行和详情头部都提供“停止”，原实现只禁用被点击的按钮。隔离单文件夹具将任务 DELETE 延迟 5 秒后，旧页面在连续点击两个入口时实际发出两次请求；轮询重建按钮还可能清除局部禁用状态。现在任务面板以任务 ID 记录进行中的停止请求，两个入口同步禁用；轮询重建的按钮继承禁用状态，请求结束或失败后统一清理。

最终单文件 Home `.build/mdo-packed-docks-ju8y033o` 中，点击列表停止后立即看到列表和详情两个按钮均禁用，代理只记录一次 `DELETE /api/v1/tasks/1`，任务进入“已停止”，页面无脚本错误。有界 QA 夹具新增 `--task-cancel-delay-ms`（0–5000），便于复查此竞态。Windows/Linux 发布门禁均通过 114 项 Python、76 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过零旁路写入与 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `8f2ba6c7bdb6726a76d4b5e08bd37cf3f377cce6772d2d5cb1ef1f11b68cbe4d`，Linux 根目录 `mdo` 为 `9eca264f512eb9a7a430d04fd06d9edb8a54691ce9055176c602264fb97f4c6b`。原生 WebView、实体触控仍待验收；未运行压力或高负载测试。

## 2026-09-28：后台任务轮询不再打断阅读

旧包的任务检查器每 1.4 秒刷新运行中任务、空闲后每 5 秒刷新，会重建列表和详情 DOM。隔离 Home `.build/mdo-packed-docks-1fc0abg1` 中，键盘焦点放在详情的“返回任务列表”后，下一次轮询把焦点丢到页面主体。修复后列表行、停止按钮、详情返回、输出折叠项和产物按钮按任务与操作标识恢复焦点；标准输出、错误输出、结果和文本产物预览的内部阅读位置在同一任务刷新时保留。旧的 `<details>` 节点脱离文档后不再改写折叠状态。

同轮验收发现另一个被整页宽度检查漏掉的移动端缺口：长任务命令会把任务列表撑到约 840px，产生检查器内部横向滚动。现在网格轨道和卡片允许收缩，名称在卡片内省略，完整命令仍保留在按钮的可访问名称中。隔离夹具新增 `--task-output-lines`（1–120），本次只打印 80 行、约 1.8 KiB 文本，以便有界验证长输出阅读。

最终单文件 Home `.build/mdo-packed-docks-7j9om5sb` 中，任务完成后的详情“返回任务列表”和“标准输出”折叠项各经过 5.5 秒轮询仍保持键盘焦点；标准输出滚到 720px 后继续轮询，内部位置仍为 720px、外层为 349px，折叠状态保持，浏览器无脚本错误。最终移动端 Home `.build/mdo-packed-docks-15ktja48` 用同类长命令在 320px/280px 验证任务面板、列表和卡片各自的 `scrollWidth == clientWidth`，整页宽度等于视口；任务行点按目标高 40px，280px 下可用 Enter 打开详情并保持焦点。Windows/Linux 有界发布门禁通过 114 项 Python、76 项 Node、71 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过单文件零旁路写入及 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `b5166d4703cb9b5ec58a44e33ef907215a81d3469ac4ffe9068f994ea5a9c4ad`，Linux 根目录 `mdo` 为 `309b1e15779a7835d56ff1673bf11f4c5b7d03d289f0ce496960945df31b920d`。实体触控和原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：手机用量面板提供明确关闭入口

旧页的上下文圆环通过鼠标悬停展开；新版改为点按后，320×350 下弹层会覆盖附件缩略图的移除按钮。此前点该位置实际落在弹层上，用户需要回到圆环按钮才能继续操作。现在弹层顶部有三语命名的 40×40px 关闭按钮，点击后将焦点交还圆环；Esc 与外部点击仍可关闭。用量事件刷新时保留弹层滚动位置和关闭按钮焦点，重新打开时从顶部阅读。

单文件 Home `.build/mdo-packed-docks-hx_08dhk` 用合成 PNG 在 320×350 验证：弹层关闭按钮大小 40×40px，关闭后输入焦点回 `context-meter-trigger`，紧接着点附件移除会清除图片并恢复纯文字估算。280×250 下按钮完整位于 y=65–105px，弹层可滚动，关闭后焦点仍回入口；两种尺寸的文档宽度均与视口一致，浏览器脚本错误为空。组件测试覆盖按钮关闭、焦点、刷新时的滚动位置和 Esc 行为，三语言键检查通过。Windows/Linux 有界发布门禁均通过 114 项 Python、76 项 Node、71 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `25d462c6411a14f9aa1e123fda6312acfa1a479b41007b988539c6ee5737d296`，Linux 根目录 `mdo` 为 `b88bb8ddb8055d95ffd3c56704e99668429790ac7a7920c6868995bc971bd171`。实体触控和原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：附图时明确输入 token 估算边界

旧版输入框空白时不占用 token 数字的位置。新版此前无论输入是否为空都显示“输入 ~0 tok”，附图后也只估算文字却仍使用“输入”标签，容易被误认为已计算图片 token；增删图片时该提示不会刷新。现在空白输入区不显示数字，纯文字维持原来的粗估；有图片时改为“文字 ~N tok · 图片另计”，详情面板改用“本次文字估算”并说明图片 token 未计入。附件增删立即刷新两处提示，三种语言保持同一语义。没有猜测图像的模型相关费用。

单文件 Home `.build/mdo-packed-docks-nzzw04lo` 使用合成的 1×1 PNG 验证：320×350 页面中，输入“测试图片 token”先显示“输入 ~6 tok”；上传后显示“文字 ~6 tok · 图片另计”，详情面板写明图片 token 未计入；移除后恢复纯文字提示和面板内容。文档宽 320px，浏览器脚本错误为空。组件测试覆盖空输入、文字、附图、移除的状态切换，三语言键检查通过。Windows/Linux 有界发布门禁通过 114 项 Python 测试、Node 用例、71 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `37dc3801f7707d8bef8ccda0a7da2061098f2000ebfa96e4b3424fdf1691089c`，Linux 根目录 `mdo` 为 `f808be6a0a6491e7f153f2e3b5bba1b8fe83943fbd4b40134d6efc282e684fae`。实体手机软键盘与原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：长 UTF-8 路径可在输入区补全

旧单文件 Home `.build/mdo-packed-docks-6lmvjl5q` 的工作区里存在 48 个汉字组成的文件名；输入区接受这 48 个字符，但 `/workspace/files` 只允许 128 UTF-8 字节，144 字节的查询返回 `invalid_query` 400，候选静默消失。现在服务端查询上限与其已有的 512 字节相对路径缓冲区一致，为 511 UTF-8 字节；前端按相同字节上限判断，避免多字节字符的字符数与服务端字节数不一致。扫描目录数、条目数、深度和返回候选数的既有界限不变。

真实 xs/TCC API 探针验证 144 字节中文查询返回对应文件、511 字节查询可接受而 512 字节明确拒绝。最终单文件 Home `.build/mdo-packed-docks-g_ndy1hu` 在 320×350 下输入 `@` 加 24 组“资料”后显示 `notes/` 下的长文件候选；按 Tab 插入完整引用，光标与焦点留在输入框，刷新后草稿保留。文档宽 320px、浏览器脚本错误为空。Windows/Linux 有界门禁均通过 114 项 Python、全部 Node 用例、71 个模块解析、21 个运行探针及确定性打包；Windows 另通过零旁路写入与 20 秒单文件启动检查。根目录 `mdo.exe` SHA-256 为 `62c98628ad4e1d68774f550f796febd943f94044114ddb645ed4cc29731258c1`，Linux 根目录 `mdo` 为 `c257fa813b28ca13d2c59cc08910229e9497ce9b24d29e7a7731acbe43b2c827`。原生 WebView、实体手机软键盘仍待验收；未运行压力或高负载测试。

## 2026-09-28：权限资源页显示与输入区一致的方案名称

旧单文件 Home `.build/mdo-packed-docks-s1w031kr` 的权限资源页把内置方案直接显示为 `read-only`、`balanced`、`full-access`，默认项也显示 `balanced`；输入区却使用“只读 / 询问 / 完全访问”。现在权限资源页复用 Agent 卡片已使用的稳定 ID 映射，内置方案与默认项按当前语言显示，自定义方案名称保留原文；中文说明将混用的 “profile” 改为“权限方案”。

最终单文件 Home `.build/mdo-packed-docks-6xrzuavm` 在中文桌面页显示“默认权限方案：询问”及三项中文方案；320×350 下切到俄语预览，默认项与三张卡分别显示“Спросить / Только чтение / Спросить / Полный доступ”，页面宽 320px、浏览器脚本错误为空。本次只验证语言预览后的资源页，未提交语言设置；语言应用与刷新持久化证据另见前文。Windows/Linux 有界门禁通过 114 项 Python、全部 Node 用例、71 个模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `51a0609b46b0bddaeef2894368bc5985debac40a0cc8b884faa9d9cccc3d5593`，Linux 根目录 `mdo` 为 `b27cb9b9625433d7b7df43f3b91ee89b215310fe300ea0ce43713f8d2febace5`。其他未本地化服务端错误和资源描述仍按审计继续处理；未运行压力或高负载测试。

## 2026-09-28：询问提交期间切换会话的单文件验收

隔离打包夹具新增 `--ask-delay-ms`，只让询问 PUT 延迟最多 5 秒，以检查提交回调与当前会话切换的边界。单文件 Home `.build/mdo-packed-docks-t0we1qu_` 在 320×350 下，第一会话发送 `ASK UI` 并出现“需要你回答”；点击 Fast 后立即打开手机侧栏并切到第二会话，输入未发送草稿 `SECOND SESSION DRAFT`。第一会话的迟到回答完成时，第二会话仍无询问卡、标题和路由不变，草稿与输入焦点保留；切回第一会话可见完整 Agent 回复且待答清空。再回第二会话并刷新，草稿仍在。代理记录询问 PUT 恰好 1 次，运行与队列 POST 各 1 次；文档宽 320px、浏览器脚本错误为空。

此阶段只扩展有界 QA 夹具并补充打包页证据，应用代码未改变；夹具 `py_compile` 与 `git diff --check` 通过，沿用上一阶段同应用字节的 Windows/Linux 有界门禁。根目录 `mdo.exe` 已重建，SHA-256 仍为 `c1c4eb91e709542ef0cad76e9e072e0726033d4fe892852fca4ac0c190896c4c`。旧有模块夹具继续覆盖询问失败后切换、返回及重试。原生移动端触控与 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：完整斜杠命令收起候选并保留 Tab 导航

旧版只在斜杠输入仍有更长匹配项时显示候选；完整 `/help` 由 Enter 或发送按钮执行。新版单文件 Home `.build/mdo-packed-docks-u1w3ldcc` 在输入完整 `/help` 后仍显示唯一候选，按 Tab 直接打开帮助弹层，夺走了正常键盘焦点导航。现在完整且没有更长匹配项时收起候选，部分输入仍可用 Tab 选择；精确命令继续由现有发送路径执行。

生产模块浏览器夹具确认完整 `/help` 隐藏菜单、部分 `/he` 点击执行以及文件候选点按均通过。最终单文件 Home `.build/mdo-packed-docks-jqpl8uzc` 在 320×350 下输入 `/help` 后菜单隐藏、Tab 把焦点移到附件按钮且未打开弹层，回输入框按 Enter 则打开帮助；关闭后输入 `/he`，Tab 选中 `/help` 并打开帮助。文档宽 320px、浏览器脚本错误为空。Windows/Linux 有界门禁通过 114 项 Python、全部 Node 用例、71 个模块解析、21 个运行探针与确定性打包；Windows 零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` SHA-256 为 `c1c4eb91e709542ef0cad76e9e072e0726033d4fe892852fca4ac0c190896c4c`，Linux 根目录 `mdo` 为 `0c6d44835e136107336e30800c7984b3fc971692a88b350d8952e4c893b69ba8`。实体软键盘及原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：文件名内的 @ 不再截断补全

旧版 `app_bak/wwwroot/src/ui.js` 的文件补全允许首个 `@` 之后继续输入 `@`，但模块版把后续 `@` 当成引用边界。旧 Windows 单文件 Home `.build/mdo-packed-docks-ty_0rpx3` 中，服务端 `/workspace/files?q=alpha%40b` 返回 `src/alpha@beta.c`，输入 `@alpha@b` 却无候选。现在只把引用开头的 `@` 当标记，后续 `@` 保留在查询和完整引用后缀里；隔离夹具固定提供 `src/omega@beta.c`。

最终单文件 Home `.build/mdo-packed-docks-i78k6_tl` 的 320×350 页面在 `@omega@b` 下显示 `src/omega@beta.c`；从 `@omega@beta.c next` 的中途选择后得到 `@src/omega@beta.c next`，光标在 `next` 前、输入焦点保留，菜单关闭。普通 `@alp` 仍显示原有两个文件候选；文档宽 320px、脚本错误为空。Windows/Linux 有界门禁均通过 114 项 Python、全部 Node 用例、71 个模块解析、21 个运行探针及确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `0efa915d88bc2230d951ef86b9c0d227a8cba7c01befb9fd07a79a9710e71f9a`，Linux 根目录 `mdo` 为 `e780b9ad1c990ae45c92ab3b9acf8e1ce5a21b8a37ecd945b27c7facfbd4e5bc`。实体手机软键盘和原生 Linux WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：Linux 单文件服务上的移动宽度文件补全

将提交 `57ab517` 的同一源码同步到 Linux ext4，用锁定的 xserver 重新生成 Linux 根目录 `mdo`，并以单文件隔离 Home 启动真实 HTTP 服务。在 Windows 内置浏览器连接该 Linux 服务的 320×350 页面，验证从 `@alpha.c and more` 的 `@alp` 中途按 Enter 选择后得到 `@src/alpha.c and more`；`/explain @alp` 仍出现两个文件候选，选择后得到 `/explain @src/alpha.c `；`@QA next` 从引用中途按 Tab 选择带空格路径后得到 `@"notes/QA notes.txt" next`。三次选择后光标均落在正文前、焦点保持在输入框。刷新后最后一条草稿仍在，文档宽 320px、浏览器脚本错误为空。

隔离 Home 为 Linux ext4 的 `.build/mdo-packed-docks-vgjjvbu3`；此项验证的是 Linux 打包后端与浏览器页面，并未运行原生 Linux WebView 或实体手机软键盘。根目录 `mdo.exe` 已重建，SHA-256 为 `4b608356be692da3974b76eb97ecce9c5d79f8acd66e11e7ec8cf5da4a49dfc2`；Linux 根目录 `mdo` SHA-256 为 `0c648ebb3dd08b77827e02319487e44bbf5d1ce7b8ec0e39ac082757c424958e`。应用源码未改，沿用上一阶段同源码的 Windows/Linux 有界门禁；未运行压力或高负载测试。

## 2026-09-28：斜杠开头的普通输入仍可补全文件

旧版只在输入尚为无空格的斜杠命令时优先显示命令候选。新版文件补全额外拒绝所有以 `/` 开头的输入，导致隔离单文件 Home `.build/mdo-packed-docks-_bcrmrms` 中的 `/explain @alp` 等普通正文既没有斜杠菜单，也无法选择工作区文件。现在文件补全只按光标处的 `@` 引用识别；斜杠命令仍由自己的菜单处理，不再阻断后续普通正文里的文件引用。

最终单文件 Home `.build/mdo-packed-docks-e9jk76i0` 在 320×350 下，`/explain @alp` 显示 `src/alpha.c` 与 `src/alpha-test.c`，按 Enter 后变为 `/explain @src/alpha.c `，输入焦点保持。输入 `/he` 时只显示 `/help`，执行后打开帮助弹层，关闭后焦点回输入框。文档宽 320px，浏览器脚本错误为空。Windows 有界门禁通过 114 项 Python、75 项 Node、71 个模块解析、21 个运行探针、确定性打包、单文件零旁路写入与启动检查；根目录 `mdo.exe` SHA-256 为 `4b608356be692da3974b76eb97ecce9c5d79f8acd66e11e7ec8cf5da4a49dfc2`。同源码 Linux ext4 有界门禁与确定性打包通过，Linux 包 SHA-256 为 `0c648ebb3dd08b77827e02319487e44bbf5d1ce7b8ec0e39ac082757c424958e`。实体软键盘和原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：文件补全替换整个引用并保留正文间距

旧单文件 Home `.build/mdo-packed-docks-32h2bkc2` 中，输入 `@alpha.c`、把光标移到 `@alp` 后选择 `src/alpha.c`，输入区变成 `@src/alpha.c ha.c`：只替换了光标前的前缀，留下原文件名后缀。现按光标所在的完整、未加引号的 `@` 引用计算替换范围；引用后已有空白时复用它，并把光标移过分隔符，避免正文前出现两个空格或继续输入时把字贴到文件名后。

最终隔离单文件 Home `.build/mdo-packed-docks-fhagrpb7` 在 320×350 下，从 `@alpha.c and more` 的 `@alp` 处选中后得到 `@src/alpha.c and more`，光标位于 `and more` 前；从 `@QA next` 选择带空格路径后得到 `@"notes/QA notes.txt" next`，光标同样在正文前。没有后续正文时，从文件名中途按 Tab 补全为 `@src/alpha.c `，输入焦点保持、菜单关闭；文档宽 320px，浏览器脚本错误为空。Windows 有界门禁通过 114 项 Python、75 项 Node、71 个模块解析、21 个运行探针、确定性打包、单文件零旁路写入与启动检查；根目录 `mdo.exe` SHA-256 为 `9a357dcbc8bfba9a2d5a19876d84d067b035fd9a70128452fe545644bb4c26e9`。同源码 Linux ext4 有界门禁与确定性打包通过，Linux 包 SHA-256 为 `7c8e01a08b3def8ac927d5daa026df50e6237920739fc0158ab4c80d342a69bc`。实体软键盘与原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：同轮询问后审批的单文件执行边界

为补查“审批与询问同时到达”，隔离模型夹具新增 `SEQUENTIAL DECISIONS UI`：同一 Responses 输出一次返回 `ask_user` 与无害 `exec` 两个工具调用。实测当前宿主逐个推进待决工具：先只显示询问卡，回答后询问消失才出现审批卡；同一会话并未同时出现两张待决卡。因此上一阶段的同时到达优先级修复仍由生产模块夹具证明，不能把这条顺序链算作双决策同时到达的打包证据。

最终单文件 Home `.build/mdo-packed-docks-1ahiz21t` 在 320×350 下按“询问 → 选择 Inspect → 审批 → 允许一次”完成，`exec` 返回 `exit_code: 0`，随后出现 Agent 回复；审批消失后焦点回输入框，文档宽 320px，浏览器脚本错误为空。夹具 `py_compile` 通过。根目录 `mdo.exe` 已重建，SHA-256 仍为 `f78aa595b2f5e37e5155e1358389e642221d13792a26001429c5858310716466`；应用源码未变，沿用上一阶段通过的 Windows/Linux 有界门禁和确定性打包证据。未运行压力或高负载测试。

## 2026-09-28：审批与询问同时到达时保持审批优先

旧版停靠区按审批、询问、待办顺序展示。新版也先渲染审批，但两种决策同时新出现时，把最先展示的审批卡通知为 `ask`；手机端若正在查看审批检查器，会因此走关闭检查器的路径，而不是定位新审批。生产模块移动夹具在 320×350 下先复现回调类型为 `ask`，现在依据实际选中的新卡返回 `approval`。夹具同时保留了两类卡片和待办、任务的排版检查，最小操作目标 40px，文档宽 320px，浏览器错误日志为空。

最终单文件 Home `.build/mdo-packed-docks-t2j8o5km` 在 320×350 下通过本地无害 `exec` 触发普通审批，三个决议按钮均高 40px 且在视口内；点击“拒绝”后审批卡消失、焦点回输入框，时间线记录拒绝结果与完整回复，无脚本错误。这个打包页场景只核对常规审批路径；审批与询问同时到达的优先级证据来自生产模块夹具，尚需打包页双决策注入。Windows 有界门禁通过 114 项 Python、75 项 Node、71 个模块解析、21 个运行探针、确定性打包、单文件零旁路写入和启动检查；根目录 `mdo.exe` SHA-256 为 `f78aa595b2f5e37e5155e1358389e642221d13792a26001429c5858310716466`。同源码 Linux ext4 有界门禁与确定性打包通过，Linux 包 SHA-256 为 `de1c6d9b92688fee9eb0b6099fca3295343af6b2edf8f143d6f86efde8a5b252`。实体触控和原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：顶栏项目切换器显示本地化默认项目名

侧栏和输入区把内置 `default` 项目显示为“默认项目”，但旧单文件包 `.build/mdo-packed-docks-j_jy5nj1` 的桌面顶栏及菜单直接显示内部 ID `default`，无障碍名称也是“切换项目，当前 default”。现在顶栏切换器与侧栏、输入区共用 `nav.defaultProject` 文案；其他项目继续显示自身名称，内部项目 ID 和导航目标不变。

最终隔离单文件 Home `.build/mdo-packed-docks-chdnh_it` 中，中文桌面顶栏和菜单均显示“默认项目”；应用俄语后顶栏、菜单及无障碍名称显示“Проект по умолчанию”。320×350 手机顶栏和菜单也同步，入口高 40px，页面宽 320px，浏览器脚本错误为空。Windows 有界门禁通过 114 项 Python、75 项 Node、71 个模块解析、21 个运行探针、确定性打包、单文件零旁路写入及启动检查；根目录 `mdo.exe` SHA-256 为 `be0bb3600c5e6d5442983acaeb348f5f58d9677be97fd5ca23061444e5c303a3`。同源码 Linux ext4 有界门禁与确定性打包通过，Linux 包 SHA-256 为 `8dcd1dd6697896ede0bddcf0b543506b76df359c2a56d21aaf273e7cae6e6ad4`。实体触控和原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：模型配置表单按稳定字段键本地化

模型配置表单原先根据中文显示文字查找翻译，字段或选项一旦更换文案就可能漏译或误译。隔离打包页在俄语设置中复现了 `Provider` 留在英语、内置 Ling 模型详情显示原始 `context` 的问题。现在字段按表单 `name`、能力与窗口选项按稳定 `value` 查找语言包；Provider 标题、内置模型上下文详情和“思考强度控制”能力补齐中英俄文案。协议、提供商 ID 等用户数据不按同名翻译键改写。

最终 Windows 单文件 Home `.build/mdo-packed-docks-40d93kcd` 在俄语模型页显示“Провайдер”“Управление уровнем рассуждения”及 `131072 токенов контекста`；320×350 视口无横向溢出，保存按钮高 40px。模型表单填入未保存草稿后切到通用设置，将语言预览改为英语再返回模型页，ID 与名称仍在，字段显示“Provider”“Reasoning effort control”；浏览器错误日志为空。Windows 有界门禁通过 114 项 Python、75 项 Node、71 个模块解析、21 个运行探针、确定性打包、单文件零旁路写入与启动检查；根目录 `mdo.exe` SHA-256 为 `131a9050ee47f27c42e227ff3d0f3358ac4323650041f7a228a518c89aaaa3e6`。同源码 Linux ext4 有界门禁与确定性打包通过，Linux 包 SHA-256 为 `de7d87dcd53ab7fd40c452d115874693afbb17869efcb8170e21a72dfae9e762`。实体触控与原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-28：侧栏相对时间持续更新

侧栏会话原先只在列表数据变化或界面重绘时计算“几分钟前”。页面保持打开且无新事件时，标签会停在旧值。现在可见页面每 30 秒原位更新时间文字，后台页恢复可见时立即更新；不重建会话卡，因此已打开的菜单、键盘焦点和滚动位置不受计时器影响。任务面板已有独立的定期状态刷新，无需增加一套计时器。

最终隔离 Windows 单文件 Home `.build/mdo-packed-docks-b7222o7n` 在不刷新页面的情况下观察到侧栏标签从“14 秒钟前”变为“44 秒钟前”，再跨过“51 秒钟前”变为“1 分钟前”；跨分钟时菜单仍保持展开与原按钮焦点，浏览器错误日志为空。Windows 有界门禁通过 71 个模块解析、75 项 Node、114 项 Python、21 个运行探针、确定性打包、单文件零旁路写入与启动检查；根目录 `mdo.exe` SHA-256 为 `f1577e148a9358482a78a68b3b92b7a26b40f67e5e1217c81b5a9c280b727f2d`。同源码 Linux ext4 有界门禁与确定性打包通过，Linux 包 SHA-256 为 `d0700069cc1d2eeaf0cc40daaef3c800cd24c78c31f1906781b9854bed306fb1`。原生 WebView 和实体移动端仍待验收；未运行压力或高负载测试。

## 2026-09-28：设置语言包迟到响应的切页边界

隔离打包夹具新增 `--locale-delay-ms`，只延迟英语/俄语语言包的 GET，最长 5 秒；单独启用 `--fail-first-project` 时也会正确启动代理。单文件 Home `.build/mdo-packed-docks-llp_ljpx` 在默认中文设置中暂选俄语，语言包尚未返回就按 Ctrl+K 切到新任务。代理记录俄语 GET，延迟结束后工作区仍为中文，输入框保持焦点，俄语仅留作设置表单的未应用草稿。回到设置，草稿可继续编辑并显示俄语。

同一 Home 放弃草稿并刷新后，再暂选俄语、立即离开又返回设置；代理记录两次延迟的俄语 GET，最后一次选择生效，页面显示俄语和未预览修改提示。放弃后恢复已保存的中文。浏览器脚本错误为空。本项补强前一阶段的异步切页证据，未修改生产模块；根目录 `mdo.exe` 重建后仍为上一阶段的 SHA-256 `1b234ea83909b1ffbc23565a85e877dff82c44c3a6f02259ac3542ed6966dde6`。Windows 有界门禁通过 114 项 Python、68 项 Node、71 个前端模块解析、21 个运行探针、确定性打包、零旁路写入和 20 秒启动检查；同源码 Linux ext4 门禁与确定性打包通过，Linux 包仍为 `204dfaa58ef4873b1d8f61f8936e11ee983adfb267d781df24be0e74459d85e4`。实体触控及原生 WebView 仍待验收，未运行压力或高负载测试。

## 2026-09-28：未应用外观与语言预览离开设置后还原

设置页会即时预览主题、字号、密度和语言，但旧单文件 Home `.build/mdo-packed-docks-mif58djc` 在把主题暂选为深色后按 Ctrl+K 离开设置，工作区仍保持深色，尽管服务端配置没有应用。现在设置页只在可见期间呈现未应用的外观与语言；离开时恢复服务端已保存值，重返设置则恢复原表单草稿及其预览。较慢的语言包请求仍由现有版本号机制约束，切页后不会用旧请求覆盖较新的选择。

最终单文件 Home `.build/mdo-packed-docks-yvr059cr` 中，默认系统主题/中文时暂选深色/英语并保留新任务草稿，按 Ctrl+K 后工作区恢复系统主题/中文、输入焦点和草稿保持；重新打开设置又看到深色/英语与未应用提示。预览并应用后配置 revision 1→2，工作区和刷新后都保持已保存的深色/英语及草稿。320×350 手机页再暂选浅色/俄语，按 Esc 返回工作区立即恢复已保存的深色/英语；重新打开仍可见浅色/俄语草稿，点击放弃后回到已保存值。页面宽 320px，浏览器脚本错误为空。Windows 有界门禁通过 114 项 Python、68 项 Node、71 个前端模块解析、21 个运行探针、确定性打包、零旁路写入与 20 秒启动检查；根目录 `mdo.exe` SHA-256 为 `1b234ea83909b1ffbc23565a85e877dff82c44c3a6f02259ac3542ed6966dde6`。同源码 Linux ext4 有界门禁和确定性打包通过，Linux 包 SHA-256 为 `204dfaa58ef4873b1d8f61f8936e11ee983adfb267d781df24be0e74459d85e4`。实体触控及原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-27：会话搜索匹配项目显示名

侧栏会话卡按用户可见的项目名分组，但模块版搜索只匹配会话标题、内部项目 ID 和 Agent ID。例如 `Readable QA` 项目的内部 ID 为 `readable-qa`，输入显示名中的空格会得到零命中。现在搜索同时匹配项目显示名；默认项目匹配当前语言的本地化名称，项目 ID 与原有标题、Agent ID 查询继续可用。项目分组标题继续保持可见，命中计数只统计会话。

单文件 Home `.build/mdo-packed-docks-z2b3172v` 中创建 `Readable QA` 项目，并在其中发送标题不含项目名的 `A short test message`。搜索 `Readable QA` 只得到该项目的一条会话，搜索 `默认项目` 只得到原默认项目会话；两次查询都保留正确的项目分组和搜索焦点。320×350 手机侧栏按 `Readable QA` 搜索同样只命中该会话，页面宽 320px；刷新后再次查询结果仍正确，浏览器脚本错误为空。Windows 有界门禁通过 114 项 Python、68 项 Node、71 个前端模块解析、21 个运行探针、确定性打包、零旁路写入与 20 秒启动检查；根目录 `mdo.exe` SHA-256 为 `afc70fffd05eee8fda7909f9aa079ce4b0af613f3e1b12bb1cd9b5a24ca2c2f9`。同源码 Linux ext4 有界门禁和确定性打包通过，Linux 包 SHA-256 为 `3297810920b54496e9629af522d132381fabf1f5867b7c4684b7fe86aa0c400b`。实体触控及原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-27：搜索期间保留项目分组与新任务入口

旧版搜索只筛选项目内的会话，项目分组标题及其新任务入口仍在；模块版此前搜索无命中时隐藏所有分组，导致只能通过全局新任务入口切换项目。现在“进行中”筛选保留默认项目和所有已配置项目的分组，命中计数按搜索结果显示；无命中的分组显示“没有匹配的会话”，避免把已有会话误称为“暂无会话”。添加项目入口继续常驻结果上方。

单文件 Home `.build/mdo-packed-docks-ijbzvv5r` 创建第二个 `Group QA` 项目后输入无命中搜索词：默认项目与 `Group QA` 标题、各自的新任务入口和准确空状态均可见。桌面点击默认项目标题，直接进入 `#/projects/default/new` 并聚焦输入。320×350 手机侧栏在同样无命中时保留两个 40px 高的项目标题与 40px 添加入口，页面宽 320px；点击 `Group QA` 标题后进入该项目新任务、侧栏收起、搜索清空且输入获焦。浏览器脚本错误为空。Windows 有界门禁通过 114 项 Python、68 项 Node、71 个前端模块解析、21 个运行探针、确定性打包、零旁路写入与 20 秒启动检查；根目录 `mdo.exe` SHA-256 为 `512382e3427aa82ca03135bacb831f8488c87adf5ceaf94c8cc8ed5758a14112`。同源码 Linux ext4 有界门禁和确定性打包通过，Linux 包 SHA-256 为 `e196885a4f070ee07a4faa830128721fef7420c55bf8d00e328a3c2f14dc6402`。实体触控及原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-27：搜索期间保留侧栏添加项目入口

旧版侧栏始终将行内“添加项目”放在会话搜索结果之前；模块版此前在输入搜索词后隐藏整个项目入口，零命中时只能看到空状态。现在“进行中”列表在有无搜索词时都显示添加入口；零命中提示排在入口及已展开的表单之后。早返回的空状态也会恢复行内表单的输入焦点与选区。

单文件 Home `.build/mdo-packed-docks-82uo1gz5` 中，桌面输入无命中搜索词后仍能展开表单、提交 `Search QA` 工作区并直接进入 `#/projects/search-qa/new`；输入框获焦，搜索词在切页时正常清空。320×350 手机侧栏再次输入无命中搜索词，添加按钮为 40×40px，行内输入和两个操作按钮均高 40px；Esc 关闭表单后焦点返回添加入口，搜索词保持。页面宽度仍为 320px，浏览器脚本错误为空。Windows 有界门禁通过 114 项 Python、68 项 Node、71 个前端模块解析、21 个运行探针、确定性打包、零旁路写入与 20 秒启动检查；根目录 `mdo.exe` SHA-256 为 `f2538943fabe2ba03af52a36fc047386818abad940f0b7197e555089696a5233`。同源码 Linux ext4 有界门禁和确定性打包通过，Linux 包 SHA-256 为 `cc75d2947f1e1a26c58c846bc687155367076d66ef25c6767592540a50142df9`。实体触控及原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-27：行内项目创建的迟到失败响应

隔离打包夹具增加 `--fail-first-project`，可以让首次项目 POST 在有界延迟后返回 422。单文件 Home `.build/mdo-packed-docks-1jd7tdoi` 中提交 `Rejected QA` 工作区目录，POST 等待时立即点击“新建任务”；路由和焦点切到默认项目新任务。5 秒后失败响应返回，行内表单没有重新出现，添加入口恢复可用，默认项目路由和输入焦点保持，未出现新项目；刷新后失败项目仍不存在。代理记录项目 POST 总计 1 次，浏览器脚本错误为空。此项与上一阶段的迟到成功响应一起覆盖导航离开后的两种完成结果；实体触控与原生 WebView 仍待验收。未运行压力或高负载测试。

## 2026-09-27：行内项目创建的迟到响应

隔离打包夹具新增 `--project-delay-ms`，只对项目创建 POST 施加最多 5 秒的有界延迟。单文件 Home `.build/mdo-packed-docks-tq8znqpj` 在默认项目已有会话的侧栏提交 `Delayed QA` 目录，项目 POST 尚未返回时立即点击“新建任务”，路由变为 `#/projects/default/new`、输入框获焦，行内表单关闭且添加入口在请求期间禁用。迟到响应返回后项目列表出现 `Delayed QA`，当前路由、默认项目选择和输入焦点没有被改写；刷新后项目仍在、默认项目仍选中。代理记录项目 POST 总计 1 次，浏览器脚本错误日志为空。此验证覆盖成功但迟到的响应；失败且迟到的响应见上节。根目录 `mdo.exe` 已从同一应用源码重建，字节与上一阶段一致；未运行压力或高负载测试。

## 2026-09-27：侧栏行内快捷添加项目

旧版侧栏“项目”旁的“+”直接展开工作区目录输入行；模块版此前改成完整项目弹窗。现在侧栏重新提供快捷行内创建：输入目录后按 Enter 或点“添加项目”，名称和标识沿用完整表单的同一套生成规则，创建成功直接进入新项目任务；Esc 或“取消”关闭并把焦点还给“+”。完整表单继续留在项目设置页，用于自定义名称、标识和默认模型。创建失败时保留路径、焦点和本地化错误；导航离开后才完成的创建不会抢回用户当前页面。手机端将路径输入与操作按钮分成两行，均为 40px 触控高度。

初轮单文件 Home `.build/mdo-packed-docks-ay9eob1q` 中，桌面侧栏输入带空格的 Windows 工作区目录，回车创建 `Quick QA`，页面进入 `#/projects/quick-qa/new`，项目选择器、工作区与输入焦点均正确；Esc 关闭行内输入后焦点返回添加入口。最终单文件 Home `.build/mdo-packed-docks-pvb1g4af` 在 280×250 手机侧栏中，路径输入框宽 172px、输入及两个操作按钮高 40px，展开时侧栏自动滚动让表单进入视口；回车创建 `Mobile QA` 后侧栏关闭、项目选择器正确、输入框获焦。再次输入同一目录时显示中文“项目标识已存在”说明，保留原路径和输入焦点，取消后焦点回到“+”；页面宽 280px，浏览器脚本错误日志为空。新增 Node 测试覆盖 Windows/POSIX 路径生成相同项目标识。Windows 有界门禁通过 114 项 Python、68 项 Node、71 个前端模块解析、21 个运行探针、确定性打包、零旁路写入与 20 秒启动检查；根目录 `mdo.exe` SHA-256 为 `6409e01128165cd23e50d85824571a73b64e14c94e0396588160e0602701340c`。同源码 Linux ext4 有界门禁与确定性打包通过，Linux 包 SHA-256 为 `c837aa0643ab018fd176d6b3a49648a08c479fb3de2eaab7aa3757f56cde99ae`。导航期间创建响应延迟的防抢焦逻辑已实现，尚未在打包页注入该时序；实体触控与原生 WebView 仍待验收。未运行压力或高负载测试。

## 2026-09-27：侧栏点击项目标题直接新建任务

旧版侧栏点击项目分组标题就会在该项目新建任务；模块版此前只允许点击标题旁的“+”。现在项目名称与会话数同处一个可聚焦的按钮，点按或键盘激活均走原有项目新任务路径；旁边的“+”与项目管理按钮继续独立操作。标题的无障碍名称随项目和语言变化，默认项目使用专门文案避免“默认项目 项目”的重复表达。手机标题点按区高 40px。

最终单文件 Home `.build/mdo-packed-docks-q_ma3eb1` 中，桌面侧栏先创建 `Project Alpha`，从默认项目已有会话输入未发送草稿，再点击 `Project Alpha` 分组标题；页面进入 `#/projects/alpha/new`，输入框获焦，项目选择器显示 `Project Alpha`。320×350 手机侧栏点击“默认项目”标题后抽屉关闭、输入框获焦，选择器回到默认项目；按钮实测 166×40px，页面宽保持 320px。返回原会话并刷新后草稿仍在，浏览器脚本错误日志为空。此前初版 Home `.build/mdo-packed-docks-3yjw0ff9` 也验证点击默认项目标题、返回原会话与草稿保留。Windows 有界门禁通过 114 项 Python、67 项 Node、70 个前端模块解析、21 个运行探针、确定性打包、零旁路写入及 20 秒启动检查；根目录 `mdo.exe` SHA-256 为 `e0e7858b23d8b089474c6591618f337a61277e2062fe5bd305138e4189420156`。Linux ext4 同源码有界门禁与确定性打包通过，包 SHA-256 为 `ba33dce4c709738bd89cedbdd104b87fe1c7aa26917b23f2d0b4ee0df0f5453b`。实体触控、原生 WebView 和真实软键盘仍待验收；未运行压力或高负载测试。

## 2026-09-27：手机顶栏恢复快捷切换项目

旧版在 320px 手机顶栏仍显示项目入口；模块版此前只能从侧栏或新任务输入区改项目。现在手机顶栏以会话标题和当前项目组成两行 40px 入口，点按列出项目与“管理项目”。已有会话切到另一项目时进入该项目的新任务，原会话草稿留在原项目；新任务页也显示并允许更改当前项目。已归档等会话状态仍接在项目名后显示，模型选择保留在输入区。桌面顶栏与手机顶栏共用项目菜单逻辑，菜单有键盘导航及 Esc 焦点恢复；极窄屏限制菜单尺寸，项目长名截断而不撑宽页面。

初次打包页 Home `.build/mdo-packed-docks-8htdgf49` 在 320×350 和 280×250 检查了顶栏、输入区、双项目菜单和归档状态：从默认项目已有会话输入未发送草稿，点按手机顶栏切到 `Mobile QA` 新任务，输入框获焦，项目选择器正确；返回原会话及刷新后草稿仍在。“管理项目”可进入设置再返回；归档时状态可见，恢复后输入重新启用。最终源码重打包的 Home `.build/mdo-packed-docks-yuvoidd_` 在 280×250 再次创建 `Mobile QA`，从默认会话切到其新任务；菜单位于 x=48–238、y=47.5–179.5，页面宽 280px，输入框获焦、项目选择器显示 `Mobile QA`，脚本错误日志为空。Windows 有界门禁通过 114 项 Python、67 项 Node、70 个前端模块解析、21 个运行探针、确定性打包、零旁路写入和 20 秒启动检查；根目录 `mdo.exe` SHA-256 为 `2bc5d230433b0fa20148b4c9ddb6e16dae9de64dea85cb4f3c35ccdfb23e592f`。同一源码在 Linux ext4 上的有界门禁与确定性打包通过，Linux 包 SHA-256 为 `8ba1838443e0f552e720ff3ff5112d424780fdbd7b632ebbc618d8c50ff4e4b8`。实体手机触控和原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-27：会话顶栏快捷切换项目

旧版会话顶栏的项目面包屑可直接切换项目；模块版此前只在侧栏或新任务输入区提供项目选择，阅读已有会话时要绕到侧栏。现在桌面会话标题左侧显示当前项目入口，列出可用项目和“管理项目”；选择另一个项目进入该项目的新任务，原会话及未发送草稿保持在原项目。新任务页继续使用输入区原有项目选择，手机顶栏维持紧凑布局、通过侧栏选择项目。菜单支持方向键、Home/End、Esc 焦点返回，并忽略输入法候选键；窄桌面窗口限制菜单高度并让键盘目标滚入可见区域。中英俄名称和无障碍标签同步。

隔离单文件 Home `.build/mdo-packed-docks-8de2s1c_` 从默认项目已有会话创建隔离的 `qa-project`，在原会话写入未发送草稿后用顶栏菜单 End、上键、Enter 进入 QA Project 新任务，输入框获焦，项目选择器指向 QA Project；返回原会话及刷新页面后草稿仍为原文。“管理项目”打开项目设置，返回后聚焦原输入框。761×300 下入口和菜单均在视口内（菜单 x=284–474、y=38–150），Esc 返回入口；320×350 手机页文档宽为 320px、原侧栏与输入控件未被顶栏改动挤压，脚本错误日志为空。Windows 有界发布门禁通过 114 项 Python、67 项 Node 测试、70 个前端模块解析、21 个运行探针、确定性打包、零旁路写入和 20 秒启动检查；根目录 `mdo.exe` 与打包产物 SHA-256 均为 `4505d1d5e68a23b955bbe3b1628fc4537e5c9ecd7694b4682c02dda7f57f6d47`。实体移动端和原生 WebView 仍待验收；未做压力或高负载测试。

随后发现关闭状态直接按上键时菜单尚未渲染，初版无法定位末项；最终单文件 Home `.build/mdo-packed-docks-d_reut32` 再次从入口先按 Esc 关闭、再按上键，焦点直达末项“管理项目”。修正后重新执行上述 Windows 门禁，脚本错误为空。

同一最终源码在 Linux ext4 上使用已锁定宿主运行有界门禁：114 项 Python、67 项 Node、70 个前端模块解析、21 个运行探针和确定性单文件包均通过，Linux 包 SHA-256 为 `f849493ac233319bf113c9eb5ce76bed907f6a4f3bb5590b9cce05e2fc07a0f9`。Linux 原生 WebView 与实体手机尚未由浏览器视口和门禁证明。

## 2026-09-27：最新交互改动的 Linux 有界回归

将当前提交的源码复制到 WSL 的 ext4 文件系统，从锁定的 xserver `ca2c8c2` 重新构建 Linux 宿主和单文件包，运行 `python3 tools/qa_release.py --xserver-root /home/ubuntu/.cache/mdo-linux-qa-73d848b/xserver --skip-gui-smoke`。114 项 Python 检查、67 项 Node 测试、69 个前端模块解析、21 个运行探针与两次确定性打包均通过；Linux 包 SHA-256 为 `2c2dd66b1c3347430e76b95e6d8e1a0667e19bef4ff2207c9db7b9807ac6da46`。此前第一次复制时把 `--exclude=mdo` 错用成任意目录名排除，漏掉 `app/include/mdo`；修正为仅排除根目录可执行文件后重跑，以上通过结果来自完整源码。未运行压力或高负载测试。

再由 Linux 单文件包启动隔离 Home `.build/mdo-packed-docks-a4w0uvoe`，从 Windows 浏览器访问其 WSL 服务。在 280×250 视口打开顶栏会话菜单，End 聚焦“移到回收站”，菜单 `scrollTop=192`、末项完整可见，Esc 关闭并将焦点还给入口；文档宽为 280px，脚本错误日志为空。这个验证覆盖 Linux 打包服务与前端交互，仍不等于原生 Linux WebView 或实体移动设备。运行中异常退出若留下无 `run_id` 的 `starting` 凭据，目前必须保持核对屏障；凭据本身无法证明模型是否执行，不得凭重启自动重发。后续自动判定需要把稳定提交 ID 与持久运行记录建立可恢复的原子关联，再分别验证启动前失败、启动后退出和响应丢失。

## 2026-09-27：极短屏会话菜单首尾项键盘可见性

旧单文件 Home `.build/mdo-packed-docks-bgxx3p4v` 在 280×250 视口点击顶栏“更多会话操作”后，焦点留在入口，按 End 不进入末项；菜单内部的键盘焦点移动又阻止浏览器滚动，短屏下末项可能处于可滚动区域外。现在入口展开后可用 Home/End 与上下键直接进入首尾项；菜单内部移动焦点时显式把目标项滚进菜单可见区域，输入法候选按键不触发导航。

生产模块夹具 `tests/fixtures/session-menu-viewport-browser.html` 用真实菜单样式验证 280×250 与 1280×720 下的 End、从末项向下循环、Home、可见性、Esc 焦点恢复及原有侧栏动作。最终单文件 Home `.build/mdo-packed-docks-q96xfn8_` 在 280×250 下按 End 后聚焦“移到回收站”，菜单 `scrollTop=192`、末项完整可见；下键回首项，Home/End 与 Esc 均正常，文档宽保持 280px、浏览器脚本错误为空。1280×720 桌面菜单的 End 也聚焦末项，无横向溢出。Windows 有界发布门禁通过 114 项 Python 检查、69 个前端模块解析、全部 Node 测试、21 个运行探针、确定性打包、零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` 与打包产物 SHA-256 均为 `6e836817e2d8513f889a4b3b5f9f04c02eda0496843eb150c4edf61ac23444bd`。实体触控和原生移动 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-27：刷新后恢复待发图片清理

运行中移除携图待发项时，之前的延后清理任务只在当前页面内存中；刷新页面或关闭再打开后，附件会留在便携 Home。现在队列删除操作将尚未派发图片的清理 ID 与队列项移除一同原子写入 `queue.json`（schema 6）。页面重新读取队列时恢复清理任务，附件删除成功或文件已经不存在时，服务端再从队列文件中确认并移除清理 ID。若在删除文件与确认记录之间退出，下次请求可继续完成；已绑定运行的队列项不进入清理记录，附件删除仍由服务端核对草稿、队列、运行和历史引用。记录有每会话 256 项上限，达到上限时拒绝新的带图队列移除，不会静默丢失待清理信息。

API 运行探针覆盖 schema 2/3 迁移、持久清理记录、删除确认及“文件已删而记录未清”的恢复。单文件 Home `.build/mdo-packed-docks-91rs88ni` 在运行中排入测试 PNG、立即移除并刷新；刷新时首条任务仍运行，任务完成后只有首条回复，取消的消息未进入历史。最终 `queue.json` 的 `items` 与 `discard_images` 均为空、草稿无附件，附件目录只保留另一条已发送历史消息的图片，浏览器脚本错误为空。Windows 有界发布门禁通过 114 项 Python 检查、69 个前端模块解析、全部 Node 测试、21 个运行探针、确定性打包、零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` 与打包产物 SHA-256 均为 `9b0217034fbb0ce1b624155cd705439bde5c9f0aea9c6c777353cb36d67a7fc0`。未运行压力或高负载测试。

关闭页面后若一直不再打开该会话，清理记录会留在便携 Home，附件仍依赖后续上传时的配额回收；实体移动端与原生 WebView 仍待验收。

## 2026-09-27：携图待发消息自动派发的清理边界

补查上一阶段的图片清理改动时发现，队列自动派发后的移除入口仍引用已删除的旧清理函数。该入口只在运行成功启动之后调用，此时图片已转交运行并成为历史消息附件，不应作为废弃图片清理。现由自动派发入口仅移除队列项；用户主动取消待发项才进入延后清理流程。

根目录 `mdo.exe` 重新打包后，单文件 Home `.build/mdo-packed-docks-askzugh3` 在 320×350 视口先启动 15 秒有界回复，再将测试 PNG 加入第二条待发消息。第二条自动派发并正常回复；刷新后两轮消息、第二轮用户图片均保留，`queue.json` 为空，图片 `.bin` 与 `.json` 正确保留供历史读取。页面宽度保持 320px，浏览器脚本错误为空。最终 Windows 有界发布门禁再次通过 114 项 Python 检查、69 个前端模块解析、全部 Node 测试、21 个运行探针、确定性打包、零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` 与打包产物 SHA-256 均为 `5a96252ba47861bc0f181d90fdeb5f11a9ba999891e9a6d39ace5a7c0f1ad2af`。未运行压力或高负载测试。

## 2026-09-27：运行中移除待发图片后清理附件

旧单文件 Home `.build/mdo-packed-docks-0pv3qbu6` 在 320×350 页面运行慢回复时，附图提交第二条待发消息并立即移除：`queue.json` 与 `draft.json` 均清空，刷新后也没有第二条消息，但该图的 `.bin` 和 `.json` 仍留在会话 `attachments` 目录。附件删除 API 在该会话运行时返回 `attachment_in_use`，因为服务端会保护运行中的会话；旧前端只尝试删除一次并忽略失败。

现在待发项移除后先记住待清理的图片 ID，等该会话的运行状态结束，再调用附件删除 API；服务端继续核查草稿、队列和历史引用。针对运行中延后、空闲后只删除一次、暂时占用后重试添加了 Node 回归测试。最终单文件 Home `.build/mdo-packed-docks-b73guryh` 在同样的有界慢回复中附图、排队并移除；回复正常完成，`queue.json` 为空，`draft.json` 没有附件和待发提交，刷新后没有被移除的消息，附件目录也没有残留文件。320px 页面无横向溢出或脚本错误。Windows 有界发布门禁通过 114 项 Python 检查、69 个前端模块解析、全部 Node 测试、21 个运行探针、确定性打包、零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256 为 `09d1a0aa14854bc322a3c0f55142213631039d93fe9de6ee35b4d0eb3d37ef7d`。

待清理记录目前只存在页面内存中；若在运行结束前关闭或刷新页面，自动重试会丢失，孤立附件仍依赖服务端后续配额清理。实体移动端触控与原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-27：搜索与菜单键盘改动的有界发布回归

会话内搜索、Token 用量弹层以及顶栏/侧栏会话菜单的 Esc 事件改动提交后，运行 Windows 有界发布门禁 `python tools/qa_release.py --xserver-root D:\GIT\xserver-mdo-refactor --skip-host-build`。114 项 Python 检查、65 项前端 Node 测试、68 个前端模块解析、21 个运行探针、严格 C11 编译、两次确定性打包、单文件零旁路写入和 20 秒打包启动检查均通过。根目录 `mdo.exe` 与确定性打包产物 SHA-256 均为 `16d124ade644f9a41e3637670291c73ed7123e6bf458141e2bfa42cc010ccf8c`。此门禁证明这些代码路径未破坏已有有界检查；真实输入法及实体移动端操作仍按审计表继续验收，未运行压力或高负载测试。

## 2026-09-27：会话内搜索忽略输入法候选 Esc

生产模块夹具 `tests/fixtures/conversation-search-ime-browser.html` 先复现搜索框在中文组字或 `keyCode=229` 候选键下被提前关闭，两个检查均失败。搜索框现跟踪组字状态，并在关闭入口使用统一的 `isImeKey` 判断。修复后夹具验证候选键保持搜索与查询、首次普通 Esc 关闭搜索并将焦点还给输入框、再次 Esc 才由全局快捷键停止运行，四段均通过且无脚本错误。

最终单文件 Home `.build/mdo-packed-docks-pu7t36_y` 使用 12 秒有界本地回复发送 `SLOW UI search QA`。运行中按 Ctrl+F、输入 `SLOW`、按 Esc，搜索收起而停止按钮与“运行中”保持；随后正常生成 `UI fixture completed.`，显示 7 输入 / 3 输出 tokens。320×350 下再次打开搜索得到 1 处命中，Esc 后焦点回输入框；刷新仍有一轮用户/Agent 消息，持久 `snapshot.json` 包含两条内容，文档宽 320px，浏览器脚本错误为空。4 项相关 Node 测试、68 个前端模块解析、18 项 Python 前端契约测试及单文件重建通过；根目录 `mdo.exe` SHA-256 为 `16d124ade644f9a41e3637670291c73ed7123e6bf458141e2bfa42cc010ccf8c`。合成输入法事件不代替实体设备输入法验收；未做压力或高负载测试。

## 2026-09-27：会话操作菜单忽略输入法候选 Esc

顶栏会话菜单和侧栏会话菜单原先在收到输入法候选阶段的 Esc 时也会关闭；侧栏的方向键已经排除这类按键，关闭入口却没有同样判断。两个菜单现统一使用 `isImeKey`，同时兼容 `isComposing` 与部分 WebView 在候选提交边界报告的 `keyCode=229`。生产模块浏览器夹具 `tests/fixtures/session-menu-viewport-browser.html` 扩展为同时验证两处菜单：候选键保持打开，普通 Esc 关闭且焦点回入口；原有菜单视口定位、最后一项可点与动作执行在 1280×720 和 320×350 均通过，浏览器脚本错误为空。4 项键盘 Node 测试、68 个前端模块解析、18 项 Python 前端契约测试和根目录单文件重建通过；`mdo.exe` SHA-256 为 `ef2d6358c9863b8ad7c61d21f7925f369721d719ec80a27fad7e552de0385011`。这一轮是生产模块浏览器夹具验证，打包页的输入法行为与实体设备键盘仍待验收；未运行压力或高负载测试。

## 2026-09-27：Token 弹层与全局 Esc 的交互回归测试

新增 `tests/test_token_meter_escape.mjs`，在同一文档事件目标上挂载生产版 Token 用量弹层和全局快捷键。测试覆盖输入法候选键不关闭弹层或停止运行、首次 Esc 仅关闭弹层并返回入口焦点、再次 Esc 才停止运行；避免两个独立的键盘测试漏掉事件监听顺序。4 项相关 Node 测试、68 个前端模块解析及 18 项 Python 前端契约测试通过。重新生成根目录单文件 `mdo.exe`，SHA-256 为 `3b1c5d858cf62cdaaa6df35060dea4d12e67980843e2dc1915af0bf91da9ef4f`，与上阶段应用字节一致。本阶段仅新增测试，打包页的 15 秒有界运行证据沿用上一节；没有运行压力或高负载测试。

## 2026-09-27：Token 用量弹层 Esc 不再停止运行

旧单文件 Home `.build/mdo-packed-docks-9jfk1twa` 使用 15 秒有界本地慢回复，发送 `SLOW UI baseline` 后立即打开 Token 用量并按 Esc。弹层关闭的同时全局 Esc 停止快捷键也收到同一事件，时间线出现“Agent 已停止”，输入区进入恢复决策。这是弹层文档级键盘监听没有消费 Esc 引起的双重动作。

现在弹层只在未被其他控件消费、非输入法组字且没有打开模态对话框时处理 Esc；处理时阻止默认行为和后续同级键盘监听，再关闭弹层。销毁弹层时同时解绑点击、键盘及输入监听，避免重复挂载留下旧事件处理器。最终单文件 Home `.build/mdo-packed-docks-kwne1ex5` 重复 15 秒慢回复：按 Esc 后用量入口收起并获焦，任务仍显示“运行中”和停止按钮；随后正常产生 `UI fixture completed.`，显示 7 输入 / 3 输出 tokens，没有恢复决策。浏览器脚本错误为空，1280px 文档宽等于视口。68 个前端模块解析、4 项相关 Node 测试、18 项 Python 前端契约测试与单文件重建通过；根目录 `mdo.exe` SHA-256 为 `3b1c5d858cf62cdaaa6df35060dea4d12e67980843e2dc1915af0bf91da9ef4f`。未进行压力或高负载测试；实体设备键盘仍待验收。

## 2026-09-27：双页面归档与恢复同步

此前当前会话跨客户端状态同步的打包证据使用外部 API 客户端修改项目数据，没有让另一张真实前端页面发起归档。现在从同一单文件 Home `.build/mdo-packed-docks-wande1mh` 打开两张 320×350 页面；B 输入 `Keep draft through archive` 并保留未发送，A 从会话顶栏菜单归档。B 随即显示“已归档”，输入、模型、图片及发送禁用，侧栏“进行中”计数由 1 变 0；A 从顶栏移回进行中后，B 计数回 1、原草稿解锁，刷新 B 仍保持草稿和可发送状态。两页浏览器脚本错误为空，文档宽度 320px。整个过程未启动模型运行，也未发送草稿。两页的 `document.visibilityState` 均为 `visible`；系统后台标签唤醒、实体移动 WebView 和多进程共享 Home 不在此项证据范围。

本阶段没有修改应用代码。再次重建根目录单文件，SHA-256 为 `2cbe1c6dfdec5c3df21351b7fe13ebe6fe9e68cd00efbdd4897547a41af943f1`，与上一阶段字节一致；未进行压力或高负载测试。

## 2026-09-27：资源设置页显示未应用修改

旧单文件 Home `.build/mdo-packed-docks-vcqn58e5` 的 320×350 页面中，从会话草稿进入 Agent 设置，修改自定义系统指令，再切到“扩展与 MCP”，底部预览/应用操作栏被隐藏，顶栏也没有未应用提示。修改留在表单内，返回 Agent 分区才重新可见；这让跨分区浏览时难以判断配置是否已经保存。

现在只有当设置表单存在未应用修改、且当前处于隐藏操作栏的资源分区时，顶栏显示“未应用更改”。点击返回最近修改的常规、Agent 或联网分区，并把焦点交给对应分类按钮。最终单文件 Home `.build/mdo-packed-docks-2dwq3u5k` 在 320×350 验证暂存修改、跳转资源页、返回并放弃，入口随之消失；再次修改并预览后入口仍在，返回应用后配置从 revision 1 升至 2、入口消失，原会话未发送草稿保持。在 280×250 的中、英、俄界面，入口均高 40px、位于视口内，放弃语言预览后恢复中文；文档宽等于视口，浏览器脚本错误为空。68 个前端模块解析、三语词典 Node 测试、18 项 Python 前端契约测试及单文件重建通过；根目录 `mdo.exe` SHA-256 为 `2cbe1c6dfdec5c3df21351b7fe13ebe6fe9e68cd00efbdd4897547a41af943f1`。实体触控与原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-27：会话搜索结果的键盘选择与移动焦点

旧单文件 Home `.build/mdo-packed-docks-ard528o8` 的 320×350 页面中，搜索框筛出会话后按方向下键，焦点仍停在搜索框，必须再用 Tab 穿过其他控件才能打开结果。现在方向下键直接进入首条会话，列表内上下键移动，从首条按上键回到搜索框；无命中时方向下键不夺走焦点。输入法组字及组合键不触发该导航。

最终单文件 Home `.build/mdo-packed-docks-ep5mhxz8` 中，创建第二条会话后以 `QA` 筛出两条结果，320×350 页面验证搜索框 → 首条 → 次条 → 首条 → 搜索框的键盘焦点链。方向键选中旧会话并按 Enter 后侧栏关闭、会话载入且输入框获焦；点按另一条结果后焦点保持在侧栏打开按钮，方便阅读。1280×720 桌面页的 Enter 选择仍保留列表焦点，便于继续浏览。无命中搜索保持搜索框焦点，两种宽度文档宽均等于视口，浏览器脚本错误为空。68 个前端模块解析、10 项相关 Node 测试、18 项 Python 前端契约测试及单文件重建通过；根目录 `mdo.exe` SHA-256 为 `031236d0b499bf4196795b9313797b6dbe6c9c10fd51a1510917894a5f5b0efb`。实体手机软键盘与原生 WebView 仍待验收；未运行压力或高负载测试。

## 2026-09-27：斜杠会话搜索打开收起的侧栏

全局 `/` 会话搜索此前在独立的 document 键盘监听中直接聚焦 `#session-search`。单文件 Home `.build/mdo-packed-docks-mls87w_c` 的 320×350 页面中，手机侧栏处于 `closed` 且不可聚焦时按 `/`，按键被吞掉，侧栏没有打开也没有可见焦点。该监听还未按设置页和输入法组字范围限制。现在由统一快捷键处理器在工作区、非编辑控件、非组字状态识别 `/`；若侧栏已收起，先打开，再把焦点交给会话搜索框。

最终单文件 Home `.build/mdo-packed-docks-zlfsvfg0` 中，320×350 从会话页面按 `/` 后侧栏展开、搜索框获焦；输入 `Packed` 筛出当前会话，Esc 关闭侧栏并把焦点还给打开按钮。输入框中的 `/` 仍只打开斜杠命令候选；设置页标题获焦时按 `/` 不离开设置。在 1280×720 桌面宽度，先收起侧栏再按 `/` 也会展开并聚焦搜索。页面宽度分别保持 320px/1280px，浏览器脚本错误为空。3 项键盘快捷键 Node 测试、68 个前端模块解析、18 项 Python 前端契约测试和单文件重建通过；根目录 `mdo.exe` SHA-256 为 `645a41a7d9743726a75aad95b24063c4be8729eb9437178f387de17fbeae4262`。没有运行压力或高负载测试；实体手机软键盘与原生 WebView 仍需验收。

## 2026-09-27：不兼容图片模型下仍可点按命令

上一阶段已让图片草稿中的精确斜杠命令先于模型能力检查执行，但发送按钮本身仍按“当前模型不支持图片”保持禁用。旧单文件 Home `.build/mdo-packed-docks-8n9vj7qj` 在 320×350 视口附图并选纯文本模型后，输入 `/model` 虽显示命令候选，发送按钮仍不可点；手机用户只能改用候选或下拉框。现在仅在输入是精确命令时绕过图片不兼容这一项按钮禁用条件；服务不可用、草稿未载入、上传中等原有禁用条件继续生效。输入内容改变时只更新按钮可用性，不重绘整张图片草稿。

最终单文件 Home `.build/mdo-packed-docks-uj05disz` 中，同样附图并选纯文本模型，输入未知 `/unknown` 时发送仍禁用；改为 `/model` 后发送立即可点。点击后模型切回支持图片的 Ling 3.0 Tiny，命令文字清空，原图片仍在，没有产生对话回合；刷新后模型和图片保持。320px 页面无横向溢出或脚本错误。68 个前端模块解析、6 项相关 Node 测试、18 项 Python 前端契约测试与单文件重建通过；根目录 `mdo.exe` SHA-256 为 `f8ed6ea294a3505d9a22d89b880edb0ce50f67869a1bbd692d676675572fb354`。本阶段没有运行压力或高负载测试；实体手机触控和软键盘仍待验收。

## 2026-09-27：图片草稿下的斜杠命令发送

旧版 `app_bak/wwwroot` 的发送入口即使附有图片，也会先识别精确的斜杠命令。改造版此前只在没有附件时调用命令解析：旧单文件 Home `.build/mdo-packed-docks-boh5w3td` 中添加测试 PNG、输入 `/help` 并点“发送”后，时间线实际出现了携图的 `/help` 用户消息且启动任务。现在所有来自输入框的提交都先识别精确命令；执行命令只清除命令文字，图片留在草稿，避免误发或丢失未发送附件。

最终单文件 Home `.build/mdo-packed-docks-6povs0cs` 中，同一步骤在桌面和 320×350 视口均打开帮助弹层，没有新增对话回合；关闭后图片仍在，刷新后 `draft.json` 仍保留一条附件且没有运行事件。随后输入 `/model` 并点“发送”，模型切换至纯文本夹具，图片仍在且界面提示当前模型不支持图片，发送按钮正确禁用；页面宽度保持 320px，浏览器脚本错误为空。Windows 有界发布门禁通过 114 项 Python 检查、68 个前端模块解析、全部 Node 交互测试、21 个运行探针、严格 C11 编译、确定性打包、单文件零旁路写入和 20 秒打包启动回归。根目录 `mdo.exe` SHA-256 为 `b240bf6503b9ffd7ffa84a73010e1cb3886e4511cedf7705b32dca12cb56baef`；未运行压力或高负载测试，实体手机触控仍需验收。

## 2026-09-27：移动联网设置字段的点按高度

旧单文件 Home `.build/mdo-packed-docks-akx_w939` 在 320×350 的“联网与搜索”分区中，六个 Web 预算字段与四个代理字段的输入框实测只有 36px 高；同页常规/Agent 设置及操作按钮已经达到 40px。移动断点现为 `.settings-grid input` 设置 40px 最小高度，保留桌面双列密度。最终单文件 Home `.build/mdo-packed-docks-mta5wy3n` 的 320×350 与 390×500 页面均确认上述十个字段高 40px，文档宽分别等于视口宽；在短屏将“搜索结果数”从 8 改为 9、完成预览并放弃后回到 8，配置 revision 未增加。1280×720 桌面页仍为 36px，浏览器脚本错误为空。18 项 Python 前端契约测试、单文件重建通过，根目录 `mdo.exe` SHA-256 为 `6e83dc470ec2c055dbfcf5167c684277376061797fb6fcc3744d634d724bcdd3`。实体触控与原生移动 WebView 尚待验收；未做压力或高负载测试。

同一最终包在 280×250 极短屏的联网页再次预览并放弃“搜索结果数”修改，值恢复为 8；切到模型分区并打开“新增模型”，表单输入、选择框和操作按钮均至少 40px 高。空表单点击“保存”后必填“标识”自动滚入可见区域并获焦；切换设置分区后内容滚动位置回到顶部。页面宽度保持 280px，浏览器脚本错误为空。此项仍是浏览器视口模拟，不代替实体触控与软键盘验收。

## 2026-09-27：历史改写前复核持久待发状态

“编辑后重新发送”和“重试”此前只检查输入框与已载入的待发送队列；队列 GET 尚在进行时，`peek` 会返回空，而且空输入框也无法说明持久草稿里没有 `posting`/`rejected` 提交意图或“运行结果待核对”标记。现在入口要求会话草稿已加载、没有待核对运行与提交意图；历史读取后再刷新持久草稿和待发送队列，重新检查运行、模型、输入及待发状态，全部通过才提交截断。异步复核期间若切换会话，仍在截断前拒绝。历史操作进行中本页的队列自动派发暂停。

单文件 Home `.build/mdo-packed-docks-endhltcz` 先完成 `GUARD UI` 回合；通过隔离测试 API 写入 `run_admission_uncertain=true` 并刷新后，点击旧回复“重试”仅显示核对提示，原两张消息卡保持且没有截断边界。随后将隔离草稿改成一条 `rejected` 的持久提交意图并刷新，重试提示先处理待发送内容，历史仍不变；清除测试意图后，同一按钮正常截断并启动一轮，固定回复出现。最终字节 Home `.build/mdo-packed-docks-nbur3ljh` 再次发送 `FINAL GUARD UI` 并重试，固定回复正常完成，浏览器脚本错误为空。新增两个异步前置检查测试覆盖待发项阻断与切换会话；63 项 Node 测试、18 项 Python 前端契约测试、68 个前端模块语法检查和单文件重建通过。根目录 `mdo.exe` SHA-256 为 `3d7bd125a75419e2e05a4a5a1f5bb850c785eeb5edbd7ed0bb53364e28e237e6`。其他客户端恰在复核后修改状态、或截断与运行启动之间改模型，仍需要服务端原子事务；未做压力或高负载测试。

## 2026-09-27：历史消息改写期间保护新草稿

旧单文件 Home `.build/mdo-packed-docks-pcv_zogz` 在 2 秒有界历史 GET 延迟中点击“重试”，模型和发送被锁住，但输入框及图片入口仍可编辑；此时用户可以写入新草稿，而启动失败处理会把原消息写回输入框，存在覆盖新草稿的窗口。现在改写期间同步禁用输入框、图片入口、模型和发送，显示已有的三语“请等待当前消息操作完成”提示；提交入口也拒绝来自快捷键或其他路径的发送。操作结束后恢复控件，失败时再按原有规则恢复被重试的消息。

最终单文件 Home `.build/mdo-packed-docks-tnktf_ih` 使用 5 秒有界历史读取延迟验证：点击“重试”后原回复保持可见，输入框及图片入口均禁用，尝试输入 `NEW DRAFT` 没有进入草稿；读取结束后新回合与固定回复完成，控件恢复可用，刷新后草稿仍空、回合可回放，浏览器脚本错误为空。61 项 Node 测试、18 项 Python 前端契约测试、68 个前端模块语法检查及单文件重建通过。根目录 `mdo.exe` SHA-256 为 `abf029397d98924e1e75fca700ab76f43a2f7e0aaa5b8077eb6a8ad7204bfe59`。服务端的截断与启动仍是两个请求；跨客户端恰在其间修改模型的竞态需独立设计原子操作。未做压力或高负载测试。

## 2026-09-27：代码块复制反馈自动恢复

旧版代码块复制成功后约 1.2 秒恢复“复制”按钮文字；新版此前将“已复制”永久留在按钮上，后续点击看不到新的反馈。现在每次复制结束后重置计时，在 1.2 秒后按当前语言恢复“复制代码”；重复点击会重新计时，已从时间线移除的按钮不会被更新。最终单文件 Home `.build/mdo-packed-docks-ihqf97b5` 发送 `MARKDOWN UI`，点击代码块按钮后观察到“已复制”再恢复“复制代码”，焦点保持在按钮，浏览器脚本错误为空。68 个前端模块语法检查和单文件重建通过；根目录 `mdo.exe` SHA-256 为 `8dab4f070d8daa5b18b5ba8050bbf0172db830311d8a7da113f22495376adf8`。未做压力或高负载测试。

## 2026-09-27：图片历史重试先验证模型能力

旧单文件 Home `.build/mdo-packed-docks-xnv1rq8n` 复现了破坏性顺序：先用支持图片的模型完成含 PNG 的一轮对话，再切到纯文本模型点击旧回复的“重试”；页面先显示“会话历史已截断”，之后运行被 `image_model_unsupported` 拒绝。原回合从时间线消失，只把输入和图片放回草稿。现在“编辑后重新发送”和“重试”均在操作前检查当前模型图片能力及配置更新状态，历史读取后、截断之前再验证一次；操作进行中暂时锁定模型选择和发送，避免读取历史期间本页切换模型。服务端的运行入口校验继续保留。

最终单文件 Home `.build/mdo-packed-docks-1f0miwza` 在同样步骤点击“重试”，直接显示不支持图片提示，原用户图片和 Agent 回复保持，输入为空；“编辑”入口同样不打开无效编辑弹窗，刷新后历史仍在。切回图片模型后同一回合成功重试，产生新的用户图片和回复。320×350 页面再次点击不兼容重试，历史边界未增加，文档宽 320px，浏览器脚本错误为空。另在隔离 Home `.build/mdo-packed-docks-zoaen6q3` 以 2 秒有界历史 GET 延迟复核：读取期间原回复可见、模型和发送禁用；读取结束后重试正常完成，控件恢复，无脚本错误。新增 Node 用例验证预检拒绝先于截断；61 项 Node 测试、68 个前端模块语法检查和单文件重建通过。根目录 `mdo.exe` SHA-256 为 `f26053ac64dd6ab329c3963e1b5f8a3f294d2b91bba685e92f6bf23fad3755a5`。其他窗口恰在预检后改模型的跨客户端竞态仍需独立验证；未做压力或高负载测试。

## 2026-09-27：草稿刷新不再误判本页图片编辑

连续添加图片时曾出现“草稿已在其他窗口修改”，界面有三张图但服务端草稿只保留首张。定位到 `draft-store` 的队列刷新路径：本页图片编辑尚在 300ms 保存等待期时，GET 返回的仍是当前已知 revision 的旧草稿；原逻辑仅因附件不同就把它当成另一个窗口的修改，暂停后续保存。现在同 revision 的响应保留本页未保存编辑，让原定时 PUT 继续；真正更高 revision 的其他窗口修改仍进入冲突保护，不覆盖对方数据。新增两项 Node 回归，一项先在旧代码复现失败，另一项确保真实竞争仍被拦截。

最终单文件 Home `.build/mdo-packed-docks-a7bs44aw` 连续上传两张本地测试 PNG，API 草稿 revision 2 含两张引用，刷新后缩略图仍为两张、草稿无错误提示，浏览器脚本错误为空。60 项 Node 测试、68 个前端模块语法检查和单文件重建通过；根目录 `mdo.exe` SHA-256 为 `7822a96834cbe1dd5d48209ce933740d430d03df80531f028386d71c0d2cb9bb`。真实浏览器中队列 GET 恰落入等待期的时序由确定性 Node 用例覆盖；单文件复测覆盖普通连续上传与持久化，不将其扩大为所有跨窗口竞态已验收。未做压力或高负载测试。

## 2026-09-27：极短屏图片草稿操作区

图片草稿提示在 280×250 的单文件页把模型选择器和发送按钮推到了视口下方：旧包 Home `.build/mdo-packed-docks-hs6de_i5` 中，附件区高 128px，模型和发送按钮下缘约 271px，用户看见“切换模型”的提示却无法直接选择。现在极短屏让提示和缩略图并排，附件区固定最多 82px 并可横向滚动；提示仍先于缩略图出现，移除按钮保持 40px 触控目标。三语提示文案缩短但保留切换模型或移除图片的两条操作路径。

最终单文件 Home `.build/mdo-packed-docks-gkidwmf0` 在 280×250 验证提示下缘 158px、移除按钮下缘 146px、模型和发送按钮下缘 232px，文档宽度等于 280px。四张图片时附件区仍为 82px，高度不随数量增加，内容宽 468px 可横向滚动；末张移除按钮可点击并把下一按钮保持在焦点上。320×350 和 390×500 保持常规多行附件布局。58 项 Node 测试、68 个模块语法检查和单文件重建通过；根目录 `mdo.exe` SHA-256 为 `f49a91f1530cca7eb53c6555a40b69fd2121c3a43f66ddb0caea616b534c4aa1`。连续快速添加图片时另观察到一次草稿保存冲突，后续定位与修复见上节；本项未做压力或高负载测试。

## 2026-09-27：图片草稿与模型切换

此前会话已有图片草稿时仍可切换到纯文本模型；页面保持可发送，直到服务端拒绝运行才知道模型不兼容。现在缩略图区直接提示切换模型或移除图片，发送按钮同步禁用；输入框 Enter 的提交入口也执行同一预检，避免绕过禁用按钮。图片和文字草稿保持原样，切回图片模型或移除图片后立即恢复发送。图片能力按当前路由的会话模型判断，避免切换会话时误用旧会话的模型。

生产模块浏览器夹具新增图片→文本→图片→文本并移除的回归，八个场景全过；图片移除与中英俄焦点夹具继续通过。最终单文件 Home `.build/mdo-packed-docks-shtsymj0` 使用隔离本地模型与测试 PNG：切到 `Ling Text QA` 后提示出现且发送禁用，Enter 只显示本地错误；刷新后图片、文字和禁用状态保持。切回支持图片的 `Ling 3.0 Tiny` 后提示消失，同一草稿成功发送并出现在时间线；再添图片、切到纯文本模型并移除后，发送重新可用。浏览器脚本错误为空。58 项 Node 测试、68 个前端模块语法检查及单文件重建通过；根目录 `mdo.exe` SHA-256 为 `249a0ac45d2c0da54aedd890f295bb619284c9f2860c5cd120748516b564ad2d`。真实多模态模型的推理质量及实体移动端触控不在此项验证范围；未做压力或高负载测试。

## 2026-09-27：剪贴板图片的双通道读取

旧版从 `clipboardData.items` 读取粘贴图片，新版只读 `clipboardData.files`。生产模块浏览器夹具在仅有 PNG item、`files` 为空时复现了静默忽略：粘贴事件未被消费，附件列表为空。现在先从 item 读取图片，必要时使用 item 的 MIME 补足文件未标注类型且无扩展名的情况；没有可用 item 才回退到 files，同时具备两种通道时只上传一次。服务端仍按文件签名校验图片。夹具七种情形全部通过，包含跨会话上传、新任务上传锁、空 MIME 文件、item-only、item MIME-only、files-only 和双通道去重；等待上传完成的断言也消除了原夹具偶发的时序误判。

中间单文件 Home `.build/mdo-packed-docks-ep_990_0` 在 320×350 用浏览器剪贴板实际粘贴合成 PNG，缩略图出现，刷新后仍在；预览关闭把焦点还给缩略图，移除后焦点到添加按钮，刷新不再出现。最终重建包 Home `.build/mdo-packed-docks-z05ndp41` 重复粘贴和刷新，附件仍为一张，文档宽度 320px，浏览器脚本错误为空。浏览器剪贴板原内容在测试后恢复。58 项 Node 交互测试和 68 个前端模块语法检查通过；根目录 `mdo.exe` SHA-256 为 `4db903d70966bdbaa9270051417344982e56226aed244a46a9bf90f2c5d3da2d`。item-only 由合成 PasteEvent 覆盖，不能替代各系统真实剪贴板或实体移动端验收；未做压力或高负载测试。

## 2026-09-27：待办与新决策的停靠顺序

旧版停靠位依次展示审批、询问、待办；新版把待办放在审批之前、询问之后。旧单文件 Home `.build/mdo-packed-docks-vxauq4eo` 在 280×250 先完成 `TODO UI`，再于 `APPROVAL UI` 等待期间聚焦展开的待办：审批到达后停靠位仍停在待办（y=52–121），审批标题在 y=184–203，完全不可见。现在按旧版顺序渲染审批、询问、待办及后台任务；新决策到达时，如果焦点在不编辑的待办/任务入口，转到决策标题。用户随后主动滚动时不再因标题聚焦而被 ResizeObserver 拉回顶部。

最终单文件 Home `.build/mdo-packed-docks-n9nsjuv7` 在相同 280×250、待办聚焦的时序中，新审批标题位于 y=65–84、焦点在标题；滚动后“允许一次”完整处于 y=64–104 的停靠位内，点击执行隔离无害命令，工具结果含 `exit_code: 0`，回复完成且焦点回到输入框。继续触发 `ASK UI`，询问标题同样位于 y=65–84、排在待办前；滚动并选择首项后收到第三轮回复。页面宽度保持 280px，浏览器脚本错误为空。生产模块夹具 `conversation-dock-scroll-browser.html` 在 320×350 和 390×500 验证待办焦点转移、审批优先顺序、手动滚动和展开参数后的按钮可达，均通过。58 项 Node 交互测试及 68 个前端模块语法检查通过；根目录 `mdo.exe` SHA-256 为 `120c78cc9a024b1b59879c9565b1cb50658746e56e8e9bd152860dd37dc1ea12`。实体触控和系统软键盘仍待验收，未进行压力或高负载测试。

## 2026-09-27：回复统计回到消息操作行

旧版把 token/s 统计紧接在复制、分叉、重试和反馈按钮之后；新版的自动左边距把统计推到整行最右，在 390×500 窄屏还使它独自靠右贴近输入区。现在统计使用 12px 可读字号和数字等宽排版，紧随按钮；空间不足时从消息左侧换行，并允许长数字换行。旧单文件 Home `.build/mdo-packed-docks-6cbafuez` 的手机统计起点 x=203、字号 10px；重建后的 Home `.build/mdo-packed-docks-mo5bc12k` 在 1280px 桌面上统计起点 x=582，距末个按钮 10px，在 390px 手机上另起一行、起点 x=19，五个消息操作仍各宽 40px。280px/390px 文档均无横向溢出；回复、7 输入 / 3 输出 tokens 与 token/s 正常显示，浏览器脚本错误为空。58 项 Node 交互测试和 68 个前端模块语法检查通过；根目录 `mdo.exe` SHA-256 为 `137a0cce239066a2984d838a82799b5275718f592a344101fb971ad09298163c`。实体触控和系统软键盘仍待验收，未进行压力或高负载测试。

## 2026-09-27：极短屏输入候选的打包页回归

使用当前根目录单文件 `mdo.exe` 和隔离 Home `.build/mdo-packed-docks-j529wjtb`，在 280×250 页面核对旧版输入候选手感。输入 `@a` 后，方向键切到 `src/alpha.c`，Tab 插入 `@src/alpha.c `，输入框继续获焦；输入 `@notes` 后，Tab 插入带引号的 `@"notes/QA notes.txt" `。输入 `/` 时九个命令留在输入框上方的可滚动菜单中，菜单范围 x=10–270、y=61–139；向上键从首项循环到 `/help` 并将末项滚入可视区（`scrollTop=302`）。输入 `/he` 后 Tab 打开帮助，Esc 关闭并返回输入框，命令草稿清空。文档宽度保持 280px，浏览器脚本错误为空。这是单文件浏览器页的键盘与布局回归；实体触控和系统软键盘仍待设备验收。本轮没有生产代码改动，也未进行压力或高负载测试。

## 2026-09-27：极短屏 token 用量的键盘阅读

280×250 单文件页的输入估算文字为保留对话空间而收起，用量弹层仍可从 40px 入口打开；本地草稿“请分析当前项目”显示约 7 tokens，实际 `MARKDOWN UI` 回复后显示服务端 7 输入、3 输出。弹层 y=60–184、可滚动内容 231px，鼠标可读全部明细，但原先焦点留在入口按钮，PageDown 滚动背后的对话而非弹层。现在给弹层可聚焦的命名区域，打开时将焦点移入，关闭时恢复入口焦点。

最终单文件 Home `.build/mdo-packed-docks-jg8sqzkk` 在 280×250 实测：打开后焦点为 `context-meter-panel`，PageDown 使面板 `scrollTop` 从 0 到 106、对话滚动位置不变；底部累计和说明可读。Esc 关闭后 `aria-expanded=false`、焦点返回用量按钮，文档宽度 280px，浏览器错误日志为空。58 项 Node 前端测试、18 项 Python 前端契约测试通过；根目录 `mdo.exe` SHA-256 为 `f29a98477acb0d1d0dc62fc38e83749a686da6d6fe5aa67df14ded756259836a`。实体手机触控与软键盘仍待验收，未进行压力或高负载测试。

## 2026-09-27：短屏会话搜索命中定位

280×250 单文件页中建立两轮消息后搜索 `SEARCH QA`，原界面报告“1 处”，但命中卡片位于 y≈−15–96，几乎完全压在 y=60–100 的搜索栏下。筛选时保留了原先靠底的滚动位置。现在仅在查询变化后，把第一个命中卡片的顶部对齐搜索栏下缘；后续普通时间线刷新不再强制重置用户的阅读位置，关闭搜索仍按旧版逻辑聚焦输入框。

最终单文件 Home `.build/mdo-packed-docks-1n3xe4q7` 用相同两轮消息复核：280×250 下搜索栏下缘和命中卡片顶部均为 y=100，命中角色与消息开头出现在剩余对话区域；320×350 下同样对齐 y=100。计数为 1，关闭后输入框获焦，280px/320px 文档均无横向溢出，浏览器错误日志为空。58 项 Node 前端测试及 18 项 Python 前端契约测试通过；根目录 `mdo.exe` SHA-256 为 `8ead5ca193cfca30d95cf2cb112ec3e31df5400376f5da06a13430e9de26d956`。实体手机触控、软键盘与原生 WebView 仍待验收，未进行压力或高负载测试。

## 2026-09-27：极短屏待发送队列的可见操作

280×250 的单文件会话在 `SLOW UI` 运行时排入第二条消息，原队列卡高 121px，但对话滚动区只有 89px；滚到最底时卡片从 y≈−1 开始，标题与“当前任务结束后自动发送”说明被手机顶栏遮住。现在仅在宽度不超过 760px、高度不超过 300px 时收紧卡片内边距、列表间距与行内边距，并将卡片限制在对话区高度内；长待发文本在列表内部滚动，标题保持可见，移除按钮保留 40px 点击高度。

最终单文件 Home `.build/mdo-packed-docks-41fd9_29` 的 280×250 视口中，短待发项卡片高 88px、位于 y≈52–140，标题、自动发送提示和移除按钮同时可见，正常等候后第二条自动运行。另一最终字节 Home `.build/mdo-packed-docks-mi771ejl` 用三行较长待发内容验证卡片高 89px、标题高 40px，列表可视高 41px、内容高 131px；实际点击“移除”后持久 `queue.json` 为空。首条有界慢回复完成，刷新仍只有这一轮用户消息与 Agent 回复，第二条未执行；浏览器错误日志为空。18 项前端契约测试通过，根目录 `mdo.exe` SHA-256 为 `482e924e36c21f968791dab0b4de9b32c0387e3b2067a13b3c5034fdd04809cc`。实体触控滚动与软键盘仍待验收，未进行压力或高负载测试。

## 2026-09-27：极短屏历史消息编辑弹窗

旧单文件包的 280×250 会话中点击用户消息“编辑”，弹窗内容实际高约 322px，可见高度只有 214px；“取消”和“保存并重新发送”落在视口外。现在让编辑弹窗与其他会话弹窗一样固定标题和操作栏、只在字段区滚动；极短屏压缩内边距与文本框高度，并让较长的撤回说明自身可滚动阅读。编辑入口同时传入弹窗，在取消、Esc 或提交时只要原按钮仍存在，就把焦点还给它，避免落到页面本体。

最终单文件 Home `.build/mdo-packed-docks-pmmq5xt0` 的 280×250 页面中，编辑弹窗 y=16–234、操作栏 y=182–235，文本框与两个按钮同屏可见；取消和 Esc 后焦点均返回原消息编辑按钮。实际把 `MARKDOWN UI` 改为 `MARKDOWN UI revised` 并提交，旧回合被截断，Agent 重新回复，刷新后新内容和回复仍在。320×350 下弹窗 y=20–330、操作栏完整位于视口内，文档宽度保持 320px，浏览器错误日志为空。58 项 Node 前端测试、18 项 Python 前端契约测试通过；根目录 `mdo.exe` SHA-256 为 `9f01b97e50f0b275dbfb76466b76f03444aac1a37ed734d35b08ab4ebbb3d9dd`。实体手机软键盘与触控仍待验收，未进行压力或高负载测试。

## 2026-09-27：390px 待办与审批共存的打包页验收

用当前单文件包和隔离 Home `.build/mdo-packed-docks-9u4tvlsb`，在 390×500 手机视口先发送 `TODO UI` 建立 1/2 待办，再发送 `APPROVAL UI`。审批出现时，待办和“exec 请求权限”同时处在输入框上方的对话停靠区；“拒绝”“允许一次”“本轮均允许”都是 40px 高，位于可滚动区域末端。实际点击“允许一次”后，无害本地命令返回 `exit_code: 0`，Agent 继续回复。刷新后 1/2 待办、两轮消息及展开的工具结果均可回放；持久 `todo.json` 保留两项，`ui-events.jsonl` 中工具结果为 `success: true`、`effect_applied: true`。页面文档宽度 390px，浏览器错误日志为空。重新打包后的根目录 `mdo.exe` SHA-256 仍为 `45d748d6f0d5bae4466fb7667e3bddb5609509ca19dc1ade830578962c85ab5b`，18 项前端契约测试通过。此轮只补充交互证据，未改生产代码，也未做压力或高负载测试；实体触控和软键盘仍待验收。

## 2026-09-27：极短屏会话阅读空间与输入配置

在 280×250 的单文件会话中发送 `MARKDOWN UI` 后，原布局让输入区占据 149px，对话滚动区只剩 35px；回复的复制、分叉、重试、反馈虽已生成，却难以看到和触达。现在仅对宽度不超过 760px 且高度不超过 300px、无待决操作的输入区使用 40px 文本框和单行工具栏；模型、推理强度、权限放在可横向滚动的选项带中，模型保留至少 90px 宽度，token 明细按钮与发送按钮保持可见。待决询问和审批仍沿用原布局。

最终单文件 Home `.build/mdo-packed-docks-4hvl7pab` 在 280×250 实际收到回复，时间线有复制、编辑、分叉、重试、点赞和点踩及 token/s；对话滚动区增至 89px，文档宽度仍为 280px。选项带可从 `scrollLeft=0` 滑至 120px，权限菜单能够展开，模型实际宽度 110px。中间包 `.build/mdo-packed-docks-hb6ro6el` 同尺寸实测 `ASK UI` 询问卡可见且选择“Fast”后完成；最终包在 320×350 恢复原双行工具栏，配置选项无内部横向溢出，对话区 135px。18 项前端契约测试通过；根目录 `mdo.exe` SHA-256 为 `45d748d6f0d5bae4466fb7667e3bddb5609509ca19dc1ade830578962c85ab5b`。触控滑动、实体软键盘与原生 Linux WebView 尚待设备验收；未进行压力或高负载测试。

## 2026-09-27：新任务入口的主次层级与窄屏目标

直接用旧版 `app_bak/wwwroot` 的离线 fixture 和当前单文件页对照：旧版侧栏“新会话”是 255×37px 的显眼主按钮；新版快捷“新建任务”虽有 213×40px 点击区，但背景透明，旁边“配置后创建”只有 36×34px。现在为快捷入口加上主题色背景、边框和明确字重，配置入口扩大到 40×40px；两者仍分别执行立即开始与先配置，不改变快捷键和会话创建流程。

中间单文件 Home `.build/mdo-packed-docks-u12yui38` 验证桌面 205×40px 主入口和 40×40px 配置入口；点击前者进入新任务并聚焦输入，点击后者打开原配置弹窗。320×350 手机侧栏两个入口完整可见、文档无横向溢出。继续缩到 280×250 并应用俄语后发现 `Ctrl N` 提示挤得“Новая задача”和快捷键都换行；最终在不超过 420px 的视口隐藏这条桌面提示。最终字节 Home `.build/mdo-packed-docks-daje1oov` 的 280×250 俄语侧栏中，主入口恢复单行 154×40px，配置入口 40×40px；配置弹窗提交按钮 y=191–231，完整位于 250px 视口内。点击主入口关闭侧栏并聚焦新任务输入框，浅色与深色主题均清晰，页面无横向溢出或脚本错误。18 项前端契约测试通过，根目录 `mdo.exe` SHA-256 为 `0ae46652c03ab0c923ae131e3c486683258869046d8f5e8ba950dab3cbe02fae`。实体设备触控仍待验收，未做压力或高负载测试。

## 2026-09-27：会话筛选的空列表反馈

上一轮从回收站恢复最后一条会话后，侧栏仍停在“回收站”筛选，却显示“还没有会话，创建一个任务开始使用。”；这会让已恢复的会话看似丢失。现按当前筛选显示“暂无归档会话”或“回收站为空”，搜索无结果仍优先显示“没有匹配的会话”，三种提示均接入中英俄词典。

隔离单文件 Home `.build/mdo-packed-docks-ajqwgvuc` 在 320×350 页面验证空归档、空回收站及无匹配搜索。实际将会话移到回收站后，再从侧栏恢复最后一条，列表立即显示“回收站为空”，所选会话重新可输入；文档宽度保持 320px，浏览器脚本错误为空。整理代码后重新打包，最终字节的 Home `.build/mdo-packed-docks-p6hfjiw0` 再次核对三种空态、320px 文档宽度和空脚本错误日志。58 项前端 Node 测试通过，根目录 `mdo.exe` SHA-256 为 `3ea1e069512a6a211abc80bc52481dc5c78800f1e776a52cd34d1b6ffb4c15c2`。实体设备触控与软键盘尚需独立验收，未进行压力或高负载测试。

## 2026-09-27：移动端回收站找回会话

用当前根目录单文件 `mdo.exe` 与隔离 Home `.build/mdo-packed-docks-vgu60s4y`，在 320×350 页面创建真实会话并发送 `SESSION RESTORE QA`。移到回收站后，原消息仍可读，输入区和发送按钮禁用；刷新后状态保持。从当前会话顶栏执行“恢复”，原消息、模型设置和输入能力均回来，磁盘 `meta.json` 为 `active`。

再从侧栏把同一会话移到回收站，离开它进入新任务页。将侧栏筛选从“进行中”切到“回收站”后可找到该会话；在列表菜单选择“恢复”，它立即从回收站列表消失并重新出现在“进行中”。选回会话后，原始双消息、token 用量、编辑与重试操作、模型和输入区保持正常；隔离 Home 的 `meta.json` 状态为 `active`、修订号 5。320px 页面无横向溢出，浏览器脚本错误为空。阶段末重新打包根目录 `mdo.exe`，SHA-256 仍为 `7cd371d49182c5878aca60f2fd64aca1583de9a0f52098643c8417360925224b`。本阶段只补充打包页操作证据，未改生产代码，也未进行压力或高负载测试。

## 2026-09-27：图片预览与快捷键帮助的窄屏操作

旧版可点击图片放大并随时关闭；新版预览已存在，但隔离浏览器夹具在 320px 测得关闭目标只有 32×32px，且覆盖小图片。现将关闭目标改为 40×40px、放在图片上方独立区域，并给弹层和关闭按钮接入三语名称。生产模块夹具使用无缓存 CSS 在 320px 测得关闭目标 40×40px、与图片不重叠、无横向溢出；点击或按 Esc 后焦点回到原图片。隔离单文件 Home `.build/mdo-packed-docks-dtiuqmc3` 实际上传合成 PNG，在 320px 打开预览，确认相同尺寸、焦点回返及英语应用后 “Image preview”/“Close image preview” 名称。

同一次检查发现快捷键帮助弹窗在英语下大部分仍显示中文，且 320×350 的确认按钮落在弹窗和视口下方。现将静态说明、关闭按钮和移动端遮罩接入三语词典；两条随运行中输入模式变化的说明继续由原状态逻辑翻译。弹窗固定头尾，让帮助正文内部滚动，极短屏隐藏顶部提示以保留操作空间。最终单文件 Home `.build/mdo-packed-docks-zortnv3f` 在 320×350 及 280×250 复核：中文和俄语确认按钮均高 40px、位于弹窗内；280×250 正文能滚动，实点按钮后关闭并把焦点还给帮助入口。英语帮助正文与移动侧栏遮罩名称正确，三语词典各 1131 键；浏览器脚本错误为空。实体手机软键盘和原生 Linux WebView 仍待验收。

Windows/Linux 有界门禁均通过 114 项 Python、58 项 Node、68 个 JS 模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入和启动检查通过。根目录 `mdo.exe` SHA-256 为 `7cd371d49182c5878aca60f2fd64aca1583de9a0f52098643c8417360925224b`，Linux 包为 `5a8a65ebdbd3fb189e0992679370a57f0b0475bd5451976e3217d9b43ee9399d`。未进行压力或高负载测试。

## 2026-09-27：运行期间阻止系统休眠

旧版常规设置的防休眠仅调用 Windows `SetThreadExecutionState`。新版将 `settings.power.prevent_sleep` 接入便携配置、设置 API 和常规设置页；默认关闭，仍沿用预览、ETag 应用及刷新回放。状态 API 返回平台检查结果、当前是否有交互或计划任务运行、抑制请求是否实际生效。若系统机制不可用，页面明确提示并阻止启用，避免出现已打开但无效的开关。三语词典各增加 5 键。

运行管理器只在交互任务启动/执行或计划任务执行期间持有请求，任务结束、关闭设置或退出时释放。Windows 在同一管理线程上设置和清除 `ES_SYSTEM_REQUIRED | ES_CONTINUOUS`；Linux 可选使用 logind `Inhibit` 返回的文件描述符，关闭描述符释放请求；macOS 使用 IOPM 的 `PreventUserIdleSystemSleep` assertion。三种机制都允许屏幕按系统策略熄灭。[Windows 系统休眠条件](https://learn.microsoft.com/en-us/windows/win32/power/system-sleep-criteria)、[logind 抑制接口](https://wiki.freedesktop.org/www/Software/systemd/logind/)、[Apple IOPM assertion](https://developer.apple.com/documentation/iokit/1557078-iopmassertioncreatewithdescripti) 为平台行为依据。

有界生命周期探针覆盖交互任务、计划任务、关闭开关及活动期间退出。Windows/Linux 发布门禁均通过 114 项 Python、58 项 Node、68 个 JS 模块解析、21 个运行探针和确定性打包；Windows 单文件零旁路写入和启动检查通过。根目录 `mdo.exe` 已重建，Windows SHA-256 为 `bbd04a4b63cff688492b4566fcdcd7c19a83f216515da81bfe63343ca069838f`，Linux 包为 `a7b71a4fcdba81e5f8a2b58ec3ae0f6f094cf7b0a5761a9d7cdb1379decd1c09`。隔离单文件 Home `.build/mdo-packed-docks-f78yru8c` 的常规页实测：预览、应用至 revision 2、刷新后保留；320px 视口开关可点击，页面宽度仍为 320px，草稿可放弃。Linux WSL 环境无可用 logind，验证了不可用反馈但尚未在实体 Linux logind 会话检验真实抑制；macOS 仍待原生构建与设备验收。未进行压力或高负载测试。

## 2026-09-27：常见操作失败的三语反馈

设置、项目、任务创建、计划、审批、询问和运行记录的若干稳定 API 错误此前直接显示服务端英文原文。现在统一在 `errorMessage` 的错误码表中处理：版本信息缺失提示刷新，项目/审批/询问已被其他窗口处理时提示核对，计划忙碌提示等待，项目清除预览不完整时明确不能沿用旧清单。未知错误仍保留服务端详情，避免用笼统译文掩盖未覆盖的故障。原先七个单独分支也并入同一表，方便继续扩充和审阅。

三语词典各增 12 键；Node 用例在中、英、俄切换后核对已知码不泄漏服务端英文、未知码仍保留原文，且三包键和参数一致。Windows/Linux 有界发布门禁均通过 114 项 Python、58 项 Node、68 个模块解析、20 个运行探针及确定性打包；Windows 单文件启动与零旁路写入通过。根目录 `mdo.exe` 已重建，SHA-256 为 `b7d6be8db00cf89685c75a949bd0afbb88337a0609916f5c6fbfdf1b599ea68a`，Linux 包为 `ad004b62485271fe8077d46a34ca759998f7e67c0e186015dbfbd9db637fe733`。本阶段是错误文案与模块测试，尚未把每一种服务端故障逐一在真实打包页注入；项目彻底清除、防休眠、运行中异常退出判定仍属于独立缺口。未进行压力或高负载测试。

## 2026-09-27：极短屏俄语审批的可点击空间

上阶段恢复“本轮均允许”后，继续用单文件打包页检查极短视口。旧包 Home `.build/mdo-packed-docks-zob4w87h` 在 280×250 的俄语界面发送 `APPROVAL RUN UI`，有待决操作时输入区工具栏被 token 输入估算挤成两行、高 95px；审批停靠位仅 27px，40px 高的三个决议无法完整露出。这是实际操作阻塞，不能只凭无横向溢出或按钮存在判为通过。

现在仅在移动端高度不超过 300px 且有待决操作时隐藏输入估算文字；token 明细按钮、输入框、附件入口和发送/停止操作仍保留。工具栏回到 53px 单行，审批停靠位达到 69px。最终单文件 Home `.build/mdo-packed-docks-dl0v3uoi` 在同样 280×250 俄语条件下，滚动时“拒绝”“允许一次”“本轮均允许”三个 40px 按钮分别完整进入停靠位，实际点击“本轮均允许”后同一轮的两条 `exec` 正常完成，首条 `exit_code: 0`、第二条不再请求审批，回复和输入焦点正常；页面宽 280px、浏览器脚本错误为空。Windows/Linux 有界门禁分别通过 114 项 Python、58 项 Node、68 个前端模块解析、20 个运行探针和确定性打包；Windows 单文件零旁路写入及启动检查亦通过。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `7e067ad80c100ae12bd1723f55a911211b5b3e471f2d494da65fd46bf6695449`，Linux 包为 `49dd86cc037cdbc2699ac4f39c1322cca3b881b60b189c949ba5d92e38a64cfe`。此项是浏览器视口模拟，实体设备触控和软键盘仍需验收，未进行压力或高负载测试。

## 2026-09-27：审批的本轮授权范围

旧版 `app_bak/wwwroot` 按钮称“本会话均允许”，但旧引擎将 `bAutoAllow` 放在 `MdoRun`：它允许当前运行后续工具，包含子 Agent，不延续到下一轮或进程重启。新界面在对话审批卡和检查器都补上“本轮均允许”，与“允许一次”“拒绝”并列；三语文案明确当前运行的范围。`PUT /api/v1/approvals/{id}` 接受 `allow_run`，同一请求只接受一次决议。服务端由每次运行创建的 Agent owner 持有授权状态，父子 Agent 共用权限回调数据；在管理器锁内授予当前及同一 owner 中已等待的请求，之后该 owner 的需审批请求直接允许。新运行获取新 owner，重新询问。取消、超时、关闭和无法准确表示的请求仍拒绝，已作出的单次决议不修改授权状态。生产路径通过显式运行选项绑定状态；独立 Agent 测试夹具仍可注入原有回调数据。

有界 API 探针覆盖 `allow_run` 后同一 owner 的第二个请求无弹窗、新 owner 重新进入待审批；前端共享锁测试覆盖两个入口不会重复提交。单文件 Home `.build/mdo-packed-docks-fkhu5qcv` 在 320×350 用本地模型发送 `APPROVAL RUN UI`：对话卡和检查器都展示三个决议，检查器的第三个按钮完整可见；点击“本轮均允许”后两条顺序 `exec` 均 `exit_code: 0`，第二条没有再弹审批。随后发送 `APPROVAL NEXT UI` 再次出现审批卡，拒绝后保留策略拒绝输出。页面脚本错误为空，输入焦点恢复。Windows 有界发布门禁通过 114 项 Python、58 项 Node、68 个模块解析、20 个运行探针、严格 C 编译、确定性打包、零旁路写入及启动检查；根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `96329429e97ee70b449cb08739d40e53aa6fd2522da914073dd2fcd52d1cd557`。Linux 有界门禁同样通过，确定性包 SHA-256 为 `6097f0f5b5b87a6304a21d44e533970c662396ed7073db1d991e75f37fe7d91c`。实体手机触控与软键盘尚需设备验收；未进行压力或高负载测试。

## 2026-09-27：极短停靠位的滚轮阅读步长

在旧单文件 Home `.build/mdo-packed-docks-fd41240o` 的 280×250 页面，长询问出现时停靠位只有 69px 高。手动滚回问题开头后，鼠标滚轮一次使 `scrollTop` 从 0 跳到 250，略过问题中段，用户难以在短桌面窗口读全问题。修复后只在滚轮位移超过停靠位可视高度时，把单次位移限制在高度的 80%；小幅触控板滚动保持浏览器原生行为，触摸滚动、键盘滚动和 Ctrl/⌘+滚轮也沿用原行为。换算像素、行、页三种标准滚轮单位，避免系统或浏览器报告单位不同时跳过正文。

最终单文件 Home `.build/mdo-packed-docks-l07jsyu8` 的 280×250 页面，长俄语询问从顶部滚动一次只前进 55px，连续滚动可达选项；实际选择后工具结果和 Agent 回复出现，刷新仍保留答案，待决项为零，文档宽度保持 280px、脚本错误为空。中间单文件 Home `.build/mdo-packed-docks-l_9g457s` 还在相同尺寸逐段滚过审批卡，第三次滚动后“允许一次”按钮位于停靠位内，点击“拒绝”后工具输出为 `tool execution denied by approval policy`，输入焦点回到主输入框。Windows/Linux 有界门禁均通过 114 项 Python、57 项 Node、68 个前端模块解析、20 个运行探针和确定性打包；Windows 另通过单文件零旁路写入及 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `23d7fdcb611889efe8a0f0223c86fe2bb4e59f70140e6ba46176430e1e0e2bc2`，Linux 发布包为 `788f0be3255411839e728cf56100e850f32d83974afdf21417acfc717fc30a9c`。视口为浏览器模拟，实体手机触控和原生 Linux WebView 仍待验收；未做压力或高负载测试。

## 2026-09-27：长询问选项在短屏自动进入可操作区域

旧单文件 Home `.build/mdo-packed-docks-7fszg8_t` 的 320×350 停靠位为 y=52–221，但长俄语询问的首个选项位于 y=206.8–283.7，只露出约 14px；320×250 时选项完全在停靠位之外。原因是到达时没有定位选项，而旧滚动算法即使发现选项越界，也只把卡片顶部对齐，几乎无法改善选项可见性。现以首个决策操作为目标，在待决项到达和停靠位高度变化后重新定位；尺寸超出停靠位时显示操作的开头，用户仍可在停靠位内滚动阅读完整问题与所有选项。`ResizeObserver` 同时观察对话区和停靠位，处理待决控件收起造成的后续高度变化；用户主动滚动后，普通尺寸变化保留其阅读位置。

最终 Windows 单文件 Home `.build/mdo-packed-docks-6coe5pjw` 在 320×350 下首个长选项完整位于 y=139.8–216.7、停靠位 y=52–221；320×250 下选项开头位于 y=55.8–132.7、停靠位 y=52–121，可直接点击或继续内部滚动。恢复到较高视口会重新露出更多问题正文；手动滚回问题后再调整高度，滚动位置保持不变。实际点击长选项后，工具结果与 Agent 回复出现，输入焦点回到输入框；刷新后答案和回复仍可回放，待决项为零。相同包在 320×330 还完成审批卡内部滚动及“允许一次”，无害命令返回 `exit_code: 0`。生产模块夹具 `tests/fixtures/conversation-dock-scroll-browser.html` 在 320×350、390×500 均通过，浏览器脚本错误为空，打包页宽度保持 320px。Windows/Linux 有界门禁均通过 114 项 Python、57 项 Node、68 个前端模块解析、20 个运行探针和确定性打包；Windows 另通过单文件零旁路写入及 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `d3bc4913a77d44b8ea3735728267f8c699b58ba6240da7a3d9bccf1eb0417e5d`，Linux 发布包为 `40c65fc0525edf5fc3cf7918bbe461ac0580f0448349cf2561cab57b99af1703`。短视口来自浏览器模拟，实体手机软键盘与原生 Linux WebView 仍待验收；未做压力或高负载测试。

## 2026-09-27：扩展页权限摘要与来源标记

旧单文件 Home `.build/mdo-packed-docks-nyvzij6b` 的 320×350“扩展与 MCP”页把默认 Agent 权限直接显示为内部值 `balanced`，内置 Skill 同时显示两个“内置”。资源卡现在复用输入区已有的权限翻译键；Skill 的信任类别已经说明来源时不再重复同义的来源标记，其他信任类别仍保留来源信息。未知权限值继续原样显示，避免错误地解释自定义 Agent 数据。

最终单文件 Home `.build/mdo-packed-docks-of3oj46k` 在 320×350 下显示中文“询问”且 Skill 只留一个“内置”；即时预览英语和俄语后对应显示 “Ask first” 与 “Спросить”，内置来源也分别译为 “Built in” 和 “Встроенный”。资源页按钮均高 40px，文档宽 320px，浏览器脚本错误为空。资源名称与描述仍来自模块或 Skill 自身，内置英文描述未在此阶段改写。Windows/Linux 有界门禁均通过 114 项 Python、57 项 Node、68 个前端模块解析、20 个运行探针和确定性打包；Windows 还通过单文件零旁路写入及 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `39b51d8a147865b7d5473c3152a8ef26abb7f26671c9fdb94a83630513b6541f`，Linux 包为 `b71361001a5745a541a92dc457dbcc0e45199c5af435ea2b6cfcedbd64028767`。短视口由浏览器模拟，实体手机触控和原生 Linux WebView 仍待验收；未做压力或高负载测试。

## 2026-09-27：`/settings` 返回对话的打包页补验

在未改程序的单文件 Home `.build/mdo-packed-docks-y27di18d` 中，从会话输入框键入 `/settings`，候选显示“打开设置”，按 Enter 跳转到 `#/settings/general` 且设置标题获得焦点；点击“返回 Agent 工作区”后回到原会话 URL，输入框重新聚焦，命令草稿已清空。浏览器脚本错误为空，文档没有横向溢出。本次只补充旧版斜杠命令的图形操作证据；程序仍为提交 `7899e14` 的构建，未做压力或高负载测试。

## 2026-09-27：配置创建任务的标题边界与三语反馈

旧单文件 Home `.build/mdo-packed-docks-8my8uke4` 在 320×250 下允许输入 100 个汉字作为标题；HTML 的 `maxlength=160` 按字符计数放行，服务端却因标题为 300 个 UTF-8 字节、超过 256 字节上限而只返回英文 `The session create document is invalid`，提交后焦点也离开输入项。现将会话标题字节计数集中在 `session-title.js`，配置创建、重命名、分叉提交前都按同一上限校验；超限时在弹窗中显示当前字节数与上限，并把焦点留在标题输入框。自动生成的新任务标题也复用这一上限。配置创建弹窗的标签、选项、按钮和免费模型后缀接入已有中英俄词典。

最终 Windows 单文件 Home `.build/mdo-packed-docks-a94kkhnj` 在 320×250 俄语界面输入 100 个汉字时，完整俄语错误提示与确认按钮均留在弹窗内，标题仍可编辑且聚焦；改为 85 个汉字（255 字节）后成功创建会话、手机侧栏关闭、焦点进入输入框，文档宽度仍为 320px，浏览器脚本错误为空。中间包还验证中文和英文的超限提示、英文/俄文免费模型后缀及重命名弹窗的超限反馈。极短视口压缩配置弹窗的标题和边距，字段区独立滚动，不隐藏错误或操作栏。Windows 有界门禁通过 114 项 Python、57 项 Node、68 个前端模块解析、20 个运行探针、确定性打包、单文件零旁路写入和 20 秒启动检查；根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `91b2204308fb29fc2a8c2ae3daa799cc0a0ca740e089d50ede2d644f93fff4a6`。Linux 有界门禁输出 20 个运行探针与确定性打包 PASS，构建产物两次 SHA-256 均为 `1de3ce2dfa026341f72d38a2182f142f38963fb4b71b1786caa03255fce21f38`。短视口由浏览器模拟，实体手机触控和原生 Linux WebView 仍待验收；未做压力或高负载测试。

## 2026-09-27：配置创建任务的短屏操作与焦点

旧单文件 Home `.build/mdo-packed-docks-js94isep` 的 320×350 页面打开“配置后创建任务”时，弹窗下缘 y=333，而“创建会话”按钮在 y=608–648，标题以外的设置和提交操作需滚动整个弹窗。现在它与会话操作弹窗共用固定标题和操作栏、字段区内部滚动的布局；有错误提示时也保持操作栏占位。中间包 `.build/mdo-packed-docks-kv13b23x` 在 320×350 量得按钮 y=287–327、小于弹窗下缘 y=334；320×250 时按钮 y=187–227，全部字段可滚动到达。中间包实际创建还暴露出手机侧栏留在打开状态、焦点回到原入口的问题，故配置创建成功后收起手机侧栏，并等待新会话详情就绪再把焦点交给输入框；如果用户在等待期间改选其他控件，沿用现有焦点保护。

最终单文件 Home `.build/mdo-packed-docks-4f940zi5` 在 320×250 从弹窗选择 Ling 3.0 Tiny、高推理、只读权限，创建 `Focus final QA` 后侧栏关闭、焦点在输入框；刷新后标题、模型、推理强度和权限仍保持。761×300 下弹窗 y=16–284、“创建会话”按钮 y=237–272，创建 `Desktop focus QA` 后桌面输入框获得焦点。浏览器脚本错误为空。Windows/Linux 有界门禁均通过 114 项 Python、57 项 Node、67 个前端模块解析、20 个运行探针及确定性打包；Windows 还通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `b703e2388547eb6eaf0e9574b12b1bbd436547efbe9277481201e8c51d9383de`，Linux 包为 `4264979363ed6a14d1fde0a75e04c752fb4a2048d670b4a6a3b6bcb596a1aff4`。短视口由浏览器模拟，实体手机触控和原生 Linux WebView 仍待验收；未做压力或高负载测试。

## 2026-09-27：短屏分叉弹窗

`/fork` 在原单文件 Home `.build/mdo-packed-docks-yhme0zwv` 的 320×350 页面已能打开弹窗，但“创建分支”按钮下缘 y=344 超出弹窗下缘 y=333，正文出现不必要的滚动条。会话操作弹窗现把标题、字段和操作栏组织为可收缩纵列：操作栏始终留在弹窗内，仅在极短视口让字段区滚动；手机断点收紧标题、字段与操作栏间距，不改变分叉参数或服务端事务。

重新构建根目录 `mdo.exe` 后，隔离单文件 Home `.build/mdo-packed-docks-z2r97wxn` 在 320×350 下实测弹窗 y=28–322、确认按钮 y=273–313，字段区无需滚动；320×250 下弹窗 y=16–234、按钮 y=187–227，字段区从 154px 内容收缩到 79px，滚动后仍可看到序列输入。实际发送一次 `MARKDOWN UI` 后执行 `/fork`，在 320×250 创建 `Fork mobile QA`，新会话保留用户与 Agent 的 Markdown 回合、焦点回到输入框，刷新后历史仍在；原 `Packed docks QA` 会话保留原回合，浏览器脚本错误为空。Windows/Linux 有界门禁均通过 114 项 Python、57 项 Node、67 个前端模块解析、20 个运行探针及确定性打包；Windows 还通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `2da53652cee9ecfcd2c2bdbf240439f634d9fb91cb348e651583afcea7bb1c16`，Linux 包为 `273a267353e4ee661adab36cbb9eddd88be5538028a4dbe5b59e8e6c60dd0158`。短视口由浏览器模拟，实体手机触控和原生 Linux WebView 仍待验收；未做压力或高负载测试。

本阶段建立界面语言基础层：`app/web/js/i18n.js` 从单文件包内的 `/lang/` 读取中、英、俄词典，提供翻译、参数替换、静态节点翻译和切换订阅；迟到的异步加载不能覆盖更新的语言选择。语言标识沿用旧配置迁移的 `zh-CN`、`en-US`、`ru-RU`。侧栏、会话状态、欢迎区和输入区的静态文本及无障碍名称使用翻译键。当前仍固定中文启动，设置页不开放语言切换；动态消息、设置与任务面板尚未全部迁入词典，不能把此阶段视为三语体验完成。`tests/test_frontend_i18n.mjs` 检查三份词典键集合、HTML 标记覆盖、来回切换与迟到请求。最终单文件页的三个词典 URL 均返回 200 和 43 个键，页面 `lang=zh-CN`、中文入口正常且无浏览器脚本错误；有界发布门禁的 114 项单元/契约测试、18 个运行探针、确定性打包、零旁路写入和短时启动检查通过。根目录 `mdo.exe` SHA-256：`7346106b5c615c202c8a34b39cb3bb23d733e073c535004e6da9b5057f03f069`。

补验会话导出的实际下载数据。隔离打包 Home `.build/mdo-packed-docks-bqwgny1p` 中发送 `MARKDOWN UI`，真实本地模型回复包含 Markdown 标题、安全 HTTPS 链接与 C 代码块；桌面会话栏点击“导出当前会话 Markdown”后，通过临时浏览器调试钩子读取传给 `URL.createObjectURL` 的 Blob，确认其文本以 `# Packed docks QA` 开始，包含用户消息 `MARKDOWN UI`、`## 助手`、`[Example](https://example.com/guide)` 和完整 `int answer(void) { return 42; }` 代码块。页面显示下载开始提示，没有脚本错误，调试钩子随临时页面关闭移除。此前同一打包导出链的调试协议已确认 281/281 字节下载完成；本次核对的是浏览器下载数据本身，内嵌浏览器未提供磁盘文件路径，因此不声称已另行读取磁盘落地文件。程序未变，根目录 `mdo.exe` SHA-256 仍为 `7346106b5c615c202c8a34b39cb3bb23d733e073c535004e6da9b5057f03f069`。

语言迁移继续覆盖会话导航的动态内容。侧栏分组、空状态、未读提示、相对时间、项目按钮和会话操作名称现从同一词典读取；桌面与手机顶栏共用动作描述，切换语言时重绘并保留菜单项的键盘焦点。三份词典均有 68 个相同键，测试核对占位参数、静态及动态键、异步切换和动作名称。隔离打包 Home `.build/mdo-packed-docks-mxxv203d` 实测英语和俄语下的会话列表、相对时间与顶栏菜单；菜单焦点在切换后仍位于“重命名”，Esc 返回入口。320px 俄语手机侧栏文档宽度保持 320px，菜单边界在视口内；切回中文后无浏览器脚本错误。语言设置入口仍未开放，聊天、任务、设置模块的动态文案尚未迁完。有界发布门禁通过 114 项单元/契约测试、18 个运行探针、确定性打包、零旁路写入和短时启动检查；根目录 `mdo.exe` SHA-256：`4d44cec2ed1f205f3d208561e6789257c8cb97ea5fbb38f304e9b3bafa84a97a`。

中断恢复补齐成功路径的打包页证据。`tests/manual_packed_docks_qa.py --resume-verify` 为独立测试会话启用一次有界的只读工作区检查；固定模型仅在 xwork 发出完成验证提示时请求 `exec`，检查夹具 `README.md` 的预期内容，不改变工作区。隔离 Home `.build/mdo-packed-docks-t26mxke5` 中发送 `SLOW UI` 后停止，页面提示恢复并聚焦“继续恢复会话”；提交后模型先给出固定回复，再收到验证提示并请求只读命令。通过页面“允许一次”后工具结果 `exit_code: 0`，恢复运行状态为 `succeeded`、`tool_calls=1`、`resume=true`，API 返回 `resume_required=false`、待审批数为零。刷新后恢复标记、回复和可展开的工具结果仍在，页面无脚本错误。原慢速回合状态为 `cancelled`；此验收只增加隔离夹具与证据，不改变生产恢复机制。根目录程序 SHA-256 仍为 `4d44cec2ed1f205f3d208561e6789257c8cb97ea5fbb38f304e9b3bafa84a97a`。

输入区动态语言迁移覆盖模型免费标记、思考强度、项目选择、权限标签、工作区提示、运行状态、发送快捷提示和 token 估算/明细。三份词典均有 114 个同名键，测试核对占位参数和使用键；语言切换时重绘选项与用量，但保留当前模型、权限和草稿。隔离打包 Home `.build/mdo-packed-docks-gvn41mta` 中，英语页面的输入区与 token 明细均显示对应译文，`Review README.md` 草稿保持不变。320px 俄语实测发现 token 明细弹层原先左边界为 `x=-23`，虽文档宽度仍为 320px，但弹层内容被视口裁掉；现让弹层相对整个输入操作组定位。新打包 Home `.build/mdo-packed-docks-5u_1k1ts` 中，中文和俄语的弹层左右边界均为 `x=23..303`，运行中停止按钮出现时亦如此；模型、思考、权限、token 按钮和发送按钮均高 40px，页面无脚本错误。设置页语言入口及其他动态模块仍未完成。有界发布门禁通过 114 项单元/契约测试、18 个运行探针、确定性打包、零旁路写入和短时启动检查；根目录 `mdo.exe` SHA-256：`ced0eced82f7c52b2dde255a22444ece685866857b894aaa0abea59b7e0bf188`。

## 已落地

| 交互 | 当前实现 | 约束 |
| --- | --- | --- |
| 消息统计 | 会话事件保留模型输入、输出 token；回复显示 token/s 和完整时间提示 | 旧事件 schema 1/2 可回放，新事件 schema 3 还记录用户消息的持久序号 |
| 恢复轮次显示 | 新事件中无用户消息序号的恢复启动显示为“恢复”系统状态，不再伪装成用户消息；取消运行写入终止事件，回复保留已生成内容并显示“已停止” | 旧 schema 1/2 没有持久序号，继续按原事件投影；真实用户消息仍可复制、编辑和重试；取消状态重启后可从会话日志回放 |
| 思考与工具 | 思考过程和工具调用使用可键盘展开的折叠行；工具调用内容、结果及失败输出分区显示，可分别复制 | 时间线刷新时保留展开状态以及工具输入、输出复制按钮的焦点；持久事件 ID 区分工具卡，同一会话内重启后复用的运行编号不会串接思考或工具结果；长输出仅在展开时挂载并限制显示高度 |
| 消息操作 | 复制、分叉、点赞/点踩采用紧凑图标栏，位于回复下方；用户消息可编辑，对应回复可重试；图标保留名称和悬停提示，手机窄屏始终可见并提供 40px 触控目标 | 编辑/重试在原会话截断目标用户消息及后续历史，再发送新文本和原图片；有草稿、待发队列或运行中任务时先要求处理它们；回复分叉以所点回复后的账本序号为边界；旧事件无序号或文本截断时不提供精确操作；同一会话的时间线刷新后保留消息按钮、代码复制和链接的键盘焦点；编辑、分叉、重试及反馈在请求期间共用会话级操作锁，重绘和键盘触发也不能重复提交 |
| 任务与审批 | 当前会话内嵌后台任务和一次性权限审批；任务详情、审批按钮和参数展开项在状态刷新后保持键盘焦点，参数展开状态也保留 | 按会话和 run 关联，审批决策由服务端执行；提交中及已提交但列表尚未更新时禁止重复决策，决策卡片消失后焦点返回输入框或决策页入口 |
| 后台完成提醒 | 切到其他会话后持续轻量刷新运行列表；后台会话完成时标题显示未读会话数，侧栏标出“有新结果”，进入该会话后清除 | 完成提示音可在“常规”中开启，偏好存于便携 Home 并迁移旧版 `sound`；无音频能力时保留视觉提醒；页面不可见时暂停轮询，返回后立即刷新 |
| 新任务入口 | 侧栏“新建任务”、Ctrl/⌘+K、Ctrl/⌘+N 和 `/new` 直接切到空白输入区，首条消息再创建持久会话；输入区可选已有项目，旁边的配置按钮仍可指定 Agent、项目、模型和标题 | 从会话新建任务沿用其项目；项目选择写进 `#/projects/<id>/new`，刷新或从设置返回仍保持项目和草稿；手机端收起侧栏并聚焦输入；重复点击空白任务不清除其草稿 |
| 空白任务示例 | 恢复旧版五个入口：项目分析、自我介绍、测试修复、小工具和定时任务；前四项点击后直接发起任务，定时任务进入计划页 | 示例发送复用输入区的运行路径，但不覆盖正在编辑的草稿；新任务页的草稿留在原路由，切回或刷新可恢复；示例运行失败时，若当前输入框空白则填入示例文本供重试 |
| 工作区提示 | 输入区显示当前会话工作区的目录名，悬停可查看路径；新任务改为显示所选项目的工作区，旁边的上下文面板列出项目与路径 | 会话继续显示创建时的独立工作区快照；从会话切回新任务或切换项目时立即更新提示，不沿用上一会话目录；未注册项目显示“本地工作区” |
| 侧栏项目分组 | 置顶会话单独列在顶部，其他会话按项目分组；每组可直接新建该项目的任务，进行中视图保留无会话的项目入口 | 搜索和状态筛选仍按会话过滤；新建任务会清除搜索并回到“进行中”，避免新会话被原筛选隐藏；菜单、选中态和刷新后的焦点均以项目 ID 加会话 ID 定位 |
| 添加项目 | 侧栏“项目”标题可直接添加工作区；输入目录后自动带出名称和标识，允许修改并选默认模型；空项目立即留在侧栏，创建后进入该项目的新任务输入区 | 定义单独保存在便携 Home 的 `projects/<id>.json`；未定义但已有会话或计划的项目仍显示；创建会话时继承项目工作区和可选默认模型，显式会话选项优先；只读列表和单文件启动不创建 Home |
| 项目配置管理 | 侧栏项目标题的管理按钮直达项目设置；可编辑名称、工作区和默认模型，模型也可在项目卡片直接切换；取消注册前显示保留会话与计划的确认弹层 | 编辑与取消注册使用当前 ETag，过期版本拒绝覆盖；更新对新会话生效，已有会话工作区保持创建时快照；取消注册只移除项目定义，保留会话与计划，文件更新和移除前保存 `.bak`；项目列表刷新后恢复操作焦点 |
| 项目清除范围 | 已注册项目可在设置页打开只读清单，查看当前会话、关联计划、项目记忆、运行态与诊断数量，并手动刷新 | 清单是瞬时快照，不授权删除；弹窗说明共享审计记录与迁移报告的处理边界，彻底清除入口在安全事务完成前不开放 |
| 全局与项目记忆 | 项目设置可打开全局记忆或各项目记忆，浏览条目摘要并按需读取正文；支持新建、编辑、置顶、标签和删除，显示 Home 内的可追溯 JSON 路径 | 存储沿用现有记忆管理器的限额、敏感内容校验及审计；整个作用域共用 ETag，旧版本写入返回 412；未保存的草稿不会因切换条目或关闭弹层丢失，刷新明确放弃草稿；只读访问不创建 Home |
| 打开记忆目录 | 项目页可一键打开全局或项目记忆所在文件夹，记忆弹窗也提供相同入口 | 只在用户点击时按需创建 Home 下对应目录；API 只接收空 JSON 对象，不接受任意本地路径；xrt 使用宿主系统默认文件管理器打开，远程浏览器收到的是运行 mdo 的设备上的打开结果 |
| 模型管理 | 模型页恢复 Provider 与模型的列表/详情编辑，可新增、编辑、删除自定义项并切换全局默认模型；协议、能力、思考强度、附件和上下文参数均可配置 | 页面只使用 `secret_ref`，不接收 API Key 明文；模型事务先预览，再用配置 ETag 提交，过期版本拒绝覆盖；Ling 3.0 Tiny 的 Provider 和模型由服务端保护，均不可编辑或删除；运行时配置覆盖期间禁用页面编辑 |
| 桌面分栏 | 侧栏和任务面板可拖动分隔线调宽，也可用方向键、Home、End 调整；桌面侧栏可收起并从工作区顶栏恢复 | 首次使用时与旧版一致，任务面板默认收起，给聊天与输入区完整宽度；宽度和开关保存在便携 Home 的 `data/pane-layout.json`，已有展开偏好继续生效，首次读取不创建 Home。可用宽度不足时临时收窄分栏，偏好值保持不变；手机端使用原有抽屉，临时开关不覆盖桌面偏好 |
| 移动端抽屉与输入 | 手机侧栏和窄屏任务面板打开时将键盘焦点移入面板，Tab 在面板内循环，Esc 或关闭按钮返回入口；手机顶栏、输入区及会话内待办/审批/询问操作采用 40px 触控目标 | 手机端同时只显示一个抽屉；桌面常驻分栏保持原有焦点行为；320px 和 390px 宽度下输入选项、长任务标题及询问选项不会使页面横向溢出 |
| 启动时打开 | 常规设置的“上次会话 / 新任务 / 每次询问”在无 hash 的应用入口生效；上次会话是最后选中的会话，询问弹窗可继续它或进入空白任务 | 选择写入便携 Home 的 `data/workspace-state.json`，首次读取不落盘；明确的会话 URL 和 `#/` 空白任务优先于启动设置；会话不存在时回退到最近的进行中会话或空白任务 |
| 中断恢复入口 | 输入或队列因持久会话要求恢复而暂停时，输入区提示可直接打开“决策”面板并定位到恢复按钮 | 手机端同时展开任务抽屉；焦点落到恢复操作，草稿和 `pending` 队列保持不变 |
| 待办计划 | 内置 `mdo.todo` 工具提交完整计划快照；会话内显示可折叠的完成进度，刷新后恢复，折叠按钮在状态重绘后保留焦点 | 仅投影主 Agent 成功的工具结果；最多 24 项，每项 1024 UTF-8 字节，总输入最多 12 KiB；快照保存在便携 Home 的会话 `todo.json`，读取缺省计划不创建 Home；截断后从保留的工具事件恢复上一版计划，清空后清除计划，分叉从复制的事件生成独立计划 |
| 询问用户 | 会话绑定的 `ask_user` 工具等待用户回答；聊天区显示选项及自由输入，刷新后读取待答卡片；自由回答提前校验字节上限并显示提交状态；提交中保留操作焦点，卡片消失后焦点返回输入框 | 最多 16 个并发询问，每题最多 8 个选项；空回答不可提交，超过 1024 UTF-8 字节时禁用自由回答按钮但不阻断选项；回答锁、已提交状态及自由输入草稿按项目/会话/询问隔离，切走再返回可恢复未提交草稿；回答只作用于原会话且只能提交一次；取消、运行超时和进程关闭会清除在线待答；进程中断后的待执行工具由 xwork 会话恢复时安全重试并重新提问 |
| 用量 | 输入 token 估计、最近一次模型输入与上下文窗口比例；模型和上限跟随输入区当前选择 | 输入估计是近似值，模型用量取服务端记录；切换模型后最近一次调用可能属于旧模型 |
| 运行中输入 | 可继续输入、排队、删除待发消息；待发队列显示数量、顺序和图片缩略图，可收起及展开；按输入模式选择中断优先发送；刷新后恢复队列和优先发送意图 | 队列存于会话 `queue.json`（schema 5；兼容旧 schema）；优先项在刷新后仍会请求取消当前运行，等待服务端确认后检查恢复状态；无待决工具调用时以 revision 和账本序号核对并结束中断轮次，遇到取消末尾事件的短暂竞态会重新读取并有界重试，然后派发队首；有待决工具调用时保留 `pending` 队列及草稿，交给“决策”面板；刷新遇到不确定发送状态会暂停并提示核对，队列收起时提示仍可见；用户确认后才能重试；队列重绘保持键盘焦点，清空后返回输入框；重试、移除和自动派发锁按会话隔离，后台会话失败不会覆盖当前会话的输入错误 |
| 输入提交模式 | 常规设置可选“排队”或“引导”；排队模式下 Enter 入队、Ctrl/⌘+Enter 中断并优先发送，引导模式下两者对调；提示文字及快捷键帮助随设置更新 | 独立的 `settings.composer.submit_mode` 控制输入，不改 Agent 的 `interaction_mode`；旧版 `interactMode` 在新迁移中映射到此字段，已有旧版迁移结果不能自动推断用户原来的输入偏好 |
| 模型、思考、权限 | 输入区直接选择；新任务先用项目默认模型，否则用服务端全局默认；同一项目的手动选择在当前页面会话内保留；空闲会话先恢复上下文并验证配置，再原子更新元数据 | 活跃运行和待恢复调用不能切换；配置更新锁按项目和会话隔离，切换到其他会话不阻塞其输入；已有会话详情加载前不将配置选择误写进新任务偏好；会话 schema 3 兼容旧 schema 1/2 |
| 回复排版 | 用 DOM 节点渲染标题、列表、代码、表格、引用、安全的 HTTP(S) 链接和可放大的 Markdown 图片 | 不使用 `innerHTML` 插入模型输出；图片只接受 HTTP(S) 或至多 1 MiB 的 PNG/JPEG/GIF/WebP data URI，远程图片不传 Referer；预览关闭及时间线刷新后焦点返回图片按钮 |
| 点赞、点踩 | 回复下方互斥切换；以成功的 `model_done.event_id` 为键保存到会话 `feedback.json` | 服务端验证事件归属；API 截断/清空按历史边界清理已删除回复的反馈，GET/PUT 可修复中断的清理，同时保留仅因有界日志滚动而不可见的旧反馈；单文件模式无反馈时不创建 Home |
| 反馈记录 | 设置页汇总所有项目的点赞和点踩，可继续分页读取并打开对应会话 | 服务端每页最多返回 50 条反馈、检查 100 个会话；游标带会话目录代数，目录变化时提示重新读取；仍可回放的回复显示原事件时间，旧日志无法回放时明确显示时间不可用；损坏目录诊断有提示 |
| 斜杠命令 | 输入 `/` 显示可键盘操作的命令菜单；新建、模型轮换、分叉、清空、导出、停止、设置和主题切换复用现有操作 | `/model` 直接轮换到下一个可用模型，已有会话按配置事务持久化；`/theme` 直接切换深浅主题，并与侧栏按钮、快捷键共用配置预览及 ETag 应用；清空与分叉仍走原有服务端会话操作和确认流程；未知命令仍可作为普通提示发送 |
| 快捷键帮助 | 侧栏底部、输入框外的 `?` 或 `/help` 都可打开帮助；侧栏也可直接切换主题；`Ctrl/⌘+K` 和 `Ctrl/⌘+N` 新建任务，`Ctrl/⌘+F` 搜索当前会话，`Ctrl/⌘+E` 导出，`Ctrl/⌘+J` 切换深浅主题，`Ctrl/⌘+,` 打开设置；Esc 优先关闭弹层、搜索或移动端抽屉，其后中断运行 | 帮助弹窗由原生 `dialog` 承载，关闭后焦点返回原元素；运行中断仍由服务端确认；侧栏按钮与主题快捷键共用配置预览、ETag 应用流程，并避免覆盖未保存的设置表单 |
| 会话操作菜单 | 鼠标打开后保留按钮焦点，键盘打开后聚焦首项；上下方向键、Home、End 浏览选项；Esc、菜单外点击或焦点离开时收起，执行菜单项时立即收起再打开对应操作 | 菜单与弹窗不叠在一起；列表刷新或置顶重排后焦点跟随会话，归档使会话离开列表时转到可见会话；手机侧栏中的首次 Esc 只关闭菜单 |
| 会话导出 | `Ctrl/⌘+E`、`/export` 与会话菜单恢复可读 Markdown；菜单另有 JSON 原始快照备份 | Markdown 从持久事件流按 32 条分页读取，单次最多 4096 条；旧事件已滚出、日志正文截短或达到导出上限时在文件开头说明。图片只列附件 ID，文件本身不嵌入 Markdown；JSON 保留原有服务端快照格式 |
| `@` 文件补全 | 输入 `@` 加文件名或路径片段，从当前工作区显示候选；新任务首条消息也能根据所选项目补全；方向键、回车、Esc 和点击可操作 | 已有会话使用其创建时的工作区快照，新任务使用项目当前工作区，未注册项目沿用创建会话时的默认工作目录；服务端有界扫描并跳过子符号链接；仅插入相对路径文本，不读取或上传文件内容 |
| 会话内搜索 | 顶栏搜索按钮或 Ctrl+F 打开浮动搜索框；按文本过滤消息并显示命中数，Esc 关闭 | 只搜索时间线已加载的最近记录；出现记录截断时明确提示早期记录未载入。全量历史检索需独立的服务端索引 |
| 长对话导航 | 向上翻阅时显示“回到底部”，点击回到最新消息；位于底部时自动隐藏 | 依据对话容器的滚动位置判断，实时输出仅在用户保持底部时自动跟随 |
| 输入草稿 | 新任务与各会话分别存入便携 Home；刷新和切换会话恢复输入 | 服务端 revision 防止多窗口静默覆盖；保存失败保留页面输入并显示状态；快速离开使用 keepalive 请求 |
| 图片输入 | 选择、粘贴、拖入图片后显示可移除缩略图；文件拖到输入卡片时显示落点提示；点击草稿或历史图片可放大查看；纯图片也可发送或排队，刷新后恢复草稿与历史图片 | 预览可用关闭按钮、背景点击或 Esc 关闭，焦点回到原缩略图；草稿缩略图重绘保持预览或移除按钮的键盘焦点，移除最后一张后回到添加图片按钮；附件按会话上传并按 ID 引用，上传与移除锁按会话隔离；移除草稿或队列图片后仅删除服务端确认无其他引用的原件，配额不足时回收超过 24 小时的无引用上传；当前模型必须声明图片能力；Ling 3.0 Tiny 保持纯文本默认模型 |
| 计划任务管理 | 侧栏直接进入计划列表；可创建、编辑、暂停、启用和删除，表单覆盖频率、运行日、时区、项目、Agent、模型及主要执行策略 | 编辑读取详情及 ETag，写入用 If-Match 防止并发覆盖；启停保留未保存的表单内容并更新编辑版本；删除经原生确认弹层；本地时间转换为 API 微秒时间戳 |
| 计划执行历史 | 列表卡片可打开运行历史，按最近完成顺序查看时间、结果和内容摘要 | 读取便携 Home 已有的 JSONL 历史；单次最多返回 32 条，每条文本按 UTF-8 边界截断到 1024 字节；无历史时不创建文件，损坏记录返回错误而不伪造成功状态 |
| 计划立即运行 | 列表卡片可直接启动一次 Agent 执行，完成后进入同一历史视图 | 使用当前 revision 和统一 scheduled task；保留周期游标，遵守并发上限；暂停的单项计划可运行，全局执行关闭或存储故障时按钮不可用；页面可见且有活动任务时轻量刷新状态 |

实现记录：`63130c0`、`5ee831b`、`e0d5b5f`、`826d572`、`9762626`、`3e7a5cd`、`1be36f5`、`f803b2f`、`6d8a185`、`d810457`、`7f2ab7a`、`1f57440`、`2f8cf31`、`e7edc75`、`9e09465`、`64ef3bf`、`55f4ee2`、`3e79e1a`、`d47b966`、`95d7f30`、`21d552a`、`c5afe20`、`e317195`、`916385b`、`8bd3b55`、`d2411ae`、`6174fcf`、`6217e9c`、`ce1119e`、`89ab359`、`8f5f44b`、`951ee9f`、`4fdd53e`、`00965f1`、`21f3de5`、`e4ec5c0`、`b667064`、`7ed4213`、`7a271b4`；近期补齐图片引用保护、显式与过期清理、历史截断后的引用修复及旧版图片放大交互。

## 尚未迁移

- 旧版项目管理页中的彻底清除数据尚未迁入。旧版的项目默认项只有模型，新版已经覆盖；旧版 purge 把单个项目数据桶移入 `.trash` 后注销项目。新版项目定义、会话、记忆与计划分散保存，不能把“取消注册”当作彻底清除。只读 `GET /api/v1/projects/{project}/purge-preview` 已列出当前会话、计划、项目记忆和运行阻碍，设置页也能查看和刷新清单；它仍是瞬时清单，不开放删除。执行所需的并发租约、锚定搬迁、崩溃恢复及共享审计记录策略见 [项目清除事务边界](project-purge-transaction.md)。
- 旧版与新版会话内搜索都只查已载入的节点；全量历史检索需要独立的服务端索引，属于后续增强。截断/清空现同步修剪 UI 事件日志与模型账本；旧 schema 无法定位边界时丢弃旧可见投影，避免显示已删除消息，并写入递增 ID 的历史边界事件。图片输入的选择、粘贴和运行中排队已在隔离页面验收，最终 `mdo.exe` 的内置前端也已验证选择、粘贴、带图运行和取消；操作系统原生拖放与图片移除仍须按 [图片附件实施约束](image-attachment-implementation.md) 完成。
- Markdown 渲染覆盖常用块和行内语法，尚未实现完整 CommonMark 嵌套规则。后续可在保持纯 DOM 安全边界的前提下扩展。

## 验收

运行 `python tools/qa_release.py --xserver-root D:\GIT\xserver-mdo-refactor --skip-host-build`。门禁含 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、Windows 单文件零旁路写入和短时启动检查，不含压力或高负载测试。2026-09-25 的打包 SHA-256 见本节末尾。编辑/重试的服务端消息边界和可见事件、图片上传、纯图及文图模型请求、图片草稿与队列持久化，以及进程重启后的 run ID 复用、旧记录兼容、图片分叉独立性与失败回滚已通过运行探针验证；打包页可访问性树确认图片入口及原有输入选项；弹窗及按钮仍待打包页手动验收。Windows 辅助功能树可以读取打包页，但本次设备的窗口截图接口返回 `SetIsBorderRequired` 不支持，按钮坐标操作因此无法验收。新增项目阶段还通过本地真实 xs/TCC 页面验收：桌面侧栏添加 `mdo` 项目后空项目保留、URL 切到该项目的新任务；390px 手机视口能从侧栏打开项目弹窗，弹窗无横向溢出。API 探针验证空项目列表、冲突与坏字段、默认模型和工作区继承。整套门禁通过，打包 SHA-256 为 `b937c9cc2594a5f4e1ee49d6ab7017b5e94e9f9736e0ba75d5eca61d88afeb4d`。

项目管理阶段的真实 HTTP 页面验收覆盖：侧栏管理按钮进入项目设置、项目改名同步到侧栏、直接切换默认模型、取消注册确认层取消后项目仍在；390px 手机视口下设置页宽度保持 390px。API 探针覆盖编辑与取消注册的 ETag、过期拒绝、备份、相对工作区更新、新会话继承和已有会话保留。单元/契约与 18 个有界运行探针、严格编译、确定性打包和单文件检查全部通过；本阶段 `mdo.exe` SHA-256：`b2359aebdbc8e31146d7a0646142bbd1f160f6c7abde262b5776301f446f3dcd`。

手动验收已在打包版完成：同一会话连续输入两条消息，第二条进入可见队列并在首轮结束后自动运行；刷新后持久队列恢复，首条 `sending` 时显示核对提示并阻止后续消息自动派发；计划显示完成比例、项目状态，折叠后可重新展开，刷新后重新读取会话快照；切换思考强度和权限后刷新，服务端返回的新配置保持一致；390px 手机视口的输入区无水平溢出；历史回复生成真实标题和列表；新任务草稿在普通和快速刷新后恢复，会话草稿在切换和刷新后恢复。另用本地 HTTP 开发页实际操作了会话菜单、重命名弹窗取消、设置切换、斜杠菜单、`@README` 文件补全，以及返回会话后的草稿恢复。询问工具的真实模型路径已通过有界 API 探针验证回答、取消及中断恢复；打包页的询问卡片、`@` 菜单及会话搜索操作仍需单独验收。

图片输入的本地 HTTP 页面验收发现：移除草稿缩略图会更新草稿，但页面在清理原件时因 `app.js` 漏导入 API 而报 `api is not defined`。已补齐导入；草稿和附件的服务端删除路径由有界探针覆盖。图片历史消息、输入选项、手机视口侧栏和任务面板均已实际呈现。空闲会话里的预置队列会立即派发，不能据此判断运行中排队；打包页的选择/粘贴/拖放、修正后的移除按钮仍需单独验收。图片运行探针现额外验证分叉后源会话仍能继续运行；先前一次未复现的停顿保留为观察项。

对照旧版后补回图片放大：本地 HTTP 页面用有效 PNG 验收了草稿预览，以及历史图片打开、Esc 关闭、关闭按钮、背景点击和焦点返回；桌面与 390px 手机视口的预览均能正常显示。打包文件仍需另行检查实际点击操作。

思考与工具折叠使用本地静态页面驱动真实时间线模块验收：工具展开后同时显示调用和结果，思考过程可展开；追加事件后保留展开状态，折叠行持有键盘焦点时刷新也能回到同一行；模拟进程重启后复用相同运行编号和工具调用 ID，两轮卡片、结果及折叠状态互不串线。390px 视口未出现横向溢出。真实模型路径的打包页面仍需复核视觉与操作。

待发队列使用本地 HTTP 页面和真实会话 API 验收：人为将一条带图片的队列项置为 `sending`，页面显示待核对提示；收起队列后提示继续可见，重新展开后可打开图片预览，Esc 关闭时焦点返回缩略图。390px 与 320px 视口均无水平溢出。验收数据只存于忽略的 `.build/ui-qa-image-home`。

快捷键帮助在本地 HTTP 页面验收：`?`、`/help` 打开同一弹窗，Esc 关闭；`Ctrl+K` 打开新建任务、`Ctrl+F` 打开会话搜索、`Ctrl+E` 触发会话导出、`Ctrl+,` 打开设置且 Esc 返回工作区。390px 视口弹窗可滚动且无水平溢出。后续本地慢速模型页面已验证 Esc 实际中断运行。

输入提交模式已由 API 探针验证默认值、配置写入和旧数据迁移：旧版 `guide` 进入 `composer.submit_mode`，Agent 策略保持 `agent`。本地 HTTP 页面经设置预览、应用和刷新后仍显示“引导”；Agent 页仍为 Agent 模式，快捷键帮助里的 Enter / Ctrl+Enter 说明随之对调。排队模式的 Ctrl+Enter 已在真实慢速模型路径验收；后续慢速模型页面也验证了引导模式 Enter 中断优先发送、Ctrl+Enter 仅入队并在首轮结束后接续运行。

真实慢速模型路径暴露了中断后持久账本要求恢复的情况：此前队列项在新运行异步失败后可能被移除。现用本地 HTTP 页面及真实会话 API 验证恢复门槛下队列项保持 `pending`、直接发送的输入仍在文本框、界面提示处理恢复决定；无待决工具的优先发送现可自动结束中断轮次并接续运行。

恢复提示的“打开恢复决策”已在本地 HTTP 页面实际操作：桌面端切到决策标签并聚焦“继续恢复会话”；390px 手机端同步展开右侧抽屉并聚焦该按钮，文档宽度保持 390px。

本地慢速模型端到端验收确认：提交恢复决定后，先前保留的 `pending` 消息在恢复运行终止后自动派发并完成；原草稿仍在输入框。测试模型对恢复轮次重复返回相同文本，使恢复运行因缺少工作区验证而按 xwork 策略失败，这不影响随后队列消息的独立成功。恢复启动事件此前被误画成英文用户消息，现按 schema 3 的零用户序号显示为“恢复”系统状态；旧 schema 2 的零序号用户消息仍保持用户类型。

中断轮次的终止事务由本地慢速 Responses 模型、有界运行探针与真实页面操作共同验收：API 对过期账本序号、重复提交及有待决工具的轮次返回 409；成功结束后重启 XS，恢复要求消失，新提示可成功运行。页面实际用排队模式的 Ctrl+Enter 取消首轮，第二条消息自动优先发送并完成；桌面和 390px 手机视口均可在“决策”中手动结束无待决工具的轮次，手机端文档宽度保持 390px。

取消终止事件在 xserver `410f189` 的 xwork 中补齐：取消不再吞掉最终的 `agent_done`；其 `success=false` 与正常失败的 `error` 保持区分。中断运行探针验证该事件只写入一次且重启后仍可读取。本地 HTTP 页面实际停止慢速模型轮次后显示“Agent 已停止”，刷新后仍在。该阶段单文件 `mdo.exe` 的 SHA-256：`c0b9b594326ba3815af3534eb45d5f0d40014992a8b6052c2e292a9e0c611424`。

后台完成提醒在本地 HTTP 页面用两个会话和慢速 Responses 模型验收：A 运行时切到 B，A 完成后标题出现 `(1)`、侧栏出现“有新结果”；进入 A 后两处标记清除。完成提示音开关经设置预览、应用和页面刷新后保持开启。API 探针覆盖新配置的默认值、写入读回和旧版 `sound` 迁移；音频设备实际发声未在本机自动化环境测量。

本阶段发布门禁通过：114 项单元/契约测试、18 个有界运行探针、确定性打包、单文件零旁路写入和短时打包启动检查。生成的 `mdo.exe` SHA-256：`069fd7f5c1f0485aedafc23d60015d87da5d21459993d837590cc8ffce4affbe`。

新任务入口用本地 HTTP 页面验收：配置按钮仍打开原创建表单并成功创建会话；已有会话写入草稿后点击“新建任务”直接进入空白输入区，原草稿切回后仍在；Ctrl+K 与 `/new` 均不弹表单。390px 手机视口点击侧栏入口后，侧栏收起、输入框聚焦、文档宽度保持 390px。本阶段发布门禁再次通过，打包 SHA-256：`b080ee19bb6235e72f074be859dc598e5425a566ee86e217b214b0ba54c76d55`。

启动设置的本地 HTTP 页面验收使用两个会话：选中列表第二位的 A 后从无 hash URL 进入，正确恢复 A；切换到“新任务”后从同一入口进入空白输入区；切换到“每次询问”后弹窗显示上次标题，继续和新任务两条路径均有效。`#/` 刷新不重复询问，明确指向 B 的 URL 可直接进入 B；390px 手机视口弹窗宽 356px、文档宽 390px，Esc 进入空白任务。API 探针覆盖无状态读取不落盘、选中写入和读回、错误 ID 与不存在的会话不覆盖已存选择。

本阶段发布门禁通过：114 项单元/契约测试、18 个有界运行探针、确定性打包、单文件零旁路写入和短时打包启动检查。生成的 `mdo.exe` SHA-256：`92046ef9503cc0448a603027a17490533c268e582216357a9e797baf1931658d`。

优先发送意图已写入持久队列项，旧 schema 2 队列读取后按 schema 3 升级。真实本地 HTTP 页面在“运行仍活跃、优先项已落盘”的状态打开会话，自动取消首轮、结束无待决工具的中断轮次，并派发优先消息；队列清空且恢复要求解除。首次验收还定位到取消返回与终止事件落账之间的版本竞态，现重新读取 recovery revision 和账本序号并有界重试；复测后无需人工点击“发送下一条”。本阶段发布门禁通过：114 项单元/契约测试、18 个有界运行探针、确定性打包、单文件零旁路写入和短时打包启动检查。生成的 `mdo.exe` SHA-256：`dfa3d3571ecd91eb708e0379d07e82321ece79509cc3a7623045ecc8eab7abe5`。

旧版的 `Ctrl/⌘+J` 主题切换已恢复，经本地 HTTP 页面验证：快捷键将“跟随系统”切到深色、再次切到浅色，配置 revision 更新并读回；设置页存在未预览的改动时只提示处理改动，表单内容和服务端主题均未被覆盖。帮助弹窗显示该快捷键。使用真实慢速 Responses 路径复核引导模式：Enter 取消当前轮次并优先运行输入，Ctrl+Enter 只排队；Esc 停止活动运行，离开设置返回原会话后待发消息继续派发。发布门禁通过：114 项单元/契约测试、18 个有界运行探针、确定性打包、单文件零旁路写入和短时打包启动检查。生成的 `mdo.exe` SHA-256：`8ffece3b0671e1bb10e160c195b8e9a0a74f1b01e2a443d6bcb31fc1cd22b80a`。

询问卡片用真实本地 Responses 工具调用验收：选项和自由文本都能回答，刷新后仍显示待答卡片；390px 手机视口可操作，页面无横向溢出。同一页面验证手机端会话搜索的命中与 Esc 关闭，以及 `@web` 补全列表用方向键、回车插入路径。自由回答曾只按字符限制，400 个汉字会到服务端才因超过 1024 字节失败；现输入时显示字节上限、禁用自由回答提交，清空输入也禁用提交，选项回答仍可用。有效中文回答和超限草稿下的选项回答均经实际模型回合验证。

本阶段发布门禁通过：114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和短时打包启动检查。生成的 `mdo.exe` SHA-256：`8e3d9422d27a6766612cb47c896ea287c0be8d0b9a22384c36391f5db530a3a3`。

会话操作菜单的本地 HTTP 页面验收覆盖了鼠标和键盘入口：方向键移动菜单焦点，Esc 关闭并返回操作按钮，点击搜索框收起菜单并保留搜索焦点，键盘打开重命名弹窗后输入框获焦，取消后返回原操作按钮。390px 手机侧栏里，第一次 Esc 只关闭菜单，第二次才关闭侧栏。置顶引起列表重排后，焦点仍留在该会话的操作按钮；归档使其离开“进行中”列表后，焦点转到剩余会话。发布门禁通过：114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和短时打包启动检查。生成的 `mdo.exe` SHA-256：`f19b9bb3270394ea3d9fa911daaf721f8b5b4b297f33d932393dc9fd5613e4fe`。

新任务项目选择已用两个项目的本地 HTTP 页面验收：从 `alpha` 会话点“新建任务”进入 `alpha` 的空白输入区，可切换回默认项目；刷新后 `alpha` 路由与草稿都恢复；配置创建表单预填当前项目，直接发送的首条消息也实际创建在 `alpha` 会话。手机端从会话新建任务会收起侧栏，进入设置再返回仍保留项目与草稿。390px 视口下项目、模型、思考和权限选项都可见；320px 视口可在选项行内滚动，整页没有横向溢出。发布门禁通过：114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和短时打包启动检查。生成的 `mdo.exe` SHA-256：`d71682439a42be90937c58b2a763922bddaa35c6884f2e463af4172766d6b724`。

侧栏项目分组用默认项目与 `alpha` 的三个会话实际验收：跨项目置顶后会话进入顶部置顶区，键盘焦点跟随原会话；搜索 `alpha` 仅保留匹配项目组；归档后活动组数量更新，在“已归档”筛选中可找到该会话。390px 手机侧栏无横向溢出，点击项目组的新增按钮会关闭侧栏、选中正确项目并聚焦输入；从归档筛选新建任务会回到“进行中”，避免新会话被隐藏。菜单 Esc 的焦点返回在项目与会话复合标识改造后复核通过。发布门禁通过：114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和短时打包启动检查。生成的 `mdo.exe` SHA-256：`451fd396665e77e30e8760622a6456479c3c3a744d1153c8657c7b7404f77b8d`。

计划任务管理用真实本地 HTTP 页面验收：创建一次性计划后列表出现并显示下次时间；读取详情编辑任务内容后保存，再暂停和启用；编辑草稿期间暂停后仍可保存，说明 ETag 已衔接新 revision；删除确认弹层可取消且列表保留计划。390px 手机视口下文档宽度仍为 390px。删除 API 由有界运行探针覆盖；页面最终删除按钮未在本次浏览器验收中触发。发布门禁通过：114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和短时打包启动检查。重新生成的 `mdo.exe` SHA-256：`049282b78dcf5eddfe924a2bd3732345e0464584f6de6b4cd40a5e720fa2f251`。

“立即运行”阶段的真实 HTTP 页面验收确认：暂停计划卡片仍显示可操作按钮，位置与编辑、启停、历史操作一致。xwork 定向测试、xs 扩展覆盖、114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和短时打包启动检查均通过；真实 API 探针验证 `202`、task/run ID、周期游标不变、完成历史和陈旧 ETag 的 `412`。重新生成的 `mdo.exe` SHA-256：`d4e611befaca1e3e298840255b7f5985eef0732c91cc9efe0dd9f92ab3b651d3`。

计划执行历史使用本地真实 HTTP 页面验收：历史弹层显示两次完成记录，最新记录在前，状态、时间、任务 ID、运行 ID 与结果文本均正确；关闭按钮可返回列表。390px 手机视口下弹层宽 356px，文档宽保持 390px。API 探针覆盖空历史零写、32 条上限与 `has_more`、长中文文本在 UTF-8 边界截断。发布门禁通过：114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和短时打包启动检查。重新生成的 `mdo.exe` SHA-256：`7adbea2a445dc759e63dbb0db98575b4724c9de31028b495b233e4ad89ad7b3c`。

记忆入口阶段在项目设置加入全局与项目记忆弹层。真实 xs/TCC HTTP 探针验证了只读访问不创建 Home、项目记忆新建与读取、列表不批量返回正文、缺少或过期 ETag 被拒绝、删除后不可读取；静态模块图与 JavaScript 语法检查通过。Windows 页面自动化在初始化浏览器时因无法可靠识别当前 URL 被 computer-use 停止，因此本阶段尚未声称桌面和移动端的实际点击、焦点与视觉验收通过。发布门禁通过：114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和短时打包启动检查。重新生成的 `mdo.exe` SHA-256：`c10d2b7bb89e49c9ae837f7854fe1c2601b1652c432bdf4e85adf81854220295`。

桌面分栏阶段通过本地真实 HTTP 页面验收：拖动侧栏和任务面板分隔线、键盘调宽、收起和恢复侧栏、刷新后保持宽度；1440px 宽屏显示三栏，1204px 断点与 390px 手机视口均无横向溢出，手机侧栏仍按抽屉操作。顺带复核了上阶段的全局记忆弹层：桌面可打开并聚焦关闭按钮，390px 视口下弹层宽 356px，文档宽保持 390px。API 探针验证布局缺省读取零写、非法宽度拒绝、写入与读回；发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和短时打包启动检查。重新生成的 `mdo.exe` SHA-256：`e5925257731e7a633e4e4cfaba8480378b6a87877842e8b544d96f5e57b79a1e`。

模型管理阶段通过本地真实 HTTP 页面验收：新增只有 Responses 接口的 Provider，新增模型时自动选中该 Provider 与可用协议；模型可设为全局默认、编辑名称，未保存的表单切换列表时保留并提示处理。内置 Ling 的配置只读，仍可选为默认；1018px 时列表与表单纵向排列，1440px 时双栏，390px 手机视口无横向溢出。API 探针覆盖默认配置零写读取、完整模型配置预览和应用、过期 ETag 的 412、内置模型篡改拒绝、默认模型切换及自定义项移除；页面未执行最终删除按钮。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和短时打包启动检查。重新生成的 `mdo.exe` SHA-256：`b3d3a0887f10b118b0fb2ca436d4817040aaddb32a7012770eefe444c23ffbc7`。

设置操作区随后按页面职责收紧：常规、Agent、联网与搜索保留全局“预览 / 应用 / 放弃”操作；模型和其他资源页使用各自的操作入口。模型表单移出全局设置表单，避免表单嵌套和事件串扰。本地 HTTP 页面确认模型页底部无无关按钮，切回常规页后全局操作区仍正常显示。发布门禁再次通过，重新生成的 `mdo.exe` SHA-256：`9e622bd4635bd65980c12e04d120a78c73d88437e2964aff16ff3ed096ebc4eb`。

会话导出恢复旧版 Markdown 操作，同时保留 JSON 快照备份。真实单文件 `mdo.exe` 在隔离目录启动，页面创建任务后可在桌面及 390px 手机视口打开会话导航；更新后的打包页菜单分别触发 Markdown 与 JSON 下载，并显示成功提示。Markdown 转换用独立事件样例验证用户、助手、工具、附件 ID、代码围栏与缺口说明，分页读取用模拟 API 验证导出时刻的事件边界，空会话再经真实 API 验证。此处未以无消息会话声称历史内容导出完整；事件日志本身按容量滚动，故导出文件明确标记不完整情况。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；重建的 `mdo.exe` SHA-256：`b0c43e0957f5580afa87df27908a27d80eae2b016f811f35a08b6b4f327f3c13`。

记忆目录入口用真实单文件打包页验收：项目页直接点击全局及项目按钮后 API 返回成功，对应 `mdo-home/memory` 与 `mdo-home/memory/projects` 目录按需出现；记忆弹窗也提供打开文件夹按钮。打开前的只读页面没有创建记忆目录，GET 被 405 拒绝，`text/plain` POST 被 415 拒绝。390px 视口下项目工具栏换行、文档宽度保持 390px，弹窗宽 356px；新按钮加入后明确将弹窗初始焦点留在“关闭”，最终打包页复核通过。发布门禁再次通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`31acd6c576cb225692cdb76bc4fff53b29f7ddef755626caf87150f896836329`。

图片预览在真实单文件打包页进一步复核：复制旧版验收 Home 到隔离目录后，历史消息、草稿和待发送队列中的图片均可点击放大，Esc 或关闭按钮可退出；390px 手机视口无横向溢出。手机端关闭草稿预览时曾丢失焦点，因为草稿重绘替换了原按钮；现在历史、草稿和队列缩略图都有会话内稳定标识，关闭弹窗时若原按钮已被替换，会定位其新节点。最终打包页的手机草稿预览经 Esc 关闭后焦点回到“查看图片 1”。此阶段没有触发文件选择、粘贴、拖放或移除动作；它们仍保留前述验收范围。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`1886ed6789765d280c2e60715d4b2de17a16c8ea3d17e8811d3a3bf758b7e51a`。

键盘焦点阶段修复草稿图片和待发队列刷新时按钮被替换后焦点丢失。真实单文件打包页载入隔离图片会话，聚焦草稿“移除图片 1”后，页面自动重绘了缩略图，焦点仍在新按钮上；独立本地浏览器回归页使用真实队列模块及两条模拟队列项，聚焦第二条的移除按钮并触发重绘后，焦点仍对应第二条；折叠按钮重绘后也保持焦点。此次没有通过页面执行图片或队列删除。旧版项目配置核对确认只提供默认模型，该项已经迁入；项目 purge 仍需跨会话、记忆和计划的安全事务。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`6732e415a6d93802092cc0ef1c193a9832f933b79b2ccca893aff1a36b16232f`。

待发队列的“发送下一条”和“移除”现共享一次在途操作门槛：异步请求期间即便队列重绘，新生成的操作按钮仍禁用，事件入口也拒绝重复触发；完成或失败后恢复可用。隔离本地浏览器回归页用真实队列模块和 600ms 模拟回调验证：点击重试、立即重绘后，重试及两条移除按钮都禁用，回调计数保持 1；完成后按钮恢复，焦点停在队列折叠按钮。没有在真实会话上执行删除或发送操作。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`1770ea54f2de558f1ac303538f016664aeee3a8bd2911d4355d72c3e87fd39d9`。

对照旧版静态页面与新版单文件打包页，在 1280px 宽度发现旧版首次使用时详情栏收起，而新版默认展开 336px 任务面板，使输入区和模型、思考、权限选项明显拥挤。现把前后端缺省布局都改成收起；打包页无 Home 时首次打开确实只显示聊天和完整输入区，展开按钮可打开任务面板，布局写入 Home 后刷新仍保持展开。已有 `pane-layout.json` 的用户偏好不被改写。API 运行探针验证缺省值与零写读取；发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`b818a94e123a5dba20e7cc5eb3df72b5307d1a2deabd33aa2ecdae4c8a8aab47`。

新任务输入框按旧版 hero 输入区的 52px 最小编辑高度呈现，已有会话仍沿用随内容增高的输入区。单文件打包页在 1280px 桌面视口确认新任务输入框高 52px、整张输入卡高 97px，工具条不被挤压；390px 手机视口下输入框仍高 52px、卡片随两行工具条增至 129px，文档宽度保持 390px，未出现横向溢出。自动增高上限和输入、队列逻辑未改。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`642b5575917a21d44037312cdee4c8afb0631b6795ea78a07bfa953adad74484`。

消息区每次收到事件或反馈更新都会重建 DOM，过去只有思考、工具折叠标题能找回焦点；现在同一会话中的消息复制、编辑、分叉、重试、点赞/点踩、代码复制、链接、工具输入/输出复制以及图片缩略图都按稳定标识恢复焦点。隔离浏览器页用真实时间线模块和 F2 触发的十次重绘验证了这些可操作节点；单文件打包页确认历史消息及图片预览带有相应标识。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`0711f8ab924cfb5c197c678f3b4397388675baa5a7c62041743fbab3cfc0757d`。

分叉、编辑、重试及点赞/点踩现在共享同一会话的在途操作锁。按钮用 `aria-disabled` 表示忙碌，仍保留键盘焦点；点击和键盘事件入口均检查操作锁，时间线重绘生成的按钮也继承忙碌状态。隔离浏览器页用延迟回调验证：分叉开始后全部消息命令标为不可重复操作，重绘后焦点仍在分叉按钮，再按回车不会二次触发；回调完成后按钮恢复。真实单文件打包页确认命令标识与空闲状态且无浏览器脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`9d301b086ec3c7aeec1c4051d034d49d9d714fb762abc551b24d1d401cfc573a`。

新任务首条消息现可使用 `@` 补全所选项目工作区文件。新只读 API 复用会话补全的排序与扫描限额，项目相对路径按会话创建规则解析；输入区切换项目时会取消旧查询并重新查当前项目。API 运行探针在项目尚无会话时验证了候选排序、隐藏目录过滤、HEAD、坏查询和坏项目 ID；单文件打包页实际输入 `@mention` 出现项目文件，按 Enter 插入 `@mention-ready.txt` 后仍停留在新任务页。390px 手机视口同样显示候选，菜单右边界 380px，文档宽度 390px。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`07e675bd3a1d4ee3a2f82dfa73aa901152a348d2b90d258787064a003f97e7da`。

新任务的工作区按钮和上下文面板现在显示所选项目的目录；已有会话继续显示自己的创建时工作区。真实单文件打包页中，`mention-qa` 新任务显示 `workspace` 及完整路径，切到默认项目后恢复“本地工作区”；另建独立 `other-workspace` 会话后从侧栏返回新任务，按钮随即改回项目的 `workspace`，没有遗留旧会话路径。浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`6407be81d4bda636d0b53084dd4436712de03b0cde99a804161fd0470a421467`。

待办、审批和询问卡片的焦点回归使用本地浏览器页 `tests/fixtures/conversation-focus-browser.html`，加载真实模块并模拟状态刷新：待办折叠按钮、聊天区与决策页的审批参数展开项刷新后仍有焦点，展开状态保留；询问草稿保留，待答卡片结束后焦点回到输入框。审批模拟 500ms 提交、列表过期及后续刷新失败，两处入口均只发起一次请求，回车不能重复提交；审批与询问的在途状态按类型隔离，即使数值 ID 相同也不互相阻断。浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`df466e19af8a7560c67899dd918ea086213a63f370f1ac92fb50ad71bcdaa8e4`。

回复中的 Markdown 图片已恢复旧版的直接查看与放大预览。`tests/fixtures/markdown-image-browser.html` 使用真实时间线、Markdown 渲染器和预览模块验证：有效 PNG data URI 生成图片按钮，点击后弹出预览，Esc 关闭回到原按钮，F2 触发时间线重绘后焦点仍在新按钮；`javascript:` 和 SVG data URI 保留为文本，浏览器无脚本错误。远程 HTTP(S) 图片由安全 URL 检查放行，加载时设置 `no-referrer`。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`46b456cdaf0f22125048e869e74fcb40365e24f910d761b7228a840db9288139`。

项目清除的先决清单阶段加入只读预览 API，不在界面提供未完成的删除操作。真实 xs/TCC HTTP 探针创建项目、两条会话、一条项目记忆和一条禁用计划，验证预览的分类计数、ETag 随项目更新变化、HEAD、坏 ID 与不存在的项目；执行测试中的取消注册后，预览返回 404，而保留数据仍由原有项目列表呈现。预览还报告会话运行态、交互运行、全局计划执行数和目录诊断，以便后续事务在独占锁内重新核对。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`29b4b2751cf13124d9445276851a4790784879ad5b5bd9cdc59e4b96511906d7`。

新任务模型选择修复了项目默认模型被输入区显式参数覆盖的问题：模型目录 API 暴露服务端当前全局默认，输入区进入项目时先取项目默认，否则跟随全局默认；手动选择只覆盖该项目在当前页面会话的后续空白任务。Token 面板改为读取当前输入区选中模型，切换项目、手动模型及全局默认更新后立即显示对应上下文上限。隔离浏览器页加载真实输入区模块，逐项验证项目默认 32,000、全局默认从 8,000 更新为 16,000、手动切换及跨项目返回时的显示和选择；真实 xs/TCC API 探针验证模型目录默认字段随配置事务更新。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`8f94f37812387767732cc67d835a365588362436d3bf81c9dd6b2c714923c56b`。

图片输入操作复核发现：默认纯文本模型点击附件会显示能力错误，切到支持图片的模型后错误却仍留在输入区；文件校验失败时选择器也未复位，影响再次选择同一路径。现给能力错误稳定代码，模型切换成功后清除过期提示，并在读取 FileList 后立即复位文件选择器。隔离 xs/TCC HTTP 页面用仅存在于测试副本的图片模型和本地合成 PNG 验证了能力提示、模型切换、文件选择上传、无效类型拒绝、刷新恢复草稿、纯图片发送及历史图片；390px 视口文档宽度为 390px，浏览器无脚本错误。这个页面不是最终打包页，粘贴、拖放、运行中图片队列、取消和移除仍待验收。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`a7c02a63a3f344278f11c0d4a90b9720a253bb23239fb5c1f77ccf53b722ff12`。

图片拖放复核发现新版把混入普通文件的整批文件拒绝，旧版则跳过普通文件并加入有效图片。输入区现逐文件筛选 PNG/JPEG/WebP 和 8 MiB 上限，提示跳过数量后继续保存有效图片；超过四张仍整体拒绝。隔离 xs/TCC 页面以一张 PNG 加一个文本文件的模拟拖放事件复现并验证修复，先前的无效文件提示在有效上传开始时清除，双图片草稿在 390px 下无横向溢出。真实剪贴板图片粘贴、运行中图片排队与自动发送、图片运行即时取消也在隔离页面通过；一次取消后会话要求恢复时，后续文字和图片草稿保持原样。最终 `mdo.exe` 在隔离目录使用外部 HTTP 和仅供验收的图片模型配置，仍从包内读取前端资源；文件选择和剪贴板粘贴各加入一张合成 PNG，手机视口宽度与文档宽度同为 390px，浏览器无脚本错误。此配置覆盖不改变发布内置 Ling，尚未声称原生拖放或打包页图片运行/移除完成。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`04687c310e97fd0c64d14bb0e51e4957ff4cb596efff76a794f92f49f88f68ab`。

中断恢复提示现跟随当前会话的恢复状态，而不必等到下一次发送才出现；停止请求已经终止时立即读取恢复状态。输入区提供“打开恢复决策”入口，状态解除后自动清除提示，草稿和待发送队列保持原样。真实 xs/TCC 页面通过本地慢速 Responses 测试服务复核：停止运行后提示出现，入口打开决策面板并聚焦可执行操作；通过测试 API 结束中断轮次后，提示及决策计数归零，草稿未丢失。恢复状态每秒轮询时入口按钮保持原节点，键盘焦点经 3.5 秒复核仍在按钮上；浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`25653dd6943324a1f9ac1b8d4da266d9fea147759751cfff52a8e958b06cad4b`。

恢复决策面板的定时刷新现保留参数展开状态、决定选项和键盘焦点；目标调用消失时将焦点退回“决策”页签。提交中的按钮保留焦点并用 `aria-disabled` 标示忙碌，点击或回车仍由操作锁阻止重复请求。`tests/fixtures/recovery-focus-browser.html` 加载真实恢复面板模块并每 750ms 模拟状态刷新：参数展开、选项和焦点跨数次轮询不丢；模拟提交挂起期间按回车只产生一次请求，完成后面板清空，浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`08b98f8572711e33123f63820978fe3ab6b83136e2cac78871ad9163e2d0fcfd`。

旧版拖文件到输入卡片会显示虚线边框，新版此前只接收文件而无落点反馈。现文件进入卡片时显示虚线边框及“松开以添加图片”，拖过内部子元素不闪烁，放下、离开或窗口失焦时清除。`tests/fixtures/composer-drag-browser.html` 在真实浏览器中加载生产输入图片模块，用带文件的合成 DragEvent 验证进入、内部离开、放下及非图片校验；CSS 计算结果确认为虚线边框和提示文字，浏览器无脚本错误。浏览器自动化的普通元素拖动不会把 File 项传到目标，因此操作系统原生文件拖放仍未验收。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`c18e39249cda4a4bba57c9241e143807d51297545ae7f63d1047fd0beaec2bce`。

最终打包版的带图运行与即时取消已在隔离目录验收。将当前 `mdo.exe` 复制到只含外部 `xs.json` 和便携 Home 的测试目录；仅在该目录配置 `image-qa` 模型与本地 Responses 测试端点，发布内置 Ling 未改。通过 API 预置合成 PNG 草稿后，打包页发送首条消息，测试端点收到图片 part，历史显示用户缩略图和回复及用量；重新预置同一图片草稿并在第二条运行中点击停止，端点再次收到图片 part，时间线显示“Agent 已停止”并立即给出恢复决策入口。停止后输入的文字草稿刷新仍在，历史两张用户缩略图仍可见，浏览器无脚本错误。此项只覆盖打包页运行与取消，未验证图片移除或原生文件拖放；有界发布门禁及根目录 `mdo.exe` SHA-256 与上一阶段相同：`c18e39249cda4a4bba57c9241e143807d51297545ae7f63d1047fd0beaec2bce`。

移动端抽屉复核发现打开侧栏或任务面板后焦点仍留在背后入口，Tab 会继续走向页面。现打开抽屉时聚焦其操作项，Tab/Shift+Tab 在面板内循环，Esc 关闭后焦点返回入口；1024px 窄屏任务面板关闭时也返回可见的桌面入口；手机端开启另一抽屉时先关闭当前抽屉。顶部按钮与输入区主要操作目标扩至 40px。本地真实 xs/TCC 页面验证了 390px 手机和 1024px 窄屏任务面板的焦点进入、循环与返回，手机侧栏焦点进入及会话切换；320px、390px 视口无横向溢出，浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`5d74b4cc8b83d347c585698308899289bf6db1361116c3ec64815a5aae3c1a3b`。

项目设置新增只读“核对清除范围”弹窗，显示会话、计划、记忆、运行态与诊断计数，并明确清单不授权删除、共享记录不会随单个项目清除；原取消注册操作仍说明保留数据。隔离 xs/TCC 页面以一个已注册项目验证首次清单含 1 条会话，另建会话后刷新为 2 条；刷新按钮持续保留焦点，Esc 关闭后焦点返回原项目按钮。390px 手机视口弹窗宽 356px，页面无横向溢出，浏览器无脚本错误。彻底清除仍需文档所列的并发租约、锚定搬迁和崩溃恢复，未开放执行按钮。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`fe5d97ffb55d6b30b18b8b7cc49b1aa39e821a39d4466501a8b80dadcd6223ad`。

消息操作栏从文字按钮恢复为紧凑图标；复制、编辑、分叉、重试、点赞和点踩保留可访问名称、悬停提示与原有请求逻辑，手机窄屏始终显示 40px 目标。真实时间线模块的浏览器测试页补入完整的持久用户序号及模型用量事件，在 390px 和 320px 下核对全部操作、时间及 token/s；320px 页面无横向溢出，复制图标点击后显示成功提示，F2 重绘后焦点仍在对应按钮，浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`dc4ead1ffecc816bd41acc4347bcc1c5d5febe927caf2bac7f77f126c0e6a91f`。

图片移除时现在等待草稿附件列表保存完成；保存失败会恢复缩略图及原引用，并保持移除期间输入的文字。进行中的移除只阻止所属会话发送或添加图片，切换到其他会话后仍可正常操作；完成后键盘焦点返回下一张图片的移除按钮或附件按钮。`tests/fixtures/composer-remove-browser.html` 在真实浏览器中加载生产模块，验证失败回滚、成功移除、旧会话请求返回后不改新会话，以及草稿存储层首次写入失败后恢复附件且保留文字，浏览器无脚本错误。这是隔离模块测试，没有触发最终打包页的实际图片删除；该项仍待验收。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`4e1f2a97f80e37f050743635f542e7a4f29feb771b99403d01cd1823cb536d7f`。

空白会话示例恢复旧版的四个直接发起任务入口及“建个定时任务”入口。新旧输入路径共用运行与排队逻辑，示例发送不覆盖现有草稿。隔离 xs/TCC 页面接本地 Responses 测试端点验收：输入未发送草稿后点击“分析当前项目”，页面创建会话、时间线出现预设用户消息和测试模型回复；切回新任务仍有原草稿，进入计划页返回及刷新后也保留。计划示例直接进入计划设置页，390px 手机视口五张卡片单列且页面无横向溢出，浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`eba22985cdca2dfcd30f2efc8c53c98e5f659f86522da491c4ca31a8a4821995`。

队列的重试、移除和自动派发锁现在按项目及会话标识隔离；旧会话的后台派发失败会给出通知，不再把错误写进当前会话的输入区。`tests/fixtures/queue-session-lock-browser.html` 在真实浏览器加载队列模块，模拟 A 会话重试挂起时切到 B：A 的按钮保持禁用，B 的重试和移除均可用；随后 A 的派发锁挂起时 B 仍能独立进入派发，旧请求完成后 B 的按钮仍可用，浏览器无脚本错误。此隔离页不触发实际消息删除或发送。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`99e60f851b2ffb9cfb438a20d505b7b11314ddf203b81832e33a331d147a2c23`。

图片上传忙碌状态也按会话隔离。`tests/fixtures/composer-upload-session-browser.html` 使用真实输入模块和模拟附件 API 验证：A 会话上传挂起时 B 可继续上传，B 的草稿只含 B 的图片；A 返回的孤立图片只向 A 的附件 API 发出清理请求，不改 B 的草稿或显示过期错误。新任务创建会话期间的忙碌状态会转移到新会话，直到图片保存结束。浏览器无脚本错误；此测试只模拟 API 响应，不代替最终打包页的实际附件移除和操作系统原生拖放验收。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`b2c45e176485e6fac6c26d5db904309356428c0c7d11f5e4d190d543c75bb501`。

新任务创建和首条发送的在途状态现按项目与会话隔离。会话创建请求返回前切到 B，后台 A 仍可完成，但不得重选 A、清空 B 的草稿或把错误写进 B 的输入区；切到 B 再返回同一个新任务路由，也按导航代数识别为新输入上下文。发送成功时只清除仍与原输入相同的全局新任务草稿，避免覆盖后来写入的草稿。图片入口等待新会话创建时也遵守同一导航边界。隔离 xs/TCC 页面把创建会话请求延迟 2 秒，实际提交 A 后切到 B，再返回新任务输入 C：A 在后台完成，页面保持 C 的路由与草稿，发送按钮恢复可用，浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`7dfcb5defcbca0da86382af53efe0677685012e2c67d0ecd15be66b5a3166965`。

旧版设置中的反馈汇总已迁入新版。新增只读 `/api/v1/feedback` 分页接口，从会话目录逐页读取有效反馈，并把记录关联到项目、会话和原回复事件时间；页面显示赞/踩、标题、时间、读取范围与继续加载入口，可直接打开会话。API 探针验证点赞出现、撤销后消失、HEAD/OPTIONS、合法游标、坏游标、目录代数冲突及无数据读取不创建 Home；隔离 xs/TCC 页面用真实本地模型回复和反馈侧车验证设置页显示一条赞，点击回到对应会话且该回复的点赞状态保持选中。另用 180 字符无空格标题和一条踩记录检查 390px 手机视口，文档宽度保持 390px，未发生横向溢出；浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`6a2f55d27b2f18669da4cab722e95ccf7560f1e0cc51c05f93b0301559b8f013`。

旧版侧栏底部的帮助与主题入口已恢复。帮助按钮复用现有快捷键弹层，Esc 关闭后焦点回到按钮；主题按钮与 `Ctrl/⌘+J` 共用同一个配置事务。隔离 xs/TCC 页面验证按钮可连续切到深色、浅色，手机 390px 侧栏中两个入口均可访问，帮助弹层关闭后焦点仍在侧栏按钮，页面宽度保持 390px，浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`45a71d9f86808889a560d01ff99cb05373eb26d4e6a156d898a6fefbe9baf6e0`。

`/theme` 已恢复旧版直接切换深浅主题的操作，不再只打开外观设置。隔离 xs/TCC 页面从已有会话输入命令两次，依次得到深色和浅色主题；会话路由不变，输入框清空，浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`0ef81687e63c0ec7c9a42acd94d11c9f4949d34b7027bab4f62688f97299fef5`。

模型、思考强度和权限的配置更新锁现按项目与会话隔离。A 的更新请求未返回时切到 B，B 的配置和发送不再被 A 禁用；已有会话详情加载前，配置控件保持禁用，防止误改新任务偏好。`tests/fixtures/composer-profile-session-browser.html` 在真实浏览器中延迟 A 的配置响应，同时更新 B，再让 A 返回，验证 B 的选择和会话状态不被旧响应覆盖；浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`5e0c57ba2d94cef6c482fde1e39c217104ae1fbeeda64d12c6d4f2997ff4b3af`。

同号询问在不同会话间不再共用提交锁或草稿。A 的回答提交挂起时，B 的询问仍可独立回答；切回 A 会显示原草稿和提交中状态。若 A 的请求失败，返回后的卡片会重新开放选项与自由输入，而不丢草稿。`tests/fixtures/ask-session-browser.html` 用真实询问模块和模拟 API 验证了 A 挂起、B 成功、返回 A、A 失败及重试成功的完整序列，浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`4704ba2c2b4d5e80956491868856a81cb0f7c6b3dcdc3b9574d3b58d4841dfa3`。

旧版 `/model` 直接轮换模型的操作已恢复。最终打包页的隔离临时 Home 配置了两个模型，从已有会话连续执行两次 `/model`，输入区和会话标题依次切到 Ling 3.0 Tiny、Image QA；服务端读回 `model_id=image-qa` 和递增 revision，浏览器无脚本错误。仅有一个可用模型或配置控件禁用时显示明确错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`106b40f28ab21845e5486490d1872c4e9c29f11f8cc41c604e64569b4754b8dc`。

会话内卡片的移动端排版已压实。`tests/fixtures/conversation-docks-mobile-browser.html` 使用 180 个连续字符的任务标题和询问选项，修复前 320px 视口的文档宽度达 2592px、最小按钮高度约 25px；修复后 320px 和 390px 视口均无横向溢出，待办、审批、询问按钮至少 40px，浏览器无脚本错误。发布门禁通过 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`4790e54e3d0da734a8abcdc0ef17985c3479d9211b469c7512382af87e09f361`。

补建 [前端操作体验完成审计](frontend-completion-audit.md)，按核心操作链区分打包页实测、模块浏览器夹具、服务端运行探针与尚缺证据；同时记录此前漏列的旧版中/英/俄界面语言切换缺口。此次只更新文档，没有改动应用资源；有界发布门禁再次通过 114 项单元/契约测试、18 个运行探针、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已按正式流程重建，SHA-256 仍为 `4790e54e3d0da734a8abcdc0ef17985c3479d9211b469c7512382af87e09f361`。

新增 `tests/manual_packed_docks_qa.py`，复制当前根目录打包程序到隔离 `.build` 临时 Home，以只监听本机的有界 Responses 模型触发 `TODO UI`、`ASK UI` 和 `APPROVAL UI`。实际打包页验收：待办 1/2 可折叠，刷新后从服务端恢复；询问刷新后仍在，点击 `Fast` 后工具结果进入时间线、Agent 回复并将焦点还给输入框，服务端待答列表归零；审批卡片显示高风险、命令和两个决策按钮，刷新后仍在。审批决策经本地 API 拒绝以结束测试，**没有验收图形按钮的提交行为**。两次浏览器页面均无脚本错误；服务端待办投影保留两项。该夹具是手动浏览器验收工具，不进入发布门禁，也不执行压力测试；本阶段只增测试支持和文档，程序字节未变。

为同一夹具加入四个合成工作区文件。最终打包页验证：已有会话中输入 `@alpha`，下方向键选第二项后 Enter 插入 `@src/alpha-test.c`；输入 `@QA` 插入带引号的 `@"notes/QA notes.txt"`；切到新任务仍能补全 `README.md`，切回后原会话草稿保留。`/he` 显示 `/help` 候选，Enter 打开帮助弹层、Esc 关闭并将焦点还给输入框。会话菜单重命名后侧栏与标题同步，置顶后分组重排；侧栏搜索可过滤再命中，`Ctrl+K` 清掉筛选进入新任务，保留其草稿。320px 打包页的项目新任务补全、抽屉切换、待办与询问提交、会话内搜索均可操作；待办按钮、询问选项和回答输入高度至少 40px，文档宽度为 320px，没有脚本错误。服务端读回新标题、置顶、两项待办和空待答列表。仍未执行审批按钮或附件删除。夹具脚本编译检查通过；根目录 `mdo.exe` 重建后 SHA-256 仍为 `4790e54e3d0da734a8abcdc0ef17985c3479d9211b469c7512382af87e09f361`。

最终打包页的真实双轮对话补验消息操作：用户消息与回复均可复制，首条回复点赞后服务端 `feedback` 记录并在刷新后保持选中；从第一条回复分叉只含第一轮，从第二条回复分叉含两轮，源会话及其反馈保持不变。320px 下消息操作目标均为 40px，文档宽度 320px，页面无脚本错误。发现分叉按钮随原时间线卸载后焦点退到页面根节点，无法立即键入；现将一次性焦点请求绑定目标项目/会话，等新会话加载且输入框可用时才聚焦，若用户已转到其他控件或路由则不抢焦点。新打包页验证点击回复分叉后焦点直接进入新会话输入框，无需再点击即可输入。Node 语法检查、114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`fac2406c65a6966aec3864756dfb7fb8a60cce22929969aea885f89d2d5f03fb`。

侧栏“创建分支”也恢复键盘续写路径。原实现的分支创建和导航发生在模态弹窗关闭前，弹窗关闭后焦点落到页面根节点；现在标题打开时自动选中，提交成功后关闭弹窗，再按目标会话和输入框就绪状态恢复焦点；若用户已选择其他可见控件则不抢焦点。最终打包页使用隔离 Home `.build/mdo-packed-docks-_bmhcoqr` 和本地 Responses 夹具验收：完成一轮模型回复后，在侧栏会话菜单用方向键选择“创建分支”，标题输入初始选区覆盖默认名称，保留到序列 3 并创建独立会话；新页输入框直接聚焦，可以不点击就键入草稿。服务端事件读回源会话和新分支各一轮 `agent_start`，页面无脚本错误。320px 视口再次打开分支弹窗，标题自动聚焦，弹窗宽 286px 且提交按钮完整位于 320×720 视口内；测试后恢复默认视口。Node 语法检查、114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`41eb3801c1f42d2ccf109535be3356391f8eb1ca1867a26b4bf948391de024ef`。

继续用隔离 Home `.build/mdo-packed-docks-67b9lq5n` 的最终打包页核对斜杠命令：已有会话输入 `/ne` 可用 Enter 选择 `/new`，进入空白任务并保持输入焦点；`/export` 在完成一轮本地模型回复后清空命令并显示开始下载提示；`/fork` 打开标题自动选中的分支弹窗，取消后焦点回到输入框；`/clear` 打开永久清空确认弹窗，取消后服务端仍保留原有一轮 `agent_start`。390px 视口再次执行 `/new`，文档宽度与视口同为 390px，输入框保持焦点；手机侧栏的“导出 Markdown”入口也可触发同样提示，页面无脚本错误。内嵌测试浏览器未报告下载事件，且未找到可核对的导出文件，因此这轮只确认 UI 触发，不将文件落地或内容标记为通过；`/clear` 仅验证确认前路径。本阶段只补验收记录，程序字节保持上一提交的 SHA-256：`41eb3801c1f42d2ccf109535be3356391f8eb1ca1867a26b4bf948391de024ef`。

审批按钮补全最终打包页验收。隔离 Home `.build/mdo-packed-docks-3tgm97y5` 用本地 Responses 夹具触发 `exec` 审批，320px 视口文档宽 320px，“拒绝”和“允许一次”触控按钮均高 40px；点击“允许一次”后仅执行夹具内 `print('approval UI fixture')`，工具输出显示 `exit_code: 0` 和预期 stdout，审批卡消失且焦点回到输入框。刷新后展开工具卡仍可读取该输出，服务端 `/api/v1/approvals` 为零项。另在独立 Home `.build/mdo-packed-docks-ib8n1pdr` 触发同一审批并点击“拒绝”，工具卡显示 `tool execution denied by approval policy`，刷新后错误仍在，待审批数同为零；两页无脚本错误。这覆盖允许、拒绝、移动端布局和结果回放，尚未用延迟响应验证提交中防重复。只补验收记录，根目录程序 SHA-256 仍为 `41eb3801c1f42d2ccf109535be3356391f8eb1ca1867a26b4bf948391de024ef`。

消息操作的打包实测增加可重放的 Markdown 回复。`tests/manual_packed_docks_qa.py` 在输入 `MARKDOWN UI` 时返回代码块和安全链接；隔离 Home `.build/mdo-packed-docks-88r9h97m` 中，代码复制按钮把 `int answer(void) { return 42; }` 写入浏览器剪贴板，链接实际属性为 HTTPS、`target=_blank`、`rel=noopener noreferrer`，点踩后服务端反馈侧车记录 `event_id=4,value=bad`，刷新后仍选中。编辑按钮打开含原消息的模态弹窗并聚焦文本，取消后焦点回到原按钮。发现 320px 编辑弹窗底部两个按钮仅高 35px；现在将手机视口下所有标准弹窗底部操作按钮提高到 40px。新打包 Home `.build/mdo-packed-docks-5o_yslw1` 复测编辑弹窗和会话分支弹窗，取消、确认按钮均高 40px，文档宽度为 320px，页面无脚本错误。编辑与重试的实际提交会截断测试会话历史，等待图形操作的当次确认；本轮未声称它们的回放已通过。夹具 Python 语法检查、114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`f1adc5abcdc1f02baadd7cc47966fd2c391b55521bf2f7d1744ca78864d12e82`。

手机排队卡和代码块按钮补齐触控目标。此前 320px 打包页的队列折叠、发送下一条、移除按钮分别约高 26px、22px、22px，代码复制按钮约高 20px；现在移动端均至少 40px。手动夹具增加单次 15 秒的本地模型延迟，仅用于稳定捕获运行中队列，不增加请求负载。最终打包 Home `.build/mdo-packed-docks-h83oc2vi` 中，在首条 `SLOW UI` 运行时将 `QUEUED UI` 排入队列，320px 下三个队列按钮实测均高 40px、文档宽度 320px；折叠后 `aria-expanded=false`、列表隐藏、焦点仍在折叠按钮。首条结束后第二条自动发送，服务端队列零项且事件中有两轮 `agent_start`，页面无脚本错误。前一版打包 Home `.build/mdo-packed-docks-kkvnypf6` 已确认代码复制按钮高 40px；最终 CSS 变更只补队列折叠按钮的优先级。仍需核对优先发送、移除和恢复决策。本阶段 Python 语法检查、114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`a12eff0b98ac64f49a354838910cf04f234fca2ef92f9e8f1a37d88c460cf9f8`。

新任务配置链的打包验收发现首次发送竞态：空白任务创建会话后，路由载入与提交动作各自读取恢复状态，随即启动运行；服务端恢复读取与运行启动都独占同一会话运行时，导致首次提交偶发 `recovery_state_conflict`，留下未发送的草稿，第二次才成功。前端现按项目/会话串行化恢复读取、恢复决策、运行启动及配置更新；不同会话仍可并行，失败也会释放队列。`tests/test_frontend_session_runtime.mjs` 验证了新任务导航读取在前、运行启动在后，以及失败释放和跨会话独立执行。重新打包后的隔离 Home `.build/mdo-packed-docks-grx4108j` 从空白任务选择 Ling 3.0 Tiny、高思考、只读权限，输入 `PROFILE UI：请检查当前配置。` 时显示约 11 tokens；**第一次**点击发送即创建会话并获得本地模型回复，没有错误提示。服务端读取 `reasoning_effort=high`、`permission_profile=read-only`、`runs_started=1`、`runs_completed=1`、`runs_failed=0`，账本含完整 `agent_start` 到 `agent_done`。界面显示 7 输入 / 3 输出 tokens，刷新后配置和回复保持；320px 下三个配置选项、用量及发送按钮均高 40px，文档宽 320px，无脚本错误。Node 并发测试 2 项通过；有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒打包启动检查；根目录 `mdo.exe` SHA-256：`d3e7941d1bf91aa9bc9cabbfd9c4693c1c22d8591ed7d4836f215fe010df960f`。

同一打包 Home 中继续验证优先发送。已有会话发送 `SLOW UI`，本地模型的首个慢速响应保持 15 秒；运行中输入 `PRIORITY UI` 并按 Ctrl+Enter 后，页面先显示被中断的回合，随后自动发送优先消息并显示固定回复，输入框恢复焦点。服务端记录总共三轮 `agent_start`（含前一轮配置测试）、三轮完成记录，其中慢速运行状态为 `cancelled`、优先运行状态为 `succeeded`；待发送队列空，恢复状态 `resume_required=false`，页面无脚本错误。此轮只增加打包操作证据，程序内容及 SHA-256 与上一提交一致；移除队列项、手动恢复决策和跨会话切换仍待打包验收。

打包 Home `.build/mdo-packed-docks-24f1pyty` 补验手动恢复入口和会话切换。发送 `SLOW UI` 后，在 15 秒本地模型等待期输入 `AFTER RECOVERY UI` 并停止运行；页面提示上轮尚未恢复，点击“打开恢复决策”打开右侧决策页并聚焦“继续恢复会话”。该会话没有待决工具调用；点击继续后服务端接受恢复请求，时间线出现恢复标记，最终恢复状态 `resume_required=false`。刷新和切换到空白任务、再返回后，原草稿保持 `AFTER RECOVERY UI`，空白任务自身的 `NEW TASK DRAFT` 也保持独立。恢复后的本地固定模型回复连续三次只返回文本，没有执行 xwork 的工作区验证要求，运行以 `XWORK_ERROR_LOOP_GUARD` 失败；这是测试模型未遵循恢复时的验证提示，本轮只证明决策提交、状态清除与草稿隔离，**不声称恢复运行成功**。页面无脚本错误。程序内容未改，根目录 `mdo.exe` SHA-256 仍为 `d3e7941d1bf91aa9bc9cabbfd9c4693c1c22d8591ed7d4836f215fe010df960f`。

手机侧栏与设置操作补验。隔离打包 Home `.build/mdo-packed-docks-s481w0bj` 在 390px 先完成一轮 `ARCHIVE UI` 回复，再从会话菜单归档；当前页变为只读，筛选“已归档”可找到同一会话，点击“移回进行中”后输入恢复可用，Home 中 `meta.json` 为 `status=active, revision=3`，五条会话事件仍在。测量发现侧栏菜单入口 28px、菜单项 32px、搜索输入 22px、状态筛选 18px、项目新任务按钮 22px；移动端现统一至少 40px。新的打包 Home `.build/mdo-packed-docks-r1jglmp2` 在 390px 和 320px 复测这些控件均高 40px，菜单处于视口内，文档宽度等于视口宽度。设置分类、表单选项和底部操作按钮原先约 35–36px，也已提高到至少 40px；Home `.build/mdo-packed-docks-62skt5o8` 的 390px 设置页完成深色主题预览和应用，配置 revision 从 1 升到 2，刷新后主题和值保持。

设置页刷新会丢失内存里的来源会话，导致“返回 Agent 工作区”落到空白任务。现在进入设置时把来源项目和会话记录在该浏览器历史条目中，刷新后验证 ID 再恢复。最终打包 Home `.build/mdo-packed-docks-nk022ile` 在 390px 从已有会话进入设置、刷新、返回，URL 精确回到原会话；从 `default` 新任务进入设置、刷新、返回，则精确回到 `#/projects/default/new`。320px 设置分类、选项和操作按钮仍至少 40px，无横向溢出或浏览器脚本错误。Node 语法检查、有界发布门禁的 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`ca52399c6bcbfe467e8c592bd8303d98dda3d0be8cd102a217fcaca8268956eb`。

旧版会话标题旁的一键导出已迁回桌面会话栏与手机顶栏。两处按钮共用 Ctrl+E 的当前会话导出路径，仅当路由与载入的会话 ID 一致时可用，切换会话期间和新任务页禁用，避免拿旧会话数据导错文件。先前 `/export` 没有 Playwright 下载事件不能作为下载失败的证据；隔离打包 Home `.build/mdo-packed-docks-renpgkql` 中，调试协议显示 `/export` 与 Ctrl+E 都触发 `mdo-Packed docks QA.md` 下载。新打包 Home `.build/mdo-packed-docks-emvdm9dm` 发送 `MARKDOWN UI` 得到含标题、链接与代码块的一轮回复后，桌面和 320px 手机按钮都触发同名 Markdown 下载，浏览器进度事件分别确认 281/281 字节且状态为 `completed`；手机按钮高 40px，文档宽 320px，新任务页按钮禁用，页面无脚本错误。仍需直接核对下载文件的文本内容，不把事件完成等同于内容正确。Node 语法检查及有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查；根目录 `mdo.exe` SHA-256：`5e567c8bc4570190c6f8da37ff683e47f68aa55bd92d74ff7e362cbd8c4de987`。

旧版会话标题旁的“更多”操作现接回桌面会话栏和手机顶栏。菜单动作由一个描述模块供侧栏与标题栏共用，按会话进行中、归档、回收站状态显示对应选项；按钮只对与当前路由一致的已载入会话启用。独立的菜单控制器处理方向键、Home/End、Esc、失焦关闭和动作后的焦点返回。隔离单文件 Home `.build/mdo-packed-docks-ge7xu9zr` 中，桌面页按下 ArrowDown 直接聚焦“重命名”，End 聚焦末项，Esc 关闭并回到“更多”；从该菜单置顶后刷新仍显示“取消置顶”，再经同一菜单重命名，侧栏、标题同步更新，`meta.json` 记录 `title=Header menu QA,pinned=true,revision=3`。320px 页顶按钮高 40px，菜单边界 `x=92..272` 位于 320px 视口内，文档无横向溢出；新任务页按钮禁用，浏览器无脚本错误。静态契约测试已随共用动作模块更新，避免继续把动作字面量错误地固定在侧栏文件中。114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`0f5b78fdf249b9936dd49a9734fc2dc13b76713fc0d992bc2b7a3d26da0704d0`。

审批双入口补上共享一次性提交状态。此前对话卡片和右侧决策面板各自维护 `deciding` 集合；隔离代理延迟审批响应 2 秒时，从卡片点“允许一次”后立刻从右侧再点，代理实际收到两个 `PUT`，虽然服务端最终只执行一次工具。现在共享审批状态位于 `state/approvals.js`，提交中和已提交但刷新未确认两种状态同步通知两处 UI；第二次操作在前端入口被拒绝，服务端刷新失败时继续锁住已提交的请求，直到成功读回待审批列表。`tests/test_frontend_approval_decisions.mjs` 验证并发去重和刷新失败后的锁保持。最终单文件 Home `.build/mdo-packed-docks-ko8hhm3x` 用 `python tests/manual_packed_docks_qa.py --approval-delay-ms 5000` 触发本地无害 `print('approval UI fixture')`：首次点击后两处按钮同时呈不可重复状态；在右侧按钮上再作一次物理点击，代理总计只有一个审批 `PUT`，工具输出 `exit_code: 0` 且只有一条执行结果，API 待读审批 `total=0`，页面无脚本错误。Node 两项并发测试、Python 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`9c4a5384d97b8996ea61b138278ffe131262e6f6ec4b08836fad546a5daf4b6f`。

常规设置重新提供旧版的中、英、俄语言选择。选择时立即预览，设置 patch 的 `locale` 随预览及 ETag 应用事务提交，放弃更改会恢复已保存语言；初始化从服务端设置读取 locale，刷新不回到中文。三语词典从 114 键扩至每种 172 键，覆盖常规设置分类、选项、提交反馈及本地服务状态。俄语较长按钮一度把 320px 设置网格撑到 493px；给网格列和操作栏设置可收缩宽度后，在最终单文件 Home `.build/mdo-packed-docks-_0nuvtfm` 中，320px 俄语页面宽度和文档宽度同为 320px，语言选择与底部可见操作按钮至少 40px。俄语预览、应用后 revision 1→2、刷新保留、便携 Home `config/settings.json` 写入 `locale: ru-RU`；再预览英语并放弃则回到俄语，浏览器无脚本错误。其他设置分区和消息/任务动态文案仍需继续翻译，不能据此宣布全量国际化完成。Node 词典/语法检查、Python 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`058c1e9f288db64c757d03e8083ed8a2fd7dfdef52b3e72e600e33dc5a368c8e`。

时间线渲染恢复旧版按消息保留未变化卡片的行为。此前每次事件刷新都会清空并重建整列 DOM，打断旧消息的文本选择与焦点；现按消息键和内容签名只替换变化的卡片，保留旧节点与折叠状态，变化中卡片被替换时仍恢复相同操作按钮的焦点。`tests/fixtures/timeline-reconcile-browser.html` 的真实浏览器验证了流式增量后旧卡片节点稳定、旧文本选择仍为 `First prompt`、早期反馈只更新对应卡片、变化中回复的复制按钮焦点恢复。最终单文件 Home `.build/mdo-packed-docks-7u7zzlxf` 连续发送两轮本地回复，刷新后四张消息卡片、Markdown、时间与 token/s 均可回放；320px 文档宽度等于视口，14 个消息操作按钮均为 40px，浏览器无脚本错误。编辑和重试的提交验收仍按审计清单保留。Node 语法与前端契约、Python 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`cf78fd0f60ca6d78f7717e8c07b970208e298f32fb0b88631883dfeb14dd41ee`。

时间线消息操作与 Markdown 代码块现在跟随已保存的中、英、俄语言设置。角色、事件回退说明、折叠状态、图片可访问名称、复制/编辑/分叉/重试/反馈、用量、代码复制和完整时间提示都从词典读取；语言改变时有意刷新时间线节点，避免旧语言标签被节点复用缓存保留。三语词典从每种 172 键扩至 235 键，静态测试检查时间线与 Markdown 的键及占位参数。最终单文件 Home `.build/mdo-packed-docks-okihit8i` 发送 `MARKDOWN UI` 后，将设置应用为英语再应用为俄语，已有消息随之切换；俄语刷新后角色、按钮、代码块与本地化时间仍正确，代码复制实际得到 `int answer(void) { return 42; }`。320px 文档宽度等于视口，八个消息与代码操作按钮均高 40px，浏览器无脚本错误。队列、待办/询问、其他设置分区及部分外壳提示仍需迁移。Node 词典/语法检查、Python 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`c6364376859c653b00d3af8bbd133682c2245dbfcbb9ee00c5a49382baacec07`。

对话停靠卡片与队列接入现有语言切换。队列计数、发送/重试、未确认提示、移除、图片名称，待办、后台任务、询问和审批卡片的按钮、状态、风险等级、资源类型及无障碍名称均从三语词典读取，词典每种增至 290 键。询问卡片复用 DOM 时就地更新标题、输入提示、提交按钮和校验状态，不丢失已有草稿与焦点；队列在语言切换时重新渲染并恢复对应操作焦点。隔离单文件 Home `.build/mdo-packed-docks-g84f0u56` 中，`ASK UI` 待回答时输入草稿并切换、应用英语，返回后草稿仍在；加入排队消息显示英语计数及操作，再切俄语后询问与队列同步切换，320px 页面宽度为 320px，询问和队列按钮均至少 40px，浏览器无脚本错误。同一会话选择询问选项后，排队消息自动发送；`TODO UI` 显示俄语计划 1/2，`APPROVAL UI` 显示俄语决策及影响说明。最终重新打包的 Home `.build/mdo-packed-docks-v7qi6xah` 核对了审批风险“高风险”和资源“命令”的中文翻译，320px 参数展开区、允许/拒绝按钮均为 40px，文档无横向溢出，浏览器无脚本错误。Node 词典/语法检查、Python 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`b332aa36354eab4d6312e18c9b2cfa236b70a496ce039204f4cd82f5622038e2`。其他设置分区、输入框外壳帮助文字和部分服务端提示仍需迁移；队列项移除与编辑/重试的图形提交仍按审计清单等待当次确认。

继续补齐常用工作区的混合语言提示：发送区域、输入标签、附件格式提示、导出快捷键、任务面板切换、会话内搜索及回到底部均接入三语词典，搜索结果计数也会随语言即时更新。设置页语言预览一度暴露了标题回归：进入设置时路由不带会话 ID，语言回调误将已选会话标题改为“新任务”；现依据实际保留的会话键更新标题和归档状态文案，新任务才使用项目提示。词典每种增至 305 键。最终单文件 Home `.build/mdo-packed-docks-jkapgzf_` 从已有会话进入常规设置，应用英语后返回，标题仍是 `Packed docks QA`；发送区、附件格式与导出提示显示英语，发送 `MARKDOWN UI` 后会话内搜索可显示 `2 matches · loaded messages`。320px 页面及文档同宽 320px，浏览器无脚本错误。欢迎区示例及其他设置/服务端提示仍有中文，列在后续迁移。Node 词典/语法检查、Python 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`23887a35865bcd2a68c448dff48e38e32a56f34e7220d18009ad3b02af57fa89`。

欢迎区示例与输入候选现在完整跟随界面语言：四个示例不仅更换可见标题，还在点击时按当前语言生成实际提交的提示，定时任务示例仍打开计划设置；斜杠菜单说明和文件补全的可访问名称也随语言切换。无匹配斜杠命令时清除过期的 `aria-activedescendant`，避免输入框指向已移除选项。三语词典每种 331 键，词典测试核对示例提示和命令描述键。隔离单文件 Home `.build/mdo-packed-docks-p1ucjni2` 应用英语后，欢迎区五个示例均显示英语，斜杠菜单显示英文说明；点击 “Introduce yourself” 后，时间线中的用户消息实际为英文提示，而非旧的中文字符串。切到俄语后，新任务页五个示例及 `@alpha` 文件补全名称显示俄语；320px 下五个示例触控高度 69–88px、页面无横向溢出，浏览器无脚本错误。Node 词典/语法检查、Python 114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`f9a2a0d24508393ed45417189a7a50ca9c0d60ceb8ca8a6bed2a8b3fd034690f`。

补上语言包尚未载入时的启动回退。`main.js` 为保证单文件工作区及时启动，会并行加载内置中文词典；此前极早点击示例可能把 `welcome.*.prompt` 键名作为消息发送。四个示例现同时保留原中文提示作为 `t()` 回退，斜杠候选和文件补全名称也有内置中文回退。Node 词典测试检查每个示例的回退文本，且在未加载词典时验证 `t()` 使用回退。变更后有界发布门禁再次通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查；根目录 `mdo.exe` SHA-256：`ec5ff37fa807b558de029e01b9ee92f969cced221f136a2d6477b2f9c6ca0a10`。

附件操作继续按旧版的直接操作方式完善。缩略图移除按钮此前只有 19×19px，在移动端难以准确触按；320px 等窄屏下现在扩大到 40×40px，保留小圆形视觉标记，并添加可见键盘焦点轮廓。预览/移除名称、上传/移除状态、格式与数量错误、拖放提示接入中英俄词典，三语词典各 346 键；语言切换重绘附件条并恢复原操作焦点。`tests/fixtures/composer-image-a11y-browser.html` 使用模拟缩略图、无删除动作，浏览器在 320px 验证按钮 40×40px、英俄文案、切换后焦点和无横向溢出；Node 词典检查包含新模块。打包页实际移除和原生系统文件拖放仍按完成审计保留，不以这个夹具代替端到端验收。同期核对旧版系统提示词、代理/CA 与防休眠设置：新架构尚无 mdo 运行时接线，继续列为后续设置迁移，不添加无效开关。有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查；根目录 `mdo.exe` SHA-256：`e723214c1539de6883e1ee59bea65fc46f73cb9e55ab258f5d77ef802efc3af4`。

修复消息操作在异步请求期间切换会话的竞态。此前编辑/重试在截断旧会话后仍无条件改写当前详情、待办、输入草稿和运行指示；现在消息按钮把来源项目/会话传给回调，截断前验证原路由，截断成功后只在原路由仍可见时更新界面。已提交的旧会话操作会在后台完成；若重新发送失败，文本与图片回到旧会话草稿，而不会进入刚切换的新会话。编辑弹窗提交前复核来源，分叉在切走后不会强制把用户导航回来，反馈也只发送到按钮所属会话。操作完成后若原消息按钮因时间线刷新消失，焦点交给输入框，但不抢用户新选的控件。`tests/test_message_replacement.mjs` 用有界延迟验证截断前/中切换、正常发送和后台失败草稿回收；浏览器夹具 `tests/fixtures/timeline-owner-browser.html` 验证四类按钮回调的来源身份。新打包隔离 Home `.build/mdo-packed-docks-oau9zpjm` 的服务直接返回新模块和调用它的 `app.js`，浏览器打开会话页无脚本错误。编辑/重试的真实打包页提交仍待按图形工具的删除确认策略验收，不以夹具或页面加载代替最终回放证据。Node 五项前端测试、有界发布门禁的 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`bd5973bab5ae31557ae913c6f4833cbdc5546a508201df1cdb4b0fc388d0dd47`。

旧版“系统提示词”设置以自定义系统指令形式恢复。`settings.agent.user_instructions` 默认为空，服务端按 UTF-8 有效性、NUL 和 8192 字节上限校验；设置页显示实时字节计数，超限时给出错误并禁用预览。新会话在模块与 Skill 系统提示词之后加入该指令，并继续固定当时的记忆；恢复会话通过 xllm-session 新增的只读 PINNED 提示词访问器复制原账本快照，后续清空或截断到零仍使用原指令，不会被最新全局设置改写。xserver 配套提交为 `ca2c8c23b42f`，mdo 依赖锁同步更新。旧数据迁移保留 `migration/legacy-system-prompt.txt` 供审阅和手动导入，不自动激活。

`tests/test_agent_runtime.py` 验证模型实际收到指令、配置变更后的新会话、旧会话恢复/清空/截断；`tests/test_api_runtime.py` 验证默认回读、预览拒绝超长中文指令、应用和落盘。最终打包程序在隔离 Home `.build/mdo-packed-docks-g5f0t29w` 的设置页验证：8193 字节输入显示错误且预览禁用；45 字节输入预览、应用后 revision 递增，刷新仍保持原文。桌面设置页目视布局正常；手机设置页此项仍待专项复核。Node 五项前端测试、有界发布门禁的 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`1937ae9346386acbc76f42b0358a41ecedbfdcdcbcecd5e6be2fba4c16c021d6`。

继续恢复旧版三语设置体验。常规设置原有三语基础上，Agent 和联网两个常用分区的标题、模式、推理强度、并行数量、开关、网络限额与搜索地址均接入中英俄词典，三套词典各 394 键；凭据状态改为准确说明“已配置引用”，不把尚未解析的环境变量或 Home 文件误报为凭据可用。设置页的动态加载、恢复默认与凭据状态也随语言更新。修复预览语言后切换分区时顶栏标题被写回中文的问题，资源分区标题亦按当前语言更新；异步语言包完成时不会把尚未应用的表单改动误报为已同步。

隔离打包 Home `.build/mdo-packed-docks-rraxsu9n` 与 `.build/mdo-packed-docks-xwkbw2wy` 验证：从中文预览英语、俄语后，Agent/联网标签、选项文案与凭据状态均随语言切换；跨分区往返保留未应用的系统指令输入；最终打包页的 Settings、Projects 顶栏保持对应语言，联网凭据引用状态不再暗示实际值可用。浏览器测试仅预览语言，未写入测试配置。Node 五项前端测试、114 项单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查通过；根目录 `mdo.exe` SHA-256：`fd07ad96eaff33b1bc900c124d07876d40678d6c5cb7816daacbe4a0aac3a683`。资源设置分区仍有中文固定文案，保留在完成审计中。

消息编辑与重试的可见操作继续恢复三语一致性。编辑弹窗的标题、撤回范围说明、输入标签及操作按钮改为随语言更新；编辑/重试的会话边界、运行中与草稿冲突提示及成功反馈使用同一词典，三语词典各增至 411 键。修正空白输入校验顺序：纯空格编辑现在显示当前语言的必填提示，停留在弹窗，不触发历史截断。隔离打包 Home `.build/mdo-packed-docks-hjalpe44` 发送一轮合成消息后，在英语、俄语预览下打开编辑弹窗，原文载入且焦点落在文本框；英语空格输入显示 `Enter a message`，取消后原用户/Agent 两张卡仍在且焦点回到编辑按钮。320px 俄语弹窗完整位于视口内，文档宽度 320px，取消和重发按钮均高 43px；浏览器无脚本错误。此轮没有图形提交编辑/重试，仍需按完成审计核对真正截断与重发后的打包回放。

Node 词典、消息替换与会话运行测试 7 项通过，`app.js` 和两个消息模块语法检查通过；有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查。根目录 `mdo.exe` SHA-256：`24013fc0b8985250a471f017c26d9ad85ae84de61a57a0858a13d2cc841de01c`。

修复历史消息编辑入口的迟到拒绝。旧打包 Home `.build/mdo-packed-docks-23xhynxa` 中，输入未发送草稿后点“编辑”仍打开弹窗，只有提交时才会报草稿与队列冲突。现在打开弹窗前复用与实际重发相同的会话、运行、上传、草稿和队列前置检查；提交时再次检查，以处理弹窗打开后状态变化。新打包 Home `.build/mdo-packed-docks-wgozu98j` 中，保留 `Unsaved synthetic draft` 时点击编辑立即显示冲突提示，弹窗不打开且草稿不变；切到干净的新会话完成一轮合成回复后，编辑入口仍打开原文并聚焦文本框，取消后焦点返回原按钮。浏览器没有脚本错误。实际提交编辑/重试仍按完成审计保留，不把入口验证写作截断回放通过。

Node 消息替换、会话运行与词典测试 7 项通过，`app.js` 语法检查通过；有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256：`b119f3de769c1c8937fee468a208f503e91b6662d7070eddd59f50c87a8b00a7`。

修复运行中 Esc 与输入候选的快捷键冲突。原来 `@` 文件候选和斜杠候选仅阻止默认按键，Esc 仍传到全局运行控制；同时桌面常驻侧栏被误判为临时抽屉，裸 Esc 会被抽屉分支吞掉。现在候选消费 Esc 并阻止传播，全局快捷键尊重已处理的事件；抽屉判断只计入移动端侧栏和非宽屏任务面板。修复前的 320px 隔离 Home `.build/mdo-packed-docks-yotozk_e` 在 `SLOW UI` 运行中打开 `@alpha` 后按 Esc，立即出现“Agent 已停止”。修复后的 Home `.build/mdo-packed-docks-2sk55r70` 同样按键只关闭候选，`@alpha` 草稿保留，15 秒本地回复完成，服务端记录 `state=succeeded, cancel_requested=false`。桌面 Home `.build/mdo-packed-docks-ttyvgp42` 在常驻侧栏打开时按裸 Esc，服务端记录 `state=cancelled, cancel_requested=true`。两个修复后的页面均无浏览器脚本错误。

Node 快捷键所有权、词典与会话运行测试 4 项通过，四个改动脚本语法检查通过；有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256：`b637716e8a51ba8ba7f0790a0fd24cb823f1de3fd9ac4daefb8bf4b64fa9bb1e`。

恢复旧版输入候选的 Tab 选中。新版先前只接受 Enter：隔离打包 Home `.build/mdo-packed-docks-kso1dqk6` 中，`@alpha` 和 `/he` 候选打开后按 Tab 都转焦到附件按钮，未插入文件也未执行命令。现在无 Shift/Ctrl/⌘/Alt 的 Tab 与 Enter 一样接受当前候选，输入法组合时不处理；Shift+Tab 仍可做反向焦点导航。最终单文件 Home `.build/mdo-packed-docks-lkb8cl9s` 验证桌面 `@alpha` 变成 `@src/alpha.c ` 且输入框保留焦点，`/he` 通过 Tab 打开快捷键帮助，关闭后焦点回到输入框。320px 下 `@QA` 通过 Tab 变成 `@"notes/QA notes.txt" `，输入框保留焦点，页面宽度与视口同为 320px，浏览器无脚本错误。

两个补全模块的 Node 语法检查和四项词典/会话运行/快捷键测试通过；有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` SHA-256：`1cc92a930461604bf880ae873ed51d4dc58f88fc2415de7620f6bb406b474fbe`。

修复运行结束与队列写入交错时的派发延迟：第二条消息在运行中提交，但队列 POST 尚未返回时首轮已经结束，终态回调可能看到空队列；旧发送路径在写入成功后没有再次派发，只能等待运行列表的后续刷新。现在队列写入成功、草稿清理与优先级取消处理之后，对当前会话立即再检查一次派发；首轮仍运行时原有终态回调继续负责接力，队列锁防止重复启动。为打包页夹具增加有界的队列 POST 延迟和首轮模型延迟参数。旧打包 Home `.build/mdo-packed-docks-9ydssf_j` 用 2 秒模型延迟、10 秒队列延迟观察到第二轮在首轮完成约 9 秒后启动；修复后 Home `.build/mdo-packed-docks-ext5dmk2` 用 2 秒模型延迟、6 秒队列延迟，第二轮在首轮开始约 6.4 秒后启动，服务端恰好两次 `agent_start`、队列归零，浏览器无脚本错误。该夹具只有一个运行与一条排队消息，不涉及压力测试。

`app.js` 语法检查、夹具 Python 编译和 2 项前端会话运行测试通过；有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` SHA-256：`6a2cc95a97295728ca5b4525e00329254637bafc97998bcdf241c6f4d55bfa68`。

资源设置页继续消除混合语言：扩展、权限、诊断与旧数据迁移的动态卡片改用三语词典，并在语言预览时重绘；权限布尔值和询问状态、Skill 来源、便携存储模式与 MCP 状态显示为当前语言的可读标签。相应的三个固定标题与说明也接入静态词典。最终打包 Home `.build/mdo-packed-docks-iq9ga80b` 实测英语扩展页的模块数量、空 MCP 状态，俄语诊断页的存储、迁移来源与诊断状态，以及俄语权限卡的允许、禁止、询问，浏览器无脚本错误。390px 诊断页原“重新检测”仅高 29px，资源按钮移动端规则现统一至少 40px；320px 下扩展区刷新、重编译、重载配置三个按钮和诊断区重新检测均高 40px，320px 与 390px 文档宽度分别等于视口宽度。模型编辑、项目、计划、反馈分区及部分服务端描述尚未完成全量本地化。

三语词典各 483 键，Node 词典与语法检查通过；有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256：`792b231f298fcdd85a0cb0ca13c16672bf7b0c174f44da8294593b5b8cb63232`。

补验手机端自定义系统指令与设置抽屉状态。隔离单文件 Home `.build/mdo-packed-docks-y3euf5y_` 中，桌面任务面板展开后进入设置并切到 390px，任务面板关闭且带 `inert`、位于视口外，遮罩不显示。Agent 设置输入 2731 个汉字得到 8193 / 8192 字节提示和禁用的预览按钮；换成 29 字节合成指令后预览、应用，配置 revision 升到 2，刷新与服务端设置 API 均保持原文。390px 和 320px 页面没有横向溢出，320px 预览与应用按钮高 43px，浏览器无脚本错误。本阶段仅补验收文档，没有应用代码变更；根目录 `mdo.exe` 按正式命令重建，SHA-256 仍为 `792b231f298fcdd85a0cb0ca13c16672bf7b0c174f44da8294593b5b8cb63232`，沿用该相同字节程序上一阶段已通过的有界发布门禁。

项目设置页的手机触控目标补齐。修复前的 320px 单文件 Home `.build/mdo-packed-docks-2b6rip0k` 实测顶部添加/刷新/记忆按钮高 35px，项目卡片操作高 32px。修复后的 Home `.build/mdo-packed-docks-2reluo6j` 中这些按钮均高 40px；再在隔离 Home 新建一个 `Mobile QA` 项目，已注册项目卡片的操作和默认模型选择框也高 40px。长工作区路径可换行，文档宽度仍为 320px，浏览器无脚本错误。只修改 760px 以下项目页控件的最小高度，不改变桌面排版和项目数据流程。

有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查；根目录 `mdo.exe` 已重新生成，SHA-256：`de86508b858e7bde6e08ff3f7edb4b743bc67aa7a1efc80469c75e6def6e1415`。

计划任务管理页补齐手机触控目标。修复前的 320px 单文件 Home `.build/mdo-packed-docks-a9qigtgh` 中刷新、新建、创建按钮高 35px，普通输入/选择框高 36px，“高级选项”可点区域仅高 17px。现在只在 760px 以下提高计划页按钮、普通表单控件、高级选项和每周运行日标签的最小高度，周选择框调到 18px；桌面排版和计划数据流程不变。修复后的隔离 Home `.build/mdo-packed-docks-oi9fk40i` 实测上述目标均至少 40px。手机端创建 `Mobile QA` 一次性计划后，卡片编辑/暂停/立即运行/历史/删除五个按钮各高 40px；历史弹层关闭按钮高 40px，编辑入口聚焦名称并载入原内容，刷新后计划仍在。周一勾选和高级选项展开正常，文档宽度为 320px，浏览器无脚本错误。仅验证删除按钮的触控尺寸，没有在图形界面提交删除。

有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查；根目录 `mdo.exe` 已重新生成，SHA-256：`aa579744621b2cbd0546f0c02d636ba4e5a159281e9dc96a0e8daf3c20ddad8b`。

计划任务页接入中英俄语言预览。固定表单标签、选项、占位文案和确认弹层用 `data-i18n`，动态列表、执行历史、状态提示和本地校验用 `t()`；语言切换时只重绘卡片与目录选择，不重置正在编辑的计划。日期使用当前界面语言格式化。三语词典各增加 113 键到 596 键，词典契约现在也扫描计划面板中的动态键。单文件 Home `.build/mdo-packed-docks-u6azhnfe` 实测：中文填写 `Locale QA` 周计划草稿、周一选中后切英语，草稿和选择仍在；英语创建后列表的频率、下次时间和按钮均为英文；切俄语后卡片与编辑器同步翻译。320px 下俄语卡片按钮均高 40px、文档无横向溢出。俄语预览/应用使配置 revision 1→2，未保存的编辑内容仍在；刷新后俄语与已创建计划均保持，浏览器无脚本错误。原生日期选择器和服务端错误文本不受前端词典控制。

Node 语法与词典契约通过；有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重新生成，SHA-256：`da0c77f9ee3161696939a095546872e955f9489ab863fe1d6eaf48e55e721dfd`。

反馈记录页接入中英俄语言预览和手机触控尺寸。固定标题、说明和按钮用 `data-i18n`，动态列表、点赞状态、计数、时间及游标过期提示用 `t()`；语言切换重绘已加载的卡片，保留分页位置与错误状态。三语词典各增加 14 键到 610 键，词典契约扫描反馈面板。单文件 Home `.build/mdo-packed-docks-4am_mw85` 中，本地模型回复后点赞，反馈列表读取到一条记录；英语与俄语预览即时翻译计数、时间、标记及操作。320px 的刷新和打开会话按钮高 40px、文档无横向溢出；从俄语卡片打开原会话后点赞仍选中，浏览器无脚本错误。英文单条计数的复数问题在观察后改用中性计数文案。分页“加载更多”本次仅做响应式样式修复，未用打包页触发实际分页。

Node 语法与词典契约通过；有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重新生成，SHA-256：`04e364c86d05455037504760149da4cb153512d2fc713eff2825dd89661f419d`。

项目设置的卡片、创建/编辑弹窗及只读清除范围接入中英俄语言预览，三语词典各增加 57 键到 667 键。项目列表重绘后的焦点目标现在通过稳定的 `data-project-action` 定位，不再依赖按钮中文文本；默认模型列表更新后可找回原选择框。项目编辑弹窗在当前默认模型暂不可用时保留该选项，避免打开编辑器时静默清空；这一边界尚需独立打包验收。第一次单文件 Home `.build/mdo-packed-docks-mtziu87y` 的页面加载发现项目弹窗模块括号遗漏并阻断脚本初始化；修复后 Home `.build/mdo-packed-docks-5fqx9ois` 可完整启动、创建 `Locale QA Project`、选择 Ling 3.0 Tiny 并在列表重绘后恢复焦点。英文只读清单和俄语项目卡片、编辑表单均显示对应语言，俄语 320px 无横向溢出，关闭范围弹层焦点回到对应按钮。该轮还发现手机编辑弹窗输入控件高 38px；最终 Home `.build/mdo-packed-docks-rq0uk1np` 验证四个输入/选择框均高 40px，浏览器无脚本错误。项目记忆编辑弹窗和模型编辑区仍需继续本地化。

Node 词典契约及模块语法检查通过；有界发布门禁通过 114 项单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重新生成，SHA-256：`e8d1737be992d0871600bccba689240339c297aae2afcecb4a165dd7fcca084d`。

发布门禁补入前端模块语法解析与 Node 交互测试。上一轮项目编辑弹窗曾出现括号遗漏，单独的 `node --check` 检查未能在当时的验收流程拦住，浏览器加载才发现整页初始化中断。`tools/check_web_modules.mjs` 现在用 Node 的模块解析器检查打包目录中全部 57 个 ES 模块，并在失败时指出文件；`tools/qa_release.py` 在打包前运行该检查和全部 `tests/*.mjs`。当前 10 项 Node 测试、114 项 Python 测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查通过。`tools/build_mdo.py` 仍不需要 Node.js；发布门禁需要。根目录 `mdo.exe` 已重新生成，SHA-256 与上一阶段相同：`e8d1737be992d0871600bccba689240339c297aae2afcecb4a165dd7fcca084d`。

项目与全局记忆编辑器迁入三语词典，三份词典各增加 34 键到 701 键。静态表单、按钮与占位文案由 `data-i18n` 更新，标题、空列表、状态、验证提示和成功提示由同一词典生成；语言订阅只重绘列表与提示，保留当前输入和列表焦点。手机断点的按钮及单行输入框设置 40px 最小触控高度。单文件 Home `.build/mdo-packed-docks-jcj8ybb2` 中，英语全局记忆保存 `Locale QA` 后显示 1 条、revision 1；未保存标题修改时关闭被阻止，放弃后切俄语重新打开仍能载入原内容。俄语项目记忆弹窗的标题、空状态与表单也正确显示。手机尺寸仅补静态样式，本轮尚未完成小视口页面验收；模型编辑区仍需本地化。

有界发布门禁通过 57 个前端模块解析、10 项 Node 测试、114 项 Python 单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查；根目录 `mdo.exe` 已重新生成，SHA-256：`4899cabb0c05214427e81534d7a227d6019760337c9edeb9ea7c7984a984fc80`。

模型与 Provider 双栏编辑区接入中英俄界面语言，三语词典各新增 73 键到 774 键。字段标签、协议/能力/上下文选项、内置模型说明、状态、验证反馈和删除确认均使用词典；语言切换只更新现有文案节点和无障碍属性，不重建表单。Ling 3.0 Tiny 仍保留内置免费且不可编辑的行为。单文件 Home `.build/mdo-packed-docks-7ehqto37` 中，新建模型的标识、名称、API 名及高级选项在切英语后保留，未保存时切到 Provider 会出现阻止提示，放弃后可切换；英语 Provider 草稿切俄语后也保留输入。最终 Home `.build/mdo-packed-docks-bysj6a0b` 复验英语模型页外层标题与说明。手机断点为列表、操作、选项和单行输入增加 40px 最小触控高度，仍待小视口页面实测。模型/Provider 实际保存与删除未在该浏览器夹具中提交，底层配置事务由运行探针验证。

有界发布门禁通过 57 个前端模块解析、10 项 Node 测试、114 项 Python 单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查。根目录 `mdo.exe` 已重新生成，SHA-256：`a83bcc74a69c17b13c4fac052af63269879c028767b6c22134ee84a640f03597`。

模型配置的真实保存链已在单文件 Home `.build/mdo-packed-docks-w8t_7y38` 补验：在打包页创建 `qa-provider` 和 `qa-model`，更新模型的 API 名及 Provider 名；模型设为默认后，新任务选择器选中该模型且刷新后仍保持，原会话继续使用其原有模型。再将 Ling 3.0 Tiny 设回默认，内置模型仍不可编辑。配置 revision 1→7，服务端 `/api/v1/models/config` 逐项读回 Provider URL、模型关联、API 名和默认模型；有模型引用时 Provider 删除按钮禁用。夹具 URL 使用 `example.invalid`，未调用该端点；删除没有提交。

320px 打包页检查发现“计费模型”标签的实际点按区域只有 24px，现将手机断点下的复选框字段标签设为至少 40px。重建后的单文件 Home `.build/mdo-packed-docks-j0cicd7l` 中，320px/390px 新建模型表单该项均高 40px，其余按钮和单行控件至少 40px，文档宽度等于视口，浏览器错误日志为空。发布门禁通过 57 个前端模块解析、10 项 Node 测试、114 项 Python 单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 已重新生成，SHA-256：`35a3f77341705718405101e6874bb70c5793af8227814213d219eaa3187405f4`。

补齐记忆编辑弹层的手机打包页验收。单文件 Home `.build/mdo-packed-docks-2cnzc00h` 在 320px 下从项目设置分别打开全局、默认项目记忆，保存两条隔离合成条目，两个列表各显示 1 条、revision 1；修改全局标题后点关闭会提示放弃，执行放弃后焦点回到全局入口。项目弹层正常关闭后焦点回到项目入口。刷新页面并重新打开两个弹层，条目均还在。320px/390px 弹层均完整落入视口，按钮与单行输入框至少 40px，置顶复选框的标签点按区高 40px，文档无横向溢出且浏览器错误日志为空。本阶段仅补验收记录，沿用同字节 `mdo.exe` 上一阶段已通过的有界发布门禁，SHA-256：`35a3f77341705718405101e6874bb70c5793af8227814213d219eaa3187405f4`。

会话操作确认弹层和反馈现在随中英俄界面语言切换。菜单此前已翻译，但弹层标题、说明、字段、按钮以及重命名、归档、置顶、分叉、导出等反馈仍固定中文；现在动态文案集中在会话操作模块，静态字段使用词典标注，语言变化只更新文案而不重置表单。三语词典各增至 811 键，词典契约覆盖新增键和带会话标题的俄语插值。手机断点将通用弹层中的输入框和选择框最小高度统一到 40px。

单文件 Home `.build/mdo-packed-docks-dei2c8lc` 中，英语设置应用后在会话菜单打开重命名弹层，标题、说明、字段、保存和取消均为英语；将合成会话改名为 `Localized QA` 后，侧栏与标题更新，提示为 `Session renamed`。再应用俄语，320px 分叉弹层的默认标题为 `Localized QA (ветка)`，序列字段和清空历史危险提示均为俄语；清空只打开确认后取消，未删除账本。该版输入框仍为 38px；重建后的 Home `.build/mdo-packed-docks-19d5au8w` 中，320px 俄语分叉弹层两个输入框均为 40px，弹层完整落在视口内，文档宽度 320px，浏览器错误日志为空。发布门禁通过 57 个前端模块解析、10 项 Node 测试、114 项 Python 单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 已重新生成，SHA-256：`72ef44dfca174e5a413e5f97ecbcdcb52bee95075f517a965d6b5446e48bc958`。

同一最终字节程序在 Home `.build/mdo-packed-docks-e8y1_c7s` 复核 320px 俄语分叉弹层：两处输入框均高 40px，弹层宽 286px，文档宽 320px，浏览器错误日志为空。只打开并取消，没有提交分叉。消息编辑/重试的打包页历史截断仍在独立隔离会话准备验证，图形提交需当次确认。

斜杠命令错误和运行恢复入口继续接入三语词典。`/model` 的不可切换与无其他模型、`/stop` 的空闲状态、会话未选中及运行中修改历史的提示，现在由翻译键生成；恢复说明和“打开恢复决策”按钮在切换语言时同步更新。图片任务标题、配置更新中和队列已满的输入提示也迁入词典，三语词典各为 821 键。单文件 Home `.build/mdo-packed-docks-k9422iy2` 验证俄语空闲 `/model`、`/stop` 提示；5 秒本地回复先于停止完成，因此没有将该次试验算作取消证据。Home `.build/mdo-packed-docks-ko_cqh8l` 用单次 15 秒有界本地回复复测：`/stop` 取消运行，服务端记录 `state=cancelled`、`cancel_requested=true`，时间线和恢复入口显示俄语，浏览器无脚本错误。

320px 下恢复入口原本仅约 21px 高，手机断点现设为至少 40px。重建后的单文件 Home `.build/mdo-packed-docks-e2jl5_w5` 再次触发一次有界取消，俄语恢复按钮实测高 40px，页面宽度保持 320px。发布门禁通过 57 个前端模块解析、10 项 Node 测试、114 项 Python 单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查；根目录 `mdo.exe` 已重新生成，SHA-256：`fd501196f96fc149d9bf35c0b255f2d34af74b88961343d4fff9c934c3365552`。

补验资源设置的手机布局时发现动态分区没有沿用常规设置的外层宽度：单文件 Home `.build/mdo-packed-docks-tipsyf2i` 的 320px 模型/Provider 标题贴住左缘，虽然内部表单控件至少 40px。现将直接挂在设置内容区下的资源分区统一设为居中的最大宽度容器，手机断点左右各留 14px；项目分区沿用原本宽度。修复后的单文件 Home `.build/mdo-packed-docks-baue9gwx` 实测模型和 Provider 新建表单：320px 下分区 x=14px、宽 277px，所有可见操作与单行控件及复选框标签至少 40px，文档宽度 320px；390px 与桌面断点也无横向溢出，浏览器错误日志为空。没有提交模型或 Provider 改动。发布门禁通过 57 个前端模块解析、10 项 Node 测试、114 项 Python 单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查；根目录 `mdo.exe` SHA-256：`a2e0442e2a226e0e7a06ba093957c678fdf8c1121f8c4c1ba1ee60cd27d959c9`。

任务检查器继续恢复旧版的即时可读反馈：任务、决策和上下文的固定标题、说明、状态，以及任务详情、审批资源、恢复选项和常见错误码接入中英俄词典，三份词典各增至 958 键。任务列表和详情在语言变化时同步重绘，保留选中的任务及已展开输出；审批卡保留已展开的参数和焦点，恢复卡保留选择。共享的审批标签模块避免决策和恢复对同一种影响给出不同翻译。返回同一会话时，导航的快速路径现在也刷新上下文：修复前从设置返回后标题是已有会话，检查器却显示“新任务”；修复后项目、模型、推理强度和工作区均来自该会话。手机断点将检查器内任务、审批和恢复的主要操作点按区补到至少 40px。

单文件 Home `.build/mdo-packed-docks-o8ca6t4w` 实测英语设置应用后返回原会话，上下文仍列出 `ling-3.0-tiny`、`Medium` 和该 Home 的工作区；320px 下停止一次 15 秒本地夹具运行，英语恢复卡两个按钮均高 40px，文档宽 320px，浏览器错误日志为空。Home `.build/mdo-packed-docks-pcns8d9d` 中，隔离模型夹具通过真实 `spawn` 调用执行本地 Python `print`，任务列表显示已完成；展开任务详情与标准输出后应用英语，返回时任务仍选中、标准输出仍展开，详情的状态、输出、事件和产物均已翻译。320px 下任务入口与返回按钮均高 40px，文档宽 320px，浏览器错误日志为空。夹具新增 `TASK UI` 标记，并将其匹配放在 `ASK UI` 之前，避免首轮响应子串误命中；本次最初的夹具进程先弹出询问，回答后仍完成了同一后台任务链。审批卡的英俄语言切换和展开参数此前也在隔离 Home `.build/mdo-packed-docks-h1btfo9o` 核对，实际允许一次仅用于夹具本地打印命令。

有界发布门禁通过 58 个前端模块解析、10 项 Node 测试、114 项 Python 单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重新生成，SHA-256：`f65f478ddce375a4d007c7104c82689be8debb873a853fdd851e29a80f7fd377`。

修复后台任务的会话归属匹配。`xwork` 的 `owner_session` 实际是该会话的 `snapshot.json` 绝对路径；对话停靠卡此前直接拿它与路由的会话 ID 比较，导致真实 `spawn` 任务在检查器可见、却始终不出现在所属对话下方。现在共享解析器只接受 `sessions/<project>/<session>/snapshot.json` 后缀，同时处理 Windows 和 Unix 路径分隔符，按项目及会话双重匹配，无法识别的来源不附着到当前对话。检查器详情将已识别来源显示为“项目 / 会话 ID”，不再占用一整行磁盘路径；API 仍保留原始路径供调试。隔离模型夹具把 `TASK UI` 延长为一次 12 秒本地打印任务，并排除其提示词在后续模型轮次误命中 `ASK UI`。

修复前单文件 Home `.build/mdo-packed-docks-uq8f_m8k` 中，检查器明确显示 1 个运行中任务，对话停靠区却只有询问卡。修复后的 Home `.build/mdo-packed-docks-hm3xva3d` 用同一真实 `spawn` 路径显示“后台任务 · 1 项”；点击“查看任务详情”打开检查器，详情显示 `default / <会话 ID>`。任务结束后对话卡自动消失，检查器仍保留已完成记录、退出码 0 与标准输出，浏览器脚本错误日志为空。Node 用例覆盖 Windows/Unix 路径、同名会话跨项目隔离与无法识别的来源。发布门禁通过 59 个前端模块解析、12 项 Node 测试、114 项 Python 单元/契约测试、18 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查；根目录 `mdo.exe` SHA-256：`bd33e044403188e7f19f07e9f811ea02647634c07b7a170d92b8919cb297c6df`。

同一字节程序还在 Home `.build/mdo-packed-docks-mdu12cab` 完成 320px 手机打包页复核：后台任务运行时对话卡可见，“查看任务详情”点按区高 40px，点按后任务抽屉打开；文档宽度保持 320px，浏览器脚本错误日志为空。

补验后台任务的真实停止路径。`tests/manual_packed_docks_qa.py` 的 `TASK UI` 现在接受 `--task-ms`（0–30000），便于将单个本地休眠进程留在可停止状态；默认仍为 12 秒，不进行并发或负载测试。Home `.build/mdo-packed-docks-t3_ouvsc` 的默认任务在点按前自然完成，因此未把该尝试计入停止证据。Home `.build/mdo-packed-docks-sms40ou0` 使用 30 秒夹具：检查器运行中任务点按“停止”后，页面显示“已停止”，详情事件依次有“请求停止”和状态变化，对话区后台任务卡撤下；服务端 `/api/v1/tasks/1` 返回 `cancelled`、有效退出码 1，浏览器无脚本错误。Home `.build/mdo-packed-docks-is59tyxu` 在 320px 再次点按：停止按钮实测高 40px，任务和卡片同样更新，API 为 `cancelled`，文档宽度 320px，浏览器无脚本错误。应用代码未修改，根目录 `mdo.exe` 与上阶段同字节；有界发布门禁在本阶段重新运行。

修复普通工具产物在对话中不可预览的问题。先在单文件 Home `.build/mdo-packed-docks-mwc0sxyc` 使用 `ARTIFACT UI` 夹具只读约 80 KiB 合成文件：xwork 已写出产物，但默认 64 KiB 内联片段超过 xllm-session 的 2 KiB 单条工具结果上限，运行报 `failed to append an ordered tool result to session`。mdo 现在把内联片段限制为 1024 字节，给状态前缀和产物位置留出空间；时间线直接从持久化的 `tool_done.artifact_id` 在对应工具卡片下提供按需预览，不依赖只写入全局运行记录的 `artifact_created` 事件。预览 API 限制为前 64 KiB，文本用纯文本节点显示，二进制只显示类型和读取大小；中英俄语言均有入口文案。

修复后的 Home `.build/mdo-packed-docks-lyu66m_d` 中，同一只读调用生成 82,380 字节产物，Agent 正常回复；工具卡能预览 65,536 字节并显示 SHA-256 与“仅预览前 64 KiB”。刷新后在 320px 再次展开并预览，入口高 40px、文档宽度 320px，浏览器脚本错误日志为空。Node 用例覆盖工具开始/结束配对及孤立结束事件中的产物 ID。发布门禁通过 60 个前端模块解析、14 项 Node 测试、114 项 Python 单元/契约测试、18 个有界运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查；根目录 `mdo.exe` 已重建，SHA-256：`10c80a51cec41ec5c32581a58e9270408149ba76a0a0b01dda5206ff2c808be0`。进程重启后的产物索引回放仍待独立验证。

补上产物预览在进程重启后的持久读取。重启同一隔离 Home 后，xwork 的 `/artifacts` 列表确实变为 0，虽然产物文件和会话 `tool_done` 事件都还在。现在工具卡使用会话事件 ID 请求 `/projects/{project}/sessions/{session}/artifacts/{event}`；服务端只接受属于该会话的产物事件，校验运行目录和文件名中的 ID，再将其锚定到当前便携 Home 下读取，不跟随符号链接，最大文件 8 MiB、单次响应 64 KiB。即使旧事件保存的是移动前的绝对路径，预览也不依赖原安装位置。

Home `.build/mdo-packed-docks-lyu66m_d` 使用新单文件程序真实重启后，运行态产物列表为 0，事件 #5 仍读出原产物前 65,536 字节与原 SHA-256；桌面时间线点击预览成功，320px 按钮高 40px、文档宽度 320px，浏览器脚本错误日志为空。新增 `test_session_artifact_runtime.py` 在独立 xs/TCC 进程创建会话，写入一份 86,400 字节合成产物与事件，停止程序并把 Home 搬到新目录，再验证成功读取、偏移越界、请求超限、损坏路径、缺失事件和跨会话拒绝。有界发布门禁通过 60 个前端模块解析、14 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查。根目录 `mdo.exe` 已重建，SHA-256：`77f58b01b0ba151cd5277617b03b2bd0ce33fc26531a05e0f570f901e1478389`。

工具产物的展开状态现与时间线消息键绑定；语言切换或消息重绘后仍恢复已打开的预览，切换会话及历史截断时清除旧状态。工具卡自身收起时保留已装载的正文，重新展开无需重复读取。隔离单文件 Home `.build/mdo-packed-docks-iy3v6o69` 使用一次 `ARTIFACT UI` 只读调用生成产物并展开预览；在设置中应用英语后返回同一会话，工具卡与预览仍展开、正文可读。再收起并展开工具卡，预览仍在；320px 下入口高 40px、文档宽 320px，浏览器错误日志为空。有界发布门禁通过 60 个前端模块解析、14 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查。根目录 `mdo.exe` 已重建，SHA-256：`ef3bdfd541ef6de32559bb04af43b32bff1409a17ccb88eeb99b774d37dd2256`。

长产物的正文滚动位置也需要跨设置导航保留。修复前 Home `.build/mdo-packed-docks-r472365l` 的预览滚到 700px 后进入设置，隐藏的滚动容器归零，返回会话从顶部开始。现在滚动时记录当前消息的位置，只在容器可见时更新快照；返回工作区时恢复位置，异步重新读取预览内容后也恢复。新单文件 Home `.build/mdo-packed-docks-1prisi0s` 从 1050px 的产物位置进入设置、应用英语、返回同一会话，预览仍展开、正文已加载且位置仍为 1050px；浏览器错误日志为空。有界发布门禁通过 60 个前端模块解析、14 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 已重建，SHA-256：`9dae454d893605e644df00954e350e9157ce4c6d5b317df0a86e0edc509ad830`。

排队卡恢复旧版的完整多行正文。此前样式将待发消息强制单行并用省略号隐藏后半段，三行合成指令在 Home `.build/mdo-packed-docks-n67yv86l` 中只占 16px 高，文本宽度 1488px 却只能看到 648px。现在保留换行并允许长词在容器内断行。新单文件 Home `.build/mdo-packed-docks-q22_ybj7` 中，同一指令在桌面完整显示为 64px 高；320px 下自然换行为 175px，正文和文档均无横向溢出，队列折叠、发送下一条、移除按钮各高 40px，浏览器错误日志为空。夹具只用一次 15 秒本地慢速回复与一条排队消息，没有进行负载测试；队列移除的图形提交仍需当次确认。有界发布门禁通过 60 个前端模块解析、14 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 已重建，SHA-256：`3a6ea3c75a098ad32effcbbee631a4fd9eb50f037edc183570179172504680c8`。

恢复旧版长草稿输入框的增长范围。旧版 `app_bak/wwwroot/src/ui.js` 将输入区高度封顶在 336px；新版此前桌面仅 220px、手机仅 160px，大段提示词过早变为内部滚动。现在桌面最多 336px 且不超过半屏，手机最多 260px 且不超过视口高度的 42%；视口变化时重新测量草稿高度，避免桌面转手机后停留在旧行高。修复前单文件 Home `.build/mdo-packed-docks-69skdste` 的 18 行草稿在桌面只显示 220px、320px 手机上只显示 160px。修复后 Home `.build/mdo-packed-docks-scg1j4ca` 桌面显示 336px，320×700 手机显示 260px，320×480 显示约 202px；发送按钮底部均在视口内，文档没有横向溢出。三行中等长度草稿在桌面为 105px，切到 320px 后重算为 252px，浏览器错误日志为空。整个验收只保留未发送草稿，没有修改会话历史。

本阶段有界发布门禁通过 60 个前端模块解析、14 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查；根目录 `mdo.exe` 已重建，SHA-256：`ec0f8fbb92a851cee39d81af2f17587a3a006928825d314812ced5172b8c089d`。

把核心交互中的固定中文操作反馈接入中英俄词典：后台排队或发送失败、草稿超限、询问及待办响应异常、无效消息反馈、Markdown 导出游标、资源目录刷新、启动时读取/保存上次会话，以及主题切换与设置未保存提示。三份词典各有 980 键；Node 检查扩展到相关状态模块，核对所有新增键和占位参数。单文件 Home `.build/mdo-packed-docks-f7nl9v1r` 实测英语设置未保存时点击主题切换显示英语阻止提示；应用英语后切到深色，页面显示 `Switched to dark theme`、revision 3。再应用俄语，320px 手机导航切回浅色后显示 `Включена светлая тема`，页面宽度保持 320px，浏览器脚本错误日志为空。测试使用隔离 Home，只修改夹具配置。

有界发布门禁通过 60 个前端模块解析、14 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 已重建，SHA-256：`ec8ac066800529d6868d32d447941023f5b26c2e0579644c7f6de623377d1e06`。

修复极窄手机的新任务选项裁切。原布局把项目、模型、推理和权限四个下拉框放在同一条可横向滚动的工具栏，单文件 Home `.build/mdo-packed-docks-k51ln1cc` 的 320px 新任务页中权限选项位于 x=325–381px，而可视工具栏只到 303px；280px 时推理和权限都不可直接看到。现在手机断点让项目选择单独占一行，模型随剩余空间缩小，其余选项保持完整 40px 点按高度。修复后的 Home `.build/mdo-packed-docks-tkda1zmy` 在 280px 下项目、模型、推理、权限和发送按钮均落在视口内，直接切换高思考和只读权限成功；320px/390px 页面无横向溢出。320×480 的 14 行未发送草稿将输入区封顶约 202px，发送按钮底部 463px，浏览器错误日志为空。测试仅使用隔离 Home 的未发送草稿。

有界发布门禁通过 60 个前端模块解析、14 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 已重建，SHA-256：`5180596d7e4968126d79bfd733beedbc3306406723d81dee233f026707c17381`。

恢复启动后直接键入的操作手感。单文件 Home `.build/mdo-packed-docks-leuhnjrq` 中，“每次询问”弹层选择继续上次任务后焦点落在页面；“新任务”直接启动同样没有输入焦点。启动导航现在在空白新任务立即聚焦输入框，加载已有会话时等详情就绪再聚焦；若用户先导航到其他任务，旧请求不会把焦点夺回。修复后 Home `.build/mdo-packed-docks-vn6t8jis` 的默认上次会话、新任务直接启动、询问后继续上次任务均聚焦输入框；320px 询问后选择新任务也聚焦，文档宽度 320px、发送按钮底部 683px，浏览器错误日志为空。新增 Node 用例验证异步加载与离开会话时的焦点所有权，测试只改隔离 Home 的启动偏好。

同期核对旧版联网设置的真实接线：xllm 客户端已有代理与私有 CA 参数，但 mdo 尚未从配置读取并传入；联网工具通过 `xsFetch`，当前 `XS_FetchRequest` 没有代理/CA 字段。后续恢复应覆盖模型请求与 Web 工具两条路径、凭据引用和配置事务，不能将仅有模型侧效果的开关称为全局代理。

有界发布门禁通过 60 个前端模块解析、15 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 已重建，SHA-256：`6ba5edf1754c2a0e6b4a526ba7a9dfec7c340dfa68e2e2bfc9481bea01a5f61c`。

恢复侧栏鼠标选择已有任务后直接键入的手感。此前点击新任务会聚焦输入框，再点回已有会话时焦点停在侧栏按钮。现在桌面指针选择在目标详情就绪且可输入后聚焦输入框；若用户先切到别的会话或控件，则取消待执行的焦点交接。键盘 Enter 选择继续留在侧栏。隔离单文件 Home `.build/mdo-packed-docks-_0jywrwn` 复现旧焦点，修复后 Home `.build/mdo-packed-docks-pt4170ou` 验证桌面鼠标和键盘两条路径；390px 手机 Home `.build/mdo-packed-docks-ywd5ex__` 从抽屉选会话后，焦点回到打开抽屉按钮，未自动聚焦输入区。浏览器脚本错误日志为空。Node 测试覆盖详情加载、改选控件和切走会话；验收没有发送消息。

有界发布门禁通过 61 个前端模块解析、16 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 已重建，SHA-256：`c584424b4b4630770772b8760a570360f0ccb696e72d39d59c3378de96d0feb1`。

修复 Markdown 会话导出与界面语言脱节。此前即使应用英语或俄语，导出正文的角色、历史提示、工具调用／结果、图片附件和时间仍固定为中文。现在按当前语言生成这些标签与日期时间，保留会话 ID、原始消息和附件引用；无标题任务的 Markdown 文件名也跟随语言。测试使用固定时间与合成用户、助手、工具事件，核对英俄标签、丢失／截断提示、URL 原文和含反引号工具输入的围栏。单文件 Home `.build/mdo-packed-docks-zzcw6f75` 在设置页应用英语和俄语后，顶栏导出两次均显示相应语言的下载反馈，浏览器错误日志为空；测试没有发送模型请求或修改会话历史。

有界发布门禁通过 61 个前端模块解析、17 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 已重建，SHA-256：`9ce7b8cb678d692bb57c45df944452120c9368336c6abfba7d9f002c15586693`。

恢复旧版消息操作按钮的常驻可见性。旧版在鼠标未悬停时仍显示复制、编辑、重试、分叉和反馈入口；新版桌面端此前将整排按钮透明化，用户虽然可通过键盘找到按钮，却难以发现操作。现在保留原有按钮、禁用态和悬停反馈，只移除隐藏规则。新单文件 Home `.build/mdo-packed-docks-zm1iszoo` 的一轮合成对话中，桌面 1280px 无悬停时用户与 Agent 的七个按钮均可见、计算透明度为 1；320px 下每个按钮都是 40×40px，文档宽度等于视口，浏览器无脚本错误。测试仅发送一条本地夹具消息，没有编辑或删除历史。

有界发布门禁通过 61 个前端模块解析、17 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、两次确定性打包、单文件零旁路写入与 20 秒启动检查；根目录 `mdo.exe` 已重建，SHA-256：`d58efe4c0967b1493ccba5bfec9be3d40efc9391832aa1f5428855a77e3efbf0`。

恢复旧版的会话事件轨迹。新版检查器新增“轨迹”标签，桌面会话顶栏提供一键入口；事件取自正在轮询的同一会话账本，最多显示最近 200 条并按时间倒序排列，摘要保留原始事件类型与 ID，展开时按需显示完整字段。切换会话清空旧节点，事件增量更新时复用旧节点以保留展开状态和焦点；历史已淘汰或刷新失败时显示明确提示。检查器四个标签支持方向键、Home/End 切换，手机端标签可横向滚动且保持 40px 点按高度。入口和说明接入中英俄词典。

隔离单文件 Home `.build/mdo-packed-docks-fx3ad3q_` 从空会话开始发送两条本地合成消息，轨迹从“暂无事件”增长到 5、10 条；展开 `agent_done #5` 显示原始 JSON，第二轮增量到达后 #5 仍展开。切到新任务时轨迹清空、顶栏入口禁用，返回原会话及刷新后重新回放 10 条；应用英语再返回，标题、计数、时间格式同步翻译。320px 下事件行约 46px、四个标签均 40px，无横向溢出及浏览器脚本错误。最终字节 Home `.build/mdo-packed-docks-yjgmnnpe` 另验证一轮消息产生 5 条事件，轨迹标签的右/左方向键在上下文与轨迹间切换；320px 文档宽度等于视口，浏览器脚本错误日志为空。

有界发布门禁通过 62 个前端模块解析、17 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、两次确定性打包、单文件零旁路写入与 20 秒启动检查；根目录 `mdo.exe` 已重建，SHA-256：`191dd05395e7d800c5d1945fb58f87a4ba1ad9cf94175f1107029adc50788b2e`。

修复运行中队列的虚假操作入口。隔离打包 Home `.build/mdo-packed-docks-4o1mkzo_` 复现首条运行时第二条已排队，队列仍显示可点击的“发送下一条”，但派发逻辑因运行未结束而立即返回。现在运行中的待发送项显示“当前任务结束后自动发送”状态文字；只有真正可手动重试或派发时才显示按钮。运行状态切换时只重绘一次队列，保留展开和焦点处理。`sending` 状态的核对后重试入口仍保留。中英俄词典同步更新。

修复后的隔离单文件 Home `.build/mdo-packed-docks-c58wn4s6` 验证运行中队列只显示自动发送提示，首条结束后第二条自动进入时间线、获得回复并清空队列。320px Home `.build/mdo-packed-docks-w11ae0yu` 再验证完整排队文本与提示、输入焦点保持；队列左右边界 13–307px、文档宽度等于 320px 视口，浏览器脚本错误日志为空。此轮只使用本地有界 15 秒合成回复，不做高负载测试。

有界发布门禁通过 62 个前端模块解析、17 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、两次确定性打包、单文件零旁路写入与 20 秒启动检查；根目录 `mdo.exe` 已重建，SHA-256：`a909ac82798f560f771c20a9e88f0146f1a613bf080fdd156f39e5293f924f48`。

修复连续输入时被异步提交清空的竞态。旧版在按 Enter 时同步消费输入；新版此前等队列 POST 或运行启动完成后才无条件清空输入框，隔离打包 Home `.build/mdo-packed-docks-c58wn4s6` 中紧接首条输入第二条时曾只留下 `tion QA`。现在提交时记录原输入及编辑版本，成功后只消费本次提交的前缀，保留随后键入或主动改写的草稿；附件仅在仍与本次提交快照一致时清空，后台会话只在草稿快照匹配时清除。普通输入不再为斜杠命令判定额外等待一个异步回合。

新任务创建期间还存在焦点竞态：修前 Home `.build/mdo-packed-docks-_7h10lis` 在创建会话的短暂加载中禁用输入框，连续键入的 `Next draft QA` 只留下 `Next draf`。现在只对刚创建的目标会话保持输入框可编辑，发送与附件操作仍等详情加载完成后启用；创建时把实时草稿转入新会话。修后 Home `.build/mdo-packed-docks-3aqibawq` 保留完整文本、刷新仍在新会话，返回空白新任务无残留。最终字节 Home `.build/mdo-packed-docks-zjjyenay` 再从新任务连续输入 `First final QA` 与 `Second final QA`，焦点始终在输入框，刷新后第二条完整保留，浏览器脚本错误日志为空。已有会话 Home `.build/mdo-packed-docks-9gwlhfvc` 的快速两次 Enter 也实际排入并发送完整 `Queued action QA`。

有界发布门禁通过 62 个前端模块解析、17 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、两次确定性打包、单文件零旁路写入与 20 秒启动检查；根目录 `mdo.exe` 已重建，SHA-256：`33a1fbc6fa04e21ebfc05070ad5a747d91fe78ae76b3b471cd88c2b97c34215d`。

进一步恢复旧版 Enter 当场消费输入的手感。此前虽然已避免异步成功时吞掉下一条草稿，但请求未确认期间，输入框仍会短暂保留已提交的原文。现在通过提交快照在 Enter 当场清空文字、附件和原草稿；新会话创建时只转移随后键入的内容。请求明确失败时，将原文和附件恢复到所属会话草稿；若已有新草稿，则把未发送原文置于其前并提示检查，避免静默丢失。已被队列或运行 API 接受后的后续刷新错误不会错误地恢复一份重复输入。三语提示同步更新，测试夹具新增最长 5 秒的有界运行 POST 延迟及一次合成拒绝。

隔离单文件 Home `.build/mdo-packed-docks-01nyh6zh` 验证既有会话 Enter 后输入框立即为空；3 秒后本地合成拒绝将 `Fail first QA` 恢复到输入框，错误提示可见。下一次延迟成功期间键入 `Next unsent QA`，回复到达与刷新后它仍独立保存在草稿中；从新任务发送 `New immediate QA` 时接续键入的 `Follow-up draft QA` 也转移到新会话并保留。320px Home `.build/mdo-packed-docks-ck4vardb` 验证失败时 `Original failed QA` 与随后输入的 `New draft QA` 按顺序合并，刷新后文本仍在，文档宽度 320px、脚本错误日志为空。隔离 Home `.build/mdo-packed-docks-2iqo9ulo` 以 5 秒队列 POST 和 15 秒单次本地回复验证第二条 `Queued second QA` 被自动发送，等待期间写的 `Third draft QA` 刷新后仍完整保存。Node 用例验证失败恢复时文本和图片 ID 一同保存，三条操作均未使用压力或高负载测试。

隔离 Home `.build/mdo-packed-docks-c8heqq4m` 另在原会话运行 POST 等待时切到空白新任务并输入 `Other new task draft QA`；一次合成失败后，原文 `Original session failure QA` 只恢复到原会话草稿，新任务草稿保持原样，切换返回时两者都可读取，浏览器脚本错误日志为空。

有界发布门禁通过 62 个前端模块解析、18 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、两次确定性打包、单文件零旁路写入与 20 秒启动检查；根目录 `mdo.exe` 已重建，SHA-256：`8d5f1c82590c37e4d99f1a587739bbe64a949ae656a8a1b72f047bba1a1fab87`。

修复请求尚未获服务端确认时第二次 Enter 被静默忽略的问题。输入区现在按会话保留一个提交通道：每次 Enter 当场清空本次输入，后续消息按录入顺序暂存；首条成功后依次写入服务端待发送队列，并在首轮恰好结束的竞态中主动触发派发。首条或后续队列请求明确失败时，将仍未发送的内容按原顺序恢复到所属会话草稿，排在后来键入的草稿前。新会话创建时通道随会话转移；模型配置在提交期间保持锁定，发送入口和三语提示显示暂存数量。暂存消息在请求确认前只存在当前页面内存；此时强制刷新仍可能丢失暂存项，后续需结合服务端持久队列补齐该窗口。

修复前隔离 Home `.build/mdo-packed-docks-c3isxd3m` 的两次快速 Enter 仅发送第一条，第二条留在输入框。修复后 Home `.build/mdo-packed-docks-_p3ane9q` 中已有会话及新任务创建期间的两次快速 Enter 均依次完成两轮；Home `.build/mdo-packed-docks-a3nbvmk7` 的合成首条拒绝将两条消息及新草稿按顺序恢复。Home `.build/mdo-packed-docks-uzu2e6uf` 中一轮 15 秒本地慢速回复与两次 3 秒队列延迟使 B、C 顺序入队并顺序执行，D 草稿保持；Home `.build/mdo-packed-docks-xsm0cos0` 的一次合成队列拒绝将 B、C 恢复到 D 前。各夹具只执行少量本地合成轮次，没有压力或高负载测试。

有界发布门禁通过 62 个前端模块解析、18 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查；根目录 `mdo.exe` 已重建，SHA-256：`1a7d33fd1e807af0d900cf1b179d93e04a7fb4e8454c190afb8f3a22fce9e4b3`。

暂存消息现在直接显示在待发送区域，而不只在输入提示中显示数量。正文保留换行，卡片标明“等待入队确认”，尚未获得服务端确认时不显示移除或重试入口；一旦接受，卡片转成正常的持久队列项。队列组件将服务端项与当前会话的暂存项合并计数，切换会话后只显示所选会话的内容。三语状态文案同步更新。

隔离单文件 Home `.build/mdo-packed-docks-3znwq2s2` 在 5 秒运行 POST 延迟期间看到第二条完整正文与等待入队标记；首条被接受后第二条转为正式队列项，随后完成并撤下卡片。Home `.build/mdo-packed-docks-ik8uc1qs` 的一次合成首轮拒绝中，暂存卡撤下，两条内容按顺序恢复到输入框且错误提示可见。两次测试各只有两条本地合成消息，没有压力或高负载测试。

有界发布门禁通过 62 个前端模块解析、18 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查；根目录 `mdo.exe` 已重建，SHA-256：`02ceb5c8a60060d47912cb1d679662286ab460be248e241912a823a1152c45a6`。

补齐持久队列写入和页面刷新交错时的可见性。客户端现在为一次入队生成随机 ID；若 POST 的响应丢失，读取服务端队列核对该 ID。核对失败或队列中找不到该项时，不自动重发：队列项可能已经被派发并删除，服务端只对当前队列中的 ID 去重，同 ID 重发仍可能重复执行。此时保留输入并提示先检查队列与轨迹，避免把结果断言为“未发送”。小于 keepalive 限额的请求允许在页面导航时继续完成。运行列表的既有有界轮询同时刷新所选会话队列，运行结束时也先重新读取队列再派发，以发现页面刷新后才落盘的队列项。

修复前隔离 Home `.build/mdo-packed-docks-yge6e76y` 在运行中提交第二条后立即刷新，代理记录队列 POST、`queue.json` 最终含一条 `pending`，但新页面先读到空队列，首轮结束后仍只执行一轮。修复后 Home `.build/mdo-packed-docks-3q_a_ao7` 以同样 3 秒队列延迟与一次 10 秒本地回复复测：刷新后第二条被发现并完成，代理计一次队列 POST、两次运行 POST。Home `.build/mdo-packed-docks-2iqrvx87` 将首个队列响应在服务端接受后断开，前端核对出原项并只执行一轮后续消息。旧版 Home `.build/mdo-packed-docks-1x_5pg5g` 曾在首次核对 GET 失败后自动重发，这只能证明当前队列未重复，不能证明队列项先被消费时仍安全。新 Home `.build/mdo-packed-docks-qg77gmsd` 用同样的丢响应和核对失败条件复测，代理只记录一次队列 POST、两次运行 POST；第二条执行一次，输入区保留“发送状态尚未确认”的草稿与提示，需用户核对轨迹后处理。测试没有高负载。首轮运行 POST 尚未获接受、新会话尚未创建时的本地暂存项仍无法跨强制刷新保证恢复；若要彻底关闭这类交错，需要设计便携 Home 中的可恢复提交日志及已消费 ID 去重边界。

有界发布门禁通过 62 个前端模块解析、18 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查；根目录 `mdo.exe` 已重建，SHA-256：`1abf9611b86a5ceb5c8b5b8c9720094fad9ab7bb493c7d279e0d0576312aa24a`。

取消未确认队列提交的自动重发后，同一套有界发布门禁再次通过：62 个前端模块解析、18 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入及 20 秒启动检查。根目录 `mdo.exe` 已重建，SHA-256：`ace0fce65708c78034dbbb445b02fed66222a3f0b528a0ff6e4a8fc61b5f6ac5`。

随后补齐了未确认入队项被页面轮询发现后的输入收敛。前端为每个会话保留最多 64 个本页已见的队列 ID；即使该项随即派发并从队列删除，本页仍能判定原 POST 已被接受。只有恢复的草稿和当前可见输入都与那条提交完全一致时才自动清空；用户已继续编辑、合并其他未发送内容或切换会话时，不覆盖新草稿。隔离 Home `.build/mdo-packed-docks-0h7etl4_` 在丢 POST 响应且首次 GET 失败后，只提交一次队列 POST、执行两轮，原提示草稿在队列项被看到后清空。Home `.build/mdo-packed-docks-rq6v55m8` 复测用户在核对期间写入新草稿：原队列项执行一次，新草稿保持且刷新后仍可恢复。若其他页面或进程在本页看见队列项之前就将其消费，本页仍只能保持未确认提示，不能自动断言接受或失败。

本项有界发布门禁通过 62 个前端模块解析、18 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256：`7fb5bb41699217f78546d6f9d03025116fc03a9d61784e2eadd970d8f0ec49a3`。

首轮运行启动也补上响应丢失时的明确反馈。`startRun` 对网络中断、无效响应以及服务端已启动但无法形成结果的错误标记“运行状态未确认”；输入区保留原文，提示核对当前运行和会话轨迹，立即读取运行列表，不把这类失败称为“未发出”。队列自动派发与编辑/重试后的启动共用这一标记；服务端明确拒绝仍保持原有草稿恢复提示。夹具新增 `--drop-first-run-response`，仅把第一条运行 POST 转发给本地打包服务、在服务端接受后断开浏览器响应。修复前 Home `.build/mdo-packed-docks-ioso_8nb` 的页面显示“未发送内容已恢复”，但轨迹已经有用户消息和回复；修复后 Home `.build/mdo-packed-docks-zzr1448k` 显示运行中及未确认警示，随后时间线显示同一条回复，代理总计只有一次运行 POST。由于运行启动 API 目前没有可跨刷新去重的请求 ID，完成后保留原草稿和警示供用户核对，不自动重发或删除。

本项有界发布门禁通过 62 个前端模块解析、20 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256：`891f0c7e66227e7dc7c61f833e19a705f33bff4c86df81edd34dbfc3c2eb5eae`。

未确认的运行启动现在会在便携 Home 的会话草稿 schema 3 中保存 `run_admission_uncertain`。会话刷新或切走再回来后，警示和发送禁用状态仍在；后续编辑保留标记。用户检查当前运行与轨迹后点击“已核对，允许继续发送”，才清除标记并重新开放发送。草稿加载完成前发送入口保持关闭，键盘提交会等待加载；若用户在 GET 返回前编辑，服务端已有的待核对标记会合入本地草稿，避免保存竞态清除。草稿 schema 1/2 仍可读取，下一次写入升级为 schema 3；待发队列 schema 保持不变。隔离单文件 Home `.build/mdo-packed-docks-ukoowfgu` 在服务端接受运行 POST 后丢弃浏览器响应：运行和回复均可见，刷新后警示与禁发状态仍在；改写草稿并再次刷新后内容与标记仍在，显式核对解除后草稿保留且可发送。代理最终仅记录一次运行 POST、零次队列 POST。该核对动作不自动清空草稿或重发，因为启动 API 仍没有可跨刷新去重的请求 ID。

本项有界发布门禁通过 62 个前端模块解析、22 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重建，与门禁产物的 SHA-256 同为 `ff89a2d7c3c86f5fc7a0b0bfb1999edd011cbe787f8b56b538444326e6a51835`。

待核对状态现在也约束服务端队列的自动派发。队列派发前须读完所属会话草稿；如果上一轮运行的启动状态未确认，队列卡显示“核对前一轮运行后继续发送”，不提供手动派发入口，也不会在刷新后抢先启动下一轮。用户核对后解除标记，队首若仍为 `pending` 则恢复自动派发；若为 `sending`，仍保留原有“确认未发送后重试”决策，不把旧项当作未发送。隔离夹具新增 `--drop-run-response-number 2`，可只断开第二轮运行的浏览器响应。Home `.build/mdo-packed-docks-b5r71upm` 先执行慢速 A，再排入 B、C；B 已执行但响应丢失后，队首保持 `sending`、C 保持 `pending`，刷新后待核对提示与禁发状态仍在，代理总计两次运行 POST、两次入队 POST。这个场景只能证明队首自身的 `sending` 会阻止后续派发。为直接验证新保护，Home `.build/mdo-packed-docks-3oin0nih` 让直接运行 A 的响应丢失，并在 A 运行时向隔离队列加入一条 `pending` 合成消息：A 完成、刷新后该项仍未执行，代理仅一次运行 POST。核对解除后该项自动执行一次、队列归零，最终代理总计两次运行 POST、一次入队 POST。两次测试均为少量本地合成回合。

本项有界发布门禁通过 62 个前端模块解析、22 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重建，与门禁产物的 SHA-256 同为 `9fd9e439eed6651e171efb9b69b1ff77c8f74863aa05d58ca5c9398c940027e8`。

连续输入的持久提交日志开始落地。服务端队列 schema 4 增加 `staged`：`POST /queue` 可用 `stage: true` 原子保存尚未允许派发的消息，之后只允许按 `staged → pending → sending` 提升；已标记 `sending` 的旧重试仍可由用户核对后转回 `pending`。旧 schema 1/2/3 可读；图片引用在 `staged` 期间仍受保护。运行探针覆盖原位重复提交、非法跳级、提升、删除、图片引用与旧文件升级。完整写前保存和前端恢复流程在 `docs/durable-submission-journal.md`；本阶段尚未把输入区接到 `staged`，强制刷新时内存暂存可能丢失的缺口仍在。

本阶段有界发布门禁通过 62 个前端模块解析、22 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重建，与门禁产物的 SHA-256 同为 `db40042845083985d34d0e5d5a63e7b3ceaf7f27a829758c227fb887f71d11cd`。

既有会话发送现接入便携写前快照：草稿 schema 4 增加独立 `submission`（稳定 ID、原文、图片 ID、优先标记），先保存快照再清空匹配的输入并写入队列 `staged`，确认同 ID、同内容后清除快照、提升为 `pending`，由统一队列派发运行。普通草稿可同时保存下一条输入。刷新或切换会话时按队列 ID 核对，已保存的 `staged` 项可由用户手动继续；队列项不见时保留快照和核对入口，不自动重发。前端轮询若遇到本页仍在执行的队列 POST，不再把暂时缺席误报为失败。服务端验证快照中的图片仍属于本会话且存在，引用期间不允许删除；草稿 schema 1/2/3 可读并在下次保存升级。

隔离单文件 Home `.build/mdo-packed-docks-yufu5d0r` 中，既有会话两次发送分别落入队列并执行，两次队列 POST、两次运行 POST。`--drop-first-queue-response --fail-first-queue-reconcile` 的 Home `.build/mdo-packed-docks-1pe354rv` 在服务端接受后丢浏览器响应；刷新仍显示同一条 `staged` 消息，点击“继续发送”后只记录一次队列 POST 和一次运行 POST。`--fail-first-queue` 的 Home `.build/mdo-packed-docks-uy2wjflm` 中，队列明确拒绝后保留快照，点击“核对队列并恢复”将原文放回输入区，仅一次队列 POST、零次运行 POST。另有 Node 测试覆盖快照与下一条草稿的分离和刷新恢复，API 探针覆盖 schema 升级、清除和图片引用。当前一个会话只支持一条在途快照：发送期间可继续编辑，但暂不能连续按 Enter 提交多条；新任务创建及旧 `submissionLanes` 也尚未接入写前日志。后续阶段按 `docs/durable-submission-journal.md` 完成有序多项日志与消费凭据。

本阶段有界发布门禁通过 62 个前端模块解析、23 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重建，与门禁产物的 SHA-256 同为 `2cd4d2b38c6e7d9eecb4347516bec5c2fb7a1ec985778f22d82c555cf2395eab`。

连续输入的服务端日志现升级为草稿 schema 5：`submissions` 是最多 20 条的有序列表，正文合计不超过 192 KiB，每条有唯一 ID、正文、最多四张图片、优先标记以及 `prepared/posting` 状态。服务端逐项验证图片存在、保护图片引用，拒绝重复 ID 和越界列表；旧 schema 4 的单条 `submission` 按保守的 `posting` 状态导入。GET 暂时提供旧 `submission` 首项投影，PUT 暂时兼容旧单条写法，因此本阶段的前端仍能使用上一阶段流程。API 探针覆盖两条列表保存、顺序读取、状态更新、移除、重复 ID 拒绝、图片引用和旧文件升级。前端有序入队尚未接线，不把服务端日志能力当作连续 Enter 已完成。

本阶段有界发布门禁通过 62 个前端模块解析、23 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重建，与门禁产物的 SHA-256 同为 `d444a541c548e51de529eab11a4ca147568833306510f8221e2841d1c48753b9`。

既有会话现恢复前一条入队等待时连续按 Enter 的手感。每条消息先以 `prepared` 状态追加到便携草稿日志，确认落盘后才清空对应输入；串行提交器把队首置为 `posting`，确认服务端同 ID 的 `staged` 项后清除日志项并提升队列。后续消息保留顺序，刷新后若前驱仍为 `staged`，须先由用户继续此前驱，后续才会入队。前驱 POST 被明确拒绝或结果不明时显示核对入口；多条在途时，用户核对后以原 ID 重试队首，不会丢弃后续输入。输入区只在日志达到 20 条上限或极短的日志清除事务中关闭发送。

隔离单文件 Home `.build/mdo-packed-docks-ebae8xqh` 中，首条入队响应延迟 5 秒时连续提交两条并刷新：页面按序恢复两条快照；首条变为 `staged` 后第二条仍为 `prepared`，用户继续首条后两条各执行一次。Home `.build/mdo-packed-docks-h12zxlug` 中，首条队列 POST 明确拒绝；两条快照刷新后仍在，人工核对并重试后时间线按序出现两条消息及回复，`draft.json` 中日志清空且 `queue.json` 无残留。Node 测试覆盖两次 Enter 的持久顺序、刷新读取与 `staged` 前驱屏障。新任务创建仍走内存中的 `submissionLanes`，队列消费凭据与运行启动去重尚未完成；这些边界不能推断为已可靠恢复。

本阶段有界发布门禁通过 63 个前端模块解析、27 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重建，与门禁产物的 SHA-256 同为 `2297cb2c5de3175f175ef5a3edd9ac14fa3a04d54f7e17c5f6c3642a59091008`。

新任务创建的服务端去重边界已准备：`POST /sessions` 可选 `client_session_id`（32 位小写十六进制），首次创建返回 201；同 ID、同创建配置的请求返回现有会话和 200，不重写 `meta.json`；不同配置返回 409，非法 ID 返回 422。会话管理器在创建前检查指定目录，已有但元数据不完整时返回 409，不覆盖或回滚已有内容。API 运行探针覆盖首次创建、同 ID 重试、元数据未改写、配置冲突与非法 ID。前端还未持久保存此 ID，故新任务首条消息在创建响应丢失后的恢复仍是下一阶段工作。

本阶段有界发布门禁通过 63 个前端模块解析、27 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重建，与门禁产物的 SHA-256 同为 `0e7b544ab4cfbb20baa237297e9fce02beed97340b4b76b2efb68a590ec43c7f`。

新任务连续发送现接入便携恢复流程。全局草稿升级到 schema 6，在 `new_task` 中保存项目、稳定会话 ID、创建配置及 `creating/copying` 阶段，并用有序 `submissions` 保存发送快照。首次创建以同一个客户端 ID 请求服务端；创建响应丢失或刷新后，先查询该 ID，再按服务端的幂等结果继续。确认会话存在后，将快照复制到该会话的 schema 5 草稿，确认保存才清除全局记录并导航。若复制中断且目标草稿缺少快照，停止自动恢复，要求用户核对，避免覆盖或重发。旧的纯内存 `submissionLanes` 路径已移除；已有会话与新任务在把本次输入加入本地日志后立即清空匹配的输入，恢复连续 Enter 的操作手感。

隔离单文件 Home `.build/mdo-packed-docks-bceazief` 延迟创建并丢弃第一次创建响应：两次快速 Enter 后立即刷新，页面恢复两条独立快照、空输入框，最终只有一个会话，依序执行两条消息；代理观察到两次创建 POST、两次队列 POST、两次运行 POST。最终字节 Home `.build/mdo-packed-docks-klo13hjf` 再验证延迟创建时两次 Enter 落入同一会话、时间线顺序正确；只有一次创建 POST、两次队列 POST 与两次运行 POST。测试只使用本地合成回复。纯图片新任务仍沿用直接创建路径；队列消费凭据和运行启动跨刷新去重尚未完成，不能将这两处视为可靠恢复。

本阶段有界发布门禁通过 64 个前端模块解析、29 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重建，与门禁产物的 SHA-256 同为 `641279931b9d1623afe2cda059d1271a2cb15decf86e74780577c940193c2c16`。

图片优先的新任务创建也改用全局便携草稿中的稳定会话 ID。用户选择图片时先保存尚无发送快照的 `new_task`，再创建会话；创建响应丢失则以该 ID 查询服务端，恢复后把未发送文字转入会话草稿，再开始上传。处于 `copying` 的空快照任务刷新后也可自动完成，不会因缺少队首而误报冲突。新任务标题按 Unicode 字符和 UTF-8 字节共同限制，避免表情符号把标题截成半个字符或超过服务端容量。若浏览器在图片上传前关闭，文件选择本身无法持久化，用户须重新选择；已经完成的上传继续由会话附件保存。

隔离单文件 Home `.build/mdo-packed-docks-fvgfunuu` 将内置模型临时标记为支持图片，并让代理在服务端接受新会话创建后丢弃浏览器响应。新任务先输入 `ATTACHMENT FIRST UI` 再选一张本地合成 PNG；页面进入同一个新会话，刷新后文字与图片预览仍在，发送后时间线显示带图用户消息及模型回复。代理记录一次创建 POST、一次队列 POST、一次运行 POST。测试只作用于隔离 Home 与本地模型，不改变正式打包的 Ling 能力配置。

本阶段有界发布门禁通过 64 个前端模块解析、31 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重建，与门禁产物的 SHA-256 同为 `7635a05a92a98fba003d6111258fdff857d0394c8d462bc6a5ddf367f37070c9`。

修复新任务创建被明确拒绝后的死路。此前 4xx 创建失败保留写前日志，却继续锁定模型和推理选项；“重试恢复”仍用原无效配置，用户无法自行处理。现在仅在创建 API 明确返回 4xx、且任务尚处于 `creating` 时开放模型、思考和权限；点击“使用当前选项重试创建”会先在全局便携草稿中同时更换会话 ID、首条快照 ID 和配置，再重新创建。后续快照维持原顺序。网络错误或创建响应不明时仍保持原 ID 和配置进行核对，不以换 ID 规避不确定性。中英俄提示区分这两种状态；图片入口在待创建快照期间继续禁用，避免把文件关联到未确认的会话。

隔离单文件 Home `.build/mdo-packed-docks-hhy2043i` 的代理首次对 `POST /sessions` 返回合成 422：页面仍显示原消息快照，图片入口和发送按钮禁用，推理选项可切换。切到“高”后点击新重试入口，页面进入唯一的新会话，时间线各有一条用户消息与模型回复。代理共收到两次创建 POST（首次被拒绝）、一次队列 POST、一次运行 POST；最终 `data/draft.json` 的 `submissions` 为空、`new_task` 为 null，会话 `meta.json` 的 `reasoning_effort` 为 `high`。Node 用例还覆盖两条待发送输入在拒绝与改配重试之间保持顺序。测试仅用隔离 Home 的本地合成请求。

本阶段有界发布门禁通过 64 个前端模块解析、33 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重建，与门禁产物的 SHA-256 同为 `80fd39b895e16f7018801cdc9db877c77edf1096a67099a84fd5ef174ec3528d`。

补齐三个先前仅有模块或服务端证据的打包页操作。隔离 Home `.build/mdo-packed-docks-ehwdhkzc` 中，先将合成 PNG 加入已有会话草稿，再点击缩略图移除：页面撤下缩略图，焦点回到添加图片按钮，`draft.json` 中图片引用为空，附件目录无剩余文件。同一 Home 的两轮合成对话中，第二条消息通过编辑弹层改为 `EDITED SECOND UI` 后重新发送；第一轮保留，旧第二轮撤回，新回合得到回复。随后点击该回复的重试入口，目标回合重新执行；刷新后仍只显示第一轮和重试后的 `EDITED SECOND UI`，并显示两个历史截断边界。

队列移除使用独立 Home `.build/mdo-packed-docks-igncp34n`：首轮本地模型延迟 15 秒，运行期间提交 `REMOVE BEFORE DISPATCH`，立即点按待发卡片的移除按钮。`queue.json` 随即为 `items: []`；首轮结束后时间线和四条持久 UI 事件只包含第一条消息和回复，第二条未执行。先前 12 秒尝试在点按前已经自动派发，因此未作为通过证据。三项验收均只操作隔离 Home 的合成数据，没有压力或高负载；Windows 打包页已覆盖这些操作，系统原生拖放及其他平台触控另待验证。本阶段未改应用代码，根目录 `mdo.exe` 已重新打包，SHA-256 仍为 `80fd39b895e16f7018801cdc9db877c77edf1096a67099a84fd5ef174ec3528d`。

新任务创建等交互中的稳定 API 错误码现通过三语词典呈现；例如 `session_profile_invalid` 不再把服务端英文说明直接拼在俄语恢复提示后。无效 JSON 响应、图片能力不匹配、运行并发上限、模型服务不可用、队列上限/状态冲突、草稿冲突、附件存储不可用和会话状态冲突也有对应提示。未知错误码仍保留服务端原文，以便诊断。隔离代理夹具补上 `PATCH` 转发，使带创建拒绝注入的打包页也能保存语言设置；原先代理返回 HTML 501 时，页面正确显示新译出的“无效响应”提示，但该次尝试未用于新任务验收。

在单文件 Home `.build/mdo-packed-docks-ucfo5byg` 中，俄语设置预览和应用把 revision 从 1 提到 2。随后新任务发送 `LOCALE QA profile rejection`，代理仅一次创建 POST 返回合成 422、零次队列或运行 POST。页面把原文保留在待核对区，并以俄语同时说明创建被拒绝和配置无效，提供重试入口；没有显示夹具的英文 `Synthetic profile rejection`。320px 下文档宽度为 320px，重试按钮高 40px，浏览器脚本错误日志为空。本阶段发布门禁通过 64 个前端模块解析、33 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重建，SHA-256 为 `b38d7738eced65776a4090f5767b6bd3d94bc805aeb75d202501dd2d297af352`。其余服务端错误码与外部资源描述仍须逐项梳理。

会话草稿升级到 schema 6，为有序输入快照新增 `rejected` 状态。只有入队阶段明确收到 `queue_full` 422（或本地队列已满、没有发出 POST）才写入该状态；入队之后的提升失败与其它响应不明继续保持 `posting` 核对边界。刷新不会自动重发 `rejected` 队首，用户可把唯一快照恢复到输入框，或在多条快照时明确重试队首。队列卡、错误恢复按钮和输入提示按三语区别“已拒绝”与“仍在保存/结果不明”；全局新任务草稿仍为独立 schema 6，不允许保存会话专属的 `rejected` 状态。旧会话 schema 1–5 仍可读，旧单项投影保持兼容。Node 用例覆盖明确拒绝后的刷新和多条按序重试，API 测试覆盖 schema 升级及全局草稿拒绝该状态。

隔离单文件 Home `.build/mdo-packed-docks-7lc0m343` 使用 `--full-first-queue --slow-ms 1000`：首次队列 POST 返回合成 422，页面显示“入队被拒绝 · 输入已保存”；刷新后同一快照、错误恢复按钮及“队首入队被拒绝”输入提示仍在，没有自动入队。320px 视口中文档宽度为 320px，恢复按钮高 40px。点击恢复后原文回到输入框且获焦，再按 Enter 得到一条用户消息和一次本地模型回复；代理计数为队列 POST 两次（一次拒绝、一次接受）、运行 POST 一次，会话 `draft.json` 为 schema 6 且 `submissions` 为空，`queue.json` 为空，浏览器脚本错误日志为空。该证据仅证明明确拒绝的恢复路径；队列消费凭据和运行启动去重仍未实现。发布门禁通过 64 个前端模块解析、34 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与门禁产物 SHA-256 同为 `344fcfa828d7a67b9d41f5e48b3a2d4c6a7c5159e9e0d7e55b3eec0d6c8af586`。

`/clear` 完整操作暴露一个多余提示：服务端在清空后留下 `history_truncated` 边界事件，事件 API 因较早 ID 已移除同时报告 `history_lost`；旧时间线因此先显示泛化“更早的事件已不在当前记录中”，再显示“会话历史已清空”。现仅在最早可见事件不是明确历史边界时显示泛化缺口提示，保留真实无解释的记录缺口警告。Node 用例覆盖这两种投影。清空前隔离 Home `.build/mdo-packed-docks-wp8a0n7q` 中，一个已完成回合经会话菜单清空后消失，未发送草稿在刷新后保留，但两种提示同时出现。

最终单文件 Home `.build/mdo-packed-docks-c87ethhn` 在 320px 用 `/clear` 打开确认弹层并实际提交：按钮高 40px、文档宽度为 320px，原用户消息与回复被清除，仅保留“会话历史已清空”边界；刷新后仍如此。随后发送 `AFTER CLEAR FINAL QA` 获得一条用户消息和本地模型回复，浏览器脚本错误日志为空。未对日常 Home 执行删除。发布门禁通过 64 个前端模块解析、35 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 已重建，与门禁产物 SHA-256 同为 `cb9d3c169986a3c106d8d1c6361f03b12d311e5ec129b8c971f927ebc4485048`。

本阶段把仍在队列中的运行启动和队列项关联。运行 POST 可选携带稳定 `queue_item_id`；服务端先核对 `sending` 项的正文和图片，再启动运行并把 `run_id` 写入 schema 5 的 `queue.json`。已绑定的同一项禁止再次启动，且不允许回退到 `pending`。前端在响应丢失后刷新队列，能显示“服务端已接受运行”的明确提示；语言包加载后会重新绘制核对提示，避免冷启动时露出原始词典键。人工核对并移除已接受项后，后续输入可继续派发。API 探针覆盖无效 ID、正文不匹配、持久关联、重复启动与回退拒绝。

最终单文件 Home `.build/mdo-packed-docks-66xsiqg7` 中，首条慢速合成回合运行期间排入第二条，代理在服务端接受第二轮后丢弃浏览器响应。`queue.json` 保留 `sending` 项及 `run_id`；刷新后时间线已有两轮回复，队列卡和输入区均显示已接受且待核对，没有原始词典键，也没有自动重发。人工点“已核对”并移除该项后发送第三条，得到第三轮回复；代理总计记录三次队列 POST、三次运行 POST。320px 下文档和视口同宽，发送按钮高 40px，浏览器错误日志为空。此证据只覆盖队列项仍存在的丢响应窗口；移除后的消费凭据、启动与绑定之间的原子性和并发启动去重仍须单独实现。发布门禁通过 64 个前端模块解析、35 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与门禁产物 SHA-256 同为 `fba9e3d2a7554e16229b47abb3a71ff67d925da6a2e99c38a9c9fe239b3ef52f`。

本阶段为已启动的队列项新增按 ID 命名的会话凭据：`queue-receipts/<id>.json` 先于队列 `run_id` 写回，移除队列项后仍能通过 `GET /queue/{id}` 查询 `accepted` 和运行 ID。同 ID 再入队或再启动运行被拒绝；若队列项尚在、但写回 `run_id` 失败，读取队列时可从凭据补全。凭据损坏或与队列冲突时拒绝读取，不把 ID 当作可重试。前端提交器在队列项消失时按稳定 ID 查询凭据；找到已接受的运行才清除对应提交快照，继续处理后续输入。API 探针覆盖创建、移除后查询、重复拒绝、缺失和损坏凭据；Node 用例覆盖队首被消费后释放快照并继续第二条。

隔离 Home `.build/mdo-packed-docks-n7lt2mvo` 使用 `--drop-first-queue-response --consume-dropped-queue-response`：夹具在服务端接受首条入队后，模拟另一页面提升、启动并移除该项，再丢弃原浏览器响应。页面自动核对凭据，`draft.json` 的 `submissions` 清空，`queue.json` 为空，凭据文件留在会话目录；刷新后第一条仍只有一轮消息与回复，第二条新输入正常完成。最终字节 Home `.build/mdo-packed-docks-ifuf7eyu` 重复首条故障窗口与刷新，仍只显示一轮，320px 文档宽度为 320px、发送按钮高 40px、浏览器错误日志为空。夹具记录一次浏览器队列 POST；第一轮运行由夹具直接调用上游，不计入代理运行 POST。此阶段不声称已解决运行启动与凭据写入之间的崩溃窗口，或两个页面同时启动同一队列项的竞态；单个凭据文件的长期数量也待评估。发布门禁通过 64 个前端模块解析、37 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与门禁产物 SHA-256 同为 `14bef1517ea56eae1bd38037845d472cce697d6c089f3a18d07d71795912246f`。

运行启动前现于队列锁内持久写入 `starting` 凭据，再调用运行管理器。第二个页面对同一项发起启动时得到 `queue_run_starting`，前端显示核对提示并保留“运行状态未确认”屏障，不自动重发。启动成功后凭据转为 `accepted` 并记录运行 ID；即使另一页面在启动期间删除队列项，绑定仍可写入独立凭据。`starting` 项禁止回退 `pending`，删除后同 ID 也不能再入队。运行探针用两条同时发出的请求验证一次 202、一次 409，并检查对应运行 ID；另模拟进程中途留下 `starting` 文件，验证读取、启动拒绝、回退拒绝和删除后同 ID 拒绝。Node 用例验证 409 进入人工核对边界。有界发布门禁通过 64 个前端模块解析、38 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与门禁产物 SHA-256 同为 `d5fb2ad11286863f651abaee37eae11c756492ab39eed8be83a89f75c2c3d236`。该机制仅串行化同一服务进程内的请求；多个进程共享便携 Home 尚未建立排他锁，`starting` 在异常退出后的真实执行结果仍需人工核对。两窗口打包页交互尚未实测。

随后修正 `starting` 状态的队列卡手感：服务端队列 GET 对持有该凭据的 `sending` 项只读投影 `start_claimed: true`，文件 schema 5 不变；三语队列卡标明“启动结果待核对”，明确说明同一项不能再次发送，并隐藏“核对后重试”按钮。这样关闭输入区核对提示后也不会出现必然失败的重试入口，用户仍可核对记录后移除该项。运行探针确认投影与磁盘文件分离；发布门禁通过 64 个前端模块解析、38 项 Node 测试、114 项 Python 单元/契约测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与门禁产物 SHA-256 同为 `eb3338b8838110eeeba9ff260129c1f052c002b6370438f6d265f7ba139a84a1`。该卡片状态尚未完成打包页交互验收，跨进程共享 Home 仍无排他保障。

补上该阶段的打包页核对。隔离 Home `.build/mdo-packed-docks-1m0h4jxe` 中，将一条测试队列项置为 `sending` 并写入有效的 schema 2 `starting` 凭据，刷新真实单文件页面后可直接读到“启动结果待核对”和不可重试说明，只有移除按钮，没有“核对后重试”；服务端队列 GET 投影 `start_claimed: true`，磁盘 `queue.json` 仍为 schema 5 且无该字段。同一 Home 的另一会话打开两个独立标签，再经 API 放入一条 `pending` 的 `SLOW UI`；两页同时刷新后均先显示运行中，最终各自展示一条用户消息和一条本地模型回复。服务端记录一个已完成运行、一次 `agent_start`、空队列以及同一运行 ID 的 `accepted` 凭据。此页面测试验证的是双页同时发现待发项；两页都已取得 `sending` 后同时直发运行 POST 的窗口仍由前述并发 API 探针证明。测试只使用两页与一条合成消息，没有压力或高负载。此次未修改程序代码，根目录 `mdo.exe` SHA-256 仍为 `eb3338b8838110eeeba9ff260129c1f052c002b6370438f6d265f7ba139a84a1`；多进程 Home 排他、移动端该新卡片及实际崩溃恢复仍需后续验证。

本阶段收紧运行启动失败的恢复边界。运行管理器只在进入 Agent Start 前报告“确定未执行”；队列绑定的启动若在此前失败，API 撤销 `starting` 凭据，原 `sending` 项继续留在便携 Home，页面提供“确认未发送后重试”。一旦进入 Agent Start，失败也返回 `run_start_uncertain` 并保持人工核对屏障；撤销凭据失败同样不开放重试。失效模型配置现返回 `session_profile_invalid`，提示定位到会话配置。运行管理器探针覆盖已占满且未进入 Agent Start，API 探针覆盖同一队列 ID 从无效模型明确拒绝到恢复配置、退回、重试完成，Node 用例覆盖结果不明错误分类。

最终单文件 Home `.build/mdo-packed-docks-0h2k7ifv` 中，将一条合成 `sending` 项的会话模型临时指向不存在的 ID，运行 POST 返回 `session_profile_invalid`，磁盘无凭据，队列项仍为 `sending` 且没有 `start_claimed`。恢复模型后，打包页面显示“确认未发送后重试”；点击后只出现一条用户消息和一条固定回复，`queue.json` 归零，同 ID 凭据带一个 `run_id`，事件记录对应一轮 Agent 启动。发布门禁通过 114 项 Python 单元/契约测试、39 项 Node 测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与门禁产物 SHA-256 同为 `24162985a790c35f74d2a92e46f9a25eb55eb201ddb0a52999aa7ef7578892ec`。跨进程 Home 排他、实际崩溃留下的 `starting` 结果判定及移动端该路径仍需后续验证。

本阶段恢复旧设置中的自定义 CA：`settings.transport.ca_pem_path` 只接受便携 Home 相对路径，默认空值使用系统证书；模型客户端按需打开外部 Home 的 PEM 文件，限制 1 MiB、验证至少一张 X.509 证书，再把证书传给 xllm 的 TLS 校验器。设置页提供三语输入、预览、应用和刷新回放，HTTP 代理和防休眠仍另行设计。模型运行探针验证有效、损坏和缺失 PEM，配置探针拒绝 `../` 越界路径。

隔离单文件 Home `.build/mdo-packed-docks-3848xsf7` 将 `tests/gpu-ca.crt` 放入 `mdo-home/certs/test.pem`，页面将 `certs/test.pem` 预览、应用至 revision 2，刷新后输入值和 `/api/v1/settings` 一致；随后合成模型回合成功并显示 7 输入 / 3 输出 tokens。320px 页面中文档宽度 320px，CA 输入高 40px 且右缘 306px，设置操作按钮至少 40px，浏览器无错误日志。合成端点是 HTTP，因此该页面证据验证配置、PEM 装载和模型启动接线，不证明自定义 CA 的真实 TLS 握手。发布门禁通过 114 项 Python 单元/契约测试、39 项 Node 测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查；根目录 `mdo.exe` 与门禁产物 SHA-256 同为 `e2a5a7186d091de36fba1da4605c9c4c66e345d374dd6e8abdef6fd93fbe7118`。

随后核对旧版 CA 语义，修正了自定义 PEM 覆盖系统信任库的问题：模型客户端现在先取系统信任锚，再导入便携 Home 的 PEM，以引用计数之外的临时 X.509 store 交给 xllm 创建客户端；xllm 的 TLS verifier 在创建时克隆证书，临时 store 随后释放。运行探针比较合并前后锚数量，并保持有效、损坏、缺失 PEM 三条结果。发布门禁再次通过 114 项 Python 单元/契约测试、39 项 Node 测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查；根目录 `mdo.exe` SHA-256 为 `016a4e07d3fa340b7f403d382fdb3359412bb6e0932428e283cf3c65d2695094`。此修正不改变页面操作，真实自定义 CA 的 TLS 握手仍待单独验收。

本阶段恢复旧版模型代理入口：联网设置提供关闭、HTTP CONNECT、SOCKS5，以及主机、端口、用户名、密码引用和目标主机绕过规则。密码只通过 secret resolver 进入 xllm；设置 GET 仅呈现“已配置”状态，空输入保留既有引用，显式清除才写入 null。配置和模型探针分别覆盖非法地址、明文密码拒绝、两种代理客户端创建、缺失 secret 失败；API 探针覆盖密码引用不回传、局部更新保留和清除。代理目前只支持 HTTPS 模型端点，不影响 Web 搜索。真实外部代理隧道及移动端输入手感仍需单独验收。

隔离单文件 Home `.build/mdo-packed-docks-24y4l3wq` 的打包页面核对了代理字段：填入关闭状态下保留的主机、端口、用户名、`env:` 引用和绕过规则后，预览及应用成功，revision 从 1 到 2；刷新后普通参数回显、引用输入保持空白、状态显示已配置；勾选清除后预览及应用到 revision 3，状态改为未配置。再切至 SOCKS5 并清空主机，页面禁用预览并提示校验错误，放弃更改后恢复原值。最后将尚未配置密码引用时的清除开关隐藏，避免无意义的 null patch；已有引用且未清除时，清空用户名同样会阻止预览。末两项微调由重新打包的门禁覆盖。最终门禁通过 114 项 Python 单元/契约测试、39 项 Node 测试、19 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查，根目录 `mdo.exe` SHA-256 为 `4cd2ec4e83abd3c17f70edf34af632c9b9fc466735a42d74a92ca4245527b604`。

随后新增 `tests/test_model_proxy_runtime.py`，从 mdo 模型目录创建真实 xllm 客户端，请求本机 HTTPS Responses 端点：HTTP CONNECT 和 SOCKS5 各通过一条需用户名/密码认证的本地代理隧道，端点都返回 HTTP 200；SOCKS5 的 `127.0.0.1` 绕过规则则直连端点，代理连接数保持零。端点由仓库内仅供本地测试的 CA 签发，配置使用便携 Home 的 CA 路径，故同时证明自定义 CA 参与真实 TLS 握手。测试每种情况只发送一条合成请求，所有服务只监听回环地址。发布门禁把它纳入第 20 个运行探针，114 项 Python 单元/契约测试、39 项 Node 测试、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查仍通过；`mdo.exe` SHA-256 保持 `4cd2ec4e83abd3c17f70edf34af632c9b9fc466735a42d74a92ca4245527b604`。该证据验证本机 TLS 和代理协议，不代表公网或其他操作系统端点的可用性。

附件跨平台输入边界补强：系统文件或剪贴板给出空 `File.type` 或通用 `application/octet-stream` 时，若文件名明确以 `.png`、`.jpg`、`.jpeg` 或 `.webp` 结尾，前端为上传补齐对应 MIME；浏览器给出明确且不受支持的 MIME 时不会用扩展名覆盖。服务端继续核验真实文件签名，因此把文本改名为 `.png` 仍会收到 415。Node 用例覆盖未知 MIME、大小写扩展名及 MIME 冲突，图片运行探针覆盖假 PNG 被拒绝。生产模块浏览器夹具 `tests/fixtures/composer-upload-session-browser.html` 还以空 MIME 的 `capture.PNG` 触发真实上传路径，验证发出的 Content-Type 为 `image/png`、原 File.type 仍为空、附件进入草稿且没有错误。发布门禁通过 114 项 Python 单元/契约测试、20 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查，根目录 `mdo.exe` SHA-256 为 `d39e8234591a66c498ef44b63144d3a12b2adae017de7783f5279fc223c6983f`。操作系统原生拖放仍需打包页实测，不能以合成 DragEvent 代替。

中文输入法的候选边界补强：`@` 文件补全和 `/` 命令候选在 compositionstart 至 compositionend 之间暂停并取消旧查询，结束组字且输入框仍有焦点时才更新；失焦后即使补发 compositionend 也不弹出候选。发送键同时检查输入框的组字状态，避免平台没有正确设置 `KeyboardEvent.isComposing` 时误发。生产模块浏览器夹具 `tests/fixtures/composer-ime-browser.html` 以中文文件名和含空格路径验证组字期间零查询、结束后仅一条查询、选中后插入带引号路径；另验证斜杠候选和失焦边界。实际手机输入法与桌面中文输入法仍需物理设备验收。

新打包版另在独立 Home `.build/mdo-packed-docks-64_618t8` 中打开工作区文件 `notes/中文 文件.txt`：输入 `@中文` 显示候选，按 Enter 后草稿变为 `@"notes/中文 文件.txt" `；刷新会话页后草稿仍在，任务未启动。发布门禁通过 114 项 Python 单元/契约测试、41 项 Node 测试、20 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `51592ae7e2eaa7d468b0aabf1effb10f6f7a80148d99690663c115dcbd3b0041`。

320px 触控复测发现 `@` 文件与 `/` 命令候选行均只有 34px，现将移动端最小高度提高到 40px。修复后单文件 Home `.build/mdo-packed-docks-_mst52ou` 中，两种候选行实测 40px，菜单水平范围 x=10–310px，文档宽度保持 320px；点击 `/help` 打开帮助弹层，点击 `notes/中文 文件.txt` 插入带引号路径并保持输入焦点，浏览器脚本错误日志为空。发布门禁通过 114 项 Python 单元/契约测试、41 项 Node 测试、20 个运行探针及严格 C 编译、确定性打包、单文件零旁路写入、20 秒启动检查；根目录 `mdo.exe` SHA-256 为 `289fee9e165c5f80934f9c645933c0e30d85d4b042e80ca25e2bf583e4bdd074`。

候选键盘滚动修复：旧版打包页在 320×568 下打开 `/` 的九项命令，按八次方向键后 `aria-selected` 已到 `/help`，但菜单 `scrollTop=0`，选中项位于菜单底部之外。文件候选同样只改变选中项而不滚动。现在方向键只更新两项的选中状态，并将当前项滚入菜单可视区域，避免每次按键重建所有候选节点。修复后的单文件 Home `.build/mdo-packed-docks-83klbbut` 中，命令第九项可见，向上返回首项及首尾循环后仍可见，Enter 打开帮助；另建立九个同名前缀文件，八项候选中的最后一项滚动可见，Enter 插入 `@notes/scroll-08.txt ` 且输入焦点保留。页面宽度 320px、脚本错误日志为空。发布门禁通过 114 项 Python、41 项 Node、20 个运行探针及确定性打包、单文件零旁路写入和 20 秒启动检查；根目录 `mdo.exe` SHA-256 为 `1ad3c171c3cab47b6979922b15d93c015adeba1c7dad0fbc7bef9a3f4e5caa01`。

询问卡自由回答补齐输入法组字保护。此前该输入框只检查 `KeyboardEvent.isComposing`；生产模块夹具 `tests/fixtures/ask-ime-browser.html` 复现了该字段为 false 时，组字中的 Enter 提前把“你”发给询问 API。现在同时跟踪 compositionstart/end，组字时不响应提交键，失焦后清除状态。夹具修复后组字期间零请求，结束后仅一次提交完整“你好”。新单文件 Home `.build/mdo-packed-docks-saa053bu` 另在 390px 页运行 `ASK UI`，自由回答“中文答案”进入工具结果，刷新后展开仍可回放，输入和提交按钮均高 40px，文档宽 390px、浏览器脚本错误日志为空。发布门禁通过 114 项 Python、41 项 Node、20 个运行探针及严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查；根目录 `mdo.exe` SHA-256 为 `9a46ec9ca78f84d553f50cb042aaed35a8174d250b3b4a85a90d2878aaacf5f8`。物理输入法时序仍需设备实测。

短视口操作复测与消息编辑弹窗修补：单文件 Home `.build/mdo-packed-docks-7k1hg9ni` 在 320×350 模拟键盘占屏，询问卡可滚动到自由回答输入和提交按钮，提交“短屏回答”后卡片消失，工具结果出现，文档宽 320px、脚本错误日志为空。另在 Home `.build/mdo-packed-docks-cczvjeff` 复现消息编辑弹窗的固定 170px 文本框使底部操作区落到 y=411px、超出 350px 视口。现按动态视口高度收缩文本框；修复后的 Home `.build/mdo-packed-docks-ndva58ox` 中，320×350 的文本框高 84px、操作区底部 y=329px，不滚动即可点击取消并恢复原编辑按钮焦点；320×700 下文本框仍有 168px。发布门禁通过 114 项 Python、41 项 Node、20 个运行探针、严格 C 编译、确定性打包、单文件零旁路写入和 20 秒启动检查，根目录 `mdo.exe` SHA-256 为 `f7cf4c86a9949217da670ef97e942f0d014b300df65b4ec001e9f7048231cfbf`。浏览器视口缩放可验证布局响应，不能替代实体手机的软键盘行为。

Linux 打包页跨平台复核发现并修复一处时间线回放竞态：事件写者先追加 JSON、再追加换行，读者在两次写入之间看到未提交的尾行时，曾误报 `history_lost`，导致前端抛弃已展示的首条用户消息并显示虚假的记录缺口；刷新后账本仍能恢复消息。回放现在只处理换行结束的记录，忽略暂未提交的尾行；若进程崩溃留下尾行，重新打开会话时仍按原恢复流程补换行，再将坏记录报告为历史缺口。`test_session_runtime.py` 覆盖写入窗口 `lost:0` 与崩溃恢复后的 `lost:1`，`manual_packed_docks_qa.py` 可在 Windows/Linux 选择对应程序名。修复后的 WSL Ubuntu 单文件 Home `.build/mdo-packed-docks-dc3p_4u3` 在 390px 页执行 `ASK UI`，选择 `Careful` 后首条用户消息、询问调用、工具结果和最终回复均留在时间线，脚本错误日志为空。Windows 和 Linux 有界发布门禁各通过 114 项 Python、41 项 Node、20 个运行探针、严格 C 编译和确定性打包；Windows 另通过单文件零旁路写入与 20 秒打包启动检查。Windows 根目录 `mdo.exe` SHA-256 为 `6eab1d04d81c3fcbbfc91f232ebc256e7954573f21f305d0e67fcc87ba2c3f52`，Linux 打包 SHA-256 为 `f6430fbc5d2009f297a55f60908391df3abe98e0bc3e7fdf59be05f125267506`。WSL 页验证不代表原生移动端或 macOS 已验收。

输入法按键边界继续收紧：主输入框、询问自由回答、文件/斜杠候选及全局快捷键共用 `isImeKey` 判定，除 `compositionstart/end` 状态和 `isComposing` 外，也忽略某些 WebView 在候选确认时产生的 `keyCode=229`。这样候选确认不会误发送任务、提交询问、执行命令或触发全局 Esc 停止。Node 用例覆盖正在组字的 Esc、229 键及普通 Esc；生产模块浏览器夹具 `composer-ime-browser.html` 与 `ask-ime-browser.html` 覆盖 229 候选确认后继续用普通 Enter 完成原操作。Windows 单文件 Home `.build/mdo-packed-docks-gxzdft3r` 在 390px 下用普通 Enter 发送 `IME NORMAL QA`，出现一次用户消息和本地回复，输入焦点回到主框，脚本错误日志为空。Windows/Linux 有界发布门禁均通过 114 项 Python、20 个运行探针、64 个前端模块解析、Node 测试及确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `0324ed7aebaaf2b98361628e683e2c537aa751b73fefa71c952473b68d81d94e`，Linux 打包 SHA-256 为 `d54571d7a4a6a4f5942187dec33d526c11c2da26e0030d8c9a18144f1a2a84c9`。合成 CompositionEvent 不能替代原生手机或桌面输入法实测。

移动端可视视口补齐软键盘覆盖场景：当 WebView 的布局视口仍保持原高、`visualViewport` 却缩小时，应用容器改用可见区域的底边高度，使主输入区留在键盘上方；可视视口恢复、桌面断点或页面缩放时，移除覆盖值并继续使用原来的 `100dvh`。Node 用例覆盖缩高、平移、缩放和恢复；加载生产 CSS 与模块的浏览器夹具在 320×700 中测得容器高度依次为 700、385、405、700px，脚本错误日志为空。Windows 单文件 Home `.build/mdo-packed-docks-kovex5xi` 在 320×350 下普通 Enter 打开 `APPROVAL UI`，滚动后拒绝/允许按钮位于 y=124px 且高 40px；选择拒绝后工具错误结果和最终回复出现，焦点回主输入框，文档宽度为 320px，浏览器无脚本错误。Windows/Linux 有界门禁通过 114 项 Python、20 个运行探针、65 个前端模块解析、Node 测试和确定性打包；Windows 另通过单文件零旁路写入和 20 秒打包启动检查。根目录 `mdo.exe` SHA-256 为 `43c8f53fe2d53fc6de28b776263f5fd23b488b178bed5742d4db374dedd767f9`，Linux 打包 SHA-256 为 `01167dc5641971efc4f5a97b815d912420cd544984797258bf59fdd9c4dc3c4e`。浏览器夹具模拟可视视口变化，实体系统软键盘仍需设备验收。

接着补齐容器内的长草稿与候选高度：原 CSS 的输入框和文件/斜杠菜单仍按 `dvh`/`vh` 计算上限，布局视口 700px、可视视口 300px 的生产样式夹具中，发送按钮底边为 405px，菜单顶部为 -230px。移动断点现按可视高度给输入框保留顶栏和两行工具栏空间，并压低候选菜单上限；修复后的同一夹具中，20 行草稿的输入框高 80px、内部可滚动，发送按钮底边 283px，菜单顶部 60px。无键盘覆盖时仍为原来的 260px 输入框与 280px 菜单。独立单文件 Home `.build/mdo-packed-docks-q6r23cw9` 在实际 320×350 短视口输入 20 行和 `@README` 后，输入框高 130px、`scrollHeight=462px`，候选顶部 y=56px（手机顶栏底边 y=52px），发送按钮底边 y=333px；Tab 插入 `@README.md` 后焦点仍在主输入框，文档宽 320px、脚本错误日志为空。Windows/Linux 有界发布门禁通过 114 项 Python、20 个运行探针、65 个前端模块解析、Node 测试及确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` SHA-256 为 `709e87457deede9229735fb358647a659fb1d5996aa891d3cd2cd69ac24c5cfc`，Linux 打包 SHA-256 为 `1ad8d4acf73ee5fa5a401561009b7be2fd10ca7562209fa0819ec285a19d9ca4`。真实键盘覆盖后的系统视口时序仍需设备验证。

旧版/新版输入区并排复核后，恢复桌面端更宽松的写作空间：输入框常态最小高度提高至 74px，模型/思考/权限选择器和发送按钮加大，发送按钮恢复圆形；桌面输入条隐藏冗长快捷键提示（快捷键帮助仍在侧栏），761–1050px 再隐藏工作区名称，避免 960px 页面里名称被压成单个残字、模型选项与 token 估算挤在一起。工作区上下文仍可由会话标题栏的检查器打开；手机断点继续保留 52px 输入框起点和 40px 操作控件，短视口不会因桌面高度增加而失去发送按钮。单文件 Home `.build/mdo-packed-docks-7vql0m1k` 在 1280px、960px 和 320×350 下分别核对布局；320×350 输入 `COMPOSER VISUAL QA` 后估算约 5 tok，发送一次并出现用户消息、回复、7 输入 / 3 输出 tokens 和 token/s，输入焦点回到主框，浏览器错误日志为空。Windows/Linux 有界发布门禁均通过 114 项 Python 测试、65 个前端模块解析、Node 测试、20 个运行探针和确定性打包；Windows 另通过单文件零旁路写入及 20 秒打包启动检查。根目录 `mdo.exe` 与 Windows 发布打包 SHA-256 同为 `0b024eed01e850f0bd84642dbe33faae03f2cb80790688edaea1f4d394cafbc2`，Linux 打包 SHA-256 为 `11ab2ad6431fdadfdca1e89edd0fd02e8e726161632a7b3d075c0cdb5c09375c`。实体手机键盘与 macOS 仍需独立验收。

最终根目录程序还在独立 Home `.build/mdo-packed-docks-decs66r9` 的 1440px 打包页复核：工作区、模型、推理、权限、输入估算和发送入口同时可见，模型名称未再被快捷键提示挤压。

会话轨迹入口继续按旧版习惯增强：宽桌面标题栏的轨迹按钮增加可见的三语标签；窄桌面保留图标以给会话标题留空间。此前 960px 单文件页从该按钮打开检查器后，关闭按钮把焦点送到相邻的检查器切换图标，键盘用户需重新寻找原入口。抽屉现在记录实际开启控件，关闭时只在该控件仍连接、可见且可聚焦时还原焦点，否则回退到对应的通用入口。隔离单文件 Home `.build/mdo-packed-docks-kjc06h14` 在 1280px 确认中文“轨迹”文字，960px 验证关闭按钮与 Esc 都返回原轨迹入口，320px 验证手机检查器打开和关闭仍返回手机顶栏按钮。最终根目录程序 Home `.build/mdo-packed-docks-nf4qxra3` 再次确认 960px 的 Esc 往返，浏览器错误日志为空。Windows/Linux 有界发布门禁均通过 114 项 Python、65 个前端模块解析、Node 测试、20 个运行探针和确定性打包；Windows 另通过单文件零旁路写入及 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `a55cd662c0a85477e8057be307a0d6edca5565b1b204e51360ffeb730966d000`，Linux 打包 SHA-256 为 `8ffca46912f5ed95cc4ca80dd4b392482f771a873af2d6c82441fd070fb46430`。本轮未做压力或高负载测试。

同一操作链在断点变化时另暴露状态错位：960px 已打开轨迹抽屉，缩至 320px 后手机顶栏检查器按钮仍报告“未展开”。现在切换手机/桌面断点会同步当前可见入口的 `aria-expanded`，保持抽屉标签焦点，并在关闭时按当前断点选择可见的焦点回退目标。单文件 Home `.build/mdo-packed-docks-1d6xceq_` 复现旧错；修复后的 Home `.build/mdo-packed-docks-a9m_ir15` 验证 960→320 后手机按钮报告“已展开”、关闭聚焦手机按钮，320→960 后桌面按钮报告“已展开”、关闭聚焦桌面按钮；浏览器错误日志为空。最终 Windows/Linux 有界发布门禁通过 114 项 Python、65 个前端模块解析、Node 测试、20 个运行探针及确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `273561c110e0799d35be03f6e9d0c49d93e3e029977f538ddc1497ec2c508250`，Linux 打包 SHA-256 为 `af17a6286b68ab5d77dcde46e18a9124923a3a351898c9ddf616bce5ee19bbd2`。

新任务项目选择恢复旧版的独立位置：所属项目现在位于输入框上方，模型、思考、权限仍留在输入框底部；进入已有会话或设置页时项目控件收起。控制器继续以 URL 保存新任务项目，选择、刷新及首次发送不改变原有草稿和队列事务。隔离单文件 Home `.build/mdo-packed-docks-w5cfoyeu` 在 320×350 下实测项目控件 y=66–106、20 行草稿输入框 y=115–245、发送按钮 y=292–332，文档宽 320px；`@README` 候选出现，Tab 插入 `@README.md` 后项目控件重新可见且输入仍聚焦。向隔离 Home 新建 `Pill QA` 项目，选择后 URL 为 `/projects/pill-qa/new`，刷新保留项目与长草稿；发送 `PROJECT ROUTE QA` 后会话归属 `pill-qa`，收到一次用户消息和回复，项目控件收起，浏览器错误日志为空。Windows/Linux 有界发布门禁均通过 114 项 Python、65 个前端模块解析、Node 测试、20 个运行探针及确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `93a4542e66ec5453d28294e0d43ff72b334beee93fe1acd0a0795e1497500120`，Linux 打包 SHA-256 为 `48b7fd2d9ac4ad4c6b0e7a8fbf12605046c165138abee707ace73598ff0197f6`。未做压力或高负载测试。

模型配置删除确认恢复键盘层级：此前在确认区按 Esc 会直接退出整个设置页；现在确认区先消费 Esc，关闭提示并把焦点送回原“删除”按钮，与点击“取消”一致。隔离单文件 Home `.build/mdo-packed-docks-1grihjg5` 先复测 Provider 的 Esc 取消和确认；再创建引用该 Provider 的模型，确认 Provider 删除按钮禁用。模型确认区按 Esc 后设置页仍在、焦点返回；提交模型删除后 Provider 解除引用，随后可删除 Provider。配置 revision 从 1 升至 5，刷新后两个自定义项均消失，内置 Ling 模型和 Provider 保留，浏览器脚本错误日志为空。测试只使用 `example.invalid` 作为配置 URL，未向它发送模型请求。Windows/Linux 有界发布门禁通过 114 项 Python、65 个前端模块解析、Node 测试、20 个运行探针和确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `99610e9dafe254bf07e67fe5dac66dc549f6a89af0e02ccf55c62c94e033713b`。未做压力或高负载测试。

草稿超限与分栏异常反馈阶段：此前超过 64 KiB 的草稿会每 300 毫秒重新编码并再次报告同一失败，切换会话后错误还可能消失；现在保留未保存状态及稳定错误码，停止超限内容的定时重试，用户修改后恢复一次正常保存。草稿、分栏读取与保存失败的提示接入中英俄词典，语言切换时草稿错误即时重译；两个分栏宽度控制的无障碍名称也随语言切换。打包页首次复测发现已保存英语刷新后，分栏失败提示仍因初始化竞态显示中文；现让分栏读取等待设置视图实际加载所选语言的同一 Promise，不阻塞其余工作台启动。隔离单文件 Home .build/mdo-packed-docks-k2x03u1r 在英语和俄语应用、刷新后，设置 revision、分栏读取失败及宽度控件名称均显示对应语言；另在故障代理中触发英语分栏保存失败。320×350 的俄语页面输入 22000 个汉字产生超限提示，文档宽度保持 320px、发送按钮仍在视口内；缩短后错误隐藏，刷新保留短草稿，浏览器脚本错误日志为空。最终字节 Home .build/mdo-packed-docks-bnxj5ay0 复核保存英语后刷新，设置 revision 与分栏错误均显示英语，浏览器脚本错误日志为空。Node 用例验证超限无周期重试、跨会话错误保留和缩短后恢复保存。Windows/Linux 有界门禁均通过 114 项 Python、65 个前端模块解析、Node 测试、20 个运行探针和确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 mdo.exe 与 Windows 发布包 SHA-256 同为 bf19b5a4530934cdf9da7e4e846b741bc50e0f2a7a213e8ac130ffc3d4d5288b，Linux 打包 SHA-256 为 bf1712eaf04679cb678ab2c0f8b2cfb70341fa93f49b07c3567ee055dc02772f。未做压力或高负载测试。

会话切换滚动归属修复：此前在 320×350 的打包页把会话滚到历史顶部后，进入新任务再返回，会话仍停在顶部，最新回复和操作按钮需手动滚动寻找；新任务欢迎页还错误显示“回到底部”。时间线现于会话 ID 改变时重置跟随状态，已有会话落在最新回合，新任务从欢迎标题开始且不显示对话回底按钮；同一会话的普通更新仍保持用户当前阅读位置。隔离单文件 Home `.build/mdo-packed-docks-3o1igyz1` 以真实 `MARKDOWN UI` 回合复核：切走前时间线 `scrollTop=0`，返回后剩余距离为 0、Markdown 回复和消息操作可见；新任务 `scrollTop=0`、回底按钮隐藏，文档宽 320px，浏览器脚本错误日志为空。Windows/Linux 有界门禁均通过 114 项 Python、45 项 Node、65 个前端模块解析、20 个运行探针和确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `7befa93a1e8211d7796704a1dabc88566d35b07905e00bf1df2dcc87c90ca5b1`，Linux 打包 SHA-256 为 `3c08f1d870c1602c6b6ec79b77b9e7443ce8bab8cbd3e5f56aa1a97abc6d49ee`。未做压力或高负载测试。

短视口的输入与用量面板修复：单文件页在 320×350 切换俄语后，token 用量弹层高 247px、顶端 y=37px，被 52px 手机顶栏盖住；761×350 桌面窗口又被侧栏的最小内容高度撑到 505px，输入框落到视口外。现在主网格行允许收缩，侧栏在 480px 以下的桌面高度可整体滚动，同时为会话列表保留 126px；token 弹层按可视高度和顶栏高度限制自身高度，超出内容在弹层内滚动。最终单文件 Home `.build/mdo-packed-docks-2cf2boks` 在 761×300 验证工作区和侧栏均高 300px、会话列表高 126px、输入框底边 y=284px、用量弹层顶端 y=65px（顶栏底边 y=58px）；320×350 俄语弹层顶端 y=60px（手机顶栏底边 y=52px）、内部可滚动，文档宽 320px、浏览器脚本错误日志为空。中间版 Home `.build/mdo-packed-docks-wgvjn4dj` 还在 761×400 实测滚动侧栏后设置入口进入视口；最终版 1280×720 保持原桌面布局。Windows/Linux 有界门禁均通过 114 项 Python、45 项 Node、65 个前端模块解析、20 个运行探针和确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `06176f32fc7b959370953f82c6ff6af92816bfe70a4019139861bf3a612fb90f`，Linux 打包 SHA-256 为 `1e7d030e5e49a04b87fc957b9fe3884df782c5ae2395747f135f214473e5fb12`。未做压力或高负载测试。

归档与回收站消息操作对齐：此前会话进入回收站后输入框已禁用，但历史消息的编辑、重试、分叉仍显示，点击编辑还会打开“保存并重新发送”弹窗。时间线现在仅在当前已加载会话处于进行中时呈现这三种操作；复制、代码查看和反馈仍保留。执行入口在真正修改历史或分叉前再次核对会话状态，防止旧节点或弹窗跨越状态变化。生产模块夹具 `tests/fixtures/timeline-owner-browser.html` 验证归档、回收站隐藏入口，恢复进行中后入口回来且操作仍绑定原会话。隔离单文件 Home `.build/mdo-packed-docks-rujo5srg` 完成一轮 Markdown 回复后，桌面归档、移回进行中、回收站、刷新和恢复的状态链均通过；320×350 手机顶栏再次移入回收站，保留的复制与反馈按钮高 40px，三种历史变更操作消失，刷新后点赞状态保留，恢复后入口和可写输入框回来，文档宽 320px、浏览器脚本错误日志为空。Windows/Linux 有界门禁均通过 114 项 Python、45 项 Node、65 个前端模块解析、20 个运行探针和确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `6d425bc13cfc4ba7b74fa9337088caf05eda4bee2fd12b7fbfac109fae4dc8fc`，Linux 打包 SHA-256 为 `21c5a564a3d1d8eee5d7d3b481732daafcec51f5367929abdca1b1eb97079a40`。未做压力或高负载测试。

归档/回收站中的待发送队列状态对齐：先前会话输入框已不可写，但持久队列仍呈现“发送下一条”或“当前任务结束后自动发送”，点击后服务端才以 `session_state_conflict` 拒绝。队列现在从当前会话状态决定是否呈现发送/重试入口；非进行中时保留队列项、附件查看和移除，明确提示“恢复到进行中后继续发送”。旧按钮跨状态点击也被挡住；从回收站恢复到原归档态不会派发，移回进行中时立即重新核对并接力队列。生产模块夹具 `tests/fixtures/queue-session-lock-browser.html` 验证会话切换锁、只读状态、旧按钮和恢复入口。单文件 Home `.build/mdo-packed-docks-5k5p2w_8` 将一条 `pending` 项置于归档会话：桌面归档与回收站均显示待发送 1、无发送入口；恢复到归档仍保留待发送 1，移回进行中后只产生一条成功运行，队列为 0。另一独立归档会话在 320×350 显示相同提示，文档宽 320px、移除按钮高 40px、浏览器脚本错误日志为空。Windows/Linux 有界门禁均通过 114 项 Python、45 项 Node、65 个前端模块解析、20 个运行探针和确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `fff808f34bf24582b2ce30dbb4c0f16d8c8039a2708e8350d759bfb818f5647a`，Linux 打包 SHA-256 为 `36ae3c3ff6de20723a59d080fd24b213312cf09d6df80cef1e3101ac09d1891a`。未做压力或高负载测试；跨标签远端变更状态的即时同步尚未实测。

当前会话的跨客户端状态同步：另一个客户端归档、移入回收站或恢复已选会话时，页面此前保留旧元数据，输入框与侧栏可能继续按旧状态显示。可见的工作区现在每 8 秒有界读取一次已选会话，只在服务端 revision 增加时发布新状态并刷新侧栏；从后台重新可见时立即核对。异步响应须匹配当前项目、会话与较新的 revision，避免切换会话或本地修改后被旧响应覆盖。远端恢复到进行中还会重新核对待发送队列；从设置返回原会话则立即刷新详情和侧栏，不等待轮询。Node 三项用例验证归档更新、旧响应隔离、并发读取合并及恢复回调。单文件 Home `.build/mdo-packed-docks-7ix9ll1a` 中，外部 API 客户端归档 revision 1→2 后，可见桌面页自动禁用已有 `SYNC DRAFT UI` 草稿的输入与发送，侧栏进行中计数变为 0；远端移回进行中 revision 3 后，草稿原文与输入入口恢复。320×350 下外部移入回收站、恢复也同步切换手机标题与只读状态，文档宽 320px，无浏览器脚本错误。最终单文件 Home `.build/mdo-packed-docks-71a4fo9e` 在设置页打开期间由外部归档，点击返回后立即显示已归档、禁用输入，并把侧栏进行中计数从 1 更新到 0。Windows/Linux 有界门禁均通过 114 项 Python、48 项 Node、66 个前端模块解析、20 个运行探针及确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `8f3dde782d79ef348035689c394a9467b809307c0a33d7f6bd6769886c4147e4`，Linux 打包 SHA-256 为 `13b638ade5bbe5f0d184bcd687772e6c827868816a59d48799802210d480a817`。此项使用外部 API 客户端模拟另一页面的修改；两张真实浏览器标签同时操作及实体手机仍待独立验收，未做压力或高负载测试。

两张真实标签的队列恢复验收：单文件 Home `.build/mdo-packed-docks-lqynlse_` 预先为归档会话保存一条 `pending` 的 `TWO TAB QUEUE UI`，然后分别在两张浏览器标签打开同一会话。B 标签通过会话菜单移回进行中并派发；A 标签稍后自动更新为可写状态，已消费的队列卡消失，时间线出现同一条用户消息与固定回复。两页分别刷新后仍各只显示这一轮，浏览器脚本错误日志均为空；服务端队列为 0、该会话只有一条 `succeeded` 运行。本项证明真实双标签下由其中一页恢复会话时，另一页能收敛到同一持久结果；两页真正同时直发运行 POST 的窄竞态仍由并发 API 探针覆盖，不能以此页面测试代替。未做压力或高负载测试。

输入候选的触控执行时机修复：此前 `/` 命令和 `@` 文件候选在 `pointerdown` 就执行，按住候选准备滚动时可能误选；现在按下只保留输入焦点，完整 `click` 才执行。生产模块夹具 `tests/fixtures/composer-menu-click-browser.html` 分别验证模拟 touch `pointerdown` 时命令和文件输入均未改变、随后的点击才打开 `/help` 或插入带引号的文件引用。最终单文件 Home `.build/mdo-packed-docks-7pwp45a6` 在 320×350 浏览器页实际点击 `/help` 候选打开帮助弹层，再点击 `notes/QA notes.txt` 插入 `@"notes/QA notes.txt" `，输入焦点仍为 `prompt`，文档宽 320px，浏览器脚本错误日志为空。Windows/Linux 有界门禁均通过 114 项 Python、48 项 Node、66 个前端模块解析、20 个运行探针及确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `9eae84abaf4748ca7d521011e2992ab6d2f8d6e67ca197d3b1cb19cb64f8c728`，Linux 打包 SHA-256 为 `303f3dd0c7df1b1074235a1a836c463c33de427ff4487a1377cfa03596d9d432`。真实手机手指滚动候选时的取消时序仍需实体设备验收，未做压力或高负载测试。

文件误放边界：输入区开始接受系统文件拖动后，若在输入区外松手，部分浏览器的默认文件打开行为可能替换页面并打断未发送草稿。现在窗口只拦截未被其他目标处理的 `Files` 拖放，输入区内部仍按原逻辑接收图片，其他已处理拖放目标保持自己的事件；拖离时清除输入区高亮。生产模块夹具 `tests/fixtures/composer-drag-browser.html` 验证输入区外拖过/放下均被取消、落点高亮清除、原草稿和 URL 保留；输入区内部嵌套拖入/离开/放下仍运行，其他目标自行接收文件时不被重复处理，浏览器脚本错误为空。最终单文件 Home `.build/mdo-packed-docks-agj5w26c` 确认页面加载、未发送草稿及脚本错误为空。该浏览器夹具使用合成 `DragEvent`；操作系统原生文件拖放仍需实体环境验收，不将夹具写作原生通过。Windows/Linux 有界门禁分别通过 114 项 Python、48 项 Node、66 个前端模块解析、20 个运行探针和确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `ed92ace31e0ea1f84bee543e422e3db76f1a415755c7fd309611cf6923149359`，Linux 打包 SHA-256 为 `ea08299ff8fe877f127073af1db6ca999ca3f16b85a16d3621edd3b86017f94c`。未做压力或高负载测试。

跨标签直接发送的缺陷追踪（修复前）：在 `.build/mdo-packed-docks-o34ffnp5` 中，以两个标签同时提交不同文本，第二标签的 `draft_conflict` 使未持久化提交留在内存，刷新后消失。当时仅完成复现、API 状态核对和事务设计记录；详见 `docs/cross-tab-submission-race.md`。代理总计一次队列 POST 和一次运行 POST，无浏览器脚本错误；不能用“一次运行”误判两条输入均安全。该缺陷的修复与回归见下文。未做压力或高负载测试。

跨标签提交的修复：会话草稿增加按稳定 ID 原子新增、状态转移和删除提交意图的 API，保留旧 revision 对普通草稿 PUT 的冲突检测。客户端在普通 PUT 失败时用同一 ID 核对或追加；收到持久化确认后才清空输入，完全无法核对时保留原文。删除意图时，服务端在同一事务清理与该意图完全匹配的草稿文本及附件；人工恢复先删除意图再重新保存原文。API 探针覆盖双请求、幂等与匹配清理，Node 覆盖旧标签、响应丢失、离线保留与人工恢复。

最终单文件 Home `.build/mdo-packed-docks-cdujm5me` 的两张真实页面同时提交 `TWO TAB FINAL A/B`，刷新后两页各保留 A/B 两轮回复，输入框、草稿、提交意图和队列均为空；服务端 2 次成功运行，代理 3 次队列 POST、2 次运行 POST，额外队列请求没有产生额外运行，两页无脚本错误。Windows/Linux 有界门禁分别通过 114 项 Python、52 项 Node、66 个前端模块解析、20 个运行探针和确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `99b83899a04d6a2732ee508501b54c540f0ed10d8450dbf2d20274de45209408`，Linux 打包 SHA-256 为 `a26c5b907689edfbf206eabd0cb508088ab6a3ac1d13b1277f454b5c4e071834`。未做压力或高负载测试；多进程共享 Home 的排他仍待独立设计。

双标签发送的实时收敛补修：前一阶段刷新后能恢复，但在另外两次两页同时提交中，运行已经完成的一页仍显示原输入和草稿冲突。临时日志定位到 `flush()` 发出清空草稿的 PUT 时暂时把 `dirty` 置为 false；并发 GET 的旧草稿响应因此误走“恢复输入”分支。现在保存过程中保留本地输入所有权，并忽略低于当前 revision 的响应。提交 ID 若已被其他标签推进或消费，页面还会用同一 ID 的草稿、队列项或已接受运行凭据延迟确认，避免确认未回到原标签时产生重复输入。另一处运行竞态中，第二条消息已标记 `sending`，启动明确收到 `session_busy` 且无启动凭据；页面现在在服务端受锁保护的状态转移下将它退回 `pending`，由现有派发流程继续，不把明确未接受的运行误标为待人工核对。核对后无提交意图时同步清除过期的人工核对提示；若已有启动凭据，仍保持人工核对，不自动重试。

最终单文件 Home `.build/mdo-packed-docks-_hyg7dxr` 从两张真实页面同时发送 `FINAL FLOW A/B`，两页不刷新即各显示 A/B 两轮回复，输入框、队列、草稿提交意图和核对提示均清空；刷新后结果保持。服务端 2 次成功运行、队列为空、代理 2 次队列 POST 和 2 次运行 POST，两页浏览器脚本错误为空。Node 用例覆盖保存期间旧响应不得恢复输入、延迟凭据确认及他页取得提交意图时不重复入队。Windows/Linux 有界门禁分别通过 114 项 Python、57 项 Node、66 个前端模块解析、20 个运行探针和确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `656d2417a1b19f0a076fc086c0a9f37d64daaaeda740a6c072309815fad3b259`，Linux 打包 SHA-256 为 `0bf207d0b98990ac3e5503e8fee655612a451885e948c004b83bb2955b82d3be`。本项只验证同进程双标签，不代表多进程共享 Home、macOS 或实体移动端通过；未做压力或高负载测试。

正常发送状态提示修复：旧包 Home `.build/mdo-packed-docks-6b_uz6_8` 中，普通消息刚点击发送就短暂显示“发送状态待确认”和“首条消息可能已被服务端接收”，即使随后成功；这会把正常运行误报成需要人工核对。现在队列由当前页面的独占发送流程持有时显示“正在发送…”，隐藏核对警告和重试入口；独占流程退出后若持久化队列仍为 `sending`，原来的核对警告和重试限制继续生效。三种语言均有发送中提示。生产模块浏览器夹具 `queue-session-lock-browser.html` 验证本页正常发送、所有者退出后的核对提示，以及切换会话时的操作锁边界。

最终单文件 Home `.build/mdo-packed-docks-4vonymti` 使用 5 秒有界运行接入延迟：点击发送后队列显示“正在发送…”，没有不确定警告或重试按钮；完成后队列消失，用户消息和 Agent 回复进入时间线。该页输入 `/export` 后浏览器报告 `mdo-Packed docks QA.md` 下载启动并完成，共 213 字节；脚本错误日志为空。Windows/Linux 发布门禁通过 114 项 Python、57 项 Node、66 个前端模块解析、20 个运行探针及确定性打包；Windows 单文件零旁路写入和启动检查也通过。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `92334d888f2c904d7b417e92f50d7be6f1b89e5931e73e2b59e808de82523eca`，Linux 包为 `7bb0eecfd98acf67eaed01702cea843add092c6877f5abe5db69d8e5d129e346`。未做压力或高负载测试；跨进程共享 Home、macOS 与实体手机仍待单独验收。

移动端侧栏会话菜单修复：旧单文件 Home `.build/mdo-packed-docks-eefs73yb` 的 390×500 视口中，菜单实际位于 y=297–617，而侧栏会话列表在 y=300 截断，九项操作几乎全部不可点；320×350 视口下，页脚又把会话列表挤到 0 高。现在会话操作菜单挂到页面浮层，按视口剩余空间向上或向下展开，菜单自身可滚动；Esc、点击外部、执行动作后的焦点返回继续有效。低高度手机侧栏改为可滚动，同时会话列表保留 126px 高；滚动侧栏时关闭菜单，避免浮层失去锚点。生产模块夹具 `tests/fixtures/session-menu-viewport-browser.html` 验证视口内可见、底部操作可达、Esc 焦点返回和动作执行。

最终单文件 Home `.build/mdo-packed-docks-nrrezjkq` 在 320×350 下，列表可滚到完整 40px 会话按钮，菜单展开后位于视口内且能滚到“导出 JSON 备份”；点击该项后浏览器下载 `mdo-session-…json` 2,225 字节并报告完成，脚本错误为空。滚动整个侧栏使菜单收起，页面宽度保持 320px。桌面端同包验证菜单展开、置顶后列表刷新及焦点回到操作按钮；此前中间包在 390×500 已核对菜单不再被侧栏列表裁切。Windows/Linux 有界门禁通过 114 项 Python、57 项 Node、66 个模块解析、20 个运行探针及确定性打包；Windows 单文件零旁路写入与启动检查通过。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `670e431d5035bd4bc06cec603697a18133f892559770c74a3f98de7f1e1cf789`，Linux 包为 `e1cbae048188f4e56791f4e03f5b581db2bd98860e7ee23fc455270f3cf8d8b0`。未做压力或高负载测试；实体移动端和其他系统的触控仍需验收。

对话卡的滚动跟随修复：旧单文件 Home `.build/mdo-packed-docks-h_3azqgk` 在 390×500 的已有会话出现新审批卡时，操作按钮落到对话可视区下方，而“回到底部”仍隐藏；新卡片高度变化未通知时间线的滚动控制。现在待办、询问、审批卡重排时，先记录用户是否停在底部；停在底部就跟随新内容，主动上滚则保留阅读位置并更新回底按钮。生产模块夹具 `tests/fixtures/conversation-dock-scroll-browser.html` 用真实时间线和卡片模块验证这两种状态，几何断言及脚本日志通过。新单文件 Home `.build/mdo-packed-docks-opbnj52v` 依次发送 `TODO UI`、`APPROVAL UI`，390×500 下审批卡位于 y=103–316 的对话可视区 y=52–337 内；点击“允许一次”后无害命令返回 `exit_code: 0`，回复与工具结果进入时间线，浏览器脚本错误为空。Windows/Linux 有界门禁分别通过 114 项 Python、57 项 Node、66 个模块解析、20 个运行探针及确定性打包；Windows 单文件零旁路写入和启动检查通过。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `2f682bf99e43752eb369f0b6f6bf46a9d0fea453b4eea5c904be065062305442`，Linux 包为 `5e720e0d03701d2cabfdb7e3b70045e733ee9d13d5f25b1334b6ef49258a9b89`。未做压力或高负载测试；实体手机触控仍待设备验收。

审批卡内部展开的滚动补修：前一轮只监听卡片数据重排，现有卡片的 `<details>` 展开仍会在停留底部时把操作按钮推到可视区外。旧包 Home `.build/mdo-packed-docks-jxse3032` 于 390×500 展开“查看调用参数”后，允许按钮下缘 y=347 超过对话区下缘 y=337，滚动位置仍为 314/358 且回底按钮隐藏。现在观察卡片容器的实际尺寸；内部参数或询问提示改变高度时，时间线按已有跟随状态调整滚动和回底按钮，数据重排仍使用变化前的明确位置。生产模块夹具 `tests/fixtures/conversation-dock-scroll-browser.html` 增加参数展开的几何回归。最终单文件 Home `.build/mdo-packed-docks-5zwlz9ii` 同尺寸展开后滚动位置为 358/358，允许按钮下缘 y=303，仍在对话区内；点击允许后隔离无害命令返回 `exit_code: 0`，焦点回到输入框，两页脚本错误日志均为空。Windows/Linux 有界门禁分别通过 114 项 Python、57 项 Node、66 个模块解析、20 个运行探针和确定性打包；Windows 单文件零旁路写入及启动检查通过。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `91f55eed5cc8369378308bb81bdfe1fc6b37717d6813d3c76da446879b0e4663`，Linux 包为 `f5d82a40c5ec7220c612cb2eefba7ed4b06af5a45406b71d1cbdc67792c0d903`。未做压力或高负载测试；实体手机触控仍待验收。

待办、询问和审批已恢复旧版“输入区上方停靠位”的结构。前两次滚动补丁仅处理卡片仍在消息滚动区的症状；Linux 单文件基线 Home `/home/ubuntu/.cache/mdo-linux-qa-73d848b/mdo/.build/mdo-packed-docks-b8kx8nce` 于 320×350 显示询问正文 y=38–56、审批标题位于 y=-47 起的卡片中，均被顶部 y=52 的对话边界遮住，虽选项和允许按钮可点，用户读不到决策依据。现在停靠位覆盖在输入区上方，自身高度受消息可视区限制并独立滚动；新待决项滚到自身标题，待办保留在其前方。窄屏待决期间临时收起已禁用的模型、推理和权限控件，保留附件与发送操作，决策结束后控件恢复。原来的时间线与停靠卡高度同步代码已移除，`tests/fixtures/conversation-dock-scroll-browser.html` 改为生产模块停靠位几何回归：320×350 和 390×500 均验证问题、选项和审批标题先可读，内部滚动能到达按钮及展开参数，文档不横向溢出。

最终 Windows 单文件 Home `.build/mdo-packed-docks-xy1hs8e0` 在 320×350 下，询问卡完整位于 y=54–221，问题、选项及自由回答全部可见；模拟视口缩至 320×250 后，自由回答输入能滚入 69px 高的停靠位并成功提交，模型控件在决策结束后恢复。中间版 Home `.build/mdo-packed-docks-66b1m8n2` 验证待办共存时新询问优先可读、审批标题与命令先显示，向下滚动一次能执行“允许一次”，工具结果 `exit_code: 0`，焦点回到输入框。最终 Linux 单文件 Home `/home/ubuntu/.cache/mdo-linux-qa-73d848b/mdo/.build/mdo-packed-docks-9u11ds2z` 经 Windows 浏览器连接 WSL 服务在 320×350 重复待办→询问→审批：询问正文 y=93–111、两个选项及自由回答可见；审批标题 y=65–83、命令可读，内部滚动后允许按钮下缘 y=208 小于停靠位下缘 y=221，结果 `exit_code: 0`，脚本错误为空。Windows/Linux 最终有界门禁分别通过 114 项 Python、57 项 Node、66 个模块解析、20 个运行探针与确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `db78d8163fa87259642cf021d5ecffe58462bf0f426632700f3b5dda79ab60c7`，Linux 包为 `316c5f2acddda2b435d6a5f44787e33928d8e5c6248bfb7c6e72429c462de2c9`。WSL 服务加 Windows 浏览器不等于原生 Linux WebView，缩小浏览器视口不等于实体手机软键盘；未做压力或高负载测试。

外观设置恢复旧版选择即预览的手感：主题、文字大小和界面密度在表单变化时立即应用到当前页面；“放弃更改”恢复服务端快照，保存仍需原有预览校验及配置事务。旧单文件 Home `.build/mdo-packed-docks-k6nhzhf8` 在 320×350 选深色后页面仍是系统主题；新 Windows 单文件 Home `.build/mdo-packed-docks-6o7q0bcp` 中，深色/大字号/紧凑密度即时反映为页面属性，放弃后均回到系统/标准/舒适。随后选择浅色，预览和应用使 revision 从 1 到 2，刷新后仍为浅色；页面宽度保持 320px，浏览器无脚本错误。Linux 单文件 Home `/home/ubuntu/.cache/mdo-linux-qa-73d848b/mdo/.build/mdo-packed-docks-85j_takz` 经 Windows 浏览器访问 WSL 服务，在相同尺寸复核即时预览和放弃回退；这不等同于原生 Linux WebView。Windows/Linux 有界门禁各通过 114 项 Python、57 项 Node、66 个前端模块解析、20 个运行探针及确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `111d50e30721011f2780a069c162abb0ea7758085037a38143c7dd956c906ee7`，Linux 包为 `b019d8898bfff71c7060b34a07918f35dfc43f60d21167d8f736d589ded2c73b`。未做压力或高负载测试。

长草稿叠加待决卡的短屏操作补强：旧单文件 Home `.build/mdo-packed-docks-x8hfal7c` 在 320×350、大字号、待办与询问并存且输入 20 行草稿时，停靠位只有 y=52–143，首个选项下缘 y=159 被裁切。现在手机端仅在询问或审批待决时把输入框限高 52px 并允许内部滚动，决策结束后保留草稿并恢复原高度。另修正桌面宽度出现卡片后缩小视口的定位时序：最大高度更新后的下一帧才判断卡片可见性；用户主动滚动查看待办时不抢回滚动位置。生产模块夹具 `tests/fixtures/conversation-dock-scroll-browser.html` 在 320×350 和 390×500 模拟高视口到短屏收缩，核对询问正文、首选项、审批标题和按钮、草稿恢复及横向宽度。最终 Linux 单文件 Home `/home/ubuntu/.cache/mdo-linux-qa-73d848b/mdo/.build/mdo-packed-docks-sra9intc` 由 Windows 浏览器访问 WSL 服务：桌面出现询问并输入 20 行草稿后缩至 320×350，停靠位 y=52–221，正文 y=93–111，首选项 y=119–159，输入框高 52px、文档宽 320px；手动滚回待办再缩至 320×330 时滚动位置保持 0，随后仍能滚回选项并完成回答，草稿与输入焦点保留。前一中间包 `.build/mdo-packed-docks-frqaom3w` 同样验证桌面出现审批后缩屏，标题与命令先可读，允许一次后结果包含 `exit_code`，草稿恢复。最终 Windows/Linux 有界门禁分别通过 114 项 Python、57 项 Node、66 个模块解析、20 个运行探针和确定性打包；Windows 单文件零旁路写入及 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `045e7926b4c7f33012b8a27243e49e0a602d5880331125c9f1a43a90190b741b`，Linux 包为 `82e8c76b16dfd961c9d2df8d2369f1090efaa760e7cd997d3b1a101ebbd47d1c`。浏览器脚本错误为空；缩小桌面浏览器和 WSL 服务不等于实体手机软键盘或原生 Linux WebView，未做压力或高负载测试。

移动端新决策到达时的抽屉遮挡已修复。旧包 Home `.build/mdo-packed-docks-1eybwy2u` 在 320×350 打开“任务”检查器后收到 `ASK UI`，虽然询问卡已生成，抽屉和遮罩仍覆盖它。新包 Home `.build/mdo-packed-docks-eo9hwbk_` 使用有界 5 秒本地模型延迟，在同样顺序下自动收起抽屉，询问标题与选项可见，标题获得键盘焦点；提交 Fast 后回复及 token/s 正常。随后在“决策”检查器打开时触发 `APPROVAL UI`，抽屉保持开启，待处理计数更新为 1，决策页可直接允许，无害命令继续执行。另一隔离 Home `.build/mdo-packed-docks-47zp7p6b` 在“任务”页打开时收到审批，抽屉自动收起、遮罩消失，审批标题位于 y=65–84 的可见区且重绘后焦点仍在标题，允许后请求清空，浏览器脚本错误日志为空。生产模块夹具另外验证新询问/审批只触发一次到达通知、审批标题焦点经卡片重绘保持，以及 320×350 的决策按钮可达。`tests/manual_packed_docks_qa.py` 新增仅供隔离测试的 `--model-delay-ms`（0–5000，默认 0），以稳定复现决策到达与抽屉打开的时序。Windows/Linux 有界门禁分别通过 114 项 Python、57 项 Node、66 个前端模块解析、20 个运行探针与确定性打包；Windows 单文件零旁路写入及启动检查通过。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `12328369b4b0004ef597e30435538d37d4269adf148398e589ed36be4507b632`，Linux 包为 `db5178e9c0894539d2def9d2b239d517d4396e69aa23cf13c150ac0681172784`。实体手机触控及原生 Linux WebView 仍需验收；未做压力或高负载测试。

手机“决策”抽屉的新审批定位继续补齐。旧单文件 Home `.build/mdo-packed-docks-qlnfn8_z` 在 320×350、决策页已打开时收到 `APPROVAL UI`：审批标题位于 y=233–252，但“允许一次”位于 y=408–448，超过抽屉底边 350px，焦点仍在标签。新单文件 Home `.build/mdo-packed-docks-ti6dfyx6` 对同一有界时序按审批 ID 定位面板卡片，面板滚到 `scrollTop=168`：标题 y=65–84、允许按钮 y=240–280 都在视口内，焦点落到标题；允许后本地无害命令完成、待审批归零、输入焦点恢复，脚本错误为空。`tests/fixtures/conversation-focus-browser.html` 加载真实审批模块后，聚焦标题并用 F2 重绘，焦点仍在同一审批 ID 的标题，新节点已连接。已打开“决策”页时保持抽屉；其他抽屉的自动收起沿用上一阶段。Windows/Linux 有界门禁分别通过 114 项 Python、57 项 Node、66 个模块解析、20 个运行探针与确定性打包；Windows 单文件零旁路写入和启动检查通过。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `f05b8d9b9998532e72ec093e3e7b90c58fd617b2d0943f71ad8f8348e6095809`，Linux 包为 `83ffc57c9e0dc2e2efb86199788763d642bbed331277584a78b6e07732a08962`。实体手机触控及原生 Linux WebView 仍需验收；未做压力或高负载测试。

空白任务示例的首屏可达性按旧版手感收紧：旧版离线演示在 1280×720 一眼可见五张示例卡；上一单文件 Home `.build/mdo-packed-docks-7idmbieq` 的第五张“建个定时任务”位于 y=508–600，而对话滚动区底边仅 y=533，必须额外滚动才能发现。卡片原最小高 92px，现改为紧凑的 66px，缩小内边距及介绍文字后的间隔，不更改示例动作。最终单文件 Home `.build/mdo-packed-docks-7kteidt7` 在 1280×720 的第五张下缘为 y=514、960×720 为 y=500、761×720 为 y=524，均在 y=533 的对话区内；桌面第一张高 66px。320×350 手机端单列卡片高约 65px，文档宽 320px、发送按钮底边 y=332；滚动后点击第五项确实进入计划创建页，浏览器脚本错误为空。旧版 320×350 离线演示把输入区放到视口之外，因此短手机视口沿用新版的输入优先、示例可滚动策略。Windows/Linux 有界门禁分别通过 114 项 Python、57 项 Node、66 个前端模块解析、20 个运行探针及确定性打包；Windows 单文件零旁路写入与启动检查通过。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 同为 `abf39b5f91a1a38c377162f338a5bb27f33f7b9566531577eb52f7d5b8f0b9df`，Linux 包为 `683a1f22293f709727fc0a2eaa93367581704c6fb9f7829f5203990674f1d77e`。未做压力或高负载测试。

设置页焦点流转补验：隔离打包 Home `.build/mdo-packed-docks-_wb4qymd` 复现从输入框执行 `/settings` 后焦点落在页面根节点，点返回后也没有回到输入框。现在进入设置时将焦点落到“设置”标题；仅在离开时焦点仍属于设置区的情况下恢复输入焦点，分区按钮导航不被抢焦。最终单文件 Home `.build/mdo-packed-docks-ogfrxd3d` 在桌面和 320×350 视口确认 `/settings` → 设置标题 → 返回 → 输入框的焦点顺序；切换 Agent 分区后焦点保留在分区按钮。320px 页面无横向溢出，浏览器脚本错误为空。Windows/Linux 有界门禁分别通过 114 项 Python、57 项 Node、66 个前端模块解析、20 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `66d4b99bd11f890642db7044553b81d0dc19c86581d04df08d5fded10fa0011b`，Linux 包为 `87a0fbfa761e6b4e365a11af34a60c856327e17d140c5d7d0c34b80d005ed9ae`。原生手机软键盘和 Linux WebView 仍待验收；未做压力或高负载测试。

短屏命令与文件补全菜单现在按输入卡上方的实际空间限高，而不是用固定视口扣减。旧单文件 Home `.build/mdo-packed-docks-bvh5ucju` 在 320×350 输入 `/` 时菜单仅高 56px，九个命令同时只能看到约一项；最终 Home `.build/mdo-packed-docks-_wtfqetd` 同尺寸菜单高 124px、顶边 y=61，位于 52px 手机顶栏下，可同时看到约三项并滚动点选末尾 `/help`。`@alpha` 的两项文件候选完整显示在 92px 菜单内；761×300 桌面短屏菜单顶边 y=67，不遮住 58px 顶栏。文档宽度等于视口，浏览器脚本错误为空。输入卡、外壳和可视视口变化时会重新测量菜单可用高度，服务端补全逻辑未变。Windows/Linux 有界门禁分别通过 114 项 Python、57 项 Node、67 个前端模块解析、20 个运行探针及确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 为 `a7a11ff97ce504e8d461aba14a298ff151fb1e1fb32e3025f263dd7104f6cb19`，Linux 包为 `8c93549309930a59a8c168f34d4eb2973c7bb2b0b5f75d6c6a503f00cb228669`。实体手机软键盘和原生 Linux WebView 仍需验收；未做压力或高负载测试。

长草稿中的补全菜单继续压实：上一阶段虽按输入卡位置限制了菜单高度，但旧单文件 Home `.build/mdo-packed-docks-5g_uk3vu` 在 320×350 输入 20 行草稿再输入 `@alpha` 时，输入框高 130px、菜单仅高 46px，一项 40px 候选也显示不全。最终 Home `.build/mdo-packed-docks-6ckcn0ou` 在文件或斜杠候选展开期间临时把输入框限高 52px：同一草稿下菜单高 92px、两项文件候选完整显示；点击第二项后原 21 行草稿保留，末行变为 `@src/alpha-test.c `，输入框恢复 130px、焦点和光标仍在输入框。Esc 取消后原草稿及高度同样恢复；候选打开时在 320×350 与 390×500 间切换，可用空间随之更新，文档宽度等于视口且无浏览器脚本错误。Windows/Linux 有界门禁分别通过 114 项 Python、57 项 Node、67 个前端模块解析、20 个运行探针和确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `4684b48eca6342c61d4a65f1cb40a82a6456a4678046bf5e0a92aab20f5aaeb4`，Linux 包为 `e45650e4d576e096b58c36dbf469f0263d66f6c26f8dcd24f75b762e9d65526d`。实体手机软键盘与原生 Linux WebView 仍待验收；未做压力或高负载测试。
Home 多进程边界按便携目录实现排他：外部 Home 首次挂载时在目录内创建 `.mdo.lock` 并持有非阻塞 OS 文件锁，第二个 mdo 进程收到明确的初始化失败；页面常驻显示“便携数据目录正由另一个 mdo 进程使用”，桌面及 320×350 窄屏均可见，同时禁用输入、发送和图片添加。先前占用页面还会显示重复的草稿读取错误，现由启动失败提示统一说明。隔离打包实测：A 创建会话并持锁，B 明确失败；强制结束 A 后重启 B，原会话仍可读。锁文件保留在 Home 内，锁由 OS 随进程退出释放；未创建外部 Home 时仍保持单文件零写入启动。`tests/test_home_runtime.py` 和新增 `tests/test_packed_home_lease.py` 覆盖锁路径及其大小写、末尾句点别名保护、占用拒绝与异常退出接管，并纳入发布门禁。Windows/Linux 门禁均通过 114 项 Python、57 项 Node、67 个前端模块解析、20 个运行探针和确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 和 Windows 发布包 SHA-256 均为 `e27b055d33656d24fe50b931a26fbaba57fda9ae8b8e45013fa581a2219ea577`，Linux 包为 `38dffd212617ceab792202a3db51bd91bfd6eaef8d1fefa305672806002d67fd`。同进程多标签仍可用；不支持两个进程同时写同一 Home。实体手机与原生 Linux WebView 尚待验收，未做压力或高负载测试。
390×500 移动布局补验：隔离打包 Home `.build/mdo-packed-docks-svtw8_p9` 用有界本地模型触发 `APPROVAL UI`。对话中的审批卡直接可见；打开检查器后“决策 1”页内标题、命令和“允许一次”按钮均在视口内。点击允许后待决计数从 1 归零，工具结果显示 `exit_code: 0`，最终回复出现且焦点回到输入框；浏览器脚本错误为空。抽屉关闭后可继续发送，打开抽屉期间输入仍保留。该验收不等于实体手机触控、软键盘或其他审批状态的全面验证；未做压力或高负载测试。
390×500 拒绝链另用隔离打包 Home `.build/mdo-packed-docks-buekf9ck` 验证：`APPROVAL UI` 到达后点击对话卡上的“拒绝”，审批卡消失，工具卡保留 `tool execution denied by approval policy` 错误输出，回复完成且焦点回到输入框。窄屏时间线能看到错误、回复、消息操作和底部模型/思考/权限选择器；浏览器脚本错误为空。该本地模型夹具的固定回复文案不用于判断真实模型在拒绝后的语义；未做压力或高负载测试。
设置操作栏短屏可达性与焦点流转：旧打包页在 320×350 英文下把“Restore defaults”排到 x=-46–26，421px 俄语下“Сбросить настройки”排到 x=-60–32，按钮大半不可见。现于 ≤640px 使用四等分短标签，完整中英俄名称保留给无障碍接口；最终打包页 320px 中英俄四个按钮均位于 x=12–308、高 40px，421px 俄语位于 x=12–409，641px 恢复完整标签并位于 x=18–629。隔离 Home `.build/mdo-packed-docks-o589bmv6` 实测预览成功后焦点到“应用”，应用后到状态提示；英文恢复默认确认层在 320×350 完整可见，Esc 回到原按钮，确认后 revision 更新并聚焦状态提示；俄语确认文案与按钮同步本地化。后续包 `.build/mdo-packed-docks-e46o4djd` 验证“放弃更改”恢复原值后聚焦状态提示，最终包 `.build/mdo-packed-docks-qlmzquhc` 验证 421/641px 俄语断点无裁切，浏览器脚本错误为空。Windows/Linux 有界门禁均通过 114 项 Python、57 项 Node、67 个模块解析、20 个运行探针和确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 和 Windows 包 SHA-256 均为 `ea2dc244745cefb112b7739a40a799adf1a5fb41fb1943ad4fd6bc31e7ee90ba`，Linux 包为 `d5cd9ed7658e6de380d2738d441f1817b384f6010295b9efb50cde8609d1181d`。实体设备触控与软键盘仍需验收；未做压力或高负载测试。

设置分区导航的跨断点可达性：先前单文件 Home `.build/mdo-packed-docks-ftcglzek` 在 320×350 直接打开“计划任务”时，当前分类右缘为 333px，分类栏可见右缘仅 305px。现切换或深链进入分区时，只滚动分类栏使当前项完整可见；分类栏自身尺寸变化后也会重新定位。最终单文件 Home `.build/mdo-packed-docks-2nmcsivz` 验证从“诊断与存储”将窗口缩至 320×350 后当前项完整可见，再转到“计划任务”仍完整可见，文档宽度 320px，设置标题保持焦点；761×300 短桌面窗口的纵向分类栏末项下缘为 292px，小于栏底 300px。浏览器脚本错误为空。Windows/Linux 有界门禁均通过 114 项 Python、57 项 Node、67 个模块解析、20 个运行探针与确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 均为 `53d6bef5704e645a8907ab25506710c23e3620587a62e86d44e87060ed267c7a`，Linux 包为 `0f9798cc88239cc1cfb11f2f03f1473d43bec325ee77c2bb8a76f06475d26b52`。其他设置分区操作及实体设备触控仍待验收；未做压力或高负载测试。

手机设置页长表单与短视口的操作链修复：旧单文件 Home `.build/mdo-packed-docks-17iz7gb6` 在 320×350 将计划任务表单滚到下方时，分类栏从 y=104–174 滚到 y=-1074–-1004；直接切到 Agent 后沿用旧滚动量，标题位于 y=-859。最终单文件 Home `.build/mdo-packed-docks-nbjmizf1` 将手机分类栏停靠在设置内容顶部，隐藏其可继续触控滚动的横向滚动条；切换分区时仅重置内容滚动，不清除未应用的表单值。手机设置页收起无关的会话顶栏，返回对话后恢复它并聚焦输入。320×350 的当前分类按钮高 40px、栏高 49px、文档宽 320px；390×500 计划任务表单向下滚动后分类栏仍在 y=52–101，切到 Agent 从标题开始显示，跨 Agent/联网分区的未应用系统指令保持。浏览器将已聚焦的系统指令框从 390×500 动态缩至 390×250 时，极短屏使用可滚走的分类栏和 80px 多行框，输入框完整位于 y=78.75–158.75 的内容区内；恢复 500px 高度后焦点及输入仍在。脚本错误为空。这是浏览器可视高度模拟，不代表实体手机软键盘已验收。Windows/Linux 有界门禁通过 114 项 Python、57 项 Node、67 个前端模块解析、20 个运行探针及确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 均为 `31bdd978358fc3160cf9c7679fffe9c2d312983f3d7f57e43575be71f3247d46`，Linux 包为 `051c9de45ed8721f2db9d339369863c5b1ad653e654eec444eaa84d70e432d09`；未做压力或高负载测试。

2026-09-28：待发送队列的短屏阅读位置。队列每次接纳、移除或刷新消息都会重建列表；此前只恢复按钮焦点，短窗口中用户滚动查看较后消息时会被带回顶部。现按当前会话保存列表滚动量：停在中段则保留阅读位置，已在底部则跟随新增消息，切换会话不沿用旧位置。`tests/fixtures/queue-scroll-browser.html` 在真实浏览器中得到中段 20→20px、底部 113→182px（新底部 182px）、切会话后 0px，全部通过；修复版单文件 Home `.build/mdo-packed-docks-fms9guds` 在 280×250 验证运行中可加入三条待发消息、列表内部高 41px 且内容可溢出。浏览器用例验证滚动语义；该打包页仅验证真实队列接纳和短屏布局，未把自动化滚轮注入结果当作触控验收。Windows/Linux 有界门禁通过 114 项 Python、68 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 均为 `0427292f4371e663a44ab9af600a586f69c09ded21508aae02463a90bf7d1312`，Linux 包为 `3564395f663683772e453f844be9791e91fb17e2440183622f6da481b0a684f6`。未做压力或高负载测试。

2026-09-28：极短屏长待发卡片的完整阅读。上一阶段只恢复了队列重绘时的滚动偏移；本轮单文件页发现 280×250 的队列列表高 41px 时，网格自动行被压成 40px，长正文溢出卡片，后续消息会与它重叠。队列网格现以内容高度确定每一行，列表自身继续滚动。生产样式夹具 `tests/fixtures/queue-scroll-browser.html` 在修复前 `cardsFit=false`，底部新增后旧、新底部均为 227px；修复后 `cardsFit=true`，中段 20→20px，底部随新增 443→512px，切会话回到 0px。最终单文件 Home `.build/mdo-packed-docks-y22x8g1b` 在 280×250 用两条长待发消息得到卡片高度 242/449px，正文高度 239/446px，均完整容纳；列表可从顶部滚入正文，阅读中段新增一条后 500→500px，停在底部新增一条后 698→742px（新底部）。文档宽度 280px，浏览器脚本错误为空。Windows/Linux 有界门禁通过 114 项 Python、68 项 Node、71 个前端模块解析、21 个运行探针和确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 均为 `f40a600ace18ea52e1680cde2bef299d300a73fb8a1fd5612b916aed18d1089d`，Linux 包为 `23d42bd222167695a777c7678aee456578d21e92f4315a3c3988d9b4daac6c5c`。实体触控与原生 WebView 仍待验收；未做压力或高负载测试。

2026-09-28：队首移除后的阅读锚点。上一阶段保留队列滚动数值，但用户正阅读后续长消息时若队首被移除，相同数值会指向另一段内容。队列重绘现记录首个可见待发项的稳定 ID 和相对位置，重绘后恢复该项位置；停在底部时仍跟随新增项，新任务的暂存提交也携带稳定 ID。生产样式浏览器夹具 `tests/fixtures/queue-scroll-browser.html` 在修复前得到队首移除后的相对位置 0.25→-164px、`passed=false`，修复后 0.25→0px、`passed=true`；同时保留中段 20→20px、底部 443→512px（新底部）及切会话 0px。最终单文件 Home `.build/mdo-packed-docks-0od5h4gb` 在 280×250 从另一客户端移除队首，页面自动刷新后仍使正在阅读的下一项保持 -253.94→-254px；再从页面入队第三项后仍为 -254px，浏览器脚本错误为空。此打包验收覆盖跨客户端移除与页面入队的重绘路径，未把自动派发完成的瞬间单独计为已验证。Windows/Linux 有界门禁均通过 114 项 Python、68 项 Node、71 个前端模块解析、21 个运行探针与确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 均为 `d9476248915d63942ce21dde56addcee8d3ac1e55dbcc225035f78d7df1ee42b`，Linux 包为 `60adb3847a9088d20c3e31b87af2c90019db4df8df07714c5f36e8dd93690c8f`。实体触控与原生 WebView 仍待验收；未做压力或高负载测试。

2026-09-28：移除正在阅读的待发卡片。上一项的稳定 ID 锚点在当前卡片自身消失时也会丢失，原滚动数值可能让下一条从正文中段开始。现在按旧列表顺序寻找下一条仍存在的卡片，并把它的开头放到列表顶部；若后面没有卡片则回到剩余列表底部。生产样式浏览器夹具 `tests/fixtures/queue-scroll-browser.html` 在修复前得到当前卡片相对位置 -80px、移除后下一条仍为 -80px、`passed=false`；修复后下一条为 0px、`passed=true`，队首移除、中段、底部跟随和切会话检查继续通过。最终单文件 Home `.build/mdo-packed-docks-y1cv1z2t` 在 280×250 的归档会话保留三条长待发卡片；阅读第二条时从另一客户端移除它，页面自动刷新后第三条顶部落在列表顶部约 0.3px，列表从 3 项变为 2 项，浏览器脚本错误为空。这验证跨客户端移除时的打包重绘；当前卡片的页面按钮点击及实体触控没有在此项单独验收。Windows/Linux 有界门禁均通过 114 项 Python、68 项 Node、71 个前端模块解析、21 个运行探针与确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 均为 `e608ef809f810c12586274b02e0d6665e76065d01fe39f1e000f01ef3b758c3c`，Linux 包为 `9e0c68806a4c097bcc9679486f8cf1d3f25f8005eb340b1df3617a0a46494082`。未做压力或高负载测试。

2026-09-28：待发项连续移除的焦点与可见性。前一项补齐了卡片阅读锚点，但真实单文件 Home `.build/mdo-packed-docks-aqat7bsa` 在 280×250 点击第二条移除后，焦点错误地回到队列标题：删除请求期间把所有移除按钮设为原生 disabled，焦点无法转到下一项。改为请求期间保留按钮可聚焦、用 aria-disabled 表达忙碌状态，并由现有点击守卫阻止重复提交；重绘后若恢复的焦点落在列表可视区外，只滚动队列列表使按钮可见。生产样式浏览器夹具 `tests/fixtures/queue-remove-focus-browser.html` 在修复前得到 `focusedId=toggle`、`passed=false`；修复后延迟 DELETE 期间焦点保持、重复点击只产生一次 DELETE，完成后焦点转到下一项，连续删除最后一项后前一项按钮仍可见，`passed=true`。最终单文件 Home `.build/mdo-packed-docks-o2qsktg9` 在 280×250 从页面点击第二条、按 Enter 移除第三条：第三条最初顶部约 0.3px，随后第一条移除按钮可见且获焦；服务端只剩第一条，文档宽 280px，浏览器脚本错误为空。Windows/Linux 有界门禁均通过 114 项 Python、68 项 Node、71 个模块解析、21 个运行探针和确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 均为 `c9de83ebb8bd54c05d291c96e8d547a74f35d577ef6e96dff07a2541b37a6e34`，Linux 包为 `c276845083fa707dcd968c1af0d79f63f01dca64041b3187e192bcad32e7d185`。实体手机触控及原生 WebView 仍待验收；未做压力或高负载测试。

2026-09-28：运行中切换后续消息配置的缺口确认。旧版 `app_bak/wwwroot/src/ui.js` 在任务运行时仍提供模型、思考强度、权限选择；新版 `composer-profile.js` 用 `runActive` 禁用三个选择器。隔离单文件 Home `.build/mdo-packed-docks-bpncv2lo` 在 390×500 发送 `SLOW UI` 后实测输入框可继续编辑，但三项均为 disabled。服务端 `MdoSessionSetProfile` 明确要求会话空闲，简单解锁前端会导致选择被拒绝或无效。已将逐条队列配置快照、草稿持久化、跨标签幂等和运行启动原子性写入 `docs/deferred-composer-profile.md`；该功能尚未实现，不能把现有空闲时的选择测试当作运行中切换已恢复。此轮只确认边界并确定实施合同，未做压力或高负载测试。

本次仅文档与验收边界更新，根目录 `mdo.exe` 重新打包后字节未变。Windows/Linux 有界门禁仍通过 114 项 Python、68 项 Node、71 个前端模块解析、21 个运行探针与确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 均为 `c9de83ebb8bd54c05d291c96e8d547a74f35d577ef6e96dff07a2541b37a6e34`，Linux 包为 `c276845083fa707dcd968c1af0d79f63f01dca64041b3187e192bcad32e7d185`。

2026-09-28：运行中切换后续配置的第一阶段已落地。会话和新任务草稿的 `draft.json` 升至 schema 7，继续读取 1–6；可选 `composer_profile` 保存下一条消息的完整选择，提交意图的 `profile` 冻结模型、思考强度与权限。浏览器草稿存储保留快照，同 ID 的配置差异被视为冲突。真实 HTTP 探针覆盖旧 schema 6、未提供字段时保留、显式清除、非法权限拒绝、同 ID 幂等冲突以及状态转换后的快照；Node 用例覆盖文字先于加载编辑、两条不同配置的消息和刷新回放。Windows/Linux 有界门禁通过 114 项 Python、69 项 Node、71 个前端模块解析、21 个运行探针和确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 均为 `03c1ac62d65734b1d94ab12acb5826a2b344da3a0de564c9428ed95d2b6b4a91`，Linux 包为 `69710295686bce58ea8e58e0703bde2801d000ce9191ac65c87e8bccef650dbd`。队列快照、启动时原子应用和运行中选择器尚未完成；本轮不把持久化合同当作页面操作恢复。未做压力或高负载测试。

2026-09-28：配置快照进入待发送队列。共享校验器统一约束草稿与队列的模型、思考强度和权限三字段，`queue.json` 升至 schema 7 且继续读取旧 schema 1–6。队列 POST/GET、状态转换和磁盘往返保留不可变快照；同 ID 不同配置返回冲突，浏览器提交意图的入队与恢复核对也比较快照。真实 HTTP/TCC 探针覆盖兼容读取、非法权限、重复 ID、状态转换和启动入口保护；带快照项目前明确返回 `queue_profile_pending`，防止使用会话旧配置静默启动。Windows/Linux 有界门禁通过 114 项 Python、70 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `e1375d25ca4ba25e8614650eb10a9df8011205b3dc31694b2fce9f02bfe1f85a`，Linux 包为 `d3f3dc92fe16af74e44e7a2848acc907903a42e53b08b0bfa628cb5fb9aefbe7`。原子运行启动和前端运行中切换仍待实现；未做压力或高负载测试。

2026-09-28：绑定队列运行按快照启动。队列 prepare 从同一项读取配置，claim 再核对文本、图片与完整配置；运行管理器把快照带入会话独占打开流程，用快照模型预检图片能力，在 Agent 创建及恢复检查通过后提交会话有效配置，随后启动该项运行。临时的 `queue_profile_pending` 已移除。真实 HTTP/TCC 探针在同一会话连续执行高思考只读与普通思考平衡两条快照项，运行记录和会话元数据分别对应两项；不存在的模型返回 `session_profile_invalid`，无凭据且元数据未变；两客户端同时抢带快照的同一项仍只有一次接受。Windows/Linux 有界门禁通过 114 项 Python、70 项 Node、71 个模块解析、21 个运行探针和确定性打包；Windows 单文件零旁路写入及 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `ff43f3d7b3e5ff46e70cb376ce1cf5d2d5ee653a7e0fcd9da94047b854bd6109`，Linux 包为 `3e280d5954115bc7122d6a2a510dbfb3cdcea38ecae5cb561c9e84c474cc4502`。页面还未在运行中开放配置选择及提交快照，打包页完整链未验收；未做压力或高负载测试。

同阶段模型切换补验：在隔离 API 环境新增指向本地 Responses 夹具的第二模型，随后由队列项快照切换到该模型并成功运行；运行记录与会话元数据均显示新模型 ID，元数据 revision 再加一。此项把前述“配置切换”证据从思考强度与权限延伸到实际模型选择；重新执行的 Windows/Linux 有界门禁与确定性打包均通过，包哈希未变。

2026-09-28：运行中后续消息配置已接入模块化输入区。模型、思考强度、权限在运行时可选择；会话草稿保存后续选择，输入区显示“下次任务生效”，每次提交复制当时完整配置到不可变意图与队列项。待发卡片显示已固定的模型、思考强度和权限；切会话与刷新从便携草稿恢复，空闲时仍直接更新会话配置。新任务各条初始意图也保存快照；明确创建失败并由用户复核后，尚未创建的任务和待发项一起采用复核配置。模块浏览器夹具 `tests/fixtures/composer-profile-deferred-browser.html` 通过运行中保存、重载、切会话与空闲更新；Node 用例确认两次 Enter 的配置互不改写及新任务失败复核时快照同步更新。

隔离单文件 Home `.build/mdo-packed-docks-rh4d3141` 实测运行中选第二模型、高思考、只读并排队后，首轮及后续轮分别以原模型中思考、第二模型高思考完成。重新打包后的 Home `.build/mdo-packed-docks-f9e1l7aw` 在 390×500 验证状态标签及冻结配置卡片处于可见区，三个选项均可操作且浏览器脚本错误为空；三次实际运行记录依次为原模型中思考、第二模型高思考、原模型中思考。Windows/Linux 有界发布门禁、包哈希与剩余验收范围见下一条。

本阶段 Windows/Linux 有界门禁均通过 114 项 Python、70 项 Node、71 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。旧的跨会话异步 profile 浏览器夹具回归 `passed=true`，新夹具 `passed=true`。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `c2991d7519411e24dc9b041bba66d89c021e32047b5fdb3f15a33c54f07238f1`，Linux 包为 `f9fad31ee58534509d76414fedbf03322a069904fde619bf2c5a169e6648518c`。两条不同配置同时待发、跨标签同会话并发、实体移动设备及原生 WebView 仍待验收；未做压力或高负载测试。

2026-09-28：继续压实运行中配置并修复跨标签冲突提示。单文件 Home `.build/mdo-packed-docks-eye7nuqn` 在一轮 `SLOW UI` 中连续排入 `QUEUE PROFILE A/B`；队列同时保存第二模型/高/只读及原模型/中/询问两份快照，刷新后页面两张卡保持原样，三轮最终分别按原模型中、第二模型高、原模型中完成。另一双标签 Home `.build/mdo-packed-docks-nt_ms6y6` 复现第二标签草稿 revision 冲突：服务端保留第一标签的后续选择，第二标签已有草稿错误提示，却仍显示“下次任务生效”。新增草稿 profile 保存状态后，输入区在写入中显示“正在保存后续配置…”，冲突时显示“后续配置未保存”。模块夹具同时验证状态及远端快照不被覆盖；重新打包的双标签 Home `.build/mdo-packed-docks-5b7eg_vp` 复测未保存标签、原错误提示和刷新恢复，页面脚本错误为空。完整有界门禁与包哈希见下一条。

本次 Windows/Linux 有界门禁均通过 114 项 Python、70 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 和 Windows 包 SHA-256 为 `75ce0935c7c9bd2e791de1594b8541dbcda7f7f22c1f9b02ab941aad4a0e799c`，Linux 包为 `2bd0403ea541a730e8fea9af519328129bd1d189690de05a5c4a26b10ba11176`。跨标签冲突仍要求刷新后核对，实体设备触控与软键盘、原生 Linux/macOS WebView 尚待验收；未做压力或高负载测试。

## 2026-09-28：待发图片清理的页面内重试

运行中移除携图待发消息时，服务端将待清理图片 ID 持久记在 `queue.json`，页面等会话空闲后调用附件删除接口。此前只有运行状态刷新或重新进入会话会再试；一次附件存储临时故障之后，若页面一直打开且没有后续状态变化，取消的图片可能长时间留在便携 Home。现在每个会话只保留一个重试定时器，从 15 秒退避到最多 60 秒；运行结束通知仍会立即触发清理，删除成功或图片已不存在时取消定时器。服务端报告图片仍被草稿、队列或历史引用时等待下一次明确的状态变化，不对此状态定时轮询。删除请求期间新增的待清理 ID 也会进入下一轮，避免并发添加被遗漏。

`tests/test_unused_image_cleanup.mjs` 的 6 个定向用例覆盖运行结束、临时故障自动重试和退避、错过状态通知、仍被引用时不轮询，以及删除中新增图片。Windows/Linux 有界发布门禁分别通过 114 项 Python、74 项 Node、71 个前端模块解析、21 个运行探针与确定性打包；Windows 还通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `1bfb075196388b566eb034223985aa8b5148043aa19bcf4e5a4dec516aad42c9`，Linux 包为 `efaef7f0f678323a36e34a7fac1043b04ccf80d081bfc2d3c00e21309be40c59`。没有在打包页注入瞬时附件故障，也没有验证操作系统原生文件拖放或实体手机；未做压力或高负载测试。

## 2026-09-28：待发模型决定图片能力

运行中模型选择已经作为下一条消息的草稿和队列快照保存，但附件模块仍优先读取会话元数据的旧模型。于是当前纯文本模型运行时，即使输入区改选支持图片的模型，也无法添加图片；反向切换时则可能允许把图片放进纯文本模型的待发消息。附件入口、草稿警告和提交前校验现在统一以输入区所选模型判断能力，只在选择器没有值时回退到当前会话模型。服务端在按队列快照启动时继续独立验证图片能力。

新生产模块浏览器夹具 `tests/fixtures/composer-image-deferred-model-browser.html` 在修复前得到 `deferredVisionAllowed=false`、无法上传；修复后得到 `passed=true`：模拟拖入 PNG 后上传一次，切回纯文本模型时阻止选择器打开并标记已有图片不可发送，再切回带图模型清除警告。隔离单文件 Home `.build/mdo-packed-docks-huob8t4t` 在当前带图运行、待发纯文本模型时实测附件入口提示不支持；Home `.build/mdo-packed-docks-4bnzazwp` 在当前纯文本运行、待发带图模型时通过文件选择器上传隔离生成的 `fixture.png`，随后发送图片消息并得到回复。队列卡显示冻结的带图模型；服务端最终会话模型为带图模型，队列与草稿提交意图为空，刷新后历史图片仍可打开，浏览器脚本错误为空。

Windows/Linux 有界发布门禁分别通过 114 项 Python、74 项 Node、71 个前端模块解析、21 个运行探针与确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `d1552ca2f8c9e332401df2164e764db59877ec6f0dd8da1dfdbd48a53070a3e0`，Linux 包为 `380f4539b1003dbee60fb6448bad9116d4b3ea34965b3d1947d10948c987094a`。前一隔离 Home 另暴露发送刚启动时立即切换模型的竞态：运行尚未进入页面 active 状态，空闲配置 API 可能先成功，随后队列快照在启动时覆盖它；本阶段未修复该启动窗口，下一阶段须让队列已占位时的选择也保存为后续意图。原生文件拖放、实体手机与原生 WebView 未验收；未做压力或高负载测试。

## 2026-09-28：发送启动窗口中的后续模型选择

发送已进入持久提交意图、队列或独占派发，但页面尚未收到 active 运行记录时，原输入区仍走空闲会话 profile API。用户此时改选模型，会看到“会话配置已更新”；本轮队列快照启动后却会覆盖它，造成后续选择丢失。输入区现在把草稿提交意图、队列加载及派发、已知未结束运行都视作后续选择的边界，写入便携草稿而不修改本轮会话配置。队列项的不可变模型、思考强度和权限仍按发送当时的快照执行。

生产模块夹具 `tests/fixtures/composer-profile-deferred-browser.html` 新增“无 active 运行但提交待决”用例：模型切换只增加草稿 PUT，未增加会话 profile PUT；待决解除后回到当前模型会清掉后续草稿。隔离单文件 Home `.build/mdo-packed-docks-85yvkzvl` 用 5 秒有界运行 POST 延迟复测，发送 `SLOW UI startup profile race` 后立即选第二模型：队列卡固定原模型，页面显示“下次任务生效”，服务端会话仍为原模型、草稿后续选择为第二模型。随后提交 `NEXT MODEL after startup`，队列卡固定第二模型；两轮服务端运行按原模型、第二模型的顺序成功，最终会话模型为第二模型，队列为空，代理记录两次队列 POST、两次运行 POST，页面无脚本错误。刷新后配置和两轮时间线保留。该夹具在隔离 Home 运行，没有使用真实外部模型端点。

Windows/Linux 有界发布门禁分别通过 114 项 Python、74 项 Node、71 个前端模块解析、21 个运行探针与确定性打包；Windows 单文件零旁路写入及 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `cd559716e64a2d26f1561bf8d5505f6625ce186728f9d7ead0ada65f52a4fabd`，Linux 包为 `9a53f53147e7f09173cbadc415f7ec5c5e6affc69c576262976dc30b87de64ac`。本轮只验收同一页面的发送启动窗口；多进程共享 Home、原生 WebView 与实体设备仍需独立验证，未做压力或高负载测试。

## 2026-09-28：Token 用量按实际调用模型归属

输入区现可在运行中或两轮之间预选下一条消息的模型，原 Token 环和提示却始终拿这个预选模型的上下文上限除以上一轮输入量；模型上下文大小不同就会显示错误的占比。时间线 `model_done.model` 是发给模型端点的 wire 名，多个 mdo 模型配置可以共用它。现优先按时间线的 `run_id`、当前项目与会话关联 `/runs` 中的 `agent_run_id`，再以运行记录的 `model_id` 查目录模型。只有 wire 名在目录中唯一时才回退匹配；同名而运行记录不可得则隐藏占比、显示说明，不把可能错误的配置展示为确定值。弹层分列“输入区模型/上下文上限”和“上次调用模型/上下文上限”；切会话时仅统计与会话一致的时间线。中、英、俄三种语言包同步更新。

`tests/test_token_meter_escape.mjs` 新增同一 wire 名、两个模型上下文分别为 1,000 和 10,000 的回归用例：上次调用关联前者、输入区选择后者时，占比仍是 500/1,000 = 50%；撤去运行记录后不误用后者的上限。QA 夹具 `tests/manual_packed_docks_qa.py --image-capable --second-model-context-tokens 262144` 提供两个同 wire 名、不同上下文的合法模型配置。隔离单文件 Home `.build/mdo-packed-docks-puv_xby3` 用最终包完成首轮本地有界回复后，切换到第二模型，弹层同时显示输入区 262,144、上次调用 131,072，提示仍归属 Ling 3.0 Tiny；页面脚本错误为空。先前 Home `.build/mdo-packed-docks-4ygl3yoz` 还完成第二轮、刷新并反向切回首轮模型，弹层显示上次调用 Ling Text QA 的 262,144 上限，累计输入/输出 14/6。

Windows/Linux 有界门禁均通过 114 项 Python、75 项 Node、71 个前端模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 均为 `a3e363a9582827962f9c16a3aa63403b142a86bc3881011b49a05d4e1817348b`，Linux 包为 `da8a0ef877c445eec2927a743a2322d70c715f3611f4d918645fba8f0a26cf40`。运行记录只保留于进程内，服务重启后若历史 wire 名对应多个模型，当前仅能诚实显示归属未知；历史精确占比仍需在持久事件中增加模型配置标识。实体触控与原生 WebView 未验收；未做压力或高负载测试。

## 2026-09-28：子 Agent Token 调用的归属边界

继续检查持久事件发现它只记录接口模型名，没有 mdo 模型配置 ID；`xwork` 为子 Agent 事件标注 `agent_depth`，而 `/runs` 的模型 ID 描述顶层交互运行。Token 弹层现仅把深度为 0 的调用与顶层运行记录相连，避免子 Agent 的同名接口调用误用主 Agent 上下文。生产模块回归在相同 run ID、相同 wire 名、深度为 1 的模拟事件下确认占比为未知；完整的历史归属还需兼容旧版 `ui-events.jsonl` schema 1–3，且会话分叉重写事件时须逐条保留原配置快照，不能简单套用当前模型。

Windows/Linux 有界门禁均通过 114 项 Python、75 项 Node、71 个模块解析、21 个运行探针及确定性打包；Windows 单文件零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `d4167e730648b8b165737fd79d67f50b5f4f7ba0befc65922d72e63e1e68b6f0`，Linux 包为 `79d7b48447fd22ef78c9768f74808f7a5cae676066af467ee62f5a2eabba2b03`。真实子 Agent 模型调用的打包页验收、重启后精确归属、实体设备与原生 WebView 仍需后续验证；未做压力或高负载测试。

## 2026-09-28：重启后保留历史 Token 模型归属

交互运行记录只在进程内保留。此前服务一旦重启，两个 mdo 模型配置共用同一 wire 名时，旧时间线无法判定上次调用属于哪一个模型。会话 UI 事件升至 schema 4：主 Agent 的 `model_done` 写入模型配置 ID 与运行时的上下文上限；旧 schema 1–3 保持可读，缺字段的旧事件继续按运行记录或唯一 wire 名回退。页面优先使用事件自身的快照，模型目录后来修改或运行记录被清空也不会改变当时的占比。子 Agent 不套用主 Agent 快照。分叉重写日志时逐条复制原事件配置，历史截断标记留空；`MdoSessionEventSnapshotAt` 仍接受旧版结构体大小，避免只因新增末尾字段就拒绝旧调用方。事件桥校验模型 ID 时按会话的长度和 UTF-8 规则处理，允许合法的自定义 ID 包含 `/` 等字符。

定向会话运行探针覆盖 schema 2/3 读取、schema 4 模型事件、分叉时逐条保留配置以及旧结构体大小的读取；前端用例覆盖运行记录消失后的事件快照优先级。隔离单文件 Home `.build/mdo-packed-docks-zyfemq_h` 在本地有界模型夹具下完成两轮调用：两条 `model_done` 的 wire 名同为 `ling-3.0-tiny`，持久模型 ID 分别是 `ling-3.0-tiny` 与 `ling-3.0-tiny-text-qa`，上下文上限分别是 131,072 与 262,144。停止服务并从同一 Home 重启后，`/runs` 的 `items` 为空；页面把输入区切回首模型，Token 弹层仍显示上次调用为 Ling Text QA、上限 262,144，累计输入/输出 14/6，脚本错误为空。再从该会话建立完整历史分叉，子会话日志仍分别保存两条原始配置。测试仅使用隔离的本地回复夹具，没有外部模型调用。

最终代码的 Windows/Linux 有界门禁均通过 114 项 Python、75 项 Node、71 个前端模块解析、21 个运行探针与确定性打包；Windows 单文件零旁路写入和 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 均为 `75056a4c833779aea8f04e65f1f9d3338d30df3ca20e0add0dce96d755383cef`，Linux 包为 `796a4b9a68bcd25bea364d61eeb7389a8055d878d068854192a161f054368c7f`。既有 schema 1–3 事件未保存可反推出配置的资料，不能追补同名 wire 的精确归属；子 Agent 模型快照、实体设备及原生 WebView 仍待验收，未做压力或高负载测试。

## 2026-09-29：最终打包页导出文件落盘核对

先前的单文件页面验收只看到浏览器下载完成事件和 Blob 正文，没有独立读回磁盘文件。此轮从根目录最终 `mdo.exe` 复制启动隔离 Home `.build/mdo-packed-docks-a5wvlxal`，用本地有界模型夹具从页面发送 `MARKDOWN UI export disk QA`，待含链接和 C 代码块的回复完成后，在桌面顶栏依次执行“导出 Markdown”和“导出 JSON 备份”。Edge 浏览器实际下载并保存文件，磁盘读回的 Markdown 为 296 字节，包含用户指令、`## Markdown QA`、HTTPS 链接和代码块；JSON 备份为 2574 字节，可解析，含 `export_schema`、`exported_at_us`、`meta` 和 `snapshot`。同一会话在 320×350 视口经手机顶栏下载 Markdown，磁盘读回仍为 296 字节且含相同回合；文档宽度 320px，页面脚本错误为空。这证明当前 Windows 浏览器经最终单文件包的导出落盘链路；实体手机与原生 WebView 的下载行为仍须独立验收。

本阶段没有修改应用代码。Windows 有界发布门禁通过 114 项 Python 检查、72 个前端模块解析、21 个运行探针、确定性打包、单文件零旁路写入和 20 秒打包启动检查；未做压力或高负载测试。重新生成的根目录 `mdo.exe` 与门禁包 SHA-256 均为 `65e2e0efed4bb9adb44c88ed8939ce5ad554fecb078c0c53f0b88453c001cefa`。

## 2026-09-29：设置与导出斜杠命令的打包页操作链

继续使用最终单文件 `mdo.exe` 与隔离 Home `.build/mdo-packed-docks-0yzx973m`，仅在该 QA 实例增加第二个模型配置。1280×350 与 320×350 页面分别输入 `/settings` 并按 Enter，进入常规设置；设置标题得到焦点，点击返回后回到原会话、输入框清空并重新获得焦点，两个视口均无横向溢出。随后从页面发送 `MARKDOWN UI slash export QA`，在桌面与 320×350 页输入 `/export` 并按 Enter；两次下载都独立保存到磁盘，读回的 297 字节 Markdown 包含用户指令和 `## Markdown QA` 回复，命令本身没有作为聊天消息留下。页面脚本错误为空。此项补齐了旧版已有的 `/settings` 和 `/export` 命令操作手感；原先打包页已验收 `/model`、`/theme`、`/help`、`/clear`，新版扩展的 `/new`、`/fork`、`/stop` 另有前述证据。旧版 `/demo` 与 `/image` 只调用展示夹具，不作为生产命令迁移。

没有修改应用代码；本轮沿用前一阶段已通过的 Windows 有界门禁和确定性单文件包，重新核对根目录程序 SHA-256 为 `65e2e0efed4bb9adb44c88ed8939ce5ad554fecb078c0c53f0b88453c001cefa`。实体手机的软键盘与触控、原生 WebView 下载行为仍待独立验收；未做压力或高负载测试。

## 2026-09-29：重启后回收过期孤立图片与半写入文件

附件 POST 先写 `.bin` 再写 `.json`，页面拿到响应后才知道图片 ID。此前上传中断若在响应前留下孤立文件，服务端只在后续上传触及配额时扫描；若崩在两次写入之间，没有元数据的 `.bin` 还会被扫描跳过。现在会话草稿 GET 在释放草稿锁后执行有界回收：只处理超过 24 小时的无引用上传，沿用草稿、待发队列、运行和历史的引用核对。完整文件对读取元数据时间；仅有 `.bin` 且元数据确实不存在时才用文件修改时间；仅有 `.json` 时核对原始创建时间。损坏的元数据不当作“缺失”，保留文件供人工检查。回收失败不阻断健康草稿读取。保护期内的上传保持不动，因为另一个浏览器标签可能尚未保存草稿。

真实 xs/TCC API 探针在 Windows/Linux 验证：过期完整、仅 `.bin`、仅 `.json` 三种无引用文件被删除；被草稿引用的旧图片、近期上传及损坏元数据被保留。最终 Windows 单文件 Home `.build/mdo-packed-docks-bc95f41l` 通过 API 上传两张合成 PNG，一张加入草稿、一张保持无引用；将元数据时间调到两天前，再构造过期的单边文件、近期单边文件和损坏元数据对。结束打包进程并从同一 Home 重启，首次草稿 GET 删除过期的无引用文件，保留旧草稿图片、近期文件和损坏对。320×350 页面仍显示一张草稿缩略图，文档宽 320px，脚本错误为空。测试通过人工回拨文件时间覆盖 24 小时边界，没有等待真实两天，也未在上传写入的精确机器指令处强杀进程；保护期内无法即时判定没有 ID 的上传是否仍在进行。

最终代码的 Windows/Linux 有界门禁均通过 114 项 Python 检查、72 个前端模块解析、21 个运行探针和确定性打包；Windows 单文件零旁路写入与 20 秒打包启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `f97f7b5a5710275e4fc19fee2526f77356fd900a397fb07e201852d6da6a98de`，Linux 包为 `177fe481910f6a38699098079d82769283490c7778dfa38edb9b2c0277841342`。未做压力或高负载测试；操作系统原生文件拖放与实体移动端触控仍待验收。

## 2026-09-29：凭持久 Agent 启动事件恢复队列接受凭据

队列运行原先先写 `starting` 凭据、再调用 Agent Start，成功返回后才写入运行 ID。若进程在启动事件落盘与凭据升级之间退出，重启后只能看到 `starting`，用户必须判断是否已经执行。本阶段在进入 Agent Start 前，把队列 ID、管理器运行 ID 和 Agent 运行 ID 写入 schema 3 凭据；会话 UI 事件升至 schema 5，仅顶层 `agent_start` 记录对应队列 ID，旧 schema 1–4 仍可读取。队列或独立凭据读取时，只有持久事件同时匹配队列 ID 与 Agent 运行 ID，才把预写凭据升级为 `accepted`。缺失、裁剪或损坏的事件不能证明未执行，继续显示待核对并禁止自动重发。普通成功路径仍沿用运行启动后的绑定；预启动明确失败可清除凭据。

Windows/Linux 真实 xs/TCC API 探针验证顶层事件关联、模拟凭据尚未升级时的自动收敛，以及没有匹配事件时仍保持 `starting`。新增单文件重启探针在隔离 Home 完成一次有界本地运行、确认事件落盘后结束进程，离线还原“事件已写、凭据未升级”的崩溃窗口，再由另一份相同打包程序接管 Home：首次队列 GET 升级凭据并投影同一运行 ID；同 ID 再启动返回 `queue_run_started`，没有第二次执行。此探针通过离线还原窗口验证恢复逻辑，没有在精确机器指令处强制杀进程；无匹配事件的崩溃窗口仍须人工核对。Home 排他门禁在 Windows 临时可执行文件解锁稍迟时加入五秒有界重试，不改变产品行为。

最终 Windows/Linux 有界门禁均通过 114 项 Python 检查、82 项 Node、72 个前端模块解析、21 个运行探针、单文件重启探针和确定性打包；Windows 零旁路写入与 20 秒启动检查通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `467f5feedf5deb6d7c5b57545ecce1019fa35d8560e07275de2b7c5e5a071d1b`，Linux 包为 `a26ed2fb861b97dac4e28091b86b047edd7f7b829c87b869d3887a03f239ec50`。未做压力或高负载测试；实体移动端与原生 WebView 仍待验收。

## 2026-09-29：队列、草稿与恢复失败的三语提示

此前这三条常用路径的 21 个稳定 API 错误码会在中文或俄语页面直接显示服务端英文详情，包括队列回执缺失、运行已启动、草稿提交冲突和恢复状态无效。现在统一映射为当前界面的行动提示，三份词典均为 1190 键。前端测试逐个以英文服务端详情核对译文，未知错误仍保留原始详情；本轮尚未在打包页逐个注入 21 种故障。

发布门禁的单文件检查另暴露一个独立问题：Windows WebView 在 AppData 以可执行文件名选浏览器配置目录。门禁原先把临时测试包命名为 `mdo.exe`，读到了已有墨斗窗口的待提交前端状态，并在隔离目录自动创建会话。现在门禁保留测试包的 `mdo-release-a.exe` 文件名，避免借用用户的浏览器配置；这只修正了测试隔离，并未解决正式程序的 WebView 配置便携性，须在 xs/WebView 集成层另行设计和验证。

Windows 完整有界发布门禁通过 114 项 Python、82 项 Node、72 个前端模块解析、21 个运行探针、确定性打包、单文件零旁路写入和 20 秒启动检查；Linux 门禁也通过。根目录 `mdo.exe` 与 Windows 包 SHA-256 均为 `7b5d167f90d4dbc4ab3c1b7d7b9fb52faad23ab00d540d2bed48d763cdf9e6a1`，Linux 包为 `371d1c6982fcc5c11dba1af525414a33f87158cf0f0d652b6e6fca9612d27934`。未做压力或高负载测试。

## 2026-09-29：xs 支持显式 WebView2 用户数据目录

追查单文件门禁的浏览器状态串用时，确认 vendored webview 0.12.0 的 Windows 后端默认把配置放在 AppData 下以 exe 文件名命名的目录，而既定 mdo 方案要求首次仅启动退出不产生外部目录、默认不写系统目录。这两项约束与 WebView2 初始化需要磁盘用户数据目录之间存在产品取舍，不能通过改一个默认路径来同时满足。

xs 在提交 `7d28779` 增加可选 `window.profile_dir`：UTF-8 绝对路径或相对程序目录的本地路径，非法配置拒绝窗口启动，不回退 AppData；未配置时保持现有 xs 应用兼容行为。独立 Windows 短时真 WebView2 探针验证相对路径、含中文的绝对路径及上级跳转拒绝，三种场景均无按测试程序名生成的 AppData 配置。mdo 已将依赖锁升级到该 xs 提交，但内置 `xs.json` 尚未设置 `profile_dir`；正式程序仍沿用旧配置位置，不能将此阶段视为便携性修复完成。待确定首次启动的数据目录策略后，再接入 mdo 并验证 `MDO_HOME`、`--home`、搬移和重启。

新依赖的 Windows 完整有界发布门禁通过 114 项 Python、82 项 Node、72 个模块解析、21 个运行探针、确定性打包、单文件零旁路写入和 20 秒启动检查；Linux 也完成宿主重编与相同有界门禁。根目录 `mdo.exe` 与 Windows 包 SHA-256 为 `482122473b4bb8a0891342321c6b0513c27b847fcb891a82c9424fb069c5794c`，Linux 包为 `26ceae038ee16ed6eb37e8596e596d362b4a7b82fdd7aeb9b9960c9190327a62`。未做压力或高负载测试。

## 2026-09-29：核心失败反馈补齐三语提示

新任务、会话读取与导出、图片上传及引用、工作区补全、询问、审批、运行和待办等核心路径的 28 个稳定 API 错误码，现在通过统一 errorMessage 显示当前语言的行动提示。对于任务创建、回答或审批结果可能已经落盘的错误，提示先刷新核对，避免盲目重复提交。中英俄三份词典各有 1218 键；未知错误继续保留服务端原始详情。

定向前端用例逐码以合成英文服务端详情验证三语映射。Windows/Linux 有界门禁各通过 114 项 Python、85 项 Node、72 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过单文件零旁路写入及 20 秒启动检查。根目录 mdo.exe 与 Windows 发布包 SHA-256 均为 4a6437fb7daed10b800eb6cc4e19b4a1ee008fec5983f7fde9bd2ef47ec5fa62，Linux 发布包为 bf15dee7f794271f7eaa93f0018b5780ca4a68a27316eea5220dd842507ac4e5。本轮未在打包页面逐个注入这 28 种故障，余下稳定错误码与服务端资源描述仍需继续审计；实体移动端和其他原生 WebView 尚待验收，未做压力或高负载测试。

## 2026-09-29：无效图片上传后恢复输入焦点

使用隔离单文件 Home .build/mdo-packed-docks-dk9hn587，在带图模型的已有会话中经系统文件选择器选入扩展名为 PNG、实际内容为普通文本的文件。服务端返回 image_type_invalid，页面显示正确中文提示且未留下图片预览，但文件选择器关闭后键盘焦点落在页面根节点。输入框图片模块现只在文件控件、添加按钮或页面根节点仍持有焦点时，把焦点交回任务输入框；若用户已经聚焦其他控件则不挪动焦点。

修复候选 Home .build/mdo-packed-docks-ufcbxnno 在桌面及 320×350 视口重复选入同一无效文件，均显示“图片内容与文件类型不符，请选择有效的 PNG、JPEG 或 WebP。”，焦点为 prompt，图片预览数为零；320px 文档宽度等于视口宽度，脚本错误为空。最终根目录 mdo.exe 的 Home .build/mdo-packed-docks-m1dtkkk4 再次复核桌面路径，得到相同提示、焦点与预览结果，脚本错误为空。这只覆盖文件选择器和无效类型失败，未验证操作系统原生拖放或实体设备软键盘。

最终 Windows/Linux 有界门禁均通过 114 项 Python、85 项 Node、72 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 mdo.exe 与 Windows 发布包 SHA-256 均为 d0b85c9ea0931d65fbf32d6b2d6b158aeba56b37304b124cf990e03b091b487c，Linux 发布包为 28d74a2362a3045f723e78855f645329c305ecdfc0ce026c11ebcc99ad5189f4。未做压力或高负载测试。

## 2026-09-29：输入区错误随语言预览更新

原单文件 Home .build/mdo-packed-docks-3ze8m91a 中，带图模型会话选择伪装为 PNG 的普通文本文件，服务端返回 image_type_invalid，输入区显示中文错误；在设置应用英语后返回原会话，整个界面已为英语，该错误却仍是中文。输入区现在保留错误对象及附加说明源，语言变化时重新生成文案；运行恢复和提交核对的附加说明改为可按当前语言求值，重绘操作按钮后保留原按钮焦点。本地图片选择错误记录词条和回退文案，使它们也可随预览语言更新。

候选单文件 Home .build/mdo-packed-docks-j84w90lt 复测同一服务端错误：英语预览显示英文，俄语预览显示俄文，放弃后恢复中文；零字节 PNG 的本地校验错误也在英语预览后翻译。该测试同时发现原空文件提示只说“不得超过 8 MiB”，不能解释拒绝原因，因此三份词典和内置回退均改为要求非空且不超过 8 MiB。最终根目录 mdo.exe 的隔离 Home .build/mdo-packed-docks-ovb6ngq4 在 320×350 视口选择零字节 PNG，显示新的中文提示、焦点留在输入框、文档宽度等于视口宽度；桌面设置预览依次显示新的英语、俄语提示，放弃后回到中文，浏览器脚本错误为空。服务端错误的三语预览在修复候选中实测；错误操作按钮在语言切换时的焦点仍缺独立打包页实测。实体设备软键盘和其他原生 WebView 尚待验收。

最终 Windows/Linux 有界门禁均通过 114 项 Python、85 项 Node、72 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 mdo.exe 与 Windows 发布包 SHA-256 均为 ed6b7204664d83cf8f64ef1d41e2200e075c7d07ec1017df5ea133849b130b73，Linux 发布包为 5b947c8b33dfc08313d5f1bc86efd8254b03663a22745457e2f65041e1979a60。未做压力或高负载测试。

## 2026-09-29：新任务切项目时清除旧项目输入错误

新任务页的会话键统一为空。旧单文件 Home `.build/mdo-packed-docks-2c5t__7z` 中，在默认项目选择零字节 PNG 后出现图片校验错误；通过输入框上方的项目控件切到新建项目 `qa-second`，路由已改为 `/projects/qa-second/new`，原项目错误却仍留在输入区。现在导航记录当前项目；仅在两个空白新任务页之间切换项目时清除临时输入错误。同项目进入设置再返回不会因此清除错误，待创建任务的原有导航保护保持不变。

修复候选单文件 Home `.build/mdo-packed-docks-hj_np4i_` 中重复上述路径，错误随项目切换消失，输入框保持焦点。320×350 视口再从 `qa-second` 切回默认项目，错误同样消失；文档宽度与 320px 视口一致，浏览器脚本错误日志为空。定向 Node 用例中导航保护、新任务控制器与图片输入共 9 项通过；未在打包页另造待创建请求与跨项目切换的交错。

Windows/Linux 有界发布门禁均通过 114 项 Python、85 项 Node、72 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过单文件零旁路写入和 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 均为 `62607120d7d471e197417b687b587a38b76d25c719c9b0ca6ba36b1e9089ee23`，Linux 发布包为 `ddf7558cec28236379b66d73dfec752363201b6180763161d9d7e366fa5b010a`。实体移动端和其他原生 WebView 仍待验收；未做压力或高负载测试。

## 2026-09-29：新任务项目切换与延迟创建的焦点交接

旧单文件 Home `.build/mdo-packed-docks-qa2hdwhl` 中，新任务输入区上方的项目选择器从 `default` 切到 `src` 后，URL 和工作区虽正确，键盘焦点却落到页面根节点，不能直接继续输入。这个控件原先只调用导航；侧栏项目切换已有回到输入框的操作手感。输入区选择器现于成功切换后聚焦输入框；若有待创建新任务阻止跨项目，则让焦点留在选择器以便用户处理。创建完成会隐藏该选择器，因此完成回调也接管其焦点，将其交回输入框。

中间候选 Home `.build/mdo-packed-docks-4zvc8218` 验证正常切项目后聚焦输入框；5 秒延迟创建期间尝试跨项目被阻止，项目选择器保持焦点，但创建完成后焦点再次掉到根节点。最终候选 Home `.build/mdo-packed-docks-8hzb6tsg` 重走完整链：在 `src` 项目发送 `FINAL PROJECT GUARD FOCUS QA`，立即尝试改到默认项目；页面仍是 `/projects/src/new`、提示先处理另一项目的新任务、焦点在项目选择器。创建完成后路由为 `/projects/src/sessions/...`，显示唯一用户回合和本地有界回复，输入框重新获焦。320×350 视口又验证 `src`→`default` 正常切换后输入框获焦，页面宽度为 320px，脚本错误为空。延迟创建未产生错误归属会话或重复请求；夹具记录一次创建、一次队列提交和一次运行。定向导航及新任务控制器的 7 项 Node 用例通过。

Windows/Linux 有界发布门禁均通过 114 项 Python、85 项 Node、72 个前端模块解析、21 个运行探针与确定性打包；Windows 另通过单文件零旁路写入与 20 秒启动检查。根目录 `mdo.exe` 与 Windows 发布包 SHA-256 均为 `22adc275fb89f0710d911a5b6f7bd3c3a74339881285569ac45eeec1e710a9cc`，Linux 发布包为 `ceb1e41157202082c57b051507378e75cab2be9226999b802ac8478ea7c7601c`。这些为浏览器访问打包服务的证据，实体手机软键盘和其他原生 WebView 仍待验收；未做压力或高负载测试。

## 2026-09-29：原生 WebView2 配置进入便携 Home

用户选择便携优先：Windows 原生窗口首次打开可以创建程序旁唯一的 `mdo-home`，而不是为保持空启动零写入而把浏览器状态留在 AppData。xs `a3c9885` 在窗口初始化前依据应用参数 `--home`、环境变量 `MDO_HOME`、程序旁默认 `mdo-home` 依次解析 Home，并将 WebView2 用户数据写入其 `data/cache/webview2`；无效显式配置拒绝启动，不回退 AppData。mdo 内置 `xs.json` 已启用该对象形式，`deps.lock` 锁定对应 xs 提交。无窗口的只读启动仍按需创建 Home。

xs 的短时原生 WebView2 探针覆盖默认、中文绝对路径环境变量、CLI 优先级、等号语法、重复参数及越界子目录。mdo 发布门禁从空目录复制单文件，以唯一程序名启动，确认旁边仅出现 Home；关闭后搬移程序和 Home，再启动并确认浏览器目录中已有测试标记保留。另以 `MDO_HOME`、`--home` 启动确认各自目录及 CLI 优先级，检查没有按测试程序名写入 AppData。服务端 API 探针改用无窗口配置，避免浏览器子进程持有临时 Home 文件；原生窗口由上述独立门禁验证。Windows/Linux 有界门禁各通过 114 项 Python、85 项 Node、72 个模块解析、21 个运行探针和确定性打包；Windows 另通过 20 秒打包启动回归。根目录 `mdo.exe` 与 Windows 候选 SHA-256 同为 `b3ce5a4a2197a83c4ab61aac82b774f7de1db20ca379f52eccbf7bfe53ed1416`，Linux 包为 `e6149367e507122d8a4f497acbaa94c45b8b74c6fa51510b072311c6f7e2a229`。实体移动端和其他原生 WebView 尚待验收；未做压力或高负载测试。

同一阶段还在 320×350 的隔离打包浏览器页复核询问与审批的连续交接：回答询问后才出现审批卡，允许一次后安全 shell 调用返回 `exit_code: 0`，模型回复和 token 计量出现；重载后卡片仍在，页面无横向溢出和脚本错误。这只证明顺序到达场景，不证明两个待决请求同时到达时的稳定性。

## 2026-09-29：错误操作按钮跨语言切换保持键盘焦点

`tests/manual_packed_docks_qa.py` 增加仅在隔离代理中启用的 `--locale-hotkey`：向打包页注入 F9 测试键，由它调用产品原有的 `loadLocale`，不移动当前焦点，也不修改正式单文件内容。与 `--fail-first-run` 组合，在 Home `.build/mdo-packed-docks-jkd5yexv` 发送 `FOCUS I18N REPRO`，首次运行 POST 收到夹具 503，页面保留待核对项并显示“确认未发送后重试”。经键盘聚焦此按钮后按 F9，页面切成英语；重建后的 `Retry after confirming it was not sent` 仍获焦。按回车执行人工重试，夹具记录总共两次运行 POST（第一次拒绝、第二次转发），最终时间线恰有一条用户消息和一条 `UI fixture completed.` 回复、7 输入 / 3 输出 tokens，持久 `queue.json` 的 `items` 为空。这补齐了上一阶段尚缺的错误操作按钮跨语言焦点实测；语言切换由测试键触发，不能代替真实设置弹窗中的鼠标和键盘路径验收。

同一标签页第一次重载时，URL 仍指向上述会话，但页面在观察期内停留于“新任务”空壳视图；打开同 URL 的新标签页立即加载完整会话，原标签页第二次重载也恢复，服务端消息与队列均未丢失。本轮首次测试只有这一例，未确定属于应用初始化、代理还是浏览器控制器，故列为后续独立复现项，不把它计作刷新回归通过。另用浏览器自动化尝试拖动测试页构造的 `File`，接收页只得到 `text/plain` 而非文件，因此不能据此判断操作系统原生文件拖放是否正常；该设备级验收仍保留。

提交后再用同一打包字节、独立 Home `.build/mdo-packed-docks-76cusafk` 复核刷新：新标签页初载原会话正常，第一次重载又在会话 URL 停于“新任务”空壳，五秒后仍未自行恢复。此时页面 `readyState=complete`，入口及部分模块资源返回 200，但 Performance 记录中没有初始化 API 请求，浏览器错误日志为空；再次重载后 129 个静态/API 请求均完成、会话恢复。随后两个新标签页的首次重载正常，其中一个在重载前启用 CDP 网络记录，也没有请求失败或脚本异常。故现象可复现但并非每次触发，且只在测试用内嵌浏览器与代理组合中观察到；尚无法归因于 mdo、代理或浏览器控制器，未改动生产启动流程。下一步需捕获失败那次完整模块网络瀑布，并与原生 WebView2 的刷新对照。

已补做不经代理的对照：同一根目录打包字节在 Home `.build/mdo-packed-docks-8at52v86` 直接提供页面，新标签页初载及连续两次重载均进入原会话，输入框获焦。样本量仅两次重载，不能据此排除产品端的偶发问题，但暂时把代理及浏览器控制器的组合列为优先排查对象；原生 WebView2 仍需独立实测。

本阶段未修改产品应用代码。Windows 有界发布门禁通过 114 项 Python、85 项 Node、72 个模块解析、21 个运行探针、确定性打包、便携 WebView2 Home 检查及 20 秒单文件启动；Linux 有界门禁通过相同单元、模块与运行探针以及确定性打包（跳过 GUI 启动）。根目录 `mdo.exe` 已用新生成的 Windows 候选覆盖，SHA-256 为 `b3ce5a4a2197a83c4ab61aac82b774f7de1db20ca379f52eccbf7bfe53ed1416`；Linux 包为 `e6149367e507122d8a4f497acbaa94c45b8b74c6fa51510b072311c6f7e2a229`。未做压力或高负载测试。

## 2026-09-29：项目清除事务的锚定搬迁原语

xrt `ad47ae3d`、`6040abda` 增加 `xrtRootRenameNoReplace`：源与目标父目录均从根句柄逐段解析，拒绝中间符号链接、越界路径和现存目标，末级链接作为对象自身移动；Linux 使用 `renameat2(RENAME_NOREPLACE)`，macOS/FreeBSD 使用排他改名接口，Windows 使用目标目录句柄相对的 `FileRenameInformation`，不支持的平台失败关闭，不以检查后普通改名代替。Windows 模块及单头 `file_root` 套件通过；Linux 原生临时文件系统上的新增目录根与单头用例通过。Linux 全套 `file_root` 的大小写策略测试在修改前的锁定 xrt 基线和本次版本均失败，故不把它记为本次回归通过；macOS/FreeBSD 尚未实机验证。

xs `5f1e31a` 同步 xrt 与 xhttp 宿主单头、TCC 导出符号，并通过 Windows xwork/webview 宿主重建、35 项扩展单测及 xhttp vendored 字节核验。mdo 的依赖锁指向上述提交；`MdoHomeRenameNoReplace` 在 Home 锁内调用锚定 API，不创建 Home 或父目录。真实 xs/TCC Home 探针验证搬迁成功、已有目标不被替换。它只是项目清除事务的底层原语，项目设置仍只有只读预览和取消注册；跨进程项目租约、清单刷盘、逆向恢复和共享审计记录策略仍未落地。

Windows 有界发布门禁通过 114 项 Python、85 项 Node、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 与 20 秒打包启动，候选 SHA-256 为 `335481499760e91b821083aeb477b31c941f320fa51e264a76d513072f96d8f4`。Linux 在 WSL 的 Windows 挂载盘上运行迁移探针时遇到无覆盖改名失败；将同一 mdo 工作树及 xs 提交放入 WSL 原生文件系统后，完整有界门禁通过，确定性包 SHA-256 为 `b8d5cc25103412178d76aad05bae191baa83ce19f4c3f99f6972d6bbc19c4e39`。未做压力或高负载测试。

## 2026-09-29：入口模块失败时提供可重试启动状态

隔离代理第一次拒绝 `/js/main.js` 后，旧打包页保持会话 URL，却只显示未初始化的“新任务”空壳，且没有任何初始化 API 请求。原因是入口脚本的静态依赖解析失败时，其顶层 `boot()` 和错误处理均不会执行。页面现在用一个极小的内联模块动态导入入口，捕获导入失败及异步 `boot()` 失败；失败时使未初始化的工作区不可操作，显示有说明和“重新载入”按钮的可访问错误层，并把键盘焦点交给按钮。重载保留 hash 路由，现有语言包加载仍不阻塞正常启动。

`tests/manual_packed_docks_qa.py --fail-first-module` 只在隔离代理拒绝首次入口请求。修复前该夹具确定性得到空壳；修复候选单文件 Home `.build/mdo-packed-docks-vh4vz80l` 里，首次打开出现启动错误且重试按钮获焦，点击后同一 URL 恢复 “Packed docks QA” 会话，模型为 Ling 3.0 Tiny、权限为询问，输入框获焦；代理记录首次拒绝及恢复后两次队列读取。静态合同和前端模块解析均通过。先前偶发刷新空壳有一次已观察到模块请求返回 200，未证明是同一故障；它和原生 WebView2 刷新仍需单独排查。

Windows 有界发布门禁通过前端模块解析、85 项 Node、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 与 20 秒单文件启动；删除一项仅重复实现断言的静态测试后，最终工作树 114 项 Python 通过。Linux 在 WSL 原生文件系统中的独立拷贝通过同一单元、模块、运行探针和确定性打包（跳过 GUI 启动）。根目录 `mdo.exe` 已更新，SHA-256 为 `667cfe2055a21d6e7f8c7a6da3d48381a583e4ff4420753b384b47ab835f01c3`；Linux 包为 `25b503d695931a8a778a224de6599388103e7e2c7e45df0808a3c3b0d59651ea`。未做压力或高负载测试。

## 2026-09-29：启动长期无响应时提供恢复入口

上一阶段捕获了入口模块加载失败，但加载请求或 `boot()` 的初始 `Promise.allSettled` 中任一资源若一直不返回，原页面仍可能无限停在未初始化的新任务外壳。HTML 的独立启动层现设 20 秒观察上限：超时后遮住不可操作的工作区，显示原因、可聚焦的“重新载入”按钮，并保留当前 hash 路由。若慢请求随后完成且 `boot()` 成功，错误层自行收起，恢复工作区和键盘焦点；真正失败仍保留可重试状态。这个观察上限不取消网络请求，也不会自动重放用户消息。

隔离代理新增 `--delay-first-module-ms`，只延迟首次 `/js/main.js` 请求，最多 60 秒。候选单文件 Home `.build/mdo-packed-docks-w_8al53i` 延迟 60 秒时，约 20 秒后会话 URL 下出现超时说明且重载按钮获焦；点击后同一 URL 加载 “Packed docks QA”，Ling 3.0 Tiny、询问权限与输入焦点恢复。另一独立 Home `.build/mdo-packed-docks-sb02jfsc` 延迟 30 秒后未点击重试，最终自行进入该会话，输入框获焦。页面证据限于隔离浏览器和入口请求；初始 API 请求长时间不返回、原生 WebView2 及此前 200 响应空壳的具体根因还须分别验证。

Windows 有界发布门禁通过 114 项 Python、85 项 Node、72 个前端模块解析、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 与 20 秒单文件启动。Linux 在 WSL 原生文件系统的独立拷贝通过相同单元、模块和运行探针及确定性打包（跳过 GUI 启动）。根目录 `mdo.exe` 已重建，SHA-256 为 `6f13b13c738a90cea43d2b42598bf650cd71632e714ae718acb782d3cc4f4ac8`；Linux 包为 `653fb2ae27e182742a3b39b9e7b6913e895d89f19f57a10dd6eb90ddd5c7b538`。未做压力或高负载测试。

## 2026-09-29：代码块复制兼容缺少异步 Clipboard API 的 WebView

普通消息复制已有旧式 `execCommand("copy")` 回退，但 Markdown 代码块只调用 `navigator.clipboard.writeText`。隔离代理新增 `--no-clipboard-api`，仅在测试页隐藏该 API；修复前打包页的 `MARKDOWN UI` 回复点击“复制代码”显示“复制失败”，同页普通消息复制成功。现两处操作共用小型复制工具：优先使用异步 API，缺失或拒绝时尝试旧式路径；回退清理临时输入框并把焦点还给原按钮，确定失败才显示错误。

修复候选单文件 Home `.build/mdo-packed-docks-m6i67cww` 在上述隔离模式完成一轮回复，代码按钮显示“已复制”且保持焦点；剪贴板读取和实际粘贴均得到 `int answer(void) { return 42; }`。同页普通消息复制仍得到完整 Markdown 原文。三项 Node 用例覆盖异步成功、缺少 API 时的回退和异步拒绝后的确定失败。这里验证的是打包页与隔离浏览器的旧 API 路径；实体移动端和各系统原生 WebView 仍需验收。

Windows 有界发布门禁通过 114 项 Python、88 项 Node、73 个前端模块解析、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 与 20 秒单文件启动。Linux 在 WSL 原生文件系统的独立拷贝通过同一有界门禁（跳过 GUI 启动），Linux 包 SHA-256 为 `f464aac33084e5a60446cf81f8cc4f6bd96c3c710c2b12c8b9846270781d534d`。根目录 `mdo.exe` 已更新，SHA-256 与 Windows 候选同为 `50c9207109e66ec8b6961ddeda7aae3d2b4595f33ad3bd2507ce5256bc6ab833`。未做压力或高负载测试。

## 2026-09-29：缺少 Files 类型标记时仍接收文件拖放

旧版输入卡对拖放不检查 `DataTransfer.types`。新版只认其中的 `Files`；若 WebView 在拖入时仅提供 `items.kind=file`、放下时才提供 `files`，附件便无法进入选择流程。文件落在输入卡外时，浏览器还可能导航到本地文件并丢失未发送草稿。现在按类型、条目和实际文件三种证据识别拖放，输入区内交给原有上传与类型校验，外部落点阻止文件导航；纯文本拖放不受影响。

隔离浏览器夹具 `tests/fixtures/composer-drag-browser.html` 构造空 `types`、拖入时仅有文件条目、放下时仅有文件列表的事件。输入卡的 `dragover/drop` 均被接收，非图片文件进入原有“仅支持 PNG、JPEG 和 WebP 图片”反馈；输入卡外的两类事件均阻止默认导航，落点提示清除。纯文本的 `dragover/drop` 均没有被拦截，浏览器脚本错误为空。这证明生产模块对该 WebView 数据形态的处理，不代替 Windows 资源管理器或其他操作系统的真实拖放验收。

Windows 有界发布门禁通过 114 项 Python、101 项 Node、75 个前端模块解析、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 与 20 秒单文件启动。Linux 独立 ext4 工作树通过相同单元、模块、运行探针与确定性打包（跳过 GUI 启动）。根目录 `mdo.exe` 已从验证候选覆盖，SHA-256 为 `7e88df8fe1e75f245d339c4be42878fff76c88e133d79c059115b62357dc2513`；Linux 包为 `039a98e65c627eadaf2a991a28786214147d6138e5815b1c9cdb685361511ffe`。未做压力或高负载测试。

## 2026-09-30：打包页长模型回复复制全文

此前长消息按需复制的 Node 用例覆盖模型增量分片，最终单文件页仅实际复制过长用户消息。隔离模型夹具现用 `LONG RESPONSE UI` 返回固定 4,192 字符正文。Windows 单文件 Home `.build/mdo-packed-docks-pfd25ja5` 的常规事件 API 把该 `model_text_delta` 裁至 4,096 字符并设置 `text_truncated=true`，页面显示截断说明。点击模型回复的“复制消息”后，剪贴板内容与模型原文逐字一致，反馈为“消息已复制”；刷新同一会话后将剪贴板设为测试标记，再次点击仍复制完整 4,192 字符。浏览器脚本错误为空。

这次只扩展测试模型夹具，没有改动产品代码。夹具通过 Python 语法检查；根目录 `mdo.exe` 重新由锁定源码生成，SHA-256 仍为 `7e88df8fe1e75f245d339c4be42878fff76c88e133d79c059115b62357dc2513`。上一阶段的 Windows/Linux 有界发布门禁覆盖同一产品源码；本阶段的新增验证是上述真实打包页操作，不声称已覆盖其他原生 WebView 或实体设备剪贴板。

## 2026-09-30：明确会话不等待宿主状态读取

`boot()` 原来把 `/bootstrap` 与会话列表、模型目录和运行恢复等请求一起放进初始 `Promise.allSettled`。`/bootstrap` 只更新本地服务状态和初始化失败提示，却会阻止启动 Promise 完成；明确会话即使已经载入且输入可用，20 秒启动观察上限仍会误判为未启动并遮住工作区。现在该读取跟随任务、审批与管理资源在后台完成，宿主状态订阅照常更新 UI 和真正的初始化失败提示。

隔离代理 `--startup-bootstrap-delay-ms 30000` 只延迟首次宿主状态 GET。旧单文件 Home `.build/mdo-packed-docks-gxdruq3m` 中，原会话“Packed docks QA”及输入框已可用，但根元素仍是 `loading`，20 秒后变成 `timeout` 且遮罩盖住会话。候选 Home `.build/mdo-packed-docks-vizufmuz` 在同样延迟期间已是 `ready`，原标题与可用输入保持，遮罩隐藏；延迟结束后状态改为“本地服务 0.1.0-dev”，浏览器脚本错误为空。这个受控案例不能说明此前静态模块请求均为 200 的偶发空壳具有相同根因，仍需在该故障当次抓取模块执行和原生 WebView2 状态。

Windows 有界发布门禁通过 114 项 Python、101 项 Node、75 个前端模块解析、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 与 20 秒单文件启动。Linux 独立 ext4 工作树通过同样的单元、模块、运行探针和确定性打包（跳过 GUI 启动）。根目录 `mdo.exe` 已从验证候选覆盖，SHA-256 为 `7aeab0d241d47a943813ef82eb193ce311d85ddc5fccc7d8929baf54924b6b42`；Linux 包为 `4df2b57c6e18681c7b104d2b19b97a010dd23a9a32949573b0b0f08b5c8bbfc2`。未做压力或高负载测试。

## 2026-09-30：明确会话不等待侧栏与选择目录

`boot()` 仍把初始会话列表和模型、Agent、项目目录与运行恢复一起等待。明确会话 URL 已经指定目标，当前会话详情及队列可独立读取；侧栏和选择目录的慢响应不应让启动 Promise 留在 `loading`，更不应让 20 秒遮罩盖住原会话。现在所有目录请求照常立即发出并更新各自视图，但仅在没有明确会话 URL、需要决定“继续上次任务”目标时作为启动门槛。运行与恢复状态仍在门槛中，未放松发送前的安全核对。

隔离代理 `--startup-catalog-delay-ms 30000` 同时延迟首次 `/api/v1/sessions` 和 `/api/v1/models`。旧单文件 Home `.build/mdo-packed-docks-e5alvk6j` 在“Packed docks QA”标题和输入已经可用时仍是 `loading`。候选 Home `.build/mdo-packed-docks-j0hmnaui` 在代理确认两条请求仍被延迟期间进入 `ready`、保持原会话且无遮罩；实际发送 `CATALOG DELAY UI` 后，服务端记录一次队列 POST、一次运行 POST，页面显示固定回复及 7 输入 / 3 输出 tokens，浏览器脚本错误为空。这个受控目录延迟不能解释先前静态资源均返回 200 的偶发空壳；原生 WebView2 与实体设备仍需验收。

Windows 有界发布门禁通过 114 项 Python、101 项 Node、75 个前端模块解析、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 与 20 秒单文件启动。Linux 独立 ext4 工作树通过同样的单元、模块、运行探针及确定性打包（跳过 GUI 启动）。根目录 `mdo.exe` 已更新，SHA-256 为 `bf5e097f3ead77d7b23ad5539a1aa2d26416986a2fcf33f4b55ace7f6fb91a20`；Linux 包为 `53c9f23ec3b01e4c212b6729f7baf69f7591bbbc3ac8e11f4fc59322326973c5`。未做压力或高负载测试。

## 2026-09-30：原生 WebView2 单文件窗口对照

之前的发布门禁只确认 Windows 原生窗口能打开、WebView2 数据写入便携 Home、进程能存活 20 秒，不能证明页面实际绘制出可用内容。新增 `tests/native-webview-snapshot.ps1`：把指定单文件包复制到独立目录，可选复制会话 Home；等待真实原生窗口，以 `PrintWindow` 抓取窗口内容，随后正常关窗。截图留在被忽略的 `.build/native-webview-qa-*/window.png` 供人工检查；脚本不把像素数量当作启动成功判据，也不发起模型调用。

对 SHA-256 为 `bf5e097f3ead77d7b23ad5539a1aa2d26416986a2fcf33f4b55ace7f6fb91a20` 的根目录 `mdo.exe` 做两次有界检查。全新 Home `.build/native-webview-qa-4701858b2eeb4edd8e0031563fff0b0a` 的原生窗口显示新任务欢迎页、焦点框、Ling 3.0 Tiny、思考强度和权限。会话 Home 从此前目录延迟验证夹具复制到 `.build/native-webview-qa-659fda46249f4e549bd9ed364cc50dd0`；原生窗口直接恢复“Packed docks QA”，显示上一轮用户与 Agent 消息、复制/分叉/重试/反馈入口、7 输入 / 3 输出 tokens 和 token/s，输入区可见。两个窗口均在约 5 秒抓图后正常关闭，原始夹具 Home 未被修改。

本项缩小了“原生 WebView2 完全不能执行新前端”的可能性，不能推断偶发空壳已解决。两次没有复现该故障，且窗口截图不包含模块执行状态、网络响应或操作结果；真正故障发生时仍须抓取这些证据，原生点击、文件拖放及实体移动端仍待验收。Windows 有界发布门禁再通过 114 项 Python、101 项 Node、75 个前端模块解析、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 与 20 秒单文件启动；重新生成的包与根目录 `mdo.exe` 同为上述 SHA-256。此阶段仅新增手动 QA 脚本与文档，产品源码和打包字节不变；不做压力或高负载测试。

## 2026-09-30：慢运行状态读取时保持待发闸门

明确会话的路由加载已经读取当前运行、恢复状态及队列，并在完成前持有派发闸门；`boot()` 另发一组运行/恢复读取并等待，慢响应可能触发启动超时。先移除这组重复的启动读取后，隔离代理延迟 `/api/v1/runs` 暴露出更重要的竞态：同会话的详情刷新使用同一个“最新加载”闸门，能在首次运行核对仍悬而未决时替它解除派发阻断。现将运行核对与详情刷新分为两个加载种类；同类新加载仍可取代旧加载，但详情刷新不能解除未完成的运行核对。页面可先恢复，会话输入可以写入持久待发队列，运行仍须等核对完成。

`tests/manual_packed_docks_qa.py --startup-runs-delay-ms 30000` 延迟前两次运行列表 GET，并记录单调时钟。未修复闸门的候选 Home `.build/mdo-packed-docks-ck0injoz`：首次 GET 于 `1717151.656` 开始延迟，队列 POST 于 `1717151.953`、运行 POST 于 `1717152.031`，首次 GET 到 `1717181.656` 才返回，证明运行过早启动。修复后 Home `.build/mdo-packed-docks-7ywu6pcr`：首次 GET 于 `1717505.921` 开始，队列 POST 于 `1717506.218`；直到该 GET 于 `1717535.921` 返回后，才于 `1717535.968` 产生唯一运行 POST。页面在读取未完时已是 `ready`、标题为“Packed docks QA”且无遮罩；最终显示 `GATED RUN UI`、固定模型回复和 7 输入 / 3 输出 tokens，浏览器错误日志为空。代理最终计数为一次队列 POST、一次运行 POST。

`tests/test_queue_gate.mjs` 直接验证详情刷新不能释放运行闸门、同类新运行核对仍能取代旧核对，以及人工复核阻断独立于加载闸门。这个受控延迟修复不等于偶发 200 响应空壳的根因已知；原生 WebView2、实体移动端及其他加载失败路径仍须继续核对。

Windows 有界发布门禁通过 114 项 Python、103 项 Node、75 个前端模块解析、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 与 20 秒单文件启动。Linux 独立 ext4 镜像通过相同的单元、模块、运行探针及确定性打包（跳过 GUI 启动）。根目录 `mdo.exe` 已用验证候选更新，SHA-256 为 `31d15a3692bad1b797edf62e04eed4cd6bb9d2acf3a58656b8b0c9faf502cf09`；Linux 包为 `f8b5cacdb6dfc4069d8561a3b2df1795876900d6e002821f4228f6657008a0f1`。用新根目录程序与上述已完成会话 Home 再次打开 Windows 原生窗口，截图 `.build/native-webview-qa-080bec5ab2f24a8d92254bdb7e704dea/window.png` 中可见同一回复、操作按钮和用量；该截图不证明原生交互或偶发空壳已解决。未做压力或高负载测试。

## 2026-09-29：项目级新任务草稿存储接口

代码审计确认新任务在首次发送前共用 `data/draft.json`：切换项目时 `selectedKey` 仍为空，只有写前日志生成 `new_task` 后才阻止跨项目导航。因此未发送文本会跟随项目切换；旧版按懒会话 ID 分开保存草稿。新接口 `GET/HEAD/PUT /api/v1/projects/{project}/draft` 为合法项目 ID 提供独立的 `data/project-drafts/{project}.json`，沿用 revision 冲突、64 KiB 文本上限和 Home 原子写入。空草稿 GET 不创建文件；项目定义尚未建立时也可保存，因为现有新任务允许先选择默认或未登记项目。此接口只接收文本与可选输入配置，不接受图片、待提交意图、`new_task` 或运行核对标记；创建中的写前日志仍由全局 `/draft` 保存。

API 探针验证两个项目的文本及 revision 相互隔离、空读取零写入、HEAD、旧 revision 冲突和非法路径/提交载荷拒绝。此提交是前端接线前的存储阶段：当前打包界面仍选用全局草稿，跨项目文本串用尚未修复；下一阶段必须接入项目键、迁移旧全局文本，并在打包页验证切换、刷新、发送和创建失败恢复。

Windows 有界发布门禁通过 114 项 Python、88 项 Node、73 个前端模块解析、21 个运行探针、严格 C 编译、确定性打包、便携 WebView2 Home 和 20 秒单文件启动；候选 SHA-256 为 `9b7204bf06ffe1b2d08b7900e80d887fbd764ca70a0f97aeccf683097aaa262f`。Linux 在 WSL 原生文件系统的独立拷贝通过相同的有界单元与运行探针及确定性打包（跳过 GUI 启动），包 SHA-256 为 `e6b98d4c91e1b0a7e875a6ac63d8e00d3529ae2b746b23b581fd8a928db3d41f`。未做压力或高负载测试。

## 2026-09-30：移动端待决卡片接管空输入框焦点

320×350 隔离打包页中，同一模型回复先产生询问，回答后再产生审批。修复前，两张卡到达时空白主输入框仍保持焦点；真实软键盘可能占住决策空间。现在只在移动布局且主输入为空、没有附件、输入法没有组字时，将空输入框或页面根节点的焦点交给新决策标题。已输入的下一条草稿、附件和其他正操作的控件不被抢焦；打开抽屉时原有决策定位逻辑保持不变。

候选单文件 Home `.build/mdo-packed-docks-7s7oekha` 实测 `SEQUENTIAL DECISIONS UI`：询问到达后焦点在“需要你回答”，点击 Inspect 后审批标题获焦，允许一次得到 `exit_code: 0`、固定模型回复、待决卡清空。第二轮 `ASK UI` 模型延迟时预先输入 `Draft for the next turn`，询问到达后草稿与主输入焦点都保留。页面宽度等于 320px，浏览器脚本错误为空。这证明的是服务端依次推进的两张决策卡，不证明两张卡同时待决；实体手机软键盘仍需验收。

Windows/Linux 有界门禁均通过 114 项 Python、113 项 Node、77 个模块解析、21 个运行探针和确定性打包；Windows 另通过便携 WebView2 Home 与 20 秒打包启动。根目录 `mdo.exe` SHA-256 为 `c9e6f573957f3dd26712df6eef14e3b1afc0db0457354b5c05535d7921ff7644`，Linux 包为 `4922f340b0b272987b2d4383d89546205275c381e2940287a0265b6bf631aad2`。未做压力或高负载测试。

## 2026-09-30：自由回答时保留问题回看入口

生产停靠卡在 320×700 布局、软键盘仅把可见视口缩至 250px 时，原来会把自由回答输入框滚进仅 76px 高的卡片，但询问正文和手动展开入口都在屏幕上方。正在作答的用户无法回看问题。现在只在可见视口压缩、询问输入框获焦且停靠卡拥挤时自动展开决策层；标题和展开入口保留在层顶，回答框与提交按钮留在可见底部。卡片仍可上滚读完整问题、下滚继续回答；Esc 或展开按钮的手动收起会保持到软键盘场景结束，恢复视口后自动回到普通布局。

新增 `tests/fixtures/ask-keyboard-viewport-browser.html` 通过生产 `createConversationDocks` 与 `trackMobileViewport` 模拟该时序。隔离候选单文件 Home `.build/mdo-packed-docks-c6l77t44` 经 `--ask-keyboard-viewport-fixture` 从候选包读取 CSS/JS：修复前同场景标题位于 y=-135–-116、展开入口 y=-135–-95；修复后入口 y=57–97、输入与提交 y=202–242。滚到卡片顶部时 72px 高的问题段落完整位于 y=127–199，滚回底部后回答框可见；Esc 不会立即重新展开，布局高度恢复后输入值和焦点仍在。脚本错误为空。此验证使用模拟 `visualViewport`，未代替实体手机软键盘与触控验收。

Windows/Linux 有界门禁通过 114 项 Python、113 项 Node、77 个模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒启动。根目录 `mdo.exe` SHA-256 为 `0528a2745cedbbbea2314022d79600a4725dd6dac142fed0d0ea9a8790a0d3a3`，Linux 包为 `a0c5922c4059cc6817e1ee95e48f5c385018dae42b7b66cff021c18cc96fe891`。未做压力或高负载测试。

## 2026-09-30：消息操作失败提示与弹窗焦点

旧版支持中英俄切换，新版核心消息操作的成功路径已经恢复，但会话修改、分叉、截断及反馈中的 11 个稳定 API 失败码仍直接显示英文服务端原文。现为这些码补齐三语提示，保留未知错误的原始详情；其中“会话已更改、反馈未同步”明确提示先刷新核对，避免重复操作。三语语言包各 1264 键，`test_frontend_i18n.mjs` 核对每个新增码在三种语言下的文本。

隔离候选单文件 Home `.build/mdo-packed-docks-6e4vvo5_` 的代理首次拒绝分支 POST，返回真实稳定码 `session_fork_invalid` 和英文详情。320×350 页显示中文错误，错误位于 y=249–266、确认按钮 y=287–327，文档宽 320px；修复前失败后焦点掉到页面根节点，修复后聚焦错误提示，Tab 可进入操作按钮，重试只创建一条独立分支并回到新会话输入框。Home `.build/mdo-packed-docks-jhn2e8no` 的首次新任务创建返回 `session_profile_invalid`，错误位于 y=231–266、创建按钮 y=287–327，焦点停在错误提示；仅用键盘重试后创建成功，焦点回输入框。两页脚本错误为空。项目取消注册弹窗也补上失败焦点，但本轮没有为该失败分支做打包页实测。

最终 Windows/Linux 有界发布门禁均通过单元、77 个前端模块解析、21 个运行探针及确定性打包；Windows 另通过便携 WebView2 Home 和 20 秒单文件启动。根目录 `mdo.exe` SHA-256 为 `644a1867bad9975136f74c4076095fd0db3ff9e97524930520ac5464a6b34663`，Linux 包为 `d94a657a76aca53608901eadc7e7600d62beab50b1dbfb994719b94fb7d3b48f`。原生手机触控、软键盘以及其余服务端失败码的本地化仍待验收；未做压力或高负载测试。
