# 2026-10-30 Android 真机验收补充

本轮补齐 Android 真机核心回归，发现并修复 4 处问题。修复后的完整版通过工具执行、进程输出、会话恢复和覆盖安装检查；**整体版本仍未达到发布放行标准，未发布。** 上一轮的 SDK 制品路径回归、历史发布门禁，以及本轮观察到的工具刷新性能问题仍待处理。

工作从 10 月 29 日延续至 30 日。设备为 RMX5090、Android 16 / API 36、ARM64。测试使用独立开发签名包 `org.xleaves.mdo.qa.release`（Mdo Release QA），版本保持 `0.8.0 / 05000800`，没有修改正式版本编号、线上下载或更新。

机器可读收据见 [验收结果](2026-10-30-android-device-acceptance.json)。原始证据位于 `.build/android-device-20261029/`。工作区仍含另一任务未提交的扩展依赖改造；本次只提交自己的修改，不能把整个工作区当作干净的正式发行源码。

## 检查结果

| 检查 | 结果与范围 |
| --- | --- |
| 精简版初始候选 | 42 项断言通过，覆盖 SDK 进程输出、Agent 进程工具及扩展目录；这是修复前初始包的实际结果。最终精简版已重建并通过包内检查，最后一轮加固后未再次单独安装精简版 |
| 完整版主要回归 | 92 项断言通过；包括与精简版相同的进程/目录检查、13 个程序执行、多媒体处理、启停与文件保护 |
| 最终链接边界补测 | 11 项断言通过；拒绝覆盖未知普通文件、其他包的链接、其他库的链接、含 `..` 的非规范链接，冲突移除后可恢复 |
| 实际 APK 容器 | 12 个 ARM64 C 场景通过，包括已安装的真实签名 APK、签名块零填充和畸形长度/填充拒绝；普通扩展 ZIP 仍拒绝 APK 签名间隙 |
| 打包器单元 | 7 项通过，包括程序与共享库区分、伴随命令导出、重复内容复用及冲突拒绝 |
| SDK 及 Agent 进程工具 | 短 stdout/stderr、空 argv、退出码、长输出完整字节、分页读取、输出独占路径、大小上限、超时保留、错误原因、显式合流和流事件通过；`exec/spawn/status/wait/stop`、`@results` 读取及重启后文件保留通过 |
| 真实程序执行 | BusyBox、curl、jq、OpenSSH、aria2、ripgrep、7-Zip、Python、Git、SQLite、Poppler、FFmpeg、ImageMagick 均在应用自身 UID 下经转发器运行成功 |
| 实际文件处理 | FFmpeg 生成 WAV，ffprobe 读取格式；ImageMagick 生成 1×1 PNG；Poppler 提取 PDF 文本并渲染 PNG；SQLite 执行查询，Python 导入 SSL/SQLite 并输出 200 KB stdout 与 18 KB stderr |
| 目录功能 | 九类目录及逻辑 ID 可列出；深层知识库保存、读取、过期修订拒绝、停启和整目录卸载通过；真实 ARM64 C 编辑器命令调用 SDK 并卸载通过 |
| 会话恢复 | 精简版、完整版均通过本地 SSE 夹具：重试、断连、半截流恢复、配额硬错误、退避时取消、恢复、继续和冷启动后的草稿/测试凭据保留。配额场景预期返回 `daily_token_limit`，并非成功请求 |
| 再次覆盖安装 | `adb install -r` 后确认系统原生目录确实变化；最终 140 个配置及会话文件在安装后、首次启动后哈希均不变，13 个工具有效、Python 可执行，绑定错误和诊断均为空 |
| 编译和 APK 检查 | 当前源码严格 C11 `-Wall -Wextra -Werror -fsyntax-only` 通过；两包签名、zipalign、CRC、逻辑 ID、真实 ELF 程序入口、动态依赖及 16 KiB 对齐通过 |
| 界面观察 | 已查看真实手机截图：会话、恢复后的答复与草稿正常呈现，一个真实 WebView 客户端已注册。截图为 `qa-full-native-screen.png`；这不等于完成全部触控、键盘和无障碍验收 |

模型驱动测试只使用本机有界模拟服务，没有付费模型调用，没有使用真实供应商密钥或账号登录。启停和边界测试串行进行。

## 修复的问题

1. **签名 APK 中的工具索引初始化失败。** 实际 APK 在最后一个 ZIP 条目与签名块之间有零填充，原读取器要求签名块紧接条目结束。初始精简包的填充为 459 字节，初始完整版为 867 字节，均被拒绝；可选初始化失败后完整版显示零个工具。读取器改为从签名块页脚定位，校验双端长度、有限的全零对齐填充及边界，扩展 ZIP 的连续性要求保持不变。初始化错误也保留 ZIP 阶段的具体原因。提交 `a0b19e31`。
2. **可选工具把共享库当作主程序，伴随命令缺失。** 缺少旧目录条目的工具使用递归排序找到的第一个 ELF，Poppler/ImageMagick 因此导出了共享库，FFmpeg 只导出了首个程序。改为收集工具顶层的实际程序，排除共享库和内部子目录；真实包检查进一步验证每个命令是程序，并要求 `ffprobe/pdfinfo/pdftotext/pdftoppm/magick` 等出口存在。提交 `3b505fe6`。
3. **Python 等工具的环境未注入。** 逻辑 ID 已变为 `external-tool:mdo.python`，Android 环境配置仍与 `python` 比较，因此删除了 `MDO_PYTHON_HOME/PYTHONHOME` 等变量。Python 回退到系统原生库目录并找不到 `encodings`。改用目录 ID 配置派生路径；实际 Python、SSL、SQLite、长输出和停用/重新启用后的环境恢复均通过。提交 `09992241`。
4. **APK 更新后旧转发链接无法恢复。** PackageManager 更换原生安装目录，旧链接指向已移除的地址；原代码只承认当前地址，因此把自己的旧链接视为未知文件并中止加载。现在保存转发器宿主收据，识别原有受管链接，并对旧无收据链接限制为同一包、同一 ABI、同一转发器、规范安装路径。只重建受管缓存，未知文件和其他链接保持原样。连续覆盖安装与拒绝覆盖检查通过。提交 `ebf3e168`。

