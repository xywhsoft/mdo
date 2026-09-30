# 旧版 mdo 数据迁移

当前 mdo 只在用户显式操作后导入旧数据。无窗口的只读检测和迁移预览不会创建 `mdo-home`，也不会修改旧目录；Windows 原生窗口为便携 WebView2 数据首次启动即创建 Home，与旧数据导入无关。

## 入口与前置条件

在“设置 → 诊断与存储 → 旧版数据迁移”中可以检测两个固定来源：

1. 当前可执行文件旁的 `data/`；
2. 当前用户主目录中的 `.mdo/`。

目标是当前生效的 Home 路径。默认目标为 `mdo.exe` 旁的 `mdo-home/`；`--home` 与 `MDO_HOME` 仍按正常优先级覆盖。目标必须不存在，迁移不会合并或覆盖现有 Home。

当前前置条件与便携原生窗口有冲突：首次打开窗口已创建 `data/cache/webview2`，因此即使没有用户配置或会话，默认 Home 也不能导入。需要后续实现保留浏览器缓存的导入事务与启动恢复，不能删除正在使用的缓存或直接合并用户 Home。本阶段只完成迁移生命周期保护，未解决此 GUI 入口缺口。

界面先显示来源、目标、文件数、项目、会话、模型、计划、记忆、不支持项、冲突和绑定内容的 64 位十六进制预览令牌。用户展开第二步确认并再次点击“确认导入”后，服务端才开始写入。成功后必须重启 mdo，使所有运行时目录使用迁移后的配置代。

## API

只读检测：

```http
GET /api/v1/migrations/legacy
```

显式导入只接受两个字段：

```http
POST /api/v1/migrations/legacy
Content-Type: application/json

{
  "source_id": "user-home",
  "preview_token": "<GET 返回的 64 位令牌>"
}
```

`source_id` 只能是 `portable-data` 或 `user-home`。来源内容、目标状态或令牌发生变化，或关联项目正被独占时返回 `409 migration_conflict`；旧数据不能按当前 schema 转换时返回 `422 migration_invalid`。

## 转换规则

- 配置从内置当前默认值开始构造，只迁移旧版可表达的设置。自定义模型被拆成 provider 与 model，`responses`、`anthropic` 和 OpenAI chat completions 分别映射到当前三种协议。
- Ling 旧标识映射到受保护的 `ling-3.0-tiny`，不会生成可编辑副本。
- 旧 API key 写入权限为 `0600` 的独立 secret 文件，模型配置只保存 `file:` 引用。报告和 API 响应不含 secret 值。
- 项目 ID、会话 ID、计划 ID 和记忆 ID不满足当前便携标识规则时使用稳定哈希 ID；完整映射写入迁移报告。
- 会话的 xllm-session snapshot/journal 通过当前恢复器验证并 checkpoint 到当前格式，产品元数据再经当前 session schema 解析。旧 UI event 日志不进入新 timeline。
- 旧 `userPrompt` 不再作为隐藏系统指令激活，原文保存在 `migration/session-prompts/` 供人工核对。
- Markdown 记忆逐条转换并经当前敏感内容和大小限制校验。敏感、空、超限或超过单 store 256 条上限的条目被跳过并计入报告。
- `once` 与 `interval` 计划转换为当前 scheduler 定义；旧 cron 表达式无法无损映射，保留在旧目录并记为不支持。
- 旧 audit、计划运行历史、旧 UI event 和未知文件不复制。旧目录完整保留，报告会列出兼容性标志和跳过计数。
- 旧代理设置不自动启用。旧系统提示词和代理密码如存在，会分别保存到迁移目录和 secret 文件，供用户按当前配置合同重新确认。

## 原子性与恢复

服务端对来源执行锚定、无链接、有界扫描，并用文件路径、大小和内容计算预览令牌。导入写入目标同级的唯一临时目录；配置、会话、记忆和计划都必须通过当前解析器。发布前再次计算预览并确认目标仍不存在，然后以一次不覆盖 rename 发布目录。

Apply 要求项目生命周期服务已初始化。来源扫描后先只读生成完整项目 ID 映射，含哈希重映射及默认 tasks 桶；创建暂存目录前取得全部项目共享租约，任一项目冲突即释放已取得的租约且不写入。全部租约保持到成功发布或失败临时目录清理结束。检测与预览不取得租约，仍保持只读。该门是进程内保护，不替代整个 Home 的发布、跨进程与崩溃恢复协议。

任一步失败都会关闭锚定根并删除本次临时目录，既有 Home 与旧来源保持不变。成功后生成：

```text
mdo-home/migration/report.json
```

报告包括来源、目标、预览令牌、迁移时间、导入/跳过计数、兼容性标志以及模型和项目 ID 映射，不记录凭据正文。

如需回退，先退出 mdo，把新 Home 整体移到旁路备份位置，再用原版本和原旧目录启动。不要只替换 Home 中的部分文件；不同 manager 的 schema 与 generation 必须保持同一迁移批次。
