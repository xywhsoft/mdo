# mdo 重构进度账本

> 本文件只记录已经提交并验证的结果。进行中的试验不标记为完成。

## 仓库与分支

| 仓库 | 开发位置/分支 | 当前基线 | 说明 |
| --- | --- | --- | --- |
| mdo | `D:\GIT\mdo` / 当前分支 | `37f95cf6c033` | 产品、计划与集成账本 |
| xrt | `codex/mdo-refactor-xrt` 独立工作树 | `2a4f6811dfaa` | 原工作树有既存未提交内容，隔离开发 |
| xserver | 待阶段二建立 | `695988f7b8ee` | xrt 阶段门后开始 |

## 状态定义

- `TODO`：尚未开始；
- `DOING`：正在实现或验证；
- `BLOCKED`：存在明确阻断条件；
- `DONE`：已经提交且对应验收通过。

## 工作包

| 工作包 | 状态 | 仓库提交 | 验证 | 说明 |
| --- | --- | --- | --- | --- |
| BASE-001 重构计划与旧 app 归档 | DONE | 本提交（`BASE-001`） | 31 个旧文件逐字节一致；文档结构、UTF-8、Git diff 检查通过 | 建立长期任务基线 |
| XRT-0 Future 生命周期阻断 | DOING | 待提交 | 待建立定向竞态测试 | 当前发现 xllm 操作槽存在无锁竞争，需先形成可复现证据 |
| XRT VFS RFC | DOING | 待提交 | 文档评审与 API 一致性检查 | 阶段一首批产物 |
| XRT-101～105 native xfile backend | TODO | - | - | VFS RFC 冻结后实施 |
| XS-101～109 | TODO | - | - | XRT-GATE 后实施 |
| LIB-0～3 | TODO | - | - | XS-GATE 后实施 |
| MDO-0～10 | TODO | - | - | LIB-GATE 后实施 |
| QA-RELEASE | TODO | - | - | MDO-GATE 后实施 |

## 已确认的工程事实

1. 三库权威源码位于 `xrt/extlibs`，`xserver/lib` 是零分叉 vendored 副本。
2. `xrtFutureWatchRemove()` 要求调用期间传入的 Future 仍然有效；对已经释放的指针在入口调用 `xrtFutureRef()` 不能恢复生命周期。
3. 当前 xllm 的 `pOpFuture` 与 `pOpWatchNode` 在完成回调和 `xllmCallDestroy()` 之间无同步访问，正在为此建立回归测试与所有权修复。

## 下一步

1. 提交 BASE-001；
2. 建立隔离的 xrt 工作树；
3. 完成 xllm Future/watch 竞态复现与修复；
4. 完成 `xrt/docs/design/VFS.md`；
5. 进入 xfile native backend 无行为变化重构。
