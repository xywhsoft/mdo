# mdo 墨斗 — 原生 C 栈 Agent 工作台

mdo 是基于 xrt、xs、xllm、xllm-session 与 xwork 的便携式 Agent 工作台。
当前仓库正在按 [重构实施计划](docs/mdo-refactor-implementation-plan.md) 重建；
`app_bak/` 只保留旧产品实现作为行为参考，新代码全部位于 `app/`。

## 构建

`deps.lock` 固定 xrt、xserver 和三库的完整提交、版本与生产源码树哈希。
构建器会先验证依赖，再从 `app/sources.json` 生成单个 TCC unity 入口，构建
匹配的 xs 宿主并打包应用：

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
门禁范围、Linux 文件系统要求与 Ling 线上探针见[发布门禁](docs/release-gate.md)，
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
app_bak/             旧代码，只读参考
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

## 配置

内置 `config/defaults.json` 与外部 Home 中的 `settings.json`、`models.json`
和 `permissions.json` 按键合并。外部文件只保存用户差异，使用 schema v1、
原子替换和 `.bak` 备份；敏感值只允许保存 `secret_ref`。完整格式、运行时
覆盖、导入预览和 Ling 3.0 Tiny 保护规则见
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
