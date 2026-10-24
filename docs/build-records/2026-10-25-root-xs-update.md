# 2026-10-25 根目录 xs 开发宿主更新

根目录 `xs.exe`、`xsw.exe` 从 2026-10-06 的 `b5ab39c` 更新为 xs 最新已提交源码
`771574b142daaffab0ce330e231cd90dcac1e231` 构建的 Windows x64 全扩展版。
对应 xrt 快照为 `803501226fd91ccbcbd39d2b52faccf593146a68`，版本横幅仍为 XServer 1.0.0，
构建日期为 2026-10-25。没有沿用 xs release 目录中更早的程序或带 dirty 标记的产物。

构建命令（在 `D:/GIT/xserver` 执行）：

```powershell
python tools/build.py all --build-dir D:/GIT/xserver/.build/mdo25 --output D:/GIT/mdo/.build/xs-root-update-20261025/xs.exe
```

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| xs.exe | 9,047,552 | 901f4e81681cfd0755cadc700feb61658c36774f27caa75431a7bf06c0c650e0 |
| xsw.exe | 9,047,552 | 02f2b1cb42d22a46067bfb09e9fa1f68eed7b06d1eb944a5cce2edfeabe5a61b |

验证包括源码与二进制收据、两个实际版本横幅、PE x64/控制台与 GUI 子系统、
25 个扩展及 2863 个公共 SDK 导出的外层和嵌套 TCC 编译运行、XRUDP IPv4/IPv6
本机握手与传输。控制台版和 GUI 版站点 VFS 打包、列举、提取、剥离、HTTP 服务及
磁盘覆盖各 32 项检查通过。

GUI 版按 xs 既有实现把输出写入程序旁的 `xsw.log`。原控制台 VFS 测试最初在
读取 `pack --list` 标准输出的断言上失败（31/32），改用逐命令新增日志字节接收输出后，
保持原断言重新执行，32/32 通过。构建与测试日志保留于 `.build/xs-root-update-20261025/`。

覆盖前后均校验哈希，旧程序备份于 `.build/xs-root-update-20261025/backup/`。
两份程序已由仓库跟踪，本次连同构建记录提交。此次范围为根目录通用 xs 开发宿主；
mdo 产品构建继续使用 `deps.lock` 中锁定的兼容 SDK，未执行应用依赖迁移或线上发布。
