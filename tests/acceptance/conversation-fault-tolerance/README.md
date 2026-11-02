# 会话历史局部容错验收

2026-11-02，Windows 0.8.4 开发测试包。这里只记录本地验证，不代表正式发布。

历史显示遇到损坏、乱序、单条超大记录或不完整的附件元数据时，保留其余可读内容，
在相应位置显示一行提示。正文不会跨过损坏位置拼接，提示没有重试、分叉等操作，
也不会把整个任务标记为失败。中、英、俄三种界面语言均有对应文案。

原先单个执行过程超过 16 MiB 扫描限额就拒绝整页。现在达到扫描、正文或保留预算时
返回已读部分及续读位置。`next_scan_before` 是绑定 `epoch` 的字节游标；读取更早内容时
发送 `scan_before` 和同一个 `epoch`。因此，即使损坏区没有一个有效事件 ID，仍可向前
翻页。历史被替换时旧游标返回 409，避免把两份记录拼在一起。正常回复仍尽量完整展示。

诊断信息通过独立的 `notices` 返回，只影响显示，不写回日志、快照或模型上下文。
前端只隔离无效行或操作锚点；会话归属、整页哈希及文件代际仍严格校验。
完全无法打开文件、服务不可达等整体读取故障仍显示读取错误，已加载的内容继续保留。
这次修复不改变模型重试策略，也不修复模型执行 Excel 任务本身的失败。

验证结果详见 `acceptance.json`：

- 两个原始会话分别经过 6 页、60 页读到文件开头，SHA-256 与修复前诊断一致。
  使用实际原生解析器、游标和显示投影代码，只以 `XFILE_READ` 打开原始文件，未导入或复制会话。
- 原生测试覆盖中段损坏及乱序、18 MiB 损坏尾部、全损坏、单条超大记录、超长回复。
- 完整程序 HTTP 测试另覆盖附件元数据损坏，六类异常均返回 200 及局部提示；
  无效或不匹配的续读游标分别返回 400、409。
- 162 项相关前端测试、248 个模块语法/依赖检查通过；普通历史的五段完整回复、
  十对工具记录、恢复运行和增量读取回归通过。
- 后台浏览器打开真实打包程序，确认错误两侧正文分别可见、提示只有一行，
  大段损坏尾部之前的正常历史可恢复查看。截图见 `browser.png`、`bad-suffix.png`。

复现命令，在项目根目录执行：

```powershell
python -X utf8 tests/test_conversation_faults_native.py
python -X utf8 tests/test_conversation_faults_runtime.py --packed .build/history-fault-tolerance-20261102/mdo.exe
python -X utf8 tests/test_conversation_groups_runtime.py --packed .build/history-fault-tolerance-20261102/mdo.exe
node --test --test-concurrency=1 tests/test_conversation_snapshot.mjs tests/test_conversation_notices.mjs tests/test_timeline_reply_actions.mjs
node --experimental-vm-modules tools/check_web_modules.mjs
```

原生测试可用 `--journals <ui-events.jsonl> ...` 对已有文件做只读回归。
HTTP 测试加 `--pause-for-ui` 会打印浏览器地址，按 Enter 结束并清理自身测试目录。
所有测试均不调用模型、不需要登录账号，也不复制 SDK 或整个项目。
