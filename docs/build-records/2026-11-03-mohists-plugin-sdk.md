# 2026-11-03 Mohists 本地验收所需 SDK 修复

插件目录接口返回稳定逻辑 ID（如 `plugin:mohists`）和物理目录 ID。
旧前端把逻辑 ID 拼成 `plugin.plugin:mohists`，被 Application SDK 的身份验证拒绝，
无法注册协作面板或调用插件服务。`aa319c87` 改用物理目录 ID，
与原生编译器的 `plugin.<directory-id>` 所有者保持一致，没有放宽 SDK 身份校验。

定向回归 11 项通过，包括插件启动、面板、菜单、渲染器、快捷键、失败回滚，
以及逻辑 ID 与目录名不相同的作用域检查。

本地 Windows 构建：`.build/mohists-acceptance-20261103/mdo.exe`。
重用现有锁定宿主和 SDK，未增加仓库副本或运行时。版本仍为 0.8.5，
SHA-256 为 `9a787783d4786d39593a59769e6ed6295a275d04d0fee419a19041a6f4b5dffd`。
仅按用户要求替换 `E:\software\mdo\mdo.exe` 供墨者测试，原程序保留备份。
该实例重启后角色、协作窗口及默认设置实际加载，诊断为 0，原账号仍登录、原会话文件不变。

本轮不发布线上程序或插件；用户确认模型体验后再处理正式上架与客户端发布。
完整事实及使用说明见 Mohists 仓库的 `docs/acceptance-2026-11-03.md`。
