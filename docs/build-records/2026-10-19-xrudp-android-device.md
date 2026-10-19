# XRUDP Android 真机回归（2026-10-19）

本轮使用独立测试包，不更新手机上日常使用的 mdo，也不发布安装包或修改线上服务。
可提交的范围收据见 `tests/acceptance/xrudp-android/acceptance.json`。

## 验证对象

| 项目 | 固定值 |
| --- | --- |
| 手机 | 真我 GT7 Pro 竞速版 / RMX5090，Android 16，ARM64，4 KiB 页 |
| mdo | 已提交源码 `e6865f3c4fe889b12cad3db26a3555b32a2e4bd0`，0.2.0 |
| 产品 SDK | `a6906e202e051b6bc624f16ec131b88892963457` |
| XRUDP 运行时 | `3d55ec41e13f0884ffe840d44a57a0189d9310f3` |
| 测试包 | `org.xleaves.mdo.xrudp.congestionqa`，独立 Home，开发签名 |
| 普通 QA APK SHA-256 | `d5496bfd572c16d1344ae760b25c5f61d6e59a2c18e67a6d906b5d5d4dfb62e1` |
| APK 内原生库 SHA-256 | `559cbb723b9b5d4cdf273eef21302358c6aed1bfcd913ea63903f127e4cce11d` |

从 Git 提交归档生成应用，未带入工作区内其他任务的未提交代码。仅在隔离副本中改为本机测试账号站点、指定既有 STUN 服务，并增加原生计数和 Future 初始化检查接口。手机和 Windows 都使用实际原生端点；ADB 只承载 HTTP 控制和私有账号信令，业务数据走真实设备 UDP。

本轮没有同步其他任务新增的 XRT Executor 修改，没有用主线 XS 替换 mdo 的产品 SDK。

## 已通过

1. 独立 APK 实际加载 JNI、六扩展宿主和内置 TCC；调用了导出的 `xrtRudpOperationInit` 并核验初始化结果。Android 凭据保护实际可用。
2. ARM64 原生 Future 测试在手机上执行 IPv4/IPv6 × Select/Epoll 四项，均退出 0。覆盖字节/消息、取消、超时、额度、关闭顺序、并发取消和 Engine 停止等原测试中的断言。它以 Android shell UID 执行；不能将它描述为四项都在 APK 的 JNI 线程内执行。
3. Windows 控制手机、手机控制 Windows 两个方向，均通过直连、仅目标设备发生一次写入、实时订阅、只读拒绝、取消后的中继收据查询及注销。关闭后两端原生传输数归零。
4. 实际 UID 冻结后再回到前台，两方向均通过同一 Android 进程恢复、重新会合直连、原收据权限保留与写入不重复。冻结超过 60 秒后重新建立连接，未要求已失效的旧连接继续可用。
5. 用户解除冻结并允许后台运行后，普通 QA 包在两个方向各持续后台空闲 65 秒，随后业务读取、实时订阅及取消恢复均通过。同一 Android 进程保持运行，结束时父 UID 的 `cgroup.freeze=0`；没有添加临时电池豁免，关闭后原生传输数归零，注销和测试进程清理通过。

首轮冻结恢复尝试的正向已通过；反向在旧控制连接刚关闭时取得 409。测试端随后按已有浏览器回归的做法，在三秒内等待控制名额释放，仅重跑反向并通过。原失败收据保留，未将整次失败尝试改成成功；该调整没有修改产品协议。

## 后台超时的直接原因

第一次长后台空闲请求超时后，实测发现：

```text
/sys/fs/cgroup/apps/uid_<测试包 UID>/cgroup.freeze = 1
/sys/fs/cgroup/apps/uid_<测试包 UID>/cgroup.events: frozen 1
子进程自身 cgroup.freeze = 0，但 cgroup.events 仍为 frozen 1
```

父 UID 控制组冻结了整个测试包。ActivityManager 的进程记录仍显示前台服务、`isFrozen=false`，因此只看它不足以排除祖先控制组冻结。前台服务和 PARTIAL_WAKE_LOCK 均已实际建立；临时 Android 电池豁免也未防止该冻结。记录未确认是谁写入冻结状态，不将其归因于某个特定厂商组件或第三方工具。

用户已为测试包解除冻结并允许后台运行。该条件下两方向各 65 秒持续后台运行复验已通过，收据为 `device-background-user-unfrozen-receipt.json`。本轮未发现需要修改 XRUDP 协议或运行时的已确认缺陷；持续后台运行和冻结后的恢复分别保留独立证据。

ADB 的默认端口先前连到了 WSL 服务；本轮改用独立 Windows 端口 5038。诊断中还比较了 Windows USB 后端，采用 `ADB_USB_LEGACY=1` 后明确由 Windows NATIVE 后端连接；后台冻结在该后端仍可复现，不能归因于一次 ADB 后端切换。

## 清理和实际边界

- 普通 QA APK 已替换临时 Java 线程快照诊断包；日志属性恢复原值，测试包临时电池豁免移除，系统 USB 供电亮屏配置未改动。后台复验结束后再次核验 APK 哈希、退出、注销和清理，最终收据为 `diagnostic-cleanup-final.json`。
- 原有 `org.xleaves.mdo` 的版本和安装更新时间未变；测试不读取或修改其 Home。没有真实模型调用或线上账号写入。
- 两设备当前处于同一 Wi-Fi NAT。这不代表两个独立 NAT、公网不同网络、系统休眠或 Android Doze 已通过。手机是 4 KiB 页，16 KiB 设备兼容仍只有此前的静态证据。
- WebView 外壳实际启动，Activity 前后台生命周期实际执行；没有据此宣称触摸操作和全部视觉界面验收完成。
- 本机原 72 小时测试的状态文件停止更新、对应进程已不存在，旧计时和收据原样保留，未认定产品故障或 72 小时通过。已从独立已提交源码另起四项 Windows 原生测试和一项业务测试，使用隐藏 WMI 子进程脱离工具进程 Job，启动后核验仍在运行且不属于工具 Job。新的 72 小时从新时钟开始；服务器上既有四项当前运行时测试未重启。

私有原始证据在 `.build/xrudp-android-device-20261019-r4/`；长期测试的新控制记录在 `.build/xrudp-soak/windows-detached-resume-20261019-r1/`。本轮不宣布首次生产验收或公开发布完成。
