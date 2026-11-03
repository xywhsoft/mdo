# 2026-11-03 请求耗时仅调试模式显示

此方案随后按用户要求撤销：调试启动参数、bootstrap 的 debug 字段及请求耗时详情均已删除。以下保留为历史构建记录，不代表当前功能。

实现提交 `40495778ee003504edd394017d8d088d5279fab3`。

- `/api/v1/bootstrap` 增加布尔字段 `debug`，由宿主传递的应用参数 `-debug` 或 `--debug` 决定。`--home` 的后一个路径参数不作为调试开关。
- 前端默认隐藏“请求耗时”，仅在该字段严格等于 true 时创建详情 DOM；启动数据延迟到达、切换目标/重载启动状态及历史会话均适用。runtime 订阅在视图销毁时解除。
- 原有 token 用量、LLM/工具等待合计、输出速度与持久化模型耗时事件保留。
- 启动命令 `.\mdo.exe -- -debug`；现有 xs 宿主使用 `--` 分隔应用参数，直接在宿主参数中写 `-debug` 不受支持。使用说明见 `docs/session-diagnostics.md`。

验证：98 项相关 Node 测试、19 项前端合同和 261 个模块图检查通过；真实 Chromium 中文/英文/俄文 × 桌面/窄屏共 6 组、每组 11 项检查通过（报告 `.build/model-timing-debug/report.json`）。打包后的原生程序分别以正常、`-- -debug`、`-- --debug` 启动，bootstrap 的 debug 字段依次为 false/true/true。

测试包 `D:/GIT/mdo/.build/debug-timing-20261103/mdo-debug-timing.exe`，版本 0.8.8 / 05000808，6,726,806 字节；SHA-256 `219b757c4a005154925a1eb3b1d90cbd33b6c2f87117dd1776b3a1a0a2d351ae`。应用输入指纹 `f41a3fe053f11985a1614102dda5b8827040b0300fe49e5da81fa952ed6cfb1e`，打包前后输入未变化，复用既有宿主。

此包含其他进行中的工作区改动，`mdo_worktree_dirty=true`；未替换根目录或用户安装目录程序，未发布线上。
