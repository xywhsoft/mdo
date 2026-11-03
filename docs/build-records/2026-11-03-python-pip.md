# Python 外部工具：pip 与便携依赖

用户要求允许模型自行安装任务需要的 Python 依赖。原实现没有执行权限层面的
pip 禁止开关；问题是精简运行时未附带 pip，且工具提示词强调不可假设 pip 存在。

## 实现

- 三平台独立 Python ZIP 与 Android full APK 打包路径加入 pip 26.3，官方 wheel 的
  URL、字节数与 SHA-256 固定在 `tools/runtime/build/pip.lock.json`，缓存命中也校验。
- 提示词允许 `python -m pip install PACKAGE`，仍遵守任务的进程与网络权限。
  不预装通用第三方依赖，不修改系统 Python，不关闭 TLS 校验。
- `.pth` 引导在启动时重新定位 Home。默认 `PIP_TARGET` 为
  `data/external-tools/python-packages/<CPython ABI 与平台>/site-packages`，自动加入导入路径。
  Python 字节码缓存进入 `data/cache/python`；pip 下载缓存默认关闭。
  显式 `PIP_TARGET` 和 Python 缓存位置仍可覆盖默认值。
- 模块与依赖不写入扩展本体，避免审核摘要失效、工具升级丢失依赖；ABI／平台变化时
  使用不同目录，需要重新安装对应依赖。未配置 Home 的独立测试解释器使用自身前缀下的
  `mdo-packages` 和 `mdo-cache`。
- Linux 子进程使用随包启动脚本；pip 的 musl 检测改用随包加载器，而不是主机的
  `/lib/ld-musl-*`。仅修改暂存中的 pip；固定的上游源码定位不匹配则构建失败。
- Windows 保留标准项目导入和显式 `-I`、`-E`、`-P` 行为。

## 验证

| 检查 | 结果 |
| --- | --- |
| pip 路径布局、损坏缓存、目录逃逸／符号链接拒绝 | 3 项通过 |
| Android 原有八工具选择、原生镜像与别名策略 | 9 项通过 |
| Windows 真实解释器项目导入、参数与退出码 | 12 项通过 |
| Windows 与 Linux 实际 pip 安装 openpyxl 3.1.5、python-docx 1.2.0 | 通过，含各自 lxml 平台 wheel |
| Excel／Word 创建及重新读取 | 两平台通过 |
| 安装依赖及导入前后工具目录逐文件 SHA-256 对比 | 完全相同 |
| Home 移到中文／空格目录，工具目录改名后继续导入和读写 | 两平台通过 |
| Linux 私有加载器选择与 Python 子进程执行 | 通过；检测禁止调用主机 `/lib/ld-musl-*` |
| Android full 运行时容器 | pip 位于 Python ZIP；仍仅原有八工具、74 个去重原生镜像 |
| 三平台商店列表及完整 HTTPS 下载 | 版本可见，字节数与 SHA-256 全部匹配 |
| E:\software\mdo 本机正常商店更新 | runtime_ready=true，Python 保持启用，新提示词生效 |
| 本机 pip 安装与 Excel／Word 实测 | 通过，扩展本体不变 |

Windows 版本 `3.13.7+pip.26.3`，Linux `3.12.15+pip.26.3`，Android
`3.14.8+pip.26.3`。本机已安装测试所需的 openpyxl、python-docx；测试文档写在项目
`.build/python-pip-20261103`，不改动用户会话和项目文件。

## 发布及边界

本次仅发布 Python 外部工具，保留此前版本和其他工具的发布头，私有数据库备份位于
`/home/xs/mdo-python-pip-20261103-5530ed47/catalog-before.db`。未重启线上服务；发布后
`xs.service` 为 active。未修改或重新发布 mdo 主程序及其版本号。

Android 加入的都是纯 Python 文件，不增加原生镜像。Android full APK 的打包接线已
完成，随下一次 full APK 发布生效；本轮未构建签名 APK、未真机验证 pip，也未修改用户
原有的 Android 安装。纯 Python 包可按此机制安装；原生扩展还需 Android 兼容 wheel，
不能假设 PyPI 上的桌面 wheel 能直接使用。开发头文件／原生编译工具链不在本次范围。

证据摘要见同名 JSON；完整私有收据和测试输出在 `.build/python-pip-20261103`。
