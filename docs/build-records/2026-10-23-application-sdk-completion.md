# Application SDK 收尾与跨平台回归

产品版本：mdo 0.2.4，SDK v1。本轮没有发布官网或自动更新。

## 已完成的修复

| 提交 | 问题与结果 |
| --- | --- |
| `746e9335` | 空/非法流句柄不再命中未分配槽位导致除零崩溃；参数错误返回 400，有效但失效的句柄返回 410。 |
| `6a5c809e` | 原生/自定义异步操作持有项目生命周期租约，取消、结束及入场失败释放；产品路由保留各自的生命周期协调，避免与独占清除互锁。 |
| `8ca671da` | 插件启停、重装同一 revision 不再覆盖 previous，保留真正的上一版供回滚。 |
| `e10eb757` | 异步操作身份使用递归规范化对象键；JSON 键序变化可去重，数组顺序和数值精度保留。 |
| `ab66e355` | 插件面板、菜单、动作、快捷键和渲染器回调传入所属插件 SDK；失败注册及失败激活清理贡献，重复清理不误删新贡献。 |
| `1f6cf6ca` | 客户端意图记录损坏、不可读时停止执行，不再当成空记录重放副作用。 |
| `20420c4e` | 真机发现的 Skill 祖先搜索权限缺陷：遇到不可访问的父目录停止可选 Git 发现，仍严格验证工作区。原实现会在 Android `/data/user/0` 边界导致会话创建 503。 |
| `07b7a965` | 真机日志发现的插件启动竞态：等待启动恢复门禁放行，再注册客户端和发现/激活插件。目录读取可恢复，激活副作用不自动重放。 |
| `0a0eaa83` | musl 真实 SDK/子 Agent 调用崩溃：默认线程栈 128 KiB，小于工具批次约 280 KiB 的单层栈帧。链接指定 8 MiB，实际 ELF 校验覆盖缓存宿主。原崩溃点、调整后的完整回归均已验证。 |

`d8597452` 补齐 251 个产品入口（含别名）的请求和资源契约，明确 path/query/body、修订前置条件、原始文件上传和引用 DTO。目录保留摘要，详细契约通过 `sdk.describe` 获取，避免一次目录响应超过大小预算。新增未分类 handler 会阻止生成；生产 handler 仍负责完整的业务与状态校验。`fe474f8e` 修正备份预览为无请求体操作。

`6311b477` 增加独立 Android 验收脚本；`9b69f458` 补充具名项目的异步租约、取消、物理工作区缺失及失败入场清理回归。

## 验证结果

| 项目 | 结果与范围 |
| --- | --- |
| Python 单元回归 | 164 项：160 通过，4 跳过，无失败。隔离源码下显式提供只读 `MDO_XADMIN_ROOT`、`MDO_TEST_WEBSITE_REPO` 测试依赖路径。 |
| 前端回归 | 1058 项：1057 通过，1 跳过，无失败。包含插件作用域、清理、启动门禁及意图记录异常。 |
| Windows C11 | `-Wall -Wextra -Werror` 全应用语法检查通过。 |
| Windows 打包程序 | 实际 TCC SDK 综合回归、产品 API、命令隔离/取消/超时/去重、手动压缩与故障原子性通过。具名项目租约新增用例也通过。 |
| Linux glibc | 新编译宿主和最终打包的 server 程序，实际 TCC SDK 综合回归通过，包含本机模拟模型及子 Agent 工具调用。 |
| Linux musl | 静态宿主重新链接 8 MiB 默认栈；旧包能稳定复现崩溃，栈修正后完整 SDK 回归通过。独立命令、父进程退出清理通过；默认线程栈的真实 pthread 回归通过。 |
| Skill 权限边界 | 真实 TCC 回归模拟不可访问祖先通过；实际工作区拒绝访问、损坏 Skill 配置仍失败，不会被忽略。 |
| Android ARM64 | NDK 真实目标文件编译通过：`-Wall -Wextra -Werror`（保留已有未使用静态辅助函数例外），无未解析的 `Mdo*` 产品函数。 |
| Android 安装包 | 轻量/完整包均通过 CRC、条目映射、原生 ELF 依赖及 16 KiB ELF/ZIP 对齐检查；构建完成 v2/v3 签名检查和安装策略测试。完整包 76 个原生 ELF、125 个运行时路径。 |

