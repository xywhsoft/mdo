# 知识库 v1 兼容声明与长绝对路径修复（2026-10-25）

按用户确认，只统一知识库自身格式与兼容声明。知识库继续使用 `mdo.knowledge.v1`，没有把所有扩展容器降为 v1；`mdo.extension.v1/v2` 的小包、大包能力保持不变。

## 兼容声明与线上同步

- 客户端提交 `c1ca6a5c`，平台提交 `6f3800addcdd41a25c483ef1755980472bcbe62a`。
- C 校验器和前端构建器共用知识库下限 `60000009`。小包、跨 1 MiB 或 6 MiB 阈值的大包、浏览器目录创作、客户端本地导出统一携带该下限；作品明确要求更高构建时保留原要求。
- 客户端在 v1 和 v2 容器中都检查最低构建，不再只对 v2 生效。缺失下限、旧的 `30000126`/`60000001`/`60000002`/`60000008` 与高于本机的下限都被拒绝。
- 线上原有知识库作品数为零，无需迁移历史作品。没有修改账号、会话、计费或其他作品数据。
- 只部署平台的 `ecosystem_package.h`、`knowledge_format.h`、创作中心 `knowledge-format.js`、`knowledge-builder.js` 四个文件。先比对线上旧哈希、保存私有备份，再以生产原运行时在隔离实例验证，短暂重启 `xs.service` 加载。
- 部署后四文件哈希匹配，程序 `/opt/www/xs` 与配置 `/opt/www/xs.json` 哈希保持原值。公网 HTTPS 可读到 `60000009`，旧构建 `60000002` 的知识库目录为空。
- 私有证据及备份：本机 `.build/knowledge-compat-20261025`、服务器 `/home/xs/mdo-knowledge-compat-20261025`。

## 长绝对路径修复

提交 `8cc2a605`。旧 `MdoHomePublishTempLocked` 在 rooted 写入后，重新拼接 Home 绝对路径调用 `xrtPathRename`，导致 Windows 原子发布仍受到绝对路径长度影响。

新接口 `xsRootReplaceFile` 通过同一 Home 内的父目录句柄进行原子替换：Windows 使用 NT 相对句柄重命名，POSIX 使用 `renameat`；不再退回绝对路径操作。保持父目录不跟随链接、源和目标必须为常规文件、备份和原有 no-replace 行为。已有内容被提交后，不将句柄关闭失败误报成未提交。

原生接口通过仓库现有 SDK overlay 提供，锁定 xs 基线 `216f5445163ca30c815ad7d5d2da3b71cb2f6401`，没有覆盖其他任务的 xrt 或 xs 工作区。

SDK overlay SHA-256：`bbf47c296cd2f3f2b8d5cfd6ba4e8978cb2ce2fd2e6f1f63c14802134c806acc`。

Windows、Linux、Android 构建回执必须匹配 overlay 哈希，防止使用旧宿主打包需要新接口的 C 源码。新安装编号为 `60000017`～`60000024`；展示版本仍为 `0.6.0`，版本码仍为 `05000600`，知识库能力下限仍为 `60000009`。

## 验证

- Windows 原生源码和 GUI 成品、Linux 原生源码和服务端成品通过长 Home 完整知识库测试。实际文档绝对路径大于 350 字符，覆盖九层索引、100 层目录、最长 220 字节 Home 相对路径、盘点、预览、CAS 修改、快照、导出、删除、重新导入和卸载。
- Windows 目录联接及 Linux 符号链接保持拒绝遍历；删除不伤及外部哨兵文件。
- Windows/Linux Home 探针验证第一次原子写、覆盖写、`.bak` 内容、拒绝目录替换、no-replace、保留路径及多进程 Home 互斥。Linux 探针使用 Linux 原生临时目录，避免把 DrvFS 不支持的 no-replace 操作当作 Linux 原生文件系统能力。
- 10 项知识库 Node 测试、2 项模块图检查通过。原生 overlay 回执测试、3 项宿主构建契约测试通过。
- 知识库双宿主发布、审核、下载、更新、下架、保留本地安装链路通过。生产原运行时隔离校验也通过 Skill、Agent 工具、编辑器命令、MCP、知识库、外部工具及描述迁移。
- 新原生宿主另通过 MCP 与 Skill/Agent 工具/编辑器命令的客户端及平台完整链路。测试提交 `19e890cc` 使后两类商店探针接受 `--host`，避免发布门禁误用缓存旧宿主；桌面探针参数分发检查通过。

## 成品与边界

Windows QA：`.build/longpath-fix-20261025/mdo.exe`，6,312,808 字节，SHA-256 `0cce81c5c323db7865665e76d33f3ee0b289ca7aea2467e1673310b5a3512dcd`。

Linux QA：`.build/longpath-fix-20261025/mdo-server-linux`，5,815,642 字节，SHA-256 `75c3695a5877b9c6621615db77f625c70bdb8cf7095fc8c78e4fc4447e4ed613`。此次 Linux 构建使用本机 glibc，仅作专项回归，未验证旧发行版 ABI 基线。

本轮已上线平台兼容校验，没有发布正式客户端更新。Android 尚未重编 APK 或进行真机回归；专项回归通过不代表完整发布门禁全部通过。其他任务的 `app/xs.json`、`tests/test_remote_asymmetric_pause.py` 及平台工作区修改保留。

此前 `2026-10-25-knowledge-fixes.md` 中记录的知识库能力下限不一致和 Windows 长 Home 原子发布限制，均由本轮取代。
