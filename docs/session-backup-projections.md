# 恢复前的历史投影修复

`MdoSessionBackupReconcileHistory` 从一个 immutable decoded v2 backup 生成
独立拥有的副本。它修复历史删改或投影写入失败留下的 todo、feedback、
事件图片绑定；不修改输入，也不接触 Home、catalog、模型客户端或队列。
返回结果供后续身份/队列转换及独立 staging 使用，仍不授予发布资格。

## 证据与保留规则

复用 decoder 的同一历史索引、删改区间合并及引用检查，不另写一个近似的
历史状态机。先检查原 UI/侧车/队列关系，再复制文件。只处理这些证据：

| 证据 | 修复动作 |
| --- | --- |
| 明确删改区间覆盖的 `attachments/events/<id>.json` | 从副本移除绑定；图片原文件保留 |
| 明确删改区间覆盖的反馈 event ID | 从副本反馈 items 移除；其他 items 保留 |
| todo 来源被明确删改或旧零来源 clear 失效 | 从最新保留的成功主 Agent todo 重建，没有来源则置空 |
| 成功 todo 事件比现有 todo 来源新，或侧车缺失 | 从该事件的真实 todo parser 重建 |
| 被 retention 淘汰的前缀，缺少明确删改证据 | 保留原侧车字节，并继续报告未验证引用 |
| 旧 `attachments/runs/` 绑定 | 保留原字节，仍报告未验证，不能用重启后重复 run ID 推断对应消息 |

零来源的旧 clear 只作为 todo 的清空证据；不据此删除其他反馈或图片绑定。
它同时清除本轮扫描中的旧 todo 候选，避免恢复出 clear 前的待办。失败或
子 Agent todo、截断的输出不作为重建来源；选中的成功主 Agent todo 若
内容不符合真实输入 schema，明确失败，不能跳过最后状态后假装修复成功。

未知前缀不能证明引用已删除。当前保留范围内部或末尾缺失且没有删改区间
覆盖的引用仍按原验证器拒绝，不通过修复放宽成“可接受的旧数据”。

## 字节、预算与生命周期

只重编码需要修复的 todo/feedback，未改的文件保持原字节。snapshot、
journal、UI 日志、metadata、图片原字节/名称、artifact、draft、queue 和
receipt 不改。文件路径重新排序供共用查找器使用，清单/总字节同步更新。
修复后重新跑共用 schema/CRC/资源引用和历史关系检查，结果的预览事实
来自副本；没有单独伪造一份“通过”摘要。

输入与修复结果可以独立释放。Stage 的磁盘读回和投影副本共用拥有 clone
入口，逐文件及每 64 KiB 检查取消/截止时间；保留最多一份副本文件集合。
所有步骤共享 30 秒协作预算，文件数、单文件、总字节限额只能降低。新增
todo 或重编码增长仍受输出预算限制，失败释放整个副本；Facts 清空，只
保留 Size。Size 不匹配不写调用方内存。取消不修改输入、Home 或其他对象。
这不是进程堆内存硬配额，不能抢占一次原生 JSON/codec 操作。

返回 Facts 包含移除的绑定/反馈数量、todo 是否重建及修复后未验证引用数。
UI/model 语义与图片像素仍由独立门禁检查；本层成功不代表那些门禁通过。
旧无 turn/工具 ID 的 UI 记录在 model 关系门禁中仍报告未验证，不能借修复
将它改写成完整模型重放证明。失败后可以用同一原备份重新尝试。

## 后续事务

独立 staging、投影修复都已提供实际接口，正式恢复仍需串起 worker 生命周期、
新项目/会话身份及来源记录、明确选择目标 workspace、artifact 路径重绑定、
队列/提交意图待确认转换、完整产品 UI replay、非覆盖原子发布与 catalog
通知。阶段性成功均不改变 `restore_ready:false`，不能自动发起模型或工具。

2026-10-02 后续增量：[输入待确认转换](session-backup-inputs.md) 已提供
独立 owning API，旧受理输入不重排，其他输入使用新 ID 的审核状态。
持久 provenance、身份/产物重绑定及生产事务仍待接入。

## 有界证据

扩展已有 HTTP/TLS staging 探针：真实三轮 mdo 模型/UI/inline PNG、草稿、
待确认队列及普通 2 MiB artifact，配合受控的历史删改和旧 todo 记录。
核对明确删改时三类侧车处理、保留反馈/图片原文件、未知前缀原字节保留与
未验证计数、零来源 clear 的有限作用、落后/缺失 todo 的重建和输出增长
限额。最后一条 todo 内容无效、预取消和截止时间失败后 Facts 为空、原备份
可再试；原备份前后重新编码一致，整个源 Home 字节不变。

修复副本释放后，Stage 继续从磁盘重查真实模型/UI 关系及像素；未修改文件
逐一核对原字节。旧 todo 样例明确缺少 turn/工具 ID，其 model 门禁仍报告
未验证，测试不把这些样例冒充完整工具 run 证据。没有压力或高负载测试。

最终 Windows/Linux 有界门禁通过 115 Python、252 Node、90 JS 模块、严格
C11、40 runtime、三项 packed 与独立 A/B。新增断言扩展已有 staging
runtime 探针，探针总数没有增加。Windows 另通过便携 WebView2 Home 与
20 秒启动，根目录 `mdo.exe` 更新为 6,397,671 字节、SHA-256
`e05cb327c3da1b1b400cf4cdd37980a92a8127a580db04d9bfccd319474e23e1`。
Linux A/B 为 6,447,831 字节、SHA-256
`85f745d6d131f780fee766807ea388106c41375e3aed4e09c384778e01c53615`。
SDK 未变化，复用已验证 native host；新 ext4 staging 中七份生产/头文件/
探针输入与 Windows 按 LF 归一化核对。日志
`.build/qa-projection-{windows,linux}-final.log`。本轮没有新增原生点击或
实体设备证据，Linux queued HEAD reset 仍单独待处理。
