# Web 工具合同

MDO-6E 把联网搜索实现为 xwork 标准工具，而不是 HTTP 路由或浏览器自动化。
Agent 目录会看到三个独立工具：

- `web_search` 查询搜索服务并返回 URL、标题、摘要、来源与抓取时间；
- `web_open` 获取一个页面，提取有界 UTF-8 文本并返回进程内 `document_id`；
- `web_find` 在已打开文档中查找有界上下文，不再次联网。

所有返回的搜索摘要和页面正文都带有 `"untrusted": true`。它们是引用材料，
不能改变 Agent 的系统指令、权限或工具策略。浏览器点击、表单、截图和登录态
属于另一组交互工具，不与这里的无状态 HTTP 获取合并。

## 配置

内置 `settings.web` 提供以下产品边界：

```json
{
  "allow_http": false,
  "allow_private_networks": false,
  "timeout_ms": 30000,
  "idle_timeout_ms": 10000,
  "max_response_bytes": 1048576,
  "max_text_bytes": 262144,
  "max_documents": 16
}
```

联网工具仅在存在可用登录会话且 `settings.agent.web_search` 为真时发布给 Agent。
短期访问令牌过期但可刷新时保留工具；有效刷新失败、退出账号或关闭 Agent 开关后，
新的任务不再看到这组工具。账号状态同步只在可用性变化时替换目录，保留已有文档缓存。
`settings.web.enabled` 已移除，旧文件、导入及进程覆盖中的该字段都会清理，
不能再暗中覆盖界面开关。
「网络」设置页配置模型请求的代理与证书。联网搜索没有独立地址选项，
使用原生账号管理器的服务来源 `https://ai.xywhsoft.com`。旧配置中的 `web.search`
在读取和导入时移除，不会仅为启动而写入 Home。下次正常保存配置会写入清理后的 patch。

`web_search` 向账号服务发送 `POST /api/v1/search`，正文为 `{ "query": "...", "count": 5 }`，
count 可省略，由服务端决定默认结果数；显式 count 允许 1–10，服务端可进一步限制。
接收 xadmin 的 `{ code: 0, data: { provider, request_id, count, truncated, results } }`，
结果保留 title、url、snippet、site、published_at 和 fetched_at，空数组为成功。
外部结果不可信；HTML 验证页、错误封装、畸形字段和不安全 URL 均不伪装成搜索成功。

博查和 z.ai 选择、平台密钥、账户校验和配额由 xadmin 管理。客户端既不保存平台密钥，
也不自动回退到另一个搜索平台。搜索只使用网站登录建立的原生账号会话：
未登录时等待用户登录，允许跳过或取消；访问令牌留在原生管理器中，前端、配置、
工具参数和模型都不能提供令牌。旧 `MDO_SEARCH_ACCESS_TOKEN` 环境变量不再生效。
账号的加密保存、刷新、切换、独立退出和等待逻辑见 `account-login.md`。

搜索请求禁止重定向，明确的 401 允许一次账号刷新。账号服务的新错误封装在
`data.error` 返回固定 `code`、`retry_safe`、`dispatched`、`retry_after_ms`。
只有已知的提交前忙碌、分钟限流、数据库锁或工作线程不可用，且明确
`retry_safe:true` / `dispatched:false` 时，才在一次工具调用内指数退避。
最多六次请求，共享一分钟与调用者的较短期限；遵守正文及 Retry-After 的较大
最小等待时间，等待可取消。中间尝试不产生模型工具结果、产物或账号错误通知。

日额度与全站日额度耗尽、联系方式验证、平台未配置、平台鉴权和平台限流各有
独立最终说明。未知或不一致封装只按状态给出保守提示，不假设 503 必然是缺 key。
网络断开、超时或已经提交平台的失败不自动重放 POST，避免重复消耗额度；
平台失败也不会被当作会员登录失效。原始服务和上游错误正文不回显。

联调测试：`python tests/test_search_api_runtime.py` 使用本地模拟 HTTP 服务，不消耗平台额度；
`python tests/test_search_recovery.py --host <兼容 xs>` 验证有限退避、等待、取消和
不可重放情况；`python tests/test_search_xadmin_integration.py --host <兼容 xs>`
使用 home 的统一 mdo 插件隔离副本、真实 JWT 和上游模拟传输，核查准确原因、
提交次数及额度事务。网站宿主默认 home/xs.exe，可用 `--website-host` 单独指定。

