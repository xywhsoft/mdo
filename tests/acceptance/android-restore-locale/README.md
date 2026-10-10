# Android 备份恢复、语言初始化与多行输入

2026-10-11，RMX5090 / Android 16 / ARM64 / WebView 138。使用独立
`org.xleaves.mdo.qa.stability`，版本码 30000048，应用 VFS 为 630556a0，Java 窗口层
为 xs f05f766。未操作日用包、个人会话、全局系统语言或安全设置；没有压力或高负载测试。
这是冻结候选的实际手机验收，不覆盖工作区后续未提交的改动。

## 手机导入和继续

从实际会话菜单选择导入备份，在 Android 文件选择器中选取上一阶段系统保存的
[原始 JSON](../android-handheld/exports/mdo-session-V-OSUDF4wsc0aEQlyck42u5l6Xf6Na2N.backu.json)。
文件为 49,147 字节，SHA-256 与原保存记录一致。导入预览显示 7 个文件、45 条记录；
实际点击核对目标、确认恢复、打开会话，目标仅为独立 QA Home 的 workspace。
见 [预览](preview.png)、[核对](review.png)、[恢复完成](committed.png)。

恢复状态为 committed、terminal，无需重启、没有待清理状态。原有 45 条 UI 记录逐项
与备份比较一致，仅按正常恢复规则替换会话编号。新会话编号为
`40a721cd7549054b74dd928781e4db57`。在手机模型面板切换为 QA Text，实际发送
`after-native-restore`，模型收到一次请求并成功完成，见 [续聊](continued.png)、
[续聊事件](restored-events.json)。导出中的 `restore_ready=false` 不妨碍导入器核对后恢复。

## 多行输入

在同一恢复会话中输入 `line-one`，通过 ADB 向实际 WebView 注入原生 Shift+Enter
组合键（59、66），然后输入 `line-two`。没有误发，实际草稿为 `line-one\nline-two`，
见 [两行草稿](multiline.png)。点击真实软键盘 Enter 后，模型仅收到一次完全相同的
两行文本，成功完成，见 [发送后](multiline-sent.png)、[执行事件](keyboard-events.json)。

此项覆盖原生按键分发与手机软键盘发送，不声称验证了实体外接键盘。原交互规则
仍为 Enter 发送、Shift+Enter 换行。手机中文输入法候选确认及系统粘贴另见
[上一阶段](../android-handheld/README.md)。

## 初始语言路径

仅对 QA 包使用 Android 的应用语言覆盖，并在正式设置接口将其语言偏好置为 auto，
然后结束 QA 进程、重新启动实际 APK。界面观察与保存的偏好一致：

| QA 应用语言环境 | 原偏好 | 初始化后 | 截图 |
| --- | --- | --- | --- |
| ru-RU | auto | ru-RU | [俄文](russian-auto.png) |
| ja-JP | 已选择 ru-RU | 保留 ru-RU | [用户偏好保留](explicit-russian-preserved.png) |
| ja-JP | auto | en-US | [默认英文](unsupported-auto.png) |
| zh-CN | auto | zh-CN | [中文](chinese-auto.png) |

这验证的是未设置偏好时的语言初始化路径，未重新安装，也未修改整个手机的系统语言。
完成后清除 QA 应用语言覆盖，恢复原中文偏好。不能称为新设备全新安装验收。

## 证据与复现边界

[结果摘要](proof.json) 保存精确请求、版本和恢复摘要；
[恢复后的原始记录](restored-ui-events.jsonl) 保留原 45 条及后续两轮各 5 条记录。
原始 JSONL 的 kind 是数值，HTTP 事件的 kind 是字符串，按各自格式核对。
两个夹具各有一次建立基准的请求和一次手机实际发送，共 4 次有界模拟模型调用。
语言检查没有模型调用；所有夹具已正常结束并清理自己的端口转发。

可使用 [原生 UI 夹具](../../manual_android_ui_qa.py)，填写实际 ADB 路径及设备号，
证据目录必须是新目录，时限不超过 1800 秒：

```powershell
python tests/manual_android_ui_qa.py --adb <adb路径> --serial <设备号> --package org.xleaves.mdo.qa.stability --evidence .build/android-native-restore-new --seconds 1800
```

系统选择、确认、复制和软键盘操作由实际手机 UI 完成，夹具不模拟这些行为。
复验语言时，`adb shell cmd locale set-app-locales org.xleaves.mdo.qa.stability --locales ru-RU`
只修改此独立包；完成后用不带 `--locales` 的同一命令清除覆盖。
不要对日用包清空设置或数据。

Windows WebView2 的系统剪贴板和文件保存仍需独立手工确认，手机通过不代替桌面通过。
