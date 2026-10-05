# 对话中的确认、提问与未完成回复

输入框上方的交互卡片使用三个明确名称：需要确认、需要回答、上次回复未完成。
原始工具参数放在“查看操作详情”中。前端只展示操作内容，不从命令文本推断安全性。

## 权限确认

默认展示具体命令、文件或资源，以及“拒绝此次操作”和“允许此次操作”。
“本次运行均允许”放在“更多授权选项”内，并说明它覆盖当前运行中的所有后续工具操作，
运行结束后失效，不改变会话默认权限。此按钮沿用后端的 `allow_run`，不是按命令前缀授权。

当前后端仍有等待期限。临近到期时卡片明确说明请求会失效，不会自动放行。
提交中的请求以及已确认但状态同步失败的请求都禁止重复提交。

## Agent 提问

选项和自由回答共用一个卡片。选择选项立即提交，填写自由回答后点击“提交回答”。
继续沿用字节长度校验、中文输入法保护、刷新时保留输入和跨会话隔离。

## 未完成回复

点击“继续任务”才会向后端提交恢复请求。默认处理方式由后端元数据决定：

- 工具可用且 `automatic_retry_safe` 明确为 `true`：重新执行只读调用，只展示汇总说明。
- 其他调用：默认 `record_uncertain`，不重复执行，并将结果未知告知 Agent。
- 工具已不可用：不提供重跑按钮。

用户可以为结果未知且仍可用的工具选择“再次执行”。界面明确提示可能重复产生效果。
选择始终绑定会话及恢复令牌；新令牌不会继承重跑授权。已接受的请求即使刷新失败或
切换会话后返回，也不会再次提交。

没有待处理工具调用时，还可选择“结束本次回复”，再发送新消息。
这不会删除已有内容、撤销文件修改，或伪造工具执行成功。
未完成状态通过卡片提示；只有实际发送被阻止时才显示输入区提示，避免重复警告。

本次改动调整呈现及默认恢复选择。停止原因分类、模型重试策略和审批等待期限属于运行时机制，
不由这些前端组件改变。

## 维护与验证

`features/approvals/tool-preview.js` 提供权限和恢复卡片共用的操作预览。
`features/approvals/recovery-decisions.js` 管理恢复默认值、快照绑定和提交锁。
`features/asks/ask-card.js` 负责选项及自由回答。

```powershell
node --test tests/test_recovery_decisions.mjs tests/test_tool_preview.mjs tests/test_frontend_approval_decisions.mjs tests/test_ask_refresh.mjs tests/test_frontend_i18n.mjs
node --experimental-vm-modules tools/check_web_modules.mjs
```

浏览器验证页面：`tests/fixtures/interaction-presentation-browser.html`。
用仓库根目录的静态 HTTP 服务打开，可检查权限、提问、未知结果、只读恢复、无工具中断及刷新。
该页面拦截自己的 API 请求，展示实际提交动作，不会执行工具。
