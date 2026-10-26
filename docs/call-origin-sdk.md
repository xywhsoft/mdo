# Application SDK 调用来源

`mdo_sdk_context_v1.Origin` 由宿主入口赋值，`mdoCallOrigin(context)` 提供按 Size 兼容的读取。返回用户、Agent、扩展初始化或未知来源；来源不是权限授予，也不将可信的原生 C 插件变成沙箱。

HTTP 用户入口为 user，模型工具、C Agent 的 Run/OnEvent 和全局 Agent 生命周期为 agent，模块注册为 extension。根 C Agent 即使没有 RunId 或 MemberId，也具有 agent 来源。后台 operation 复制并纳入准入身份，不能因丢失 RunId 而成为 user。JSON 中自行提供 origin 不会覆盖宿主来源。编辑器命令沿用实际发起入口。

`sdk.context` 返回可读的 origin。`instance.send` 将用户输入标为 user，其余来源标为 agent；插件的恢复、交接等需人类决定的操作应明确要求 `MDO_ORIGIN_USER`，不能仅以 RunId 是否为空判断。旧上下文缺少此字段时，存在 RunId/MemberId 可判断为 agent；其余为 unknown，不推断人类授权。

验收使用 Mohists 唯一开发实例中的真实根 C 角色：无 RunId/MemberId、后台传递来源、被动消息标记、用户专属恢复拒绝、一次本地模型回复。未创建隔离环境或调用供应商模型。
