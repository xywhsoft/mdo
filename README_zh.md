# 墨斗 mdo — 原生 C 栈 Agent 工作台

> 仅约 4MB 即具备完整的 Agent 能力。可在任意平台构建、扩展与运行。
> 英文版见 [README.md](README.md)。

mdo（墨斗）是一款便携 Agent 工作台，在**原生 C 栈**（`xrt`、`xs`、`xllm`、`xllm-session`、`xwork`）
之上叠加原生 HTML/CSS/JavaScript 前端。所有业务逻辑用 C 编写，编译为单个 TCC unity 入口，由 `xs`
运行时托管，并在 WebView 中渲染界面。最终产物是一款轻量、自包含的桌面 / 移动 / 服务器应用，
具备完整的本地 Agent 功能。

---

## ✨ 亮点

### 🪶 约 4MB 具备完整 Agent 能力
整套 Agent 功能以极小的体积交付：

| 产物 | 大小 | 说明 |
| --- | --- | --- |
| `mdo-arm64-v8a.apk`（Android 精简版） | **约 4MB** | Android 上的完整 Agent |
| `mdo-linux-x86_64-glibc` | 约 4.8MB | 桌面版，动态核心 |
| `mdo-server.exe` | 约 5MB | 无头服务版 |
| `mdo.exe`（Windows） | 约 5.1MB | 带原生窗口的桌面版 |
| `mdo-full-arm64-v8a.apk`（Android 完整版） | 约 59MB | 内置浏览器 |

精简版 APK 在约 4MB 的目标体积内，仍交付了完整的 Agent：模型对话、工具、项目、会话、记忆、
技能和远程控制。

### ⚡ 完整功能
- **本地 Agent 核心** — 带流式输出、工具调用和引导构建器的模型对话。
- **项目与会话** — 创建、恢复、搜索、分叉、清空、截断、导出、置顶、归档、回收站；snapshot/journal 分层持久化。
- **技能（Skills）** — 编写、限定作用域和调用技能；内置创建器与原生校验。
- **记忆（Memory）** — 项目级记忆，并支持设备间传输。
- **联网工具** — 有界的 `web_search`、`web_open`、`web_find`，含 DNS 重绑定防护。
- **配置** — 默认值叠加用户设置，原子替换、`.bak` 备份，敏感值仅存 `secret_ref`。
- **MCP** — Model Context Protocol 支持。
- **计划、运行、审批、提问** — 后台自动化与权限门控。
- **自动更新** — Windows 与 Linux 更新通道。

### 🛠 二次开发能力
mdo 为可扩展和可重建而设计：

- **C 源码即应用** — 所有逻辑位于 `app/src/`，按产品边界组织（`api/`、`sessions/`、`remote/`、`skills/`、`projects/` 等）。约 200 个 C 文件，约 7.3 万行。
- **确定性构建** — `deps.lock` 把每个依赖锁定到完整提交 + 源码哈希；`tools/build.py` 从 `app/sources.json` 生成 TCC unity 入口，构建匹配的 `xs` 宿主并打包应用，并拒绝未引用的 C 源与私有头。
- **原生模块 ABI** — `app/include/mdo/` 定义版本化外接模块（MDO-3），用于可插拔功能。
- **前端契约** — `app/web/` 存放 HTML/CSS/JS；发布检查会拒绝缺失的导入和无法从页面入口到达的模块。
- **按需扩展** — 技能、模型、MCP 服务器和自定义模块均可接入同一构建管线。

### 🌐 跨平台能力
一套代码，四个平台，便携 Home：

| 平台 | 产物 | GUI | 说明 |
| --- | --- | --- | --- |
| Windows | `mdo.exe` / `mdo-server.exe` | 原生窗口 | 便携 Home，无窗口的只读启动零写 |
| Android | `mdo-arm64-v8a.apk` / `mdo-full-arm64-v8a.apk` | WebView2 | 精简版（约 4MB）与完整版 |
| Linux | `mdo-linux-x86_64-{glibc,musl}` | GTK / WebKitGTK | GUI 版与无头服务版 |

- **便携 Home** — 所有运行时数据（`mdo-home/`）位于可执行文件旁，不污染用户目录；可用 `--home` / `MDO_HOME` 覆盖。
- **会话一致** — 跨平台共享同一会话格式、配置 schema 与 Home 布局。
- **Linux 构建** — glibc/musl、GUI/服务版、Debian 基线引导、`--wsl` 支持。

### 📱 多设备远程控制
在设备之间互相控制，或在工作之间迁移：

- **双向控制** — PC ↔ Android 远程控制（已实机验证）。两端登录同一账号并开启远程控制即可。
- **发送到设备** — 在菜单中选择发送单个会话或完整项目；接收端获得独立副本（源数据不移动）。
- **传输内容** — 单会话：模型账本、UI 历史、用量、附件、产物、计划、草稿及暂存队列；完整项目：全部会话（含归档和回收站）、整个工作目录和项目记忆。
- **可靠传输** — 原生后台任务，分块落盘、校验和、断线续传，并基于租约的排他性，避免运行中的进程破坏发送。
- **安全传输** — 复用远程控制认证（同账号票据、写令牌、请求序号）；可用局域网 TLS，否则经现有服务器中转。

---

## 🚀 快速开始

构建应用（Windows PowerShell）：

```powershell
python tools/build.py
```

先生成入口，再启动构建出的宿主：

```powershell
python tools/build_mdo.py --prepare-only
.build\host\xs.exe dev.json
```

发布门禁（前端检查需要 Node.js）：

```powershell
python tools/qa_release.py --xserver-root D:\GIT\xserver-mdo-refactor
```

---

## 📂 源码布局

```text
app/
  include/mdo/     版本化外接模块 ABI（MDO-3）
  src/             按产品边界拆分的 C 模块
  default-home/    mdo.exe 内置的只读默认资源树
  web/             原生 HTML/CSS/JavaScript 前端
  sources.json     unity 源清单
  xs.json          单文件应用配置
deps.lock          跨仓库依赖与格式版本锁
tools/build_mdo.py 验证 → 生成 → 宿主构建 → 打包
tools/build.py     桌面版与 Android 精简版、完整版统一构建
```

---

## 📚 文档
完整契约与指南见 [`docs/`](docs)：
`configuration.md`、`session-storage.md`、`web-tools.md`、`device-transfers.md`、
`device-remote-control.md`、`linux.md`、`android.md`、`release-gate.md`、
`mdo-refactor-implementation-plan.md`。

[工作区与浏览器 SDK](docs/workbench.md) 包含插件标签页、原生浏览器视图，以及
C／JavaScript 共用的面板、资源与浏览器接口。

---

## 📄 许可
见 [LICENSE](LICENSE)。当前源码版本：**0.5.0**。
