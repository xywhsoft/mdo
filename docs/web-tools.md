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
    "provider": "brave",
    "endpoint": "https://api.search.brave.com/res/v1/web/search",
    "secret_ref": "env:MDO_BRAVE_SEARCH_API_KEY",
    "max_results": 8
  }
}
```

`settings.agent.web_search` 与 `settings.web.enabled` 必须同时为真才注册工具。
搜索 provider 的凭据只保存 `secret_ref`；`web_search` 每次调用时解析，传输
返回后立即安全清零。缺少凭据不会触发网络请求，而会返回明确的策略错误。
当前生产适配器是 Brave，配置结构保留 provider、endpoint 和凭据的独立边界，
后续增加 provider 不需要改变三个工具的模型侧合同。

## 网络与资源边界

默认只接受 HTTPS URL，拒绝 userinfo、fragment、控制字符、反斜杠和非法端口。
mdo 对每次请求设置总超时、空闲超时、正文上限、最多五次重定向和协作取消。
xs 的 `XS_FETCH_PUBLIC_ADDRESSES_ONLY` 在 DNS 查询工作线程中筛掉回环、私网、
链路本地、共享、文档、基准、过渡、组播和保留地址；TCP Dial 只能看到过滤后
的不可变地址列表，重定向继续使用同一策略。这一位置避免了“先检查 URL、后
重新解析域名”造成的 DNS 重绑定窗口。

只有显式把 `allow_private_networks` 改为真时才会关闭地址过滤；网络 effect
仍进入 xwork 的结构化权限请求。`web_search` 声明 read、network、
external-service 和 secrets 四种 effect，并分别描述 endpoint、provider 和
secret reference；`web_open` 声明 read 与 network；`web_find` 是纯 read。

## 文档缓存与提取

缓存只存在于当前进程内，使用单调不复用的文档 ID 和固定条目上限，满时淘汰
最早文档。它不在 Home 中创建文件，也不会把网页数据混入会话配置。HTML
提取跳过 comment、script、style、noscript 和 SVG 内容，规范化空白并解码
常用及数字实体；非 UTF-8 或非文本响应会被拒绝。标题、正文、查找结果数量、
上下文字节和最终工具输出都各有硬上限。

传输接口可注入仅用于确定性功能测试。生产入口固定使用 `xsFetch`，注入接口
沿用同一请求/响应所有权合同，不能改变工具 schema 或权限声明。
