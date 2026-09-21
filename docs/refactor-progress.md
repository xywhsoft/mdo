# mdo 重构进度账本

> 本文件只记录已经提交并验证的结果。进行中的试验不标记为完成。

## 仓库与分支

| 仓库 | 开发位置/分支 | 当前基线 | 说明 |
| --- | --- | --- | --- |
| mdo | `D:\GIT\mdo` / 当前分支 | `899083c` | 产品、计划与集成账本 |
| xrt | `codex/mdo-refactor-xrt` 独立工作树 | `a90e616c41fb` | 原工作树有既存未提交内容，隔离开发 |
| xserver | `D:\GIT\xserver-mdo-refactor` / `codex/mdo-refactor-xs` | `6362acc3bef2` | 原工作树有既存未提交内容，隔离开发 |

## 状态定义

- `TODO`：尚未开始；
- `DOING`：正在实现或验证；
- `BLOCKED`：存在明确阻断条件；
- `DONE`：已经提交且对应验收通过。

## 工作包

| 工作包 | 状态 | 仓库提交 | 验证 | 说明 |
| --- | --- | --- | --- | --- |
| BASE-001 重构计划与旧 app 归档 | DONE | `443e5e4` | 31 个旧文件逐字节一致；文档结构、UTF-8、Git diff 检查通过 | 建立长期任务基线 |
| XRT-0 Future 生命周期阻断 | DONE | xrt `e94a5d9b`、`63ba83d6` | xrt TCP/TLS Dial Future 的 Windows 模块化、IOCP、单头轨通过；xllm Windows、Linux ASan、Linux TSan 完整通过；符号保留宿主的单次 GDB 捕获与最小 pack 启动回归完成 | Future 发布与 xllm operation/transport 所有权已修复；旧报告的 mdo 崩点实际为 `xrtValueRelease`，根因是 `app_bak` 对未初始化 `MdoJson` 的释放，不是 Future UAF |
| XRT VFS RFC | DONE | xrt `d4ffb8a9` | API、路径、挂载顺序、snapshot/generation 生命周期、provider ABI、失败原子性与测试矩阵已冻结 | 阶段一设计合同 |
| XRT-101～105 native xfile backend | DONE | xrt `087a645c` | Windows 模块化/单头及全部直接 xfile 消费者通过；Linux 模块化/单头、ASan/UBSan/LSan 通过；dispatch 约 3.16 ns/op，native read-at 约 1.59 us/op | native 状态已抽离，全部文件操作经版本化 ops/capability 分派；非 native map/lock/async 明确拒绝；close-once、OOM 与性能基准已覆盖 |
| XRT-201～207 VFS namespace/provider | DONE | xrt `029248ba` | Windows 模块化/单头、OOM、并发与重入通过；Linux 模块化/单头、ASan/UBSan/LSan、Clang TSan 通过；公开结构跨裁剪 ABI、API 文档、release maturity 与性能 smoke 通过 | 实现规范路径、不可变 mount snapshot、generation 生命周期、provider v1 ABI、普通 xfile/xdir 适配与目录合并；8 个 MISS mount 下 stat 约 384 ns/op、open/read/close 约 506 ns/op |
| XRT-208 provider CaseMode 合同 | DONE | xrt `0a7b7a00` | Windows/Linux 模块化与单头、OOM、并发、公开 ABI 和文档检查通过 | Open、Stat、DirOpen 统一接收 mount 冻结的大小写策略，修复 prefix 与 provider 内相对路径语义不一致 |
| XRT-209 VFS 原生文件打开能力 | DONE | xrt `5c0bd333` | Windows 模块化/单头、backend、OOM、并发和公开 ABI 通过；Linux GCC ASan/UBSan/LSan 模块化/单头及 Clang TSan 并发通过；API 文档、单头生成和 release maturity 通过 | 以兼容旧 v1 尺寸的尾扩展加入 OpenNative；原生 handle、map、lock 和 OS async 能力不再被 provider wrapper 降级，打开文件继续持有 generation |
| XRT-210 根句柄目录枚举 | DONE | xrt `a38e949b` | Windows 模块化/单头与逐分配点 OOM 通过；Linux 原生文件系统上的模块化/单头 ASan/UBSan/LSan 通过；公开 ABI、API 文档、单头生成和 release maturity 通过 | POSIX 以 fdopendir/fstatat、Windows 以 NtQueryDirectoryFile 从已锚定句柄枚举；目录改名和根关闭后迭代器仍有效，点条目不暴露父目录能力 |
| XRT-211 根内精确无链接解析 | DONE | xrt `0233e4db` | Windows 模块化/单头、完整 root 回归与逐分配点 OOM 通过；Linux 新增 policy 模块化/单头 ASan/UBSan/LSan 通过；公开 ABI、API 文档、单头生成和 release maturity 通过 | 公共 root 行为保持兼容；内部策略逐 segment 以锚定目录枚举确认精确名称、探测创建时的大小写别名，并在重写前拒绝符号链接/reparse point，为 disk provider 提供统一安全底座 |
| XRT-301 memory provider | DONE | xrt `83217171` | Windows 模块化/单头、穷举 OOM、四线程压力通过；Linux ASan/UBSan/LSan 与 Clang TSan 通过；API 文档和 release maturity 通过 | 支持 copy/owned buffer、空文件、原子 seal、大小写双索引、目录派生、只读文件能力和独立 blob 生命周期；旧打开文件不受 unmount/namespace 销毁影响 |
| XRT-302 disk provider | DONE | xrt `2c3e9914` | Windows 模块化/单头、穷举 OOM、四线程压力通过；Linux 原生文件系统上的模块化/单头 ASan/UBSan/LSan、OOM 与 Clang TSan 通过；公开 ABI、API 文档、单头生成和 release maturity 通过 | 以 `xroot` 锚定物理根并冻结读写授权；逐 segment 处理敏感/ASCII 折叠查找，拒绝折叠冲突、链接/reparse point 和特殊文件；保留原生文件能力与 generation 生命周期；目录改名后仍可访问；修复 Windows 复制目录句柄共享枚举游标导致的并发失败 |
| XRT-303A pack 格式与所有权合同 | DONE | xrt `604f7544` | `git diff --check` 与设计文档复核通过 | 冻结范围内相对偏移的新格式、严格 `XSVPACK` v1 迁移解析、解析预算、缓存状态机与统计；Create 成功接管任意 ReadAt source，失败保留调用方所有权 |
| XRT-303B pack provider 核心实现 | DONE | xrt `48ac36df` | Windows 模块化/单头、公开 ABI、API 文档、单头一致性与 release maturity 通过 | 实现新格式与严格 XSVPACK v1 parser、STORE/LZMA1、不可变文件和目录、大小写双索引、条件变量单 loader、稳定失败、硬缓存预算、LRU 淘汰与统计；打开文件独立持有 blob |
| XRT-304 pack provider 压实 | DONE | xrt `d64cb745` | Windows 模块化/单头、逐分配点 OOM、100 个同步首开线程和 1000 轮确定性 fuzz 通过；Linux Clang libFuzzer 10000 轮及模块化/单头 ASan/UBSan 冒烟通过；API/ABI、单头一致性、fuzz 闭包和 release maturity 通过 | 覆盖 Create 所有权失败、LZMA 分配失败、单 loader 成功/永久校验失败广播、缓存统计、双格式畸形输入和持久语料；强制干净重编译发现上一提交的 `7zTypes.h` 被误截断，本提交恢复完整上游源码并验证其实际参与编译。后续按用户要求略过压力与高负载测试 |
| XRT-401 overlay 示例与 API 文档闭环 | DONE | xrt `2fc7988d` | Windows memory VFS 模块化、单头与示例运行通过；VFS API 文档 31 个函数、39 个常量、24 个类型 `missing=0`；single 与 release maturity 通过 | 官方示例演示内置资源、高优先级覆盖、MISS 回退、卸载恢复和 ERROR 阻断语义；不扩张 VFS ABI |
| XRT-402 可观测性与故障注入边界 | DONE | xrt `93410a17` | Windows、Linux Clang ASan/UBSan 的模块化和单头用例通过；API/ABI、单头一致性和 release maturity 通过 | 以 MountId 和 provider 自有统计支撑诊断，不在 lookup 热路径加入全局计数；用普通 source backend 验证合法短读、提前 EOF、结构化 I/O 错误、缺少 ReadAt 和 Source close-once，不增加生产全局故障开关 |
| XRT-403 pack fixture 隔离 | DONE | xrt `d3dc726e` | VFS 组合功能门通过；OOM/并发目标仅编译模块化与单头轨，未执行高负载用例；单头一致性与 diff 检查通过 | 临时文件 helper 改为测试显式 opt-in，避免组合 suite 因全局 FILE_TEMP 特性暴露未使用静态函数；遵循后续略过压力和高负载测试的约束 |
| XRT-404 单头 LZMA 宏卫生 | DONE | xrt `a769dadf` | Windows `single_all_tests` 的完整与排除 memory debug 变体均以 warning-as-error 编译、链接、运行；pack 模块化/单头低负载功能门通过 | xs 首次同步编译发现 SDK 的 `Align`、`Literal` 等内部宏污染后续 logger/regex；权威 xrt 已统一清理 92 个实现宏并增加泄漏断言，未在 xs 打本地补丁 |
| XRT-405 LZMA 符号隔离 | DONE | xrt `36955ba1` | decoder 对象只导出 `__xrtLzma*`；xs 默认宿主与自身 LZMA decoder 共存链接通过 | 将 SDK 源码私有 helper `LzmaDec_InitDicAndState` 收紧为 static，消除单头与宿主 pack decoder 的重复符号 |
| XRT-406 代理 TLS Future 拨号 | DONE | xrt `96197cc5` | Windows 模块化与单头的代理取消、超时用例通过；既有 callback 取消、超时回归通过；公开 ABI、API 文档、单头一致性和 release maturity 通过 | `xrtTlsDialProxyAsync` 与直连 Future 复用同一发布桥，统一覆盖回调早于构造返回、取消转发、错误和 Stream 所有权 |
| XLLM-101 上收安全代理传输 | DONE | xrt `a90e616c` | 专用 13 项低负载代理配置/匹配/快照测试通过；原有完整测试严格编译链接通过但未执行其中的并发压力段 | 权威 xllm 提供显式代理类型、无固定缓冲的 bypass 匹配、凭据深拷贝与清零，并调用官方 `xrtTlsDialProxyAsync`；不完整代理和明文 HTTP 代理配置明确失败 |
| XRT-GATE VFS 库门禁 | DONE | xrt `d37bca64` | 组合功能门、可选高负载测试编译门、API 文档、公开 ABI、release maturity、单头一致性和 diff 检查通过 | 库内实现允许进入宿主集成；真实打包路径仍是 XRT-0 的必要宿主验收，不能用库测试替代 |
| XS-101 同步并锁定 xrt | DONE | xserver `32e7a23` | xrt 单头/声明头逐字节匹配 `36955ba1`；3114 个 TCC 导入符号再生；Windows 默认宿主和 26 项扩展/VFS 单元测试通过 | 首次集成发现的宏与符号隔离问题均回到权威 xrt 修复 |
| XS-101B 同步代理 Future 与 xllm | DONE | xserver `aa27aca` | xrt 单头/声明头及 xllm 已收录文件逐字节匹配 `a90e616c`；3115 个 XRT 导入符号再生；26 项扩展/VFS 单元测试、xllm 宿主构建和版本启动冒烟通过 | xserver 不再保留本地代理桥接分叉；宿主只消费权威 XRT/xllm 实现 |
| XS-101C SDK 头自包含 | DONE | xserver `248df08` | 五扩展 webview 宿主重建、26 项扩展/VFS 检查和最小 pack 启动冒烟通过 | `xsbase.h` 自行包含其内联 `strcmp` 所需的标准声明，TCC 应用不再依赖使用方碰巧先包含 `string.h` |
| XS-102 Application/SDK VFS 对象 | DONE | xserver `de3fd94` | 双命名空间生命周期与隔离测试、26 项扩展测试、默认宿主完整构建和版本启动检查通过 | 建立进程级 Application/SDK VFS；公开接口仅返回 Application 借用句柄，SDK 命名空间保持宿主内部可见 |
| XS-103 应用资源统一读取 | DONE | xserver `6362acc` | Application VFS 路径/overlay 契约、双 VFS 生命周期、26 项结构测试、22 项 pack/HTTP 端到端、默认与五扩展宿主构建、提交号版本启动和 5 秒打包启动冒烟通过 | 配置、TLS 证书、HTTP 静态文件、脚本主文件和 reload 快照统一经 `xsAppOpen`/`xsAppReadAll`；ReadAll 始终 owned；disk provider 保留 sendfile，普通 provider 使用有界分块泵；旧包仅由 XS-104 前的 memory bridge 兼容 |
| XS-104～109 | TODO | - | - | 继续迁移 pack 与 TCC 文件系统 |
| LIB-0～3 | TODO | - | - | XS-GATE 后实施 |
| MDO-0～10 | TODO | - | - | LIB-GATE 后实施 |
| QA-RELEASE | TODO | - | - | MDO-GATE 后实施 |

