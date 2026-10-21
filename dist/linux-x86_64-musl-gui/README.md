# Linux 桌面版与服务版

mdo 共用 C Agent 核心、HTML 前端、会话格式和便携 Home。Linux 首批产物为
x86_64；在 ARM64 Linux 上运行相同脚本会生成 ARM64 产物，不等同于已通过
ARM64 设备测试。Windows 使用 MinGW GCC；musl 是 Linux 的 libc，不适用于 Windows。

## 构建

以下构建命令均从 mdo 源码根目录执行。
发布构建固定使用 Debian 10 的 glibc 2.28 和 GCC 8，避免开发机升级后悄悄
提高最低系统要求。引导脚本创建独立的 x86_64 构建根目录，校验 Debian
软件包签名及 Python 源码 SHA-256，不替换宿主系统的 libc：

```sh
sudo apt install python3 debootstrap
sudo sh tools/linux/bootstrap_baseline.sh /var/cache/mdo-linux-buster
sudo python3 tools/build_linux.py --baseline-root /var/cache/mdo-linux-buster
```

Windows 在 WSL 中准备同一个构建根目录后：

```powershell
python tools/build_linux.py --wsl --baseline-root /var/cache/mdo-linux-buster
python tools/build_mdo.py --edition server --cc gcc --output dist/windows-x86_64-server/mdo-server.exe
```

`--edition gui|server|both` 和 `--libc glibc|musl|both` 可选择产品。
默认 `--glibc-max 2.28` 会在编译前检查工具链、链接后检查 ELF 符号版本；
GUI 窗口也必须通过同一检查。开发者可显式使用 `--glibc-max native` 本机
编译调试，此类产物不具有发布版本的兼容性承诺。
`--sysroot` 可指定包含 `include/` 和 `lib/libc.a` 的目标 musl 环境。
构建脚本会核对 deps.lock，自动生成 TCC 的目标 CRT 资源，不依赖开发机上的
绝对 SDK 路径。每个版本独立存放宿主、对象、应用暂存和产物，防止混用。
官网直接提供可执行程序，下载后 `chmod +x` 即可运行。压缩包是可选分发
形式：运行 `python3 tools/package_products.py --platform linux` 生成压缩包
及 SHA-256 清单；说明、构建记录和服务模板在 `mdo-home/docs`。

默认输出：

| 目录 | 程序 | 链接方式 |
| --- | --- | --- |
| linux-x86_64-glibc-gui | mdo | glibc 2.28 动态核心、系统 GTK/WebKitGTK |
| linux-x86_64-glibc-server | mdo-server | glibc 2.28 动态核心，无图形依赖 |
| linux-x86_64-musl-gui | mdo | musl 静态核心、独立 glibc GTK 窗口 |
| linux-x86_64-musl-server | mdo-server | musl 全静态，无图形依赖 |

**musl GUI 并非全静态浏览器。** 静态核心不能加载 glibc 图形库，因此 GTK
窗口编译为独立 C 程序（glibc 2.28 基线），嵌入宿主并按内容校验后释放
到 Home 的缓存内。
窗口通过继承的私有 socket 接收导航和关闭命令；不会启动额外公开端口。
纯服务版不包含这个窗口程序，运行时也不需要 GTK、显示服务器或桌面 D-Bus。

GUI 用户须安装 GTK 3 和 WebKitGTK 4.1 或 4.0（WebKit 至少 2.30）。窗口
优先加载 4.1，缺少时加载 4.0；构建产物不直接链接 WebKit 或 libsoup，
避免在同一个进程中混用两套 ABI。Debian/Ubuntu 通常安装 `libgtk-3-0`
（新系统可能为 `libgtk-3-0t64`）和 `libwebkit2gtk-4.1-0`，旧系统可使用
`libwebkit2gtk-4.0-37`；其他发行版使用对应系统包。缺少图形依赖时保留服务并
打印浏览器地址。不要关闭 WebKit 沙箱作为常规启动方式。
X11 缺少 DRI3 时自动使用软件渲染；显卡驱动异常时也可设置
`XS_WEBVIEW_SOFTWARE=1`。中文显示需要系统中文字体（例如 `fonts-noto-cjk`）。
桌面版应使用普通桌面用户运行。
联网功能还需要维护更新的 `ca-certificates` 系统证书包。旧版发行版的历史
证书包可能含有不符合当前证书解析要求的根证书，导致信任库导入失败；
glibc 编译基线不等于证书数据库基线。发布文件不携带历史系统证书包。

## 运行与数据目录

```sh
chmod +x mdo
./mdo
# 纯服务版默认 127.0.0.1:5390，可显式设置端口。
./mdo-server --port 5390 -- --home /opt/mdo/mdo-home
```

首次按需创建程序旁的 `mdo-home`；也可使用 `MDO_HOME` 或 `-- --home PATH`。
窗口缓存位置为 `mdo-home/data/cache/webkit`。服务版可通过浏览器或现有
设备互联功能访问，支持 SIGINT/SIGTERM 优雅停止。桌面和服务进程不能同时
写入同一个 Home；数据锁会拒绝第二个进程。

服务默认只监听本机。需要远程访问时优先使用设备互联；不要直接将当前
本机网页端口映射为公开互联网服务。网站会员登录不替代本机控制接口的认证。

## 系统服务

Linux 示例见 `tools/service/mdo.service`。部署到用户选定的路径后修改
`ExecStart`、`WorkingDirectory` 和 `User`，再由用户显式注册到 systemd。

Windows 服务版支持 `--service`，例如在管理员终端：

```powershell
sc.exe create mdo binPath= '"D:\mdo\mdo-server.exe" --service --port 5390 -- --home "D:\mdo\mdo-home"' start= auto
sc.exe start mdo
sc.exe stop mdo
```

程序不会自动安装服务。建议设置专用运行账号及 Home 权限。

## 兼容性与已知边界

glibc 发布基线固定为 2.28，最低符号版本写入构建记录并由构建脚本强制
检查。兼容性方向是旧基线到新系统；glibc 更旧的机器应选择 musl 服务版。
musl 静态服务核心没有 ELF 解释器或动态依赖，仍依赖 Linux 内核及需要执行
的外部工具。GUI 的独立窗口依赖其目标系统的 glibc 和 WebKitGTK。

Linux 当前没有接入桌面密钥环，账号可在进程内登录使用，“记住登录”由
现有能力检测禁用，不会把凭据改存为明文。Linux x86_64 的扩展工具包和
四种产品的在线更新已接入；构建、发布及安装细节见 [Linux 分发](linux-distribution.md)。
发行版、X11/Wayland、中文输入法、剪贴板和文件对话框须以实际测试记录为准。
