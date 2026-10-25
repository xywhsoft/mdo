# 代码检查与 Android 真机验收（2026-10-25）

本轮按用户要求先完成代码与已连接 Android 真机测试。客户端版本保持 `0.6.0` / `05000600`，没有发布正式更新，没有覆盖手机日常使用的 mdo，也没有修改线上服务。

## 修正检查门禁

- `199b1f5a`：构建契约补入知识库源码，并使独立 unittest 能找到工具目录中的 `application_sdk`。
- `2275f505`：模型 SDK 导出测试的模拟回执带上实际要求的 SDK overlay 指纹。
- `c20ea2ca`：前端扩展分类测试从九类同步到包括知识库的十类。
- 平台 `1b3c3118`：后台菜单断言覆盖十类独立扩展入口、创作者管理及 `_component` 导航；隐藏的旧组合管理入口不再要求出现在菜单中。
- `23f96535`：拆开 15 处容易误读的同一行条件语句，使严格 C 编译通过；语句顺序和条件范围未变。

## 测试发现并修复的实际问题

1. `29ce7992`：Android 默认构建路径生成新打包宿主后，没有在校验前记录 SDK overlay，导致新宿主被当成过期缓存拒绝。现在与桌面构建保持一致，先验证二进制哈希再记录指纹，旧缓存仍必须重建。有真实回执格式的定向回归。
2. `83d5d9f2`：完整版 APK 真机启动失败，屏幕显示 `Invalid native runtime name`。打包器保留的 `libmdo_ext_c++_shared.so` 被 Java 的文件名白名单拒绝；FFmpeg、ImageMagick、Poppler 三组工具引用了它。白名单现允许合法的 `+`，保持目录穿越和链接越界保护。打包时也执行对应校验，防止无效文件名等到真机启动才暴露。Python 打包回归和实际 Java 路径回归都覆盖 C++ 库名。
3. `2d1341b6`：SDK 直接 HTTP 调用 `tool.call` 时，无截止时间以 `0` 表示，却覆盖了原生 Agent 的无限截止时间，导致操作被立即当作超时/取消。Android 和 Windows 均先复现 409；SDK 工具与压缩入口现在将未设置的截止时间转换成原生 `MDO_DEADLINE_NEVER`。桌面完整 SDK 回归新增直接 HTTP 用例，有限截止时间的命令调用及取消逻辑继续保留。

失败日志和手机启动失败截图保留在 `.build/knowledge-android-20261025`，没有将失败收据改成成功。最初两次知识库脚本失败属于测试对 CAS 快照参数及导出选择结构的假设错误，修正脚本后重新执行；与上面的完整版和 SDK 产品缺陷分别留有证据。

## 代码验证结果

- Windows Python unittest：182 项，177 项通过、5 项平台条件跳过。四个 Linux ELF/musl 工具链用例及一个 GTK 用例已在 WSL 补测覆盖；Linux 构建契约 11 项中 10 项通过、1 项 Windows 路径转换跳过，真实 GTK/X11 回归通过。
- Node：1,187 项全部通过、零跳过。设置 `MDO_ECOSYSTEM_SITE_ROOT`，同时验证网站与客户端共享渲染器。
- 前端模块图：232 个模块可解析且可达。
- 产品 C unity：`-std=c11 -Wall -Wextra -Werror -fsyntax-only` 通过。
- 实际 XS/TCC 扩展包校验：44 项通过，包含知识库、原有九类组合及非法路径/版本。
- 平台隔离发布全链路通过，包含新菜单、迁移、推送、过滤、CSRF、大于 32 MiB 文件传输、更新、撤回及冲突保护。
- Windows 完整 Application SDK 回归通过，包括新增的直接 HTTP 工具调用；新编译 Windows GUI 成品的最长相对路径及长 Home 知识库回归通过。

## 真机验证结果

设备为 RMX5090，Android 16，`arm64-v8a`。独立包名 `org.xleaves.mdo.qa.knowledge`，显示名 `Mdo Knowledge QA`，开发签名、可调试，独立私有 Home。日常应用 `org.xleaves.mdo` 的版本保持 `0.1.1` / `30000090`，未安装覆盖、未读取其私有账号或会话。

重新编译了 ARM64 `libxs.so`，SDK overlay 为 `bbf47c296cd2f3f2b8d5cfd6ba4e8978cb2ce2fd2e6f1f63c14802134c806acc`，没有借用缺少新原子替换接口的旧原生库。

- 轻量包通过知识库创建、九层索引、最长 220 字节 Home 相对路径、修改、陈旧修订冲突、路径越界拒绝、坏库警告及修复清除、启停、兼容下限、导出、递归删除、重新导入、强制停止及冷启动保留。
- 同包名从轻量包覆盖为完整版，再覆盖修复包，知识库正文和私有 Home 哨兵均保留。没有清数据或卸载绕过升级问题。
- 修复后完整版重跑上述知识库全部用例；13 组外部工具的真实可执行程序均在 APK UID 下成功运行，其中包括 C++ 依赖的 Poppler、FFmpeg、ImageMagick。
- 真实 ARM64 对话测试通过重试、连接中断、流中断、额度错误、退避取消、恢复续接、冷启动后凭据及草稿保留。模型由本机合成 SSE 服务提供，没有付费或真实供应商调用。
- 最终完整版通过完整 Application SDK 真机回归：JNI/TCC 目录、类型契约、原生导出、流、Unicode 文件、空参数进程调用、C 模块及命令、工具上下文、合成模型调用、异步取消和项目租约、VFS 发布/回滚、系统凭据存取、实际 WebView 插件激活与 UI ACK。
- 测试进程已停止，测试创建的 ADB forward/reverse 已移除；独立测试包和私有证据保留。

新增回归及可配置 QA Home 的测试提交为 `48a0b621`。

## 成品与范围

- Windows QA：`.build/knowledge-android-20261025/windows/mdo.exe`，6,312,854 字节，SHA-256 `25ce117d9475d7a1e4695d1baec4edeb60390522e2cd0e53aa15bcb553f4c4c8`。
- 最终真机完整版 QA：`.build/knowledge-android-20261025/mdo-qa-full-final.apk`，149,735,000 字节，SHA-256 `c766c5b56205dba7adac6203e5a8a6dc4ec68e04b9b5f3357e74d40f9cc23359`。
- 构建用轻量版：`.build/knowledge-android-20261025/mdo-lite-final.apk`。它使用日常包名和开发签名，仅供构建验收，本轮没有将此文件装到日常应用上。最初实际安装的轻量 QA 使用独立包名。
- 两个最终 APK 的 app pack SHA-256 均为 `c4ba717c2349d3d30e3595ba0f82f73686c7321aebd2a7e287ef1a8e3fba1449`，原生库摘要相同；均通过 v2/v3 签名和 16 KiB ZIP 对齐检查。

本轮完成代码与 Android 真机验证，未执行完整跨平台正式发布流程。Linux 老发行版 glibc ABI 基线仍需后续验收；已通过的 Linux 小型工具链/GTK 测试不能替代该项。其他任务的 `app/xs.json`、`tests/test_remote_asymmetric_pause.py` 和平台工作区改动均保留。
