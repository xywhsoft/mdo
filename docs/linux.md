# Linux 桌面版与服务版

mdo 共用 C Agent 核心、HTML 前端、会话格式和便携 Home。Linux 首批产物为
x86_64；在 ARM64 Linux 上运行相同脚本会生成 ARM64 产物，不等同于已通过
ARM64 设备测试。Windows 使用 MinGW GCC；musl 是 Linux 的 libc，不适用于 Windows。

## 构建

Linux 安装 Python 3、Git、GCC、binutils、musl-tools、pkg-config。GUI 构建还需要
GTK 3 和 WebKitGTK 4.1 开发包。例如 Debian：

```sh
sudo apt install python3 git gcc binutils musl-tools linux-libc-dev pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev libx11-dev
python3 tools/build_linux.py
```

Windows 已安装 WSL 和上述 Linux 工具链时：

```powershell
python tools/build_linux.py --wsl
python tools/build_mdo.py --edition server --cc gcc --output dist/windows-x86_64-server/mdo-server.exe
```

`--edition gui|server|both` 和 `--libc glibc|musl|both` 可选择产品。
`--sysroot` 可指定包含 `include/` 和 `lib/libc.a` 的目标 musl 环境。
构建脚本会核对 deps.lock，自动生成 TCC 的目标 CRT 资源，不依赖开发机上的
绝对 SDK 路径。每个版本独立存放宿主、对象、应用暂存和产物，防止混用。

默认输出：

| 目录 | 程序 | 链接方式 |
| --- | --- | --- |
| linux-x86_64-glibc-gui | mdo | glibc 动态核心、系统 GTK/WebKitGTK |
| linux-x86_64-glibc-server | mdo-server | glibc 动态核心，无图形依赖 |
| linux-x86_64-musl-gui | mdo | musl 静态核心、独立 glibc GTK 窗口 |
| linux-x86_64-musl-server | mdo-server | musl 全静态，无图形依赖 |

**musl GUI 并非全静态浏览器。** 静态核心不能加载 glibc 图形库，因此 GTK
窗口编译为独立 C 程序，嵌入宿主并按内容校验后释放到 Home 的缓存内。
窗口通过继承的私有 socket 接收导航和关闭命令；不会启动额外公开端口。
纯服务版不包含这个窗口程序，运行时也不需要 GTK、显示服务器或桌面 D-Bus。

GUI 用户须安装运行库：Debian/Ubuntu 为 `libgtk-3-0`、`libwebkit2gtk-4.1-0`；
Fedora 为 `gtk3`、`webkit2gtk4.1`。缺少显示环境或图形依赖时会保留服务并
打印浏览器地址。不要关闭 WebKit 沙箱作为常规启动方式。
X11 缺少 DRI3 时自动使用软件渲染；显卡驱动异常时也可设置
`XS_WEBVIEW_SOFTWARE=1`。中文显示需要系统中文字体（例如 `fonts-noto-cjk`）。

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

glibc 产物应在支持范围内最老的构建环境编译，再在新发行版上验证。构建
主机的 glibc 最低依赖通过 ELF 检查记录；同名的 gcc 不保证跨发行版兼容。
musl 静态服务核心没有 ELF 解释器或动态依赖，仍依赖 Linux 内核及需要执行
的外部工具。GUI 的独立窗口依赖其目标系统的 glibc 和 WebKitGTK。

Linux 当前没有接入桌面密钥环，账号可在进程内登录使用，“记住登录”由
现有能力检测禁用，不会把凭据改存为明文。Linux 自动更新与预置工具包
需要对应平台的发布目录；缺少目录时可使用系统 PATH 工具并手动升级。
发行版、X11/Wayland、中文输入法、剪贴板和文件对话框须以实际测试记录为准。
