# 五项可选外部工具：构建及发布记录（2026-10-24）

## 范围

新增 Git、SQLite CLI、Poppler、FFmpeg、ImageMagick，共 15 个平台版本。Windows x64 / Linux x64 使用独立 MDOCLI1 包；Android ARM64 的可执行程序仅随完整 APK 分发，不增加动态安装渠道。工具商店中按普通可选工具发布，`recommended=false`；Android 新增五项默认停用，已有工具的用户启停选择保持不变。

## 版本和文件大小

以下是完整、未压缩 MDOCLI1 归档大小；包含每个工具自身的依赖和许可。精确文件名、SHA-256 和上游来源见同名 JSON 收据。

| 工具 | Windows x64 | Linux x64 | Android ARM64 |
| --- | --- | --- | --- |
| git | 2.56.3 / 91.85 MiB | 2.52.0 / 18.03 MiB | 2.56.0 / 19.65 MiB |
| sqlite | 3.54.1 / 4.10 MiB | 3.53.4 / 4.08 MiB | 3.54.1 / 1.82 MiB |
| poppler | 26.9.0 / 70.77 MiB | 25.12.0 / 29.24 MiB | 26.4.0 / 26.91 MiB |
| ffmpeg | 9.0.2 / 53.90 MiB | 9.0.2 / 71.20 MiB | 9.0.2 / 44.37 MiB |
| imagemagick | 7.1.2034 / 32.20 MiB | 7.1.2034 / 55.83 MiB | 7.1.2034 / 21.11 MiB |

商店仅接受三段数字版本。Windows Git 原生版本为 `2.56.0.windows.3`，对应包版本 `2.56.3`；ImageMagick `7.1.2-34` 对应 `7.1.2034`。原始版本保留在构建收据和提示词中。不同平台的版本由各自已验证的源或发行版决定，没有将不同字节标成同一上游版本。

## 体积与可迁移性

- Windows FFmpeg 采用自行交叉编译的共享库版本，约 53.9 MiB；候选全量共享发行版解包约 259.8 MiB，缩减约 79%。包含 x264/x265、VP8/VP9、AOM/dav1d AV1、AAC、MP3、Opus、ASS 字幕、drawtext/fontconfig、WebP、HTTPS 和常用原生编解码/滤镜。未包含 ffplay 或可选 GPU/GUI 着色器依赖，未声称支持 FFmpeg 所有可选组件。
- Windows ImageMagick 仅保留 `magick.exe`，通过子命令访问 identify/compare 等，避免多个相同的大型主程序。
- Linux 使用私有 musl loader 和按 ELF 依赖闭包收集的库；启动包装器从自身路径计算工具根目录，避免依赖构建机绝对路径。系统内核及 `/bin/sh` 仍属于宿主环境。
- Android Git 共享同字节的原生程序和 149 个显式别名。APK 原生文件按哈希去重，同名不同内容拒绝打包。
- Android 新增工具的第三方 SONAME/DT_NEEDED 统一使用 `libmdo_ext_*.so` 私有名称，保留 16 KiB ELF 页对齐。实际发现 generic liblzma 会抢占 Android 平台库并缺失 `XzUnpacker_Construct`，已完成隔离修复。
- Poppler 随附 CMap；字体、fontconfig、CA、Git templates/helpers 和 ImageMagick 配置一并处理。ImageMagick 的 PDF 读取不假定已安装 Ghostscript，提示词建议使用 Poppler。

## Android 正式包

- 应用 ID：`org.xleaves.mdo`。
- 当前源码版本：`0.5.0`；正式完整包 build `50000017`，工具修订号 `4`。
- 文件：`mdo-full-arm64-v8a.apk`，150,100,377 字节（143.1 MiB）。
- SHA-256：`f81254793fd23707f3a5b36171781411650d3e870f6e4f1dfb63a949b56c696b`。
- 签名证书 SHA-256：`94b1947b60f3ce0852b19d9c37a47cc4a084fe9d601692c6a1f6b204c2174e84`；与原线上正式包一致。签名材料只在本机私有目录中读取，没有写入源码或发布材料。
- Java/资源/VFS 由冻结的 `8b98c776` 源码快照构建；复用与锁定 SDK 相同提交 `216f5445163c`、构建 profile 相符的原生宿主。v2/v3 签名和 ZIP/ELF 16 KiB 对齐校验通过。
- 本次仅更新 Android 完整版；其他七个客户端渠道的构建号和文件保持原发布。服务器使用全局递增构建号；这些渠道以后发布时应重新分配大于 `50000017` 的新构建号，不能直接发布本地预留的较小编号。

## 下载修复

