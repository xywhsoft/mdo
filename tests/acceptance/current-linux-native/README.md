# 当前 Linux 原生窗口的生命周期

2026-10-11。从锁定 xs `f05f766` 源码重新构建 x86_64 glibc 和 musl GUI
宿主，再打包当前应用。431 个应用输入与 Windows、Android 集成候选一致，
摘要为 `82cb0b6354b30226a4d69dead949fe36df061ee86ed782d1e3c34d3b0a8945e2`。
包含工作区未提交源码，来源见 [source.json](source.json)。正式构建退出码 0，
Windows 日志重定向正常完成，没有强制卸载临时挂载。

## 实际原生进程

使用 [有界生命周期入口](../../manual_linux_native_lifecycle_qa.py)，在 WSL
Ubuntu 26.04.1、GTK 3.24.52、WebKitGTK 4.1 中以普通用户 `nobody` 运行。
DBus 和 Xvfb 提供独立显示环境；没有关闭 WebKit 沙箱。每个版本使用自己的
临时目录和便携 Home，不读取个人数据，也不发送模型请求。

两版分别验证：原生宿主/TCC 初始化就绪；实际 `xs-window` 和
`WebKitWebProcess` 子进程存在；启动后保持 12 秒；仅终止一个所属渲染进程，
随后创建新的渲染进程，服务仍就绪；正常 SIGTERM 停止返回 0；已跟踪的所属
子进程均退出；使用同一 Home 重启，复用摘要一致的便携窗口助手并正常停止。
信号操作同时核对 PID 与启动时间，不向不属于夹具的进程发信号。

原始结果见 [glibc](glibc-lifecycle.json)、[musl](musl-lifecycle.json)，
实际宿主日志见 [glibc](glibc-native.log)、[musl](musl-native.log)。
记录入口会在离开 WSL 前复制日志，避免 WSL 重启清理 `/tmp` 后丢失证据。
使用没有真实用户目录的 `nobody` 会产生 IBus/dconf 的 `/nonexistent` 目录
警告；Xvfb 无 DRI3 会产生 EGL 警告。这些环境警告仍保留，没有记为无警告运行。

## 字节和 ABI

| 产物 | 字节 | SHA-256 |
| --- | ---: | --- |
| glibc GUI | 4,120,684 | `36b1698cd401751484cfc861e21ec3b469939d58f4282c3715cc7ed33cb19443` |
| musl GUI | 5,958,243 | `a1fa06192864650cc73595c8b36077d7668eadd6527ac58fb1eeb8fa1caf2a58` |

实际打包文件与回执相符，宿主前缀也单独核对。glibc 核心最高导入版本 2.28；
musl 核心没有动态依赖或解释器。两版使用同一独立 glibc 窗口助手，摘要
`802655150778c17653805750d22ffcf1633b357f7f78e7b8442eb261d815380a`，
实际运行时释放的文件摘要一致。助手最高导入 glibc 2.2.5，通过 GTK 动态依赖
及运行时加载 WebKit；musl GUI 仍需要系统 GTK/WebKit，不能称为完全静态界面。
详细 ABI 见 [products.json](products.json)。

## 范围边界

这是原生进程和服务生命周期检查，没有模拟点击、复制、文件选择或读取屏幕。
新渲染进程与服务就绪不等于已验证页面像素或全部控件；SIGTERM 不等于点击
窗口关闭按钮。Linux 系统剪贴板、保存弹窗和窗口关闭交互仍需实际验收。
没有压力或高负载测试，没有更换日用产物或发布官网。
