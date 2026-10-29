# mdo 发布门禁

本文定义 `0.1.0` 及后续发布候选必须执行的低负载门禁。门禁只运行有界功能、故障恢复和确定性检查，不运行压力、高负载或长时间 soak 测试。

## 标准命令

Windows：

```powershell
python tools/qa_release.py --xserver-root D:\GIT\xserver-mdo-refactor
```

Linux：

```sh
python3 tools/qa_release.py --xserver-root /path/to/xserver
```

默认命令会从 `deps.lock` 锁定的源码重新构建宿主和第一份 pack。发布门禁还需要 Node.js 运行前端检查；`tools/build_mdo.py` 仍不依赖 Node.js。`--skip-host-build` 只用于已经精确构建过同一锁定 revision 的本地宿主；正式发布不得使用。`--skip-gui-smoke` 只适合没有桌面会话的本地或 Linux runner；Windows 发布候选必须保留 GUI 冒烟。

## 自动门禁内容

`tools/qa_release.py` 按顺序执行：

1. 校验 xserver HEAD、xrt 单头、xllm、xllm-session、xwork 生产源码、版本和格式锁；
2. 运行所有 Python 源码、构建、API 和前端合同检查；
3. 解析 `app/web/js/` 中所有 ES 模块，运行 `tests/*.mjs` 前端交互测试；
4. 从锁定依赖构建宿主并生成第一份单文件 pack；
5. 以 GCC C11、`-Wall -Wextra -Werror` 严格编译 mdo unity；
6. 运行桌面 xs/TCC 低负载运行时探针；Android 两项设备探针明确输出 `DEVICE CHECK NOT RUN`，须在连接测试设备后单独执行，不计为通过；
7. 用相同产品配置重新建立暂存目录并生成第二份 pack，要求两份文件的 SHA-256 完全一致；
8. 对同一打包候选运行 12 项 Home、工作区、目录、实时连接、队列、备份、恢复、压缩、分支、编辑和恢复确认检查；
9. Windows 上将单文件包复制到空临时目录，用唯一文件名打开原生 WebView2，确认旁边只生成 `mdo-home` 且浏览器数据在其 `data/cache/webview2` 中、没有按程序名写入 AppData；移动可执行文件与 Home 后重启，并检查 `MDO_HOME` 与 `--home` 覆盖；
10. Windows 上运行 20 秒打包启动回归，确认 TCC 服务初始化后进程仍存活，且没有 crash、dump 或 `xsw.log`。
11. Windows 上对同一打包候选运行五项原生右侧栏探针：四工具入口、浏览器承载层级和视口、
    浏览器显隐／错误恢复及文件编辑、变更视图、真实终端。它们仅启动独立的临时窗口及 Home，
    不依赖 Playwright，也不操作用户正在运行的 mdo。

任一步失败都会返回非零退出码。构建产物与探针临时目录位于 `.build/qa-release/`，不进入版本控制。

独立可选工具的实际程序探针需要显式提供包含 `input.json` 的暂存目录：
`--optional-tools-stage <stage> --optional-tools-platform windows-x86_64`（Linux 为
`linux-x86_64`）。未提供目录时明确输出 `ARTIFACT CHECK NOT RUN`，不计为通过；
发布这些工具前必须另行验证对应产物。平台或筛选条件没有选中任何工具时返回失败。

门禁在运行前记录应用源码、依赖锁文件与宿主配置指纹，并在运行时探针结束和最终
通过前复核；原生宿主哈希也必须保持不变。检查期间仍有其他任务修改这些输入时，
应先完成修改，再重跑门禁，不能合并不同输入的通过结果。

`tools/prepare_release.py` 在本地暂存八个平台产物前，会核对 `mdo-builds.json` 的
版本、应用源码指纹、锁定 SDK/runtime revision、宿主配置、工具包 revision、文件哈希与内嵌构建号。构建记录必须来自
当前源码与实际产物；不能沿用旧记录。产物回执的 `host` 指向实际嵌入的宿主，
Windows GUI 对应 `xsw.exe`。暂存清单标为 `verified-not-published`，并明确记录
`android_release_signed`；本地暂存不等于上线，也不代表 Android 真机验收完成。

Windows 真实页面绘制的人工检查可另外运行 `pwsh -NoProfile -ExecutionPolicy Bypass -File tests/native-webview-snapshot.ps1 -PackedPath .\mdo.exe`。这里的执行策略仅作用于本次 PowerShell 进程。它将截图留在 `.build/native-webview-qa-*/window.png`；需要检查会话恢复时可加 `-SeedHome <已有 mdo-home 目录>`，脚本会复制该 Home，不会改动原件。必须人工查看截图中的实际内容；这个检查不证明点击、拖放或偶发启动故障已消失。

## 当前验证证据

