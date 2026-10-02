# 恢复输入的待确认转换

2026-10-02：`MdoSessionBackupReviewInputs` 已实现，供后续恢复 worker 在
发布前使用。它返回独立拥有的 v2 副本，改变队列/草稿的执行关联；不访问
Home、live queue、catalog、模型或工具。单独调用不构成完整恢复或发布许可，
`restore_ready:false` 保持。

## 为什么不能直接改队列状态

现有 `MdoQueueReceiptApply` 只接受与回执对应的 `sending` 项。将同一个 ID
改成 `staged` 却保留旧回执，会在读回时冲突。旧 ID 还可能代表已经受理的
请求，刷新或重新调度不能把它当成新的输入。转换保留原回执和 UI 事件，
为需要再次确认的输入生成独立的 32 位安全随机十六进制 ID。

分类复用 `backup_relations.c` 的保留历史索引、回执解析和矛盾检查，
不通过 live API 的读路径核对。那些读路径会修复回执、查询并恢复运行，
不能作为离线导入的验证器。

| 已保留的证据 | 转换结果 |
| --- | --- |
| schema 1 已受理回执 | 不再入队；原回执保留，Facts 记录 accepted |
| 回执对应保留的主 Agent 队列启动事件 | 不再入队；schema 3 还须匹配 Agent run ID |
| schema 2/3 起动回执，缺少已保留的启动证据 | 新 ID、等待确认，并报告 admission uncertain |
| 无回执的 `sending` 队列项或 `posting` 草稿意图 | 新 ID、等待确认，并报告 admission uncertain |
| 无受理证据的普通队列项或草稿意图 | 新 ID、等待确认；不宣称已经受理 |

旧回执缺少 UI 证据的未验证计数仍保留；输入分类不把历史语义检查提升为
完整验证。受理证据只表示请求已进入历史，不能推断该次模型或工具执行成功。

## 对应现有操作逻辑

队列项统一转成现有 `staged` 状态，去掉 `run_id`。页面的发送调度需用户
明确继续，刷新不会提升到 `pending`。保留文本、图片、顺序、priority 和
profile；原 `discard_images` 工作清空，避免恢复后继续旧删除意图。

草稿提交意图转成现有 `rejected` 审核状态，刷新不能自动变成 `prepared`
或 POST 队列。这里使用既有操作逻辑，并不把恢复解释为服务端拒绝过请求；
后续正式恢复预览须显示其来源及待确认含义。草稿本身的文本、图片和
composer profile 保留，revision 从 1 开始。旧 direct-run admission 关联
解除，其不确定事实保留在 `DirectRunAdmissionUncertain` 中。

同 ID 的队列与草稿必须有同一 payload：队列文本与浏览器 `String.trim()`
后的草稿文本匹配，图片顺序、priority/interrupt 及完整 profile 一致。
Unicode 空白按实际浏览器 trim 集合处理，只用于比较，不修改原意图文本。
匹配的草稿副本不再另行提交，等待确认输入只保留一份；已受理副本也不再
入队。不匹配时整体失败，不能猜测应该保留哪份输入。

## 拥有、配额和追溯合同

输入必须是成功拥有解码的 v2。最多 20 个队列项和 20 个草稿意图，Facts
最多 40 条 source/review ID 映射，另记录 accepted/duplicate/review 数量、
旧 discard 数量和 direct-run 不确定性。新 ID 避开所有源输入 ID、保留
回执路径、历史来源/审核 ID 和本轮生成的其他 ID；重试最多 16 次，取消与碰撞失败不返回部分
Facts。错误 Size 不写调用方对象，其他失败仅保留 Size。

对已有执行侧车只重编码存在的 `queue.json` 和 `draft.json`，两者缺失时不凭空创建。
转换同时新增/追加 `restore-inputs.json`，保留精确源意图、metadata 及映射。
模型 snapshot/journal、UI 日志、metadata、图片原字节/完整名称、artifact、
todo/反馈和全部历史回执保持原字节。源备份不可变，能独立释放及重新尝试。
结果用 `MdoSessionBackupRelease` 释放。

所有步骤共享一个新的 30 秒协作预算；拥有复制每 64 KiB 检查取消，JSON
替换共用文件/总字节预算。输出重新通过真实 queue/draft codec、通用 schema/
CRC/资源引用及保留历史关系检查。配额不允许为转换结果自动增大，不能用
丢弃尾部输入的方式让预算通过。一次原生 JSON 或 UTF-8 比较不能被抢占，
需在有界 worker 使用，协作截止时间不等于进程硬超时。

**原输入字节及映射已由转换副本持久携带。** 详见
[来源合同](session-backup-input-provenance.md)。后续身份重绑定、恢复 worker
及原子发布仍未实现，不能单凭来源文件存在发布。
正式恢复预览及跨 Home 导出/导入闭环继续实施，不能把此 API 冒充它们。

## 有界证据

扩展已有 staging HTTP/TLS 探针。源模型/UI/inline PNG 来自真实三轮 mdo
本地模型 run；队列起动/受理样例为受控历史数据，schema 3 正向关联到该
保留主 Agent start，不能把手写回执称为真实队列派发的证明。

核对 schema 1 受理、schema 2/3 无证据起动、schema 3 对应保留 start、
无回执 sending、pending/staged、prepared/posting、重复和矛盾 payload、
Unicode trim、完整 profile、有效静态 PNG 和名称。读取实际新副本的独立
磁盘字节，逐文件核对其他文件不变；释放原 decoded 及转换副本后 Stage
还能独立重查。原备份前后编码一致，整个源 Home 原字节不变。

`tests/fixtures/backup-review-ui.mjs` 直接消费该 C 转换后落盘的 sidecar，
使用实际 draft-store、prompt-queue、submission-controller。队列存在以及
队列为空两种刷新/reconcile/pump 情形均只有 GET：没有 POST/PUT/DELETE、
受理、提升、回执消费或运行调度。验证的是实际控制器逻辑；队列渲染处于
后台会话的空选择状态，没有新增 DOM/原生点击/实体设备验收。

另覆盖固定 20+20 API 容量保留、旧 v1 拒绝、缺失 sidecar 不创建、预取消、
生成首个 ID 后取消、截止时间、16 次碰撞失败、转换后总字节增长失败、
NULL Error、Size 小对象保护及失败后重试。初始探针更正了图片元数据
`file_name` 字段；实际转换暴露的自身 ID 碰撞误判已修复并重新验证。
初始失败日志保留，没有放宽产品校验。没有压力或高负载测试。

最终 Windows/Linux 有界门禁通过 115 Python、252 Node、90 JS 模块解析、
严格 C11、40 runtime、三项 packed 和独立 A/B；新增检查扩展已有 staging
runtime，探针数量未增加。Windows 另通过便携 WebView2 Home 与 20 秒
打包启动。SDK 未变化，复用已验证 native host；Linux 新 ext4 staging 中
十份代码/清单/探针输入与 Windows 按 LF 归一化核对。
根目录 `mdo.exe` 更新为 6,402,466 字节、SHA-256
`c2c76096fd0973a1d2ac371ae3e8074cf96fd9fa2c2b714585b30084ac6d261f`；
Linux A/B 为 6,452,626 字节、SHA-256
`49a769bbb80dd8a9d4d9ec4b93431b271bb669e339acbcdd9a946698371d6f41`。
日志 `.build/qa-inputs-{windows,linux}-final.log`。本轮没有新增实体设备
证据，Linux queued HEAD reset 仍单独待排查。
