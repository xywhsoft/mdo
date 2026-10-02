# 宿主产物目录与项目权限分离

此前 mdo 把工具产物放在 Home 下的会话目录，xwork 却通过项目 workspace
策略解析这个路径。Home 在项目外时，较大的工具输出保存失败。只读 profile
还把内部产物保存与项目写权限绑定，导致完整输出被丢弃，只剩预览。

## xwork 3.8 / ABI 7

宿主先取得一个已存在、已锚定的 xrt 目录，然后调用
`xworkArtifactStoreCreate(Directory, Error)`。对象克隆目录句柄并拥有自己的
生命周期；宿主可以立即关闭原 Directory。在 `xwork_agent_options` 中设置
`pArtifactStore` 后，Agent 保留引用；宿主释放自己的 store 引用。现代和兼容
子 Agent 路径传递同一 store，registry entry 在 Agent 释放后仍保留读取所需
引用。最后一份引用释放才关闭句柄。

对象覆盖 `sArtifactDirectory`。run/file 名由库生成，沿用非覆盖原子发布、
碰撞预算、完整写入及预约配额机制。读取也通过 store 根和已登记的相对路径，
末级 no-follow；不重新打开 metadata 的显示路径。目录移动或原位置换成另
一份相同长度文件，不会把 registry IO 引到替代位置。显示路径由创建时的
`xrtRootPath` 按当前目录规范化，之后可能过时，只用于展示和追溯。

Store 是显式宿主能力，不接受模型提供的任意路径，也不扩大普通文件工具
的 workspace 范围。没有 store 的调用方继续使用 workspace 内目录。只读
子 Agent 仅在父级显式交入 store 且允许产物保存时保存内部输出；默认项目
目录的 spill 在只读子 Agent 中关闭。宿主须在 import/purge/Unit 前停止原生
写入者；目录克隆不是撤销写能力的替代品。

公开 Agent options/config 增加指针，ABI 从 6 升为 7；event schema 仍为 3。
需重编译使用旧记录布局的调用方。mdo 模块 ABI 1 保持，默认工具目录没有
增加新模型工具。xs 注入 172 个公开函数，其中新增三个 store 生命周期 API。

## mdo 接入

`MdoHomeOpenStorageDirectory` 在 Home 锚定根内检查/创建目录，返回独立拥有的
根句柄。它拒绝非法相对路径、已有文件或检查时已有的 link 父目录，并遵守
Home import/restart 的写入冻结。mdo 将会话产物绝对路径换成 Home 相对路径，
从这个 API 创建 store；Home 外的宿主参数不能退回普通 native IO。

只读会话也保存 Home 内的完整工具输出。项目写权限和写后验证继续由 profile
的 workspace effects 决定，不能由“允许内部产物保存”推导。ephemeral Home
继续关闭产物保存，不为了它允许写到项目或其他目录。

运行时 registry 通过 store 读取；历史 UI 产物 API 仍通过持久 event ID 和
当前 Home 的受校验目录读取，支持整份 Home 在关闭后移动并重新启动。
普通 `read` 的项目路径限制保持；外部产物通过 registry/历史产物 API 查看，
本轮未新增模型工具的 artifact URI 读取约定。

## 有界证据

源库夹具核对空 anchor 拒绝、caller root 关闭后的 store 引用、Home 外写入、
普通路径策略拒绝、目录移动/旧路径替代、后续写入仍在原锚定目录和 Agent
销毁后读取。Windows/Linux 的完整库测试通过；无压力或高负载测试。

新增真实 mdo xs/TCC 探针连续启动三次，使用外置 Home、同一项目和会话、
只读 profile、本地模拟模型驱动四次 callback。每轮 read 超过 inline 阈值，
通过产品桥接产生真实 UI 记录；项目外读取及项目内写入被拒绝。第二次重启
和整份 Home 移动后的第三次启动均保留原产物字节，以持久 event ID 经真实
HTTP API 读回全部历史输出、长度及 SHA-256。项目输入和外部夹具不变，项目
里没有生成 artifact 目录。还核对 Home 外 store 参数、`..`、文件叶和重启
冻结时新 store 获取失败，没有网络模型或真实用户 Home 数据参与。

源库、xs 同步、mdo 依赖及平台日志分别追溯。本阶段修复产物存储接入，不
证明 staging/完整恢复、正式菜单或原生/实体设备验收已完成。

源库提交 `0f597d26749b98ab49b87f57e9945e6b1254f2c9`；xs 同步提交
`9bb75a5ab18a97816abd561f571be410affd758d`。20 个生产文件的规范化树
SHA-256 为 `c16186fcaa7867b5bacdaae3b94e37aed462e2c508de6d4cb4ef48de456abc69`，
均通过 `deps.lock` 固定；xrt 核心没有修改。初始库夹具的显示路径绝对/
相对断言失败已修正为创建时规范化显示路径，再重跑两平台完整库测试；
原始失败日志保留。Windows 既有 link 夹具因创建权限明确跳过。

最终 Windows/Linux 门禁均通过 115 Python、252 Node、90 JS 模块解析、
严格 C11、39 runtime、三项 packed 和独立 A/B。Windows 另通过便携
WebView2 Home 覆盖/移动重启及 20 秒启动。根目录程序已更新，Windows
A/B SHA-256 `c4a2e154553b7e8c244d08a606c919cfd0e35335d3a55416001e30ec005f1c27`，
Linux `2c7bfe3a2511a6013e653816b65fe69df60409740d895aec1cd0f4fee4ed1be1`。
日志 `.build/qa-store-{library-windows,library-linux,windows-final,linux-final}.log`。
八个变更源/锁/探针文件与 Linux staging 内容一致。便携 WebView2 写入
`mdo-home/data/cache/webview2`，符合用户允许首次窗口启动创建缓存的选择。
