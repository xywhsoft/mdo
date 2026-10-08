# 摘要失败、取消与继续验收

本阶段使用独立 Windows 打包程序、一次性便携目录和回环合成模型。三种故障按顺序执行，不调用付费模型，不进行压力或高负载测试。

旧候选在摘要质量修正两次失败后保留原始历史，也能够恢复，但最终事件的 `model_error_kind` 为空，页面显示内部英文诊断。新版本按主运行的上下文整理阶段记录 `context_compaction`，三个界面语言均说明原始对话保留以及继续方式。提供方的明确错误分类优先；子 Agent 或其他运行的事件不会污染主运行阶段。

## 打包验证

- 质量失败：两次无效摘要，只产生一条最终错误；重启后分类仍保留，从原输入继续并追加下一轮成功。
- 额度不足：首个摘要请求返回 `daily_token_limit`，不盲目重试；准确分类，只产生一条最终错误。测试恢复服务后显式继续成功。
- 退避取消：摘要返回 429 和十秒 Retry-After，在等待期间取消，约 30 ms 完成；没有最终错误，显式继续成功。
- 三种情况都保留原始事实，没有重复添加用户输入，摘要请求不加载工具。请求计数见 `packed.json`；其中恢复后的摘要请求属于用户明确继续，不属于失败期间自动重试。
- 原正常压缩回归仍完成六轮、四次压缩、重启后继续，零最终错误。

## 页面与检查

真实打包页面展开执行过程后显示一条“上下文整理未完成”，不显示 `missing_sections` 或十六进制诊断，后续三条成功回复和可用输入框保留。见 `browser.json` 和 `browser.png`。

原生事件回放探针验证新旧事件兼容、主/子 Agent 隔离、运行隔离、提供方分类优先、取消清理和运行标识重用。模型错误和时间线 Node 检查共十项通过。前端合约中的模块图、文本安全、恢复、API、运行控制、状态分区和分页七项通过。完整旧基线合约另有一项既有过期断言：要求 `setMcpEnabled` 位于资源面板，实际已移至资源状态模块；工作区其他任务正在修正该断言，本提交不纳入其修改。

## 可复现命令

```powershell
python tests/test_packed_compaction_failure.py --packed .build/conversation-acceptance/mdo-compaction-errors-final.exe
python tests/test_packed_compaction_recovery.py --packed .build/conversation-acceptance/mdo-compaction-errors-final.exe
python tests/test_model_error_events_runtime.py --host .build/conversation-acceptance/compaction-host/xs.exe
node --test tests/test_model_errors.mjs tests/test_timeline_resume.mjs
```

候选包构建及哈希见 `build.json`。根目录日常程序、官网和手机均未在本阶段更新，安卓行为仍待真机验证。
