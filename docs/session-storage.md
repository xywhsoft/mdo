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

`artifacts/` 是该会话的默认 artifact 根。artifact registry 与运行时事件仍由
xwork 管理，MDO-7B 会在本目录加入独立的 `ui-events.jsonl` replay 层；UI 事件
不会成为模型上下文的第二份真值。

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
回收站、还原、陈旧 revision 拒绝、失败创建回滚和损坏 meta 隔离。另有 49 项
静态合同、严格 GCC C11 unity 编译、完整单文件重建及隔离目录 5 秒零写启动。
测试全部有界，没有运行压力或高负载测试。

会话搜索、分叉、清空、截断、导出和 `ui-events.jsonl` 属于 MDO-7B，尚未由
本合同宣称完成。
