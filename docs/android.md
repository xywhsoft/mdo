# mdo Android

安卓版在设备本地运行 xs、TCC、VFS、xllm-session 和 xwork，复用 PC 版的 C
后端与 HTML 前端。APK 支持 `arm64-v8a`（ARMv8-A 64 位），最低声明 Android
8.0 / API 26，目标 API 36；已在 Android 16 / API 36 的 ARM64 真机上启动和对话。

## 构建

准备 Python 3.10+、GCC、JDK 17+。xs 的安装脚本下载并校验锁定的 Google 工具链，
不依赖 Android Studio 或 Gradle。SDK 源码必须匹配 `deps.lock`，不要直接使用有
未同步修改的 xserver 主目录。

Linux：

```sh
python3 ../xserver-mdo-refactor/tools/android/setup.py --root "$HOME/.cache/mdo-android-toolchain"
python3 tools/build_android.py --xserver-root ../xserver-mdo-refactor \
  --sdk "$HOME/.cache/mdo-android-toolchain/sdk" --java-home "$JAVA_HOME"
```

Windows 可用对应 Windows 工具链执行相同 Python 命令，也可由 Windows 工作区驱动 WSL：

```powershell
python tools/build_android.py --xserver-root D:\GIT\xserver-mdo-refactor --wsl `
  --sdk /home/ubuntu/.cache/mdo-android-toolchain/sdk `
  --java-home /home/ubuntu/.cache/mdo-android-toolchain/java/usr/lib/jvm/java-17-openjdk-amd64
```

WSL 方式的 SDK 和 JDK 参数为 Linux 路径，输出保留在 Windows 工作区。更换机器时
先安装 JDK 17，再运行工具链 setup；路径可以任意指定。内建模型凭据仍通过现有
`--builtin-connection`、环境变量或被忽略的 `.build/ornith-connection.json` 注入，
没有凭据时构建会明确失败。不要提交密钥。

默认产物为根目录 `mdo-arm64-v8a.apk`。构建中间文件在 `.build/android/`。
墨斗图标资源位于 `assets/branding/android/res/`，构建器会编译并写入 APK 的
launcher/round icon；Android 8+ 使用自适应图标。Windows 的 ICO 和前端标识
使用同一原图，见 `assets/branding/README.md`。导出资源已入库，普通构建不需要 Pillow。
开发签名位于 `.build/android-signing/development.p12`，不入库；保留它才能原位升级
保留已安装应用的数据。此构建器生成供安装验证的开发签名 APK，未发布应用商店。
公开发行时使用自己的正式签名流程。默认不启用 debuggable；本地验证可添加
`--debuggable --output .build/android/mdo-arm64-v8a-debug.apk`。

## 设备使用

打开应用即启动本地服务；通知中的“停止”会取消服务并正常排空资源。切到后台和
旋转屏幕保留后端。系统仍可能因省电策略停止应用，后台计划任务不保证准时唤醒。
可变配置、项目、会话和模块都在应用私有的 `files/mdo-home/`；内置 pack 放在
codeCache，WebView 浏览器数据也受 Android 应用沙箱管理。卸载会清除这些私有数据。

Android 的目录权限与 PC 不同：项目应使用应用可访问的实际目录。附件通过系统
文件选择器导入，导出通过系统保存窗口。`content://` 目录不能直接当作 C 文件系统
路径。Shell 使用设备已有的 `/system/bin/sh` 和 Toybox；Bash、PowerShell、Git
及依赖外部可执行程序的 MCP 需要设备实际具备相应程序。

USB 验证：

```powershell
python D:\GIT\xserver-mdo-refactor\tools\android\setup.py --root .build\android-toolchain-windows --platform-tools-only
.build\android-toolchain-windows\sdk\platform-tools\adb.exe devices -l
.build\android-toolchain-windows\sdk\platform-tools\adb.exe install -r mdo-arm64-v8a.apk
```

验证版的后端日志可通过 `adb shell run-as org.xleaves.mdo cat files/mdo-home/native.log`
查看。签名、16 KB ZIP 对齐和 ELF 链接在构建时检查；Windows/Linux 的原有构建入口
保持可用。底层宿主与 TCC 设计见 xs 的 `docs/android.md`。
