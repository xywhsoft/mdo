# adb 外部工具上线

已将 Android Debug Bridge 加入线上外部工具目录，逻辑 ID 为 `external-tool:mdo.adb`，SDK Platform-Tools 版本 **37.0.1**，adb 协议版本 **1.0.41**。在线外部工具列表搜索 `adb` 即可安装，无需更新客户端或安装 Android Studio。

| 客户端平台 | ZIP 字节数 | ZIP SHA-256 |
| --- | ---: | --- |
| Windows x64 | 4,404,527 | `ba9d2a6273b7901cff1ec130884bd5cfe2bc085973575dbbf9574654e92845eb` |
| Linux x64 glibc | 4,627,611 | `bf132dbd82aa6b45a135e6b1f513c23235c69e6c77f0baf0b97cdb831236ca04` |

Windows 使用 Google 官方 Win32 程序及配套 DLL，在 x64 系统上通过 WOW64 执行；没有将它声称为原生 x64 二进制。Linux 使用官方 x86_64 ELF，所需最高 GLIBC 符号版本为 2.16，另依赖系统 libgcc_s。该版本只发布到 `linux-x86_64-glibc`，musl 和 Android 客户端不会被提供不适用的安装包。

包内保留完整上游 NOTICE、source.properties 和来源记录，仅导出 `adb` 命令，不含 fastboot 或 USB 驱动。来源锁固定 Google 官方归档的 URL、版本、大小、SHA-256 和仓库 SHA-1；`tools/runtime/build/stage_adb.py` 可重建。程序和 ZIP 均为忽略的发布素材，不写入 Git。

## 验证

- 官方归档大小、仓库校验值、SHA-256 和 CRC 均通过，二进制依赖检查通过。
- Windows、WSL/Linux 和生产服务器 Linux 上的 `adb version` 实际执行通过，没有启动 adb 设备服务或操作手机。
- 通过真实 mdo 原生管理器安装 Windows ZIP，验证工具发现、派生 `adb.exe` 命令入口、中文 Home 路径及移动 Home 后继续执行。
- 发布器测试覆盖对象回收竞态、多平台共享作品、幂等、逻辑 ID 登记、禁止变更/盗用逻辑 ID；四项归档构建回归通过。
- 两个平台均通过公开 HTTPS 列表、逻辑 ID resolve、完整 ZIP 下载、字节数、SHA-256、CRC 与命令配置复核。musl 和 Android 的列表及 resolve 均正确拒绝此工具。
- Google 官方 Windows 包未进行 Windows 7 验收；没有执行设备连接、安装 APK、文件传输等真机操作。

## 生产保护

发布只新增一个作品和两个平台版本，不需要替换网站源码或重启网站。发布前备份专用扩展数据库；原有 40 个已发布入口及其版本、摘要、名称、修订保持不变。网站服务 PID、宿主程序及配置摘要不变，没有修改账号、会话或计费数据库。

同时补齐了运维发布脚本的逻辑 ID 登记，避免新工具虽然出现在列表中，却无法通过扩展依赖解析接口查找。没有对既有作品批量补写身份。

生产备份：`/home/xs/mdo-adb-20261103-2a9442f4b5/catalog-before.db`。本机原始证据：`.build/adb-20261103/`。完整发布收据、来源锁与公开下载地址见 [机器记录](2026-11-03-adb-online-tool.json)。本次验收服务已停止。

初次默认网络请求曾发生连接断开；随后直连公开 HTTPS 的完整复核通过，没有据此修改生产网络配置或认定断连根因。
