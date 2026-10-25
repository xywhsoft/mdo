# Agent 与 SubAgent

Agent/SubAgent 已接入独立资源商店，共用配置式和 C 开发能力。分发单元、自有文件、依赖、信任和兼容规则见 [角色独立生态](agent-ecosystem.md)。

# 知识库与其他扩展的独立分发

知识库已作为第六个落地体系接入，使用 `single_kind=knowledge`；目录、提示词注入、创作审核及 SDK 见 [知识库](knowledge-libraries.md)。


MCP 服务模板已接入同一链路，使用 `single_kind=mcp`。模板安装与设备连接分离，详见 [MCP 服务模板](mcp-service-templates.md)。下文原有三类源码资源的安装规则继续适用。

Skill、Agent 工具和编辑器命令设置页分别提供「本机」与「商店」选项卡。每个作品只含一个同类资源；不需要先创建组合包。Skill 可包含 SKILL.md、参考资料和脚本文件；安装不执行脚本。Agent 工具分发完整 C 源码，使用既有 Application SDK，在安装前显示源码及文件计划，用户确认信任后才编译注册。

编辑器命令分发独立 C 源文件，安装只校验元数据和写入文件，不编译或执行；调用 `/命令 参数` 时由独立 Worker 编译运行。参数以 `xvalue array` 传入，支持引号分词和 JSON 数组。安装前必须明确确认代码信任。

## 使用

- 在本机资源卡片点「分享到创作中心」，填写作品 ID、版本、说明、许可与已验证平台。项目 Skill 可从「来源、项目与 AI 创建」分享；项目命令可从「项目命令」分享，仅导出所选 `.mdo/commands/<id>.c`。
- 可私有保存，或附审核说明申请公布。源码与 Skill 附件一并上传，不会上传工作区其他内容。新版本仍须审核，期间旧公开版本继续可下载。
- 在对应商店搜索、查看源码与版本历史、下载安装。在本机列表检查更新、停用或卸载。商店安装的资源禁止编辑、复制、导出或再次分享，收据损坏时拒绝自定义写入。自行创建的资源保留编辑与分享入口。更新检查本地文件摘要；卸载保留本地修改过的文件。
- 「设置 → 账号与统计 → 创作中心 ↗」进入网站相应分类，管理版本、审核、下架与删除，以及申请创作者或扩容。配额继续按每版本计算：普通用户 3 件 / 10 MiB，创作者 20 件 / 100 MiB。

## 接口与兼容

本机 `/api/v1/ecosystem` 继续复用上传、下载、安装事务与回执，新增 `single_kind=skills|tools|commands`。它绑定查询和安装类型，投稿必须恰好一个匹配的资源，并由原生层传递 `kind` 与 `single_resource=true`；登录令牌不进入界面。项目导出引用可带 `project` ID，目录由项目管理器解析，通过 rooted Skill catalog 或带项目租约的 no-follow 命令文件读取。

线上 `/api/v1/mdo/ecosystem/packages?kind=skills|tools|commands&single=1` 只返回单资源作品。详情、最新版本、历史也支持 `single=1&single_kind=...`。元数据增加 `resource_count`，首次迁移从 hash 校验的不可变源文件回填。旧组合包保留原记录与对象，不混入新商店。新投稿以 Skill、Agent 工具或编辑器命令作为分类时必须只有一个对应资源。

网站 `/mdo/creator?kind=skills|tools|commands` 与 `/mdo/ecosystem?kind=skills|tools|commands` 可直接进入分类。审核复用已有九类后台的 Skill、Agent 工具、编辑器命令入口。

## 回归

`tests/test_single_resource_store_runtime.py` 使用两个隔离真实 TCC 服务验证：私有访问控制、审核后可见、类型隔离、源码/附件下载、计划确认、C 信任、Skill 调用、工具注册、命令 Worker 调用及参数类型保持、新版本审核、更新冲突、卸载、项目导出、旧组合包隔离和元数据回填。`--ui` 保留临时服务供浏览器验收，按 Ctrl+C 结束；不连接真实模型或写入生产账号。
