# WebSocket 实时通信

聊天页面使用同端口的 `/api/v1/live` WebSocket。发送消息、停止运行、设置、附件、
历史读取继续使用 HTTP。无需增加服务端口、运行程序、JS 依赖或配置项。

## 数据流

会话事件成功写入 JSONL 后，将同一条记录投递到有容量上限的内存环形缓冲。
模型线程只复制和通知，不发送网络数据。连接工作线程每 25 ms 合并一批最多
32 条事件，沿用 HTTP 的事件序列化、文本截断和附件规则。页面沿用原有时间线
合并和渲染逻辑，工具、思考、时间、token 用量和消息操作保持一致。

每个页面最多一个连接，可以切换订阅。连接建立和重连时，订阅携带当前会话的
最后 `event_id`，先从 JSONL 补齐，再接收内存事件。读取历史之前捕获内存水位，
读取期间新增的事件仍会被投递；页面按 ID 去重。订阅的 `selection` 编号用于
拒绝切换会话后迟到的旧响应，包括离开后再次返回同一会话的情况。

权限审核、ask、运行状态和 HTTP 修改通过 `changed` 通知触发一次资源读取。
页面合并通知，并串行执行刷新批次，避免一串工具事件制造重叠请求。
连接正常时停止消息、运行、审核和会话状态的周期轮询。后台任务仍保留
30 秒的状态核对，覆盖没有关联会话事件的外部 Shell 状态变化。

## 连接和生命周期

- HTTP 和 WebSocket 使用同一来源；HTTPS 页面自动使用 WSS。
- 握手必须携带精确匹配的 Origin 和页面启动令牌。令牌放在子协议中，
  不放在 URL。协商协议为 `mdo.live.v1`；该连接不接受任何写操作。
- 重连从 0.5 秒开始指数退避，最多 15 秒并带随机抖动。断线时恢复低频
  HTTP 读取。服务端每 20 秒发送心跳；页面检测静默连接并重新建立。
- 页面切到后台、关闭或进入页面缓存时释放连接；返回前台后重新订阅补齐。
  这也适用于 Android WebView 的后台/前台切换。
- 最多 8 个连接，内存事件缓存最多 512 条且不超过 2 MiB。缓存被淘汰时，
  客户端从最后的持久化事件编号补齐。入站消息最多 4 KiB，发送等待最多 2 秒，
  不响应心跳的连接 60 秒后释放。慢客户端不会阻塞其他连接或模型线程。
- 后端使用 xs 的 `XS_TAKEOVER` 拉取接口和 xrt 的 RFC 6455 握手、帧、
  分片及 UTF-8 校验。保留 xs 的连接事件表，让它正确释放 TCC 代际引用。
  退出时先取消并等待连接工作线程，再停止模型等事件生产者，最后释放缓存。

JSONL 仍是持久化事实来源；不另建 WebSocket 日志或数据库。当前改动保持原有
同步落盘策略，不改变会话恢复和导出的持久化保证。

## 开发位置

- `app/src/api/live.c`：连接、环形缓冲、补齐、帧及资源通知。
- `app/src/sessions/events.c` / `manager.c`：已提交事件的可选观察回调。
- `app/src/approvals/manager.c`、`asks/manager.c`、`runs/manager.c`：可选状态观察回调。
- `app/web/js/api/live.js`：页面连接、订阅、心跳和重连。
- `app/web/js/features/chat/timeline-store.js`：推送/HTTP 共用时间线。
- `app/web/js/app.js`：资源刷新和前台/后台生命周期。

## 验证

```powershell
node --test tests/test_live_connection.mjs tests/test_live_timeline.mjs
python tests/test_live_runtime.py --host .build/host/xs.exe
python tests/test_live_runtime.py --packed .build/mdo-ws.exe
```

运行探针使用本地模拟模型，检查握手隔离、分片和 ping、完成前的流式输出、
断线续传、HTTP 数据一致性、运行完成、ask、权限审核、错误帧和关闭。
发布检查 `tools/qa_release.py` 已包含开发与打包版探针。无需访问线上模型，
不执行压力或高负载测试。
