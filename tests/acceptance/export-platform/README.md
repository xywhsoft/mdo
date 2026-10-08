# 导出平台限制与下载桥接验收

2026-10-08，本阶段修复两个日常导出问题：

- 锁定的 XS Android 保存桥接最多接受 8 MiB，而会话备份和 Markdown 可能更大。旧前端先提示下载交接成功，再由原生层报错。现在在创建 Blob URL 和点击下载之前检查桥接限制，保留导出面板与原内容，显示准确的限制说明。桌面和普通浏览器不增加此限制。
- 商店导出的插件包使用未接入文档的链接，Android 文档级点击监听无法收到事件。会话、扩展文件和插件包现在共用下载入口，链接先接入文档再点击，并为浏览器接管 Blob 保留 60 秒。

`acceptance.json` 是真实浏览器中的正式组件验证，Android 桥接由隔离页面模拟。13 字节 Blob 的点击到达文档监听器，中文文件名、连接状态和原文读回正确；超过上限的备份在中、英、俄三种语言下均保留面板，焦点落到错误说明，没有成功通知，原会话内容不变。`android-limit-zh.jpg` 展示正式导出面板。

自动验证：

```powershell
node --test tests/test_file_download.mjs tests/test_backup_export_dialog.mjs tests/test_frontend_i18n.mjs tests/test_clipboard.mjs tests/test_session_export_full_text.mjs tests/test_session_export_images.mjs tests/test_session_export_i18n.mjs
python tests/test_frontend_contract.py
```

26 项 Node 检查与 20 项前端合约通过。上限用大小元数据验证，没有生成大文件或进行压力测试；完整备份 HTTP/TLS 传输及还原此前已独立验证。

重现页面：复制 `app/web` 到独立临时目录，把 `tests/fixtures/file-download-browser.html` 放到其根目录，以 `python -m http.server --bind 127.0.0.1 --directory <临时目录>` 启动后访问该文件。该页面使用正式模块、语言包及样式，模拟监听器只读小文件，不写手机文件。

限制：真实 Android 文件选择器、保存落盘和剪贴板仍待真机验证；本阶段没有生成或发布新安装包，不修改锁定的 XS 依赖。
