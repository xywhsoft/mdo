# C 工具开发范例

随 mdo 提供的工具都是 `mdo-home/tools/<名称>/` 下的内置扩展，
共用扩展目录、信任确认、依赖计划及启停机制。`todo` 和其他工具没有独立的管理路径。

默认会话计划工具的完整源码位于
[`app/default-home/tools/todo/tool.c`](../../app/default-home/tools/todo/tool.c)，
对应目录内的 `extension.json` 描述扩展身份、入口和平台。

该工具随 mdo 提供，模型调用名为 `todo`。源码演示 `mdoModuleEntry`、
`Registrar->AddTool`、参数 schema、Application SDK 的 JSON 处理和结果 writer。
开发新工具时，请复制整个目录并修改扩展 ID、别名、模块 ID 和工具 ID。

调用示例：

```json
{"items":[{"text":"检查项目","done":true},{"text":"验证结果","done":false}]}
```

每次提交完整列表，空列表清空；最多 24 项，每项文本 1–240 字符。
工具只维护会话待办，不操作项目文件。参数错误返回纠正提示，模型可继续调用。
成功的主 Agent 结果显示在待办栏，支持历史恢复、分支及备份；子 Agent 不覆盖主会话待办。
旧 `mdo.todo` 会话记录仍可读取。指定工具白名单时使用 `todo`。

新安装及升级时自动安装此目录，已明确停用或卸载的选择仍会保留。
停用或卸载只移除后续调用能力，不删除已有待办历史。

`read`、`exec` 等工具的 C 入口使用 `mdo/builtin_tool.h` 的
`MDO_BUILTIN_TOOL("read")` 绑定宿主已有实现，真实参数定义、执行逻辑及权限检查
仍由宿主提供。外部 C 扩展也可使用这个 SDK 入口，或像 `todo` 一样通过
`AddTool` 提供自己的实现。两种写法共用同一个扩展生命周期。
绑定同名工具会按常规注册冲突规则被拒绝，不会绕过任务权限或恢复已停用的工具。
