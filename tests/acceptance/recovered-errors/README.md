# 恢复过程中的旧错误

生产提交 `f491e797` 修复旧存储错误在成功恢复后仍突出显示，以及继续再次失败时并列显示多条错误的问题。后端实际记录同一保留输入的继续运行后，旧错误与恢复记录收进现有执行详情；本次运行状态保持真实，当前最终错误仍公开显示。新问题、子智能体、丢失的输入来源、旧格式或畸形记录不能证明归属。只改变展示，不删除历史或增加持久化状态。

九项行为检查覆盖运行/取消/失败/成功、复用运行 ID、多次尝试、不同输入、子智能体与历史边界。初次修复前有三项失败，最终全部通过。完整前端 914 项、模块图 158 个和 124 项 Python 契约通过，没有压力或高负载测试。

## 实际页面与数据

`source-events.json` 和 `source-ui.json`：隔离 xs/TCC 副本中故意让一次读取开始事件持久化失败，正式恢复 HTTP 路由自动继续，实际只执行一次读取。正式完整页面默认收起旧诊断，展开可查看原文和时间。模型由本地回调提供，没有模型网络请求；该夹具不能称为未修改打包运行。两个改变的前端文件哈希与候选相同。截图为 `source-recovered.png`、`source-diagnostics.png`。

`packed-*.json` 和对应 PNG：使用未修改干净候选副本、独立 Home、正式配置 API 和回环 HTTP 模型，连续两次返回 429/daily_token_limit。首次提示准确每日额度，明确继续失败后只有最新一条；下一次继续成功，旧错误不再突出。中文草稿原样保留，刷新也不会再次调用模型。展开及正式搜索能看到两次诊断；正式导出按钮下载的 Markdown 实际落盘，文件为 `export.md`，包含两次错误、恢复标记和成功回复。

随后正式发送保留的草稿，第二条输入与回复准确且不重复。共四次回环模型请求，依次为三次原任务、一次新输入；前三次请求都只有一条原始用户消息。两次故意的最终额度错误仍保留在原生事件里。原生事件的再次投影独立验证：第二次失败只有一条公开错误、成功后为零，原始对象不被修改；普通续聊只追加新事件。

候选 `4,539,560` 字节，比上一阶段增加 `333` 字节；SHA-256 为 `a7c12dfa6960fc2e5b871139552112597711b3d6d719b9514ec5c6a62cc0d61b`。`candidate.json` 核对干净构建来源、宿主锁及两个包内生产文件，页面副本 SHA 与候选相同，不含测试控制路由。没有外部模型或线上账号调用。日用程序、APK 与官网本阶段未更新。

刷新后的历史导航曾出现“更早的对话摘要”入口，而完整 `/turns` 接口返回两条已完成回合、`has_more=false`。尚未确定该入口是否应出现，下一阶段核查页面分段加载和导航状态；本阶段不据此宣称其他功能已全面稳定。

## 复查

```powershell
node --test tests/test_timeline_recovered_errors.mjs tests/test_timeline_resume.mjs tests/test_timeline_terminal.mjs tests/test_conversation_history.mjs
python tests/acceptance/recovered-errors/check_evidence.py --exe .build/conversation-acceptance/recovered-errors/clean/mdo.exe
```

页面可用 `tests/manual_conversation_retry_qa.py` 创建新的隔离目录，发送 `quota`、明确继续一次、在夹具目录创建 `allow-quota` 后再继续；标记只影响回环模型响应。存储故障页面使用 `tests/manual_recovery_history_qa.py`。临时页面及宿主均已关闭，原用户数据未改动。
