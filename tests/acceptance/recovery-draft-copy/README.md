# 恢复面板草稿复制兼容

2026-10-10，基线 `0de302ea`。恢复面板的复制入口复用 `utils/clipboard.js`；没有新增剪贴板实现或修改草稿数据。

在隔离的 [来源组件页面](../../fixtures/recovery-draft-copy-browser.html) 中禁用异步 Clipboard API，挂接真实恢复面板和三种语言的合成草稿。旧入口点击“复制未发送草稿”显示 `Cannot read properties of undefined (reading 'writeText')`；修复后调用已有兼容路径，页面显示“未发送草稿已复制”，见 [截图](source-component.png)。没有启动产品服务、调用模型或读取个人会话。

继续观察真实浏览器 `copy` 事件，发现 `c9d0809d` 的兼容路径虽返回成功，却没有选中完整草稿，见 [修复前](modal-before.png)。原因是 `showModal()` 使弹层外的页面不可交互，而临时文本框插入在 `document.body`。帮助函数现优先将文本框插入当前焦点所在的已打开 dialog；其他页面仍用 body。修复后同一真实复制事件选中的三种语言、换行及全部序列化草稿与预期一致，原按钮恢复焦点，见 [修复后](modal-after.png)。观察器不提供或修改剪贴板数据，选中原文通过仍不代表系统粘贴已通过。

39 项已有剪贴板、恢复状态及服务读取检查通过，164 个前端模块解析和可达检查通过：

```powershell
node --test tests/test_clipboard.mjs tests/test_project_purge_recovery.mjs tests/test_purge_service_read_recovery.mjs
node --experimental-vm-modules tools/check_web_modules.mjs
```

已有剪贴板检查覆盖复制的原文、异步接口拒绝后的兼容路径、选择和焦点恢复、两种复制方式均失败。页面测试只确认真实组件不再访问缺失接口，并由兼容路径返回成功。浏览器工具的虚拟剪贴板读取为空，Ctrl+V 被工具拒绝，因此**没有验证系统剪贴板或真正粘贴的内容**，也不把成功提示作为这两项通过的证据。首次静态服务器没有映射语言目录导致的加载失败属于夹具问题，已纠正，不属于产品故障。

手动复查时，以仓库根提供 HTTP 文件，并将 `/lang/` 映射到 `app/web/lang/`；打开上述页面，复制后关闭弹层，在核对框执行真实系统粘贴。仅在三种语言、换行及草稿字段完全一致时确认。页面不放入产品 VFS。Android WebView 和 Windows WebView2 的实际复制仍列于 [剩余验收](../../../docs/conversation-stability-acceptance.md)。没有压力或高负载测试。
