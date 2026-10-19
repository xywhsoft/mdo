# xrudp 持续拥塞与当前 SDK 验证

会话恢复原先没有调用拥塞组件已有的最小窗口收缩入口。公开 Session 的真实加密 ACK 回归确认：实测 RTT 后，已声明网络丢失包的发送区间超过三倍基础 PTO，原实现只将窗口从 12000 减半到 6000；本次完成区间证明后收缩到 2400。

新证据环按实际发送顺序、有界保留元数据，计入会话/端点峰值预算。发送时必须已有 RTT 样本，区间严格超过三倍无退避基础 PTO，任意中间确认均切断证据；旧包迟到 ACK、纯 ACK、MTU、本地背压/丢弃及路径切换均有针对性边界。单次 PTO 不收缩，所有权、在途字节和已交付数据不改写。SessionState.PersistentCongestion 统计证明完成的区间；字段及实现必须成套更新，不能用旧 SDK 声明配新库。

判定原则参照 [RFC 9002 第 7.6 节](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.6)，线路仍是 XRUDP001；未引入新依赖或 QUIC 兼容层。

| 项目 | 提交/验证 |
| --- | --- |
| XRT 运行时 | `3d55ec41e13f0884ffe840d44a57a0189d9310f3` |
| XS 主仓快照 | `6bc2fd8`，848 个上游文件及 141 个生成文件哈希通过，44 项相关单元检查通过 |
| mdo 兼容 SDK | `a6906e202e051b6bc624f16ec131b88892963457`，46 项单元检查通过，含 1 项平台跳过 |
| mdo bundle | 4,435,555 字节，SHA256 `9014d41448b1a28bb5691699b3f5186e55156d104eb41c733a43a7231b246a64` |
| bundle 引用/前置 | `refs/heads/master` / `695988f7b8eec4b52bcc829a2d52210443c0bc69` |

产品 SDK 的 48 个 xrudp 文件及两个核心单头，与上述 XRT 提交逐字节一致。核心单头与之前相同；保留 xllm 3.1、session 3.0-dev、xwork 3.11/ABI 8、xs-image 及 webview 的完整原有指纹和其他扩展注册项。完整依赖、换行无关哈希和 image 原生锁定三项回归通过。

固定提交验证共 136 个进程：

- 持续拥塞组件 4 个进程：Windows GCC/Linux Clang ASan/UBSan，各模块化/单头。包含 24 组公开 Session 正反场景、28,000 次固定环退休、迟到 ACK/严格阈值/待完成元数据/阈值缩短/饱和向量，以及原有双向故障和事务回归。
- 状态模型 64 个进程：两平台各 32 个种子，共 1,280,000 个虚拟探索步，计数完全一致，终结分配数/字节平衡。
- 原生 Future 68 个进程：两平台、模块化/单头、IPv4/IPv6、Select/IOCP/Epoll，包含真实跨线程操作、关闭后结果寿命、分配失败和禁用 Future 模块。所有 Engine 正常 join。

全部实际来源及二进制哈希复核通过。证据为 XRT `out/xrudp-wire-interop/congestion-acceptance.json` 和 `dev/xrudp-persistent-fixed-*`。这些结果不能当作 72 小时长稳；此前 2000 个负向/路径/MTU 独立线路进程属于旧 `b093114d`，保留为明确版本的基线，不能改标为本次新运行时。

新产品宿主从干净的 SDK 提交重新构建，profile 为 `893b7863bf7768b125dfa9ca8af27550f8fdddccca70070fcfc5d8aac4a5bc04`：

| 文件 | 字节 | SHA256 |
| --- | ---: | --- |
| `.build/xrudp-congestion-host/xs.exe` | 3,713,024 | `f27a8e680ea032739194d7eb52a82bac171878c7e467a3bc0122eb3dc46464b2` |
| `.build/xrudp-congestion-host/xsw.exe` | 3,713,024 | `752dd3c0de7d42b1b7603f47ea15c1786e121c0a38985917b5e9688910a06719` |
| `.build/xrudp-congestion-linux/xs` | 3,955,264 | `9511100a676e0b29a76a71a20d5d8926832b39c0e5438f8a0d8d90ff7772c326` |

Windows、Linux 均通过时间/条件等待/进程超时/定时器/持久化/根内移动回归、六个扩展 565 个外层与嵌套 TCC 导出，以及各 12 组真实会合/NAT/双族/黑洞场景。Windows 全应用进一步通过真实私有账号、IPv6 黑洞后 IPv4 接管、HTTP/live、一次性授权、只读、取消后的回执、中继兜底、退出及恢复。全应用来源包含原工作区中其他任务的未提交改动，未将其伪称为干净的 mdo 提交。Linux 使用本机 Clang/native ABI，不能代替发行基线 ABI 验收。

日志：产品 SDK `.build/xrudp-congestion-{windows,linux}-{build,runtime,sdk}.log`；mdo `.build/xrudp-congestion-{windows,linux}-rendezvous.log`、`xrudp-congestion-windows-remote.log`、`xrudp-congestion-dependencies.log`、`xrudp-congestion-sdk-sync.json` 和 `xrudp-congestion-lock.json`。

原 18 组长稳均保留原二进制及计时，不计为本次运行时的最终验收。用户回复稍后连接 Android，当前已构建的 Android QA 仍属旧 SDK，须刷新后再进行最终真机验收。本次没有发布、替换安装程序或重启生产服务。

## 当前版本后续验收

