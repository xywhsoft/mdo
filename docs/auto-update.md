# 墨斗自动更新方案

状态：设计方案，尚未接入客户端。2026-10-04。

目标是为单文件墨斗增加轻量更新能力，第一批实现 Windows x86_64 和
Android arm64-v8a。采用后台检查、用户下载、确认安装的流程；检查和下载不
中断对话。桌面端继续保持一个可执行文件和 `mdo-home` 的分发结构。

## 1. 当前代码与参考实现

当前版本来自 `app/include/mdo/version.h`，固定为 `0.1.0-dev`。
`tools/build_android.py` 又独立写死 versionName，并默认按日期生成 versionCode。
同一天多次构建无法可靠区分更新，必须先统一版本来源。

参考 `D:\GIT\auto_test_ide`：

- `src/update/update_client.c`：启动后用工作线程检查版本，主线程提示用户。
- `src/update/update_main.c`：独立更新器等待父进程退出，再安装和重启。
- `tools/prepare_update.py`、`docs/auto_update.md`：生成大小、SHA-256 和发布清单，
  先上传文件，最后激活版本。

沿用退出后替换、下载校验和最后激活的思路。墨斗目前的 EXE/APK 均约 6 MB，
适合整包下载；不引入参考项目的多文件增量、LZMA 分片和删除清单。
参考更新器直接写最终文件且没有回滚，墨斗应增加旧 EXE 备份与替换失败恢复。

本机实测 `https://mdo.xywhsoft.com/update/version`：Python、PowerShell 和
Windows Schannel curl 均未通过证书校验；curl 报 `SEC_E_UNTRUSTED_ROOT`。
因此尚未确认接口正文、版本格式或下载地址。此结果只说明本机当前连接的
证书链不被信任，尚不能断言故障必然在源站。客户端保留证书与主机名校验；
正式联调前需修复证书链或明确受信任 CA 配置。

## 2. 版本来源

增加仓库根目录 `version.json`，作为发布版本的唯一编辑入口：

```json
{
  "version": "0.2.0",
  "release_code": 2026100501
}
```

这是建议格式，文件尚未创建。version 为展示版本，release_code 为严格递增
的发行序号；跨平台共用一个发行序号，不能将字符串按字典序比较。
序号限定为 1～2100000000，与现有 APK 构建器的范围一致。日期加批次仅是
人工编号习惯，构建器不得自行取日期替代发行序号。

两个构建器从此文件生成供 TCC 使用的版本头、APK versionName 和 versionCode。
发布脚本复核包内版本，不允许仅修改服务器清单中的版本标签。新 APK 的序号
必须高于已安装版本；目前按日期构建的 APK 也需要考虑在内。
开发构建可用 release_code=0，默认关闭自动更新，不发布到稳定接口。
本地版本以正在运行的构建信息为准，不能由可编辑的用户配置覆盖。

## 3. 服务器接口

推荐指定的 `/update/version` 直接返回完整 JSON。客户端一次请求便可获得
版本、说明和当前平台的安装包，不再增加一个清单请求。

```json
{
  "schema_version": 1,
  "version": "0.2.0",
  "release_code": 2026100501,
  "notes": "本次更新说明",
  "assets": {
    "windows-x86_64": {
      "url": "https://mdo.xywhsoft.com/update/releases/2026100501/mdo.exe",
      "size": 123456,
      "sha256": "替换为该 EXE 的 64 位小写十六进制 SHA-256"
    },
    "android-arm64-v8a": {
      "url": "https://mdo.xywhsoft.com/update/releases/2026100501/mdo-arm64-v8a.apk",
      "size": 123456,
      "sha256": "替换为该 APK 的 64 位小写十六进制 SHA-256"
    }
  }
}
```

上述 URL、大小、版本与 hash 都是协议示例，未证明服务器存在这些包。
真实字段由发布工具读取实际产物生成，不能手填或发布占位 hash。
如果现有接口必须维持纯版本号，则保留它，并另设 JSON 清单接口；需先确定
清单地址和版本号与清单的一致性规则，再接入，不能猜测下载地址。

接口要求：

