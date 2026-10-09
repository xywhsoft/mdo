# 网页读取的安静恢复（2026-10-09）

产品提交 `5011ad6e`、`67ed55df`、`6bc03e4a`；xs 支持提交 `b9f3447`。
最终候选锁定 `153315a092b8c37d9d79f090122f81ed2bcc139d`，同时保留新合入的 Responses 续聊修复。
候选由干净的产品源码生成；没有混入日用工作区的其他未提交改动。

## 复现和行为

- `baseline.json`：旧版真实 xs/TCC 请求遇到一次 503 或提前断连，即返回失败；HTTP 错误没有状态与原因。
- `native.json`：修复后的 20 个有限原生案例。503、断连、正文截断恢复成功；429 数字/日期等待、重复/非法/溢出 Retry-After、拒绝/缺页、格式错误、响应大小、嵌套权限错误、调用期限和取消均检查。额外检查已过期的请求期限覆盖旧线程错误且零请求；短期限仅允许一次尝试时不会声称已经重试多次。
- 网页 GET 最多六次尝试，0.5 秒起步指数退避及正向抖动；共享一分钟恢复期限和调用者更短的期限，每次也遵守原来的请求/空闲超时。
- Retry-After 的服务端最低等待不被截短；超过剩余期限就返回准确的限流/暂时故障原因，不发送提前重试。取消退避不再发出后续请求。
- 只在最终结果创建文档和输出。20 次工具调用、20 次权限描述，得到七份文档；中途恢复不产生额外工具输出或缓存项。
- 403/404、策略拒绝、格式错误和过大响应不重试。常规网页失败交给模型选择其他路径，不因此终止整个会话。HTTP 错误正文及 URL 凭证不回显。
- 搜索是可能计费的 POST，继续只执行一次；一次明确的 401 凭证续期仍由原有账户流程处理。本次没有新增搜索 POST 自动重放。

## 完整会话与打包检查

`session-recovery.json` 验证实际会话、xwork 执行器、账本、UI 事件及结果文件：两次 503 后第三次网页请求成功，只有一个成功网页完成事件，随后读结果文件并正常结束。

`session-denied.json` 验证 403 只请求一次、只记录一个失败工具结果，模型回合随后正常结束，没有会话恢复围栏。失败工具仍保留一个已完成回执；它不是成功的网页文件或额外重试。

`session-search.json` 是原搜索到结果文件读取的回归。以上三项使用**模拟模型完成回调**，真实原生执行、HTTP 和持久化；本阶段没有对外部模型发出付费故障请求，也没有宣称完成真实浏览器、安卓或输入法验收。

另外通过：xs 最终响应头/v1-v2 前缀兼容/截断分类检查、搜索 API 鉴权/状态/UTF-8/不重试检查、网页 search/open/find、结果文件只读及四项 Web 契约。真实候选的打包 VFS 启动、旧设置迁移和登录门控检查通过。

`regression.json` 保存最终锁定宿主下的八个回归入口的独立退出码和输出。第一次会话夹具把后台账号查询误当 `/page`，以及把正常的失败回执误当多余文件，均已修正；最终记录不含这些夹具错误。

候选：`D:\GIT\mdo\.build\conversation-acceptance\web-open\package\mdo.exe`。
大小、SHA-256 与干净构建标记见 `candidate.json`。日用 mdo.exe、手机和官网没有被本次 QA 覆盖；未做压力或高负载测试。

## 复验入口

```powershell
python tests/test_web_open_recovery.py --host PATH_TO_NEW_XS
python tests/test_web_result_session_runtime.py --host PATH_TO_NEW_XS --page-recovery
python tests/test_web_result_session_runtime.py --host PATH_TO_NEW_XS --page-denied
python tests/test_search_api_runtime.py --host PATH_TO_NEW_XS
python tests/test_search_packed.py --packed PATH_TO_CANDIDATE
```

原版复现可用 `test_web_open_recovery.py --expect-baseline` 配合旧 xs 宿主；夹具从 `01894d08` 读取原网页实现，不修改产品文件。

长期任务继续：搜索侧额度/并发/上游暂时故障仍需更准确的结构化区分；安卓 WebView 和可信输入法验收及其余功能审查尚未全部完成。
