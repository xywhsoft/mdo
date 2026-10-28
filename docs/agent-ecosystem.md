# Agent 与 SubAgent

每个角色使用 `mdo-home/agents/<id>/` 或 `subagents/<id>/`，根清单 extension.json 指定 kind、alias、name、entry、platforms。entry 可为 agent.md/agent.json，使用 mdo.agent.v1 配置；C 实现与头文件、提示词等依赖在同一目录。配置式角色可引用目录内 C 程序，独立 C 入口使用公共 mdo/agent.h。

本机、商店、创作与审核统一使用 [整目录 ZIP](ecosystem-store.md)。安装目录的 XID 不改变公开调用别名；商店内容只读，项目仅保存选择。插件可通过公共 AddAgent 注册角色，并同时提供工具、服务和界面；普通 Agent 工具目录不能注册角色。

配置可控制系统提示词、各来源注入、工具/Skill 白名单、模型、只读/委派、预算和运行上限。主角色与子角色沿用同一配置和 SDK，子角色继承会话的权限上限和外部工具拥有者。分发检查不执行 C，启用 C 时需要用户确认信任。
