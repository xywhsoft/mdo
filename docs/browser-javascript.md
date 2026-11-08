# 在浏览器标签页和 iframe 中执行 JavaScript

Browser Use 插件新增模型工具 `browser_evaluate`、应用服务 `browseruse.evaluate`。
同一能力也可直接通过主线 `browser.command(command="execute_js")` 调用。
当前支持 Windows WebView2，包括同源、跨域及嵌套 iframe；Android 执行端仍不支持。
普通网页浏览与 JS 自动化的支持状态分开报告。

## 参数与执行环境

先使用 `browser_observe` 获得当前主文档 `document` 与 `frames` 清单。

| 参数 | 含义 |
| --- | --- |
| `browser` | 明确的标签页 ID；直接调用主线 `browser.command` 时字段名为 `id` |
| `document` | 最新观察中的主文档 ID，必填 |
| `script` | JS 异步函数体，必填；用 `return` 返回结果，可使用 `await` |
| `frame` | 观察清单中的 frame ID；省略时选择主页面 `f0` |
| `frame_document` | 所选 iframe 的原生文档标识；指定 iframe 时必填 |
| `world` | `page`（默认）或 `isolated` |
| `timeout_ms` | 100～10000 毫秒，默认 10000 |
| `max_bytes` | JSON 结果预算，256～65536 字节，默认 16384 |

`page` 可以访问网站自己的 JS 全局变量。`isolated` 使用独立全局环境、共享 DOM，
也与受信任的 Browser Use 观察运行时分开。两种环境均不提供 mdo 主窗口原生绑定。
脚本中的点击不是可信原生输入；需要原生输入事件时使用 `browser_act`。

主页面的模型工具参数示例：

```json
{
  "browser": "<标签页ID>",
  "document": "<当前主文档ID>",
  "script": "return { title: document.title, url: location.href };"
}
```

执行到 iframe 时增加观察结果中的两个身份字段：

```json
{
  "browser": "<标签页ID>",
  "document": "<当前主文档ID>",
  "frame": "<frames条目的id>",
  "frame_document": "<该frames条目的document>",
  "world": "page",
  "script": "await Promise.resolve(); return document.querySelector('h1')?.textContent;"
}
```

不能通过网址、iframe 序号或原始 CDP ID 猜测目标。导航、frame 替换或执行环境退出后，
旧身份失效；必须重新观察。

## C 与 JS

插件的 `sdk/browser_use.h` 提供 `mdoBrowserUseEvaluate`；底层调用为
`mdoCall(Context,"browseruse.evaluate",Params,...)`，返回异步回执。
使用 `browseruse.result` 检查真实终态，然后释放；不能把 `pending` 当作执行成功。

`sdk/client.mjs` 导出的 `createBrowserUseClient(sdk).evaluate(params)` 会等待终态。
变更请求使用持久操作身份；若响应丢失，使用原操作 ID 调用 `recover`，不重新提交脚本。
这些声明由 `contracts.json` 同源生成，当前共有 17 个模型工具、14 个应用服务。

## 权限、失败与资源

任意 JS 都按外部服务变更处理，不能自行声明“只读”来绕过权限。模型工具遵守当前任务
的只读、询问／完全访问及用户接管状态。网页返回的内容仍是任务数据，不授予额外权限。

脚本上限 64 KiB UTF-8，原生同步执行最多 3 秒，整个调用最多 10 秒。
结果必须能转换为有界 JSON；不要返回函数、BigInt、循环对象或 DOM 句柄。
异常和超限会作为工具错误返回，后续可以纠正参数继续操作。

SDK 保留结构化回执及 `effect_may_have_occurred`；模型工具使用现有托管工具的结果／错误
文本格式。超时和取消不能撤回已经运行的代码、计时器或网络请求；检查实际效果后再决定
下一步，不自动重复执行。每个浏览器最多保留 16 个原生未结束执行，超时不会提前释放
仍占用的名额；`script_pending_limit` 提示需检查并明确结束该浏览器。

本能力的主线与实际运行验证见
[2026-11-08 记录](build-records/2026-11-08-browser-javascript.md)。
