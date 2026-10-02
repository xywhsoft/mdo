# 恢复输入的持久来源记录

2026-10-02：输入转换副本现在自动携带 `restore-inputs.json`。私有 Stage
排他落盘、读回与清理均将它作为普通 owned 文件处理；释放原上传、原备份
和转换副本后，Stage 仍独立拥有来源数据。后续普通 v2 capture/encode/decode
会继续携带并验证该文件，不依赖浏览器缓存、外部数据库或其他目录。

这是输入追溯的一环。[明确目标的离线准备](session-backup-restore-preparation.md)
现已重建 metadata/UI 身份和便携 artifact 路径。生产恢复 worker、真实
项目/workspace 验证、异常退出回收、非覆盖原子发布、catalog 和正式导入
页面仍待接入，`restore_ready:false` 保持。

## 便携文件合同

`restore-inputs.json` 的 schema 1 包含 `imports` 数组。每个条目包含：

- `captured_at_us`：源备份的捕获时间；
- `source_files`：原 `meta.json` 和存在的 `queue.json`、`draft.json`；
- `inputs`：source/review ID、disposition 与不确定受理标记；
- `direct_run_admission_uncertain`：源草稿的直接运行不确定性；
- `cleared_discard_images`：源队列中被解除的删除意图数量。

源文件记录是 `{path, bytes, sha256, data}`。`data` 是精确 UTF-8 字节的
JSON 字符串表示，没有重新序列化源文件；读取字符串后按 UTF-8 编码即可
取得原字节。保留源 metadata 的旧身份、workspace、fork/profile/revision
等全部内容及原始意图文本、图片 ID、状态与 profile。源路径是历史描述，
不能被用作恢复目标、文件打开参数或执行指令。

每次实际转换只追加条目，先前条目的语义、源字节与校验和保持。provenance
文件外层会重新编码，不宣称外层 JSON 排版的原字节稳定。没有 queue/draft
时不新增条目；已有历史仍携带，没有历史时不创建空文件。

## 证据边界

SHA-256 证明记录与其声明的源字节一致，不能认证谁创建了文件。历史
disposition 是描述性数据，不能替代当前 queue/receipt 和保留 start 证据。
即使过去条目声称 accepted，当前 staged 输入仍需用户明确确认，不能据此
消费、提升或执行。历史图片 ID 只是原意图内容，后续用户已删除的图片不
需要为被动来源记录重新下载或恢复。

再次转换仍未确认的输入时，按先前 review ID 继承不确定受理标记。只有
当前可靠的受理证据能产生 accepted；历史不确定性只能要求继续审核。
直接运行的不确定性没有可重排的 input ID，保留在历史条目与源草稿中。
后续正式预览必须显示这份历史；当前草稿清除 admission 标志不能被解释
为过去的运行已撤销或已确认。
新 ID 还避开历史 source/review ID、当前源输入、保留回执和本轮其他 ID。
历史值树每次操作只解析一次用于碰撞查找，不为每次随机尝试重解析全文。

## 校验、预算和失败

共用备份验证器检查该文件，拒绝未知 schema/字段、空/超长历史、重复或
越界源文件路径、声明长度失真、内层 SHA 不符、无效 UTF-8/NUL，以及
metadata/queue/draft 的真实 codec 错误。重新计算外层备份 SHA 不能绕过
内层检查。映射必须完整对应源输入的有序并集，目标 ID 互不重复且不重用
源 ID；disposition/队列来源、accepted 状态、posting/sending 不确定性、
直接 admission 和 discard 数量必须一致。同 ID 的源 queue/draft payload
仍复用实际浏览器 trim/profile/image 比较规则。

最多 16 个历史条目，文件上限 8 MiB，同时受 caller 较小的文件/总字节/
文件数预算约束。达到上限明确失败，不截断历史、不提高配额。每份源文件
仍受真实 64/256 KiB codec 上限约束，条目和源文件之间检查取消与同一个
30 秒协作截止时间；每条目的映射扫描至多 40 项。单次 JSON/SHA 操作
不能被抢占。验证逐条目复用一个
拥有读取器，释放全部 queue/draft 及中间分配。

原备份保持不可变；失败释放整个副本，公开 Facts 清空，仅保留 Size。
新 ID、侧车及来源记录未完成时不返回部分成功，也没有向 Home/catalog
发布。最终事务仍必须保持 worker 生命周期、身份/路径重绑定和持久事务
所有权，不能以“来源文件已存在”代替完整发布门禁。

## 有界证据

已有 HTTP/TLS staging 探针新增精确源字节/SHA、独立磁盘读回、释放上传/
原对象后重查、原生 encode/decode 全文件原字节 round-trip、二次转换保留
历史与继承不确定性、固定 16 条历史的追加失败/17 条拒绝、内层校验和/
真实 codec/映射矛盾，以及首份源文件 hash 后取消、文件数和总字节增长
失败/重试。修改过去 accepted 描述不能提升或消费当前 staged 输入，
现有前端控制器的刷新/reconcile/pump 仍只有 GET。

模型/UI/inline PNG 来自真实三轮本地 run；受理/起动回执仍是受控历史夹具，
不冒充真实队列派发或原生点击证明。初始测试通过网络回调同步发送大导出
响应遇到传输超时，已改为进程内原生编码/解码逐文件核对并仅返回小结果；
生产有界下载实现未改变，初始日志保留。没有压力或高负载测试，没有新增
DOM、原生或实体设备证据。

首次完整门禁在两平台的旧会话探针中发现手写 TCC 源文件清单遗漏新的
来源验证器，报 `MdoBackupInputsArchiveValid` 未定义。补齐探针清单，
并将共享 payload 比较拆成独立纯函数模块。拆分后的首次定向探针还暴露
本地生成 unity 清单未刷新；重新生成后，会话及 staging 定向探针通过。
这些初始失败日志保留在 `.build/qa-input-origin-*-initial.log`，不能作为
最终通过证据。

最终 Windows/Linux 门禁通过 115 Python、252 Node、90 JS 模块、严格 C11、
40 runtime、三项 packed 和独立 A/B。Windows 另通过便携 WebView2 Home
及 20 秒打包启动；首次打开窗口创建 `mdo-home` 符合用户的便携优先选择。
SDK 未变化，复用已验证 native host。Linux 新 ext4 staging 的十二份代码/
清单/探针输入与 Windows 按 LF 归一化核对。根目录程序更新为 6,407,289
字节、SHA-256
`d909eac42fb3f7c6457dcf64457af89e715f1a3616431902c4c71e80c9473403`；
Linux A/B 为 6,457,449 字节、SHA-256
`c8de9cc2f786c3354a1d03709f91c5ff203b8485705554dc74d0c1d1889928af`。
最终日志 `.build/qa-input-origin-{windows,linux}-final.log`，源码核对记录
`.build/qa-input-origin-source-equivalence.log`；发布时没有运行的根目录窗口。
完整门禁之后仅删除新纯函数文件的一行末尾空行，代码内容不变。两平台
再次通过严格 C11、独立 A/B 及三项 packed，Windows 另重过便携 Home 和
20 秒启动；最终字节来自 `.build/qa-input-origin-{windows,linux}-post-format.log`。
完整行为门禁日志保留，没有重复全部 40 项运行探针。