## 网络与资源边界

普通网页默认只接受 HTTPS URL，拒绝 userinfo、控制字符、反斜杠和非法端口。
搜索结果保留网页链接中的 fragment（`#` 后的定位片段）；`web_open` 校验完整链接后，
在权限资源与 HTTP 请求中统一移除 fragment，因为定位片段只由浏览器使用。
服务地址与配置 URL 继续使用不允许 fragment 的严格校验。
mdo 对每次请求设置总超时、空闲超时、正文上限、最多五次重定向和协作取消。
xs 的 `XS_FETCH_PUBLIC_ADDRESSES_ONLY` 在 DNS 查询工作线程中筛掉回环、私网、
链路本地、共享、文档、基准、过渡、组播和保留地址；TCP Dial 只能看到过滤后
的不可变地址列表，重定向继续使用同一策略。这一位置避免了“先检查 URL、后
重新解析域名”造成的 DNS 重绑定窗口。

只有显式把 `allow_private_networks` 改为真时才会关闭地址过滤；网络 effect
仍进入 xwork 的结构化权限请求。`web_search` 声明 read、network、
external-service 和 secrets 四种 effect，分别描述账号搜索端点、xadmin.search 和
原生账号的会员令牌引用；`web_open` 声明 read 与 network；`web_find` 是纯 read。

## 文档缓存与提取

缓存只存在于当前进程内，使用单调不复用的文档 ID 和固定条目上限，满时淘汰
最早文档。它不在 Home 中创建文件，也不会把网页数据混入会话配置。HTML
提取跳过 comment、script、style、noscript 和 SVG 内容，规范化空白并解码
常用及数字实体；非 UTF-8 或非文本响应会被拒绝。标题、正文、查找结果数量、
上下文字节和最终工具输出都各有硬上限。

传输接口可注入仅用于确定性功能测试。生产入口固定使用 `xsFetch`，注入接口
沿用同一请求/响应所有权合同，不能改变工具 schema 或权限声明。

## 本次回归

2026-10-05，实际会话 `a80f8d35cb7a6cb2766c643371fc1f59` 的 31 次 `exec` 调用确认来自
旧 `web.enabled=false` 导致工具未发布。已移除该配置及设置 API 的对应字段，
保留 Agent 页面可见开关；默认 Agent 和搜索工具说明优先引导使用 `web_search`，
避免通过 shell 猜测搜索端点。

隔离回归覆盖旧字段加载/导入/进程覆盖、启动不改写原文件、正常保存清理字段，
以及未登录、登录后启用、关闭开关、重新启用、退出账号和目录无需重复发布。
打包 VFS 回归覆盖真实设置页和配置保存。线上通过原生 xwork 执行
`郑源 一万个理由 官方 歌词 页面` 查询，博查返回 2 条结果，测试会话随后独立退出。

实现已提交于 `9fece69`。Windows 根目录 `mdo.exe` 与 ARM64 APK 均已重新构建，
APK 版本号为 `20261008`，已覆盖安装到实测手机。手机保留原有登录状态，
Agent 设置页显示「仅登录账号后可用，Agent 按权限策略调用联网搜索工具。」
Windows 严格编译、打包 VFS、账号与工具目录、配置迁移及前端设置回归均通过；
Android APK 的签名和 16 KiB 对齐检查通过。

同日，新会话 `a6fb8b102a172bcf8d9e0be9740e7136` 已能调用搜索，但连续返回
`invalid xadmin response`。线上博查结果包含酷狗 `#` 分享链接，客户端把正常的
定位片段误判为非法 URL，导致整批结果失败。现已区分网页链接与服务地址的校验，
搜索保留定位片段，网页读取和权限检查使用统一的无片段地址。回归先复现旧解析失败，
再验证混合结果、HTTP 分享链接、拒绝非法控制字符及嵌入凭据；原生
`web_search` 使用原查询「郑源 一万个理由 歌词」（不传 count）成功返回 8 条博查结果。
严格编译、搜索与网页运行时、打包配置回归通过。Windows 程序已更新并重新打开，
Android APK `20261009` 通过签名与 16 KiB 对齐检查，已覆盖安装并启动。
