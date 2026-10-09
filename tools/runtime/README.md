# mdo 可选外部工具

已准备 Windows x64 与 Android arm64-v8a 两组运行文件，每个工具独立打包。
基础组包括 BusyBox、curl、jq、OpenSSH 客户端、aria2、ripgrep、7-Zip；Python 为可选增强包。
原有 Windows Git 保留为单独的可选包，Android Git 不在本轮范围内。

| 工具 | Windows x64 | Android arm64-v8a | 入口 |
| --- | --- | --- | --- |
| BusyBox | 1.38.0-FRP-6075-g169694ebd，Unicode UCRT 版 | 1.38.0，精简静态构建 | `busybox[.exe] <applet>` |
| curl | 保留原有 8.13.0，Schannel | 8.22.0，OpenSSL 3.5.9 / zlib 1.3.1 静态纳入 | `curl[.exe]` |
| jq | 1.8.2 | 1.8.2，含 Oniguruma 正则 | `jq[.exe]` |
| OpenSSH | 10.3p1 / OpenSSL 3.5.7，MSYS 依赖随包提供 | 10.3p1 / OpenSSL 3.5.9，第三方依赖静态纳入 | `ssh`、`scp`、`sftp`、`ssh-keygen`、`ssh-keyscan` |
| aria2 | 1.37.0，官方 x64，Windows TLS | 1.37.0，OpenSSL 3.5.9 / zlib 1.3.1 | `aria2c[.exe]` |
| ripgrep | 15.2.0，官方 MSVC，含 PCRE2 | 15.2.0，Rust 1.94.0，默认正则引擎 | `rg[.exe]` |
| 7-Zip | 26.04，官方 x64，7z.exe + 7z.dll | 26.04，Alone2 完整命令行格式支持 | `7z.exe` / `7zz` |
| Python | 保留原有 3.13.7 embeddable | 官方 3.14.8 Android SDK + C 启动器 | `python/python.exe` / `python/bin/python3` |
| Git（可选） | 保留原有 2.55.0.windows.3 | 未准备 | `git/cmd/git.exe` |

Windows OpenSSH 额外提供 `ssh-agent.exe`、`ssh-add.exe`。Android 本轮仅构建
客户端，不含 sshd、agent、Kerberos、FIDO/PKCS11 辅助程序；公钥、密码登录和
SCP/SFTP 是主要使用范围。Python 保留标准库、SQLite、TLS、ctypes，去掉 SDK
头文件、测试库、GUI、ensurepip；第三方 wheel / pip 尚未准备。

## 目录与发布素材

```text
tools/runtime/
  windows-x86_64/{busybox,curl,jq,openssh,aria2,ripgrep,7zip,python,git}/
  android-arm64-v8a/{busybox,curl,jq,openssh,aria2,ripgrep,7zip,python}/
  packages/             # 每工具 ZIP + SHA256SUMS
  sources/              # BusyBox、aria2、ripgrep、7-Zip 对应源码归档
  build/                # 来源锁、构建、打包、验证脚本及 Python C 启动器
  catalog.json          # 版本、入口、依赖、每文件哈希、ZIP 哈希和体积
  verification.json     # Windows 与手机测试结果，不包含测试私钥
```

