# Android 工具目录再次启动验收

2026-10-09；修复提交 `72e8380a`。设备为 ARM64 RMX5090，Android 16。
精简版 build ID：`30000041`；完整版：`30000042`。

## 复现与修复

手机原完整版在强制关闭后重新打开，日志明确报错
`MdoRuntime.initialize: Invalid runtime path`。首次初始化创建的原生工具软链接
指向 PackageManager 安装目录；旧校验解析最终链接，将合法目标误判为越界。

修复只校验目标父目录，并用同目录临时文件和原子 rename 更新最终链接或数据。
真正越界的父目录、路径穿越和非法原生文件名仍被拒绝。

## 实测结果

- 修复版首次启动成功，页面显示新建任务界面。
- 调试完整版两次强制关闭、重新打开均成功；每次验证 98 个原生工具链接，
  指向当前安装目录且源文件存在；配置 SHA-256 保持不变。
- 覆盖安装非调试完整版，PackageManager 原生安装目录发生变化，启动成功。
- 非调试完整版再次强制关闭、重新打开成功；API 返回 build ID `30000042`，
  BusyBox、curl、jq、SSH、Python、SCP、SFTP 七项功能探测全部完成。
- 最终 APK 保留原手机版运行清单中的所有文件，包括额外工具文件。
- 两个 APK 的签名验证和 16 KB ZIP 对齐检查通过。

原手机版签名文件已丢失，本次经用户确认没有需要保存的数据后重新安装；
此后的调试版、非调试版覆盖升级使用同一个持久签名文件。临时源码快照构建必须
显式指定仓库外层 `.build/android-signing/development.p12`，避免另建签名。

## 可重复的路径回归

在 Linux/WSL 执行 `python tests/test_android_runtime_paths.py --java-home <JDK目录>`。
它使用真实软链接验证首次初始化、已有链接、升级后的失效链接及目录边界。
设备验证日志、截图、APK 和 SHA-256 保存在 `.build/android-restart/`。
