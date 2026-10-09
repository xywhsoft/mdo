# 当前 Windows 与 Android 测试产物

2026-10-10。本阶段整理 [功能覆盖及剩余检查](../../../docs/conversation-stability-acceptance.md)，没有修改生产代码，也没有运行压力或高负载测试。

Windows 使用上一阶段实际验收的干净 b9f42347 包。候选、根目录 `mdo.exe` 和根目录构建回执完全一致，用户数据未改；旧程序保存在 `.build/conversation-acceptance/schedule-results/workspace-before`。具体更新记录在 `../schedule-results/workspace-update.json`。

Android 从锁定的 xs 93ef1f61 重新编译 ARM64 原生宿主，使用相同压缩 VFS 打包为独立调试包 `org.xleaves.mdo.qa.stability`，不覆盖日用包。宿主为 2,669,120 字节，APK 4,191,469 字节。最低 Android API 26，目标 API 36，中文桌面名“墨斗”、英文和俄文为“mdo”。APK SHA-256 `af37974eeee348ed37f3de866f7e934e88b8e834c6114141b7302b494044eef4`。

`native.json`、`apk-badging.txt`、`apk-build.txt`、`artifacts.json` 保留实际构建及检查结果：v2/v3 签名、ZIP/ELF 16 KB 对齐、唯一 ARM64 原生库、系统依赖、宿主配置/导出符号、锁定源码摘要、原生页面地址解析类及与 Windows 包完全相同的 VFS。

```powershell
wsl -e env XS_BUILD_COMMIT=93ef1f61e3203cfd71868ae1c4d8a4743d96d9c8 python3 /mnt/d/GIT/mdo/.build/search-token-xs/tools/android/build_native.py xllm xllm-session xwork webview image --sdk /home/ubuntu/.cache/mdo-android-toolchain/sdk --out /mnt/d/GIT/mdo/.build/conversation-acceptance/stability-final/android/native --profile /mnt/d/GIT/mdo/tools/host-profile.json
python tests/acceptance/stability-final/check_artifacts.py --exe .build/conversation-acceptance/schedule-results/final/mdo.exe --apk .build/conversation-acceptance/stability-final/android/mdo-stability-qa.apk
```

APK 创建使用 `tools/android/build_apk.py`、仓库开发签名、`--debuggable`、独立包名及中文/英文/俄文品牌资源；摘要检查绑定实际产物，不能拿此报告证明另一个包的签名。通用构建步骤见 `docs/android.md`。

当前 adb 没有设备，测试包尚未安装。此报告没有 Android WebView、输入法、剪贴板、文件选择/保存或同签名覆盖升级的成功结论。官网未发布新版本。