- `Content-Type: application/json; charset=utf-8`，`Cache-Control: no-store`。
- JSON 最大 64 KiB，拒绝重复键、未知 schema 和无效整数；notes 最大 4 KiB，
  前端按纯文本展示。缺少当前平台包时展示版本与“不支持自动安装”，不选用其他包。
- 下载 URL 使用 HTTPS，第一版仅允许 `mdo.xywhsoft.com` 同源；重定向的每一跳
  也要复核，不降级到 HTTP。未来使用 CDN 时显式扩展可信来源名单。
- size 为正整数，初期上限 128 MiB；hash 必须是 64 位十六进制。
- `release_code <= 当前版本` 不发更新提醒；不通过修改清单执行静默降级。

SHA-256 验证下载字节是否符合清单；清单真实性依赖经过验证的 HTTPS，不能把
“hash 正确”描述成独立的发布者签名。第一版不增加自建签名 PKI。

## 4. 用户操作

在设置的“常规”页增加“应用更新”卡片，显示当前版本、检查结果、更新说明和
一个随状态变化的主要按钮。沿用现有设置框架，不新增侧边栏入口或调试面板。

- 默认启动完成后延迟 5 秒自动检查；持续运行时每 6 小时检查一次。
- 保留“自动检查更新”开关和“检查更新”按钮。默认不自动下载。
- 发现新版时只出现一次非阻塞提示；关闭提示后仍可在设置中更新。
- 下载时显示进度和取消；校验完成后 Windows 显示“安装并重启”，Android
  显示“安装更新”。不自动关闭程序或打断正在执行的任务。
- 安装前有活动运行、启动中的运行、后台工具/子 Agent 或存储迁移时，说明
  仍有任务正在执行，允许用户稍后重试；不强行取消工作。
- 下载或安装失败保留可操作错误与重试按钮。自动检查失败不连续弹窗。

远程浏览器中的版本指宿主版本，更新的也是宿主程序，按钮应明确说明这一点。
远程浏览器第一版只能检查、查看和下载到宿主缓存；安装与退出操作限定于
可验证的本机原生窗口。不得仅根据请求来自 loopback 就认定原生授权。

## 5. 缓存、下载和状态

默认桌面结构：

```text
mdo.exe
mdo-home/
  config/                         # 原有用户配置
  data/updates/
    <release_code>-<hash>/
      manifest.json              # 校验后固定下来的本次清单
      package.part               # 正在下载
      package.exe 或 package.apk # 大小/hash 校验通过才发布
    helper/<helper_hash>/        # 按需提取的 Windows 更新器
      mdo-update.exe
    previous.exe                 # 最近一次替换的旧 EXE
    install.json                 # 小型本地安装状态，供失败恢复
  sessions/                       # 原有数据，不参与包替换
```

状态：`idle → checking → available → downloading → ready → installing`，
失败和取消回到可重试状态。进度、错误和当前任务由一个更新管理器持有。
只检查版本时保存在内存中，不因检查强制创建 Home；显式下载才申请 Home 写入。
只保留一个候选包和一个旧 EXE，下次正常启动清理已退出的旧 helper 和临时文件。

网络操作放在受管理的工作线程中，HTTP 路由只提交任务和读取快照。
检查连接超时 10 秒、总时限 20 秒；下载连接超时 10 秒、空闲超时 30 秒、
总时限 10 分钟。采用流式写入和 SHA-256，不把包或压缩包全部加载到内存。
第一版取消后重下，不增加断点续传与补丁链。

复用已有传输代理和显式 CA 配置，始终验证更新服务器证书，不继承模型
Provider 的“关闭证书验证”选项，也不发送模型 Key、聊天、项目或设备标识。
停止时先取消请求、停止定时器并等待 worker/callback 退场，再释放状态与锁。
更新子系统失败不能导致墨斗启动失败。

## 6. Windows 安装

使用小型原生 C helper，只负责等待、校验、替换、必要时恢复和重启，不处理
网络与 UI。构建时将 helper 放入应用 VFS，安装时提取到上述目录；分发时仍
只有 mdo.exe，不额外要求用户携带 update.exe、Python、PowerShell 或 Bash。
helper 使用系统 API，不能拼接 shell 命令；支持 Unicode 和含空格的路径。

安装流程：

