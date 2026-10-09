# Linux 工具包与在线更新

Linux x86_64 的 glibc、musl 两种 GUI／服务版共享 `linux-x86_64` 工具包。
在设置的应用更新入口安装常用工具包和可选 Python，无需 root，不修改系统
PATH、软件包或共享库。Android 工具仍随完整版 APK 更新。

常用包提供 BusyBox、curl、jq、SSH/SCP/SFTP、aria2c、rg、7-Zip；Python
单独安装。各入口通过私有 musl 加载器运行所带的依赖库，证书路径也指向包内。
安装位置是 `mdo-home/data/toolpacks`，每次安装建立新代，校验整包及每个文件
SHA-256、设置明确的 0644/0755 权限并试运行，成功后才激活。现有卸载、修复、
回退和旧代清理入口继续适用。服务版可通过浏览器进入相同设置页面。

## 可复现构建

```sh
python3 tools/runtime/build/stage_linux.py
python3 tools/build_linux_toolpacks.py --packer .build/host/linux-server-glibc/xs
```

普通构建只使用 `tools/runtime/build/linux.lock.json` 中固定的 Alpine 3.23
包及 SHA-256；显式 `--refresh-lock` 才解析新的仓库快照。原始 APK、运行文件、
发布包是本地构建材料，不进入 Git。锁文件保留许可证、包来源、aports 源码提交，
安装包内的 `provenance.json` 保留同一份记录。工具包无需与主程序使用同一种 libc。

## 程序更新

程序使用下面四个独立身份查询官网；GUI 的 edition 为 `desktop`，服务版为 `server`：

| 平台 | edition | 构建编号字段 |
| --- | --- | --- |
| linux-x86_64-glibc | desktop | linux_glibc_gui_build_id |
| linux-x86_64-glibc | server | linux_glibc_server_build_id |
| linux-x86_64-musl | desktop | linux_musl_gui_build_id |
| linux-x86_64-musl | server | linux_musl_server_build_id |

更新清单必须匹配平台、edition 和更高的构建编号。下载到
`mdo-home/data/update/new.bin`，确认安装后等待任务空闲，校验 SHA-256、打包尾标，
在程序同目录准备新文件并试运行 `--version`。上一版保存为
`mdo-home/data/update/previous.bin`；最后原子替换程序并同步目录。

xs 的 `xsAppRequestRestart()` 在关闭窗口、排空任务及释放 Home 后执行新程序，
保留原 PID、参数和环境。systemd 的 MainPID 不变，无需 sudo 或额外启动服务。
程序所在目录和 Home 必须由当前服务账号可写；只读安装目录需要管理员手动更新。
替换失败时保留旧程序和已下载的包；替换成功但尚未请求重启时出现错误，会尝试
恢复旧程序。新版本的应用启动错误不做自动回退，可停止服务后从 `previous.bin`
恢复。更新不会迁移、删除或回退会话数据。

网站继续使用现有统一 mdo 插件。Windows 与 Linux 的工具包 ID 都可为 `core`
或 `python`，修订号、依赖和回退记录按平台分别验证，发布 Linux 包不覆盖 Windows。
四个 Linux 程序各有独立上传卡片，公开更新接口继续使用 `/update/version`。

## 验证

`tests/manual_linux_distribution_qa.py` 使用临时目录和本地发布服务验证两类工具包、
十项运行探测、权限、损坏更新、错误 edition、原子替换、保留旧程序及 PID 不变的
重启。网站的 `tests/test_mdo_linux_delivery.py` 验证 ELF 发布、版本隔离、不同平台
同名工具包、修订号回退和跨平台依赖拒绝。仅执行有界功能测试。

`tests/manual_linux_online_qa.py` 启动实际打包的 glibc／musl 服务版，使用隔离 Home
查询公网更新和工具包目录，确认平台、edition、线上 SHA-256 与当前程序一致。
不登录、不调用模型、不安装工具包，也不改动网站数据。

2026-10-08 已发布四种 Linux 程序（构建编号 30000037～30000040）及常用／Python
工具包（revision 2）。六个文件均通过公网 HTTPS 完整下载校验，实际服务版的线上
更新检查通过；两种 libc 的工具安装、程序替换和重启通过，两个 GUI 的虚拟桌面
启动及窗口关闭通过。发布清单和验收记录见
[`releases/linux-distribution-2026-10-08.json`](releases/linux-distribution-2026-10-08.json)。
本轮仅覆盖 x86_64；尚未实测 ARM64、物理 Wayland 桌面或真实 systemd 单元。
musl GUI 的程序核心为静态链接，窗口仍通过系统 GTK3／WebKitGTK 运行。

2026-10-09 已更新四种程序（30000043～30000046），固定 glibc 2.28 编译基线，
并支持 WebKitGTK 4.1／4.0 自动选择。官网直接提供可执行文件，用户选择
glibc 或 musl 后下载运行；应用内更新按相同身份发布。两个版本的旧、新
运行环境均通过启动、持久化、工具包安装、原子更新、原 PID 重启及公网
接口检查；两套 WebKit ABI 均通过原生窗口验证。旧环境使用更新的系统 CA
证书包，内核为 WSL，不能据此推断旧内核或所有发行版均已实测。
验收记录见 [`releases/linux-baseline-2026-10-09.json`](releases/linux-baseline-2026-10-09.json)。
