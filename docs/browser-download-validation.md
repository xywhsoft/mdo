# 浏览器下载交付复验

2026-10-02，产品基线 `2f5e1ab`。前一阶段同一内嵌浏览器曾将 JSON 和
Markdown 下载取消，原因尚未确认。本轮保留失败记录，使用独立对照及
未修改的产品包补充下载完成和交付内容证据。

## 复验方法

运行 `python tests/manual_packed_docks_qa.py --packed mdo.exe
--image-capable --export-download-fixture`。显式 QA 开关才提供以下页面：

- `/__qa/download-control`：固定 33 字节测试文字，HTTP 附件、同步 Blob
  和半秒延迟 Blob 三种入口；不加载 mdo 前端或读取会话。
- `/__qa/export-download`：保留正式工作台 iframe、原始对象 URL 与下载
  动作，只观察交付 Blob。v2 的小 UI 文件经 UTF-8 解码核对输入/回复，
  附件 metadata 读回原名；不展开大产物。Markdown 图片用浏览器原生
  SHA-256 与前一份 v2 的图片哈希逐一核对，避免硬编码另一份 PNG。

每次激活前保存事件游标，按同一个 GUID 核对 `Page.downloadWillBegin`
和 `Page.downloadProgress` 的字节及终态。下载等待接口在独立 Blob
已收到 33/33 `completed` 时仍超时，因此该接口超时不是下载失败判据。
它也未返回磁盘路径。本轮未改变浏览器下载策略、产品配置或证书策略。

## 实际结果

| 场景 | 文件 | 字节与终态 | 交付内容 |
| --- | --- | --- | --- |
| Windows 单文件服务，960px 正式页 | JSON v2 | 10,684/10,684，completed | 观察页另读回 3,093 字节 UI、输入/回复、68 字节 PNG、fixture.png |
| 同一正式页 | Markdown | 451/451，completed | 正式导出入口，页面原回复及图片仍可操作 |
| Linux ext4 单文件服务，320×350 浏览器工作区 | JSON v2 | 10,774/10,774，completed | 3,092 字节 UI、输入/回复、68 字节 PNG、fixture.png |
| 同一小屏页 | Markdown | 450/450，completed | 一张 68 字节内嵌图，SHA 与前一份 v2 相同 |
| 独立同步/延迟 Blob 对照 | TXT | 各 33/33，completed | 固定测试文字 |
| 独立 HTTP 附件对照 | TXT | canceled，receivedBytes=0 | 保留失败，不推断与先前产品取消同因 |

Windows Home `.build/mdo-packed-docks-xtun15e9`；Linux Home
`/home/ubuntu/.cache/mdo-linux-backup-export-3oes7ch8/.build/mdo-packed-docks-ksm0vfu_`。
两者各只执行一次本地模型回合并上传一张合成 PNG。原图 SHA-256 为
`4da89a37a8f8c0048c4bbf732baeab36393b615f243010ed9e56a98b7546991e`。
下载 GUID、文件清单和 UI/像素/名称核对在
`.build/qa-download-control-{windows,linux}-*events.json`、`*captures.json`。
Linux 小屏截图 `.build/qa-download-control-linux-hash.jpg`；Windows 正式
页截图 `.build/qa-download-control-windows-direct.jpg`。

Windows 观察页重载时记录一条无堆栈 `MutationObserver.observe` 错误；
未取得调用来源，不能仅凭同文异常归因。另开未包装的正式会话页，两个
导出均完成，脚本错误为空。Linux 首次 JSON 交付检查也无新增脚本错误。
保留 `.build/qa-download-control-*-console.json`，不宣称所有夹具无错误。

本轮结果证明上述正常浏览器交付完成及 v2 原图/名称/UI 内容，不证明
此前取消已修复、磁盘文件独立读回、Linux 原生 WebView、macOS 或实体
手机通过。恢复 worker、持久结果与导入确认页继续待办，
`restore_ready:false` 保持。

## 验证与发布

本轮只改 QA 夹具和文档，产品资源不变。115 Python、262 Node、93 个
前端模块检查通过；手动脚本在 Windows/Linux 编译通过。两系统重新
打包与上一阶段通过严格 C11、44 runtime、三项 packed、独立 A/B 和
Windows 便携窗口/20 秒启动的包逐字一致。本轮不重复计数这些旧门禁。

Windows 根目录程序重新生成，6,439,017 字节、SHA-256
`5797aaf6f1d7b84bb80357b99f8b0a743404f9cf0b601dec00270b91c129bc81`；
Linux 为 6,489,177 字节、SHA-256
`2d125b7e1d937a704161dae040005f2e03b1e02678d2214441d38d6999067c65`。
构建记录 `.build/qa-download-control-{windows,linux}-repack.log`，
其余结果 `.build/qa-download-control-{python,node,web}.log`。没有压力或
高负载测试，不改变用户 Home。
