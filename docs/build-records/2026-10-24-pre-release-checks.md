# mdo 0.5.0 发布前检查（2026-10-24）

冻结候选源码：`07ce5feb9eb197c2fddaa2d540b87607ac62e28b`；在 `master` 提交修正。本轮没有发布线上。
最终候选重新构建后执行整轮 Windows 门禁，未拼接修改前后的通过结果；早期失败日志保留。
日常 mdo 的程序、账号、会话和项目数据未被替换。

## 发现与修正

1. 独立工具探针需要 `--stage/--platform`，门禁却传入 `--host`。现在支持显式暂存目录和平台；
   未提供产物明确标注未执行，不计通过。没有选中工具或工具没有探针实现时失败退出，防止空跑成功。
2. 严格 C11 被 37 处告警阻断：36 处多语句误导缩进、1 处 Windows 未使用参数。
   分行澄清控制流并标记参数用途，未关闭告警规则，未改变这些语句的控制流。
3. 旧测试未同步生态包响应上限、独立会话快照上限及项目清理新增的变更基线目录。
   按实际边界更新断言，保留普通响应限额和完整清理清单检查；未改动这些业务策略。
4. 显式提供已应用兼容补丁的 SDK 时，构建仍依赖原缓存和 Git 元数据。
   现在复用锁定记录匹配的 SDK，仍验证补丁哈希和依赖内容。不同缓存、无 Git 元数据与错误哈希回归通过。
5. 标准门禁缺少原生右侧栏功能验收。已加入五项同一打包候选的原生探针，覆盖入口、视口、显隐、
   错误恢复、文件、变更与真实终端；确认隐藏/展开保留面板身份和已加载浏览器内容。修正了测试场景输出错标。
6. 线上 Android full 已使用 `50000017`，旧源码仍复用该构建号，新内容不会触发升级。
   版本名保持 `0.5.0`，八个渠道分配 `50000018`～`50000025`；均高于检查时对应线上渠道。
   公共更新目录只读核对收据为 `.build/pre-release-20261024/public-updates-system.json`，没有修改线上目录。

## 最终验证

| 检查 | 结果 |
| --- | --- |
| Windows Python | 173 项，条件跳过 5 项，其余通过 |
| Linux Python | 173 项，条件跳过 2 项，其余通过 |
| 前端 | 218 个模块可达；1112 项 Node 测试全通过，含实际网站源码联动比较 |
| 严格 C11 | Windows/Linux `-Wall -Wextra -Werror` 通过 |
| Windows 完整门禁 | 111 项运行探针、13 项打包产品探针通过 |
| 原生右侧栏 | 五项通过：四入口、浏览器承载视口、浏览器生命周期/文件、变更、Unicode 终端 |
| 便携和启动 | Home 搬移/覆盖、WebView2 缓存位置、20 秒打包服务初始化/存活通过 |
| 确定性打包 | 两份 Windows GUI 字节一致，SHA-256 `9342baca24ecbc8a5001f356cb1e9a187e179c6d4056d9e949dcd2b7ce5a8f6c` |
| Windows server | 重新构建无界面宿主；实际服务端包的公开 SDK、CRUD、进程、模拟模型、错误处理通过 |
| Linux 补充 | 原生文件系统上的文件、PTY、原始工作区基线通过；最终服务端包的公开 SDK 通过 |
| 可选工具 | Windows/Linux 的 Git、SQLite、Poppler、ImageMagick、FFmpeg 实际操作均通过 |
| Android 安装 | 19 项文件策略边界及锁定安装桥接的 Java 编译/接口漂移检查通过 |
| Android 两个候选 | 正式签名 v2/v3、证书、CRC、构建号、运行时映射、ELF 依赖及 16 KiB 对齐通过 |

Android lite 包含 2 个宿主 ELF、无外部工具；full 包含 150 个原生 ELF、734 个运行时逻辑路径。
证书 SHA-256 为 `94b1947b60f3ce0852b19d9c37a47cc4a084fe9d601692c6a1f6b204c2174e84`，与仓库公共正式证书及现网一致。
最终门禁使用 `tools/qa_release.py`，显式提供锁定 SDK 和工具暂存目录；未跳过宿主重建或 GUI 门禁。

## 候选

| 渠道 | Build ID |
| --- | ---: |
| Windows GUI/server | 50000018 / 50000019 |
| Linux glibc GUI/server | 50000020 / 50000021 |
| Linux musl GUI/server | 50000022 / 50000023 |
| Android lite/full | 50000024 / 50000025 |

Windows 文件在 `.build/pre-release-20261024/delivery/`，Android 正式签名候选在 `android-release/`：

| 平台 | 版本 | 字节 | SHA-256 |
| --- | --- | ---: | --- |
| Windows x64 | GUI | 6,214,112 | `9342baca24ecbc8a5001f356cb1e9a187e179c6d4056d9e949dcd2b7ce5a8f6c` |
| Windows x64 | server | 5,957,995 | `fec1098cd9c10960009f5567d46908cc0a0e6f3003ee303ad9a4a1dd633b0787` |
| Android ARM64 | lite | 5,756,225 | `26ecb611ba6f71afe0b509326e92e5feba998fe7178b5ff9f8b60f53e1449cfa` |
| Android ARM64 | full | 150,100,377 | `29734ee8c864e109b4fe8893ee25843f7f5274c3653b58b9b06086a6a29dfdf6` |

本轮没有重建四个 Linux 正式发行物或生成八平台正式发布清单；Linux 构建号预留给后续发行物。
已测 Linux 服务端包保留在 `.build/pre-release-20261024/linux-final/`，使用回执校验过的锁定缓存宿主。

## 证据及边界

- 完整门禁日志：`.build/pre-release-20261024/release-gate-final.log`；汇总：`summary.json`。
- Android 正式签名候选回执：`android-release-validation.json`；构建号检查：`build-id-checks.json`。
- 原生测试结果在 `.build/qa-release/native-*/native-result.json`。Linux 临时源码已清理，日志和已测程序保留。
- 当前 ADB 没有设备，Android 真机安装、升级和系统后台交互未执行。
- Linux GUI/musl 的完整发行门禁本轮未执行，Linux 补充检查不替代该验收；实际发布相应渠道前仍需完成。
- 未做压力或长时间稳定性测试。模型回归使用隔离的模拟供应方，没有真实付费模型负载。
- 原生浏览器使用本地固定页面验证，没有将其当作两个公网页面的目视复测。
- 未修改或提交其他任务的 `tests/test_remote_asymmetric_pause.py`。

修正提交：`a46244ae`、`af95c3aa`、`8a1c8df2`、`a7e399b8`、`45b41658`、`296dd96a`、`07ce5feb`。
