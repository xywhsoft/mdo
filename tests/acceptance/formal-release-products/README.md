# 同一来源的八个正式签名发布候选

2026-10-11。完成 [正式 Windows 门禁](../formal-release-gate/README.md) 后，
从隔离副本生成构建号 30000065–30000072 的八个候选，并通过现有严格暂存器。
状态为 `verified-not-published`，没有覆盖日用程序或发布官网。

| 产品 | 构建号 | 字节 |
| --- | ---: | ---: |
| Windows 桌面 | 30000065 | 4,570,279 |
| Windows 服务 | 30000066 | 4,376,060 |
| Linux glibc 桌面 | 30000067 | 4,120,688 |
| Linux glibc 服务 | 30000068 | 4,091,782 |
| Linux musl 桌面 | 30000069 | 5,958,247 |
| Linux musl 服务 | 30000070 | 5,929,405 |
| Android 精简 | 30000071 | 4,203,757 |
| Android 完整 | 30000072 | 60,961,614 |

[发布清单](manifest.json) 与 [构建记录](mdo-builds.json) 保留每个文件、宿主、
来源和摘要。Windows GUI/服务宿主和 Android ARM64 宿主在本轮从锁定源码编译；
Linux 复用同一轮已实测的四个宿主，提取真实程序前缀并核对完整回执和摘要后重打包，
不冒称又做了一次原生编译。glibc 最大版本 2.28；musl GUI 仍需系统 GTK/WebKit。

严格暂存检查通过：版本/八个独立构建号、应用/依赖/配置摘要、实际宿主前缀、
非 Android 程序的内嵌构建号/产品类型/SDK、APK 版本与宿主均匹配。
通过实际 xs 解包逐一比较七个独立应用归档，430 个公共应用文件逐字节一致，
仅排除按平台规范化的 `xs.json`。完整 APK 与精简 APK 的应用归档相同。
见 [实际归档读回](archive-source-readback.json)。

两个 APK 的 CRC、重复项、16 KiB ZIP 对齐、v2/v3 签名与新正式证书指纹通过；
99 个 Android 工具素材文件已按清单核对。精简 APK 的实际原生库和 `classes.dex`
与已经真机验收的 30000064 QA 包逐字节相同，但新签名的官方包尚未安装或验证
升级迁移，不能把原 QA 安装当作该包已验收。公开身份见
[正式签名说明](../../../docs/android-release-signing.md)。

稳定副本保存在 `.build/conversation-acceptance/formal-release-20261011/products`，
六个桌面压缩包保存在同目录的 `../packages`。私钥和密码保存在仓库外的私有目录，
未写入这些产物或 Git。应用包含工作区未提交修改；只在隔离副本中改变发布构建号，
详见 [来源说明](source.json)。冻结副本的输入保持一致。收尾时主工作区的
`app/src/models/completion.inc.c` 和 `online_access.inc.c` 出现新的未提交恢复策略
更新，未计入本轮候选，需后续审查并集成；没有覆盖这些新修改。

剩余：上述新恢复策略的集成、新签名 Android 的数据迁移/安装、网站 App Links 登记及正式发布；Linux
原生系统复制/保存由用户安排后置。没有压力、高负载或生产模型调用。本项完成
构建与暂存核对，不代表这些剩余操作已经完成。
