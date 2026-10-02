# 单会话恢复的 Home 存储事务

本阶段实现 `MdoHomeSessionRestoreBegin/End` 和启动回收，及 Stage 的目录身份
输出/无删除释放。它是存储边界，不是正式导入入口。生产项目/workspace
核对、新 ID 预留、session/catalog 管理器同步、异步 worker 和页面仍待接入。
备份的 `restore_ready:false` 保持，不将存储成功解释为用户执行授权。

## 调用顺序

1. 上层核对实际目标项目、workspace 和版本，取得项目/会话协调所有权并预留
   新 ID。源模型/Agent 配置是历史描述，恢复不会调用它们。
2. `Begin` 接受项目 ID 和恰好 32 位小写 hex 会话 ID，在同一个 leased Home
   内创建 `.mdo-session-restore` 和 `payload`，先保存不可变 `owner`，再返回
   独立的目录锚。空目录往返重命名预先检查非覆盖目录移动能力。
3. 上层将 `PrepareRestore` 的结果交给真实 `StagePrepare`，再 `StageCheck`。
   取消/配额/语义失败进入中止；这段昂贵工作必须运行在有界 worker。
4. 保存成功的 `StageInfo`。其中 `DirectoryIdentity` 只在 Verified 时供发布
   使用；重新检查失败会清除它。关闭 caller 的 parent，使用 `StageRelease`
   关闭 Stage 的全部锚和独立 bytes。它不删除文件；此时 journal 已承担回收。
   独立 Stage 仍使用 `Discard`，不能用 Release 留下无所有者目录。
5. `End` 一次性消费当前事务，检查 owner 与内存中的项目、目标和目录身份
   一致，完整扫描唯一的 `restore-<32hex>`，核对 Stage 验证所得 root 身份。
   Publish=false 中止；Publish=true 保存不可变 `ready` 后进行一次非覆盖
   `payload/<name> -> sessions/<project>/<id>` 重命名。目标已有对象均拒绝。
6. 上层按 `Committed` 同步管理器/列表，并发布操作结果。提交后清理失败
   仍保留 Committed=true，Home 隔离写入并要求启动恢复；不能重试执行。

End 前必须关闭所有暂存 writer 和目录锚，否则 Windows 可能阻止目录
退休/删除。存储锁只覆盖 begin/end/recovery，不跨 Stage 或模型验证持有。
普通不相关 Home 操作仍可用；第二个 restore、Home 导入、project purge
不能与本事务同时开始。上层仍须防止项目定义或目标会话的并发操作；
shared project lease 本身不冻结项目 workspace 的编辑。

## 提交与恢复

owner 记录版本、目标项目/会话、journal 和 parent 的 Device/Identity。
ready 记录 Stage 目录名及 Device/Identity。记录最多 2048 字节，使用严格
有限 JSON reader；临时文件独占创建、flush 后以非覆盖重命名发布。
所有操作使用 Home 根锚，不重新打开显示路径，不跨文件系统移动。

| ready 后的位置 | 恢复处理 |
| --- | --- |
| 私有 source 保留，live target 缺失 | 尚未提交，清理私有 source |
| 私有 source 保留，target 是外来对象 | 碰撞，保留 target，清理私有 source |
| source 缺失，target 身份等于 ready | 已提交，保留 live target，清理 journal |
| source/target 均缺失、身份替换或两处同身份 | 矛盾，拒绝启动写入，保留证据 |

重命名返回 false 也可能已经完成；以实际位置/身份决定 Committed。中止
不回滚已经发布的会话。清理前将整个 journal 退休为
`.mdo-session-restore-cleanup`，退休目录只有清理语义，永远不再解释 live
target；因此清理中断或 live target 后续正常写入不会造成错误回滚。

HomeInit 在任何项目/session/runtime 管理器启动前运行该恢复。相互冲突
的其他 journal、损坏 owner/ready、链接、替换身份或外来文件都整体失败。
普通 Home API 拒绝两种内部根目录及其大小写、尾随点/空格别名。

