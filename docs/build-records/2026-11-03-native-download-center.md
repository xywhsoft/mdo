# 原生下载中心交付记录

2026-11-03，在 `D:\GIT\mdo` 单线程开发、原目录测试。复用已有 SDK 和宿主，没有复制仓库、SDK 或用户运行环境，没有修改 `E:\software\mdo` 的账号、会话或安装内容。

## 功能

- 原生持久化任务，应用更新与扩展商店归档统一显示；暂停、继续、有限自动重试、显式重试、取消并删除、清理记录。
- 更新包使用 Range/强 ETag 续传；扩展使用既有 offset 分块协议。重启后显示暂停，用户继续时恢复固定版本与校验身份。
- 大小及完整 SHA-256 校验；扩展继续执行 ZIP/清单检查。恢复下载不自动执行中断的安装事务。
- 真实进度、速度、预计剩余时间、重试/校验状态；中、英、俄错误说明与可展开原始诊断。
- 目的文件所有权保护，旧失败任务不能删除后来的下载。无法清理受管临时文件时保留记录，允许再次清理。
- 损坏或超过 128 条的登记文件保留原件、管理器只读，其他应用功能仍可启动；界面提示读取失败、保留展示并自动重试。

实现与边界见 [下载中心说明](../download-center.md)。当前是原生单连接方案，接入现有应用包和商店扩展；未引入 aria2，未增加任意外部 URL 或模型权重下载入口，未调整既有资源大小上限。本地模型部署扩展仍是后续功能。

## 验证

- `python -X utf8 tests/test_native_downloads_runtime.py --host .build/host/xs.exe` 通过。回环模拟服务覆盖暂停/继续、断线后的 Range、服务器忽略 Range 时安全重下、强制退出恢复、校验失败、取消清理、清理旧失败任务保护新下载、扩展归档重启恢复与封存、更新策略及断点恢复。恢复扩展没有安装到目录，恢复更新没有执行安装。
- 同一原生测试覆盖无效 JSON、129 条登记记录：下载 API 返回 503，bootstrap 可用，损坏文件没有被覆盖。
- `node --test tests/test_downloads.mjs tests/test_download_center.mjs tests/test_frontend_i18n.mjs`：15 项通过，包含快照读取失败时保留记录、禁用旧操作并在刷新成功后恢复。
- `test_application_sdk_contract.py`：11 项通过；`test_frontend_contract.py`：19 项通过。
- 浏览器使用独立测试 Home：暂停进度在刷新后保留，点击继续后到达下载就绪，错误详情可展开。1280×720 桌面列表独立滚动，390×844 窄屏全屏显示且无横向溢出，底部操作可见。截图保存在 `.build/acceptance/native-downloads/desktop.jpg` 和 `mobile.jpg`。验收后恢复浏览器尺寸、关闭测试标签页和服务。
- `python -X utf8 tests/test_packed_native_downloads.py --packed mdo.exe` 通过：打包程序读取并恢复任务，取消/清理写回原生登记文件，实际提供的下载 JS 与三个语言包逐字节匹配源码。测试使用临时 Home 和同一可执行文件的硬链接，没有复制程序或应用源码。

## 构建

命令：

```powershell
python -X utf8 tools/build_mdo.py --skip-host-build --xserver-root .build/sdk-overlays/3173f94f8307f94899f04ceaccd88695f128f14bb68b333dbc3a8e1b8dae1b5e
```

- 产物：根目录 `mdo.exe`，Windows GUI，版本 `0.8.6`，版本代码 `05000806`。
- 大小：6,702,443 字节，约 6.39 MiB。
- SHA-256：`b95c49d5c2b6733e1613c484b51be2769638bd83edf23f8ec4c486064177e26b`。
- 构建源码提交：`4ee34140cbc8ca4ec3398422695f5e73455b0576`；宿主/SDK 提交：`216f5445163ca30c815ad7d5d2da3b71cb2f6401`。
- 阶段提交：`d4869f62` 原生任务与续传，`807efc6f` 更新/扩展/界面接入，`4ee34140` 所有权、恢复和错误处理加固。

仅完成本地构建，没有发布安装包或线上部署。本任务未修改或提交原有工作区中 `app/default-home/config/defaults.json`、`app/include/mdo/config.h` 的其他改动。
