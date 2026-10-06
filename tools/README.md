# 构建与维护工具

这些脚本在开发机上运行，不会打包到 mdo。构建需要 Python、匹配 `deps.lock`
的 xs 源码和本机 C 工具链；前端发布检查另外需要 Node.js。普通构建不依赖 Node.js。

| 脚本 | 用途 |
|------|------|
| `build_mdo.py` | 验证依赖、检查 C 源码引用、生成 unity、构建宿主与打包 |
| `build_android.py` | 构建 ARM64 Android 应用，复用相同应用源码与默认 Home |
| `build_toolpacks.py` | 构建 Windows 常用工具包与可选 Python 包 |
| `prepare_release.py` | 校验三个客户端产物并生成本地发布清单 |
| `host-profile.json` | mdo 专用 xrt 根与 Windows SDK 选择；默认启用精简宿主 |
| `qa_release.py` | Windows/Linux 有界发布门禁，不运行压力或高负载测试 |
| `check_web_modules.mjs` | 检查前端语法、导入目标和页面入口的模块可达性 |
| `inspect_session.py` | 离线读取会话事件与工具统计，不改写会话 |
| `export_icons.py` | 从既有品牌图导出图标；普通构建不需要再次运行 |

运行示例及工具链要求见根目录 README 和 `docs/android.md`。
Windows/Android 构建默认使用精简配置；完整扩展 SDK 可加 `--full-host`。
`--skip-host-build` / `--skip-native-build` 会验证源码版本、profile、图标和二进制
指纹，不能混用旧宿主。新增 xrt 调用若未进入闭包，打包前会明确报错。
配置范围、体积结果和验证见 `docs/compact-host.md`。
仅用于模块 ABI 验证的 Echo 实现位于 `tests/fixtures/modules/echo.c`，由测试明确
注入临时 Home，不进入正式工具目录。

可选外部工具统一放在 [`runtime/`](runtime/README.md)，按 Windows x64 和
Android arm64-v8a 分类。原有 `git/`、`python313/`、`curl.exe` 已分别归入
`runtime/windows-x86_64/` 的 `git/`、`python/`、`curl/`，另补充 BusyBox、jq、
OpenSSH 客户端及对应 Android 工具。Python、Git 保持可选。

Windows 常用工具及 Python 支持通过“设置 → 常规 → 扩展工具包”安装；Android
使用 `--edition lite` / `--edition full` 构建精简版与带工具完整版。Git 仍是可选
发布素材。安装不会修改系统 PATH，模型提示词使用已探测工具的绝对路径。
通知、打包与网站发布流程见 `docs/distribution.md`；网站插件只维护在 home 仓库
对应 vhost。来源哈希、构建方法与此前真机 Shell 验证见 runtime 文档。这里不存放
模型凭据或用户会话数据。
