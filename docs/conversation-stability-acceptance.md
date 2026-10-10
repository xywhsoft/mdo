# 对话稳定性验收快照

2026-10-11。此文件汇总本次长期任务的功能覆盖与剩余实际检查，便于从逐阶段记录直接找到证据。详细修复过程见 [持续改进记录](conversation-stability-plan.md)。没有进行压力或高负载测试，不能据此承诺所有设备和第三方服务均无故障。

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

表中包含不同阶段的实际程序和宿主，不能将较早打包记录说成最终二进制重跑了每个案例。完整对话、计划和中断换模型检查见各阶段记录。最新修复的来源组件完成真实复制事件前后对照，39 项已有相关检查和 164 模块检查通过，见 [恢复草稿复制](../tests/acceptance/recovery-draft-copy/README.md)。近期未改动的功能沿用其明确的先前证据；如改动涉及该路径，应重新做相关检查。

## 当前可使用和待实测的程序

- 工作区 `D:\GIT\mdo\mdo.exe` 和 `E:\software\mdo\mdo.exe` 当前为 630556a0 应用，4,559,767 字节，包括中断换模型、恢复状态自动刷新，以及弹层草稿复制兼容修复。原生宿主沿用前阶段，最新复制修复做了来源组件页面检查；没有把它说成重新完成所有原生检查。两处上一版备份在 `.build/conversation-acceptance/recovery-draft-copy/install-before-modal`，Home 的文件清单、大小和修改时间未变，没有启动日用 Home。更新摘要见 [记录](../tests/acceptance/recovery-draft-copy/install-update.json)。
- 最新独立 ARM64 精简测试包在 `.build/conversation-acceptance/android-handheld-20261010/picker-fixed/mdo-stability-qa.apk`，版本码 30000048、SHA-256 `ed95ab0ca106526c85b960990064c80c8503cca5292f1297bf757a060dfdda0f`。安装后的实际 APK 字节相同，v2/v3 签名和 16 KB ZIP 对齐通过。此版只更换 xs f05f766 的 Java 文件选择层；宿主仍为 xs 93ef1f61、xwork ABI 8，应用 VFS 仍为 630556a0，两项与上一轮逐字节一致。它使用独立 `org.xleaves.mdo.qa.stability` 包，不是日用包的升级安装器。真实 ARM64 重试/恢复、复制粘贴与系统导出见 [手机验收](../tests/acceptance/android-handheld/README.md)，选图及发送见 [选择器修复](../tests/acceptance/android-file-chooser/README.md)，恢复与语言见 [后续补验](../tests/acceptance/android-restore-locale/README.md)。这些结果不覆盖工作区后续未提交的改动。
- 此前 b9f42347 已补验四次真实 Windows 原生窗口启动、移动 Home 后重启、环境/命令 Home 路径及 20 秒打包启动，全部通过；阶段产物证据见 [stability-final](../tests/acceptance/stability-final/README.md)。最新原生/页面/旧数据复核、982 项前端和 142 项 Python 检查（3 项环境跳过）见 [interrupted-profile](../tests/acceptance/interrupted-profile/README.md)。没有更新官网或手机日用包。原生启动不代表复制/文件保存已验证，构建检查也不代表 Android WebView 可用。

## 剩余实际检查

1. **Android 原生页面已补验**：冻结应用在 RMX5090 验证启动、原生发送；正式 API 验证退避取消、恢复和进程重启后继续。发送后模型切换和前台返回保留文字/图片通过，见 [原生补验](../tests/acceptance/android-file-chooser/README.md)。实际 Android 语言环境的俄文、中文、其他语言回退英文及保留用户偏好通过；仅清除 QA 语言偏好来验证初始化路径，没有全新安装或改变全局系统语言。见 [后续记录](../tests/acceptance/android-restore-locale/README.md)。
2. **输入与手机系统接口已补验**：真实中文候选确认、系统复制粘贴、Markdown/备份保存并读回通过。系统图片选择发现 xs 丢失 `ClipData`，修复已提交并锁定依赖；实际选图、发送图片摘要及中英俄六行复制粘贴通过。通过系统文件选择器导入原备份，45 条历史完整保留，实际更换模型后续聊成功；原生 Shift+Enter 生成两行草稿、软键盘 Enter 只发送一次。多行组合键由 ADB 注入，不声称实测实体外接键盘。见 [恢复与输入](../tests/acceptance/android-restore-locale/README.md)。本轮没有使用个人会话。
3. **Windows 系统交接**：在正式原生 WebView2 窗口完成复制后 Ctrl+V，以及 Markdown/备份保存落盘。本轮官方入口能读取控件树和填写合成输入，但窗口截屏及输入几何信息仍失败；没有发送、复制或导出，隔离模型调用为零。实包模块的草稿选区检查通过，仍不能代替系统粘贴或保存。当前具体命令见 [桌面手工验收入口](../tests/acceptance/desktop-copy-export/README.md)。

本机已连接 ARM64 手机，独立测试包已安装。明确的输入、原生保存或生命周期问题将继续修复并逐阶段提交；不为了这些限制无限重跑已经通过的回环模型和前端检查，不新增治理框架或压力用例。
