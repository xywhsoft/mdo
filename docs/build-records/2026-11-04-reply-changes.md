# 回复完成后的文件变更摘要

已实现：托管 Agent 每次运行结束后，在聊天记录中显示该轮文件变更；点击文件在右侧定位对应 diff，点击“查看变更”连续浏览本轮文件。没有净变化时不显示卡片。默认先列 3 个文件，其余可展开；摘要显示文件数、新增/删除行数，二进制文件有独立标记。

## 比较口径与持久化

- 比较该轮开始前和结束后的原始工作区快照，不把开始前已有的脏修改作为本轮新增变化。比较范围是工作区所属 Git 仓库；并行外部编辑可能出现在同一时间段，界面提示此口径，不宣称全部由 Agent 独占修改。
- 复用现有私有 bare Git 快照库，原始文件通过 fast-import 保存，包含符合现有快照规则的未跟踪文件。模型调用、shell 执行、提交等方式造成的净变化都按最终字节比较，提交后仍可查看。
- `changes.list` 和 `changes.diff` 增加 `scope: "turn"`、`turn: <agent_start event_id>`，使用相同冻结树读取摘要、分页 diff 和修订号。读取本轮比较不再附带磁盘实时身份，后续磁盘修改或提交不会改变旧比较。
- 以持久事件 ID 标识一次运行，不依赖可能在重启后复用的原生 run ID。收据还校验项目、会话、工作区、原生运行 ID、事件类型和时间范围，防止历史替换后误挂旧收据。
- 收据存放在 Home 的 `workbench-baselines/<project>/sessions/<session>/turns/<event>.json`；对象在该项目现有私有快照库中，不写用户 Git 索引或工作区。
- 每阶段捕获最多 5 秒，开始阶段还遵守原运行更早的截止时间/取消信号。捕获失败不决定 Agent 运行成败，已有消息仍正常显示；变更区域只显示一行可重试提示。

## 界面

- 聊天摘要展示于相应运行的最终回复/错误之后，连续工具调用仍沿用现有折叠规则。
- 每轮右栏复用一个变更面板。点击文件显示该文件；“查看变更”使用已有连续 diff 浏览能力。标签明确为“原版本：本轮开始前 / 新版本：本轮结束后”。
- 异步摘要读取与当前项目/会话绑定；行移出、切换会话、页面销毁时取消该视图自己的读取。不会因变更读取失败隐藏整个会话。
- 界面文案已加入简体中文、英文、俄文语言包。路径使用文本节点，不插入 HTML。

## 验证

新增验收测试使用现有 Windows x64 xs/TCC 宿主，直接编译项目源码，只有小型 Home、Git 工作区和临时配置放在 `.build` 中；没有复制 app 或 SDK，没有调用真实模型网络服务。

1. 离线模型回调驱动真实托管 Agent 生命周期，运行期间创建/修改/删除文件并执行 Git 提交。首轮 5 个文件，第二轮仅 1 个文件，第三轮无变化；开始前的脏文件没有混入。
2. 后续回复、额外外部编辑后，首轮摘要和 diff 保持不变；Unicode 路径、新增和删除正确。
3. 第四轮放入超过 16 MiB 单文件限制的输入，快照不可用，但 Agent 正常完成，界面保留其回复并显示一行提示。
4. 真实 Edge 浏览器通过摘要展开、单文件右栏定位、跨文件面板复用、连续 diff、页面刷新和无变化卡片隐藏检查，无页面脚本错误。检查浅色/深色截图。
5. 停止宿主，再用正常产品入口与同一 Home 启动，HTTP API 仍能返回首轮冻结 diff，并拒绝 `../outside` 路径。
6. 120 项前端测试通过，覆盖时间线、终态/恢复、工具调用、文件变更、语言包；264 个前端模块语法和可达性检查通过。
7. 现有 Git workbench 运行时回归通过，覆盖比较范围、重命名、二进制、Unicode/未跟踪/字面路径、大 diff 分页、过期修订与空仓库。

复现：

```powershell
python -X utf8 tests/test_turn_changes_runtime.py --browser
$files = @(Get-ChildItem -LiteralPath tests -Filter '*timeline*.mjs' | ForEach-Object FullName)
node --test @files tests/test_turn_changes.mjs tests/test_workbench_changes.mjs tests/test_frontend_i18n.mjs
node --experimental-vm-modules tools/check_web_modules.mjs
```

浏览器检查需要安装 Playwright/Edge；已有环境可用 `MDO_PLAYWRIGHT_PATH` 指向其模块目录。试验截图位于 `.build/turn-review-qa/review.png` 和 `review-dark.png`，小型快照工作区和 Home 会自动清理。

## 范围

当前基于本机 Git 仓库及已有快照限制（20,000 个条目、单文件 16 MiB、单次内容 64 MiB，私有存储复用现有容量准入限制）。非 Git 目录和未捕获的旧回复不会补造 diff；超限或读取失败可独立降级。子仓库沿用现有 Gitlink 快照语义。本轮未验证 Linux/macOS/Android 原生窗口，也未发布安装包或变更版本号。

实现提交：`8f48bfac`（快照与持久 API）、`0cc9510d`（聊天摘要与右栏联动）；最后的验收提交补充截止时间约束与超限回归。
