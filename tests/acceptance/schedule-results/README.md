# 计划执行的最终模型错误

2026-10-10，Windows 实际打包程序；仅隔离便携目录和回环模型，不消耗线上模型额度，不做压力或高负载测试。

## 复现与修复

旧候选 b481fbdf 运行一次额度不足的计划，模型返回 `daily_token_limit` / HTTP 429。模型请求只有一次，但历史只保留英文 `text`，中文页面无法显示对应中文说明。原始 API、请求数和页面见 `baseline-*`。

b9f42347 从实际执行的最终 `xwork_error` 保留分类、HTTP 状态和尝试次数，并优先记录失败原因，避免将失败前的一段回复误当原因。复用已有模型错误翻译，成功和取消的输出保留原文。已有调用入口及旧 JSONL 均可继续使用；没有增加数据库、工具或额外后台重试。完整错误原文不从提供方透传。

## 有界验收

- 实际页面点击一次“立即运行”，得到一条准确的中文额度说明，模型仅调用一次。使用真实语言设置切换英文、俄文，分别重新打开历史，对应语言说明正确。
- 将旧隔离夹具的一条原始 JSONL **原封不动**放到新夹具历史，检查新旧记录同时可读。旧记录没有分类，保留原说明，不猜测或改写。这是兼容性夹具，不代表迁移了用户数据。
- 重启该隔离打包进程，原分类、HTTP 429、尝试次数 1、旧记录和中文设置仍保留。设置及计划页面往返、重新加载后，原对话草稿保留。
- 回环模型解除模拟额度限制，再点击一次计划运行，成功原文 `FIXTURE_OK: quota` 保留；原失败原因仍在历史。回到原会话实际发送草稿，只有一次回复、一次成功完成事件，没有最终错误。
- 最终夹具共 **3 次模型请求**：1 次故意额度失败、1 次计划成功、1 次正常对话成功。浏览器无警告或错误；旧、新夹具均正常 exit 0，验收页面已关闭。
- 981 项完整前端、164 个可达模块、124 项契约、严格 GCC、计划持久化、计划原生执行及完整 API 运行门禁通过。原生执行另覆盖 8 个错误元数据解析边界、重载和成功/取消记录不混入错误分类。

候选 4,559,374 字节，比上阶段增加 1,085 字节，SHA-256 `6cc10d6f72391b237ea53a9505e53545de03f8cf2a80e80dc34c6149750a2361`。构建回执确认应用已提交且干净；候选、实际运行复制件及九份包内生产文件逐字节核对一致。

```powershell
python tests/manual_conversation_retry_qa.py --packed .build/conversation-acceptance/schedule-results/final/mdo.exe --directory .build/conversation-acceptance/schedule-results/final/run --duration 1800
# 必须用新的隔离目录；浏览器操作通过 CUA 执行。collect.py 只读取实际 API，setup 创建验收计划并复制旧夹具记录。
python tests/acceptance/schedule-results/check_evidence.py --exe .build/conversation-acceptance/schedule-results/final/mdo.exe --running-copy .build/conversation-acceptance/schedule-results/final/run/mdo.exe
```

本阶段没有安装 APK、替换日用程序或发布官网；三种语言的余额、超时及输出上限原因另有前端行为检查，实际打包页只制造了每日额度错误。计划历史并不翻译成功回复，也不能从缺少分类的旧记录恢复一个确定分类。Android WebView、输入法、剪贴板、文件选择和其他剩余功能继续验收。
