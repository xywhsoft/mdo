# 扩展能力

设置 → 扩展能力提供 Agent、SubAgent、工具、Skill、MCP、编辑器命令、外部工具、知识库和插件。九类使用相同的目录安装、ZIP 分发和依赖机制。

每个安装位于 `mdo-home/<kind>/<directory_id>/`，根部包含 `extension.json`，入口、资源、隐藏文件和空目录保留为真实文件。商店目录名是作品 XID，本机创作可用自己的目录名。逻辑 `id`（如 `skill:acme.review`）独立于目录、别名及显示名称。

| 类型 | 常见入口 | 用途 |
| --- | --- | --- |
| Agent / SubAgent | agent.md、agent.json 或 agent.c | 主任务和独立委派角色 |
| 工具 | tool.c | 按权限调用的 C 工具导出 |
| Skill | SKILL.md | 按需读取工作流程与参考资料 |
| MCP | mcp.json | 可分发服务模板，与私有连接分离 |
| 编辑器命令 | command.c | 编辑器命令及附属资源 |
| 外部工具 | tool.json | 明确导出的本机程序 |
| 知识库 | knowledge.json | 索引和文档提示 |
| 插件 | plugin.json | 前端/原生入口及应用文件覆盖 |

本机作者可以编辑整个目录、导入/导出普通 ZIP 和投稿。商店扩展通过同作品的审核版本更新，禁止直接编辑或再次分享。内置扩展的卸载选择会保留，重启不会自动复活。

所有类型支持稳定 ID、必需/可选依赖和冲突声明。详情显示依赖清单、原因、就绪状态和使用者；平台依赖项可以跳转到对应作品。安装、更新、启用、停用和卸载先检查整个受影响集合，确认后通过可恢复事务应用。知识库只能依赖知识库。具体清单、角色引用、MCP 绑定、旧数据迁移和 API 见[扩展身份与依赖](extension-dependencies.md)。

依赖不授予权限。项目选择、角色工具范围、父 Agent 限制、代码批准和账号授权继续生效。MCP 安装模板后仍需选择私有连接并完成连接和能力发现；安装 Skill 不自动执行其脚本。知识库索引按预算注入定位信息，正文仍通过文件工具按需读取。

- [目录商店与分发](ecosystem-store.md)
- [知识库](knowledge-libraries.md)
- [角色配置与程序接口](agent-authoring.md)
- [Skill 格式与使用](skill-format.md)
- [MCP 服务模板](mcp-service-templates.md)
- [目录改造历史设计](extension-archive-design.md)
- [部署和验收](ecosystem-deployment.md)

接口的当前身份/依赖约定以本文和依赖文档为准；类型指南中的历史单文件路径、旧包格式和别名引用不应作为新扩展的发布格式。
