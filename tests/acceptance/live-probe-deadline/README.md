# 实时连接令牌探测的等待与取消

基线 `3bd9e8a`。WebSocket 重连原先直接等待令牌刷新，默认实现只在八秒后 abort。如果传输或正文解析不响应取消，tokenCheck 会一直占住重连。暂停、停止和更换运行实例也无法释放这个等待。

`live.js` 使用既有 createRequestRecovery 为单次只读探测提供自身八秒期限；重连外层仍负责带抖动的指数退避，基础间隔上限十五秒，没有嵌套重试。暂停、停止、更换实例立即取消旧探测。运行实例变更通知中的观察者若停止连接，旧探测不能继续宣布可达。刷新仅更新读取订阅令牌，页面写入令牌保持原有隔离规则，不能重放旧输入或工具。

## 确定性检查

八项新检查在修复前全部失败，修复后全部通过。覆盖不响应取消的传输、暂停和停止释放、新连接不被旧探测清理、HTTP 开始前取消、迟到拒绝、不返回探测的退避上限，以及通知观察者停止。最后一项使用实际导出的 liveConnection、实际 api.get 与不返回的 Response.json，仅替换传输与页面时钟，确认探测取消以及读取令牌更新后写入令牌不变。

旧连接测试改为等待实际异步请求开始，而非固定两个微任务。以下七个实际文件共 75 项检查通过；四项前端契约通过。假时钟验证不构成压力或高负载测试。

```powershell
node --test tests/test_live_probe_deadline.mjs tests/test_live_connection.mjs tests/test_live_timeline.mjs tests/test_timeline_read_deadline.mjs tests/test_run_poll_read.mjs tests/test_run_stop_controller.mjs tests/test_runtime_read_recovery.mjs
```

## 真实打包页面

候选在隔离工作树构建，使用匹配的 xs `3232f7b8efb02fdd6bf63a508ed1e69654d6759c` 和构建收据；产品没有 QA 注入。四个实际 HTTP 服务资源与隔离源码逐字节一致，候选哈希及大小见 build.json。

manual_live_probe_qa.py 使用标准库代理透传真实 WebSocket 握手和双向字节，不伪造实时事件。独立 Home、回环模型和真实 Windows 打包程序启动后，断开一次实时连接，并把随后一次 project-purge-intent 的原生成功响应延迟十二秒。下一次探测在 9.157 秒开始，早于旧回复释放；真实连接再次返回 101 并传输两次回复。两条有意浏览器输入各执行一次，两个成功终态，零最终模型错误，空队列与草稿，无遗留运行。详见 proof.json 和 checks.json。

保存草稿保留，恢复后编辑发送，再切换第二会话发送均成功。browser.json 的警告与错误日志为空。名为 reconnecting-draft.png 的采集点实际已重连，不能把文件名或输入里的 LOCAL_DURING_RECONNECT 当作在途编辑证明；clock-observation.json 保留时间依据。不响应 abort 的正文解析和取消前不启动 HTTP 由确定性检查证明，原生浏览器本身遵从 abort。本阶段没有证明逐帧恢复时延或安卓原生后台恢复。

只保存隔离候选，没有替换日用程序、APK 或官网。长期任务仍进行，后续继续核查会话同步与其他基础操作。
