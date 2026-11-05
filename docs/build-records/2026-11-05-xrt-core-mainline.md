# XRT 主线核心与 Agent 库同步验收

日期：2026-11-05。仅更新开发依赖；没有发布、推送、升级用户安装或修改版本编号。

## 归并核对

- 主线 `88bfa0de` 将 mdo 所需的 xllm、xllm-session、xwork 行为归并到规范模块化源码。
  原分支保留为 `archive/mdo/*-20261105` 标签；这是行为归并，不以旧提交成为 Git
  祖先作为判断依据。归并来源及测试范围见 XRT 的
  `docs/development/mdo-branch-consolidation.md`。
- 本轮核验时 XRT `master` 为 `f49b186de53221d67babe29591fc71e2e0672e13`。
  从归并提交到该主线提交，三个 Agent 库没有生产源码变化；机械导出检查覆盖全部
  56 个 SDK 文件，和 mdo 之前已验证的三个生产树摘要一致。
- 之前 mdo 只同步了 Agent 库，底层核心仍为 `3d55ec41`。本轮同时升级核心实现头、
  声明头及其 TCC VFS。原正则修复 `a539c579` 已在主线，无需重复回补。
- 保留现有 6 处进程兼容补丁：已退出 leader 的进程树清理、POSIX PID 保留与最终
  回收、同步捕获的子进程管道收尾、Windows ConPTY 启动。没有为了升级丢掉产品修复。
  这些补丁仍在受校验的宿主 overlay 中，不宣称运行时是未经适配的原版单头。
- 新宿主与 SDK 成套重编，不能据函数名存在或版本文本相同就混用旧二进制。
  xllm 为 3.2.0，session 持久化 schema 4，xwork ABI 9 / event schema 3。

## 固定依赖

完整机器可读记录见同名 JSON 及 `deps.lock`。

| 项目 | 固定值 |
| --- | --- |
| XRT 核心及三个 Agent 库源码 | `f49b186de53221d67babe29591fc71e2e0672e13` |
| XS 宿主基础快照 | `e9a3551a5a533de34e2ca4291a8def71612fbf35` |
| bundle 标签 | `refs/tags/mdo-xrt-mainline-20261105` |
| bundle SHA-256 | `b1eb49b9b68bd9750d7e337f00bcc12086242f2eb9d58ec5fce7e1c6184c3c59` |
| 宿主 overlay SHA-256 | `dd8b39e4d0d4387e6e74b977fcc2cde4d9e9ce85c576b545976aa2a5ebb3b665` |

宿主快照为已有 XS 基础的后继固定提交，不覆盖 XS 主线文件；XRT 主线及工作区保持
不变。没有创建新开发分支。新实现头和声明头分别锁定 SHA-256，防止声明/运行时错配。

## 验证

- Windows GUI 原生宿主、TCC SDK VFS 与候选 `mdo.exe` 实际构建成功，收据通过校验。
- 23 项构建、模型 SDK、原生 overlay 收据契约通过；新增 SDK 缓存复用检查，确认已有
  SDK 无需再次创建 checkout，损坏的补丁仍被拒绝。
- 原生运行：媒体投影及恢复、36 项进程工具输出检查、模型终止/取消/协议错误/TLS
  尾部、不可覆盖文件发布、外部只读 Home、结果文件、调度 executor、MCP reliability。
- 搜索恢复：七组真实客户端进程重启场景通过；请求 ID、单次供应商派发、单次记账、
  用户消息及结果文件保持正确。所有模型/供应商均为本地合成夹具，无真实模型请求。
- Windows 与 Linux 均验证 leader 先退出后的完整进程树清理和同步捕获管道收尾。
- Linux 原生宿主实际构建，模型流式与 TLS 终止夹具实际运行通过。该宿主使用当前
  WSL 的 libc，仅用于开发验收，不作为正式 Linux 发布基线产物。
- Android NDK 30 / API 26 / ARM64：完整原生宿主与匹配 TCC VFS 编译、链接通过；
  不是 APK 发布或真机运行验收。
- source bundle 标签及前置历史验证通过；重新应用全部 25 个兼容路径后，各依赖
  摘要及最终源码完全一致。

私有日志与收据：`.build/xrt-core-mainline-20261105/`。已更新默认 Windows 和 Android
开发运行时，并在该目录保留替换前的字节。根目录正式分发文件、正式构建编号、账号、
会话、计费数据均未调整；Linux 正式宿主在下次发布时按发布基线重建，不能复用旧依赖。

## 后续维护

XRT 通用改动优先直接进入 `master`；必须开临时分支时，小阶段验证后尽快合并。
产品依赖只锁定已提交主线源码及校验摘要，宿主适配继续通过固定快照与明确 overlay
维护，避免长期 mdo 专用分支。具体约定已写入 `tools/dependencies/README.md`。
