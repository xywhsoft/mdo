# mdo 墨斗 — 原生 C 栈 Agent 工作台

mdo 是基于 xrt、xs、xllm、xllm-session 与 xwork 的便携式 Agent 工作台。
当前实现全部位于 `app/`，按 [重构实施计划](docs/mdo-refactor-implementation-plan.md) 开发。
旧版参考源码已从工作树移除，需要对照历史行为时可读取 Git 提交 `ccb0f6c` 中的
`app_bak/`，例如 `git show ccb0f6c:app_bak/wwwroot/src/ui.js`。

当前开发方向已调整为[基础 Agent 优先](docs/core-agent-priorities.md)：工具、
聊天体验、项目/会话和设置先行。内建默认模型为 `ornith-1.5-35b`。

## 构建

`deps.lock` 固定 xrt、xserver 和三库的完整提交、版本与生产源码树哈希。
构建器会先验证依赖，再从 `app/sources.json` 生成单个 TCC unity 入口，构建
匹配的 xs 宿主并打包应用。构建会拒绝没有入口引用的 C 源文件与私有头；
前端发布检查也会拒绝缺失的导入与无法从页面入口到达的模块：

```powershell
python tools/build_mdo.py
```

默认在仓库同级目录中寻找与锁文件提交完全匹配的 `xserver` 工作树。也可以
显式指定：

```powershell
python tools/build_mdo.py --xserver-root D:\GIT\xserver-mdo-refactor
```

Windows 可直接运行 `生成程序.bat`。Linux 使用同一个 Python 入口，输出文件名
自动为 `mdo`。首次构建需要 xserver 支持的 Python、GCC/Clang 和平台链接工具；
构建不依赖仓库根目录中预先存在的 `xs.exe` 或 `xsw.exe`。

发布候选使用统一的低负载门禁：

```powershell
python tools/qa_release.py --xserver-root D:\GIT\xserver-mdo-refactor
```

它会验证依赖锁、Python 合同检查、全部前端 ES 模块语法与 Node 交互测试、
严格 C11 编译、真实 TCC 低负载运行时探针、两次确定性 pack，
以及 Windows 无窗口只读启动零写、原生窗口便携 Home 和打包启动回归。构建命令不依赖 Node.js，
发布门禁运行前端检查时需要 Node.js。
门禁范围和 Linux 文件系统要求见[发布门禁](docs/release-gate.md)，
当前功能和限制见[0.1.0-dev 发布说明](docs/release-notes-0.1.0-dev.md)。

开发前先生成入口，再启动构建出的宿主：

```powershell
python tools/build_mdo.py --prepare-only
.build\host\xs.exe dev.json
```

## 源码布局

```text
app/
  include/mdo/       应用私有公共头
  src/               按产品边界拆分的 C 模块
  default-home/      mdo.exe 内置的只读默认资源树
  web/               原生 HTML/CSS/JavaScript 前端
  sources.json       unity 源清单
  xs.json            单文件应用配置
include/mdo/         版本化外接模块 ABI（MDO-3）
deps.lock            跨仓库依赖与格式版本锁
tools/build_mdo.py   验证、生成、宿主构建与打包入口
```

发布物可以只有 `mdo.exe`。运行时持久化数据只允许进入可执行文件旁的
`mdo-home/`。Windows 原生窗口首次打开时，WebView2 会按需创建
`mdo-home/data/cache/webview2/`；无窗口的只读启动和退出不会创建 Home。
该行为由便携 Home 与发布门禁测试固定。可以用 `MDO_HOME` 环境变量覆盖，也可以把应用参数
放在 xs 的 `--` 分隔符之后：

```powershell
mdo.exe -- --home D:\Portable\mdo-home
```

默认项目的新任务使用 `mdo-home/workspace/` 作为专用工作目录，首次创建任务时
自动建立，不再使用程序目录或启动目录。`--home`、`MDO_HOME` 会同时改变这个
目录的位置；Android 上位于应用私有 Home 内。已有会话保留创建时记录的工作
目录，显式指定的项目或会话路径仍优先；移动旧文件请自行复制到新工作目录。

## 配置

内置 `config/defaults.json` 与外部 Home 中的 `settings.json`、`models.json`
和 `permissions.json` 按键合并。外部文件只保存用户差异，使用 schema v1、
原子替换和 `.bak` 备份；敏感值只允许保存 `secret_ref`。完整格式、运行时
覆盖、导入预览和内建 Ornith 模型规则见
[配置合同](docs/configuration.md)。

内建联网能力由 `web_search`、`web_open` 和 `web_find` 三个有界工具组成；
配置、权限、DNS 重绑定防护与进程内文档缓存见
[Web 工具合同](docs/web-tools.md)。

会话使用 `meta.json` 与 xllm-session 的 snapshot/journal 分层持久化；创建、
恢复、搜索、分叉、清空、截断、导出、置顶、归档、回收站和并发更新规则见
[会话存储合同](docs/session-storage.md)。

旧版可执行文件旁 `data/` 与用户目录 `.mdo/` 只能通过设置页中的显式预览、
二次确认和原子导入迁移；目标 Home 不允许已存在，旧目录始终保留。转换规则、
不支持项、失败清理和回退方式见[旧数据迁移说明](docs/legacy-migration.md)。
