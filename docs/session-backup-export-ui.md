# 正式会话 JSON 完整备份导出

2026-10-02：会话菜单的“导出 JSON 备份”现调用现有完整 v2 下载入口。
保留原来的一次点击操作；新增接收/校验进度、取消、错误说明与同会话重试。
Markdown 快捷键和入口保持原操作。旧 `/export` v1 API 保留兼容，但正式
JSON 菜单不再使用它。完整恢复入口尚未开放，`restore_ready:false` 保持。

## 内容与接收边界

`state/sessions.js` 将原项目/会话交给独立 `api/backup-download.js`。
服务端捕获已保存且仍保留的模型历史、UI 事件、附件原字节/名称、产物、
待办、反馈、草稿和队列。页面明确说明未保存输入及已清理历史不包含在内。
下载不修改来源；旧 v1 快照不因入口换名变成完整备份。

前端接收使用共享 30 秒截止时间，最大 96 MiB；要求 HTTP 200、精确
Content-Length、JSON MIME 和完整 SHA-256 ETag。逐块限额后复制接收
字节，增量计算校验；每 256 KiB 让出一次执行以响应取消。错误响应也
最多接收 16 KiB。短包、超量、错误 MIME/状态/哈希、超时或取消均不创建
下载文件，并释放 reader/controller/listener/timer。

`utils/sha256.js` 是小型独立增量实现，避免手机通过普通 HTTP 访问时依赖
安全上下文 Web Crypto；Node 使用独立原生 SHA-256 核对标准向量、分块
和填充边界。这里只验证传输完整性，不给备份内容提供额外恢复资格。
校验成功才构造 Blob，没有在页面解析大 JSON 或重复展开 base64 文件。

## 弹层生命周期

`features/sessions/session-backup-export.js` 只管理一项活动导出。会话 ID、
项目和标题在打开时复制，重复入口不能改变归属，重试使用原会话。取消
会立即终止请求并关闭弹层，旧进度、结果和原生迟到 close 事件不能取消
或交付新任务。成功提示为“已交给浏览器下载”，不声称磁盘保存成功。

失败保留说明和重试入口。错误位于滚动内容的开头，原生焦点会将其显示
出来；头尾保持可见，按钮至少高 40px。中英俄文案随语言预览更新，
不丢失活动请求或错误。弹层关闭后回到原入口，入口失效时回到输入框。
原生 HTML/CSS/JS 随单文件打包，不增加包管理器或前端构建依赖。

## 本轮证据

- 两平台通过 115 Python、262 Node、93 个 JS 模块解析、严格 C11、44
  runtime、三项 packed 和独立 A/B；Windows 另通过便携 WebView2 Home
  及 20 秒启动。SDK 不变，复用已验证 host；21 份改动输入按 LF 核对。
- 新 HTTP/TLS 探针实际调用生产前端下载器，将完整文件保存到独立临时
  路径，再交给真实 C 解码/模型历史检查。保留 UI/模型、68 字节 PNG/
  原名、草稿、待发输入及 2 MiB 普通产物，来源 inventory 字节相同。
  TLS 绕过仅在本机隔离夹具的 Node 子进程；未改变产品证书策略。
- 打包 Home `.build/mdo-packed-docks-3d838zkl` 实际选择 PNG 并发送一次
  本地模型回合。首次导出模拟 503 后错误获焦；320×240 工作区的错误
  y=101–139.375、内容区 y=89–162，重试按钮 y=176–216。键盘重试交付
  10,684 字节 v2 Blob，含模型/UI 日志、原图和原名。截图
  `.build/backup-export-short-screen.jpg`，Blob 摘要
  `.build/qa-backup-export-browser-captures.json`。浏览器脚本错误为空。
- Home `.build/mdo-packed-docks-03tngijb` 仅延迟第一条 GET 五秒：等待时
  Esc 取消，焦点回菜单；重新导出总计两个 GET，只交付一份 v2 Blob，
  后续没有旧任务下载事件，没有发起模型/队列提交。Node 另定向覆盖
  快速 Esc/立即重开、旧 close/进度/成功到达及同会话重试。

首轮静态合同还在旧 client 文件找文件名解析器，已改为检查新下载器
和校验标记；小屏实测另发现错误会被裁切，已经修复。Windows 第一次
整轮的 Home 恢复探针看到半行 printf 就终止进程，导致记录被截断；
现等待完整换行记录，原业务断言不变，定向及两平台整轮重新通过。
初始日志和修复前日志均保留，没有把失败替换成成功记录。

本轮内嵌浏览器的 JSON 下载（观察页与独立正式页）以及既有 Markdown
对照都收到 `Page.downloadProgress state=canceled`、receivedBytes=0。
下载等待接口也超时，原始事件保存在
`.build/qa-backup-export-browser-*events.json`。没有返回文件路径，原因未确认，不能将本次浏览器交付
称为磁盘下载成功或将该取消宣称为已修复。后端/前端下载器的真实文件
读回证据独立成立；浏览器、原生 WebView 与实体设备下载仍须继续核对。

最终日志 `.build/qa-backup-export-{windows,linux}-final.log`；源输入核对
`.build/qa-backup-export-source-equivalence.log`。根目录程序 6,439,017
字节、SHA-256
`5797aaf6f1d7b84bb80357b99f8b0a743404f9cf0b601dec00270b91c129bc81`；
Linux A/B 6,489,177 字节、SHA-256
`2d125b7e1d937a704161dae040005f2e03b1e02678d2214441d38d6999067c65`。
更新时没有根目录窗口，用户 Home 保持不变。没有压力或高负载测试。

正式恢复 worker、持久请求结果、预览/确认/导入页面、实体设备验收和
既有 Linux queued HEAD reset/大响应间歇失败仍按完成审计继续处理。
