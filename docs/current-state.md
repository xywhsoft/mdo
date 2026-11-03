# mdo 当前实现与验收索引

更新：2026-11-03。本文是当前入口；按日期追加的历史计划保留背景，不直接作为当前缺陷清单。源码版本为 0.8.8 / 05000808，各平台安装序号见 [release.json](../app/release.json)。本次修改与本地候选不等于线上发布。

## 本次审计修复

| 范围 | 当前行为 | 依据与有界验证 |
| --- | --- | --- |
| 下载状态 | 更新按 build 收据确认安装；归档快照清除缺失行；清除后不复活 | [下载中心](download-center.md)，`test_downloads.mjs`、`test_native_downloads_runtime.py` |
| 下载持久化 | 坏/重复记录隔离并保留原索引；清理失败显示并可重试；未保存的登记/继续回滚 | 原生运行时故障注入，取消、重启及文件归属测试 |
| 下载历史 | 正常完成文件和已消费包历史保留 30 天，满 128 条时只清理可淘汰记录 | 128 条普通历史容量检查，暂停和待安装记录保留 |
| 网络与资源 | 全局代理/CA，认证 HTTP/SOCKS5、绕过；受信任 HTTPS 来源、域名跳转白名单、64 位大小与磁盘预检 | `test_download_network_runtime.py` 的小文件 TLS/代理/跳转/旧上限边界测试 |
| 模型验证 | 当前参数与运行时工具描述纳入指纹；模拟工具结果后再次回复；旧合同重测 | [模型目录](model-catalog.md)，三个协议的真实回环 HTTP 验证 |
| 首 token 排障 | 每次模型调用的首思考/正文、重试等待、末次 HTTP 统计和 Request ID | [耗时说明](model-request-timing.md)，journal/API 与前端多调用测试 |
| 发布门禁 | 增加 managed aria2、单文件原生下载、可显式指定的平台 Range 验收 | [发布门禁](release-gate.md)，遗漏的平台/产物明确标记未测 |
| 前端接线 | 更新、下载、通知的创建与销毁统一放入 `features/shell/delivery-centers.js` | 全量 ES 模块与前端交互检查；继续按功能逐步拆分 app.js |

## 已有能力的正确入口

- 原生消息队列调度已经存在：`app/src/api/queue_dispatch.inc.c`；worker 通过正式运行入口处理队列，不依赖网页常驻。
- 备份恢复已经存在：`app/src/api/backup_restore.c` 与 `features/sessions/session-backup-import.js`，提供 review/apply 与后台恢复流程。旧准备文档中的“恢复 worker 待实现”只描述当时的阶段。
- 设置中的模型验证只覆盖保存的配置和通用运行时工具；自定义 Agent、图片与其他思考档位仍需要对应场景验收。

## 下载配套能力矩阵

| 组件/目标 | 源码能力 | 本轮验收 | 发布状态 |
| --- | --- | --- | --- |
| Windows x64 客户端 | 内置 + 受管理 aria2；全局网络策略；持久任务 | 回环、故障恢复与单文件候选检查 | 本地候选，本轮未发布 |
| Linux 客户端 | 复用 HTTP/进程抽象；已有磁盘空间检查 | 本轮未在 Linux 运行 | 不推断已发布 |
| macOS 客户端 | 保留跨平台接口；仍需产品适配与磁盘空间实现 | 未运行 | 尚不宣称支持 |
| Windows aria2 工具 | 命名导出，按任务创建进程并保持目录租约 | 真实进程/RPC、暂停、重启、崩溃回退、认证 HTTP 代理 | 使用现有本机工具产物，无新发布 |
| Linux/macOS aria2 工具 | 接口保留；自定义 CA/SOCKS5 选内置引擎 | 未运行目标产物 | 需目标平台验收 |
| 服务端归档 Range | `D:/GIT/home` 的 `dc2e3294`，兼容整包/chunked/旧 offset | 实际发送器 22 项有界编译检查 | 本轮未部署线上 |

aria2 1.37 的 WinTLS/AppleTLS 不支持自定义 CA 文件，选中该设置时自动使用 mdo 内置 TLS，不修改系统信任库。参见 [上游手册](https://aria2.github.io/manual/en/html/aria2c.html#cmdoption-ca-certificate)。

## 尚未被此次验收证明的事情

没有运行真实多 GB 模型权重下载，没有宣称 HF/镜像吞吐或所有 CDN 链路都已验证。本地模型商店、运行库与模型进程管理仍是后续独立功能；本次提供其下载基础接口。没有凭本地测试判断线上网关已经修复，新增的 Request ID 与分段耗时用于下一次真实慢请求定位。发布仍需完整发布门禁和目标平台/线上验收。
