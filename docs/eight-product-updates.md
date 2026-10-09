# 八种发行版的下载与更新

2026-10-09：官网直接提供八个单文件产物；网站统一 `mdo` 插件的“应用发布”
页面独立维护八条通道。检查更新不需要会员登录。

| 发行版 | platform | edition | 当前编号 |
| --- | --- | --- | --- |
| Windows 桌面 | windows-x86_64 | desktop | 30000047 |
| Windows 服务 | windows-x86_64 | server | 30000054 |
| Linux glibc 桌面 | linux-x86_64-glibc | desktop | 30000048 |
| Linux glibc 服务 | linux-x86_64-glibc | server | 30000049 |
| Linux musl 桌面 | linux-x86_64-musl | desktop | 30000050 |
| Linux musl 服务 | linux-x86_64-musl | server | 30000051 |
| Android 精简 | android-arm64-v8a | lite | 30000052 |
| Android 完整 | android-arm64-v8a | full | 30000053 |

接口：`GET https://ai.xywhsoft.com/update/version?platform=…&edition=…&build_id=…`。
服务器只返回匹配平台与发行版的包。旧 Windows 客户端省略 edition 时仍选择桌面版；
Windows 服务版必须携带完整平台、发行版和编号，拒绝桌面版及缺少发行版的响应。
glibc、musl 和桌面/服务版不会互相升级。Android 用户可在现有更新界面选择精简/完整。

`app/release.json` 中八个编号必须唯一。任何已发布编号与 hash 不可复用；新编号
高于服务器的 `last_build_id`。本次 Windows 服务版增加更新能力，单独重新打包为
30000054；其余七个包保留前一轮验证的原始字节。根目录 `mdo-builds.json` 记录实际
文件大小和 SHA-256，不能用源代码当前状态推断旧包的内容。

## 安装行为

默认启动和每十分钟检查，提示后由用户执行下载、安装；本次发布均非强制更新。
有活动任务时先结束任务，避免中途替换程序。界面和服务版均使用网页的更新入口。

Windows 服务版的普通命令行进程：保存完整原始启动参数，包括 `--port` 和 `--home`，
旧进程优雅停止后在同一目录原子替换 EXE，再隐藏控制台重新启动。安装失败保留错误
记录，已替换时恢复旧 EXE。备份和下载缓存位于 Home 的 `data/update/`。

注册为 Windows 服务：安装器按当前进程 ID、创建时间、可执行路径和 SCM 注册路径
识别当前唯一的独立进程服务。在退出前验证重新启动该服务的权限；退出后通过 SCM
启动，保留服务账号、注册参数、启动类型和恢复策略。专用服务账号必须有本服务的
`SERVICE_START | SERVICE_QUERY_STATUS` 权限，以及程序目录/Home 的写权限。
[Windows 服务权限说明](https://learn.microsoft.com/en-us/windows/win32/services/service-security-and-access-rights)。
共享进程服务或权限不足时拒绝安装，不停止当前程序。

Linux 保留现有同目录原子替换和 exec 重启流程；systemd 管理的服务保留 PID 与参数。
运行账号需要程序目录写权限。Android 经系统安装器确认，保持原包名和签名。

## 发布流程和验证

1. 将八个已验证文件放在仓库根目录，更新 `app/release.json` 与 `mdo-builds.json`。
2. 运行 `python tools/prepare_release.py`：核对文件记录、EXE/ELF 结构和内置编号/发行版、
   APK 内置元数据，在 `.build/releases/` 生成可审核的清单和文件。
3. 更新官网下载页面的三种语言与 `site-facts.json`，下载入口在 JS 未运行时也可见。
4. 在网站现有 `mdo` 插件发布八条记录，保留通知和工具包；按 hash 保存不可变对象。
5. 分别验证八条公开更新接口、实际下载 hash 和官网三种语言。

有限功能测试：`tests/test_update_installer.py` 检查替换、回滚、身份与 hash 拒绝，
`tests/test_windows_server_update.py` 使用真实打包程序检查网页下载/安装/重启、
桌面版拒绝及端口/Home 保留；home 仓库 `tests/test_mdo_linux_delivery.py` 实际上传
八个文件，验证独立通道、非法组合和编号冲突。无压力或高负载测试。

本机非管理员，SCM 注册服务的安装重启未做实机测试；普通 Windows 服务版的网页
更新已实测。Linux 和 APK 本轮沿用先前的构建验证，未新增安卓真机安装测试。
