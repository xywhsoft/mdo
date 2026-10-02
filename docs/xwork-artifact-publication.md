# 重启后工具产物的非覆盖发布

完整会话恢复前，先保护已有工具输出。xwork 的 run ID 和 artifact ID 是运行时
计数，重新创建运行时后会从头分配。旧实现对
`artifacts/run-<20位运行号>/<20位产物号>-<工具名>.txt` 调用
`xrtFileWriteAtomic`；该 API 的合同是原子替换，因此相同目录、运行号和工具名
可能覆盖此前的文件。会话恢复到新 Home 后再次运行同样会遇到这条路径。

xwork 3.7.1 使用现有 xrt 根目录 API 解决此问题，没有修改 xrt 核心：

- 锚定 workspace，在根内逐级创建目录，拒绝检查时已存在的 link 父目录。
  后续操作使用锚定的 run 目录句柄；目录解析竞争仍受 xrt 根边界约束。
- 排他创建随机临时文件，只写一遍完整内容，flush、close 后调用
  `xrtRootRenameNoReplace`。现有目标为文件、目录或 link 均不能被替换。
- 仅 `XERR_EXISTS` 分配新编号并重试，最多 128 次；编号耗尽或碰撞超限返回
  `XWORK_ERROR_LIMIT`，其他文件系统错误返回 `XWORK_ERROR_IO`。不使用先查存在
  再替换的模拟，也不使用覆盖 fallback。不支持原子非覆盖操作的平台会失败。
- 一个 registry 配额预约贯穿全部尝试，失败后释放。内存、调用方路径和摘要
  在发布前备齐；registry 锁覆盖最后一次改名和登记，成功后没有可失败的登记
  分配或再次取锁。失败清理只删除本次排他取得的临时路径，不删除发布目标。

产物目录格式、ABI 6、事件 schema 3 保持。最终成功编号进入 metadata 和事件，
碰撞尝试不产生成功事件。编号仍是 runtime 范围身份，不能当成跨重启的持久
会话身份；现有 UI 使用持久 event ID 及受校验的目录路径定位历史输出。

另有现存宿主接入缺口需要后续修复：mdo 把 `ArtifactDirectory` 指向 Home 下的
会话目录，而 xwork 仍用 workspace 路径策略解析该目录。Home 不在项目
workspace 内时，较大工具输出的 spill 会被策略拒绝。这不由非覆盖发布
解决。应为显式由宿主指定的产物存储设计独立信任/锚定边界，继续限制模型
提供的普通文件路径；不能简单放宽所有 workspace 检查。后续需通过真实
`MdoAgentSession`、外置 Home、工具输出和 UI 原路径读取验证。当前 xs/TCC
探针是 native xwork 同 workspace 存储证明，不冒充这项 mdo 接入证明。

这保障发布时的非覆盖，不保障后续人工修改，也不承诺包含目录的掉电持久性。
超过单次碰撞预算会明确失败，原内容保留；失败清理受操作系统权限及 I/O
结果约束。会话导入仍需 staging、来源记录、身份重绑定、投影修复、目录原子
发布和 catalog 更新，本修复不授予 `restore_ready:true`。

## 有界验证

源库 `test_xwork --artifact-publication` 用小型确定性夹具覆盖：两套独立 runtime、
销毁后重建、发布瞬间出现竞争目标、实际成功编号和单次事件、连续碰撞上限、
I/O 立即失败、UINT64_MAX 不回绕、空文件、目录目标和 link 父目录。link 创建
权限不可用的平台会明确报告跳过。核对老文件原字节、registry 配额及临时文件。

`python tests/test_artifact_publication_runtime.py --host .build/host/xs.exe`
通过真实 xs/TCC 及 native xwork 连续启动三次，执行本地 `read` 工具并触发
产物 spill。每轮输入不同，runtime 重置但沿用同一 workspace/run 目录；核对
编号、路径、完整 SHA-256、之前两轮的原字节及无剩余临时文件。使用本地模拟
模型驱动一次真实异步 run，严格核对两次 callback；没有网络模型或真实用户
Home 数据参与。

两平台库测试使用 mdo 锁定的 xrt/xhttp、xllm 和 xllm-session 依赖组合。
源 worktree 内较早生成的 xhttp 单头包含旧 xrt 实现，不能用它代替这组锁定
依赖的 native bridge。构建和测试记录保留在 `.build/qa-artifact-*`；不运行
`dev/v1` 的压力测试，也没有高负载测试。
