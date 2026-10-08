# 被实时推送替代的历史刷新立即结束

基线 `835f419`。实时推送、更晚的 HTTP 刷新或重新订阅原先只增加 refreshVersion，使旧回复无法写回页面；旧请求的等待和自动重试仍运行。一个不响应取消的历史传输可能继续占用一分钟，即使会话早已通过实时通道恢复。

timeline-store.js 为增量刷新单独维护 AbortController。接受同会话合法推送、重新订阅或新刷新时，立即释放被替代的 GET 或退避等待；切换会话、清空及页面退出也释放它。保留版本、epoch 和游标规则。旧消息分页与索引读取继续使用独立的会话选择生命周期，实时更新不会取消用户主动加载旧消息。读取开始前再次检查取消，避免已失效的微任务发起 HTTP。

## 检查

八项新测试在修复前一项通过、七项失败；修复后全部通过。覆盖忽略 abort 的传输立即释放、退避中取消、连续刷新、健康通道重新订阅、错误归属及非法游标不能取消合法刷新、旧消息读取独立保留、取消早于 HTTP，以及旧协议 events 回放。十四个实际会话相关文件共 100 项 Node 检查通过，四项前端契约通过。

```powershell
node --test tests/test_timeline_refresh_ownership.mjs tests/test_timeline_read_deadline.mjs tests/test_live_timeline.mjs tests/test_conversation_snapshot.mjs tests/test_timeline_replay.mjs tests/test_session_cache.mjs tests/test_timeline_resume.mjs tests/test_timeline_terminal.mjs tests/test_timeline_interleaving.mjs tests/test_timeline_history_i18n.mjs tests/test_timeline_search_position.mjs tests/test_conversation_history.mjs tests/test_live_connection.mjs tests/test_todo_refresh.mjs
```

## 原生打包验证

manual_live_probe_qa.py 增加可选 --slow-replay，复用已有真实 WebSocket 透传和独立 Home，不注入产品 JS。该模式先生成一条固定回复，断开实时连接一次，同时把令牌探测和增量历史的原生成功回复各延迟十二秒。

实时连接在历史读取开始后的 7.125 秒重新打开，早于该 GET 八秒期限和十二秒回复释放。增量历史只读取一次，没有后续重试；两个实际 WebSocket 握手均为 101。历史、保存草稿和恢复后编辑都保留，第一次浏览器发送及切换另一会话发送均成功。三个模型调用包括一次准备输入和两次有意浏览器输入，各执行一次；三个成功终态、零最终模型错误，空队列与草稿，无遗留运行。详见 proof.json、checks.json、browser.json。

界面采集实际在恢复后进行，不能把采集动作称为断线期间编辑验证；时间依据保留在 clock-observation.json。取消忽略 abort 的传输及微任务由确定性测试验证，原生浏览器验证真实恢复与无后续历史重试。截图展示恢复后页面，未声称逐帧时延或安卓原生后台恢复完成。

候选在隔离工作树构建，匹配 xs 3232f7b8efb02fdd6bf63a508ed1e69654d6759c 和构建收据。四个真实 HTTP 服务资源与隔离源码逐字节一致；build.json 保存候选哈希及大小。只保存隔离候选，没有更新日用程序、APK 或官网。无压力或高负载测试，长期任务继续进行。
