# XS 锁定当前 XRT 提交

最新运行时已推进到 `3d55ec41`，补齐会话持续拥塞证明，XS 主仓 `6bc2fd8`、兼容 SDK `a6906e2`、mdo 依赖锁和 bundle 均已同步。新宿主和固定提交验证见 [持续拥塞与当前 SDK 验证](2026-10-19-xrudp-congestion.md)。下文保留此前 `b093114d` 等版本的历史验证，不将旧二进制或长稳计时改标为新版本。

当前 XS 主仓和 mdo 实际使用的产品 SDK 均已锁定到 XRT 运行时功能提交 `b093114d3d78d9700a113518d9030ffe836c7602`。使用提交导出，包含已完成的 xrudp Future 接口，不包含其他任务尚未提交的文件系统、内存和核心单头修改。后续 XRT `5735f4b4`、`700e6133` 仅改动长测基础设施、测试和进度文档，运行时代码未变，产品依赖保留上述功能提交。下面保留此前同步的记录，最新同步和验证见文末。

- XS 主仓提交：`773a903`。快照来源、扩展来源和脚本导出来源同步更新。
- 产品 SDK 提交：`7e34997e717f1fe91dd340430dc4662d32ee1358`，保留原有 xllm、session、xwork API 和产品修复。
- mdo `deps.lock`、`tools/dependencies/xs-xrt-current.bundle` 同步更新。bundle 为 4,417,895 字节，SHA256 `6ed5c8570db612289ca947d9a744537fab9bec0b934931ddb214c12c70b75add`，引用 `refs/heads/master`，公共历史前置 `695988f7b8eec4b52bcc829a2d52210443c0bc69`。

时间、等待和原生 HTTP 的兼容性迁移已在此前完成，详见 `2026-10-18-xs-xrt-upgrade.md`。本次最新提交相较 `7c82e311` 仅新增独立协议测试、长稳测试驱动和文档；核心单头、声明和全部 46 个 xrudp 快照文件逐字节验证一致。没有重新引入旧 XRT 实现或改变持久化时间、公共 ABI。

本轮检查：XS 快照同步、来源固定和扩展配置 44 项通过；mdo 依赖锁定、换行独立哈希和 image 原生锁定 3 项通过。bundle 引用、前置和全部产品依赖校验通过。证据分别为 XS `.build/xrt-latest-snapshot-tests.log`，mdo `.build/xrt-latest-dependency-tests.log`、`.build/xrt-latest-lock.log`、`.build/latest-xrt-byte-verification.json`。

Linux 新宿主已从产品 SDK 新提交构建（本机 Clang/native ABI，非发行版 ABI 验收）。时间、等待、进程、60 ms 定时器、持久化日历和根内禁止覆盖移动通过；六个扩展的 562 个导出及 IPv4/IPv6、固定身份 Noise、可靠字节/FIN、取消、关闭句柄均通过外层/嵌套 TCC。mdo 十二组实际会合场景通过。证据为产品 SDK `.build/xrt-latest-linux-{build,runtime,sdk}.log` 和 mdo `.build/xrt-latest-linux-rendezvous.log`。

Windows 新宿主及 GUI 子系统变体也已成功重建，实际构建版本为产品 SDK `7edc866`。同一组时间/等待/进程/定时器/根内移动回归、六个扩展的 562 个外层/嵌套导出及原生 IPv4/IPv6、十二组 mdo 会合场景均通过。证据为产品 SDK `.build/xrt-latest-windows-{runtime,sdk}.log`、`xrt-latest-windows-build-recovered.log` 和 mdo `.build/xrt-latest-windows-rendezvous.log`。此前内存压力导致的失败日志保留；内存恢复后重新构建通过，未通过修改或降级运行时代码规避问题。

新宿主位于 `.build/xrt-latest-host/xs.exe` 和 `.build/xrt-latest-linux/xs`，收据分别与真实二进制 SHA256 核对。Android 旧构建的运行时代码与本次一致，但其旧提交收据不能充当本次新提交的构建收据。未发布或替换已安装程序；正在运行的隔离长稳也继续使用其原来记录的二进制，不在运行中偷换版本。