1. 在同一个安装入口内关闭新任务准入并暂停调度认领，排空已经开始的启动流程
   和写入，再检查所有活动工作。单次读取活动数不能替代这个并发边界。
   若忙则恢复准入并返回“稍后重试”，不在后台悄悄等待安装。
2. 确认候选包与固定清单一致，拿到安装互斥，建立 helper 与父进程的握手。
   未准备好 helper 时保持原应用可用，不能提前退出。
3. helper 持有父进程句柄，父进程正常关闭服务与窗口。helper 等待真正退出，
   不根据 PID 轮询后强杀，也不替换仍被其他实例占用的文件。
4. helper 再次验证包大小/hash、目标原文件身份和路径约束。目标是宿主确定的
   当前可执行文件，不接受服务端路径、前端绝对路径或任意命令行。
5. 使用同卷的 Windows 替换操作，将旧 EXE 保存在 `previous.exe`，写入安装
   状态后启动新 EXE，继承明确记录的 Home 和启动上下文。
6. 替换失败保持或恢复旧 EXE。恢复失败时保留备份与清楚的人工恢复指引，
   不把失败状态写成安装成功。新程序正常就绪后确认安装完成。

启动确认只用于识别安装结果；不能在新版本已写入会话后盲目回滚旧程序。
启动失败保留旧包供显式恢复，并说明数据格式兼容限制。第一版更新不携带
用户数据迁移；需要不可逆迁移的版本另行制定迁移流程。

默认 Home 与 EXE 同卷。若用户用 `--home`/`MDO_HOME` 指向其他卷，或目录
没有写权限，第一版保留下载和手工替换指引，不偷偷提权或在其他位置散落文件。
重启保留原 Home；不得因 helper 位于缓存目录而把缓存目录误作应用根目录。

## 7. Android 与其他桌面平台

Android 同样检查与下载完整 APK，但安装交给系统完成，不能替换运行中的
`libxs.so` 或仅覆盖 `app.xrtpack`。包名维持 `org.xleaves.mdo`，包的
versionCode 要增加，且更新必须沿用现有安装的签名身份。

xs Android 宿主需要补充窄范围安装桥接：原生窗口传递本地已验证安装票据，
桥接复核来源、票据、包名、版本、签名和候选文件，不接受任意路径/URL。
目前 Manifest 没有安装权限或安装用 ContentProvider，需要补齐
`REQUEST_INSTALL_PACKAGES` 和一个只暴露已验证 APK 的只读 ContentProvider，
使用 `content://` 与临时读权限交给安装器。已有构建未引入 AndroidX，可以用
窄范围原生 Provider，避免仅为此功能扩大依赖。

若未获“安装未知应用”授权，则由用户动作进入系统授权页，返回后重试。
系统取消或未完成安装时仍保持旧版本；再次启动从实际已安装版本判断成功。
提供 APK 给安装器之后，候选文件不能在安装器读完之前被缓存清理。

