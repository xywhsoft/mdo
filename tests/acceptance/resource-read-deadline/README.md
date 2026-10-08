# 资源读取拥有自己的超时等待

基线 `bf5b857`。已启用只读恢复的资源存储原先只在八秒后发出 abort，然后继续等待 loader。传输适配器或响应正文解析若不响应取消，首次加载和页面退出后的旧 Promise 都不会结束，退避重试也无法开始。

`createResourceStore` 现在同时结束自身等待。保留原有同步 loader 调用、每次八秒、六次尝试共享一分钟、静默退避、旧数据保留及耗尽后最终提示。新读取、reset、权威数据和页面退出会释放旧等待；旧请求迟到的结果或拒绝不会应用到新状态。未启用 recoverRead 的普通存储保持原行为。

## 确定性检查

新文件 `test_resource_read_deadline.mjs` 的七项检查在修复前是一项通过、六项失败，修复后全部通过。包含拒绝 abort 的 loader、真实 API 客户端的正文解析挂起、完整一分钟预算、页面退出与恢复、旧新读取交叠、权威状态取消和明确权限拒绝。没有使用长时间真实等待。

最终运行以下九个实际存在的文件，共 100 项通过；四项前端契约检查也通过。

```powershell
node --test tests/test_resource_read_deadline.mjs tests/test_catalog_read_recovery.mjs tests/test_session_read_recovery.mjs tests/test_decision_read_recovery.mjs tests/test_task_read_recovery.mjs tests/test_settings_state.mjs tests/test_todo_refresh.mjs tests/test_run_poll_read.mjs tests/test_run_stop_controller.mjs
```

## 真实打包验证

隔离候选只复制本阶段产品变更，使用匹配的 xs `3232f7b8efb02fdd6bf63a508ed1e69654d6759c` 与已验证收据。候选 SHA-256、大小以及四个 HTTP 资源与隔离源码逐字节一致的结果见 `build.json`。没有产品 QA 注入。

`manual_resource_deadline_qa.py` 启动真实 Windows 打包程序、单独 Home、真实草稿/队列/运行存储和回环模型。代理捕获模型列表、设置、会话列表及会话详情的首个原生成功响应，将其延迟十二秒；后续请求正常转发。WebSocket 不可用，正常兜底刷新仍在工作。

会话列表在 8.594 秒、设置在 8.578 秒开始下一次读取，早于旧回复的 12.015 秒释放，证明真实浏览器的超时退避继续工作。模型列表和会话详情的下一次读取分别在 0.047 秒和 0.078 秒开始，由其他正常启动读取覆盖了首个等待；不能把这两个时间冒充八秒超时证明。拒绝取消的 JavaScript 等待由前述确定性检查覆盖，浏览器原生 fetch 本身会响应 abort。

页面恢复后保留 `SAVED_RESOURCE_DEADLINE_DRAFT`，可以直接发送；切换到另一个会话发送 `RESOURCE_RECOVERY_FOLLOWUP` 也正常结束。`proof.json` 和 `checks.json` 确认两个有意输入各执行一次、两个成功终态、零最终模型错误、空队列和空草稿。`browser.json` 保存页面采集点的空错误 alert、空警告/错误日志。采集点没有覆盖每一帧，中途不报错另由确定性状态检查确认。截图为 `recovered-draft.png` 和 `follow-up.png`。

只保存隔离候选，没有覆盖日用程序、安装手机或发布官网。无压力或高负载测试。长期任务仍在进行；Android 原生中断恢复、独立状态读取耗尽时的提示归并，以及此前备份检查偶发目录差异还需继续核查。
