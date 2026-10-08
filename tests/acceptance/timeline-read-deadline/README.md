# 会话历史的静默读取恢复

基线 `5d4ce11`。首次历史快照、后台增量同步、旧消息分页及跳转读取原先直接等待 HTTP，没有请求期限。一个不返回的响应可能一直占住初始化或历史加载按钮；缓存同步遇到暂时错误时也会立即发布 syncError。

`timeline-store.js` 的历史 GET 统一经过已有 `createRequestRecovery`，每次八秒、最多六次、每个 GET 的恢复预算一分钟，带抖动的指数退避。自动恢复期间不发布中途错误；旧消息与已有实时推送继续保留。明确权限拒绝等永久错误仍保留准确原因。此处只重复读取，不重放输入、模型调用或工具。

选择变化、清空、页面退出和进入后台会释放当前等待，包括不响应 AbortSignal 的传输；返回页面后按当前选择恢复读取。退出时也释放旧消息按钮的 loading 状态。既有项目/会话、版本、历史 epoch、游标、校验和及实时合并规则保留。开始主动刷新时取消旧轮询计时，避免其另开重复刷新。每个 GET 具有自己的预算，不能把多页历史读取称为整批共享一分钟。

## 确定性检查

首批八项新检查在产品修复前是一项通过、七项失败；修复后全部通过。再增加后台冷启动恢复和旧协议事件回放的两项检查，最终十项全部通过。覆盖正文解析不响应取消、后台挂起、六次耗尽仅一个最终读取错误、切换与清空立即释放、旧消息重试期间保留实时推送、页面退出后重新加载、准确权限错误、进入后台再恢复，以及 snapshot 路由缺失后的 events 回放。

部分旧检查更新为等待实际 HTTP 开始、驱动真实读取退避预算，而非假定 API 同步开始或第一次失败就返回。另发现 `test_timeline_replay.mjs` 的待办重试只控制 window 时钟，而共享 store 使用全局时钟；该单项在未修改的隔离基线也失败。修正测试时钟范围后通过，未放宽产品断言。

以下 13 个实际文件共 92 项检查通过；四项前端契约检查通过。

```powershell
node --test tests/test_timeline_read_deadline.mjs tests/test_live_timeline.mjs tests/test_conversation_snapshot.mjs tests/test_timeline_replay.mjs tests/test_session_cache.mjs tests/test_timeline_resume.mjs tests/test_timeline_terminal.mjs tests/test_timeline_interleaving.mjs tests/test_timeline_history_i18n.mjs tests/test_timeline_search_position.mjs tests/test_conversation_history.mjs tests/test_live_connection.mjs tests/test_todo_refresh.mjs
```

## 真实打包页面

候选使用匹配的 xs `3232f7b8efb02fdd6bf63a508ed1e69654d6759c` 与构建收据。隔离树仅复制本阶段产品变更，没有产品 QA 注入。候选哈希、大小及四个实际服务资源与隔离源码逐字节一致的记录见 `build.json`。

`manual_timeline_deadline_qa.py` 使用真实 Windows 打包程序、HTTP/TCC、单独 Home 和回环模型。先生成一条固定回复与一份保存草稿，再分别把首次 conversation 快照及后续后台增量 GET 的原生成功回复延迟十二秒。WebSocket 不可用，真实历史刷新走 HTTP。

首次加载在 8.468 秒、后台同步在 8.594 秒开始下一次读取，均早于旧回复的十二秒释放。页面保留固定历史及 `TIMELINE_SAVED_DRAFT`；发送保存草稿后，再在第二会话发送 `TIMELINE_FOLLOWUP` 正常结束。证明见 `proof.json`、`checks.json`。三次模型请求由一条测试准备输入与两条有意浏览器输入构成，各一次调用、三个成功终态、零最终模型错误、空队列和空草稿。后台路径的 17 次读取还包含恢复后正常低频轮询，不能冒充 17 次故障重试。

`browser.json` 的采集点没有错误 alert，警告/错误日志为空。`cold-recovered.png`、`background-recovered.png`、`follow-up.png` 是采集结果；首个名为 cold-loading 的观察实际已恢复，不能把它当作在途等待截图。不报中途错误和退出取消另由确定性检查确认，未声称逐帧采集或安卓原生验证完成。

只保存隔离候选，没有更新日用程序、手机或官网。无压力或高负载测试。长期任务仍在进行；实时令牌重连探测的自身等待、多个最终状态读取提示的归并及其他原生平台体验还需继续核查。
