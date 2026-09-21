# mdo 重构进度账本

> 本文件只记录已经提交并验证的结果。进行中的试验不标记为完成。

## 仓库与分支

| 仓库 | 开发位置/分支 | 当前基线 | 说明 |
| --- | --- | --- | --- |
| mdo | `D:\GIT\mdo` / 当前分支 | `5fb69a2dbfe3` | 产品、计划与集成账本 |
| xrt | `codex/mdo-refactor-xrt` 独立工作树 | `2fc7988db1d3` | 原工作树有既存未提交内容，隔离开发 |
| xserver | 待阶段二建立 | `695988f7b8ee` | xrt 阶段门后开始 |

## 状态定义

- `TODO`：尚未开始；
- `DOING`：正在实现或验证；
- `BLOCKED`：存在明确阻断条件；
- `DONE`：已经提交且对应验收通过。

## 工作包

| 工作包 | 状态 | 仓库提交 | 验证 | 说明 |
| --- | --- | --- | --- | --- |
| BASE-001 重构计划与旧 app 归档 | DONE | `443e5e4` | 31 个旧文件逐字节一致；文档结构、UTF-8、Git diff 检查通过 | 建立长期任务基线 |
| XRT-0 Future 生命周期阻断 | DOING | xrt `e94a5d9b`、`63ba83d6` | xrt TCP/TLS Dial Future 的 Windows 模块化、IOCP、单头轨通过；xllm Windows、Linux ASan、Linux TSan 完整通过 | 核心竞态与 xllm operation/transport 所有权已修复；打包版 100 次启动回归留在集成门执行 |
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
| XS-101～109 | TODO | - | - | XRT-GATE 后实施 |
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

## 下一步

1. 审计 VFS 可观测性与可控故障注入是否已满足 xs 消费和 XRT-GATE；
2. 以低负载功能回归、OOM、sanitizer 冒烟、静态检查和干净重编译完成 XRT-GATE；按用户要求不再执行压力或高负载测试；
3. 在 mdo 集成节点复现并验证打包启动崩溃修复，关闭 XRT-0；
4. 通过 XRT-GATE 后建立隔离的 xserver 阶段分支。
