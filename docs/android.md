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
先安装 JDK 17，再运行工具链 setup；路径可以任意指定。APK 不包含上游模型密钥，
内建在线模型使用网站账号登录和模型网关；构建无需模型凭据。

默认 `--edition both`，同时生成根目录精简版 `mdo-arm64-v8a.apk` 和完整版
`mdo-full-arm64-v8a.apk`，复用同一次宿主、应用 pack 和原生库构建。
`--edition lite` / `--edition full` 可单独构建一版；`--output` 与 `--version-code`
只适用于单版构建。完整版包含 BusyBox、curl、jq、SSH/SCP/SFTP、Python、aria2c、ripgrep 和 7-Zip。两版使用相同
应用 ID 和签名；每个发布产物使用 `app/release.json` 中各自的递增内部编号。
工具通过 Android 原生库安装目录执行，数据留在应用私有目录；完整流程及产物
位置见 `docs/distribution.md`。构建中间文件在 `.build/android/`。
墨斗图标资源位于 `assets/branding/android/res/`，构建器会编译并写入 APK 的
launcher/round icon；Android 8+ 使用自适应图标。Windows 的 ICO 和前端标识
使用同一原图，见 `assets/branding/README.md`。导出资源已入库，普通构建不需要 Pillow。
开发签名位于 `.build/android-signing/development.p12`，不入库；保留它才能原位升级
保留已安装应用的数据。此构建器生成供安装验证的开发签名 APK，未发布应用商店。
公开发行时使用自己的正式签名流程。默认不启用 debuggable；本地验证可添加
`--edition full --debuggable --output .build/android/mdo-arm64-v8a-debug.apk`。

根目录 `生成程序.bat` 和 `python tools/build.py` 默认构建桌面版与这两种 APK。
已有工具链可以通过 `--sdk` / `--java-home`（WSL 加 `--wsl`）或
`MDO_ANDROID_SDK` / `MDO_ANDROID_JAVA_HOME` 提供。未指定时检测工作区 `.build/`
中的工具链、用户缓存及 Windows 默认 WSL 的 `~/.cache/mdo-android-toolchain`。
`--skip-host-build --skip-native-build` 可复用已通过锁文件和指纹验证的构建缓存。

## 设备使用

桌面应用名由 Android 系统语言决定：中文（含繁体）显示“墨斗”，其他语言显示
“mdo”。首次打开界面时，以系统首选语言选择中文、俄文或英文；其他语言默认
英文。选择会通过现有设置 API 保存到 `mdo-home/config/settings.json`，后续启动
保留该选择，也可以在“设置 → 通用”手动修改。已有语言设置不会被升级覆盖。
内置 `locale: "auto"` 仅代表尚未初始化；无界面的服务启动不写入语言设置。
Android 首选语言从 xs 的只读 `XsPlatform.languages()` 桥接读取，避免 WebView
的 `navigator.language` 与系统语言不同；普通浏览器仍使用 `navigator`。

打开应用即启动本地服务；通知中的“停止”会取消服务并正常排空资源。切到后台和
旋转屏幕保留后端。系统仍可能因省电策略停止应用，后台计划任务不保证准时唤醒。

恢复页面会等待本地服务就绪。若服务重启后更换端口，会把当前会话的路径和页面
锚点接到新地址，保留所在会话；原服务仍在运行时可以恢复页面历史。此行为已有
JVM 地址解析和安卓外壳编译检查，系统回收 WebView、输入法和文件选择器仍需真机
验收，不能用桌面浏览器测试代替。
可变配置、项目、会话和模块都在应用私有的 `files/mdo-home/`；内置 pack 放在
codeCache，WebView 浏览器数据也受 Android 应用沙箱管理。卸载会清除这些私有数据。

启用设备远控前，在目标手机的系统“应用信息 → 电池／耗电管理”中允许墨斗后台
运行。部分机型需确认“完全允许后台行为”或“不受限制”；仅打开“允许后台活动”
可能仍被厂商冻结或限制联网。建议允许通知，以查看常驻服务状态和停止入口；若
系统提供应用速冻／休眠选项，取消对墨斗的限制。墨斗不会自动修改这些系统设置。
开启远控会增加耗电，强制停止、重启或系统后台策略仍可能导致离线；请重新打开
墨斗确认。远控设置页包含相同提示，PC 控制 Android 时也按目标平台显示。

Android 的目录权限与 PC 不同：项目应使用应用可访问的实际目录。附件通过系统
文件选择器导入，导出通过系统保存窗口。`content://` 目录不能直接当作 C 文件系统
路径。精简版 Shell 使用设备已有的 `/system/bin/sh` 和 Toybox；完整版提供
额外工具并自动注入经过探测的绝对路径。Bash、PowerShell、Git 及其他依赖外部
可执行程序的 MCP 仍需要设备实际具备相应程序。

USB 验证：

```powershell
python D:\GIT\xserver-mdo-refactor\tools\android\setup.py --root .build\android-toolchain-windows --platform-tools-only
.build\android-toolchain-windows\sdk\platform-tools\adb.exe devices -l
.build\android-toolchain-windows\sdk\platform-tools\adb.exe install -r mdo-arm64-v8a.apk
```

