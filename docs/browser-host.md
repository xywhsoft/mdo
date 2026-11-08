# 浏览器宿主能力

Browser Use 开发中形成的通用宿主能力已合入 mdo 主线。它们通过正常构建生成的
mdo 提供，不再要求部署私有应用源码覆盖层。当前版本与发布编号保持 0.9.5。

## 主线提供什么

- 浏览器实例、原生视图、面板展示与接管生命周期；关闭实例会取消其排队动作。
- 文档观察、稳定元素引用、原生输入、导航与等待、跨 frame 读取及截图。
- 原生对话框、弹出窗口归属、上传选择与下载的有界处理；下载采用独立暂存、
  校验和明确的发布步骤，不将界面成功响应当作文件已经落盘。
- 显示客户端、会话、项目和工作目录的固定归属；恢复旧任务前检查原始身份与修订。
- 操作和流的取消、期限、终态及重启恢复，工具失败后的真实结果投影与审批唤醒。
- 私有浏览器结果的 ACK 处理；不把执行结果写入可重放的前端意图日志。

这些是应用服务和客户端宿主能力。Browser Use 的模型工具、提示词、Skill 及插件自身
仍在独立扩展中维护；安装扩展后才有 `browseruse.*` 服务和对应模型工具。
主线没有自动安装或启用该扩展。

## C 与 JS 使用同一契约

通用入口仍为 Application SDK。JS 使用 `sdk.browser`、`sdk.files`、`sdk.call`；
C 使用 `<mdo/workbench.h>` 中的便捷函数或 `mdoCall`。
`tools/workbench_contracts.py` 生成 C 头文件、JS 包装和服务描述，不能单独修改生成物。

`browser.command` 增加 `observe`、`act`、`navigate_to`、`wait_for`、`dialog`、
`chooser`、`upload`、`download`、`save_download`、`capture`、`execute_js` 等命令。
`browser.popup.admit/settle` 处理原生弹窗绑定；`browser.workspace.lookup/adopt/prepare`
处理原任务工作目录。文件服务增加 `files.tree/digest/publish` 和 `files.receive.*`。
具体参数和限制以实际宿主的 `sdk.describe` 为准。

动作提交后读取 `browser.result` 的真实终态，再释放回执。超时、取消或断线不代表
动作从未执行，不应自动重放点击、表单提交或文件发布。必须指定显示客户端；
远程项目的 localhost 仍属于远端设备，不提供隐式跨设备端口映射。
基础面板和会话用法见 [工作区 SDK](workbench.md)。

`execute_js` 在明确的浏览器文档或 iframe 内执行 JS 异步函数体。传入 `script`，
用 `return` 返回 JSON，用 `await` 等待异步结果。`document` 是新观察的主文档 ID；
iframe 还须指定观察结果中的 `frame` 和 `frame_document`。
`world:page`（默认）访问网页全局变量；`world:isolated` 使用独立全局环境、共享 DOM，
与受信任的观察运行时分开。执行环境没有 mdo 主窗口原生绑定。原生协议使用不可重绑的
唯一上下文 ID，不接受调用方提供的原始协议身份。此功能目前仅支持 Windows。

脚本上限 64 KiB UTF-8；整个调用最多 10 秒，原生同步执行最多 3 秒；JSON 结果默认
16 KiB、最大 64 KiB。JS 可能改变页面，模型工具 `browser_evaluate` 按外部服务变更
处理，受只读、审批与用户接管限制。超时或取消不能撤回已经运行的脚本、计时器或请求；
需检查原页面，不能因丢失响应直接重放。
每个浏览器最多保留 16 个未完成的原生脚本回调；超时不会提前释放其名额。
达到 `script_pending_limit` 后需检查并明确结束浏览器，避免无限积累永不完成的 Promise。
完整参数、模型工具与 C／JS 调用见 [浏览器 JavaScript 执行](browser-javascript.md)。

## 标准构建与原生源码

Windows 原生实现位于 `tools/native/xs_browser*.inc.hpp`，Android 私有 CDP 传输位于
`tools/native/android/`，Java 适配器位于 `tools/android/`。
`deps.lock` 中的 SDK revision、兼容补丁及 `sdk_overlay.native_sources` 固定实际构建内容。
`tools/build_mdo.py` 在准备或复用 SDK 时校验主线原生源码和实际 SDK 文件的规范化哈希；
过期补丁或不匹配的源码明确失败。产品构建不依赖 `mdo-browser-use` 的目录或部署脚本。

修改原生实现时，应同步更新锁定 SDK 补丁、源码清单及哈希，再通过源码一致性检查。
Android APK 构建与 Java 编译检查共用 `prepare_java_sources`，避免检查遗漏真实打包来源。

## 平台状态

Windows WebView2 的 SDK 链路和实际托管工具链路已经在主线应用源码上通过验证，
包括文档/frame 观察、输入、导航、等待、截图、对话框、弹窗和只读权限拒绝。

Android 的适配器和私有传输也已合入；本轮通过 78 个 Java 源文件编译及 ARM64 JNI
语法编译，没有完成新 APK 链接、安装或真机回归。自动化能力仍明确报告
`browser_use.available:false`：缺少隔离世界或适配器尚未完成时拒绝执行，不降级到
网页执行世界。普通人工浏览与 Browser Use 自动化的支持状态不能混为一谈。

## 回归入口

- `tools/qa_release.py` 的前端阶段包含 `tests/test_*.mjs` 与
  `tests/browser_host/test_*.mjs`，顺序执行。
- `tests/test_browser_host_native_sources.py` 检查主线源码与原生 SDK 一致性及目录边界。
- `tests/test_application_sdk_contract.py`、`tests/test_model_sdk_build.py`
  检查 SDK 契约和构建约束。

组件测试不能代替原生浏览器、手机或双设备验收。本次合并的准确范围、构建与运行
收据见 [2026-11-08 集成记录](build-records/2026-11-08-browser-host-integration.md)。
