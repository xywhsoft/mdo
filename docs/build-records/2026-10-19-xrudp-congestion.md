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

原 18 组长稳均保留原二进制及计时，不计为本次运行时的最终验收。仍需当前运行时独立线路复验、接入/资源压力审查、XS 服务类型新宿主复验、最终长稳、实体 Android 和双独立 NAT。用户回复稍后连接 Android，当前已构建的 Android QA 仍属旧 SDK，须刷新后再进行最终真机验收。本次没有发布、替换安装程序或重启生产服务。