相关平台依据：Android 官方
[canRequestPackageInstalls](https://developer.android.com/reference/android/content/pm/PackageManager#canRequestPackageInstalls())、
[REQUEST_INSTALL_PACKAGES](https://developer.android.com/reference/android/Manifest.permission#REQUEST_INSTALL_PACKAGES)、
[content URI 与临时读权限](https://developer.android.com/training/secure-file-sharing/share-file)。

当前 APK 使用构建机的持久开发签名。正式发行前需确定签名身份并备份密钥；
更换签名不能当作无损原位更新。密钥不进 Git，发布工具不能悄悄生成新身份。

Linux 后续可复用同卷 rename 与权限保留；macOS 必须结合最终包形态与签名
方式实现安装。没有对应构建、适配器和真机验收的平台不宣传自动安装支持，
可显示版本及手动下载入口。

## 8. 模块边界与本地 API

计划新增以下文件，均为普通 C/JS 模块，不引入插件框架或治理服务：

```text
app/include/mdo/updates.h
app/src/updates/manager.c          # 状态、检查、下载、生命周期
app/src/updates/manifest.c         # 严格解析、版本与平台选择
app/src/updates/install.c          # 本机安装票据、活动工作准入
app/src/api/updates.c              # 现有 HTTP API 的薄适配
app/web/js/features/settings/update-panel.js
tools/update-helper/windows.c     # 原生 helper
tools/prepare_update.py           # 生成发行清单，不负责上传
version.json
```

接入现有 `app/sources.json`、bootstrap init/unit 和设置框架。
产品行为留在 mdo；xs 仅增加通用的安全关闭请求及 Android 安装桥接。
目前 xs 原生侧有 `xsHostRequestStop`，TCC SDK 没有对应关闭入口，应补一个
线程安全、请求式的 SDK 适配，不能从 HTTP handler 直接拆毁 host。
xrt 继续提供 HTTP、哈希、文件和线程基础能力，无须修改 xllm、xllm-session
或 xwork 来实现版本检查。

本地路由建议：

| 路由 | 用途 |
| --- | --- |
| `GET /api/v1/updates` | 版本、状态、进度、错误、安装能力快照 |
| `POST /api/v1/updates/check` | 异步检查；已检查中时复用当前任务 |
| `POST /api/v1/updates/download` | 下载指定发行序号与 hash 的已知候选 |
| `POST /api/v1/updates/cancel` | 取消检查或下载，不能取消已开始替换 |
| 原生安装动作 | 消费短期本机票据，确认安装与正常退出 |

写请求沿用当前 API 写入校验和 Home 迁移边界。状态页可见且正在检查/下载时
每秒读快照，离开页停止轮询；不为更新新增全局 event/audit 系统。
下载固定在一份清单上；下载过程中服务器发布下一版本不能改变本次包身份。

## 9. 发布与实施顺序

1. 统一版本：增加单一版本文件，两个构建器和 bootstrap 共用，确认旧 APK
   可以接受新 versionCode。提交这一阶段。
2. 生成清单：`prepare_update.py` 只接受显式的平台产物，复核版本、APK 包名
   和签名，流式计算 size/hash。不得扫描整个工作区或打入 mdo-home。
3. 接入检查/下载及设置卡片：使用本地可控 HTTP 测试服务覆盖错误和取消；
   正式接口 TLS 可用后联调，提交。
4. Windows helper、xs 正常关闭入口及安装握手；在隔离目录中替换和重启，
   然后生成单文件包并提交 mdo/xs 阶段。
5. Android 安装桥接与构建配置；用同签名的递增版本 APK 实测原位升级、
   取消与会话保留。更新 deps.lock，提交阶段。
6. 验收后生成正式清单和产物。上传需单独授权：先上传不可变版本目录中的
   全部包，再复核可下载性，最后原子发布 `/update/version`。

包 URL 不覆写，长缓存可用 immutable；版本接口不缓存。
发布前验证 size/hash 匹配真实服务响应，而非只验证本地文件。
若某个平台未验收，只发布已通过的平台项，不放入不可用的下载承诺。

## 10. 验收范围

只做必要的单元、集成与小型端到端测试，不进行压力/高负载测试。

- 版本解析与比较：同版、旧版、新版、同日第二版、错误 schema、缺包、
  重复键、溢出、过大正文、错误 URL/hash。
- 下载：无网络、超时、HTTP 错误、截断、错误大小/hash、重定向、取消、
  空间/权限不足、退出期间 worker 与回调安全退场。
- 并发：重复点击仅一个任务；安装检查与新 run、队列、计划任务认领的
  竞态；存储导入时不能申请下载/安装写入。
- Windows：中文/空格路径、EXE 重命名、不同卷 Home、另一个实例占用、
  helper 启动失败、替换失败、各步骤中断后恢复、明确 Home 的重启。
- 数据：EXE 与内置资源更新后，配置、项目、会话、Skill、MCP 和自定义
  模块都保留；启动确认失败不触发不安全的数据降级。
- Android：未知来源授权/拒绝、安装取消、包名/签名不符、版本不增、同签名
  原位升级与设备上的对话。模拟浏览器不能代替系统安装实测。
- 前端：桌面/手机设置卡片、进度、取消、焦点与错误重试；不挤占聊天工具栏。

真正完成的判据是 Windows 单文件和 ARM64 APK 均能从旧版本升级到指定新
版本、保留用户数据，且失败仍可启动旧程序。仅显示“发现新版”不算完成自动更新。
