# 对话恢复浏览器验收

2026-10-08，以独立便携目录运行 Windows 候选包，模型仅访问回环地址。
使用 `tests/manual_conversation_retry_qa.py --packed <mdo.exe>` 可重建验收环境；
脚本打印浏览器地址，环境最多保留 20 分钟。在生成的目录创建 `stop` 文件可结束。
再次运行需指定新的 `--directory`，避免覆盖之前的记录。

依次在对话框发送 `retry`、`partial`、`quota`、`stop`。`stop` 运行期间输入
`continue` 并按 Ctrl+Enter，中断等待并优先发送。通过界面确认回复、草稿替换、
准确的额度提示以及自动接续。`acceptance.json` 保存服务端请求数及事件检查。

标题改进使用外部文件优先 VFS 更新前端后刷新页面，确认既有记录显示
“模型额度不足”。验收截图为 `browser-proof.jpg`。这是人工浏览器功能验收，
不代表已完成所有平台或真实手机验证，不访问付费模型制造故障。
