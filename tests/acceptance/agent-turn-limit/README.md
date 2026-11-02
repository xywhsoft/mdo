# Agent 执行轮数配置验收

2026-11-02，项目目录内开发和测试，单代理、顺序执行；没有复制项目或 SDK。
当前 SDK 缓存就地更新并重编译宿主。本任务未执行线上发布。

设置位置为“Agent 行为 → 执行轮数上限”，默认 `0`（不限制），支持中、英、俄三语、
自动保存、刷新回填和重启保留。全局与角色上限都为正数时采用较低值，内置主 Agent 和
通用子 Agent 的角色默认值均已改为 `0`。已有用户自行设置的角色上限继续保留。

真实原生循环使用可注入的本地模型回调和实际只读文件工具，未访问账号或模型服务：

- 全局 `0`：主 Agent 成功完成 130 轮；委派子 Agent 也成功完成 130 轮。
- 全局 `2`：主 Agent 在 2 轮后返回原生轮数限制错误；委派子 Agent 也只执行 2 轮。
- 全局 `0`、角色上限 `3`：仍在 3 轮停止，验证自定义角色限制保留。
- SDK 子 Agent 目录中 `uMaxTurns=0` 原样保留，避免静默回退为 8。
- 旧设置文件缺少新字段时正常启动，并读取到默认 `0`。

完整程序设置 API 验证 `0`、`512`、`1024` 的保存与重启回读；负数、小数、越界、
布尔、字符串和 null 均拒绝，保留最后一次有效值及已有记忆设置。使用此前测试程序
创建旧 Home，再启动候选程序，确认默认 Agent 和通用子 Agent 自动升级为新默认值。

后台浏览器实际把值从 `0` 改成 `2`，等待自动保存并刷新确认，再改回 `0`、刷新确认。
`settings.png` 展示最终值和已保存状态；浏览器错误日志为空。相关前端测试 58 项、
配置契约测试 6 项、248 个前端模块检查通过。

底层兼容修正仅涉及现有 `xwork-mcp.patch` 中子 Agent 的一条赋值及其公开字段说明：
保留明确传入的 `0`，默认初始化函数仍保持原有行为。依赖锁中的补丁与生产源摘要同步更新。
验收数据及测试程序 SHA-256 见 `acceptance.json`。

```powershell
python -X utf8 tests/test_agent_turn_limit_runtime.py
python -X utf8 tests/test_agent_turn_limit_settings.py --packed .build/agent-turn-limit-20261102/mdo.exe
node --test --test-concurrency=1 tests/test_settings_proxy.mjs tests/test_settings_autosave.mjs
python -X utf8 -m unittest tests.test_config_contract
```

设置测试支持 `--baseline-packed <旧程序>` 验证旧 Home 升级，
加 `--pause-for-ui` 可打开该测试 Home 的设置页，按 Enter 结束并清理自身目录。
