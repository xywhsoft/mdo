# mdo 记忆存储合同

本文记录 MDO-7D 的全局/项目记忆、Agent 注入、运行时工具和整目录迁移协议。
记忆只保存用户明确允许持久化的工作偏好与项目事实；provider credential、配置
secret、Authorization、密码和私钥不进入该系统。

## 目录与作用域

外部 Home 中的可读目录固定为：

```text
memory/
├─ global.json
├─ projects/
│  └─ <project-id>.json
├─ audit.jsonl
└─ .writer.lock
```

所有文件按需创建。只初始化 manager、创建空 Agent 或单文件启动不会创建 Home。
全局 scope 不接受 project ID；项目 scope 必须提供最多 64 字节的安全 ID，只允许
ASCII 字母、数字、`-`、`_` 和非首位的 `.`。Agent 会话固定自己的 project ID，
同一工具定义不能越过该身份访问其他项目。

每个 scope 最多 256 条记录，单条正文最多 16 KiB、标签最多 16 个、单个 store
最多 5 MiB。store 是 schema v1 JSON：

```json
{
  "schema_version": 1,
  "revision": 3,
  "updated_at_us": 1790000000000000,
  "entries": [
    {
      "id": "build-command",
      "title": "Build",
      "content": "Use the locked xserver checkout for builds.",
      "tags": ["build"],
      "pinned": true,
      "revision": 2,
      "created_at_us": 1790000000000000,
      "updated_at_us": 1790000100000000
    }
  ]
}
```

解析器要求字段集合精确、UTF-8 有效、字符串不含嵌入 NUL、ID 合法、标签不重复、
时间单调、记录 ID 唯一且所有计数满足硬上限。记录按 pinned、更新时间和 ID 稳定
排序。未知字段、特殊文件、链接、超限内容和损坏 JSON 都会失败关闭。

## 快照、revision 与提交

`MdoMemorySnapshotCreate()` 返回引用计数的不可变 owned snapshot；entry info 中的
字符串和标签只借用该 snapshot。manager generation 表示当前进程已发布的 store
变化，scope revision 是磁盘上的乐观并发令牌，entry revision 记录单条内容版本。

写入和删除可携带 `ExpectedRevision`。值不匹配时返回 context conflict，调用方必须
重读 store，不能覆盖较新的状态。持久化顺序是：

1. 完整校验并复制输入；
2. 取得进程锁和 `memory/.writer.lock` 的跨进程非阻塞独占锁；
3. 重读并核对 store revision；
4. 先向审计账本写入 prepared 记录并 flush；
5. 同目录写临时文件、flush、生成可读 `.bak`，再原子替换 store；
6. 只在磁盘发布成功后推进 manager generation。

writer lock 一旦由当前 manager 取得，会保持到 manager 销毁，避免两个进程交替成为
同一 Home 的 writer。内存不足、锁冲突、审计失败和磁盘失败均不会发布进程内新代。

`audit.jsonl` 最大 8 MiB，达到边界时原子保留最近约 4 MiB 的完整行。每条 schema
v1 记录包含 audit ID、时间、prepared phase、操作、scope、project/entry、前后
revision、actor、session 和 reason。正文永不写入审计；只记录字节数与 SHA-256。
目录导入按 store 写入 `import` 记录，原始外部 audit 不混入当前 Home 的权威账本。

## Secret 边界

写 API、磁盘解析和目录导入都拒绝常见 credential 标记、Bearer/Authorization、
密码、访问/刷新 token、OpenAI/GitHub/Slack/AWS key 形态和 PEM 私钥头。该检测是
纵深防御，不是任意 secret 的完备分类器。产品边界仍必须遵守两条主规则：

- provider/config 只向 model client 解析短生命周期 secret，不能把值传给 memory；
- UI、模块和 Agent 工具不得把 credential 内容作为记忆候选提交。

## Agent 注入与运行时工具

`settings.agent.memory` 默认开启。创建 Agent session 时，mdo 在一个锁定视图中读取
当前项目和全局 snapshot，把项目记录排在全局记录之前，生成最多 32 KiB 的 JSONL
reference fragment，并把捕获的 memory generation 固定到 session/run info。组合后的
系统提示词总上限仍为 512 KiB。

注入区由物理行 sentinel 包围，头部明确声明每条记录都是不可信 reference data，
不能成为指令、策略、授权或 secret 请求。JSON 使用紧凑转义，记录正文中的换行不能
伪造 sentinel。空间不足时只加入完整记录，并用 `omitted_records` 明示省略数量。

