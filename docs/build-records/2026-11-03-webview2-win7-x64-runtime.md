# Windows 7 x64 WebView2 配套文件

交付目录：`E:\software\mdo-webview2-win7-x64`。
压缩包：`E:\software\mdo-webview2-win7-x64.zip`。

## 内容与使用

- Microsoft WebView2 Fixed Version Runtime **109.0.1518.78 x64**，完整保留原始分发包的 170 个文件，共 465,105,244 字节。
- Microsoft.Web.WebView2 SDK **1.0.1518.46** 中的官方 x64 `WebView2Loader.dll`，158,648 字节。
- `start-mdo.cmd`、中文说明、SDK 许可、来源记录、签名/架构/Loader 查找结果、SHA-256 清单及 PowerShell 校验脚本。
- 配套包不含 `mdo.exe`。将包内文件复制到 mdo x64 可执行文件旁边，运行 `start-mdo.cmd`。
- 启动脚本为子进程设置 `WEBVIEW2_BROWSER_EXECUTABLE_FOLDER`，不安装系统运行时，不修改注册表或全局环境变量。

运行时原包自带的辅助组件完整保留，包括 `EBWebView/x86`；这是微软 x64 分发包的一部分，不是额外下载的 x86 运行时。

## 来源

运行时取自 [WebView2RuntimeArchive 109.0.1518.78](https://github.com/westinyang/WebView2RuntimeArchive/releases/tag/109.0.1518.78)。微软原 CDN 链接在下载时返回 404，镜像取得的完整 CAB 已验证微软 Authenticode 签名。

Loader 来自 [微软 NuGet SDK 1.0.1518.46](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.1518.46)。精确下载地址保存在交付目录的 `sources.json`。

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| 原始 x64 Runtime CAB | 207,090,243 | `7622281cf83de1a35e3a471f432f7a897d65f0a7d3975df08512b7b253dd45c7` |
| 原始 SDK NuGet 包 | 2,962,677 | `63020b2d569d09a2098ae1ca20dd4cc281885f794aa00fc8812c6ab52dd49618` |
| 最终 ZIP | 207,790,286 | `780b0f4897015e2e079dd183c9a7efb3ecb7c41500e91d876027d12a7afd7c0d` |

## 验证结果与边界

- CAB 与五个核心二进制的 Authenticode 签名均为 `Valid`，签名主体为 Microsoft Corporation。
- 五个核心 PE 均为 `0x8664`（x64）；运行时核心文件版本均为 `109.0.1518.78`。
- 在当前 Windows 主机通过官方 Loader 的 `GetAvailableCoreWebView2BrowserVersionString` 分别使用绝对目录、环境变量覆盖调用，均返回 HRESULT 0、版本 `109.0.1518.78`。
- CAB 文件表与完整解压文件的数量和各自大小一致；178 个交付文件的 SHA-256 全部通过。
- ZIP 共 179 个文件，未压缩总大小 465,297,374 字节，所有 ZIP 条目 CRC 通过。
- 本机 PowerShell 执行策略阻止直接运行 `.ps1` 校验脚本；未改变策略。完整哈希校验由 Python 流式完成，微软签名由系统 Authenticode 校验完成。
- 未在 Windows 7 实机运行 mdo。提供运行时不代表 mdo 所有依赖、Windows API 和界面功能已经完成 Win7 兼容验收。

[微软说明](https://blogs.windows.com/msedgedev/2022/12/09/microsoft-edge-and-webview2-ending-support-for-windows-7-and-windows-8-8-1/)确认 109 是最后支持 Win7 的 WebView2 主版本。此固定版本已停止维护。

下载归档和中间文件已清理。大型二进制仅放在上述交付目录及 ZIP 中，不写入 Git；本次没有修改 mdo 源码或现有安装。