XRT 提交 `ed000181` 新增真实原生端点的保留句柄内存限额回归：Windows GCC、Linux Clang ASan/UBSan/leaks，IPv4/IPv6，Select 与显式 IOCP/Epoll，模块化全功能和单头 endpoint-only，共 16 个进程、1,024 轮拒绝/释放后恢复、3,072 次压力连接关闭、32 次 Engine join。先开发验证，再用已提交源码重建复验，源文件和二进制哈希均复核。没有复现新的运行时缺陷，运行时代码仍为 `3d55ec41`。收据 `D:/GIT/xrt/out/xrudp-endpoint-quota/acceptance.json`。

XS 源码 `6bc2fd8` 的全新 xrudp-only Windows/Linux 宿主均通过实际 `class: xrudp` 字节流、消息、游戏数据报、身份/上下文、重入取消、保留连接重载、失败回滚、禁用及退出。记录提交 `0014ed9`；SDK 产品分支 `a6906e2` 不变。这补齐本次运行时的 XS 服务类型验证。

mdo 提交 `13661af2` 修正长跑测试夹具：启动前核对实际宿主和当前锁定 SDK，私有副本内重新生成 unity/依赖标记，确认根应用未改动，避免沿用历史缓存。25 秒短测完成真实 HTTP/live、大文件上传下载、两次重连和退出，随后单独启动正常 72 小时用例，不加速令牌过期。

本次 `3d55ec41` 已另外冻结源码、核心和驱动，启动 4 组 Windows、4 组 Linux 原生 Future 多流/消息/游戏用例及 1 组当前 SDK 的 mdo 业务长跑。8 组原生用例均先完成短测和退出后的 Future 资源归零校验。固定构建、进程、原始开始时刻、远端单元身份及哈希保存在 `D:/GIT/xrt/out/xrudp-native-soak/congestion-long-runs.json`。9 组当前都只记作运行中，尚未达到 72 小时；原 18 组仅作为历史基线保留，不能替代当前版本的验收。

仍需当前运行时独立线路完整矩阵、最新宿主的公网双机复验、最终长稳、刷新后的实体 Android、双独立 NAT 和接入限速表压力审查。线上 `xs.service` PID `3030721` 保持运行，本轮未改动生产服务。

## 独立线路矩阵与新版 Android QA

当前运行时的独立线路完整矩阵已完成。基于 XRT `9f449361` 的测试/文档快照重新构建，逐个确认生产 C/头文件与 `3d55ec41` 相同：Windows GCC Select/IOCP、Linux Clang ASan/UBSan/leaks Select/Epoll，IPv4/IPv6 和双 Noise 角色，共 2,080 个实际原生进程，全部正常 join。覆盖默认流/五流消息游戏 32 个、STOP/RESET/双方发起关闭 48 个、负向与合法乱序 1,920 个、地址重绑定 16 个、MTU 增长黑洞 32 个、缩小恢复 16 个、迁移后尺寸重建 16 个；执行 3,520 份未认证变异。笛卡尔积、驱动、来源及二进制哈希均重新核对。

证据：`D:/GIT/xrt/out/xrudp-wire-interop/congestion-wire-acceptance.json`；Windows 二进制 SHA256 `def882a8a05c70946a3ab7301217af733c59e33a864e2538cd23bf932b1765e2`，Linux `d7ec8aa68e1dbb3dd3b2600db92841ce6d478e50b33d575ba2110043e8b02973`。154 个来源/驱动的不可变归档为 `frozen-3d55-congestion-wire-source.tar.gz`，SHA256 `de66a735cbc4711d7b77ba7416e8f95c0037b44a63217c360281a635085558ee`；旧运行时证据保留。本轮未修改产品运行时代码。

Android 用产品 SDK `a6906e2` 重建 ARM64/API26 原生库，并在私有应用副本内生成当前依赖标记，原工作区应用树哈希保持 `0a5f9b305c4adf916a1948ed835df80baab5b4eb279468e0d541061243071e38`。新测试包目录 `.build/xrudp-android-congestion`，独立包名 `org.xleaves.mdo.xrudp.congestionqa`、独立 Home 和开发签名；没有覆盖已有应用。

| 产物 | 字节 | SHA256 |
| --- | ---: | --- |
| `native/libxs.so` | 3,479,576 | `559cbb723b9b5d4cdf273eef21302358c6aed1bfcd913ea63903f127e4cce11d` |
| `app.xrtpack` | 1,453,356 | `8194f3e2db4de6f46ab3f20ed65dea538625713e4be4bfdddd7ed2b475348d3f` |
| `mdo-xrudp-congestion-qa.apk` | 4,993,702 | `8ea1c35806b929f30ea119389a5e027c028ddb0e1f79b45a09fe2c35b7a76565` |

已核对实际 APK 内的原生库及 pack 与构建产物相同、ELF ARM64/API 架构、LOAD 段及未压缩库的 16 KiB 对齐、v2/v3 签名、SDK 收据和原生宿主导出注册表。导出注册表检查不等于在 Android 执行了 Native/Future；这些入口不是可用 `dlsym` 证明执行的 ELF 导出。正式真机用例仍待完成。

Windows ADB 从 SDK 锁定的官方 platform-tools 37.0.1 包准备，按已锁定 SHA1/大小验证并记录 SHA256；通过原生 Windows USB 主机执行 `devices -l`，当前没有设备，未用 WSL 设备列表替代 Windows 检查。用户已回复稍后连接，未重复催问、安装或发布。收据 `.build/xrudp-android-congestion/validation.json`、`.build/android-platform-tools/verification.json`。

剩余门槛为最新产品宿主的公网复验、完整长稳、Android 真机、双独立 NAT 和接入压力完整审查。当前版本 9 组长跑与历史 18 组基线继续保持各自计时。
