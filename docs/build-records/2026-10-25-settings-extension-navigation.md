# 2026-10-25 扩展能力导航顺序与名称

提交 `7b622f9d`，版本保持 0.6.0。

导航顺序为 Agent、SubAgent、Agent 工具、MCP、Skill、编辑器命令、外部工具、插件、扩展包。三个重命名同步中文、英文、俄文导航和页面标题；手机选择器从同一导航生成。沿用原 commands、extensions、store 路由及存储。

20 项前端契约检查通过；隔离浏览器验证桌面顺序、三个新标题及 390px 手机选择器顺序。截图位于 `.build/settings-extension-navigation-20261025/screenshots/`，临时服务与浏览器已关闭。

冻结源码重新构建 Windows 桌面/服务版、正式签名 Android 精简/完整版，复用已验证的原生运行时；APK 结构、原生库、zipalign 和证书检查通过，未重复原生 SDK 行为测试。产物位于 `.build/settings-extension-navigation-20261025/products/`，大小与 SHA-256 见同名 JSON。

未发布官网或自动更新，Android 未做真机回归，其他任务未提交文件保持原样。
