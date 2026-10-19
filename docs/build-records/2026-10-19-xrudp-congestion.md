# xrudp 持续拥塞与当前 SDK 验证

## 真实进程暂停恢复与浏览器自动回退

`tests/test_remote_process_pause.py` 在独立测试子进程中使用 Windows NtSuspendProcess/NtResumeProcess 或 Linux SIGSTOP/SIGCONT（pidfd 绑定进程），保留网站夹具运行。只操作本夹具创建的进程句柄，不接受外部 PID；finally 中恢复后清理。暂停时实际 HTTP 无响应，恢复后验证原生业务。运行时仍为 `3d55ec41`，产品 SDK `a6906e2`，应用来自干净的已提交副本 `2da7ad59`；没有缩短任何产品期限。

Windows/Linux × IPv4/IPv6 × 3 秒/65 秒八个逻辑组合全部通过。3 秒场景恢复原有直连、实时订阅及中继；65 秒场景超过原生 60 秒空闲期限，旧直连确实关闭，同设备中继恢复并重新建立直连。每次继续通过 Unit/restore、撤销、账号切换和注销回归，子进程均被回收。

测试代码提交 `b621acbc`，随后 `0c60bf0a` 修正预期关闭的判定：Windows 可能返回 TCP ConnectionResetError，而非关闭帧或 EOF。初次 Windows 矩阵在这里判失败，原日志保留；修正后复验两个 Windows 超时场景。Linux 四项与 Windows 短暂停两项已通过原先更严格的关闭判定，没有为五行测试修正重复这些已通过场景。各项测试源码、应用源码、宿主及日志哈希分别记录，未将不同测试版本混写成同一文件哈希。

同一干净应用和当前 SDK 宿主还通过真实 Edge `155.0.4283.62` 无头浏览器回归：双族 UDP 同时黑洞后自动回退 LAN，以及 LAN TLS 无响应时继续回退中继。每场两次实际写入对应两个唯一请求 ID，未重复执行；关闭后原生 transport 计数均为零。Playwright 仅使用已有开发工具，不引入产品依赖。此项是浏览器传输代码验证，不代表原生 WebView 外壳或发行 ABI 验收。

统一收据 `.build/xrudp-process-pause/acceptance.json` 复核了八项进程结果、两项浏览器结果、2,561 个应用提交文件、静态分析的 146 个输入与报告哈希。XRT 同期完成全部 20 个 C 翻译单元的 Clang 路径分析及两条状态不变量候选路径复核，未确认新运行时缺陷；完整告警和复核说明见 XRT `extlibs/xrudp/docs/static-analysis-validation.md`。

进程暂停同时停止同一 mdo 子进程内的两个原生端点；不等于单端暂停、物理系统休眠、Android Doze、双独立 NAT 或 72 小时验收。既有 27 组长稳继续保留原来源、二进制和时钟，未安装、发布或重启生产服务。

## 已提交源码的独立构建复核

从 mdo `2da7ad5928a35867f517d679f4b6e294bbaded51` 的 Git 提交检出独立普通副本（master，无新分支、无 worktree），仅含 2,561 个已跟踪文件，没有带入主工作区 152 个既有改动或 7 个未跟踪的传输源文件。使用锁定的产品 SDK `a6906e2`、XRUDP 运行时 `3d55ec41`；Windows 与 WSL Git 环境均确认干净，全部提交文件重新计算哈希一致。

Windows GCC 与 Linux Clang 的 server 包均构建成功；自动展开为产品要求的六个扩展。实际打包程序在隔离 Home 与回环临时端口启动，经过内置 TCC 编译与 bootstrap，并通过双进程 Home 排他、异常退出释放锁和重新打开保留会话检查。两平台再用这份干净源码分别通过真实本地账号/协调/中继夹具的双族 NAT 和 IPv6 黑洞回退 IPv4、owner/runtime/nonce pinning、HTTP/live/收据、只读权限、一次性票据、中继回退、注销及取消生命周期回归。没有发起模型请求或生产网站请求。

Windows 包 5,119,895 字节，SHA256 `e417567f4eab1cc569e47797688e3ae5f9759442b2e56507248257f5289b350f`；Linux 包 5,358,049 字节，SHA256 `e3d3a91c24ab9456668db19d0778cec9defa5ab443f1efa0797b0571712f6928`。完整源码清单、构建/实际运行日志与二进制哈希收据在 `.build/committed-source-audit-20261019/acceptance.json`。