真实的慢速 APK 下载在约五分钟被截断，确认了客户端和站点文件流原有的五分钟总时限。客户端文件传输上限改为一小时；站点 blob 流和通用 StreamBegin 上限相应调整。每次发送/排空仍受 30 秒无进展上限约束，完整 SHA-256 和 512 MiB 限制保留。模型代理请求的总时限未改动。

旧客户端内已有的五分钟总时限不会被服务端更新取消。慢速网络上旧客户端若下载失败，需要浏览器下载并覆盖安装，或升级到含客户端修复的版本。官网完整 APK 提示已注明浏览器下载方式。

## 验证

- Windows / Linux 五工具真实运行：Git init/add/commit/log/local clone（中文路径）；SQLite JSON/FTS5；Poppler 文本、元信息和 PNG 渲染；ImageMagick PNG resize/identify/WebP；FFmpeg H.264/AAC 编码、probe、抽帧及核心编码器/滤镜/HTTPS 清单。
- Windows / Linux Git 补充实际访问 GitHub 的 HTTPS ls-remote 查询，通过 CA 校验及远端引用读取。
- Android API 26 系统库 + QEMU AArch64：五工具实际执行通过。Git 覆盖本地写入和 HTTP helper capabilities；没有声称已完成 QEMU 子进程远端克隆或手机真机安装/UI 验证。
- 实际 15 个归档均经原生 xs/TCC 的导入、逐文件清单校验、审核发布、公开详情查询通过。
- 实际 150 MB 正式 APK 经原生发布校验、更新选择和完整下载哈希验证通过。
- `tests/test_android_runtime_aliases.py`、Android distribution 完整包检查通过：734 个运行时路径、150 个原生 ELF（含宿主）。
- 站点 `tests/test_large_download_deadlines.py` 直接执行真实 StreamBegin/Write 的确定性时钟回归：301 秒仍能发送、31 秒停滞拒绝、一小时总上限生效。
- `tests/test_optional_tools_publisher.py` 覆盖发布时后台对象清理竞争：提交临时 upload 引用后才放入不可变对象，最终事务将其替换为正式工具引用；临时引用被清除。发布器同时兼容线上 Python 3.10。
- 生产所用 Linux xs 运行时的独立 loopback 实例已验证三个服务端源码改动可载入，未修改生产账号或使用生产 MFA 私钥。
- 原 `home/tests/test_mdo_delivery.py` 的全量测试在旧菜单集合断言处失败（仍期待已淘汰入口）；不能将其记录为通过。本次发布使用上述针对真实文件的新隔离校验，不覆盖其他任务的菜单改动。

## 对应源码和运维材料

对应源码包：`https://ai.xywhsoft.com/mdo-sources/optional-cli-20261024-sources.tar.gz`。
大小 261,174,761 字节，SHA-256 `18d75c3f90e5f52e3f764bd503d36882186a170338fc49aed65821665f2975b8`。包含上游源码归档、vcpkg 快照、端口覆盖和 triplet、构建/收集/命名空间脚本与锁定输入；不包含 mdo 应用源码或签名材料。

私有现场：`.build/optional-tools-20261024`；线上操作目录：`/home/xs/mdo-optional-tools-20261024`。SQLite 使用在线 backup API 备份，再插入缺失的官方版本；不可变版本冲突会中止。只定点更新下载流源码、Android full 元数据及三个语言下载页，保留其他并行任务的改动。

## 上线收据

- 已上线：2026-10-24T13:52:10+08:00，更新目录修订号 `26`，Android full build `50000017` / toolpack `4`。
- 五工具 × 三平台共 15 条版本已发布，均为普通可选工具；原有推荐组合未增加成员。
- 使用实际生产 TLS 路由（loopback 连接，保留真实 SNI/Host 和证书验证）逐个完整读取 15 个工具归档、自动更新 APK、官网 APK 和对应源码包，18 次完整下载均通过大小与 SHA-256 校验。
- 本机通过公网再次核对三平台商店的五项版本、Android 更新元数据、官网下载入口和源码入口；Windows SQLite 归档另做公网完整下载与 SHA-256 校验。
- 已验证 `xs.service` 为 active；`/opt/www/xs`、`/opt/www/xs.json` 原始 SHA-256 保持不变。只部署三个下载流源码文件、Android full 元数据和官网四个静态文件；账号、会话及计费数据库没有执行改写操作。
- 线上账号只授予免密 `systemctl restart xs.service`，没有免密 stop 权限。首次操作在 stop 权限检查处中止，服务保持运行；核对已发布的 15 个不可变工具后，采用原有 restart 授权完成原子文件切换，未扩权。
- 新旧正式 APK 的签名证书一致。官网三个语言页面的完整包版本、大小、构建号和哈希已更新，其他七个下载渠道维持原发布。
- 备份和逐项收据保存在私有运维目录，供回溯和必要时定点恢复。未将任何密钥或令牌放进发布材料。
