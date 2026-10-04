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
  "enabled": true,
  "allow_http": false,
  "allow_private_networks": false,
  "timeout_ms": 30000,
  "idle_timeout_ms": 10000,
  "max_response_bytes": 1048576,
  "max_text_bytes": 262144,
  "max_documents": 16,
  "search": {
    "endpoint": "https://ai.xywhsoft.com/api/v1/search"
  }
}
```

`settings.agent.web_search` 与 `settings.web.enabled` 必须同时为真才注册工具。
搜索设置页只配置完整 API 地址；其他预算保持内部默认值，模型网络代理设置仍独立。
旧 Bing、Brave、SearXNG 配置在读取时迁移到新 API，移除 provider、secret_ref 和
max_results，不会仅为启动而写入 Home。旧网页解析和搜索服务实现已删除。

`web_search` 发送 `POST endpoint`，正文为 `{ "query": "...", "count": 5 }`，
count 可省略，由服务端决定默认结果数；显式 count 允许 1–10，服务端可进一步限制。
接收 xadmin 的 `{ code: 0, data: { provider, request_id, count, truncated, results } }`，
结果保留 title、url、snippet、site、published_at 和 fetched_at，空数组为成功。
外部结果不可信；HTML 验证页、错误封装、畸形字段和不安全 URL 均不伪装成搜索成功。

博查和 z.ai 选择、平台密钥、账户校验和配额由 xadmin 管理。客户端既不保存平台密钥，
也不自动回退到另一个搜索平台。xadmin 要求会员登录；目前 mdo 尚无账号登录界面，
本阶段通过运行时环境变量 `MDO_SEARCH_ACCESS_TOKEN` 接入已登录会员的 access_token。
这是短期会员 JWT，不是博查或 z.ai 的平台 key，不写入普通配置、工具结果或日志。
每次调用解析，传输结束安全清零；缺少或非法令牌在联网前失败。过期时需重新登录，
暂不自动刷新。正式账号 UI 与凭证生命周期后续独立接入，Android 同样需要该登录接入。

API 地址可配置为本机或局域网以便联调，例如 `http://127.0.0.1:9081/api/v1/search`。
此地址是明确授权的服务端点，拒绝 userinfo、查询参数与 fragment；线上应使用 HTTPS。
搜索请求禁止重定向和自动重试，避免凭证转发或重复计费。普通网页仍使用独立的访问策略。
401 提示重新登录，403 提示完成服务要求的联系方式验证，429 提示配额/并发限制，
503 提示管理员启用平台并设置 key，502/504 提示上游失败/超时。原始上游错误正文不回显。

联调测试：`python tests/test_search_api_runtime.py` 使用本地模拟 HTTP 服务，不消耗平台额度；
`python tests/test_search_xadmin_integration.py --xadmin-root D:\GIT\x-admin` 使用独立 xadmin
测试实例、真实登录 JWT 和上游模拟传输，覆盖客户端到插件的完整调用链。

## 网络与资源边界

普通网页默认只接受 HTTPS URL，拒绝 userinfo、fragment、控制字符、反斜杠和非法端口。
mdo 对每次请求设置总超时、空闲超时、正文上限、最多五次重定向和协作取消。
xs 的 `XS_FETCH_PUBLIC_ADDRESSES_ONLY` 在 DNS 查询工作线程中筛掉回环、私网、
链路本地、共享、文档、基准、过渡、组播和保留地址；TCP Dial 只能看到过滤后
的不可变地址列表，重定向继续使用同一策略。这一位置避免了“先检查 URL、后
重新解析域名”造成的 DNS 重绑定窗口。

只有显式把 `allow_private_networks` 改为真时才会关闭地址过滤；网络 effect
仍进入 xwork 的结构化权限请求。`web_search` 声明 read、network、
external-service 和 secrets 四种 effect，分别描述 endpoint、xadmin.search 和
固定的会员令牌引用；`web_open` 声明 read 与 network；`web_find` 是纯 read。

## 文档缓存与提取

缓存只存在于当前进程内，使用单调不复用的文档 ID 和固定条目上限，满时淘汰
最早文档。它不在 Home 中创建文件，也不会把网页数据混入会话配置。HTML
提取跳过 comment、script、style、noscript 和 SVG 内容，规范化空白并解码
常用及数字实体；非 UTF-8 或非文本响应会被拒绝。标题、正文、查找结果数量、
上下文字节和最终工具输出都各有硬上限。

传输接口可注入仅用于确定性功能测试。生产入口固定使用 `xsFetch`，注入接口
沿用同一请求/响应所有权合同，不能改变工具 schema 或权限声明。
