# 后台操作启动响应丢失验收

生产提交：`745c91b1`、`f932b658`。页面先生成操作编号，再提交一次；
响应断连、正文挂起或损坏时，只读取原编号。启动和观察共用一分钟期限，
每次请求八秒，读取沿用六次指数退避。尚未确认受理时，短暂的原编号 404
也可安静等待；确认后的真正 404 不再被当成延迟受理。

原包实际收到原生 202 后丢失响应头，浏览器自行重发，第二次得到 404，
页面显示服务器不存在。新增原生回归还复现已登记操作被忙碌的状态读取
隐藏。接口现在先返回同编号、同类型、同目标的原操作；不同目标或类型
返回冲突。管理器在锁内去重，读取已有操作无需分配新任务。

这不允许一般写请求自动重放。浏览器传输层可能自行重发，因此不能承诺
线上只出现一个 POST；确认的是这些相同编号不会重复执行后台任务。
操作记录仍是已有的进程内、最多 64 条历史；不声称进程重启、记录淘汰或
页面完全重载后的持久恢复，也不增加治理数据库。

## 真实打包页面

独立 Home 和回环模型，无外部模型/账号请求。实际修改的生产文件均来自包内，
没有通过浏览器注入替换 API。测试代理仅暂扣或丢弃真实原生响应。

| 场景 | 原生及页面结果 |
| --- | --- |
| MCP 响应头丢失 | 浏览器两个 POST 使用相同编号，原生只登记一个操作；成功发现一个工具，无错误 |
| 模块启动正文暂扣 | 八秒后读取原编号并确认编译成功，无最终错误 |
| MCP 延迟 18 秒受理 | 四次原编号 404 后 200；原 POST 只提交一次，无中途错误 |
| MCP 等待中离开设置 | 不取消后台工作；返回提供重新读取状态，一个 GET 确认原操作，无第二次启动 |

四个原生任务全部成功，实际传输五个 POST，其中一个为浏览器重复发送。
MCP 进程记录初始化一次、通知一次、工具发现三次，符合三个真实测试操作。
草稿 `OPERATION_START_FOLLOWUP` 全程保持，随后发送一次、收到一个完整回复、
一个成功完成事件、零 error。截图、DOM、原生操作和代理记录交叉核对。

959 项前端、162 个模块、124 项契约、严格 GCC、完整 API 和操作生命周期
检查通过；生命周期涵盖已完成/等待/取消的重复编号、冲突、格式与状态忙碌。
新增启动恢复行为检查覆盖断连、挂起、正文损坏、共享期限、未知受理、正常取消
及 GET-only 重新核对。原生忙碌场景保留修复前/后日志；初次 Node 基线同时
改用了新请求编号夹具，因此其全部失败数量不作为旧实现错误数量。

最终候选 `.build/conversation-acceptance/operation-start/final/mdo.exe`：
4,553,471 字节，比上一阶段增加 2,337 字节；
SHA-256 `e2779f7539ffa697248f3b6349249d30384c2928c8c8c01f678753dcbe8cadf4`。
来源 `f932b658`，构建时工作树干净；配套宿主未变。运行复制件和七份 VFS
源码逐字节核对。三个隔离夹具均正常 exit 0，两个测试页关闭。
没有压力或高负载测试，日用程序、APK、官网未更新。

## 重现

```powershell
python tests/manual_operation_start_qa.py --packed <候选mdo.exe> --directory <新隔离目录> --duration 1200
python tests/acceptance/resource-operations/setup_fixture.py --directory <隔离目录>
```

打开 state.json 的页面，填写草稿并进入 MCP 或 C 模块。点击前写 fault.json：
`{"path":"/api/v1/mcp/local/refresh","mode":"drop"}`。
mode 也支持 hold、delay、corrupt；模块路径为 `/api/v1/modules/reload`。
每条规则只消费一次，暂扣最多三十秒，也可创建 release-ack 提前释放。
测试后创建 stop，等待夹具结束，不操作日用 Home。

```powershell
python tests/acceptance/operation-start/check_evidence.py --exe .build/conversation-acceptance/operation-start/final/mdo.exe --running-copy .build/conversation-acceptance/operation-start/final-ui/upstream/mdo.exe
```

安卓设备本轮 adb 未检测到，实际 WebView、输入法、后台切换和其他功能仍
属于长期任务。浏览器一次等待观察超时后，重新读取同一页面确认成功；未因此
重新发起操作。
