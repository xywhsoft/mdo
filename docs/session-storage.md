# mdo 会话存储合同

本文记录 MDO-7A 的持久会话基础。mdo 负责产品身份、生命周期和目录索引；
xllm-session 继续拥有模型上下文账本、snapshot、journal 与恢复语义。两层不得
各自保存一份消息历史。

## 目录与权威数据

每个会话位于外部 Home 的固定目录：

```text
sessions/<project-id>/<session-id>/
├─ meta.json
├─ snapshot.json
├─ journal.jsonl
├─ ui-events.jsonl
├─ .runtime.lock
└─ artifacts/
```

`project-id` 和 `session-id` 只能使用有界的 ASCII 字母、数字、`-`、`_` 和
非首位的 `.`。新 session ID 由 xrt XID 生成，末级目录通过 Home 的锚定 root
独占创建。创建 Agent 失败时会删除已知的 journal、snapshot、meta、空 artifact
目录和空 session 目录，不向 catalog 留下半创建条目。

`snapshot.json` 与 `journal.jsonl` 是 xllm-session 的权威账本。新会话先附加
journal，再由 xwork 在稳定边界 checkpoint；重开会话必须使用
`xllmSessionRecover()` 同时处理二者。恢复出的 context/input/output 上限不得
超过当前所选模型和 Agent profile，否则恢复失败。

`artifacts/` 是该会话的默认 artifact 根。artifact registry 与事件源仍由
xwork 管理；mdo 把该会话收到的 xwork 事件写入独立的 `ui-events.jsonl` replay
层。UI 事件不会成为模型上下文的第二份真值。

`.runtime.lock` 是保留的空锁文件。活动 Agent 在其上持有跨进程非阻塞独占锁，
进程内还登记一份可观察的 active lease。lease 由 Agent callback owner 保活，
所以调用方释放 `MdoSession` 后，只要 run 仍持有 Agent，就不能从另一个句柄或
进程再次打开同一个 journal writer。锁文件本身会保留，以避免解锁后删除再创建
造成 inode 竞态。

## UI event replay

每条事件是单行 schema v1 JSON，包含 session-local `event_id`、xwork 原始事件
ID、Unix 微秒时间、project/session、kind、Agent/run/task/artifact 血缘、tool
元数据、effect、终态和有界文本。文本最大 64 KiB，单条记录最大 96 KiB。
事件先完成持久化，再调用产品注入的 event callback；写盘失败会让 callback
返回 false，请求 xwork 协作取消当前运行，不能在事件审计失败后静默继续。

事件文件上限为 16 MiB。到达上限时在同目录原子保留最近约 8 MiB，再追加新
事件；保留的 event ID 不重新编号，replay 因而能通过首个 ID 与 cursor 明确
报告 `history_lost`。启动时不完整尾行会先以换行封口；损坏的完整记录在 replay
中形成 history gap，但不阻止 xllm-session 恢复或后续事件继续使用更大的 ID。

`MdoSessionEventReplay()` 返回引用计数 owned snapshot，字符串由 snapshot
持有。调用方提供 `after_event_id` 与最多 1000 条的 limit；返回 next cursor、
当前文件 latest ID 和 history-lost 标志。这样 Web API 可以按 cursor 重连，
无需让前端读取文件或依赖进程内数组。

## meta.json schema v1

`meta.json` 是严格的 UTF-8 JSON 对象，最大 64 KiB，必须恰好包含以下字段：

- schema 与并发：`schema_version`、`revision`；
- 身份：`id`、`project_id`、`title`；
- 固定运行选择：`agent_id`、`model_id`、`protocol`、`reasoning_effort`、
  `max_output_tokens`、`workspace_root`；
- 时间与状态：`created_at_us`、`updated_at_us`、`status`、
  `previous_status`、`pinned`；
- 追溯信息：`config_revision`、`model_generation`、`module_generation`、
  `skill_generation`。

文件不保存 endpoint、credential reference、API key、回调地址或任意 secret。
workspace 在创建时转成绝对路径。模型 completion、审批、权限、hook 和 event
回调属于当前进程；调用方重开会话时通过 `MdoSessionRuntimeOptions` 重新注入。

初次发布和每次修改都使用同目录临时文件、flush、可读 `.bak` 与原子替换。
进程内 generation 表示 catalog 可观察变化；单会话 `revision` 是磁盘上的并发
令牌。修改前会重新读取 meta 并核对 revision。若另一个句柄已经提交新版本，
当前操作返回 context conflict，调用方必须重新加载，不能覆盖较新的字段。

## 生命周期

状态只有三种：

```text
active <-> archived
   |           |
   +-> trash <-+
         |
         +-> previous status
```

- `active` 可以创建 Agent 并恢复模型账本；
- `archived` 保留全部数据，但 `MdoSessionOpen()` 拒绝创建 Agent；
- `trash` 是可逆逻辑删除，不移动或永久删除目录；进入时清除 pinned，并记录
  原先的 active/archive 状态；
- restore 回到进入 trash 前的状态。

`MdoSessionLoad()` 只加载元数据，适合归档与回收站管理，其 Agent 引用为空。
catalog 与 info 的 `RuntimeOpen` 是瞬时字段，不进入 meta。归档和移入回收站会
拒绝仍有活动 runtime 的会话；重命名和置顶仍可在 revision 规则下执行。
已有活动 Agent 句柄的生命周期由引用计数保证；状态修改控制后续打开，不会在
任意线程中强制销毁调用方仍持有的 run。

## Catalog 与故障边界

catalog 是引用计数的不可变 owned snapshot。它扫描 `sessions/` 下的项目与会话
目录，并按状态、置顶、更新时间和 ID 稳定排序。调用方得到的 `MdoSessionInfo`
是定长值拷贝，不借用 manager 内部字符串。

单个丢失、损坏或不符合 schema 的 `meta.json` 会生成带路径的结构化诊断，
不会隐藏其他有效会话。无法读取顶层目录、目录迭代失败或内存不足会使整个
snapshot 失败，因为此时无法保证列表完整。

单文件首次启动只初始化 manager，不创建 `mdo-home`。第一次真正创建会话时
才建立外部 Home 与目录。所有目录枚举、元数据读写和清理都经过 Home 的锚定
文件系统接口。

## 当前验证范围

Windows 真实 xs/TCC 探针覆盖空 catalog、新建、两轮模型调用、checkpoint、
进程内释放后恢复、历史 prompt/answer 可见、重命名、置顶、归档阻止打开、
回收站、还原、陈旧 revision 拒绝、失败创建回滚、损坏 meta 隔离、重复 runtime
拒绝、事件 cursor、重开续号和损坏尾部隔离。另有 50 项
静态合同、严格 GCC C11 unity 编译、完整单文件重建及隔离目录 5 秒零写启动。
测试全部有界，没有运行压力或高负载测试。

会话搜索、分叉、清空、截断和导出属于 MDO-7C，尚未由本合同宣称完成。
