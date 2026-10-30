# 扩展身份与依赖

九类扩展共用外层 `extension.json`、稳定逻辑 ID、依赖图和操作计划。知识库只能依赖知识库；其他八类可声明跨类型关系。依赖声明不扩大工具权限，不包含账号或凭据，也不自动启用可选能力。

## 身份

| kind | ID 前缀 |
| --- | --- |
| agents | agent |
| subagents | subagent |
| tools | tool |
| skills | skill |
| mcp | mcp |
| external-tools | external-tool |
| commands | command |
| plugins | plugin |
| knowledge | knowledge |

`id` 如 `skill:acme.review`，是依赖、角色及项目选择使用的身份，创建登记后不可修改。名称最多 128 字节，完整 ID 最多 160 字节，使用小写 ASCII 字母、数字、点、短横线和下划线；禁止首尾点、连续点和路径分隔符。建议公开作品带发布者命名空间。

`directory_id` 是 Home 下的实际目录名；商店安装仍以作品 `xid` 为目录名。`alias` 是调用名称，`name` 是显示名称。运行时仍使用解析后的具体工具/角色名称。不要将三者互相替代。

平台登记逻辑 ID 的作者、类型和作品归属，删除作品后仍保留身份占用。手工同名目录不能取得商店身份。重复目录保留已经登记的提供者，其他副本标为冲突；没有可信登记时全部拒绝运行。当前不提供隐式本机覆盖商店绑定。

## 清单

```json
{
  "kind": "subagents",
  "id": "subagent:acme.reviewer",
  "name": "代码审查助手",
  "alias": "reviewer",
  "entry": "agent.md",
  "platforms": ["any"],
  "dependencies": [
    {"id": "tool:acme.git", "reason": "读取提交差异"},
    {"id": "skill:acme.review", "reason": "使用审查流程"},
    {"id": "mcp:acme.repository", "reason": "查询仓库记录"},
    {"id": "knowledge:acme.xrt", "optional": true, "reason": "涉及 XRT 时参考接口"}
  ],
  "conflicts": [{"id": "subagent:acme.legacy-reviewer", "reason": "调用方式互斥"}]
}
```

`dependencies` 元素只接受 `id`、布尔 `optional` 和不超过 600 UTF-8 字节的 `reason`；`optional` 默认 false。`conflicts` 接受 `id` 和 `reason`，任意一方声明都生效。每个数组最多 128 项，拒绝重复、自依赖、未知类型和知识库跨类型关系。首版不接受版本范围。

必需依赖传递解析、共享去重、禁止循环；可选引用不进入安装闭包，不决定加载顺序，目标缺失或未就绪只产生提示。需要可选能力时仍须通过目标自身的必需依赖和权限检查。文档中的相关资料链接不必都声明为必需依赖。

## 详情与操作

每类本机详情显示依赖名称、ID、必需/可选、原因、安装状态、阻塞链及直接使用者；插件在“依赖与冲突”页显示。平台详情的公开依赖和使用者可以点击跳转，地址为 `/mdo/ecosystem?extension=<逻辑ID>`。不可公开访问的目标不返回私有作品信息。

“已安装且依赖满足”只表示安装图有效。MCP 的私有连接、登录、能力发现，知识库索引，以及当前项目和父 Agent 的限制仍须在运行前满足。

- 安装：平台生成当前平台可满足的完整版本集合，本机固定作品、版本、清单和归档摘要后展示计划。确认后下载全部必需归档、校验并暂存，再统一提交。安装阶段不运行归档安装脚本。
- 更新：计划同时列出受影响的传递使用者，先停用并等待活动租约释放，再切换目录，恢复原来的启用意图。失败恢复整组文件和状态。代码仍被进程持有时返回忙，不强行替换。
- 启用：包含必需依赖闭包。项目、角色或父 Agent 禁止的能力不会被重新开放。
- 停用/卸载：没有级联确认时拒绝破坏已启用的使用者；界面先列出级联计划。卸载只删除根扩展，保留使用者但停用它们，不顺带删除共享依赖。
- 本机作者修改：已被启用扩展使用的提供者先通过级联计划停用，再编辑；保存后按需要重新启用使用者。商店更新使用整组事务自动恢复原选择。

只有安装或启用程序类内容的计划要求勾选代码信任。代码批准不授予文件、进程或网络权限。实际导出冲突或运行配置问题会报告运行错误，保留已提交文件和启用意图，执行入口继续检查可用性。

计划保留 24 小时，最多 128 份有效计划，取消未执行的计划会释放记录。已完成计划保留同一修订的回执，连接中断后可重试同一计划；不会重复安装。过期、状态变化或目录指纹变化后需要重新检查。单次操作最多 128 项，全部下载合计不超过 1 GiB；单 ZIP 最多 512 MiB。

事务位于 `data/extension-groups/`，计划/回执位于 `data/extension-plans/`。启动先恢复事务，再发布运行目录。无法证明文件/状态归属时保留日志、备份和目录并阻止写入，诊断页显示恢复原因。

## 角色、MCP 与外部程序

角色的 `extends` 可用 `agent:acme.base` / `subagent:acme.base`，或宿主基类 `mdo.general`。`tools`、`skills`、`mcp_servers` 可选择结构引用：

```json
{
  "tools": ["read", {"ref": "tool:acme.git", "export": "git_diff"}],
  "skills": [{"ref": "skill:acme.review"}],
  "mcp_servers": [{"ref": "mcp:acme.repository"}]
}
```

扩展引用必须声明在必需依赖闭包中。多导出的工具必须指定 `export`；具体导出归属也会核验。宿主固定工具仍使用 `read` 等名称。精确委派可用 `subagent:acme.reviewer`。继承、子任务准备、每次模型派发、工具回调、Skill 读取及编辑器命令入口都再次检查依赖和作用域。

