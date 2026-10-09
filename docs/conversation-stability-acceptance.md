# 对话稳定性验收快照

2026-10-10。此文件汇总本次长期任务的功能覆盖与剩余实际检查，便于从逐阶段记录直接找到证据。详细修复过程见 [持续改进记录](conversation-stability-plan.md)。没有进行压力或高负载测试，不能据此承诺所有设备和第三方服务均无故障。

## 已完成的核心目标

| 范围 | 已实际验证的行为 | 主要证据 |
| --- | --- | --- |
| 模型调用 | 临时 429/503、未交付内容的网络错误有界退避，遵守 Retry-After；取消停止等待。每日额度、余额、账号权限和配置拒绝有准确最终提示。 | [对话页面](../tests/acceptance/conversation-ui/README.md)、[本地配置错误](../tests/acceptance/model-request-configuration/README.md) |
| 流式与上下文 | 缺少结束标记的流不冒报成功；失败草稿替换恢复；无效或额度不足的摘要保留原历史，取消和继续可用。 | [摘要失败与恢复](../tests/acceptance/conversation-compaction-failure/README.md)、[连续完成状态](../tests/acceptance/conversation-completion/README.md) |
| 中断、队列与重启 | 真实执行期间停止宿主，原输入和队列保留；明确继续后完成，不重复原输入。普通断网保留输入，宿主重启后重新载入原会话可续聊；原模型不可用时可先更换模型，保留中断调用并自动刷新恢复界面。 | [执行中断](../tests/acceptance/conversation-queue-interruption/README.md)、[断网与重启](../tests/acceptance/conversation-network/README.md)、[中断后换模型](../tests/acceptance/interrupted-profile/README.md) |
| 真实开发能力 | mdo 使用 xge 公共 API 编写并修正 32×32 像素编辑器，画笔、橡皮、调色板、缩放及 PNG 导出通过编译、自测和实际窗口检查；72 次模型、80 次工具。 | [编辑器源码及记录](../tests/acceptance/xge-pixel-editor/README.md) |
| 项目与会话 | 目录选择、创建、重命名、置顶、归档、回收站恢复、搜索和独立草稿；原会话目录绑定与新默认目录分离；重启后续聊。 | [日常操作](../tests/acceptance/projects-sessions-ui/README.md) |
| 账号、搜索与远程 | 登录/续期、账号切换、额度/余额、会员模型及模型切换；已完成的搜索响应丢失可用原编号确认，重启恢复不重复扣减；远程消息、附件、决策及重载。 | [账号](../tests/acceptance/account-and-http-mcp/README.md)、[续期](../tests/acceptance/conversation-auth-renewal/README.md)、[搜索](../tests/acceptance/search-ui-recovery/README.md)、[远程](../tests/acceptance/remote-search-regression/README.md) |
| 附件、询问与权限 | 图片保存确认丢失后读取原件，发送后续聊正常；待办、询问和审批可操作；拒绝不导致重复执行。 | [实际图片页面](../tests/acceptance/image-upload-browser/README.md)、[询问和审批](../tests/acceptance/conversation-decisions/README.md)、[决策响应丢失](../tests/acceptance/decision-write-recovery/README.md) |
| 设置与扩展 | 设置读取/保存确认恢复；主/子 Agent、C 工具、Skill、命令、MCP 实际调用，按需加载定义；本地网站发布、审核、安装、更新和失败回滚。 | [设置](../tests/acceptance/conversation-settings-recovery/README.md)、[生态](../tests/acceptance/ecosystem-local-ui/README.md)、[MCP 状态](../tests/acceptance/mcp-http-status/README.md)、[启动确认](../tests/acceptance/operation-start/README.md) |
| 计划任务 | 读取临时失败安静恢复；创建确认丢失不重复计划，修改不盲目重放；无法确认运行时提示核查历史；准确最终模型原因、三语言页面和重启历史通过。 | [读取](../tests/acceptance/schedule-reads/README.md)、[写入](../tests/acceptance/schedule-write/README.md)、[执行原因](../tests/acceptance/schedule-results/README.md) |
| 备份与导出数据 | 备份/Markdown 内容和图片完整，导入确认后独立会话可续聊；Android 保存桥接上限在交接前准确提示。 | [备份恢复](../tests/acceptance/conversation-backups/README.md)、[导出平台边界](../tests/acceptance/export-platform/README.md) |

表中包含不同阶段的实际程序和宿主，不能将较早打包记录说成最终二进制重跑了每个案例。最新 Windows 对计划执行、重启和正常对话进行了实际页面检查，981 项完整前端及 124 项契约通过，原生/API、严格编译门禁通过。近期未改动的功能沿用其明确的先前证据；如改动涉及该路径，应重新做相关检查。

## 当前可使用和待实测的程序

- 工作区 `D:\GIT\mdo\mdo.exe` 和 `E:\software\mdo\mdo.exe` 当前为实际页面验收的 f5e55072 应用，4,559,669 字节，包括最新中断换模型及恢复状态自动刷新修复。两处上一版备份在 `.build/conversation-acceptance/interrupted-profile/install-before`，Home 的文件清单、大小和修改时间未变，没有启动日用 Home。更新摘要见 [记录](../tests/acceptance/interrupted-profile/install-update.json)。
- 最新独立 ARM64 精简测试包在 `.build/conversation-acceptance/interrupted-profile/android/mdo-stability-qa.apk`，4,191,469 字节；沿用重新编译的宿主 xs 93ef1f61、xwork ABI 8，内置 VFS 与最新已验收 Windows 包完全相同。实际 v2/v3 签名、ZIP 对齐和宿主/SDK 摘要通过，原生 ELF 16 KB 对齐沿用同一字节的先前记录。它使用独立 `org.xleaves.mdo.qa.stability` 包，不是旧日用包的升级安装器。
- 此前 b9f42347 已补验四次真实 Windows 原生窗口启动、移动 Home 后重启、环境/命令 Home 路径及 20 秒打包启动，全部通过；阶段产物证据见 [stability-final](../tests/acceptance/stability-final/README.md)。最新原生/页面/旧数据复核、982 项前端和 142 项 Python 检查（3 项环境跳过）见 [interrupted-profile](../tests/acceptance/interrupted-profile/README.md)。没有更新官网或手机日用包。原生启动不代表复制/文件保存已验证，构建检查也不代表 Android WebView 可用。

## 剩余实际检查

1. **Android 原生页面**：使用独立包验证启动、语言、发送、模型切换、退避取消、切到后台后返回及进程重启后继续。较早的真机原生/API 对话已通过，但较早测试包随后无法找到的原因没有确定；不能把电脑浏览器当成手机 WebView。
2. **输入与手机系统接口**：可信中文输入法和换行/组合输入、复制后真正粘贴、系统文件选择器添加图片、Markdown/备份实际保存并读回。程序应保留未发送内容，测试不使用个人会话。
3. **Windows 系统交接**：在正式原生 WebView2 窗口完成复制后 Ctrl+V，以及 Markdown/备份保存落盘。原生窗口自动控制受当前工具能力限制；浏览器的虚拟剪贴板和 Blob 交接不是此项通过的证据，见 [桌面验收入口](../tests/acceptance/desktop-copy-export/README.md)。

本机当前 adb 没有设备，手机检查保持待验收。明确的输入、原生保存或生命周期问题将继续修复并逐阶段提交；不为了这些限制无限重跑已经通过的回环模型和前端检查，不新增治理框架或压力用例。