后续锁定 `1d35b80b` 时重新通过 XS 快照/来源/扩展配置 44 项、mdo 依赖与哈希 3 项，并在上述 Windows、Linux 宿主上重新通过单调计时、条件等待、进程超时、XS 定时器、Unix 起点、计划持久化及根内禁止覆盖移动回归。日志为 XS `.build/xrt-current-snapshot-tests.log`、mdo `.build/xrt-current-dependency-tests.log` 和产品 SDK `.build/xrt-current-{windows,linux}-runtime.log`。bundle 校验及依赖完整校验记录于 mdo `.build/xrt-current-lock.log`，48 个文件逐字节核验记录于 `.build/current-xrt-byte-verification.json`。

此次 `7edc866` → `7e34997` 只有版本来源文本和生成注释改变，不修改运行时代码；现有宿主的真实构建收据仍为 `7edc866`，未改写为新提交的构建收据。后续产品构建由更新后的锁文件和 bundle 获取 `7e34997`。

## 当前同步到 ccd6390e

用户再次要求将 XS 使用的 XRT 更新到最新版。本地主分支 `ccd6390e` 比远程主分支 `ac059267` 领先 15 个提交，远程不存在本地缺失的主线提交。本次以本地主分支已提交内容为准。

- XS 主仓提交 `2f2b25d85d4be2d4e0d81c52cca25997c7631f55`，通过 `sync_xrt.py --source D:/GIT/xrt --ref ccd6390eb913f7689e7d34551d33cc1d7553f442` 导出，987 个来源/生成文件的哈希校验通过，快照、固定提交导出及扩展配置 44 项通过。
- mdo 兼容 SDK 提交 `4d4c10dda9e4921b40d84724b4319bb716ee4f1e`。48 个核心与 xrudp 快照文件逐字节核对，运行时代码与旧锁定版本完全相同，仅 xrudp README、版本来源和生成注释改变；保留产品的 xllm、session、xwork API。
- mdo `deps.lock` 和 `xs-xrt-current.bundle` 同步更新。bundle 为 4,419,391 字节，SHA256 `c68728de1336f532db60982d6c6352c9a94290594fca34d51bb6927e484268a4`，引用 `refs/heads/master`，公共前置保持 `695988f7b8eec4b52bcc829a2d52210443c0bc69`；完整依赖与 bundle 验证通过。

Windows 和 Linux 实际 TCC 宿主再次通过单调计时、条件等待、进程超时、XS 定时器、Unix 起点、持久化日历以及根内禁止覆盖移动回归。兼容性测试自身的栈内锁/条件变量清理改为 `Unit`，并新增清理后无错误断言，提交 `40a99c75c02f019124069b7b84aa59d1c3b0dee1`。两平台使用原 `7edc866` 宿主，其运行时代码与本次快照逐字节相同；本次没有重建宿主，也没有改写旧二进制的构建收据。

另对开发中的 xrudp Future 退出崩溃完成定位：`xrtMutexInit(&state->Lock)` 创建的是嵌入式锁，错误调用 `xrtMutexDestroy` 会释放整个 state 所在的首地址，随后清零和再次释放破坏堆缓存。应配对调用 `xrtMutexUnit`。修正后 Windows Select/IOCP 与 Linux Select/Epoll 的 IPv4/IPv6 共 8 组开发测试全部退出 0，Linux 同时通过 ASan/UBSan/泄漏检查。这是调用方的所有权错误，升级到字节相同的 XRT 不能单独修复它；该修正属于仍在开发的异步接口，不混入本次已提交 SDK 快照。

证据：XS `.build/xrt-ccd6390-snapshot-tests.log`；兼容 SDK `.build/xrt-ccd6390-{windows,linux}-runtime.log`；mdo `.build/xrt-ccd6390-{byte-verification.json,lock.log,dependency-tests.log}`；XRT `dev/xrudp-async-mutex-unit-{windows,linux}.log`。没有发布、替换已安装程序或重启正在运行的长稳测试。

## 当前同步到 b093114d：Future 接口集成与新宿主验证

XRT `b093114d3d78d9700a113518d9030ffe836c7602` 已完成可选的 xrudp Future 接口。本次同步其已提交内容，并将三个新接口加入宿主导出和外层、嵌套 TCC 的真实运行验证。XS 主仓和产品 SDK 均在 `master` 分别提交构建修复与依赖集成。

| 项目 | 提交或结果 |
| --- | --- |
| XS 主仓 Linux 构建修复 | `60ddc881c9358c6c2f38198b02ef6ed25d73eecb` |
| XS 主仓 XRT 集成 | `ea50214b07dff02436e76a23a5c8ecef2a5e509b` |
| 产品 SDK Linux 构建修复 | `35521dd` |
| mdo 锁定的产品 SDK | `49dd88fac538ca3b4988975ea9d92ac45db223d4` |
| bundle | 4,431,278 字节，SHA256 `4720b62477e17b07e5764b899723e5a5fc2026adf74d05613713c0ba498fa119` |
| bundle 引用和前置 | `refs/heads/master`；`695988f7b8eec4b52bcc829a2d52210443c0bc69` |

