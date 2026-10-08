# Linux / Windows 平台版本验收（2026-10-08）

提供 x86_64 Linux GUI、服务两种版本，每种包含 glibc 动态核心和 musl 静态
核心；另提供 MinGW GCC Windows GUI、服务版本。musl GUI 内嵌独立 glibc
GTK 窗口，浏览器运行库由系统提供。

## 构建与源码

- xs：`858a6ad1dde41dd43b6fd7028a8d2303ebc6f696`，提交存于 xserver
  的 `codex/linux-products` 分支；mdo 通过 deps.lock 与 Git bundle 固定此版本。
- mdo 构建基线：`03651e915be5428e5fcad5ebfec91cefd15394f2`。工作区另有已有
  编辑，构建记录明确标记 `mdo_worktree_dirty`，并记录实际打包应用的
  `packaged_app_sha256`，不将该基线描述为整个应用快照的唯一来源。
- Linux 发布构建环境：Debian 12、GCC 12、musl-tools、GTK 3 / WebKitGTK 4.1。
  glibc 核心和独立窗口所需的最高 GLIBC 符号版本均为 **2.34**。
- 两个 musl 主程序经 readelf 检查，没有 INTERP，也没有 NEEDED 动态依赖。
  两个 glibc 主程序仅直接依赖 libc、libm，没有 GTK / WebKit 图形库依赖。
- 每个产品的 build.json 记录宿主依赖、GUI helper 校验值、应用快照及程序校验值。
  发布文件及 SHA-256 清单见 [发布清单](releases/linux-products-2026-10-08.json)。

| 产品 | 程序体积 |
| --- | ---: |
| Linux glibc GUI | 3,967,465 bytes |
| Linux glibc 服务 | 3,938,560 bytes |
| Linux musl GUI | 6,079,297 bytes |
| Linux musl 服务 | 6,050,456 bytes |
| Windows GUI | 4,453,342 bytes |
| Windows 服务 | 4,259,124 bytes |

## 已通过的检查

- 四个 Linux 打包程序及 Windows 服务程序：隔离 Home 启动、TCC 编译、
  bootstrap / 写入令牌、项目接口、持久化、重启、SIGTERM / Ctrl+Break 正常停止。
  Linux 程序分别在 Debian 12 和 Ubuntu 环境覆盖启动与服务路径。
- 两个 Linux GUI：Ubuntu 的隔离 Xvfb / D-Bus 会话中显示实际 mdo 页面，
  截图人工检查；窗口缓存位于 Home 内，关闭原生窗口后服务正常退出。
- 独立 GTK 窗口：严格编译检查、分片控制帧、JavaScript 执行、一次渲染进程
  故障后的恢复、错误页面及正常关闭。没有关闭 WebKit 沙箱。
- glibc / musl 两个宿主：实际 HTTP + TCC + 模拟 Responses 模型链路中，
  取消、优先续发和被打断会话恢复通过。没有消耗线上模型额度。
- musl TCC：FILE / errno / 时钟 / 环境、pthread 与跨线程分配释放通过，
  确认脚本复用宿主已初始化的 libc。
- Windows 服务适配器：严格 C11 编译，两个受控线程验证启动 / 停止状态
  发布顺序、停止后禁止恢复 RUNNING、服务错误码传递；不安装系统服务。
- Windows GUI：PE 导入检查通过，没有硬绑定系统缺失时无法启动的 WebView2 DLL。
- 构建合同 16 项通过；Linux ELF / sysroot 合同测试 5 项在两平台互补覆盖。
- 六个发布包的校验值通过；musl 服务压缩包解压后只含程序和 mdo-home，
  预置 docs 不影响默认 Home 启动。

未进行压力或高负载测试。

## 尚未覆盖及现有边界

- 没有物理 Linux 桌面、Wayland、Linux ARM64 的实机验证。当前可下载产物为 x86_64。
- GUI 运行库仍须安装；不能把 musl GUI 当作无图形依赖的全静态浏览器。
- 当前终端未提升权限，未注册并运行真实 Windows SCM 服务；systemd 注册也未实测。
  服务模板与注册示例见 [构建运行说明](linux.md)。
- Linux 尚未接入桌面密钥环，账号可以在进程内登录，记住登录暂不可用。
  Linux 自动更新及工具包随后已补齐，验收见 [Linux 分发](linux-distribution.md)。
- 中文输入法、剪贴板和文件对话框还需要实际桌面环境交互验收。
