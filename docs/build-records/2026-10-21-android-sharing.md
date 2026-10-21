# Android 系统分享接收（2026-10-21）

基线：mdo `d34143e1`，xs SDK `98c7651fafe52ce81854fc3abebedc938ab13311`。
本次只提交系统分享相关修改，保留工作区中其他任务的改动；SDK 锁定版本不变。

## 实现

- Android SEND / SEND_MULTIPLE、EXTRA_STREAM 与 ClipData，文字和链接分享；
  冷启动及 onNewIntent 路径接入现有单实例 Activity。
- 在 URI 临时授权有效时，将有界内容复制到独立私有收件箱；支持稍后处理、
  丢弃、进程中断后识别未完成分享，不重放历史启动 Intent。
- 本机项目、新会话或进行中会话选择；文件落到会话实际工作区，路径引用和文字
  加入草稿。模型支持时接入现有图片上传，沿用单图 8 MiB / 最多 4 张限制。
- 固定分享、文件、图片和新会话 ID；绑定目标会话；哈希校验、不跟随目录链接、
  排他临时文件与禁止覆盖发布；保留用户已改动的文件。
- 保存精确的草稿前后计划，草稿确认落盘后再清理收件箱；丢失确认后重试不直接
  重复追加。并发草稿修改要求显式确认。远程设备切换阻止导入，内容保留。
- 分享不创建运行请求，不自动调用模型。不引入第三方依赖。

## 验证

工作目录：`.build/android-share-20261021/`。

| 验证 | 结果 |
| --- | --- |
| `tests/test_android_shares.mjs` | 11/11；保存顺序、丢失确认、草稿冲突、稳定创建、图片完整性与格式降级、容量及损坏记录 |
| `tests/test_shared_files_runtime.py` | 真实 xs/TCC/HTTP 通过；中文、空文件、并发发布、篡改保护、越界、哈希、绑定、目标目录 junction 拒绝 |
| `tests/test_android_share_files.py`（WSL JVM） | 有界流、SHA、文件名、原子写入、取消、真实软链接拒绝；实际 pinned Activity 编译与 overlay 漂移保护通过 |
| 桌面与 390×844 浏览器 | 真实 DOM/样式通过；重试后不重复追加或导入 |
| 完整 mdo 应用浏览器联调 | 真实 HTTP 草稿持久化、确认丢失后重试、第二次分享新建会话与跳转通过；无 `/runs` 请求 |
| 已有动态权限回归 | 通过；新增路由未破坏现有权限执行路径 |
| Android ARM64 | 原生库编译、全部 Java、D8、资源、v2/v3 签名、zipalign 通过 |
| APK 离线检查 | CRC、分享 Receiver/DEX、包内 pack/原生库逐字节比对、75 项原生库架构及依赖闭包、16 KiB ELF 对齐通过 |

Windows 不具备创建源文件软链接的权限，因此该单项明确跳过；目标目录 junction
用例已在 Windows 实际执行，源软链接检查另在 WSL 真实 JVM 执行。

另对仅暂存本功能的源码导出做了隔离验证。仓库基线已有的未注册
`src/transfers/capture.c`、`src/transfers/commit.c` 使该导出的严格 source graph
检查失败；本次没有改动这些无关源码。按该导出的 `sources.json` 生成 unity 后，
真实 native 导入回归与 11 项前端回归通过。包含其他在研修改的当前工作树则已通过
正常 `--prepare-only`，且用于本次独立测试 APK。因此本记录不代表完整公开发布门禁。

## 测试包与剩余验收

- 文件：`.build/android-share-20261021/mdo-share-qa.apk`
- 独立包名 `org.xleaves.mdo.share.qa`，显示名 `Mdo Share QA`；开发签名、可调试完整版。
- 版本 0.2.0，versionCode 30000110，61,866,830 字节。
- SHA-256：`263eac176e646992f67f4e5de4dc8b4a40a201f1fa53439db2f441b2bf4d33bb`。
- 与现有 mdo 分开安装，不覆盖其数据；未发布官网下载或自动更新。

当前 ADB 未发现设备，尚未完成真实 Android 来源应用、冷启动、后台唤起、多文件、
授权撤销及重启后待处理收件箱的验收。浏览器模拟桥验证不能替代这些真机项目。
相关日志、清单和机器可读 APK 收据保存在上述私有工作目录中。


## 0.2.1 编译与自动回归收尾

用户选择本轮仅完成编译和自动回归，暂不进行真机验收。版本更新提交为 `f040cab8`，
本轮使用最新工作树重新生成应用 pack，复用已通过锁定依赖收据核验的 Android 原生库。
11 项前端用例、WSL 真实 JVM 文件用例、实际 Activity 编译与漂移保护、真实 xs/TCC
文件导入、完整应用的草稿持久化和第二次分享新建会话联调全部通过；联调没有发起模型调用。

新独立测试包：`.build/android-share-0.2.1-20261021/mdo-share-qa-0.2.1.apk`。
版本 0.2.1，versionCode 30000111，仍为 `org.xleaves.mdo.share.qa` / `Mdo Share QA`，
开发签名完整版，61,866,830 字节。v2/v3 签名、zipalign、CRC、分享 Receiver/DEX、
包内 pack/原生库一致性及 75 项原生库依赖和 16 KiB ELF 对齐均通过。

SHA-256：`55c1bc7dc2c9e2c40d38f9b9750b3e9c7d1b60ad209e9363c28774fc3a4996ce`。
机器可读收据与日志保存在该测试包同目录。沿用前述独立测试包的签名，可覆盖安装该测试包，
与正式 mdo 的包名和数据分开。官网与自动更新仍保持原发布状态；Android 真机分享场景
继续标为本轮未覆盖。
