# 完整会话备份导入页面

2026-10-03。原生 HTML/JavaScript，无 npm 构建步骤。入口为侧栏底部以及
桌面/移动端会话菜单中的“导入会话备份”。导入与旧目录迁移是两个操作。

## 用户流程

1. 选择一个最多 96 MiB 的完整 JSON 备份。浏览器用纯 JavaScript SHA-256
   分块读取，再以最多 256 KiB 的二进制片段上传。整个传输最多 120 秒；
   不在浏览器整体解析大 JSON，不依赖 HTTPS Web Crypto digest。
2. 服务器预检 schema、模型历史/UI 关系及图片。页面展示来源身份、模型/
   Agent/协议、微秒时间戳转换后的日期、保留文件/字节/界面记录和未验证
   历史引用。旧 v1 部分导出只能预览，不能进入完整恢复审核。
3. 用户选择目标项目并“核对目标”。服务器捕获实际 workspace、项目版本、
   源 SHA 和固定的新请求/会话 ID；来源 workspace 不能自动作为目标。
   审核页先展示目标，再展示来源；页头/页脚固定，中间详情独立滚动。
4. 用户点击“确认恢复”。页面先把 ID、项目和源 SHA 写入当前 URL，再
   对审核的固定 ID apply。URL 身份保存失败则不发送 apply。项目变化
   不能由确认偷偷重新捕获；必须丢弃原审核后重新审核。
5. 后台结果明确区分 committed、aborted、pending、not_accepted 和未知。
   完成后用户主动“打开恢复的会话”，先刷新 catalog，再跳转到新会话。
   完成不会自动替换原会话/未发送草稿，不发送草稿、执行队列或调用模型。

`session-backup-import.js` 负责 DOM、焦点、语言和导航；独立
`backup-import-controller.js` 负责状态/身份/取消；`api/backup-import.js`
负责有界上传与窄路由调用。二进制传输共用原页面 write token、项目清理
写入 guard 和 pending-write 计数，不能绕过已有恢复冻结。

## 响应丢失和取消

URL 的 `mdoRestore`、`mdoRestoreProject`、`mdoRestoreSha` 是查询线索，
不授予执行权限。保留原 search/hash/history state；刷新只 GET 同一个
ID，不能自动 apply、重新审核或创建另一份上传。查询响应的 ID/项目/SHA
必须完全匹配保留的身份。每次请求最多观察 10 秒，一轮观察最多 60 秒；
超时显示未知，保留原 ID供用户查询。不能以 404 或网络错误判断未执行。

关闭/Esc 只隐藏窗口；显式“取消此操作”才请求 server token，然后重新
查询结果。取消与提交同时发生仍显示 committed。更换未提交目标要 DELETE
审核再 GET 证明它已消失；若其他页面同时接受，则保留该 ID 并查询结果，
不能创建另一请求。过时上传、响应或原生 close 事件不能覆盖新的页面状态。

未开始恢复的上传/预览可显式清理后另选文件；丢失 seal 响应不能把仍占有
上传的输入框伪装为可选择。结果未确定、cleanup pending 或需要重启时不能
清除原身份。已知终态可通过“另选备份”确认并释放 resident pin、预览/上传；
持久 receipt 和已恢复会话仍保留。主动作“打开会话”目前仍保留查询线索。

无 URL 线索时，打开窗口可只读发现同进程 resident 审核/预览，找回响应
丢失的准备操作。新增 `GET/HEAD /api/v1/session-backups/restores`：有
resident 返回其小型事实，否则 `{empty:true}`；不扫描历史 receipts、不
查找任意来源文件、不批准恢复。发现与 DELETE 并发可能返回 404，应查询。

新开原生窗口/更换地址时应保留原 URL 或请求 ID；当前没有跨进程的“最近
导入”页面索引。Home 的结果独立持久化，原 ID 查询在重启后仍有效。原生
窗口重开后自动找回提示，以及打开成功后的确认/清理体验继续迭代。

## 归档合同和验收范围

归档/上传/preview 的 `restore_ready:false` 继续保留：孤立归档或预检并不
包含目标授权。正式页面依赖成功的 v2 semantic preview 和服务器产生的
固定审核，而不是把 manifest 标志改成 true 来批准执行。恢复执行的唯一
入口仍是用户确认后的 `/restores/{id}/apply`。

状态/传输/弹窗测试覆盖同 ID 恢复、未知 404、不匹配身份、无法保存 URL、
审核/接受交错、取消后迟到响应、legacy partial、观察期限及清理冻结。
真实 HTTP/TLS 前端探针用完整模型/UI/图片/草稿/队列和 2 MiB 产物，丢弃
首个实际 apply 响应，重建前端 controller 后查到同一已提交 ID；目标一次
出现、队列 staged、原始来源字节不变，未启动模型运行。

Windows 隔离单文件候选打包页使用实际文件选择器完成预览、目标审核、
确认/刷新找回原结果、Esc 返回原草稿、移动端菜单重新打开和显式打开新
会话；消息按钮/时间/token 速率及“等待继续发送”队列均可见。本地模型
代理统计本次导入/打开期间 run POST 为零，无浏览器 error/warn。
320×240 实际 DOM 尺寸为窗口 286×216，确认按钮 78×40 且位于视口内。
该候选比最终代码少了一项“缺失字节总数不显示为零”的修正；本次完整
result 实际携带该字段，已验收控件不受它影响。截图和只读回执在
`.build/qa-backup-import-{review-320x240,committed-320x240,restored-desktop}.jpg`、
`.build/qa-backup-import-live-proof.json` 与 `live-helper.log`。

这不是实体移动端、IME/软键盘或原生 WebView 完整点击验收，也不是其他
平台图形验收。无压力或高负载测试。完整双平台 release gate 和最终包
记录见迁移记录；原有 Linux queued HEAD reset 等间歇问题仍独立保留。
