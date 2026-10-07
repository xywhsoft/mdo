# 扩展中心部署记录

日期：2026-10-07。服务：`https://ai.xywhsoft.com`。

## 使用入口

- 墨斗：设置 → 扩展能力 → 扩展中心。
- 网站：<https://ai.xywhsoft.com/mdo/ecosystem>。
- 后台：墨斗管理 → 生态资源，路由 `/admin/mdo/ecosystem`；使用站点现有管理员登录入口。

商店必须登录会员账号。用户选取本地 Agent、SubAgent、工具、Skill、MCP 和命令，
填写说明后直接提交；网站也可以上传 `.mdo-extension.json`。管理员查看说明和源码，
通过后进入公开列表。退回原因在“我的发布”显示。用户可以安装、检查更新、卸载，
仍通过原来的本地资源管理页编辑。

首个公开插件是“项目审查入门包”，ID `1`、版本 `1.0.0`，作者为测试会员的公开昵称
`xLeaves`。它包含只读 SubAgent、Skill 和命令；源码位于
[`project-review.mdo-extension.json`](../examples/extensions/project-review.mdo-extension.json)。
源码包 SHA-256 为 `06b836c024aa21f2a3ccb59fbdc224c4005bdd243ada80bc4792d6843f9a0f1e`。

## 部署和源码

网站实现仅扩展统一的 `mdo` 插件，版本 `2.1.0`，没有另设生态插件。
本地目录 `D:\GIT\home\host\xywhsoft_ai\plugin\mdo` 与服务器
`/opt/www/host/xywhsoft_ai/plugin/mdo` 的 71 个文件按 SHA-256 核对一致。
部署时的本地 home 和服务器 `/opt/www` Git 提交均为
`5732919ab4ef0e6dd6224249516ee2638defcd8c`。

先用服务器实际 xs 在隔离站点执行联调，随后停止 `xs.service`、备份代码和主库，
原子替换插件文件并启动。最终备份在：

```text
/opt/www/host/xywhsoft_ai/plugin_data/deploy-backups/ecosystem-20261007T080958Z
```

既有身份配置、私钥、会员、余额、搜索/模型密钥和工具包目录保留。
临时维护管理员已删除，其他管理员的账号、密码摘要和权限与上线前备份一致；
服务器暂存测试站点、发布文件副本和明文维护凭据已清理。服务状态为 `active`。
生态数据在 `plugin_data/mdo/ecosystem/`，备份网站时需一并包含它。

## 客户端发布

根目录程序已替换，同时通过原有发布 API 上传到线上更新服务。

| 文件 | 内部版本号 | 字节数 | SHA-256 |
| --- | --- | --- | --- |
| `mdo.exe` | 30000016 | 4341187 | `f289adcdbab6ae89ca5b7de905fda0a66cf8fe8daaca9c6aec46a9e3b48644a6` |
| `mdo-arm64-v8a.apk` | 30000017 | 3966189 | `5b4a82da73855147d4c643ca42f6a18e0e4aa7f34ffb6e4f231490960ead7846` |
| `mdo-arm64-v8a-full.apk` | 30000018 | 60728142 | `b237ffdbaec8e27c3c9510d847dde015f890575afcdeffc336a8263792cc1f29` |

## 验证结果

- 真实 HTTPS：会员登录、Cookie CSRF 投稿、管理员审核、作者显示、搜索/分类、详情和最新版本。
- 独立打包版：真实网站登录、插件安装、三个本地资源管理器重载和卸载。
- 隔离原生联调：未审核隔离、版本冲突、路径/大小/深度限制、C 代码信任、编译失败回退、
  Skill 附件、更新时本地修改冲突、卸载保留修改、未完成安装日志的启动恢复。
- 服务器实际 Linux xs：网站投稿和审核联调通过；重启后匿名目录返回 401，商店页面返回 200。
- 前端模块解析、格式测试和设置页面契约通过；检查了桌面和 390 × 844 手机布局。
- 原有搜索额度和模型目录接口正常，三个线上更新接口返回对应新版本和文件摘要。
- 两个 APK 的 v2/v3 签名和 16 KiB 对齐通过。本轮未安装到手机进行实机测试。

未执行压力或高负载测试。详细格式、API 和安装事务见
[`ecosystem-store.md`](ecosystem-store.md)。
