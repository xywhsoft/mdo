# 墨斗自动更新：xadmin 上传插件与 hash 对比

状态：插件、客户端及 xs 原生接口已实现，2026-10-05。
Windows 隔离安装测试、ARM64 真机系统升级已通过。本方案替代上一版发行序号、发布清单与原生更新器方案。
站点工作区已部署插件及所需宿主代码；线上尚未启用并发布安装包，两个平台的公开接口仍返回 404。

核心流程：上传整包并选择是否强更 → 插件计算 SHA-256 → mdo 检查自身 SHA-256 →
与线上比较 → 不同则提醒或阻断 → 下载、校验、原生确认安装。插件版本 1.1.0。

## 1. 站点与插件

- 源码：D:\GIT\x-admin\plugin\mdo-update。
- 目标站点：D:\GIT\home\host\xywhsoft_ai。
- 更新接口：https://ai.xywhsoft.com/update/version。

目标站点使用 xadmin ABI v4，本功能要求 4.2 SDK 的 XAdmin_ReplyBinary。
已同步 plugin_sdk/xs_plugin.h、modules/plugin_host.h 和 src/net/plugin_async.c，
复用插件路由、后台菜单、权限、CSRF 和 multipart 接口。二进制响应采用有期限的
分块发送，避免较大 EXE/APK 超过普通响应的发送队列限制。
此次只部署更新组件，没有把站点整体重新同步到 x-admin 主线。

后台菜单名“墨斗更新”，页面只有 Windows、Android 两张上传卡片。支持点击
选择或拖入文件，可选填写一句更新说明，显示当前文件名、大小、hash 和上传时间。
上传成功即生效，不需要填写版本号、hash 或制作发布清单。
两个上传表单各有“新包强制更新”开关，默认关闭；当前包旁显示发布策略，
可修改“当前包强制更新”并保存，无需重新上传。保存携带当前 hash；包已更换时
返回 409，管理员刷新后重新确认，避免把旧页面的策略应用到新包。

Windows 上传 mdo.exe，Android 上传 mdo-arm64-v8a.apk，两个平台独立。
重复上传相同文件不改变当前包。复用现有管理员登录、插件权限与
XAdmin_CheckAdminCSRF；检查和下载公开，无需墨斗账号。
不用通用附件的会员、配额或计费机制，不增加发布审核与历史管理页面。

最小文件结构：

~~~text
plugin/mdo-update/
  plugin.json
  main.c
  page/index.html
plugin_data/mdo-update/
  current.json                    # 两个平台当前包的信息
  packages/<sha256>.exe
  packages/<sha256>.apk
~~~

上传处理：限制大小（初期每包 32 MiB，与宿主请求体上限协调）→ 临时落盘 →
核对基本格式和平台 → 从实际文件计算 SHA-256 → 保存 hash 命名的整包 →
原子更新 current.json。失败不影响线上当前包，不执行上传文件。

EXE 检查 x86_64 PE 与 xs 打包结构，APK 检查当前单体 ARM64 包的基本结构；
APK 的包名、签名和安装兼容性在手机端进一步检查。
包文件按 hash 固定，发布期间不覆写已有下载文件。默认保留当前和前一个包；
更旧包发布成功后清理；进行中的下载持有独立字节，不受清理影响。
旧链接不存在时返回 404，客户端重新检查。

## 2. HTTP 接口

~~~text
GET /update/version?platform=windows-x86_64
GET /update/version?platform=android-arm64-v8a
~~~

省略 platform 默认 Windows，便于直接打开原地址；未上传的平台返回 404。
version 是沿用的路由名，更新判定只使用 hash。

响应示例（hash 与大小为示意，真实值由插件计算）：

~~~json
{
  "platform": "windows-x86_64",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "size": 6570775,
  "url": "/update/download/windows-x86_64/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "updated_at": 1791126000,
  "filename": "mdo.exe",
  "notes": "优化项目创建体验",
  "required": false
}
~~~

| 接口 | 用途 |
| --- | --- |
| GET /admin/mdo-update | 上传页面 |
| GET /admin/api/mdo-update | 当前包信息 |
| POST /admin/api/mdo-update/upload | multipart：platform、file、可选 notes、required（true/false，默认 false） |
| POST /admin/api/mdo-update/policy | JSON：platform、sha256、required（布尔值）；管理员权限和 CSRF，原子保存 |
| GET /update/version?platform=… | 当前平台的包信息，Cache-Control: no-store |
| GET /update/download/{platform}/{sha256} | 下载固定包，二进制原样返回 |

平台只接受上述两种值，hash 必须为 64 位十六进制。插件构造下载路径，不接受
任意路径。客户端只接受更新站点同源 HTTPS 下载，校验大小/hash，说明按纯文本
显示。hash 核对字节一致性；发布者可信性仍依赖正常校验的 HTTPS。

## 3. mdo 启动检查

只在打包版启用，开发模式不把 xs.exe 当成墨斗发行包来检查。普通启动在更新
工作线程中计算自身 hash 并请求接口；存在有效缓存策略时，先校验本地包并加载
策略，再启动任务执行器。进程内复用自身 hash，不重复扫描整个安装包。
启动检查、前台恢复检查和每 10 分钟自动检查复用同一工作线程。

