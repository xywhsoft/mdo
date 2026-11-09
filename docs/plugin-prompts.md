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