验证版的后端日志可通过 `adb shell run-as org.xleaves.mdo cat files/mdo-home/native.log`
查看。签名、16 KB ZIP 对齐和 ELF 链接在构建时检查；Windows/Linux 的原有构建入口
保持可用。底层宿主与 TCC 设计见 xs 的 `docs/android.md`。

## 正式签名与设备验收

默认使用持久的本地开发签名。使用正式签名时指定已有文件 `--keystore`、
`--key-alias`、`--store-password-env MDO_ANDROID_STORE_PASSWORD`；密钥密码不同时再加
`--key-password-env MDO_ANDROID_KEY_PASSWORD`。环境变量由运行时提供；WSL 构建会传递
变量名及其当前值，apksigner 使用 `env:` 读取，日志不回显密码。自定义 keystore 不会
自动创建，也不会使用默认开发密码。精简版和完整版必须始终使用同一正式签名。

`python tests/test_android_device_runtime.py` 是显式运行的设备网络检查，需开发签名的
`--debuggable` 完整版与测试机 ADB 连接。测试服务只监听本机，通过临时 ADB reverse
供手机访问；需要本机已有 Paramiko/cryptography。它验证 TLS、SSH/SCP/SFTP 后删除
临时文件并移除端口映射；最终安装正常的非调试完整版。应用的 `/api/v1/distribution`
还必须列出十项已通过功能探测的工具能力，以确认正常应用进程的执行路径。

Android 精简版与完整版覆盖同一个 mdo 图标，不会同时出现两个入口。完整版可从
“设置 → 扩展能力 → 扩展工具包”查看实际工具及其版本。覆盖升级不使用卸载或降级；
在测试前后读取配置、项目及会话指纹，确认数据未变。

## 工具目录初始化与再次启动

完整版工具通过软链接指向 PackageManager 安装的原生文件。路径检查只解析
目标的父目录并确保它位于应用运行目录内；最后一个路径分量允许是工具软链接。
每次启动使用同目录临时文件和原子 rename 更新数据及链接，覆盖升级后也会修复
指向旧安装目录的失效链接。该过程不修改 `files/mdo-home/` 内的配置或会话。

`python tests/test_android_runtime_paths.py --java-home <JDK目录>` 在 Linux/WSL
真实软链接上验证首次初始化、再次启动、升级后的失效链接，以及路径越界拒绝。
设备验收应覆盖安装完整版后连续关闭、重开，再覆盖升级并重新打开；不要卸载或
清空应用数据来绕过启动问题。

## Android 系统分享

Android 分享面板可把文字、链接、单个文件或多个文件发送到墨斗。应用冷启动和
已运行时均接收 `ACTION_SEND` / `ACTION_SEND_MULTIPLE`，支持标准 `content://`
流与 `ClipData`，不依赖额外第三方库，也不申请全盘存储权限。

接收界面可选择本机项目，以及新会话或已有的进行中会话。文字和链接加入草稿；
文件复制到该会话实际工作目录的 `shared-files/<分享ID>/`，以相对路径引用加入草稿。
模型支持图片时，PNG/JPEG/WebP 同时加入图片附件（沿用 8 MiB/张、4 张的限制）。
**分享不会自动发送消息或启动模型调用**，导入完成后仍需用户确认发送。

来源 URI 的临时授权期间，原生层在后台把内容复制到 `files/mdo-shares/` 私有
收件箱，保存完成再提示选择会话。该目录独立于 Home，避免首次分享干扰 Home
初始化。每次最多 32 个文件、单文件 64 MiB、合计 128 MiB；收件箱最多保留 8 批、
256 MiB。文字最多 32 KiB。超限、权限不足、复制不完整会明确提示重新分享。
“稍后处理”保留收件箱，可从输入框附近的“接收分享”重新打开；“丢弃分享”只清理
私有收件箱副本，既不删除来源文件，也不删除已导入工作区的文件。

导入采用固定的分享/文件 ID、SHA-256 校验、排他临时文件和禁止覆盖的原子发布。
相同操作重试不会重复写文件，用户已编辑的目标文件不会被覆盖。服务端只接受
分享 ID 和文件 ID，校验收件箱清单绑定的项目/会话并使用受限根目录 IO，拒绝路径
越界及目录链接。图片上传也使用固定 ID。草稿保存计划持久化到收件箱；只有草稿
确认落盘后才清理收件箱，因此确认响应丢失后的重试不会直接重复追加。草稿同时
被修改时会请求用户确认，拒绝静默覆盖。切到远程设备时保留分享，须回到本机
才能导入；不会悄悄写入当前远程设备。

自动回归：`node --test tests/test_android_shares.mjs` 验证草稿重放、冲突、设备
切换及图片完整性；`python tests/test_shared_files_runtime.py --host <xs程序>`
走真实本机 HTTP/TCC 文件导入；`python tests/test_android_share_files.py` 在
Linux/WSL 的真实 JVM 验证有界流、文件名、原子写入和软链接拒绝。浏览器用例为
`tests/fixtures/android-share-browser.html`，与 Android 真机测试分开。
真机验收还需覆盖冷启动分享、后台唤起、多文件、来源授权失效和收件箱延后处理。