中途有几次夹具参数/返回值解析错误，已经修正后重跑；它们没有被计成产品缺陷或通过结果。最终通过结果以表中证据目录为准。

## 产物与保留现场

最终 QA 产物：

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `.build/android-device-20261029/mdo-qa-lite-fixed.apk` | 6,292,884 | `b8a4de061204be13606d3558fc0553264ac40ce60f71ac5f390988e567fde77f` |
| `.build/android-device-20261029/mdo-qa-full-fixed.apk` | 474,032,232 | `e8f1669159820d452df6fc55286a34f5fbc0c29726fe0586115ac8bb2b4f666c` |

APK 安装序号分别为 70000071 / 70000072；它们与语义版本编码保持独立。两包均为独立 QA 开发签名，不能作为已完成正式签名验收的发布包。复用原锁定 ARM64 宿主，未复制仓库或创建额外的大体积运行时。

- 应用源码指纹（算法见 JSON 收据）：`bec02516320e4403e7c32b31aff606a435b30e51b50700104602ba1ac9368f7e`。
- 最终应用包 SHA-256：`52fb702e88132069a18b8559da49faa19306c495e8d55329835947140636ff02`。
- 提交后重新打包当前 `app`，提取的 `app.xrtpack` 与上述已测应用包字节一致，确认测试期间的应用源码没有漂移。
- ARM64 `libxs.so` SHA-256：`b46b717a3679ecca8f62e4ca728d909254554bbf5df7d3ab995ab635b1ea98bc`。
- 正式包 `org.xleaves.mdo` 保持 0.6.0 / 60000023，包路径、安装时间、更新时间前后相同；没有对其执行安装、停止、清数据或私有数据读写。
- 初始精简版留下的 55 个配置/会话文件中，54 个保持不变；仅 `config/models.json` 因完整版模拟服务的本机端口变化而按测试设计更新。最终覆盖安装检查另对当时的全部 140 个文件逐一核对，无变化。
- 已停止 QA 应用，保留安装和验收数据；本轮创建的 ADB 正向/反向映射已全部撤销，原映射列表恢复。原有系统亮屏设置未改动。当前 QA 进程日志没有发现致命崩溃记录。
- `app/xs.json` 与原有 XRUDP 探针的哈希保持不变；CLI 文件中另一任务的目录 ID 改动仍留在工作区，未混入本轮提交。

## 尚未放行的范围

- [上一轮发布报告](2026-10-29-release-acceptance.md) 中的 **SDK-ARTIFACT-PATH** 仍未修复；80 项历史运行时场景、7 项打包/GUI 门禁也没有在本轮全部补齐。
- **工具刷新/完整校验有性能风险。** 最终 8 次刷新相关的文件/链接边界场景累计耗时 198.641 秒（含 ADB 操作和验证）；上一轮完整版主场景组耗时 268.063 秒。尚未单独分解每个请求和磁盘/目录遍历耗时，不能声称已经优化完成。
- 最终精简包已经构建和静态检查，但未在最后加固后重新进行独立精简版安装。真实账号登录、供应商调用和跨设备传输未包含在本轮。
- 已验证的是 ADB 覆盖安装；**应用内原生确认、系统 PackageInstaller 的大 APK 完整安装链路以及正式签名尚未实测**。不能用 `adb install -r` 替代该门禁。
- 本轮只重建 Android QA 产物，未对八种发行组合重新打包发布。

## 复测入口

下面的 `SERIAL` 必须来自 `adb devices`，`--package` 必须选择独立 QA 包；脚本拒绝正式包 ID。证据目录必须为新目录。

```powershell
python tests/test_android_release_device.py --adb .build/android-platform-tools/platform-tools/adb.exe --serial SERIAL --package org.xleaves.mdo.qa.release --edition full --evidence .build/android-rerun
python tests/test_android_release_device.py --adb .build/android-platform-tools/platform-tools/adb.exe --serial SERIAL --package org.xleaves.mdo.qa.release --edition full --only command-ownership --evidence .build/android-owner-rerun
python tests/test_android_apk_container_device.py --adb .build/android-platform-tools/platform-tools/adb.exe --serial SERIAL --package org.xleaves.mdo.qa.release --evidence .build/android-apk-rerun.json
python tests/test_android_upgrade_device.py --adb .build/android-platform-tools/platform-tools/adb.exe --serial SERIAL --package org.xleaves.mdo.qa.release --apk .build/android-device-20261029/mdo-qa-full-fixed.apk --evidence .build/android-upgrade-rerun
```
