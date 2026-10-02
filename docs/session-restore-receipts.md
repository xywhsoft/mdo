# 单会话恢复的持久请求与结果

2026-10-03。正式恢复 Accept/Execute/Discard 与 HTTP worker 已接入这个
存储协议，见 [后台合同](session-restore-worker-api.md)；确认页面仍待接入。
`restore_ready:false` 保持。不能将此阶段计为完整导入操作已经可用。

## 请求身份和接受

`MdoHomeSessionRestoreBeginRequested` 复用单会话 journal，调用方必须已
审核并预留目标项目/会话。新的 32 位小写 hex 会话 ID 同时是请求 ID，
丢失结果后查询同一 ID，不重新生成。记录目标项目、revision/incarnation、
来源会话及传输 SHA-256、固定恢复时间；这些是追溯字段，不替代项目/
物理 workspace 复核、语义检查或用户授权。

创建 journal/payload 的真实目录并记录物理身份，在 immutable `owner`
中 flush 请求后才返回拥有 handle 和 parent anchor。带请求 owner 使用
版本 2，旧版本 1 的非 requested 事务保持兼容。caller 关闭所有 Stage
anchor 后消费 handle，退出/卸载前必须 drain。后续 worker 只有在此
接受成功后才能给出“已接受”的回应；接受后跳过 Run 也必须 End(false)。

接受前查询终态与当前 journal。同一 ID 无论 committed/aborted，或以后
删除了 target，都不能再执行。记录名额固定 1024，入场前有界检查，不
自动过期/驱逐旧身份来获得重试资格。服务端须明确报容量错误，不用新
ID 重试绕过丢失的回执。缺失查询也不证明此前没有执行。

## 提交事实与清理顺序

现有无覆盖目录 rename 是唯一提交点；ready 固定 Stage 的名称和物理
目录身份。journal 的原 payload 仍在表示未提交，payload 已消失且 live
target 为同一物理目录才能证明提交。未知/矛盾位置阻断恢复，不猜测。

End 和启动回收先检查实际位置，再 flush canonical 终态到 journal 内的
`result.tmp`，通过 no-replace rename 发布到
`data/session-restores/<请求ID>.json`，验证永久结果后才 retire/GC journal。
结果保存 request 和 committed/aborted；commit 带历史目录身份。它描述
历史事实，终态查询不再打开当前会话或项目，target 演变不会抹除记录。

提交后结果发布/清理失败，Committed 仍为真，Home 冻结写入并等待重启。
查询未发布终态的 journal 返回 pending 和独立 Committed 位置证据，
不能将 bool false 或 pending 翻译成“没有导入”。重启按同一位置证据补
终态，随后清理；不会重新材料化、更换目标或运行任何工具/队列。

GC 中即使 ready 已清掉，永久结果仍保留 Commit，不变成 rollback。
带请求的 GC 必须存在一致终态；committed 的私有 payload 不得重新出现。
不存在/冲突/损坏记录失败并保留证据。部分 `result.tmp` 只有与正确终态
canonical 字节严格为前缀时才能补写/清理；外来内容不能自动删除。
本协议覆盖进程中断，不新增断电时目录元数据持久性的承诺。

## 只读查询与命名空间

`MdoHomeSessionRestoreReceiptGet` 使用显式 Size，wrong Size 留输出不变；
其他失败清空事实并置 Found=false。缺失、pending、committed、aborted
不同；损坏与冲突是错误，不伪装成缺失。即使 Home 冻结仍可读取。不存在
的 Home 不因查询而创建，查询不会生成运行对象或驱动模型。

通用 Home 写入/rename 禁止修改 `data/session-restores`，包括大小写及
尾部空格/点的 native 别名和搬移 `data` 父目录；与项目 purge 回执共享
统一保护。文件仍在便携 Home 内，JSON 可直接用于排查与追溯。

## 有界证据和剩余接线

`test_restore_receipt_runtime.py` 使用真实 xs/TCC 和原生文件系统；每个
事务仅三个小文件。覆盖 wrong Size/缺失查询零写入、持久接受后的 pending、
commit/abort、提交后错误仍可查询、结果前后/GC/清理尾段进程退出、重复
ID、target 删除后的终态、空/部分正确/外来 scratch、冲突终态、坏结果、
namespace 别名与小配额。容量只在隔离源改为三条，没有制造 1024 个
文件或进行压力/高负载测试；所有暂停和故障只注入复制源。

首次专项夹具误用 rename 名称、冲突目录未允许已存在，已按实际合同修正；
完整门禁初次因多份手工源码复制漏新 leaf 而失败。新增共享
`runtime_sources.py`，自动带上 app 内真实 quoted include，SDK 仍由宿主
提供；不改变探针业务断言。初次日志保留。

后续 coordinator 的接受/执行句柄需要持有项目/session 预留、parent 和
预检 pin；先 durable accept 后响应，worker Run/Drop 都最终消费它。最终
项目 binding/Stage/一次 catalog 的现有边界继续使用。HTTP 结果须优先
重放该 ID 的持久事实，取消不能撤销已提交；确认页面持久保存同一 ID，
超时/断线只查询，不自动另造 ID。尚未接入这些路径，长期目标继续。

最终 Windows/Linux 通过 115 Python、262 Node、93 JS 模块、严格 C11、
45 runtime、三项 packed 和独立 A/B；Windows 另通过便携 WebView2 Home
与 20 秒启动。SDK@9bb75a5 未变化，复用已验证 native host；22 份改动
代码/探针按 LF 核对。根目录程序 6,445,190 字节、SHA-256
`36e6c72bbc63605a94428b1753761dc44d6ab336df6bc94472c4ef6a75980d5b`；
Linux A/B 6,495,350 字节、SHA-256
`1d874343e9bc31319fb6c7b5efe78e6eb5a313e7cd752843542628b780d3a288`。
日志 `.build/qa-restore-receipts-{windows,linux}-final.log`；初次失败保留在
`windows-initial.log`、`linux-initial.log`、`runtime-initial.log` 和
`runtime-recheck.log`。更新时没有根目录窗口，不改用户 Home；本阶段
没有新增 DOM/实体设备证据，也未关闭既有 Linux 间歇问题。
