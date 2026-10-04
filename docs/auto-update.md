# 墨斗自动更新：xadmin 上传插件与 hash 对比

状态：设计方案，插件和客户端尚未实现。2026-10-04。
本方案替代上一版发行序号、发布清单与原生更新器方案。

核心流程：上传整包 → 插件计算 SHA-256 → mdo 启动计算自身 SHA-256 →
与线上比较 → 不同则提示 → 用户确认后下载、校验、安装。

## 1. 站点与插件

- 源码：D:\GIT\x-admin\plugin\mdo-update。
- 目标站点：D:\GIT\home\host\xywhsoft_ai。
- 更新接口：https://ai.xywhsoft.com/update/version。

已确认目标站点为 xadmin ABI v4，其 plugin_sdk/xs_plugin.h 与当前 x-admin
源码一致，可直接使用插件路由、后台菜单、权限、CSRF 和 multipart 上传接口。
实测新域名 HTTPS 校验通过，/update/version 目前返回 404，更新插件尚未部署。

后台菜单名“墨斗更新”，页面只有 Windows、Android 两张上传卡片。支持点击
选择或拖入文件，可选填写一句更新说明，显示当前文件名、大小、hash 和上传时间。
上传成功即生效，不需要填写版本号、hash 或制作发布清单。

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
更旧包在无下载占用时清理。旧链接不存在时返回 404，客户端重新检查。

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
  "updated_at": "2026-10-04T15:00:00Z",
  "notes": "优化项目创建体验"
}
~~~

| 接口 | 用途 |
| --- | --- |
| GET /admin/mdo-update | 上传页面 |
| GET /admin/api/mdo-update | 当前包信息 |
| POST /admin/api/mdo-update/upload | multipart：platform、file、可选 notes |
| GET /update/version?platform=… | 当前平台的包信息，Cache-Control: no-store |
| GET /update/download/{platform}/{sha256} | 下载固定包，二进制原样返回 |

平台只接受上述两种值，hash 必须为 64 位十六进制。插件构造下载路径，不接受
任意路径。客户端只接受更新站点同源 HTTPS 下载，校验大小/hash，说明按纯文本
显示。hash 核对字节一致性；发布者可信性仍依赖正常校验的 HTTPS。

## 3. mdo 启动检查

窗口启动后在工作线程中计算自身 hash、请求接口，每个进程只计算一次，避免
阻塞 UI。只在打包版启用，开发模式不把 xs.exe 当成墨斗发行包来检查。

Windows 使用 xrtPathExecutable() 定位实际运行的 EXE，按块计算 SHA-256。
即使用户重命名程序，也检查和替换这个实际路径。

Android 使用 ApplicationInfo.sourceDir 定位已安装 APK，计算完整 APK hash，
不能用 libxs.so、解出的 app.xrtpack 或下载缓存替代。当前单体 ARM64 APK 适用；
未来采用 split APK 时另行调整。
依据：[Android sourceDir](https://developer.android.com/reference/android/content/pm/ApplicationInfo#sourceDir)。

hash 相同不提示，不同提示“有可用更新”，提供“更新”“稍后”。网络失败不会
影响正常使用。“设置 → 常规”保留检查和更新入口，无需新增侧边栏按钮。

hash 不同只表示与线上当前包不同，不能判断版本先后。按此方案，上传者决定
线上当前包；手工修改或重新签名的本地包也可能被提示更新。客户端只提示，
由用户确认替换，不自动降级或中断对话。

## 4. Windows 命令行替换

采用“下载并校验 → 确认重启 → 旧进程退出 → 替换 → 启动”的顺序。下载失败
时旧程序继续运行，下载完成也不直接结束正在执行的 Agent 任务。

缓存全部放在 mdo-home/data/update/：new.exe.part、校验后的 new.exe、
旧版备份 old.exe、固定 install.ps1 与脚本参数 JSON。脚本随 VFS 内置，安装时
提取，使用 Windows 自带 PowerShell 隐藏启动，不需另带 update.exe。

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

下载到应用私有的 mdo-home/data/update/new.apk.part，核对大小/hash 后
发布 new.apk。手机端检查包名为 org.xleaves.mdo、签名支持原位升级，
versionCode 不低于已安装版本；完整安装验证由安卓系统完成。

xs Android 只补充两个通用能力：提供当前已安装 APK 的路径；让本机原生窗口
请求系统安装已验证 APK。可使用系统 PackageInstaller，不新增 AndroidX 依赖。
有活动任务时先结束任务再确认安装，必要时引导“安装未知应用”授权。
取消或失败仍可使用旧版本，不先退出应用、不卸载旧包。
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

源站插件部署与客户端实现完成前，不把本方案描述为已经可用的自动更新功能。