本轮修正的是私有验证环境：共享 Git 对象路径和 CRLF 策略需要同时适配 Windows/WSL；最初 Linux 构建把 Git 环境错误记为 dirty，修正后重新构建为 false，产物 SHA 不变。最初 Linux 网站夹具误用没有 SQLite 的 mdo 精简宿主，改用已有 SQLite 网站宿主后通过。两项失败日志均保留，不计为产品失败或产品修复。

这些是 server 开发检查包，保留提交版本元数据；Linux 使用 WSL 本机 ABI，不是发行基线。未验证 GUI 发行、实体 Android、两个独立 NAT、系统休眠恢复或实际 72 小时长稳，也没有安装、发布或重启生产服务。需求与剩余门槛现集中于 `D:/GIT/xrt/extlibs/xrudp/docs/acceptance-matrix.md`，长期目标仍 active。

## 持续拥塞修复

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
| `mdo-xrudp-congestion-qa-r2.apk` | 5,076,205 | `a5347e0d8bea2f52df996fb6fb447a9fb86e7becec99c974baf91cef6ce6e27f` |

已核对实际 APK 内的原生库及 pack 与构建产物相同、ELF ARM64 架构、清单 minSdk26、LOAD 段及未压缩库的 16 KiB 对齐、v2/v3 签名、SDK 收据和原生宿主导出注册表。导出注册表检查不等于在 Android 执行了 Native/Future；这些入口不是可用 `dlsym` 证明执行的 ELF 导出。正式真机用例仍待完成。

最终 QA r2 使用应用当前的安卓轻量版构建号 `30000097`，并包含原品牌资源。初版 QA 临时使用 versionCode1，会经 MdoRuntime 写入一位数 `MDO_BUILD_ID`，随后被 MdoBuildId 的八位数校验拒绝而回退到 unity 的 Windows 默认构建号，影响真机观察到的发行身份。已修正这项私有测试包配置；核对实际清单与 `assets/mdo-runtime.json` 的 `edition=lite`、`build_id=30000097` 一致，避免用错误身份开展真机回归。初版收据另存 `validation-r1.json`，没有改动正式构建脚本或产品运行时。

Windows ADB 从 SDK 锁定的官方 platform-tools 37.0.1 包准备，按已锁定 SHA1/大小验证并记录 SHA256；通过原生 Windows USB 主机执行 `devices -l`，当前没有设备，未用 WSL 设备列表替代 Windows 检查。用户已回复稍后连接，未重复催问、安装或发布。收据 `.build/xrudp-android-congestion/validation.json`、`.build/android-platform-tools/verification.json`。

剩余门槛为最新产品宿主的公网复验、完整长稳、Android 真机、双独立 NAT 和接入压力完整审查。当前版本 9 组长跑与历史 18 组基线继续保持各自计时。

## 最新 SDK 的真实公网双机复验与接入边界

产品 SDK `a6906e2` 的 1,292 个已提交来源在显卡服务器隔离目录 `/home/xs/.cache/mdo-xrudp-public-a6906e2-20261019-r1` 重建；归档 12,157,909 字节，SHA256 `6d09a6eb69c06f20ad786f34d943c02e1a40536b8d87088f9b7332dab89ddc81`。服务端宿主 3,658,768 字节，SHA256 `ef625351ae1050738cf8d7d87499759eb7de4a2fd0dfa2117c8b9234be965c92`。构建及探针使用独立用户单元，内存 1.5 GiB、CPU 100%、TasksMax512、禁用重启；一次完成构建和验证，无须重启生产网站。

新宿主通过时间/等待/进程/持久化/根内移动和六扩展实际外层、嵌套 TCC/原生 Future 探针。随后与当前 Windows 宿主进行真实公网双向测试，均先核对实际宿主收据及 SDK，再为两端私有应用重新生成依赖标记：

| 方向 | STUN | 建连 | 实际空闲 | 用例 |
| --- | --- | ---: | ---: | --- |
| Windows 控制 Linux | `222.186.10.53` | 407 ms | 65 s | `ae735c94ddf54720b385d67508ee6b23` |
| Linux 控制 Windows NAT 后设备 | `ai.xywhsoft.com` | 547 ms | 65 s | `d772675f2984499d9a20f8ca8067e3f3` |

