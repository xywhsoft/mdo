# 搜索安静恢复与准确错误

2026-10-09。本阶段不访问外部搜索或模型，不更新日用程序、手机或线上网站。
客户端提交 `4172f718`，home 统一网站插件提交 `ec458f53`。

已修复：429 将额度耗尽与并发混在一起，503 将数据库/工作线程不可用误说成
缺少平台 Key；服务凭据被平台拒绝现在不会被误解为会员需要重新登录。
固定错误原因通过 `data.error` 返回，只认可已知且明确未提交的平台前拒绝。
可恢复情况在一次工具调用内最多六次指数退避、共享一分钟/调用者期限，
遵守更长的 Retry-After 和 API 最小等待；取消停止后续请求。
日额度、平台鉴权等不可恢复情况只给一个准确的工具结果，模型仍可继续。
网络不确定或已提交的平台失败不盲目重放，当前仍按原合同消耗一次额度。

`native.json`：25 个原生 xs/TCC/xwork 与本地 HTTP 案例。恢复、响应头最小
等待、分钟/日限额、取消、短期限、六次耗尽，以及未知、不完整、矛盾、已
提交封装、断连和畸形 HTTP。实际 POST 数、间隔和每工具一次权限检查均验证；
模型看到每项唯一最终结果，原始错误正文不回显。

`website.json`：只读备份真实主库到隔离 vhost，测试会员 JWT、假平台密钥和
统一 mdo 插件真实额度数据库。博查/z.ai 各一次；平台失败各一次且准确扣减。
同账号忙碌时三个客户端请求，预算锁定时两个请求，最终各只提交平台一次、
扣额度一次；另一会员受全站并发限制，分钟/个人日/全站日拒绝均无平台调用。
全部仅九次模拟平台提交，两条短时重叠请求用于状态验证，不进行负载测试。

`session-recovered.json` / `session-quota.json`：干净已提交 app 的真实会话、
xwork 执行、消息账本和持久 UI 事件。连续两次限流后唯一成功搜索完成事件，
随后文件读取与 Agent 正常结束；日额度耗尽仅一条失败工具事件，无搜索正文
产物，Agent 仍能正常结束。模型完成回调是离线模拟，不属于真实模型或浏览器
界面验收。`page-regression.json` 保留公共等待函数改动后的网页恢复回归。

`candidate.json`：锁定 xs `153315a`，干净 mdo `4172f718` 候选构建，VFS
启动、旧搜索配置迁移和账号开关测试通过。候选
`.build/conversation-acceptance/search-recovery/package/mdo.exe`，约 4.50 MB。
网页模块三个源码文件与候选来源核对一致；其他聊天未提交的修改未打入候选。
其他有限检查见 `regression.json`。Android、真实浏览器、外部平台及上线仍待
后续验证；POST 不确定结果的幂等恢复也尚未实现。

复现：

```powershell
python tests/test_search_recovery.py --host <兼容 xs>
python tests/test_search_xadmin_integration.py --host <兼容 xs> --website-host D:\GIT\home\xs.exe
python tests/test_web_result_session_runtime.py --host <兼容 xs> --search-recovery
python tests/test_web_result_session_runtime.py --host <兼容 xs> --search-denied
python tests/test_search_packed.py --packed <隔离候选>
```

测试副本必须保持隔离。D 盘一度耗尽；删除本轮旧副本的操作被自动审批拒绝
（无具体原因），随后已将这五份停止的副本保留移动到 C 盘临时归档，未删除。