## 已确认的工程事实

1. 三库权威源码位于 `xrt/extlibs`，`xserver/lib` 是零分叉 vendored 副本。
2. `xrtFutureWatchRemove()` 要求调用期间传入的 Future 仍然有效；对已经释放的指针在入口调用 `xrtFutureRef()` 不能恢复生命周期。
3. xllm 原有 `pOpFuture` 与 `pOpWatchNode` 的分离裸指针设计会在完成回调和 `xllmCallDestroy()` 之间形成 UAF；现已改成 slot/Watch 双引用 node，并用 `OpMutex` 管理发布与摘除。
4. TCP/TLS Dial Future 的完成回调可能早于构造 API 返回；读取构造线程发布的 `Dial` 字段前必须先经过 `xrtFutureBridgeWait()` 的 acquire 边界。
5. xllm 的 transport 状态机必须与 watchdog、取消和销毁串行化，连接指针不能在工作线程与调用线程之间裸读写。
6. `xfile` backend 表采用 `Size + Version + Capabilities`；backend state 的所有权在创建入口转移，失败和正常关闭都必须恰好消费一次。
7. file map、file lock、native handle 和 OS async 只对声明相应能力的 native backend 开放；callback provider backend 返回 `XERR_UNSUPPORTED`，声明 `XVFS_PROVIDER_NATIVE_OPEN` 的 provider 直接转移原生 `xfile` 并保持这些能力。
8. Windows `DuplicateHandle` 复制目录句柄时共享 `NtQueryDirectoryFile` 的枚举游标；并发根目录枚举必须以空相对名重新打开独立 file object。POSIX 的 disk open 必须在 `fstat` 验证普通文件前使用 `O_NONBLOCK`，避免末级文件被竞态替换为 FIFO 后阻塞。
9. `xfile` 没有可公开增加的共享引用；pack provider 对任意 backend 的正确所有权语义是 Create 成功时接管 Source、失败时由调用方继续持有，而不是只对原生文件复制平台句柄。
10. 构建缓存不能作为第三方源码完整性的证据；pack 的 OOM 强制重编译曾发现已缓存对象掩盖 `7zTypes.h` 截断，发布门必须包含干净重编译和生成单头一致性检查。
11. xs Application Root 由显式配置文件父目录或可执行文件目录确定；`/app` 外的物理绝对路径会被统一入口拒绝。配置、证书、静态文件和脚本读取不再根据来源返回借用缓存，`xsAppReadAll` 只返回调用方拥有的缓冲。

## 下一步

1. 实施 XS-104：以 XRT pack provider 替换旧解析器和 memory bridge，迁移 pack/list/extract/strip，并把旧 pack 工具的 Windows 中文源目录失败加入回归；
2. 为 TCC 建立 per-state filesystem vtable，消除进程级站点挂载；
3. 为旧 xs pack fixture 和新的 XRT pack provider 建立有界兼容迁移验证；全部后续验收继续略过压力和高负载测试。