双方真实原生公网映射、Noise、目标专属项目 HTTP/写入/live、只读拒绝、直连取消后的回执/中继、账号退出和正常原生退出均通过。账号协调使用私有测试站点；UDP 使用实际公网，不经过该站点的 SSH 隧道。测试没有覆盖双方分别位于两个独立 NAT 后的网络，不能把本表当作双 NAT 验收。

收据 `.build/public-remote-<用例>/receipt.json` 以及 `.build/xrudp-public-congestion/{launch,remote-validation}.json`；日志 `forward.log`、`reverse-idle.log`。执行后重新核对全部 1,292 个远端来源及宿主哈希；两个临时原生进程均已退出，线上 `xs.service` 保持 PID `3030721` 和 active 状态。

XRT `a148a934` 增加实际生成代码的确定性握手限速边界：同 IP 换端口、IPv6 scope 区分、64 桶满后的不驱逐/拒绝、已知地址剩余额度、时间窗口及总速率。在四个提交后构建中完成 40,000 次满表拒绝、1,024 轮虚拟窗口切换；全程禁止 XRT 堆分配。另以提交源码重建公开原生端点套件，Windows Select/IOCP 与 Linux ASan/UBSan/leaks Select/Epoll 四个进程通过 24 个生命周期场景及 464 个真实未认证 HELLO/伪 CID 包，认证连接存活、保留字节不增长，Engine 正常 join。来源、核心对象、二进制及实际用例集合均重验，记录 `D:/GIT/xrt/out/xrudp-endpoint-admission/acceptance.json`。没有复现运行时缺陷，未修改 SDK/产品运行时；这些有界检查仍不能代替大规模 CPU 洪泛/公平性验收。

剩余为完整长稳、Android 真机、双独立 NAT、接入 CPU/公平性压力审查及最终逐项验收；历史长稳不重启、不改标。

## 本机恶意 UDP 输入下的进展与资源验收

XRT `82e39f64` 新增限时公开 API 夹具及 `tools/test_xrudp_ingress.py`：两条真实 Worker、八条认证流，独立发送器向回环 UDP 混合发送非法格式、合法 HELLO 和指向已知 CID 的错误来源密文。没有替换 UDP/core 函数。Windows 调度使用有限 256 包批次达到约 6,000 包/秒，不修改全机计时器精度。

提交后重建的 Windows GCC Select/IOCP、Linux Clang ASan/UBSan/leaks Select/Epoll，IPv4/IPv6 共八个八秒场景全部通过，累计实际发包 388,352 个。所有 64 条流实例在每个 500ms 采样区间持续进展，内容校验包含完整偏移字节，最慢/最快总进展比至少 0.9581。心跳最大间隔 21.776ms，逻辑预约字节始终不变、在途账本不越界；观测到的最大私有内存增长为 15,998,976 字节（包括诊断开销），低于 32MiB 增长上限，句柄不越界。四个 Linux 进程开启 8MiB ASan quarantine 与泄漏检查；全部正常退出并 join。

另记录 129,190 个已知 CID 的错误来源丢弃、512 份受限 RETRY。CPU 数据包含服务端及八个客户端的两条 Worker，仅作该夹具的实测，不能当成服务端单独 CPU 或跨系统性能对比。此验收覆盖测得的本机负载，不保证互联网容量或任意 DDoS 下仍可用。

开发阶段修正了测试夹具的预期取消处理、Windows 短等待导致发包不足、最终退出采样竞态和诊断 EOF 提示，未将其误报为产品缺陷。正式记录 `D:/GIT/xrt/out/xrudp-ingress/acceptance.json` 已重验源文件、核心对象、可执行文件及逐用例记录。153 个实际来源/元数据归档 `frozen-82e39f64-ingress-source.tar.gz`，SHA256 `9980efad652e76027141207abac404a84191c7265bfe9929a68a3b64ac55fc7c`。

运行时仍为 `3d55ec41`，产品 SDK、依赖锁和安装版本均未变；无需重启或重置长稳。当前版本的业务长跑已完成一次正常的一小时重连，继续保持原时钟，尚未达到 72 小时。剩余为完整长稳、Android 真机、双独立 NAT 和最终逐项代码/交付审查。
