# 0.7.0 本地角色开发改造与验证

目标：完成 Agent/SubAgent 对等的本地配置式与 C 开发支持。本轮不涉及商店、创作中心或线上发布。

## 交付

- 统一 `mdo.agent.v1`：Markdown/JSON、继承、两种运行角色和通用角色、明确的工具继承/空集合、模型及运行额度。
- 上下文来源独立控制：系统正文、Skill、全局指令、语言、环境、外部工具、知识库、记忆、文件访问、项目规则、MCP 缓存说明、自定义来源。
- 配置式来源排序、筛选、模板、system/user 位置、字节裁剪、每轮或每请求刷新；历史/图片/工具结果投影不改写原始账本。
- C 上下文、请求、响应、工具和委派回调；实例状态、完整 Application SDK、自定义驱动及复用托管循环；代码 generation 在热重载后保持有效。
- 子角色独立模型预算、工具范围和 MCP 选择，使用父上下文不可变快照；共享用户授权、取消、deadline、深度及执行资源。
- 本地编辑器高级配置、JSON 导入、C 模板下载和共享弹窗预览。预览不运行用户代码/模型，不创建记忆或角色文件。
- 发送前检查额外消息及 schema，最多尝试一次安全压缩；工具回调错误保留具体原因，改写的工具结果在后续委派回调中保持有效。

开发指南：`docs/agent-authoring.md`；示例：`examples/agents/`。

## 源码与运行时

代码提交依次为：`91f3e0c8`、`02d17b25`、`54459747`、`cafd849b`、`096712a0`，全部位于 master。

xs 基线仍为依赖锁固定的 `216f5445163ca30c815ad7d5d2da3b71cb2f6401`，通过受控 SDK overlay 增加 xwork ABI 9。overlay SHA256 为 `16f28b0bd9ecf791b505bbff9001a0c18bbaf398e29d906dc01df967818bcb84`。生成工具为 `tools/native/update_sdk_overlay.py`；未修改其他仓库的源码。

## 验证结果

以下均在独立 Home 与离线/loopback 模型下通过，没有向真实供应商发送模型请求。

| 验证 | 结果 |
| --- | --- |
| `test_agent_authoring_runtime.py` | 29 个配置、继承及非法参数边界检查通过 |
| `test_agent_program_runtime.py` | C 主/子角色、独立模型额度、只读协调员授权子角色向工作区外写入、回调拒绝后继续、错误原因、结果改写、热重载固定、私有状态及示例模块通过 |
| `test_agent_context_runtime.py` | 来源刷新、禁用文件、知识库 ID 筛选、历史/工具投影、普通调用额度及恢复、超大来源发送前拦截、用户/角色只读限制通过 |
| `test_agent_runtime.py` | 原有 Agent 选择、回调 owner、异步运行、checkpoint、恢复与生命周期通过 |
| `test_agent_media_runtime.py` | 图片投影、模型切换、原始账本保留与恢复通过 |
| `test_module_runtime.py`、`test_module_contract.py` | 模块运行及 C11/C++17 公共 ABI 合同通过 |
| `test_skill_runtime.py`、`test_skill_arguments_runtime.py`、`test_skill_features_runtime.py` | Skill 相关回归通过 |
| `test_agent_contract.py`、`python -m unittest discover -s tests -p test_application_sdk_contract.py` | Agent 与 SDK 合同通过 |
| 相关 Node 测试 | 43 项通过，涵盖角色编辑/预览、JSON 保留、空工具集合、共享弹窗、分页、读取恢复和现有编辑器 |
| `test_extensions_runtime.py --packed ...` | 独立程序完成 22 次 loopback 模型请求，工具、Skill、委派、MCP、构建器与预览接口通过；收据无模型错误 |
| `test_application_sdk_runtime.py --packed ...` | 打包程序 SDK 目录、CRUD、typed values、providers、hooks、错误和重入注册通过 |
| `test_agent_continuation_packed.py --packed ...` | 9 次失败继续、10 次失败停止、成功重置失败计数、混合批次、空响应恢复等场景通过 |
| `test_tool_image_packed.py --packed ...` | PNG 读取、文本模型投影、中断后重启恢复且不重放工具通过 |

Node 验证命令：

```text
node --test tests/test_agent_authoring_ui.mjs tests/test_extension_authoring_windows.mjs tests/test_extension_formats.mjs tests/test_extension_validation.mjs tests/test_extension_reads.mjs tests/test_extension_browser.mjs tests/test_extension_loopback.mjs tests/test_composer_agent_defaults.mjs
```

Windows 构建已通过。此次未进行 Android 真机及 Linux 运行验证，不将公共 C/JS 代码的跨平台设计视为已完成真机回归。

## 本地程序

- 路径：`.build/agent-development/mdo.exe`
- 版本：0.7.0 开发版
- 文件大小：6,350,448 字节
- SHA256：`c203aae17b85533ab1a8647b9bc8aa8ff24be11b623312ae9bf7e02e9b3d0afb`
- 测试收据：`.build/agent-development/extension-receipt.json`
- 打包日志：`.build/agent-build-pack.log`

现有 `app/xs.json` 的窗口配置改动和 `tests/test_remote_asymmetric_pause.py` 均未纳入本轮提交，也未覆盖。