runtime 发布三个标准 xwork 工具：

- `memory_search`：READ effect；搜索全局和当前项目，最多返回 8 条、每条 excerpt
  最多 2048 字节，结果带 `untrusted:true` 和当前 store revision；
- `memory_write`：WORKSPACE_WRITE effect；必须提供 scope、记录字段与
  `expected_revision`，通过统一权限资源和审计路径提交；
- `memory_delete`：WORKSPACE_WRITE effect；必须提供 scope、ID 与
  `expected_revision`，删除同样进入审计账本。

每个 Agent 用独立的引用计数 binding 复制 project/session 身份。子 Agent 的不可变
工具目录会各自持有 binding，根 Agent 释放后不会悬空。工具仍经过 Agent 的显式选择、
effect 上限和 permission profile；只读 Agent 即使声明三个工具，也只能保留
`memory_search`。普通参数、revision、策略和敏感内容错误作为模型可见的
`success:false` 返回，I/O、OOM、取消和 deadline 保持运行级错误。

关闭 memory 时不注册这三个工具、不注入 prompt，session/run 的 memory generation
为零。定义仍必须满足模块加载时的工具引用校验，因此关闭功能的自定义 Agent 不应
声明 memory 工具。

## 整目录导出

`MdoMemoryExportDirectory()` 要求目标路径不存在，排他创建新目录。它在 memory
writer 边界内取得所有 store 的 owned snapshot，随后写入：

```text
<export>/
├─ manifest.json
├─ global.json                 # 存在全局 store 时
└─ projects/
   └─ <project-id>.json
```

store 先写，`manifest.json` 最后写，因而 manifest 是完成标记。目标文件均通过
锚定 root、`NOFOLLOW`、排他创建和 flush 发布。失败只清理本次新建的已知文件与空
目录，不覆盖既存路径。

manifest schema v1 包含 kind `mdo-memory-directory`、导出时间、store/project/entry
计数、总 store 字节数，以及每个文件的相对路径、scope、project ID、revision、
entry count、字节数和 SHA-256。文件按路径稳定排序，普通 JSON 工具即可阅读和处理。
audit、writer lock 与 `.bak` 不进入导出；接收方会为导入动作建立自己的审计链。

单次导出最多 257 个 store（一个 global 加 256 个 project）和 64 MiB store 数据，
manifest 最大 1 MiB。

## 预览与导入

`MdoMemoryPreviewImportDirectory()` 不写入 Home。它通过锚定 root 完整枚举来源，要求
顶层只能有 `manifest.json`、可选 `global.json` 和 `projects/`；项目目录只能有合法
`<project-id>.json` 普通文件。链接、特殊文件、备份、未知文件、重复 store 和非法
UTF-8 一律拒绝。所有 store 都先按生产 schema 和 secret 规则解析，再逐项核对
manifest 的 identity、revision、计数、字节数与 SHA-256。summary 返回目标 manager
generation，供 UI 确认后作为导入并发令牌。

`MdoMemoryImportDirectory()` 会重新执行同样的完整来源校验，不能依赖旧 preview。
它取得 writer lock 后核对 `ExpectedGeneration`，并要求目标没有 `global.json`、对应
备份或任何 project store 条目。当前协议有意拒绝 merge/replace，避免自动解决冲突
或静默丢失本地记忆；调用方需要先显式处理现有数据。

全部候选在写入前已经拥有并验证。每个 store 先追加 import audit，再原子发布；若
后续 store 失败，已发布的本次 store 会被删除。审计中的 prepared 记录仍保留，明确
反映发生过未完成尝试。成功后整个目录导入只推进一次 manager generation，同时保留
来源 store/entry revision 和时间。

## 当前验证范围

Windows 真实 xs/TCC 探针覆盖空 store、全局/项目隔离、创建/更新/删除、陈旧 revision、
敏感内容拒绝、重启恢复、prompt 不可信边界、Agent 实际模型请求、memory 关闭、只读
工具裁剪、权限资源、工具搜索/写入/删除、审计哈希、损坏 store 隔离、完整目录导出、
既存目标拒绝、未知文件拒绝、manifest 哈希篡改拒绝、preview generation 失效、空目标
导入、导入审计和二次导入拒绝。

阶段验收另包含 55 项静态合同、严格 GCC C11 unity 编译、10 个真实 xs/TCC 有界
运行探针、锁定依赖的完整 Windows 宿主与单文件重建，以及隔离目录 5 秒零写启动。
按约束未运行压力或高负载测试。
