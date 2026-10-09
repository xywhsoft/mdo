# 通知、工具包与统一网站插件

## 客户端行为

侧栏品牌旁的通知区域仅显示图标：更新为红色“更新”胶囊，错误为红色感叹号，
警告为黄色三角形，成功为绿色对勾，普通消息为蓝色 i，优惠为票券图标。
并排展示至多三个入口，数量较多时提供省略图标进入完整列表。顺序为应用更新、工具包更新、
工具包安装说明、登录说明、线上通知；线上通知再按优先级和开始时间排序。
应用与工具更新在完成前持续显示，安装／登录说明可延后一天，线上通知可按
`id + revision` 标记已读或关闭。优惠通知可独立关闭。这些偏好按目标设备保存在
浏览器／WebView 的本地存储；它们不是账号云同步数据。

气泡带有指向对应图标的箭头，悬停或键盘聚焦时显示标题和具体内容，新的未读
通知也会短暂显示八秒。更新气泡显示“有一个新版本发布了”和更新描述；较长
正文在气泡内显示预览，在详情弹窗中完整展示。点击气泡或对应图标均打开同一条
通知的详情。手机直接点图标查看详情。

通知使用纯文本，支持 message、warning、error、success、coupon 五种内置图标，以及
已上传的 PNG/WebP。按平台、发行版、语言、登录状态、时间和内部版本号筛选。
内部版本区间为 `[min_build,max_build)`；0 表示该端不限。例如 `max_build:30000002`
仅通知编号小于 30000002 的客户端。时间为 Unix 秒，结束时间不包含在区间内。
修改同一条通知须增加 revision，客户端才能重新显示。

`app/release.json` 是发行编号来源。Windows、Android lite、Android full 分别占用
一个八位递增整数；公开发布新二进制时必须增加编号，不能复用已发布编号。
`version_name` 用于展示，工具包 revision 独立递增。当前产物编号为
30000006、30000007、30000009，已发布至 ai.xywhsoft.com；工具包修订为 2。

客户端连接公开 WebSocket 收到目录变更后重新获取目录，同时每五分钟补充轮询。
断网时保留缓存通知，首次离线启动也能显示本地安装与登录说明。推送只在应用
运行时生效，不包含手机离线时的厂商系统推送。

## 外部工具

Windows 常用包包含 BusyBox、curl、jq、OpenSSH 的 ssh/scp/sftp、aria2c、ripgrep、7-Zip 及匹配的依赖；
Python 为单独可选包。Git 已分类保存为发布素材，本轮未加入自动安装目录。
入口为“设置 → 扩展能力 → 扩展工具包”。工具包采用 XRTPACK，下载上限 512 MiB，
流式下载并验证大小和 SHA-256，提取时再次验证每个文件，通过后原子切换
`data/toolpacks/active.json`；失败或取消保留当前版本。
安装、更新、修复及回退在文件校验通过后直接启用，不启动工具执行版本查询或功能试运行。
工具路径按已安装文件登记，版本来自保留的包元数据。提示词区分文件完整性校验
`file-integrity` 与启动时通过运行能力检测的 `functional-probe`，不把文件校验等同于功能检测。
安装失败会区分下载连接、响应、大小、整包 SHA-256、解包、单文件校验和启用失败。
最近一次失败写入 Home 的 `data/toolpacks/last-error.json`，记录失败阶段、文件或工具、
预期与实际大小及 SHA-256。下载文件保留为 `data/toolpacks/download.failed`，不会执行；
只保留最近一份，安装成功后自动清理。取消会清理本次临时下载。目录刷新及重启保留
安装错误，避免被后台检查成功覆盖。若文件重命名失败，则保留 `download.pending` 并在
诊断记录中注明；下一次安装重新下载和校验，不复用未校验的文件。
工具管理页显示下载字节进度、提取进度和启用阶段，支持取消、重试、修复、卸载、回退上一包和清理旧包。修复也创建新目录，避免替换正在使用的文件；回退前重新校验全部文件。卸载先原子停用，依赖它的已安装包会阻止卸载。本次运行中停用的目录保留给当前任务，重启后才能显式清理。清理后的包无法再回退。

应用与扩展工具更新共用一个更新窗口，通知入口合并成一个“更新”胶囊。Android 内置工具更新与完整版 APK 更新合并，正常检查保持当前发行版。工具详情列出兼容编号、依赖、大小、来源和许可证。安装状态只保留必要兼容数据，旧目录不会重复存储整份在线说明。

新任务、恢复的任务及子 Agent 均会刷新 `<mdo_tool_environment>`。该段只包含已登记且
文件存在的工具及绝对路径，去除旧段避免重复；提示词说明 BusyBox applet 用法、
Windows Shell 差异和 Python 的可选性质，以及 aria2 下载、rg 搜索、7z/7zz 压缩的使用方式。线上通知正文不会注入模型提示词。
安装不修改系统 PATH。内嵌 Python 不默认带 pip，也不承诺可安装任意原生扩展。

Android 使用两个同应用 ID、同签名的 APK：精简版不带外部工具，完整版带上述
工具及 Python。完整版的全部 ELF 文件作为 `lib/arm64-v8a/lib*.so` 交给系统安装，
应用私有目录只存数据及指向 nativeLibraryDir 的符号链接。数据与原生执行文件
分离，工具更新通过新版完整版 APK 完成；精简版的工具安装按钮引导到完整版更新。
切换发行版需要更大的内部编号，以满足 Android 覆盖升级规则。文件、依赖及
16 KiB ELF／ZIP 对齐检查不能替代真机应用 UID 下的执行验证。

