# 插件提示词贡献

插件提供有界、静态、可追踪的能力说明，宿主保存并验证，Agent 与
SubAgent 按自己的工具、配置和预算决定采用。贡献不授予工具或权限。

## 声明和注册

在 `plugin.json` 中声明 `prompts`，无需打开插件前端：

```json
{
  "prompts": [{
    "id": "browser-basics",
    "file": "prompts/browser-basics.md",
    "targets": ["primary", "subagent"],
    "activation": "with-tools",
    "when": {"any_tools": ["browser_open"]},
    "priority": 100
  }]
}
```

`text` 和 `file` 二选一；文件来自插件自己的目录，不允许绝对路径或
上级目录。`targets` 省略时适用两种身份。`when.any_tools` 至少满足一个，
`when.all_tools` 必须全部满足；条件使用精确工具 ID，不支持通配符。
`activation` 默认 `with-tools`，必须有关联工具；`explicit` 需要角色明确
选择，适用于没有工具的纯提示词插件。优先级 0–10000，数值小的先采用。

原生插件可用 ABI 尾部的 `AddPromptContribution` 注册
`mdo_prompt_contribution_v1`。调用前检查 registrar 的 `Size` 和函数指针，
旧宿主不支持时明确报告需要升级。宿主复制字符串及工具条件，不保留调用方
临时指针。不提供运行时任意回调生成内容。

每片段最多 2 KiB，每插件合计 4 KiB、16 项（清单与 C 注册共同计数）。
重复 ID、无效 UTF-8、越界路径和未知字段拒绝注册／激活。
规范身份为宿主提供的 `plugin:<id>/<local-id>`，并记录版本、目录修订、
内容 SHA-256。插件不能自行指定其身份或模型消息角色。

## 角色选择与请求组装

```json
{"context":{"plugins.instructions":{
  "enabled":true,"include":["plugin:mdo.browser-use"],"exclude":[],"max_bytes":8192
}}}
```

省略 `include` 时采用与当前角色可用工具匹配的说明；明确选择时可用插件 ID
或 `plugin:<id>/<local-id>`，支持结尾 `*`。`exclude` 优先。纯提示词贡献
必须被 `include` 明确选择。关闭来源可直接写 `false`。

每个 Agent／SubAgent 使用自己的实际身份、工具目录和最终请求工具筛选；
不复制父角色已经渲染的插件说明，也不以父角色自身的只读工具列表裁剪获准
写入的子角色。来源不授予工具、不绕过实时权限或审批。

`plugins.instructions` 的 `items` 保留规范来源、版本、内容哈希、条件和
采用／跳过原因。C 角色在 CONTEXT 阶段可以使用
`mdoAgentPromptContributions()` 与 `mdoAgentSetPromptEnabled()`。
片段正文或整组正文被角色改写时标记 Agent override，原插件身份／哈希不被
冒用；未知贡献不能伪造已注册来源。自定义角色文案应使用 `custom.*`。

普通来源继续进入会话的基础上下文；插件说明是托管请求中的独立消息，不写入
历史账本。BEFORE_REQUEST 之后重新按实际工具筛选，只替换宿主拥有的完整
消息。高级 C 角色主动改写／移除该消息时保留其最终决定，检查器明确标记
`agent_request_override`，不通过解析任意提示词重建消息。

说明跟随模块目录快照保存，运行中没有读取文件、连接网络或调用插件回调。
每轮重算适用条件；工具循环、恢复和压缩不会反复往历史追加说明。
角色总说明预算默认 16 KiB，可降低；超限时舍弃完整片段，不截断正文。
完整请求（含说明和工具 schema）仍使用现有模型预算检查，之后不追加说明。

静态预览仅显示候选项，实际工具筛选以运行检查器中的结果为准。自定义
`Program.Run` 使用 `mdoAgentRunDefault()` 时自动接入托管组装流程；直接绕过
托管执行器发出的原生模型请求不被隐式拦截。

## C／JS 查询

两端通过相同 Application SDK 调用 `plugin.prompts`，可选参数 `id` 筛选
插件。返回 `generation` 与 `items`，内容与正在使用的目录快照一致。
C 的 Agent 回调可直接在 ContextPlan 中选择贡献；前端修改角色配置和查看
预览，不承担每轮生成提示词的职责。无需打开插件前端也能在后台运行。
