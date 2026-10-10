# Android 系统选图修复与原生输入补验

2026-10-10 至 10-11，RMX5090 / Android 16 / ARM64，系统 WebView 138。独立包
`org.xleaves.mdo.qa.stability`；未修改日用包、个人会话和系统安全设置。没有压力或高负载测试。

## 发现和修复

原 xs 窗口层使用 `FileChooserParams.parseResult`。系统选图器选择一张图片、确认
“添加 (1/100)”后，mdo 未显示附件，实际草稿仍是空列表。该系统返回 `ClipData`，
单地址解析未交付它。xs `f05f766` 改为解析单地址和列表，保留去重顺序，遵守单选模式，
取消交付空结果；仅接受文档提供器的 `content` 地址，避免外部选择器指定应用私有文件。
回调交付前先移除旧引用，避免覆盖重入的选择器。

12 个有界 JVM 案例和 Android API 36 的完整窗口层编译通过。对应代码、检查程序
已提交 xs 兼容分支；mdo 的 `deps.lock` 和源代码 bundle 也锁定该提交。

## 真机结果

- 对话完成后，实际打开模型面板从 QA Text 切换到 QA Image，服务端配置同步成功。
- 从系统选择器搜索测试 `pixel.png`、选择并确认，输入框显示缩略图，草稿包含图片。
  见 [系统选择](confirmed-selection.png)、[收到附件](attached.png)。
- 按 Home 切到后台，再返回同一会话，`inspect-pixel` 和图片保留。
  见 [后台返回](background-return.png)。后台空闲期间的一次电脑 HTTP 转发读取断开；
  返回前台后读取恢复。本记录不把这一项称为后台运行或网络长连接验证。
- 点击实际回复复制按钮，通过手机系统“粘贴”菜单粘贴中文、英文、俄文六行原文。
  草稿逐字比较，包括换行和末尾换行，完全一致。
- 再从系统选图器添加同一图片，点击手机发送按钮。模型收到一张图片，
  SHA-256 与 [原始 xge 图片](../xge-pixel-editor/smoke_window.png) 完全相同；
  输入文本与原文去除首尾空白后的值一致。一次调用完成，队列清空，
  `model_done`、`agent_done` 成功，没有 error 事件。见
  [多语言图片输入](multilingual-image-draft.png)、[完成回复](final-sent.png)。

中途测试模型漏配 `media-input`，服务端明确拒绝图片输入。修正夹具配置后，旧的
合成待发送项被移除，再通过实际界面重新选择图片和发送。本轮不把这次配置遗漏
算作模型或附件传输故障，也不声称已验证旧队列的确认重试成功。

## 可复现和构建边界

[结果摘要](proof.json)、[两次有界模型请求及事件](runtime.json) 保存精确证据。
补验入口为 `tests/manual_android_ui_qa.py`，要求独立 debuggable QA 包、全新证据目录，
30 分钟上限，结束后清理自身的端口转发。系统交互必须实际操作，脚本不模拟复制和选择。

新 APK 位于 `.build/conversation-acceptance/android-handheld-20261010/picker-fixed/mdo-stability-qa.apk`，
版本码 30000048，安装后实际 APK 摘要与候选一致，v2/v3 签名及 16 KB ZIP 对齐通过。
本次仅更换 Android Java 窗口层：应用 VFS 仍为 630556a0，原生库仍为 xs 93ef1f61，
两项与上一轮字节完全相同；不能将此包称为当前工作区所有未提交修改的完整发行版。

六行粘贴已验证；软键盘 Enter 仍按既有规则发送，真实多行换行输入、手机导入恢复、
首次系统语言检测以及 Windows WebView2 的系统复制/保存仍未在本记录完成。
