# 搜索响应丢失的持久恢复

2026-10-09。客户端实现 `66d65df5`，统一网站插件 `9e7fbfda`；干净测试源码
`efd23d05`（后两次客户端提交只改测试）。仅隔离环境，不访问外部搜索或模型，
本阶段没有发布日用程序、APK 或网站。

新增 `POST /api/v1/search/requests`。会员、随机编号和精确请求正文哈希组成持久
记录，与三份额度一起预留，平台 I/O 不跨事务；规范化成功/固定平台失败先保存
再回复。同键重取不再调用平台或扣减额度，处理中返回匹配编号的待取结果状态。
客户端在一次工具执行中用同编号、同正文安静指数退避，最多六次，共享一分钟
及调用者较短期限。仅确定新路由不存在且没有提交不确定性时可兼容旧接口。

`baseline.json`：旧客户端 `424ffa61` 接到一次已提交但响应丢失的搜索后直接失败；
平台一次、额度一次。仅覆盖旧客户端，不声称所有新边界都在旧源逐项执行。

`receipts.json`：11 组真实网站插件/JWT/额度场景，包括相同正文重取、精确正文
冲突、登录及账号隔离、固定平台错误缓存、一个受控处理中重取、额度回滚、
网站重启、未保存结果、正文过期及清理、逻辑正文池满后恢复。容量检查仅写入
31 条合成预留元数据，不分配大正文、不发送负载。所有拒绝无新平台调用与扣减。
键保留 24 小时，正文最多十分钟；容量紧张可清除两分钟前的正文，键仍保留。

`lost-response.json`：实际原生 xs/TCC/xwork，经本地故障代理访问真实网站插件。
十组有限案例：丢响应、截断、网关 503、处理中断连、网站重启后取回，以及
不确定后禁止旧接口回退、旧接口断连不重放、待取编号/成功编号不匹配、兼容
回退不突破六次。五种恢复均成功，各只有一次平台提交和一次额度扣减。
每组只有一次权限检查、一个最终工具结果和一次最终统计；重试不产生额外产物。

`native.json`：25 组原生退避、明确错误、期限、取消及最终失败检查；断连现在
按同编号尝试六次，其余永久失败仍只请求一次。`website.json` 保留两种平台和
原额度/预算锁分类回归，合计九次模拟平台调用；`page-regression.json` 保留公共
等待函数的 20 组网页回归。

`session-lost.json` / `session-recovered.json` / `session-quota.json`：干净完整 app
的真实会话账本和 UI 事件。丢响应、连续两次限流恢复均只有一个搜索完成事件，
随后读取与 Agent 正常结束；额度耗尽只有一个准确失败工具结果，Agent 仍正常
结束。模型完成回调及此处搜索 HTTP 响应为本地模拟，不冒充真实模型、真实
浏览器或真实外部搜索验收；平台提交与额度的证据来自上述独立真实插件链路。

`candidate.json`：干净 `66d65df5`、锁定 xs `153315a` 的独立 Windows 候选，
4,506,418 字节；两个修改后的生产文件与打包来源逐字节一致。VFS 启动、搜索
配置迁移、保存和账号开关通过。其他有限回归见 `regression.json`。

尚待完成：客户端未完成工具编号持久化与 mdo 进程重启后的恢复；真实浏览器、
Android、外部服务及部署检查。本阶段保证一次工具执行及网站重启后的同编号
重取，不能扩大为客户端进程重启后的去重；已提交平台但未保存结果仍诚实终止，
不会换新编号自动再次收费。没有压力测试或高负载测试。

复现（干净 worktree 可指定网站库路径）：

```powershell
$env:MDO_TEST_WEBSITE_REPO = 'D:\GIT\home'
python tests/test_search_receipts.py --host <兼容 xs> --website-host D:\GIT\home\xs.exe
python tests/test_search_lost_response.py --host <兼容 xs> --website-host D:\GIT\home\xs.exe
python tests/test_search_recovery.py --host <兼容 xs>
python tests/test_web_result_session_runtime.py --host <兼容 xs> --search-lost
python tests/test_search_packed.py --packed <隔离候选>
```
