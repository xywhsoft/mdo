# 会话备份离线预览 API

2026-10-02。生产入口检查拥有上传字节后的 schema、模型重放/UI 关系与静态
图片像素，没有恢复写入、模型驱动、工具调用、队列执行或远程图片下载。
本接口是完整恢复的前置检查，`restore_ready` 始终为 `false`；正式恢复事务和
页面入口按 [实施记录](session-backup-plan.md) 继续开发。

## 使用顺序

1. 通过现有 uploads 接口分段上传并 seal；96 MiB 文档、256 KiB chunk 和
   transport SHA-256 合同不变。
2. 空 body `POST /api/v1/session-backups/uploads/{upload}/preview` 返回
   `202` 与独立的 32 位小写 hex 预览 ID。当前已接受或保留的预览在上传
   ID **及已 seal 的 SHA-256 都相同**时返回 `200` 同一个预览，不重复执行。
   对已移除的上传重试返回 `404`；已有结果仍可通过预览 ID 获取。
3. `GET /api/v1/session-backups/previews/{preview}` 查询状态与小摘要。
   `GET /api/v1/session-backups/previews` 返回 `preview` 或 `null`，并给出
   worker/时间预算。两种查询均支持 HEAD、OPTIONS。
4. 空 body `DELETE /api/v1/session-backups/previews/{preview}` 取消运行中的
   检查，或释放已完成的拥有备份。取消在工作边界协作生效，不能抢占单个
   parser/hash/codec 的原生 CPU 循环。仅在清理完成后变为 terminal；小终态
   回执继续可查询，到下一次预览替换或过期。重复 DELETE 保持可重试。
5. 一个已保留的成功结果会占用唯一预览槽，须先 DELETE 或等待过期才能
   检查其他内容。失败/取消没有备份 payload，可直接再次 POST；这会创建
   新预览 ID，并替换上次小回执。恢复尚未实现，客户端不能发起 apply。

POST/DELETE 使用当前 `X-Mdo-Write-Token`；无 token 返回 428，陈旧 token
遵循公共写入协议。缺失/过期上传或预览返回 404；未 seal 上传返回 409
`backup_upload_incomplete`；另一个活动/保留/清理槽返回 409
`backup_preview_busy`；资源不足返回 503 `backup_preview_unavailable`。
所有接口拒绝非空或 transfer-encoding body，ID 非法返回 400。OPTIONS
由路由统一处理；不改变普通 API 256 KiB 的 body/response 上限。

## 返回字段

状态摘要包含 `id`、`upload_id`、`sha256`、`state`、`phase`、
`completed_steps/total_steps`、时间、`terminal`、`cancel_requested`、
`discarded`、`result_available`、`error_code/message`。`state` 为 pending、
running、succeeded、failed、cancelled。phase 为 queued、decode、model_ui、
images、done；三个 completed steps 表示完成了哪些门禁，不能当作 CPU
用时百分比。错误消息有失败文件/图片身份，页面须按普通文本显示。

只有 succeeded 才带 `result`：版本、源身份/title/model/Agent/workspace、
实际文件数/字节、保留 UI 范围、侧车与模型/UI 的未验证/已移除/无投影
数量、附件/内嵌/未验证图片数、累计 RGBA 与峰值 codec 内存。源 workspace
只是来源文本，不能自动作为写入目标。原 profile 不保证当前可运行，继续
对话前必须重新选择有效 profile。

`validation: schema-model-ui-images` 只说明这些离线门禁成功。
`legacy_partial:true` 指 v1 只有 meta/snapshot；未验证引用和缺失投影原样
报告，不伪造完整历史。`result_available:true` 只说明拥有备份仍在内存槽
中。DELETE 后保留的 succeeded 回执可继续显示已检查的摘要，但
`discarded:true/result_available:false`，不能再使用它发起后续事务。

## 生命周期与预算

只有一个按需创建的 worker，队列限额 1，且槽在 Run/Drop 全过程排他。
没有每次 POST 创建一个线程，也不排队展开多个大型 JSON。decode、模型/UI、
图片门禁共享从接受开始的 30 秒协作 deadline；模型/UI 和图片分别通过
现有独立库门禁重放，没有新写一个替代模型账本的状态机。

worker decode 完即释放 transport pin，只保留解码后的独立字节；网络回调
不解析/重放/解码。删除或过期上传可以在 pin 释放后回收原 96 MiB 槽，之后
检查仍使用自己的备份。返回成功只保留至多 64 MiB 文件字节，不保留模型
对象、RGBA、请求/连接或 Home/runtime 句柄。解析树、模型临时对象和 codec
使用各自既有预算，这些字节上限不是进程 RSS 的硬上限。

终态结果五分钟后不可见；在下一次访问/退出时实际回收，不设空转定时器。
回收也占有 admission，防止旧 payload 清理尚未完成时启动新解码。序列化
和大对象释放都在小状态 mutex 之外。取消与成功发布竞争，以发布前已
接受的取消优先；跳过 Run 的任务也由 Drop 释放 pin。任务提交可能同步
调用 Drop，故提交前发布 ownership，在非递归状态锁之外提交，成功后不再
解引用 job。拒绝提交时由调用方使用同一清理/终态发布路径，保留小失败回执。

xs 停止该代请求后调用 API Unit：取消并 join 预览池，再释放上传存储和
其他 manager，最后允许 TCC 代码卸载。此生命周期合同不能用超时 detach
替代；真实原生解码/文件操作不可抢占时，Unit 等待其返回。