产品 SDK 的 47 个 xrudp 文件、XRT 单头和声明共 49 个文件，与上述 XRT 提交逐字节一致。原 xllm 3.1、session 3.0-dev、xwork 3.11/ABI 8 的文件指纹及其他扩展注册项保持一致；完整依赖验证也覆盖 image 的固定树哈希。`deps.lock` 和 `xs-xrt-current.bundle` 同步更新，未将 XS 主仓较新的其他产品 API 强行混入 mdo。

本轮确认并修复了一个独立的 Linux 构建缺陷：Clang 的 `-dumpmachine` 返回 `x86_64-pc-linux-gnu`，实际 libc 头文件位于 `/usr/include/x86_64-linux-gnu`。此前将前者直接写入 TCC `CONFIG_TRIPLET`，导致 `<stdint.h>` 无法包含 `bits/libc-header-start.h`。现在优先使用编译器的 multiarch 信息，缺失时从实际预处理器搜索目录解析 libc 目录。GCC、Clang、平坦 libc 布局和非法目标文本有针对性回归；重建后的 Linux 实际 TCC 编译、执行全部通过。此前的嵌入式锁 `Destroy`/`Unit` 所有权错误已在 XRT Future 实现中修正，未用升级版本号替代实际修复。

新产品宿主从干净、已提交的 `49dd88f` SDK 构建，使用 mdo compact profile。Windows 控制台与 GUI 子系统变体均成功链接；Linux 使用本机 Clang/native ABI，仅作为兼容性测试构建，不作为发行版 ABI 验收。实际文件与构建收据的版本、profile、大小和 SHA256 已核对：

| 宿主 | 大小 | SHA256 |
| --- | ---: | --- |
| `.build/xrudp-future-host/xs.exe` | 3,711,488 | `6053fa2315f861a088bbd5ab8730672a81db2395dc9bc809ea8f0e5f18eadc03` |
| `.build/xrudp-future-linux/xs` | 3,955,264 | `f61f2a4741f7f89abda8fa0d525f30decfa1c3c290b3f664cb2820c19d2f254d` |

验证结果：

- XS 快照来源及哈希校验通过；主仓 48 项快照、构建和扩展单元检查及 7 项构建来源检查通过。产品 SDK 40 项检查通过，其中 1 项按平台跳过；mdo 依赖与哈希 3 项通过。
- Windows、Linux 新产品宿主分别通过六个扩展的 565 个公共导出及外层、嵌套 TCC 调用，涵盖 IPv4/IPv6 Noise、可靠字节/FIN、取消、关闭句柄，以及 Future 消息复制和 Engine 销毁后结果保留。保留现有直接操作接口的测试。
- 两平台单调计时、条件等待、进程超时、XS 60 ms 定时器、Unix 起点、持久化日历及根内禁止覆盖移动回归通过；mdo 各 12 组真实会合、NAT、双族选择、黑洞和身份校验场景通过。
- Windows 新产品宿主进一步通过真实账号、直连 HTTP/live/回执和只读权限测试；独立 NAT 模拟器丢弃 IPv6 数据后，IPv4 接管成功，原路由取消、一次性会合、重放隔离、中继兜底、恢复及退出全部通过。测试只使用隔离网站与临时账号。
- XS 主仓 xrudp-only 宿主在两平台通过服务类型的字节/消息/游戏流、固定身份与上下文、重入取消、重载保留、错误回滚和禁用测试。这些主仓测试构建保留提交前的真实 `2f2b25d-dirty` 收据；未将其伪装成提交后的二进制。

证据：主仓 `.build/xrudp-future-{compiler-unit,provenance-unit,snapshot-check}.log`、`xrudp-future-sdk-{windows,linux-fixed}.log` 和 `xrudp-future-service-{windows,linux}-test.log`；产品 SDK `.build/xrudp-future-{windows,linux}-{build,runtime,sdk}.log`；mdo `.build/xrudp-future-{sdk-byte-verification.json,lock.json,dependency-tests.log}`、`xrudp-future-{windows,linux}-rendezvous.log` 和 `xrudp-future-windows-remote.log`。

