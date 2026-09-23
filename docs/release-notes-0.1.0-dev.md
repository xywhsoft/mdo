# mdo 0.1.0-dev 发布说明

`0.1.0-dev` 是 mdo 按生产级 C 栈重新实现后的首个开发发布基线。它保留原有产品布局和便携目标，同时重做底层 VFS、模型与会话库、Agent runtime、模块系统、服务端 API 和浏览器交互。

## 产品能力

- 单个 `mdo.exe` 可直接运行；配置、会话、记忆、Skill、模块和缓存统一进入可执行文件旁的 `mdo-home/`，空目录首次启动保持零写入；
- 内置不可编辑、不可删除的 Ling 3.0 Tiny，支持 Chat Completions、Responses 与 Anthropic Messages 方言；provider 与 model profile 分离，可配置连接、能力、上下文、输出上限和推理等级；
- Agent 会话、流式 timeline、工具审批、故障恢复、任务、产物、审计、checkpoint、分叉、归档和回收站使用服务端权威状态；
- `agents/`、`subagents/` 与 `tools/` 可放置 C 模块，使用版本化 `mdo/module.h`、受限 TCC 和事务式 generation 热重载；
- Skill 使用摘要优先、正文与资源按需加载的渐进披露，外部目录可完整覆盖内置 Skill；
- 内建有界 `web_search`、`web_open`、`web_find`，并在实际解析和拨号路径限制公网访问；
- MCP 支持 stdio 与 HTTPS Streamable HTTP，配置、secret、schema cache、reload 和懒加载工具均有明确所有权边界；
- 会话、记忆与计划任务持久化；计划任务复用普通 Agent run、权限、工具和审计链；
- `/api/v1`、无 Node.js 前端工具链和响应式界面覆盖桌面与 390×844 移动视口；
- 旧版 `data/` 与 `.mdo/` 通过显式预览、确认令牌和原子发布迁移，失败保留来源并清理本次 staging。

## 基础设施与工程变化

- xrt 提供版本化 VFS namespace/provider、memory、disk、pack 和 overlay 机制；普通文件 API 可透明接入；
- xs 使用独立 Application/SDK VFS，每个 TCC state 固定双 VFS 与 generation；打包资源、磁盘覆盖、reload 和嵌套编译共享同一生命周期模型；
- xllm 3.1、xllm-session 3.0 与 xwork 3.0 完成所有权、取消、持久化、批调度、子 Agent、任务、计划、MCP 与可观测性重构；
- `deps.lock` 锁定跨仓库提交、生产源码摘要、ABI/schema/pack 格式；`tools/build_mdo.py` 从锁定源码生成 unity、构建宿主并打包；
- `tools/qa_release.py` 提供 Windows/Linux 统一低负载发布门禁，包括 113 项合同检查、严格 C11、16 个真实 TCC 运行时探针、确定性 pack 和 Windows 单文件启动回归。

## 迁移兼容

旧数据只通过设置页或 API 的显式迁移流程导入。预览令牌绑定来源、目标和全部文件内容；执行前与发布前都会重新扫描。目标 Home 必须不存在，导入不做隐式 merge，不修改旧来源。成功后返回 `restart_required`，由新进程加载全部 manager。完整规则见[旧数据迁移说明](legacy-migration.md)。

## 已知限制

- 当前版本仍为 `0.1.0-dev`，尚未提供签名安装包和自动更新；
- 浏览器界面已验证桌面与 390×844 视口，仓库中没有 Android/iOS 原生宿主，其系统集成仍需对应平台 runner 验收；
- 不支持原子且不覆盖目录 rename 的文件系统无法完成旧数据最终发布；mdo 会安全失败并保留来源。WSL DrvFS 属于已知示例，Linux 原生文件系统已通过；
- Ling 三协议的离线 adapter/golden 已通过；2026-09-23 后续修复恢复了免配置的内置服务接入，隔离 mdo 实例已分别通过三种协议的真实对话；
- 按项目要求，本阶段没有运行压力、高负载或长时间 soak 测试；发布门禁使用有界功能、故障恢复、严格编译、确定性 pack 和 20 秒打包启动回归。