Windows 使用 xrtPathExecutable() 定位实际运行的 EXE，按块计算 SHA-256。
即使用户重命名程序，也检查和替换这个实际路径。

Android 使用 ApplicationInfo.sourceDir 定位已安装 APK，计算完整 APK hash，
不能用 libxs.so、解出的 app.xrtpack 或下载缓存替代。当前单体 ARM64 APK 适用；
未来采用 split APK 时另行调整。
依据：[Android sourceDir](https://developer.android.com/reference/android/content/pm/ApplicationInfo#sourceDir)。

hash 相同不提示。非强更不自动弹窗：展开侧栏时在“墨斗”右侧显示红色数字 1，
侧栏收起或手机布局时在菜单按钮右侧显示红色“更新”。点击打开更新弹窗，可
“稍后”关闭；入口持续保留。“设置 → 常规”也保留检查、下载和安装入口。

强更且 hash 不同时自动打开阻断弹窗，没有“稍后”，Escape 和背景点击不能
关闭，只能检查、下载、取消下载、安装或退出。后台同样拒绝普通写入请求，
会话运行器和计划任务执行器也直接检查限制；仅隐藏输入框不足以形成约束。
检查/下载/安装和运行/任务取消仍可调用，正在运行的会话、Shell、子 Agent 和
计划任务请求协作取消，保留已产生的结果；安装仍须等待它们排空。

`available`、`required`、`blocked` 独立于操作状态；下载失败、原生确认取消、
检查失败不会把强更变成可用旧版。有效策略原子存入
`mdo-home/data/update/policy.json`，已知强更离线重启后仍有效。404 或格式错误
不代表撤销；只有有效线上策略变为非强更或当前包 hash 匹配才解除限制。
从未获知强更的离线启动继续可用，普通检查不创建策略文件。网络失败保留最后
一次有效状态；文件无法保存会明确提示 Home 写权限问题。

前端定时 GET 只读取本地状态（操作中/强更 1 秒、空闲 60 秒），不会重复访问
更新服务器。前台恢复会请求后台检查，服务器周期检查不依赖页面保持打开。

hash 不同只表示与线上当前包不同，不能判断版本先后。按此方案，上传者决定
线上当前包；手工修改或重新签名的本地包也可能被提示更新。替换仍需用户确认；
APK 的签名和版本兼容性继续由原生安装机制检查。

## 4. Windows 命令行替换

采用“下载并校验 → 确认重启 → 旧进程退出 → 替换 → 启动”的顺序。下载失败
时旧程序继续运行，下载完成也不直接结束正在执行的 Agent 任务。

缓存全部放在 mdo-home/data/update/：校验后的 new.exe、
旧版备份 old.exe、固定 install.ps1 与脚本参数 JSON。脚本随 VFS 内置，安装时
提取，使用 Windows 自带 PowerShell 隐藏启动，不需另带 update.exe。
客户端复用 Home 的原子临时文件写入，落盘后再核对 hash，不维护单独的 .part 状态。

脚本只做四件事：

1. 等待本次墨斗进程正常退出，用具体进程身份，不按程序名称结束所有实例。
2. 备份原 EXE，再把校验好的新包替换到原路径，失败保留或恢复原 EXE。
3. 启动原路径的程序，保留原 Home 和必要启动参数。
4. 留下简短失败结果，供下次启动显示；成功后清理临时文件，只保留一份旧包。

脚本固定，路径和参数从 UTF-8 JSON 读取，不把路径嵌入脚本文本，不使用
Invoke-Expression；替换前再次核对新包 hash。命令行文件操作在 PowerShell
内完成，采用 LiteralPath，支持中文、空格和特殊字符路径。

父进程先成功启动脚本，再请求正常关闭。有活动任务时提示用户结束任务后
重试，不把 taskkill 强杀作为正常流程。脚本被系统策略阻止、文件被占用或无
写权限时，保留旧程序与新包并给出手工替换提示。不能先删除旧 EXE。
安装与退出由本机原生窗口确认，远程浏览器不直接结束宿主。

配置、会话、项目和扩展不参与更新；EXE 重启后继续从原 Home 读取。
不需要发行状态机、安装事务服务或完整发布工具。

## 5. APK：hash 检查与系统安装

下载使用 Home 原子写入，保存到应用私有的 mdo-home/data/update/new.apk，
核对大小/hash 后开放安装。手机端检查包名为 org.xleaves.mdo、当前签名一致，
versionCode 不低于已安装版本；完整安装验证由安卓系统完成。

xs Android 只补充两个通用能力：提供当前已安装 APK 的路径；让本机原生窗口
请求系统安装已验证 APK。可使用系统 PackageInstaller，不新增 AndroidX 依赖。
有活动任务时先结束任务再确认安装，必要时引导“安装未知应用”授权。
取消或失败不退出应用、不卸载旧包；非强更可继续使用，强更保持阻断并可重试。
依据：[Android PackageInstaller](https://developer.android.com/reference/android/content/pm/PackageInstaller)。

APK 不能通过命令行直接覆盖安装目录或绕过系统安装确认。hash 负责判断文件
是否变化，APK 的签名和 versionCode 仍遵循安卓规则。继续使用现有
--version-code，发布时建议每次递增，同日发布也显式指定；不新增跨平台
发行序号文件。升级用同一签名身份，换机器不能自动生成新密钥用于原位升级。
依据：[Android 版本规则](https://developer.android.com/studio/publish/versioning)。

## 6. 实施和验收

1. 在 x-admin 实现插件与接口，用隔离数据验证并提交 Git，再部署到目标站点。
2. mdo 接入启动 hash 检查与更新提示，提交。
3. Windows 固定脚本、正常退出和重启，在隔离目录验证并提交。
4. xs Android APK 路径与安装桥接、mdo 接入；同签名真机升级、会话保留通过后
   更新依赖与产物并提交。

必要验证：上传失败不改变当前包；相同/不同 hash；错误或截断下载；上传新包
期间旧包下载；Windows 文件占用、脚本启动失败和中文路径；APK 安装取消、
签名错误、同签名原位更新和更新后会话保留。不做压力或高负载测试。

## 7. 部署、源码和验证记录

在目标站点后台启用 mdo-update，给负责发布的管理员分配 mdo-update.manage，
进入“墨斗更新”上传经过验证的整包。上传后用公开接口核对插件返回的 hash，
再从不同 hash 的本地版本验证提示。若旧宿主仍在运行，先重新加载站点代际使
4.2 SDK 和插件宿主实现生效；反向代理需允许至少 33 MiB 请求体。
插件数据不得作为静态目录公开。此次未改管理员权限、未重启共享线上服务、未发布公开包。

客户端主要文件：app/src/update/manager.c、windows.c、app/src/api/update.c、
app/update/install.ps1、app/web/js/features/update/update-panel.js。
xs 的 --package-install 为可选构建项，墨斗 APK 开启；普通 xs APK 不声明安装权限。
生产客户端固定使用 https://ai.xywhsoft.com，没有运行时切换更新源的配置。
测试通过编译宏覆盖更新源，仅存在于隔离测试包。

本机验证命令（均为功能测试，无压力或高负载测试）：

~~~text
python D:/GIT/x-admin/tests/mdo_update_e2e.py --exe <候选EXE> --apk <候选APK>
python tests/test_update_runtime.py --host <带新API的xs.exe>
python tests/test_update_installer.py
python tests/test_schedule_executor_runtime.py --host <xs.exe>
python tests/test_run_manager_runtime.py --host <xs.exe>
node --test tests/test_*.mjs
~~~

- 插件：42 项上传、下载、权限、CSRF、持久化、损坏拒绝和保留策略检查通过。
- 客户端：8 组实际 xs/TCC/HTTP 测试通过，包括错 hash、取消、同源检查、安装期间写入暂停。
- Windows：实际替换/备份/重启、Unicode 与特殊字符参数、文件占用、错误 hash、进程身份不匹配通过。
- 前端：354 项 Node 测试通过；真机聊天首页正常显示。
- Android：见 tests/android-update-device.md，覆盖原生和系统取消、签名/降级拒绝及原位更新。

已有的 test_write_admission_runtime.py 在当前并行记忆重构工作区，因 fixture 仍期待
memory/projects/purge-probe.json 而失败；这不是更新功能的通过项。更新写入暂停和
计划任务暂停已分别由更新 HTTP fixture 与 executor fixture 验证。

APK 签名文件仍在忽略目录 .build/android-signing/development.p12。必须另行备份，
或通过 --keystore 指定保留的签名文件；不能换机器生成新签名后当作旧应用的更新包。
公开发布前按实际已安装版本设置递增 --version-code；本次强更支持候选包为 20261016。

## 8. 强更策略验收（2026-10-05）

- 插件 64 项实际 HTTP/TCC 检查通过，包含双平台强更/撤销、权限/CSRF、
  hash 冲突、重载持久化以及策略写入失败保留旧发布。
- 更新器 15 组功能检查通过：严格布尔类型、坏地址/404/离线不能撤销、失败/
  取消不解锁、离线重启和缓存包复验、匹配 hash、有效撤销、后台定时检查。
- 会话和计划任务运行器验证阻止新运行、协作停止已有运行，并保留结果/租约
  至排空。Windows 安装器原有四组替换/回滚校验通过。
- 前端验证真实 API 数据封装、强更不可关闭、普通提醒持久；实际浏览器检查
  桌面侧栏、收起侧栏及 390px 手机布局，无横向溢出；撤销后自动恢复工作区。
- Android 增加 xs 原生确认退出：本地来源校验、原生确认后正常停止服务并关闭
  Activity；APK 构建完成签名与 16 KB ZIP 对齐检查。

旧客户端会忽略 required。首次部署应先让用户更新到支持本策略的客户端，再
开启强更；不能远程让尚未实现强更的历史版本立刻阻断。
插件源码已同步目标站点工作区；本次没有在线发布安装包或修改真实发布策略。
UI 隔离验证入口：`python tests/manual_update_qa.py`，只使用本机发布器和临时 Home。
