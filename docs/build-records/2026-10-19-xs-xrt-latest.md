# XS 锁定当前 XRT 提交

按用户要求，XS 主仓和 mdo 实际使用的产品 SDK 均锁定到 XRT 主分支当前已提交版本 `13cc17b6d7aaff8a5fc927929b89a77e6cc151f5`。使用提交导出，不包含另一个任务尚未提交的文件系统和内存修改。

- XS 主仓提交：`e5c2ab2`。快照来源、扩展来源和脚本导出来源同步更新。
- 产品 SDK 提交：`7edc86699114ce1607f84bfa1f51eaf21fc0aeaa`，保留原有 xllm、session、xwork API 和产品修复。
- mdo `deps.lock`、`tools/dependencies/xs-xrt-current.bundle` 同步更新。bundle 为 4,417,210 字节，SHA256 `61af7d85a6fe59467621a872b9fda7ca56271494032a7f6655781a46d2e1bc5f`，引用 `refs/heads/master`，公共历史前置 `695988f7b8eec4b52bcc829a2d52210443c0bc69`。

时间、等待和原生 HTTP 的兼容性迁移已在此前完成，详见 `2026-10-18-xs-xrt-upgrade.md`。本次最新提交相较 `7c82e311` 仅新增独立协议测试和文档；核心单头、声明和全部 46 个 xrudp 快照文件逐字节验证一致。没有重新引入旧 XRT 实现或改变持久化时间、公共 ABI。

本轮检查：XS 快照同步、来源固定和扩展配置 44 项通过；mdo 依赖锁定、换行独立哈希和 image 原生锁定 3 项通过。bundle 引用、前置和全部产品依赖校验通过。证据分别为 XS `.build/xrt-latest-snapshot-tests.log`，mdo `.build/xrt-latest-dependency-tests.log`、`.build/xrt-latest-lock.log`、`.build/latest-xrt-byte-verification.json`。

Linux 新宿主已从产品 SDK 新提交构建（本机 Clang/native ABI，非发行版 ABI 验收）。时间、等待、进程、60 ms 定时器、持久化日历和根内禁止覆盖移动通过；六个扩展的 562 个导出及 IPv4/IPv6、固定身份 Noise、可靠字节/FIN、取消、关闭句柄均通过外层/嵌套 TCC。mdo 十二组实际会合场景通过。证据为产品 SDK `.build/xrt-latest-linux-{build,runtime,sdk}.log` 和 mdo `.build/xrt-latest-linux-rendezvous.log`。

Windows 新宿主及 GUI 子系统变体也已成功重建，实际构建版本为产品 SDK `7edc866`。同一组时间/等待/进程/定时器/根内移动回归、六个扩展的 562 个外层/嵌套导出及原生 IPv4/IPv6、十二组 mdo 会合场景均通过。证据为产品 SDK `.build/xrt-latest-windows-{runtime,sdk}.log`、`xrt-latest-windows-build-recovered.log` 和 mdo `.build/xrt-latest-windows-rendezvous.log`。此前内存压力导致的失败日志保留；内存恢复后重新构建通过，未通过修改或降级运行时代码规避问题。

新宿主位于 `.build/xrt-latest-host/xs.exe` 和 `.build/xrt-latest-linux/xs`，收据分别与真实二进制 SHA256 核对。Android 旧构建的运行时代码与本次一致，但其旧提交收据不能充当本次新提交的构建收据。未发布或替换已安装程序；正在运行的隔离长稳也继续使用其原来记录的二进制，不在运行中偷换版本。
