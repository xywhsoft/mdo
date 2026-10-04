# 离线会话诊断

2026-10-04 起，主界面移除轨迹、独立决策和上下文视图，以及任务事件列表、
统计卡片和内部 ID/修订展示。右侧栏已删除；任务操作、输出、产物和任务询问
通过对话中的任务卡片或会话“更多 → 后台任务”弹窗按需查看。
权限审批与 Ask 留在对话内，中断恢复也通过同一对话区域处理。
模型、思考强度、权限、token 用量仍由输入区提供。

调试时直接读取便携 Home 的会话目录，使用 Python 3.10+ 标准库即可：

```powershell
python tools/inspect_session.py mdo-home/sessions/default/<session-id>
python tools/inspect_session.py mdo-home/sessions/default/<session-id> --events --limit 50
python tools/inspect_session.py mdo-home/sessions/default/<session-id> --json
```

工具不启动 mdo，不连接模型，不修改会话文件。它读取 `meta.json` 的描述性
字段，逐行解析 `ui-events.jsonl`，统计事件、已完成模型调用的 token 用量、
工具完成/失败次数与可配对调用的耗时，并可展示最近事件的短预览。
耗时包含等待和审批等调用期间时间，不等同于纯工具执行耗时。

这些统计只覆盖现存记录。事件 ID 缺口、较晚的起始 ID、明确的历史删改、
损坏记录、未完成尾行、超长记录及文本截断都会提示。起始 ID 大于 1 也可能
由分叉或编辑产生；工具不会把它一律认定为日志淘汰。
读取活动文件得到的是尽力而为的观察，需要稳定证据时先停止该会话。
JSON 报告保留未知事件类型，不把未来 schema 强行解释为当前事件代码。

`snapshot.json` 与 `journal.jsonl` 仍是模型上下文账本；解析工具只列出文件
是否存在及大小，不模拟 xllm-session 恢复。完整工具输出可能位于 `artifacts/`
等产物文件中。现有日志不是原始 HTTP 请求/响应的完整录制。

移除的产品接口：

- `/api/v1/events`
- `/api/v1/tasks/{task}/events`

它们对 GET、HEAD 和 OPTIONS 均返回 404。会话级 `/events` 继续用于聊天回放，
审批、恢复、任务输出和取消接口继续服务日常交互。

验证命令：

```powershell
python -m unittest discover -s tests -p test_inspect_session.py
node --test tests/test_task_details.mjs tests/test_recovery_decisions.mjs
python tests/test_debug_cleanup_runtime.py --packed-path mdo.exe
```

`tests/fixtures/conversation-recovery-browser.html` 使用生产组件和合成数据，
供浏览器检查恢复选择、同 token 刷新、会话隔离、无工具中断、读取错误及手机布局。
打包探针也只使用独立 Home 和本地模型，不调用线上服务。

本阶段验证：347 项前端 Node 测试、59 项 Python 合同/解析检查、102 个 JS 模块
语法检查通过。中断事务探针、独立打包聊天/事件回放/离线报告探针通过；
浏览器检查了恢复选择保持、会话隔离、提交后消失、无工具中断和读取失败。
Windows EXE 与 ARM64 APK 已重新生成，APK 签名与对齐校验通过。
未运行压力或高负载测试，也未对本次 APK 作真机安装验证。

整套 `test_api_runtime.py` 当前在既有记忆接口断言处失败：旧探针预期列表为空，
工作区的文件记忆实现返回 `MEMORY.md`。本阶段不修改该独立的记忆功能，
本次删除接口通过 `test_debug_cleanup_runtime.py` 单独验证。

## 右侧栏清理

右侧栏、独立展开按钮、移动端右抽屉、分隔线和右栏布局偏好均已删除。
任务列表与输出通过原生 `dialog` 按需打开；关闭查看窗口不会停止任务，
切换会话会关闭窗口并清除详情选择，停止后继续展示“正在停止”直到任务实际退出。
手机上复用会话“更多”菜单，输入区不增加新的常驻按钮。

`/api/v1/pane-layout` 只读写 `sidebar_width` 和 `sidebar_open`。
便携文件 `data/pane-layout.json` 写入 schema 2；可读取 schema 1 的历史文件，
保留左栏偏好、忽略已删除的右栏偏好。读取旧文件不改写，首次用户调整才写入新格式。

验证：347 项前端测试、52 项构建/API/前端契约测试和单文件打包探针通过。
探针验证新默认值无布局写入、schema 1 读取、schema 2 写入、无效布局拒绝，
以及聊天回放与离线 JSONL 报告。浏览器检查覆盖桌面和 390px 手机宽度下的任务入口，
输出展开、Esc 关闭/焦点返回、切换会话、详情清除和任务停止状态。
APK 已重新打包并校验签名与对齐；本次未进行真机安装、压力测试或高负载测试。
