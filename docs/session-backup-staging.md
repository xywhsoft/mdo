# 会话备份的独立暂存层

本层将已拥有解码的 v2 备份落到独立目录，再从磁盘重新读取验证。它是完整
恢复事务的输入，不是会话发布接口。当前没有生产 HTTP 路由或前端恢复按钮，
不改变 `restore_ready:false`，也不将旧 v1 模型快照视为完整备份。

## 入口与生命周期

`MdoSessionBackupStagePrepare` 接受 immutable decoded backup、已经存在的
父目录 xroot、可降低的预算、可选取消及初始为 NULL 的输出指针。父目录
必须由应用独占管理，位于最终发布所用文件系统，且在 live session catalog
之外。调用只借用输入和取消；返回后可关闭 caller root 并释放原备份/上传。
成功 Stage 自己拥有父级锚定句柄、磁盘读回字节、路径身份及检查结果。

父级句柄长期保留，子目录句柄只在一次同步操作期间打开。Windows 实测在
持有嵌套目录句柄时拒绝移动其祖先；因此每次操作按父根内相对路径重新打开
子目录，并对照记录的目录身份，返回前关闭。父目录在空闲时用原生根内
非覆盖改名移动，旧显示位置被另一个目录替代后，检查和清理仍操作原锚定
父目录。代码不重新打开 native 显示路径。

`StageInfoGet` 返回 copied facts，检查 Size 后才写输出；`StageCheck` 再次从
磁盘验证。任何检查失败都撤销 Verified、清空模型/图片检查事实；它不是修复
接口。单个 Stage 同时只能由一个调用方操作，必须在 worker join 后才能
退休对象或 TCC 代码，不允许网络回调同步执行整份 materialization。

## 创建、读回与检查

目录名是 `restore-<32 lowercase hex>`，安全随机生成、排他创建，名称碰撞
最多 32 次；碰撞目录不进入拥有索引。每个文件及父目录复用备份 whitelist，
不接受绝对路径、外来文件或来源 workspace 作为目标。文件排他创建、末级
no-follow、权限 0600，目录 0700，分块写入，flush/close 后读回。

打开文件的 identity/device/type 与创建记录匹配，长度相同；每 64 KiB 的
读回字节与输入逐块比较，读后再查句柄身份/长度及根内叶身份。独立 inventory
仅接受创建过且身份匹配的普通文件、目录，不把扫描所得对象视为可清理对象。
随后对拥有的磁盘读回字节运行共用 schema/CRC/引用检查、真实保留 UI reader、
模型/UI 关系检查和静态图片像素检查。后两项使用 xllm-session 的真实无绑定
恢复核心，没有客户端、模型调用、shell、队列或 catalog 操作。

所有阶段共享从接受开始的 30 秒协作预算，最多 1024 文件、单文件 32 MiB、
总字节 64 MiB；格式的更小单项预算仍适用。父目录数最多文件上限加五，
inventory 深度/节点数受拥有索引约束。原 immutable backup 和一次读回文件
集合可能同时在内存中，材料化没有生成第二份 base64 文档。取消/截止时间
在分块 IO、文件、扫描与语义门禁之间检查，不能抢占单个原生操作或 codec
循环。本层没有总进程堆内存硬限制。

## 失败与清理

`StageDiscard(&Stage, Error)` 只删除跟踪到、身份仍匹配的文件以及空目录，
按反向创建顺序清理，不使用递归删除。外来文件、同字节但不同身份的替换
对象或不再匹配的根阻止清理，留下该对象和非 NULL handle；调用方处理自己
的障碍后重试。Prepare 失败先尝试相同清理；清理受阻时输出保留 handle，
不能因 Prepare 返回 false 就丢弃它。取消不阻止清理。完成后关闭父句柄、
释放字节并置 NULL，重复 discard 是成功空操作。

这个合同覆盖应用内单 owner 的同步生命周期，不是对任意外部进程同时修改
目录的原子锁。强制终止进程可能留下私有 staging，后续生产事务还要增加
持久 ownership/provenance 与启动回收；不得扫描未知目录后自动递归删除。

## 恢复事务仍需完成的部分

本阶段保留原 metadata、UI 身份、artifact 显示路径及 draft/queue/receipt 原
字节，仅用于后续转换。Verified 意味着落盘字节和离线检查通过，不意味着
没有旧历史/图片缺口，也不授权发布或自动续行。Info 保留 Unverified/Removed
关系、模型投影及未验证图片统计，不能把这些报告隐藏成“完整恢复”。

下一步在独立 stage 中完成新项目/会话身份及来源记录、明确选择 workspace、
artifact 路径重绑定、删除标记对应的侧车投影修复、队列/提交意图的待确认
转换、实际产品 UI replay，再做根内原子不覆盖目录发布和 catalog 通知。
正式页面入口和单文件跨 Home 导出/恢复验证仍按实施计划步骤 5–6 完成。

## 有界证据

新增 `test_backup_stage_runtime.py` 在隔离源拷贝上使用测试专用 HTTP/TLS
adapter。一个真实三轮产品 run 产生保留模型/UI/inline PNG，另带草稿、
待确认队列和普通 2 MiB artifact。检查全部落盘文件原字节、源对象释放后
重查、caller root 关闭、锚定父目录移动/旧位置替换、全部预算预拒绝、预取消、
artifact 第一块后的取消、同身份内容篡改、同字节外来身份、外来文件留存及
清理重试。schema-valid 的 UI/model 矛盾和只有 PNG 头的损坏图片在对应
语义门禁失败，之后能重试。v1 明确拒绝暂存，重复输出不替换原 Stage。
整个源 Home 的文件字节与 catalog/队列不变，没有压力或高负载测试。

最终 Windows/Linux 有界门禁通过 115 Python、252 Node、90 模块解析、严格
C11、40 runtime、三项 packed 及独立 A/B。Windows 另通过便携 WebView2
Home 覆盖/移动重启和 20 秒启动；SDK 不变，复用已验证 native host。
Linux 采用新 ext4 源 staging，六个变更源/清单/探针文件内容与工作区一致。
root mdo.exe 已更新为 6,395,308 字节，Windows SHA-256
`3c2c970924292a406394c376aecbe4b87bad5e3fda4eb9f358054327e2064eae`；
Linux `8958084d30e58f5725df3b15dabdf2fbb608f7555bb788e1dc2df9619aba0e82`。
日志 `.build/qa-staging-{windows,linux}-final.log`，初始清单断言失败日志另存。
本轮没有新增原生点击/实体设备证据；既有 Linux queued HEAD reset 未关闭。

2026-10-02 后续增量：已增加独立的
[历史投影修复](session-backup-projections.md)，可在 StagePrepare 前生成
拥有副本，再用本层落盘和验证。它仅处理有证据的侧车变化，仍保留来源
身份和 queue 原字节；StagePrepare 不自动做转换，生产恢复事务待接入。

同日已提供独立的 [输入待确认转换](session-backup-inputs.md)，可将新副本
交给 StagePrepare。转换保留历史回执，使用新 ID 的 staged/rejected 状态，
落盘后实际前端控制器的刷新/核对只读探针通过。StagePrepare 仍不自动
执行转换，也不保存来源意图/Facts；最终恢复事务需持久 provenance、
身份/产物重绑定及原子发布，不能直接将这个 Stage 纳入 live catalog。
