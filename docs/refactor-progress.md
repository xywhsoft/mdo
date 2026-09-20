# mdo 重构进度账本

> 本文件只记录已经提交并验证的结果。进行中的试验不标记为完成。

## 仓库与分支

| 仓库 | 开发位置/分支 | 当前基线 | 说明 |
| --- | --- | --- | --- |
| mdo | `D:\GIT\mdo` / 当前分支 | `443e5e4f4a6b` | 产品、计划与集成账本 |
| xrt | `codex/mdo-refactor-xrt` 独立工作树 | `087a645c` | 原工作树有既存未提交内容，隔离开发 |
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
7. file map、file lock、native handle 和 OS async 只对声明相应能力的 native backend 开放；普通 provider backend 返回 `XERR_UNSUPPORTED`。

## 下一步

1. 实施 XRT-2：公开 VFS API、namespace、provider ABI 和不可变 mount snapshot；
2. 实施 XRT-3：memory、disk、pack provider 与目录枚举；
3. 完成 VFS 模块化、单头、并发、OOM、fuzz、sanitizer 和跨平台门禁；
4. 在 mdo 集成节点执行打包版 100 次启动/退出回归，关闭 XRT-0；
5. 通过 XRT-GATE 后建立隔离的 xserver 阶段分支。
