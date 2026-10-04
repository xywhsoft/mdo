# 构建与维护工具

这些脚本在开发机上运行，不会打包到 mdo。构建需要 Python、匹配 `deps.lock`
的 xs 源码和本机 C 工具链；前端发布检查另外需要 Node.js。普通构建不依赖 Node.js。

| 脚本 | 用途 |
|------|------|
| `build_mdo.py` | 验证依赖、检查 C 源码引用、生成 unity、构建宿主与打包 |
| `build_android.py` | 构建 ARM64 Android 应用，复用相同应用源码与默认 Home |
| `qa_release.py` | Windows/Linux 有界发布门禁，不运行压力或高负载测试 |
| `check_web_modules.mjs` | 检查前端语法、导入目标和页面入口的模块可达性 |
| `inspect_session.py` | 离线读取会话事件与工具统计，不改写会话 |
| `export_icons.py` | 从既有品牌图导出图标；普通构建不需要再次运行 |

运行示例及工具链要求见根目录 README 和 `docs/android.md`。
仅用于模块 ABI 验证的 Echo 实现位于 `tests/fixtures/modules/echo.c`，由测试明确
注入临时 Home，不进入正式工具目录。

本机可能还留有未跟踪的 `git/`、`python313/`、`curl.exe` 等旧开发工具。
当前程序不再自动把这些目录加入 PATH，也不将它们作为发布依赖；Agent 的进程
工具使用当前平台提供的运行环境。这里不存放模型凭据或用户会话数据。
