# mdo 重构进度账本

> 本文件只记录已经提交并验证的结果。进行中的试验不标记为完成。

## 仓库与分支

| 仓库 | 开发位置/分支 | 当前基线 | 说明 |
| --- | --- | --- | --- |
| mdo | `D:\GIT\mdo` / 当前分支 | `9c4fdaf83efa` | 产品、计划与集成账本 |
| xrt | `codex/mdo-refactor-xrt` 独立工作树 | `e1f680ea4f4b` | 原工作树有既存未提交内容，隔离开发 |
| xserver | `D:\GIT\xserver-mdo-refactor` / `codex/mdo-refactor-xs` | `6e69a8c6da93` | 原工作树有既存未提交内容，隔离开发 |

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
| XS-104 XRT pack provider 接入 | DONE | xserver `bfb87af` | 31 项小型 pack/VFS/HTTP 门禁、26 项结构门禁、VFS 生命周期、默认与五扩展构建、精确提交号版本启动、mdo 5 秒打包启动冒烟通过；未运行压力或高负载测试 | 新 writer 固定输出 XRT v1；运行时及 list/extract 全部经 xrt provider；严格兼容旧 XSVPACK v1；删除旧 parser 和 memory bridge；覆盖中文路径、损坏拒绝、原地覆盖拒绝、失败保留输出、包内 C/头文件编译、磁盘覆盖与 `--no-vfs` |
| XS-105 TCC per-state filesystem | DONE | xserver `1a0d907` | 专用 per-state ABI 自测、31 项 pack/VFS/HTTP、26 项结构门禁、VFS 生命周期、五扩展宿主重建、精确提交号版本启动和 mdo 5 秒打包启动冒烟通过；未运行压力或高负载测试 | v1 回调表绑定到单个 `TCCState`，虚拟文件保持不透明指针；表复制、context retain/release、文件 close-once、not-found/I/O failure 分离；两个 state 同路径隔离并覆盖 UTF-8、空格和长路径；Windows TCCDIR 改用宽字符动态路径，pack 可执行文件在中文目录也能完成 relocation |
| XS-106 TCC 双 VFS | DONE | xserver `259f579` | 默认和 xllm/xllm-session/xwork/md4c/webview 完整宿主构建、per-state ABI、VFS 生命周期、26 项结构门禁、31 项磁盘/打包站点门禁、完整扩展嵌套 TCC 探测通过；未运行压力或高负载测试 | `xsCreateTCCEx` 为每个 state 持有 Application/SDK VFS；SDK provider 按需解压 `/include`、`/xs`、`/lib`；quoted include 与 angle include 隔离，应用同名 `stdio.h` 无法覆盖系统头；重载入口源码使用 state 私有 overlay，结构化错误以 owned `xerror` 返回 |
| XS-107 删除全局 TCC VFS | DONE | xserver `9824564` | 默认与五扩展宿主完整重建、31 项站点/VFS、per-state ABI、VFS 生命周期、26 项结构门禁和完整扩展嵌套 TCC 通过；源码与资源对象旧符号扫描零命中；未运行压力或高负载测试 | 删除动态资源表、固定范围伪 fd、宏重映射、全局清理和脚本旧符号导出；只读存储改名 `tcc_resource_store`，按 open 解压且无全局可变状态；未绑定 state 经 UTF-8 原生路径读取宿主文件系统 |
| XS-108 generation/reload 集成 | DONE | xserver `daa889a` | 默认与 xllm/xllm-session/xwork/md4c/webview 宿主完整重建；27 项结构门禁、VFS 生命周期、31 项站点/pack、per-state TCC、完整扩展嵌套 TCC 通过；一条保留 keep-alive 连接的确定性换代回归验证旧/新静态根隔离和 reaper 精确终结；未运行压力或高负载测试 | 每个 server generation 创建独立 Application VFS namespace，按原顺序 retained disk/memory/pack provider 并复制 Root；脚本编译、TLS、HTTP 静态文件显式绑定 generation，脚本回调中的默认 VFS 随线程局部 generation 切换；最后一个 generation 引用归零后才销毁旧 TCC、driver、VFS 和配置 revision |
| XS-109 发布资料闭环 | DONE | xserver `f122322` | 2 项发布 ZIP 元数据测试、28 项扩展/结构门禁、生成器 dry-run、真实默认宿主重建、per-state TCC 文件系统、VFS 生命周期和版本启动检查通过；未运行压力或高负载测试 | 补齐 `THIRD_PARTY_NOTICES.md`、xrt 内嵌依赖许可副本、各扩展来源/许可和 TCC 修改索引；发布 ZIP 固定携带 NOTICE、许可与精确 revision 的 `SOURCE.md`；发布目录改为可移植的 `release/dist/`；生成的 `tcc_sdk_resources.c` 只存在于 `.build/`，删除源码树内 4.5 MB 陈旧快照 |
| XS-GATE 宿主集成门禁 | DONE | xserver `6e69a8c`、mdo `e9ae004` | Windows/Linux 全 15 扩展构建和嵌套 TCC 通过；30 项结构检查、2 项发布元数据、per-state TCC、VFS 生命周期、Windows 31 项与 Linux 34 项站点/pack 用例通过；确定性 generation/reload、旧 mdo 静态/设置/中文资源/优雅退出、打包 webview 20 秒启动冒烟通过；未运行压力或高负载测试 | 修复 Linux libtcc 标准界限头、严格 VFS 下的 compact hosted C SDK、内存输出宿主 libc 解析与 POSIX pack 执行位；私有 hosted 模式仅用于普通 Linux `libtcc.c`，显式 sysroot 保留目标 libc 链接；阶段二完成 |
| LIB-0 三库 API 与实现审计 | DONE | xrt `fa6c082d` | 公开 API、所有权/线程/回调、可变全局、生命周期、错误、持久化、测试缺口、mdo 依赖、vendored 漂移与构建入口逐项复核；`git diff --check` 通过；未运行压力或高负载测试 | 冻结 xllm 3.1 兼容加固、xllm-session 显式状态机与 persistence v3、xwork 3.0 runtime/agent/run/task 分层及兼容迁移边界 |
| LIB-101 三库独立构建链 | DONE | xrt `32d75562` | Windows xllm-session 与 xwork warning-as-error 构建/功能套件通过；Linux 三库 warning-as-error 编译链接通过；`git diff --check` 通过；未运行压力或高负载测试 | session bridge 继承 xllm 完整传输模块，xwork 恢复原生 explore/regex，修正 xllm/xwork 默认 XRT 路径与跨 ABI `uint64_t` 测试格式 |
| LLM-101 UTF-8 与错误枚举 | DONE | xrt `df806881` | Windows xllm 完整功能套件、Linux warning-as-error 构建和精确编译检查通过；新增 3/4 字节 overlong 边界与公开错误码全枚举回归；未运行压力或高负载测试 | 保存 UTF-8 continuation 原始长度后校验最短标量；补齐 limit/hook 稳定名称，移除重复 hooks 声明并校正测试版本输出 |
| LLM-102 provider adapter golden | DONE | xrt `0022168b` | Windows xllm 完整功能套件、Linux warning-as-error 构建、四种 provider 请求逐字节 fixture 和三类响应归一化 fixture 通过；未运行压力或高负载测试 | 固化 completions/GLM、Responses、Anthropic 的 adapter 名称、路径、请求 wire bytes 与统一响应语义；补齐 Responses `completed` 和 `max_output_tokens` 的 finish reason 归一化 |
| LLM-108A 分配器状态隔离 | DONE | xrt `ff074576` | Windows xllm 完整功能套件与 96 个确定性 OOM 注入点、Linux warning-as-error 构建、正式对象符号扫描和 `git diff --check` 通过；未运行压力或高负载测试 | 生产分配路径直接使用 CRT，不再携带进程级 allocator selector/counter；故障分配器只在显式测试编译单元存在 |
| LLM-104 client/call 配置快照 | DONE | xrt `5eb72335` | Windows xllm 完整功能套件、Linux warning-as-error 构建、session/xwork 消费端严格编译、两线程单次共享 prefix cache 与活跃 call hook 更新回归通过；未运行压力或高负载测试 | profile 首次使用后冻结且 provider 不得切换 wire dialect；hooks 在 client/call 两级按值快照；profile、hooks 和 prefix cache 由配置锁保护，回调不持锁 |
| LLM-103A 解析与传输资源上限 | DONE | xrt `847860be` | Windows xllm 完整功能套件、Linux warning-as-error 构建、配置拒绝和 SSE 行/事件确定性边界用例、`git diff --check` 通过；未运行压力或高负载测试 | client 显式配置 HTTP 头、响应体、SSE 行和事件上限；零值保持兼容默认，上限只能收紧；每个 call 冻结快照，越界统一返回 `XLLM_ERROR_LIMIT` 并使用溢出安全计算；同步修正文档中的构建产物、多模态和测试分配器说明 |
| LLM-103B 流式解析分片语料 | DONE | xrt `72be4030` | Windows xllm 完整功能套件、Linux warning-as-error 构建；三类 dialect 的整块、逐字节和固定变长分片结果一致，CRLF/LF、注释、空事件、中文 UTF-8、末行无换行及截断 UTF-8 用例通过；未运行压力或高负载测试 | 使用小型确定性 corpus 验证同一 SSE 状态机不受传输分片影响，截断 UTF-8 在任何模型数据交付前返回 parse error |
| LLM-107 结构化错误与脱敏边界 | DONE | xrt `5b64706a` | Windows xllm 完整功能套件、Windows session/xwork 消费端套件、Linux warning-as-error 构建、TLS/provider/parser cause 与敏感值扫描用例通过；未运行压力或高负载测试 | `xllm_error` 以版本化尾扩展保留 domain、stage、operation、xrt kind/code、system code 和有界短消息；不复制请求、凭据或 error data，且 parser/limit/hook 主因不会被二次 transport cancellation 覆盖 |
| LLM-105 重试资格与幂等边界 | DONE | xrt `e27ae006` | Windows/Linux warning-as-error 完整编译，Windows/Linux 各 8 项定向重试回归，Linux session/xwork 消费端严格编译通过；只执行 3 次本地 HTTP 请求规模的确定性用例，未运行压力或高负载测试 | 已交付任一模型事件后所有公共/provider 特例均禁止重试；请求字节写出后的网络失败和传输超时仅在存在非空 `Idempotency-Key` 时允许重放，明确瞬态 HTTP 状态保持有限重试；诊断暴露幂等键事实，Windows 构建可用 `RUN_TESTS=0` 只编译 |
| LLM-106 连接池关闭与断连恢复 | DONE | xrt `e1f680ea` | Windows/Linux warning-as-error 完整编译，各 17 项连接池定向检查与 8 项重试边界检查通过，Linux session/xwork 消费端严格编译通过；仅执行固定的低负载本地请求和两调用并发交错，未运行压力或高负载测试 | 空闲连接入池即挂 READ 哨兵，复用前取消并确认未由对端关闭/可读事件抢先完成；同时执行精确 stream 状态、池龄淘汰和池外关闭，client 销毁先排空池；取消 watch 在 transport 锁外注册，预取消不会同步回调死锁或发起拨号；修复网络引擎启动失败所有权与请求头/定时器失败时的连接泄漏窗口 |
| LIB-1～3 | TODO | - | - | 按 xllm、xllm-session、xwork 顺序实施 |
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
12. xs 只负责从可执行文件 EOF 发现 pack 的精确范围；header/index/entry 校验、LZMA 解压、缓存和打开文件生命周期全部由 xrt pack provider 管理。新包写 XRT v1，旧 XSVPACK v1 仅保留严格读取兼容；Application disk provider 高优先级覆盖 pack provider。
13. libtcc 的虚拟输入文件不能继续伪装成 OS fd；`TCCState` 复制 filesystem v1 表并跟踪不透明文件，销毁前 close 全部遗留文件再 release context。Windows 模块目录必须由 `GetModuleFileNameW` 转成 UTF-8，ANSI 路径损坏会把有效后备搜索错误升级为 I/O failure。
14. Application 与 SDK 搜索路径必须在 libtcc 层区分：quoted include 才能搜索当前源码目录和 Application 路径，angle include 只搜索 SDK 路径。SDK 内置资源由只读 xrt provider 按打开粒度解压，不再为 XS 编译分配进程级伪 fd；候选入口源码由当前 state 的私有 overlay 固化。
15. TCC 内置 SDK 字节只是不可变资源存储，不是第二套文件系统。路径路由只存在于 state 绑定的 Application/SDK filesystem；未绑定 state 只走宿主文件系统，Windows 由 UTF-8 宽字符桥处理原生路径。
16. `xrtVfsRef` 只能保活同一个可变 namespace，不能作为 app generation snapshot。xs generation 必须新建独立 namespace，并按当时的挂载顺序 retained provider；磁盘 provider 冻结的是根句柄和 provider 组合，不复制文件内容，因此外部资源发布仍采用不可变版本目录后原子切换配置。
17. TCC SDK 资源 C 文件是纯构建产物，正式构建按平台/扩展组合生成到 `.build/`；源码目录不保留可漂移快照。二进制发布包必须携带适用第三方声明、许可副本和精确源码 revision/重新链接入口。
18. 严格双 VFS 下的 Linux TCC 不能借 `/usr/include` 或目标 libc 链接文件补齐 SDK。普通动态宿主必须内置 compact hosted C 头，并只在 `libtcc.c` 启用宿主符号解析；显式 sysroot/static 发布继续链接目标 libc，二者由构建计划明确分流。
19. xllm 保留单次调用边界；生产分配器已移除进程级可变 selector，UTF-8、错误枚举、profile/hooks/prefix cache 快照与锁、资源上限、provider golden、流式分片、结构化错误、幂等重试与连接池边界均已收紧。请求字节写出后的网络失败不能仅凭“尚未交付模型事件”自动重放，必须由非空 `Idempotency-Key` 显式声明调用方意图；空闲连接必须以 READ 哨兵观测对端关闭并在复用前撤销哨兵，不能只读取 stream 状态；`xllm_history` 与 session 职责重复，只保留迁移兼容。
20. xllm-session 当前是严格 single-writer 对象，读取 API也会更新 lazy cache；它没有显式状态机，snapshot/journal v2 没有 checksum 与 durability policy，磁盘错误还会丢失 path/operation/xrt cause。下一版必须提供不可变只读 snapshot 和可故障注入的 persistence v3。
21. xwork 的 task、tool registry、subagent 和 MCP proxy 全部随短命 `xwork_agent` 生存，无法支撑跨轮后台任务。v3 固定拆成进程级 runtime、不可变 agent definition、会话级 agent、单请求 run 和统一 task；Python REPL 不进入 core，原生 explore 提升为正式工具 backend。
22. 三库独立构建不能由 xs all-module 宿主替代。session 已改为继承 xllm 的规范 XRT module root；xllm/xwork 默认依赖路径、xwork unity 的 explore/regex 和 LP64 测试格式均已修复。xserver 的 xwork 仍有 Python/config 分叉，当前 `UPSTREAM.txt` 的零分叉声明要到 LIB-3 同步后才重新成立。

## 下一步

1. 继续 LIB-1 xllm 3.1 兼容加固，处理 UTF-8、错误、全局分配器、client/call 合同、parser/golden/OOM 与发布资料；
2. 依次实施 LIB-2 xllm-session、LIB-3 xwork，并从权威源码同步零分叉副本；
3. 全部后续验收继续使用有界功能、故障注入和确定性交错，略过压力与高负载测试。
