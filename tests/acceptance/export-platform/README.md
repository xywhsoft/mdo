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

限制：真实 Android 文件选择器、保存落盘和剪贴板仍待真机验证；没有安装或发布新程序，不修改锁定的 XS 依赖。

## 已提交源码的独立打包补充

修复提交 `ee47023` 后，从干净工作区重新构建精简 Windows 宿主与 `mdo-export-final.exe`，未纳入主工作区其他未提交更改。记录见 `builds.json`。依赖锁、宿主配置及二进制哈希均已校验，七个内置前端文件与提交源码一致。

该候选版通过真实 HTTP/TCC 恢复关联与重启围栏、历史编辑幂等重放、内置 VFS 完整备份/上传/预览/重启检查。真实工作台完成一轮回环模型回复和正式备份导出交接，一次模型请求，一次成功终态，零最终错误或浏览器控制台错误。`packed-conversation.jpg` 展示候选版工作台。

剪贴板组件再次确认复制后的逆向文字选择与按钮焦点保持；内置浏览器使用独立虚拟剪贴板，真实 Ctrl+V 核对被连接器拒绝，系统剪贴板内容仍未确认。`clipboard-selection-browser.html` 增加普通浏览器可手动使用的粘贴核对区域，不改变生产复制行为。

候选程序位于 `.build/conversation-acceptance/mdo-export-final.exe`，没有覆盖根目录日常版本或公开发布。实际下载落盘、Android 保存/粘贴以及同签名覆盖升级继续留待验证。