## 网站维护位置与上线

网站插件唯一维护源为 `D:/GIT/home/host/xywhsoft_ai/plugin/mdo`，包括四个兼容模块
search、devices、models、updates 及新目录、通知、对象上传下载服务。不进入
x-admin 仓库。账号和 billing 继续由网站宿主管理，原插件的私有数据库和凭据
路径继续使用。公开通知通道的 SDK／宿主扩展也仅位于此 home vhost。

网站管理入口 `/admin/mdo` 支持维护应用、工具包和通知，分块上传、校验完成后
另行发布元数据；提供独立权限和 CSRF 校验。服务器保存不可变 SHA-256 对象，
以 64 KiB 块流式下载。上传按接口返回的 chunk_size 分块，默认 16 KiB，以兼容当前宿主的 TLS 接收行为；可配置为 1 KiB 至 2 MiB。每个文件最多 512 MiB、两个并发上传。
旧客户端继续使用 `/update/version` 和原更新 URL，受旧 32 MiB 限制，应先发兼容
精简版本，再使用新客户端的大文件能力安装完整版。

激活前停止目标网站宿主。以下命令默认只预览，不会启用或发布：

```powershell
python D:/GIT/home/host/xywhsoft_ai/plugin/mdo/tools/activate.py
python D:/GIT/home/host/xywhsoft_ai/plugin/mdo/tools/publish.py .build/releases/manifest.json
```

离线激活脚本使用 `--apply` 建立主库／配置快照，合并旧插件选项，停用四个旧插件，
启用 mdo，保留 billing；迁移原管理员／会员权限编号，已切换站点会将原授权映射到新编号。发布脚本使用 `--apply`，从运行时环境 `MDO_ADMIN_COOKIE`
读取当前管理员会话，上传全部校验过的文件后维护应用和工具目录。不会改通知列表、
重启网站或保存管理员密码。具体接口、权限和限制见网站插件 README。

## 构建与验证

以下是本机实际使用的构建命令，依赖源码需匹配 `deps.lock`；跳过编译仅允许使用
已通过指纹校验的宿主／原生库。另见 `docs/android.md` 的工具链与签名说明。

```powershell
python tools/build_toolpacks.py --packer .build/host/xs.exe
python tools/build_mdo.py --xserver-root .build/implementation-xs --skip-host-build --output .build/releases/mdo-windows-x64.exe
python tools/build_android.py --xserver-root .build/implementation-xs --wsl --sdk /home/ubuntu/.cache/mdo-android-toolchain/sdk --java-home /home/ubuntu/.cache/mdo-android-toolchain/java/usr/lib/jvm/java-17-openjdk-amd64 --skip-host-build --skip-native-build --edition lite --output .build/releases/mdo-android-lite-arm64-v8a.apk
python tools/build_android.py --xserver-root .build/implementation-xs --wsl --sdk /home/ubuntu/.cache/mdo-android-toolchain/sdk --java-home /home/ubuntu/.cache/mdo-android-toolchain/java/usr/lib/jvm/java-17-openjdk-amd64 --skip-host-build --skip-native-build --edition full --output .build/releases/mdo-android-full-arm64-v8a.apk
python tests/test_distribution_runtime.py
python tests/test_android_distribution.py
python tools/prepare_release.py
```

前端使用 `node --test tests/*.mjs` 与
`node --experimental-vm-modules tools/check_web_modules.mjs`。
网站原生集成测试在 home 根目录运行 `python tests/test_mdo_delivery.py`，使用独立
临时 vhost，包含离线插件迁移、推送、CSRF、版本筛选、超过 32 MiB 的上传下载、
旧更新接口、撤回、依赖校验、编辑冲突，以及超过 256 条后的完整历史分页。客户端隔离测试涵盖校验失败、取消、原子激活、十项
功能探测（BusyBox 管道、curl HTTPS 协议、jq、SSH 配置、Python 标准库／SSL／SQLite／ZIP、SCP 复制、SFTP 握手、aria2 HTTPS 支持、rg 中文 JSON 搜索、7-Zip 创建测试解压）、恢复提示词及重启后的持久化；APK 检查覆盖 CRC、编号、完整依赖闭包、
原生／数据映射和 16 KiB 对齐。

本轮 APK 使用持久开发签名。构建支持已有正式 keystore、key alias 以及运行时密码环境变量，详见 `docs/android.md`；密码不进入命令行或构建日志。

已在连接的 Android 36 / arm64 手机上验证真实 mdo 进程的七个功能探测。独立设备测试还在 mdo UID 下连接隔离 TLS/SSH 服务，完成证书校验、SSH 公钥连接、SCP 上传、SFTP 下载；临时测试证书、主机密钥记录和文件在结束后清除。设备覆盖升级另行核对配置、项目和会话的指纹。网络测试不依赖线上站点、用户账号或真实服务器凭据。

2026-10-07 已切换线上统一插件并发布三个应用产物和两个 Windows 工具包，核对 HTTPS 下载哈希、后台七个独立页面、原样式资源、实时失效事件及旧更新接口。新增三项工具还在 Android 应用 UID 下验证了 HTTPS、四连接下载、续传、中文搜索与压缩解压。对应源码和构建脚本提供公开下载。
