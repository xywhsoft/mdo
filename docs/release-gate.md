# mdo 发布门禁

本文定义 `0.1.0-dev` 及后续发布候选必须执行的低负载门禁。门禁只运行有界功能、故障恢复和确定性检查，不运行压力、高负载或长时间 soak 测试。

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
6. 运行所有真实 xs/TCC 低负载运行时探针；
7. 生成第二份 pack，并要求两份文件的 SHA-256 完全一致；
8. Windows 上在空临时目录启动单个 `mdo.exe` 5 秒，确认目录零写入；
9. Windows 上运行 20 秒打包启动回归，确认 TCC 服务初始化后进程仍存活，且没有 crash、dump 或 `xsw.log`。

任一步失败都会返回非零退出码。构建产物与探针临时目录位于 `.build/qa-release/`，不进入版本控制。

## 当前验证证据

| 平台 | 结果 | 确定性 pack SHA-256 |
| --- | --- | --- |
| Windows x64 | 113 项合同检查、严格 C11、16 个运行时探针、单文件零写与 20 秒打包启动全部通过 | `f967c49a7b029eda540b9ad5cae2bd20f12aa556fb187693d6593d1945a6923e` |
| Linux x64 原生文件系统 | 113 项合同检查、严格 C11、16 个运行时探针和两次 pack 一致性全部通过 | `458523cddacb2e16d0a96023dd53d1331b28f0bd55ce6b4178e6daae06c9d724` |

上表为早期平台验收记录；当前测试数量以实际门禁输出为准。新增的前端模块解析覆盖打包页的全部脚本，可在打包前拦住括号遗漏等语法错误。

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
