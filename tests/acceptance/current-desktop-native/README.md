# 当前 Windows 原生窗口：系统复制与实际导出

2026-10-11。用户在本轮独立 `mdo · 桌面导出验收` WebView2 窗口完成发送、
回复复制后 Ctrl+V、Markdown 和会话备份保存；随后明确确认复制内容完整一致。
通过正式 API 与实际文件读回核对，不以浏览器模拟剪贴板或下载行为替代系统交接。

当前候选为 4,570,274 字节，SHA-256
`224318e83b37c7099386a41288672e2fa36eaa06eb512bc7eb6358ab548fb3ed`。
应用的 431 个输入与 [集成候选](../current-integration/README.md) 一致，包含
工作区未提交源码；核对结束时摘要仍为
`82cb0b6354b30226a4d69dead949fe36df061ee86ed782d1e3c34d3b0a8945e2`。
测试使用全新的隔离 Home、本地回环模型和独立标题，没有操作日用 Home。

## 实际发送、复制和保存

[窗口入口](../../manual_desktop_export_qa.py) 创建的初始会话完成两个用户回合。
用户随后在同一隔离窗口中新建“copy and export”会话
`5ac3eb831fb04c62bae9b93d19a0a0bd`，发送一次 `copy and export`，回复成功。
共 3 次有界本地模型调用，初始会话的 10 条记录见
[原生运行结果](native-runtime.json)，导出会话的 5 条记录见
[正式事件接口](exported-events.json)。回复中的 wire 模型名沿用原配置的 ornith，
但请求全部由本地夹具处理，不是对生产 ornith 服务的验证。

用户点击导出会话的回复复制按钮，在输入框按 Ctrl+V，保留草稿没有再次发送。
正式草稿接口与持久化 `draft.json` 均逐字匹配夹具回复，包括中文、英文、俄文、
空行、Markdown 原文及末尾换行，共 204 个 UTF-8 字节。见
[实际草稿](clipboard-draft.json)、[复制核对](clipboard-verification.json)。
系统输入由用户完成，代理没有设置文本值或写入剪贴板。

用户通过原生窗口将两份文件保存到 `E:\WindowsDocument\Downloads`，并提供文件名。
仅读取这两个指定文件，确认它们的来源工作区属于本次隔离 Home：

| 文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| mdo-copy and export.md | 393 | `2fe47133339c2907ff3a393cdad1774018f1d174241c5552870b3fbca75a8496` |
| mdo-session-5ac3eb831fb04c62bae9b93d19a0a0bd.backup.json | 14,446 | `0d6a17ca6316b0692c7314d27cdd48513fd7f8b4d757ba10445d34f5db74ca10` |

Markdown 包含完整合成回复；JSON 内 6 个文件的长度与 SHA-256 均正确，总原始
内容 9,660 字节。其 5 条 UI 历史与实际会话的持久化历史逐字节相同，最后一条
成功完成。见 [文件读回核对](export-verification.json) 和 [原始导出](exports/)。
目录内 `.gitattributes` 保留原始行尾与字节，不改动用户 Downloads 中的文件。

## 官方控制入口与边界

官方 `node_repl + @oai/sky` 能选择本任务独立窗口，但首次截图返回
`FrameArrived timed out: timed out waiting on channel`。刷新窗口选择并只重试一次，
返回 `window capture timed out: timed out waiting on channel`，随后停止自动输入。
自动 Windows UI 输入次数为零；本次系统操作的证据来自用户实际操作、明确确认
及 API/磁盘读回，不冒称自动截图或点击通过。见 [控制记录](official-controls.json)。

[汇总](proof.json) 记录候选、用户确认和精确核对结果。所有调用使用本地合成模型；
测试窗口已结束，夹具退出码 0。没有压力、高负载、生产账号调用或官网发布。
本项完成当前 Windows 的系统复制与两种保存检查，不代表 Linux 原生窗口验证。