正在运行的九组隔离长稳仍保留原二进制和计时，其旧运行时结果不计为本次 Future 运行时的 72 小时验收。XRT 原生 Future 的 68 组固定提交验收证据另存于 `out/xrudp-endpoint-future/acceptance-receipt.json`，当前源文件和二进制哈希复核通过。整体 xrudp 长期任务仍有最终长稳、实体 Android 和两端独立 NAT 等验收工作；本次完成依赖升级和兼容性验证，没有进行安装包发布或线上替换。

## 当前 SDK 在显卡服务器的重建与双向公网复验

从干净的产品 SDK `49dd88f` 提交导出 1291 个实际构建来源文件，剔除历史发行包和开发样例二进制。归档为 12,154,728 字节，SHA256 `d1c172813643f1812135b14e5d16e3e75c26e9d545610296b36f0daea5902940`。显卡服务器独立目录 `/home/xs/.cache/mdo-xrudp-public-49dd88f-20261019-r2` 完成 GCC 原生构建，构建前后逐文件哈希保持一致。新 Linux 宿主为 3,658,768 字节，SHA256 `e53f7dabb184857f109ecc4b987b3f59277baa702fc7707a6791f9f6994e83d4`，其完整提交、profile、导出和二进制大小/哈希与真实收据一致。此构建使用服务器原生 ABI，不充当发行基线 ABI 验收。

构建后的时间/等待/进程/定时器/持久化/根内移动回归，以及六个扩展、565 个公共导出的外层与嵌套 TCC 验证均通过，包含实际 IPv4/IPv6 Native/Future 和 Engine 销毁后结果保留。首次构建包装器的运行检查失败是测试容器 `TasksMax=64` 不足以容纳默认 64 个工作线程加主线程等；二进制已经构建成功。仅将复验容器限额提高到 512 后，同一二进制检查通过，CPU、内存和最长执行时间仍有限制，没有修改产品代码或放宽生产配置。

`tests/test_remote_public.py` 修正了测试入口使用旧生成缓存的问题：只在两个私有应用副本内按当前 `deps.lock` 和来源清单生成 unity/deps；检查复制期间及两次复制之间原应用未变化，并在连接前验证两端宿主版本、profile、扩展、大小和 SHA256。收据保存真实 SDK、XRT、原应用及测试副本指纹。原应用包含其他任务尚未提交的改动，其树哈希 `0a5f9b305c4adf916a1948ed835df80baab5b4eb279468e0d541061243071e38` 保持不变，没有把工作区来源伪称为干净的 mdo 提交。

| 真实方向 | STUN | Noise/选路耗时 | 空闲后继续业务 |
| --- | --- | ---: | --- |
| Windows 控制端 → 显卡服务器目标 | `222.186.10.53` | 407 ms | 65 秒后通过 |
| 显卡服务器控制端 → Windows 目标 | `ai.xywhsoft.com`，实际 DNS | 484 ms | 65 秒后通过 |

两次均通过真实公网反射候选、原生 xrudp、目标独有项目写入、HTTP/live、只读拒绝、取消直连后的已完成写入回执及中继读取、控制端退出和目标撤销。SSH 仅承载隔离账号的信令/测试控制，业务经过 UDP；两端原生宿主均返回 0 并正常关闭 Engine。两个用例是独立会话，未将单次短测计作连续长稳；服务器具有公网 IPv4，该结果不能代替两端各自处于独立 NAT 后或公网 IPv6 的验收。

证据为 `.build/xrudp-public-future/{launch.json,remote-validation.json,forward.log,reverse-idle.log}`，业务收据为 `.build/public-remote-d75bf6cd354c49978a7ee70f86997b06/receipt.json` 与 `.build/public-remote-567f5c68dc76418d8a55b942abfad693/receipt.json`。远端来源和退出进程另行核对；生产 `xs.service` 仍为原 PID `3030721`、active，未安装、发布或重启生产网站。

同时在当前 `b093114d` 运行时的固定输入/二进制上重新通过 48 个独立线路 STOP/RESET、双向优雅关闭和丢包恢复场景，覆盖 Windows Select/IOCP、Linux ASan/UBSan Select/Epoll、IPv4/IPv6 及两个角色；收据为 XRT `out/xrudp-wire-interop/future-controls-acceptance.json`。这些回归和 XRT 后续测试/文档提交不改变运行时依赖锁。原有十八组实际长稳继续各自计时，最新业务长稳已自然续期并完成第一次小时重连，尚未达到 72 小时。用户已回复稍后连接 Android，实体设备验收继续保留。