ready 前，持久 owner 授权的是私有 parent 中唯一的格式内 payload，允许
回收创建到一半的正常文件；不是逐文件持久身份日志。Stage 在运行时仍
使用原有逐文件身份检查。共享纯函数 `session_file_policy.h` 给 capture、
decode、Stage 和启动存储回收提供同一 whitelist，避免清理格式漂移。
整棵树先扫描再删除，每次删除重查身份；最多 4096 节点、1024 文件、
64 MiB 总字节、三层深度和各类型较小限制。只接受常规文件/目录，不
跟随链接，不删除 `.runtime.lock`、临时/backup 或未知路径；未知内容
保留并要求修复。不能把其他内容写进私有 journal 后要求程序代为清理。

本机制提供进程中断恢复。xrt 的目录重命名没有目录 metadata fsync 合同，
本阶段不宣称断电后的持久性，也未修改底层 xrt/SDK。

## 验证与证据边界

`test_home_restore_runtime.py` 通过真实 xs/TCC、native Home root 和单进程
lease，使用三份小文件验证发布/中止、前后两次目标碰撞、错误的验证
身份、提交后 close-error、清理失败隔离、精确 checkpoint 进程中断、
重复启动、owner/ready 损坏、source/target 身份替换、位置缺失、冲突
journal、未知文件和目录链接（Windows junction/Linux symlink）。缓存及
另一个会话的固定哨兵均保持。无效参数不创建 Home。

`test_backup_stage_runtime.py` 的仅测试 adapter 另将真实 v2 备份执行
PrepareRestore -> Home Begin -> Stage 写入/独立 replay/pixel 检查 -> 再检查
-> Release -> End。源备份/caller 锚提前释放，源 Home 原有文件逐个保持。
发布目标的 model/resource 字节、staged queue、来源记录和新 metadata
独立从磁盘核对；真正的生产历史 artifact HTTP reader 从 target Home
读取首 64 KiB，完整大小/SHA、event/artifact ID 匹配，两次发布拒绝覆盖。
HTTP 和 TLS 均验证。测试 adapter 没有目标项目定义/保留 ID/catalog
notification，也没有生产 restore route；不能冒充上层事务已完成。

初始严格编译发现 unity 私有函数同名，改用 Home 专用命名；首次真实
发布探针对可选 journal 文件的存在假设错误，已按源文件清单修正。
首轮完整 Windows 门禁中坏图片仍正确拒绝，但 cleanup 返回保留句柄；
修正旧探针的“失败必无目录”假设，记录原错误/cleanup 错误，并仅对该
fixture 自己的已知文件进行两秒以内的安全 Discard 重试，持续障碍仍
失败。一项仅复制源码的确定性单次删除障碍验证保留句柄及实际重试，
HTTP/TLS 均覆盖。未知文件/替换身份案例仍先拒绝删除，只有探针移除自己的障碍后
才重试，不通过放宽生产清理绕过错误。Linux 首轮完整门禁发现 schedule
手写 TCC 夹具漏拷新 Home header，同时核对并补齐 settings 的同类清单。
保留初始日志，定向验证随后通过。不做压力或高负载测试，不增加设备/UI
验收声明；原生窗口、实体手机及 Linux queued HEAD reset 仍有待办。

最终两平台完整门禁均通过 115 Python、252 Node、90 JS 模块解析、严格
C11、41 runtime、三项 packed 及 A/B；Windows 另通过便携 WebView2 Home
和 20 秒启动。27 份改动代码/探针跨平台按 LF 核对，SDK/库 pins 不变。
根目录程序 6,421,862 字节、SHA-256
`309365856566514e9bc5cfb0fe7a3bafef903122774694e184152367579a69a5`；
Linux A/B 6,472,022 字节、SHA-256
`4b606e556d79e724eaee2c8557c9dd98c27fb9b4141f26d7d76033dd44a8aeef`。
日志 `.build/qa-home-restore-{windows,linux}-final.log`，源码核对
`.build/qa-home-restore-source-equivalence.log`。没有新增 native/device 交互
证据，正式 coordinator/worker/页面继续实施。