MCP ID 标识可分发模板。对应详情页可为使用者选择已有私有连接，支持全局和项目覆盖，不猜测第一个账号。继承或间接选择沿用声明方的唯一已有绑定，声明方之间选择不一致时要求角色明确绑定。只有一个候选时可按当前作用域解析；多个候选必须明确选择。保存绑定带修订校验，数据在 `data/extensions/bindings.json`，不进入 ZIP。凭据继续使用私有连接/密钥库。连接启用、登录及工具发现完成后才可满足运行依赖。

MCP `transport.program` 可写为 `{"ref":"external-tool:acme.node","export":"node"}`，该提供者必须处于必需闭包中。宿主解析已登记的具体可执行入口，参数独立传递，进程期间保留整个闭包租约；不通过 PATH 猜测提供者。入口不能带预设参数。批准摘要涵盖必需闭包，提供者内容变化会使旧批准失效。Android 原生程序仍通过完整版 APK 安装更新。

## 知识库

依赖只写在外层清单，`knowledge.json` 保持 `mdo.knowledge.v1`。默认索引是 `index.md`，`README.md` 继续承担项目说明。索引按小节提供用途说明和文档相对路径，见[知识库文档](knowledge-libraries.md)。

提示注入按依赖闭包去重：先为完整闭包保留 ID、名称、挂载路径和索引，再在预算内补充提示。父库不会在缺少必需库条目时单独注入。索引缺失、不可读、注入被关闭、项目停用或预算无法容纳完整必需集合时，受影响角色或工具会在执行前被阻止。

## 本机 API

写入沿用现有写令牌和目标设备校验。示例请求均为 `POST /api/v1/ecosystem`：

```json
{"action":"install_plan","version_id":"<平台版本XID>"}
```

```json
{"action":"dependency_plan","operation":"disable","ref":"knowledge:acme.xrt","cascade":true}
```

```json
{"action":"apply_plan","plan_id":"<本机计划ID>","revision":"<计划修订>","trust_code":true}
```

未执行的计划可用 `discard_plan` 加同一 `plan_id`、`revision` 取消。客户端不能提交自造下载地址、版本集合或来源归属来替代计划。

`GET /api/v1/extensions/<kind>/<id或directory_id>` 返回逻辑身份、物理目录、`dependency_details`、`dependents`、`dependency_bindings` 和 `bindings_revision`。绑定操作向同一路径的 `/actions` 提交 `action:bind`、`dependency_id`、`connection_id`、`scope`、`expected_binding_revision`。

平台新增 `/api/v1/mdo/extensions/resolve?ref=...` 和 `/plan?version=...&platform=...`。带依赖或冲突的归档下载要求 `dependency_protocol=1`，防止旧客户端跳过依赖安装；普通无依赖归档保持原有下载流程。

## 旧数据迁移

缺少有效 ID 的目录显示需要处理，宿主不会从目录名或别名推测永久身份。

- 内置扩展：本机列表出现“迁移内置扩展身份”。只替换来源为 builtin、仍与原批准摘要一致的目录，保留停用和已卸载选择；编辑过的目录先保留并人工处理。
- 商店扩展：作者为同一作品提交含稳定 ID 的新 ZIP 并重新审核。客户端仅在作品归属和原批准内容都一致时绑定首个逻辑 ID，不在不可变旧归档上补写字段。
- 本机作者：关闭使用该 Home 的 mdo，通过显式映射迁移清单、旧角色引用、插件关系、项目选择和私有 MCP 模板引用。脚本默认只读预览，凭据值不进入报告。

示例 `mapping.json`：

```json
{
  "ids": {"skills/reviewer":"skill:acme.review", "mcp/repo":"mcp:acme.repository"},
  "dependencies": {"skills/reviewer":[{"id":"mcp:acme.repository","reason":"查询仓库记录"}]}
}
```

```powershell
python tools/migrate_extension_dependencies.py --home D:/mdo-home --map D:/mapping.json --workspace D:/work/project
python tools/migrate_extension_dependencies.py --home D:/mdo-home --map D:/mapping.json --workspace D:/work/project --apply --expected-plan <预览的plan_sha256>
```

活动 Home 锁冲突时拒绝写入；输入变化需要重新预览。备份和日志保留在 `data/extension-migrations/<事务ID>/`。中断后关闭 mdo，使用相同 Home 及显式工作区根运行 `--recover`。恢复会校验原/新文件摘要，外部修改或无法证明归属时保留文件并报错。无法自动确定的 C 代码引用和旧插件版本条件需人工核查，不静默删除约束。

## 验证

原生测试通过 XS/TCC 执行当前完整产品 C，平台测试使用真实 SQLite 和 ZIP；模型服务均为测试替身或未调用。核心脚本：

```text
python tests/test_extension_dependencies_native.py --url <隔离服务URL> --home <测试Home>
python tests/test_extension_dependency_group_native.py --url <隔离服务URL> --home <测试Home>
python tests/test_extension_dependency_service_native.py --url <隔离服务URL> --home <测试Home>
python tests/test_extension_dependency_runtime_native.py --url <隔离服务URL> --home <测试Home>
python tests/test_extension_dependency_store_native.py --url <隔离服务URL> --home <测试Home>
python tests/test_mcp_dependency_native.py --url <隔离服务URL> --home <测试Home>
python -m unittest discover -s tests -p test_extension_dependency_migration.py
node --test tests/test_extension_dependencies_ui.mjs tests/test_platform_directory_web.mjs
```

跨库部署顺序和实际验收范围见[部署说明](ecosystem-deployment.md)。
