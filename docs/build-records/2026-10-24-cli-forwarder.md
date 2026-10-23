# 外部工具统一命令入口

2026-10-24，工作区 `D:\GIT\mdo`。在 master 完成开发和提交，生成本地测试构建。

## 实现

- Windows 使用锁定 SDK 内的 xrt 编译通用转发器，317,952 字节，约 311 KiB。按自身文件名读取 Home 相对的命令映射；Windows 桌面版与服务版均将它放入 VFS。优先硬链接生成命令入口，不支持时复制。
- Linux 使用最后 `exec` 实际入口的脚本，保留工具原有 `$0`、运行库定位、参数、管道、工作目录和退出状态。没有增加解释 JSON 所需的外部依赖。
- 工具登记和 Windows 转发索引共用同一个原子写入的 `data/cli-tools/state.json`。Linux 包装脚本逐项原子更新，登记写入失败时恢复旧脚本；多条 Linux 命令的集合切换不承诺同时发生。
- 支持安装、更新、启停、卸载、启动修复、别名、固定前置参数和运行中更新。无法立即删除的入口记录为 `retired_commands`，后续保存或重启再清理。未知文件不覆盖，冲突仍可通过管理页停用或改名解决。
- mdo 的 PATH 只修改本进程并由子进程继承，不改系统 PATH。SDK `process.run`、Agent `exec/spawn` 以及后续子命令都可使用统一目录。
- 托管路径持久化为 Home 相对路径。Home 外的手动工具保留原位置；“复制到 mdo 工具目录”可显式复制程序及选定依赖目录，原文件保留。分享和复制保留命令声明与别名。
- Windows 转发器保留空参数、Unicode、引号和反斜杠、二进制标准流、原退出码，支持 Ctrl+C、异常退出后的进程树清理。正常完成时保留目标程序主动启动的后台进程，避免破坏 ssh-agent 的语义。构建去掉 PE 时间戳，可重复生成相同字节。
- 旧归档补齐 OpenSSH 附属命令、Python 别名及常用 BusyBox applet；新归档显式携带多命令清单，不扫描系统 PATH、不运行工具探测。
- Android 保留 lite/full 分发。full 由 PackageManager 安装原生程序并建立私有运行树；补齐 aria2、ripgrep、7-Zip 的 PATH 目录，不下载执行文件，不包含 Windows 转发器。

## 提交

- `9e05214d`：Windows xrt 转发器、构建器和原生测试。
- `c07b9906`：工具生命周期、统一命令目录、VFS、相对登记、导入与界面集成。
- `bf87a2f6`：Linux 启动时发生入口冲突仍可进行管理，恢复时保留用户替换的文件。
- `c31beb93`：正常退出保留主动后台进程，同时保留异常终止清理。
- `c3de0d82`：旧归档完整 SSH 附属入口及 BusyBox 常用入口兼容。

## 验证与证据

- `tests/test_cli_forwarder_windows.py`：不同输出目录的确定性构建；空参数、中文、引号、反斜杠、换行、硬链接分发、前置参数、二进制标准流、退出码；缺失、越界、递归和重复 JSON 拒绝；Ctrl+C、强制终止清理、主动后台进程、Home 迁移。全部通过。
- `tests/test_cli_bindings_runtime.py`：Windows 原生及实际打包程序、Linux glibc/musl 原生测试通过。真实 SDK 和 Agent exec/spawn 调用；运行中更新；损坏命令清单拒绝；命令冲突和用户文件保护；保存失败后的旧命令恢复；锁定入口延迟清理；自定义工具复制；脚本自身目录；旧归档入口与 BusyBox 前置参数；Home 迁移。
- `tests/test_cli_tools_runtime.py`：工具下载、取消、重试、校验失败回退、启停、重启、提示词、快照隔离、损坏登记保护通过。
- `tests/test_passive_home_runtime.py` 四项与 `tests/test_home_runtime.py` 通过，保留延迟创建 Home 和只读启动行为。
- 前端 `tests/test_external_tools.mjs` 四项通过；Android 命令运行时打包单元测试两项通过；Android 安装器 19 项策略和锁定 SDK 集成检查通过。Linux 构建单元检查 7 项通过、4 项按环境条件跳过。
- `tests/test_cli_product_packaging.py` 对八个实际产物解包核对。Windows VFS 内含同一新版转发器，Linux/Android 均不携带；核对最新登记、包装与归档源文件；Android lite 仅包含宿主及命令 worker，full 同时校验预制工具映射、ELF 依赖、16 KiB 对齐和文件摘要。
- 产物与最终摘要：`.build/releases/cli-forwarder-20261024/validation.json`；原生转发器报告：`.build/cli-forwarder-development/windows-regression.json`。

## 范围与限制

Android APK 使用现有本地开发签名，本轮完成编译和离线检查，没有真机回归。构建产物在 `.build/releases/cli-forwarder-20261024/`，没有执行官网或自动更新发布。

工作区还有并行任务的终端功能修改，本轮未提交这些文件。`test_build_contract.py` 的 16 项检查中，15 项通过；一项固定源清单预期尚未包含该任务新增的 `src/application/terminals.c`，因此失败，日志为 `.build/cli-forwarder-build-contract.log`。本轮产物包含构建时的当前工作区，不能视为已通过全仓库发布门禁。

首次 Linux 全量构建完成了四个产物，但退出时构建缓存绑定挂载点忙，未强制卸载；之后以经过验证的缓存重新打包完成。Windows 服务宿主首次编译因本机虚拟内存紧张失败，重试后成功。日志分别保存在 `.build/cli-forwarder-linux-repack.log` 和 `.build/cli-forwarder-windows-server-build.log`。
