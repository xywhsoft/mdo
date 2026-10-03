# 旧版核心交互恢复验收

2026-10-03，应用源码提交 `9f6b589`。本轮按用户已确定的方向检查
`app_bak/wwwroot` 的操作模式：布局允许优化，保留输入、消息、工具、
项目与会话的基本手感。当前功能优先级见
[基础 Agent 优先级](core-agent-priorities.md)；历次实施记录见
[迁移记录](frontend-migration.md)。

八条核心操作链均已有实现、相关回归和实际打包交互证据，本次逐项回读
原始捕获并核对当前源码。下面的完成结论针对核心功能恢复；实体设备、
各系统原生窗口的独立验收仍在文末明确保留，不将浏览器视口当成实体手机。

## 操作链与证据

证据文件位于工作区 `.build/`，对应增量已提交 Git。旧捕获用于证明
当时实际运行，当前源码再由相关测试及独立重建核对；不把旧包哈希当成
本轮包哈希。最终汇总为 `qa-core-acceptance-verified.json`。

| 操作链 | 当前行为与实现 | 已核对的实际证据 |
| --- | --- | --- |
| 新任务与输入配置 | 首次发送创建任务；输入区切模型、思考和权限，显示输入估算及上下文用量。配置保存中也接收发送，固定本次选择。`composer-profile`、`new-task-controller`、`token-meter` 各负责一项行为。 | `qa-profile-send-positive-api.json`、`qa-stream-core-api.json`、`qa-ornith-packed-live-proof.json`。内建 Ornith 的 Responses、Chat、Anthropic 均完成真实单文件运行。 |
| 输入、流式回复与队列 | Enter 发送/排队、Shift+Enter 换行、Ctrl/⌘+Enter 中断并优先发送；引导模式对调后两者。部分正文/思考、停止、恢复及排队反馈可见，下一条草稿与其他任务隔离。 | `qa-stream-core-api.json`、`qa-stream-core-linux-api.json` 和迁移记录中的引导/恢复打包链。Windows 两轮成功、Linux 一轮取消后一轮成功，队列排空；当前相关测试通过。 |
| 消息操作与统计 | 用户/助手/代码复制、编辑、重试、回复分叉、点赞/点踩；显示时间、用量、LLM 耗时和 tok/s。更新保留旧节点及用户焦点，迟到反馈读取不覆盖成功写入。 | `qa-feedback-after-ui.json`、`qa-message-source-final.json`、`qa-message-branch-final.json`。原文/代码核对，源两轮、分支一轮，刷新保留反馈、修改后的回合和草稿。 |
| 工具与产物 | 调用参数、写入正文、修改片段、结果和错误可读，支持折叠与复制；大结果按原事件读产物，不用模型摘要或当前文件伪造历史结果。后台工具状态通过任务面板展示。 | `qa-tool-core-after.json`：六次调用、五次成功、一次预期失败；`qa-tool-content-live-proof.json`：原调用及 8,409 字节产物复制一致。当前工具内容、任务归属与取消、产物投影测试通过。 |
| 待办、询问与审批 | 计划进度及折叠；选项与自由回答；拒绝、允许一次、本轮均允许。提交锁定双方入口，结束回到输入框。等待期间可保留草稿、切换任务；审批刷新恢复。 | 本轮 `qa-core-decisions-ui.json`、`qa-core-decisions-api.json`：五轮完成，两个实际回答，三个命令成功、两个按决定拒绝，待办 1/2→2/2，重启回读 59 条事件。 |
| 附件 | 选择、粘贴、多图拖放处理、预览、移除；带图发送、编辑、重试、分叉和草稿恢复。最后一张移除后回到添加图片。 | `qa-attachment-core-{windows,linux}-api.json`：原图与分支历史引用一致；`qa-core-drop-live.json`、`qa-core-drop-verified.json`：实际双图上传、字节一致、区外松开保护及 320px 刷新。拖放为浏览器生成的文件事件。 |
| 文件补全与快捷命令 | @ 文件候选、上下键/Enter/Tab，正确引用空格路径；斜杠命令、帮助与 Esc、模型轮换、新任务、分叉、清空确认、停止、设置、导出沿用原模式。 | `qa-session-core-before-ui.json`、本轮 `qa-core-decisions-ui.json`，及迁移记录中的命令链。`qa-export-download-{windows-text,linux-markdown}-events.json` 均有全字节 completed，正文曾实际读回；帮助/设置返回输入框。 |
| 项目、会话与移动布局 | 选择目录/快捷建项目、切换、独立草稿、搜索、改名、置顶、归档、回收站和恢复；启动恢复不覆盖新操作。窄屏抽屉及短屏决策可滚动，常用目标至少 40px。 | `qa-session-focus-api.json`、`qa-session-focus-after-ui.json`、`qa-startup-*-proof.json`。改名后返回现存按钮、移出列表返回筛选器，手机侧栏分叉收起抽屉并进入输入，刷新及跨项目草稿一致。 |

审批“本轮均允许”的范围与旧执行器一致：旧 `mdo_engine.h` 的
`allow-session` 实际只设置当前 `MdoRun.bAutoAllow`。新标签直接说明
本轮范围，不自动改成永久完全访问。本轮实测第二次命令自动允许，
下一独立运行重新询问；长期权限仍通过输入区选择。

设置的核心手感也已恢复：常用偏好自动保存、即时界面语言/主题反馈，
模型和 Provider 就地编辑，失败明确重试，保存/刷新不覆盖后续输入。
`qa-refresh-save-after-ui.json` 与 `qa-refresh-save-verified.json` 已核对
六种真实 HTTP 情形及窄屏刷新。管理资源按设置分类读取，普通聊天不
预加载全部管理目录，不为验收新增模型工具或上下文。

## 本轮交付与验证边界

- Windows、Linux 各通过 59 个相关前端测试文件的 **276 项测试**、
  18 Python 页面合同、102 模块解析及单文件资源/API 读取；测试串行，
  没有压力或高负载测试，没有无关治理探针。
- Linux 原生 ext4 对齐 283 份当前应用文件，SDK 未改。两系统独立
  A/B 打包各自一致。Windows 候选与实际交互的包、根目录 `mdo.exe`
  相同：6,487,401 字节，SHA-256
  `a18d1ae17e9f39d3d5b40d68606f7ca576b9c37122676d2f57cde70e6a3d73ab`。
  Linux SHA-256 为
  `d6d0a35a485a731a948d9ced996e8dda312af9ab0780e18aac2539599538e24b`。
- 本轮交互只使用 localhost 模型和隔离 Home，无外部模型流量，测试
  服务均正常退出。程序保持内置 VFS、外部文件优先和便携 Home；原生
  WebView2 缓存位于 `mdo-home/data/cache/webview2/`，构建无需 npm。
- 操作系统资源管理器的原生拖放、实体手机软键盘/触控及 macOS/Linux
  原生图形窗口未作为已验收结果。现有浏览器/服务证据不会关闭这些
  平台测试项；完整 CommonMark、全量历史索引及治理细化继续按优先级
  留作后续增强。

旧版核心操作恢复的长期任务可以据此收尾。后续 mdo 开发继续按实际
使用反馈推进工具、项目/会话与设置，不再因历史验收表中的治理或
硬件覆盖条目无限扩展本次功能恢复任务。