2026-10-29 对 `0.8.0` 当前工作区完成一次发布级检查，**未放行，未发布**。
八种产物构建、当前扩展目录原生回归及进程输出专项检查通过，但传统制品路径存在
SDK 回归，历史发布门禁仍有未通过项，Android 真机及正式签名产物验收也未完成。
具体结果及失败范围见 [本轮报告](build-records/2026-10-29-release-acceptance.md)。
不能用旧记录或单独的专项通过结果替代当前完整门禁。

发布脚本支持 `--host-dir`、`--output-dir`，Linux 构建支持 `--host-root`，便于使用
本轮的宿主和构建目录并保留其他任务的缓存。当前原生扩展测试可通过 `MDO_TEST_SDK`
显式复用已验证的 SDK；该路径仍须满足依赖锁校验。

2026-10-10 首个公开版本候选：Windows 标准门禁通过 148 项 Python 检查（3 项
ELF 检查在 Windows 跳过并在 Linux 补跑）、999 项 Node 检查、165 个模块解析、
严格 C11、82 项运行时和 12 项打包检查，另通过确定性打包、便携 Home 与原生启动
检查。八版构建、宿主和源码校验及发行前剩余事项见
[本轮检查报告](build-records/2026-10-10-release-audit.md)。Android 正式签名和最终 APK
的设备验收不包含在桌面标准门禁中。

| 平台 | 结果 | 确定性 pack SHA-256 |
| --- | --- | --- |
| Windows x64 | 113 项合同检查、严格 C11、16 个运行时探针、无窗口单文件零写与 20 秒打包启动全部通过（便携 WebView2 Home 接入前的记录） | `f967c49a7b029eda540b9ad5cae2bd20f12aa556fb187693d6593d1945a6923e` |
| Linux x64 原生文件系统 | 113 项合同检查、严格 C11、16 个运行时探针和两次 pack 一致性全部通过 | `458523cddacb2e16d0a96023dd53d1331b28f0bd55ce6b4178e6daae06c9d724` |

上表为早期平台验收记录；当前测试数量以实际门禁输出为准。新增的前端模块解析覆盖打包页的全部脚本，可在打包前拦住括号遗漏等语法错误。

2026-09-29 便携 WebView2 接入后，Windows/Linux 门禁各通过 114 项 Python 合同检查、85 项 Node 交互检查、72 个前端模块解析、21 个有界运行探针和确定性打包；Windows 另通过原生 GUI 的 Home 搬移与覆盖验证，以及 20 秒打包启动回归。Windows SHA-256 为 `b3ce5a4a2197a83c4ab61aac82b774f7de1db20ca379f52eccbf7bfe53ed1416`，Linux 为 `e6149367e507122d8a4f497acbaa94c45b8b74c6fa51510b072311c6f7e2a229`。没有进行压力或高负载测试。

Linux 迁移发布需要文件系统支持原子且不覆盖目标的目录 rename。WSL DrvFS 当前不支持该语义，mdo 会返回类型化错误、删除 staging 并保留来源；Linux 原生文件系统已通过完整迁移门禁。发布 runner 应在 ext4、xfs、btrfs 等原生文件系统上执行。

## Ling 3.0 Tiny 线上探针

xllm 已提供 Chat Completions、Responses 和 Anthropic Messages 三类离线 wire/响应 golden。线上发布探针只从运行时环境读取地址和密钥，不将 secret 写入仓库：

```powershell
$env:XLLM_LIVE_MODEL = "ling-3.0-tiny"
$env:XLLM_LIVE_URL = "<Chat Completions endpoint>"
$env:XLLM_LIVE_RESPONSES_URL = "<Responses endpoint>"
$env:XLLM_LIVE_ANTHROPIC_URL = "<Anthropic Messages endpoint>"
$env:XLLM_LIVE_KEY = "<runtime secret>"
$env:XLLM_LIVE_REQUIRE_THREE = "1"
D:\GIT\xrt-mdo-refactor\extlibs\xllm\build\test_xllm_live.exe
```

Linux 执行同名的 `test_xllm_live`。上面的严格 xllm 测试仍要求显式环境变量，适合测试部署覆盖和工具调用。产品默认接入已于 2026-09-23 另做有界线上验证：在隔离的 mdo 实例中清除全部 `MDO_LING_*` 变量，分别使用 Responses、Chat Completions、Anthropic Messages 创建会话并完成一轮真实对话；三条路径均成功且返回非空文本。此验证没有运行压力或高负载测试。

## 发布前人工检查

- 若发行渠道启用签名或病毒扫描，核对最终 pack 的签名和扫描结果；
- 随发行物保留 xserver 生成的第三方 NOTICE、许可和精确源码 revision/relink 资料；
- 在桌面宽度和 390 px 移动宽度检查会话、设置、迁移、恢复和审批流程；
- Android、iOS 或其他移动原生宿主由对应平台 runner 单独验收；本仓库的响应式浏览器检查不能替代原生宿主验证。