原有工具只做分类迁移：`tools/git/` → `windows-x86_64/git/`，
`tools/python313/` → `windows-x86_64/python/`，`tools/curl.exe` →
`windows-x86_64/curl/curl.exe`。二进制和源码归档仍为本地发布素材，不进 Git；
文档、构建脚本、来源锁和 catalog 可跟踪。发布时应一起上传对应源码、配置和
许可证，尤其是 BusyBox 的 GPL 源码。Windows OpenSSH 的 MSYS DLL 来自匹配的
Git for Windows 安装；来源可追溯到 [Git for Windows MSYS2 packages](https://github.com/git-for-windows/MSYS2-packages)。

`catalog.json` 是原始素材清单，`deployment_base_url` 暂为空。Windows 安装器、
实际执行探测及提示词注入现已接入；发布脚本会进一步生成常用／Python XRTPACK。
Android 工具由完整版 APK 安装到系统原生库目录。Windows 常用包和 Python 包
修订 2，以及 Windows／Android 两种发行版已发布至 ai.xywhsoft.com，维护源在
home 对应 vhost。每工具 ZIP 保留为本地素材，不是客户端的安装入口。安装不修改系统 PATH，模型使用
已探测的绝对路径，且明确告知 Python 不默认带 pip／第三方包。详见
`../../docs/distribution.md`。

## Android 运行方式

BusyBox、jq 使用 NDK r30 的 Bionic 静态库构建；curl、OpenSSH 的 OpenSSL、
zlib 等第三方依赖静态纳入，系统 Bionic 动态链接，以正确使用 Android DNS。
所有工具均不依赖 Termux 目录或 glibc。构建目标 API 26，实际验证为 Android
16 / API 36；最低系统版本尚未真机验证。BusyBox/jq 的 ELF Android note
显示 API 37，这来自 NDK 静态 libc 的构建标记，不能把它当成已验证的最低
运行版本。动态 Python 启动器目标
API 26，官方 Python 库目标 API 24。原生文件的 LOAD 对齐为 16 KiB 或以上。

根据 [Android 的执行权限说明](https://developer.android.com/about/versions/10/behavior-changes-10#execute-permission)，
Android 10 起，面向较新 API 的应用无法执行自己可写数据目录里下载的原生
程序。因此正式完整版把原生程序放进 APK 的安装代码目录，数据和 CA 存放于
私有数据目录。新增三项已通过应用 UID 下的安装代码测试；ADB 隔离目录的
执行结果另行记录，不能替代应用自身权限验证。

假设解压后的 Android 目录为 `$T`，在 ADB shell 或允许执行的环境中：

```sh
"$T/busybox/busybox" sh -c 'printf "hello\n" | sed s/hello/mdo/'
"$T/curl/curl" --cacert "$T/curl/cacert.pem" https://curl.se/
"$T/jq/jq" -n '{ok:true}'
"$T/openssh/ssh" -F none user@example.com
"$T/openssh/scp" -S "$T/openssh/ssh" ./file user@example.com:/tmp/
"$T/openssh/sftp" -S "$T/openssh/ssh" user@example.com
env MDO_PYTHON_HOME="$T/python" LD_LIBRARY_PATH="$T/python/lib" \
    SSL_CERT_FILE="$T/python/cacert.pem" "$T/python/bin/python3" -c 'import ssl,sqlite3; print(ssl.OPENSSL_VERSION)'
```

SCP/SFTP 的 `-S` 必须指向实际 SSH 路径，编译时默认路径是 `/mdo-tools/bin/ssh`。
正式 mdo 集成也应设置可写的 HOME、SSH 配置/known_hosts/密钥路径，避免落到
Android 系统用户目录。curl 显式指定随包 CA，Python 用 `SSL_CERT_FILE`；不应
关闭证书验证。Android 系统库 libc、libm、libdl、liblog、libz 由系统提供，
没有将系统 Bionic 测试夹具打包。

## 重建与验证

使用 Python 3.12+，Android 构建使用 Linux/WSL、NDK `30.0.16248370`、make、
Perl、autoconf/automake/libtool 等常规构建工具。以仓库根目录为工作目录：

```sh
python tools/runtime/build/fetch_sources.py
export ANDROID_NDK_ROOT=/path/to/android-ndk-r30
bash tools/runtime/build/build_android.sh
bash tools/runtime/build/build_python.sh
python tools/runtime/build/stage_python.py
bash tools/runtime/build/stage_android.sh
python tools/runtime/build/package_tools.py
python tools/runtime/build/package_sources.py
```

新增三项的 Windows 文件和源码用 `stage_extra_windows.py` 从来源锁内的上游
素材准备，再用 `build_extra_android.sh` 构建 Android 版本。后者需要已有的
OpenSSL/zlib 构建缓存、NDK 和 Rust 1.94.0 的 `aarch64-linux-android` 目标。
设置 `MDO_TOOL_BUILD_ROOT` 指向缓存后执行；7-Zip 源码来自已校验的 Windows
准备目录。C++ 标准库静态链接，原生工具仅依赖 Android 系统库，LOAD 对齐
至少 16 KiB。Android aria2 未包含 Metalink、SFTP、SQLite Cookie 存储；
HTTPS、批量、分段、续传与校验已实测。Android rg 不含 PCRE2，跨平台提示词
默认使用基础正则或固定字符串。

`python tools/runtime/build/verify_extra.py` 验证 Windows；加 `--adb` 和
`--serial` 验证 Android 隔离目录；加 `--app-uid` 则使用已安装调试 APK 的
原生代码，在应用自身 UID 下验证 HTTPS、四连接下载、续传及 SHA-256、中文
JSON 搜索和忽略规则、7z 中文路径压缩/测试/解压。测试文件与端口会清理。

`MDO_TOOL_BUILD_ROOT` 可覆盖 Linux 构建缓存，`MDO_TOOL_DOWNLOADS` 可覆盖下载
目录（fetch_sources 同时加 `--downloads`）。来源锁固定 URL、大小和 SHA256，
下载缓存和重建前都会校验。BusyBox 配置随 Android 包提供；脚本记录 Bionic 的
strchrnul、bzero、resolver 和较新 libc 函数兼容处理。Python 启动器通过
`MDO_PYTHON_HOME` 支持 APK 原生代码与数据分开放置。

Windows 检查：`python tools/runtime/build/verify_windows.py`。
归档检查：`python tools/runtime/build/verify_packages.py`，验证每个文件和 ZIP 的
SHA256、ZIP 权限、Windows 入口的 x64 架构以及所有 Android ELF 的 ARM64
架构与 16 KiB LOAD 对齐。
手机检查需要开发机安装 Paramiko，并运行：

```sh
python tools/runtime/build/verify_phone.py --adb /path/to/adb --serial DEVICE_SERIAL
```

手机测试仅使用独立临时目录和临时 SSH 密钥，通过 ADB reverse 连接本机 SSH
测试服务，不修改 mdo 数据；测试完成撤销端口并删除测试私钥。测试覆盖中文
路径、管道、tar.gz、jq 正则/JSON、HTTPS、Python 脚本/SQLite/TLS、SSH 公钥
认证及 SCP 上传 / SFTP 下载，比较传输哈希。开发机额外检查 Git 和 Windows
客户端。结果见 `verification.json`。

对应源码、构建脚本及随包许可证归档由 `package_sources.py` 校验来源锁后生成，
发布在 `https://ai.xywhsoft.com/downloads/mdo/2026-10-07/runtime-sources.tar.gz`。
NDK、Rust 和非 GPL 预编译 SDK 仍按来源锁和脚本单独取得。