本机模型相关用例全部使用 loopback 合成 SSE，无付费模型请求。Linux 交付物是 headless server；glibc 包使用本机 native ABI，**未作为 glibc 2.28 基线发行包验收**；未单独人工验收 Linux 原生窗口。

### Android 真机状态：部分通过，最终复验待设备

手机实际运行独立包 `org.xleaves.mdo.qa.applicationsdk`，日常 mdo 安装和数据保留。已通过：

- JNI 宿主、TCC 服务目录、产品契约及原生函数目录；
- 空/非法流句柄、环形缓冲、EOF、过期句柄；
- 应用 UID 下的中文文件、进程空参数和非零退出码；
- ARM64 原生模块服务/钩子注册、独立 C 命令 RPC。

随后创建会话触发 Skill 祖先权限问题；已定位并修复，也从真机 WebView 日志修复了插件早于恢复门禁的竞态。修复后的 APK 已编译，但手机随后从 USB/ADB 消失，用户答复“暂时无法连接”。因此**修复后的完整真机链路尚未通过**：受管工具、模型回调、异步操作、凭据库及 WebView 实际渲染仍需重新运行独立脚本，不能以编译或桌面测试替代。旧结果的 `passed` 为 false，保留分项证据。

恢复测试时使用以下命令（ADB 端口及 serial 以当时设备列表为准；evidence 目录必须是新目录）：

```powershell
python -X utf8 tests/test_application_sdk_android.py `
  --adb .build/android-platform-tools/platform-tools/adb.exe `
  --adb-port 5041 --serial <device-serial> `
  --package org.xleaves.mdo.qa.applicationsdk `
  --evidence .build/sdk-completion-20261022/android-device-recheck
```

## 源码与测试包

其他任务同时修改生态安装/插件管理文件。本轮保留这些未提交改动，使用已提交源码的 `git archive` 快照构建：产品代码在 `07b7a965` 与 `6311b477` 之间相同；Linux musl 构建包含 `0a0eaa83` 的栈修复。所有提交位于 master。打包回执的 dirty 标志来自共享检出目录，实际打包输入为冻结快照，不包含其他任务未提交的安装实现。

产物位于 `.build/sdk-completion-20261022/`（目录沿用本轮开始日期）：

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `mdo.exe` | 5717874 | `f02055e7c1ae4df498622005ba9f53f4a2338f3129bfafa7ac4a8d884f84cb5d` |
| `mdo-arm64-v8a.apk` | 5449025 | `678d0ff01e28deb32a6611e60e0422ec35ef04c80a729aa4b06aaa08c25b6487` |
| `mdo-full-arm64-v8a.apk` | 62210978 | `0a25856493a3f10e75478f312b7539354d0a3a29ed599248b2be283dc3b89369` |
| `mdo-sdk-qa.apk` | 5366522 | `6bec6aaa4859263bd17199640ef5a789165cb5e4106004ba6dd30e7447cc1e30` |
| `mdo-server-linux-x86_64-glibc` | 5423899 | `3ab5394be707f6d21459a2efd422fa99fa9e1c7be716ca30f6939befe0b5a39c` |
| `mdo-server-linux-x86_64-musl` | 7676672 | `cf5356b3b112107775ac21c4ce4dd0cad09e5f4712e7d64db4d0d94d342c4f72` |

安卓包使用本地开发签名；设备回归应安装独立 QA 包。三个 APK 的 `assets/app.xrtpack` 完全一致，SHA-256 为 `febc3006398e6a89b8b8a6000ffb97b302b8022a6bd8ee93c20cb09639dea061`。未替换正式安装包或网站下载。

私有证据包括 `.build/application-sdk-completion-*.log`、`artifact-validation.json`、`android-device-final-*/result.json` 和 musl gdb 栈。保留证据不代表该项已通过；以本记录及各结果的 passed 状态为准。没有持久化真实账号令牌或供应商密钥。
