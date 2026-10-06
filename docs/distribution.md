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
30000001、30000002、30000003，尚未发布。

客户端连接公开 WebSocket 收到目录变更后重新获取目录，同时每五分钟补充轮询。
断网时保留缓存通知，首次离线启动也能显示本地安装与登录说明。推送只在应用
运行时生效，不包含手机离线时的厂商系统推送。

## 外部工具

Windows 常用包包含 BusyBox、curl、jq、OpenSSH 的 ssh/scp/sftp 及匹配的依赖；
Python 为单独可选包。Git 已分类保存为发布素材，本轮未加入自动安装目录。
入口为“设置 → 常规 → 扩展工具包”。工具包采用 XRTPACK，下载上限 512 MiB，
流式下载并验证大小和 SHA-256，提取时再次验证每个文件。只有实际运行探测通过
后才原子切换 `data/toolpacks/active.json`；失败或取消保留当前版本。
已安装的旧版本目录暂时保留，后续可增加显式清理入口。

新任务和恢复的任务均会刷新 `<mdo_tool_environment>`。该段只包含已探测到且
文件存在的工具及绝对路径，去除旧段避免重复；提示词说明 BusyBox applet 用法、
Windows Shell 差异和 Python 的可选性质。线上通知正文不会注入模型提示词。
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
以 64 KiB 块流式下载。每块上传最多 2 MiB、每个文件最多 512 MiB、两个并发上传。
旧客户端继续使用 `/update/version` 和原更新 URL，受旧 32 MiB 限制，应先发兼容
精简版本，再使用新客户端的大文件能力安装完整版。

激活前停止目标网站宿主。以下命令默认只预览，不会启用或发布：

```powershell
python D:/GIT/home/host/xywhsoft_ai/plugin/mdo/tools/activate.py
python D:/GIT/home/host/xywhsoft_ai/plugin/mdo/tools/publish.py .build/releases/manifest.json
```

离线激活脚本使用 `--apply` 建立主库／配置快照，合并旧插件选项，停用四个旧插件，
启用 mdo，保留 billing。发布脚本使用 `--apply`，从运行时环境 `MDO_ADMIN_COOKIE`
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
旧更新接口及工具包发布。客户端隔离测试涵盖校验失败、取消、原子激活、五个
执行探测、恢复提示词及重启后的持久化；APK 检查覆盖 CRC、编号、完整依赖闭包、
原生／数据映射和 16 KiB 对齐。

本轮生成的 APK 使用持久开发签名；公开发行应沿用已发布应用的正式签名流程。
手机暂未连接，尚未验证完整版在真实 mdo 应用 UID 下的工具执行、Python 标准库、
SSH/SCP/SFTP 和精简版覆盖升级。网站源代码、离线迁移和发布工具已准备，真实
网站未切换插件、未上传产物、未发送通知。
