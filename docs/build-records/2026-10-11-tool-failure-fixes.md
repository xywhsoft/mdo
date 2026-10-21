# 工具调用失败修复候选

本轮只修复会话审计中选定的三项问题：Windows Python 项目导入、进程错误被通用提示掩盖、编辑失败的候选上下文。审计来源是 `E:\software\mdo` 的会话记录，原始记录未修改。

## 结果

- Windows Python 工具包在打包前移除 `python*._pth`，使用 CPython 标准路径初始化。脚本目录、当前目录和显式 `PYTHONPATH` 恢复标准导入行为；`-I`、`-E`、`-P` 保持其正常含义。标准库 ZIP、原生扩展和程序文件随包保留，无启动包装器。ZIP 和 XRTPACK 使用同一转换函数。该行为依据 [CPython 路径初始化文档](https://docs.python.org/3.13/library/sys_path_init.html)，并由真实解释器验证。
- 进程错误保留原生原因链、错误类别、操作、系统码和系统说明。缺失程序与工作目录、系统执行权限不足、无效可执行文件均有具体诊断。spawn 在清理任务前格式化原因；stop 在刷新状态和解锁前保存错误引用；exec 保留启动或捕获失败的运行时原因。参数、环境变量和原生错误 Data 不附加到这些错误提示中。
- 编辑零匹配提供最多三个相似锚点附近的上下文；多匹配定位实际跨行匹配。输出包含行号、制表符标记、行尾空格计数和长行截断提示。候选仅用于纠错，精确匹配、CRLF 存储和整批原子写入规则不变。找不到足够相似的锚点时明确提示读取目标区域，不再默认展示文件头。

## 依赖与产物

SDK 基于原锁定提交 `f05f7661062fdca04da8d571ad859eb4b87c0889` 增加本轮工具诊断修复，保持 xrt 和公共 xwork ABI 8。新提交为 `ffc473e3584c4d0d5a6fafd3ea04a4d471fc0d61`。`deps.lock` 锁定新的生产树和 `tools/dependencies/xs-tool-diagnostics.bundle`，已验证可恢复为干净 checkout。xserver 主工作区未改动。

候选程序：`D:\GIT\mdo\.build\tool-fixes-20261011\mdo.exe`。SHA-256：`8e7b04c9d4d5fcd87ac0f44810bf529500edee1bbc32bfef3129353b8a2561fc`。

工具包修订为 3，实际文件位于 `D:\GIT\mdo\.build\tool-fixes-20261011\toolpacks`。Python 包的 34 个清单文件均通过实际解包后的大小与 SHA-256 校验，无 `._pth` 残留。原始素材 catalog 仍描述输入素材；XRTPACK 清单和工具体积来自转换后的实际文件。

本轮产物保留为候选，未上传发布站。检查时 `E:\software\mdo\mdo.exe` 正在运行，安装仍为之前的程序和工具包；修复生效需要替换为新程序并升级 Python 包。账号和会话数据未改动。Windows 构建号沿用当前候选的 30000055，正式发布应由发布流程分配更新的构建号。Android/Linux 原生宿主需要按新锁定 SDK 重建，本轮未生成这两个平台的安装包。

## 验证

共 46 项针对性运行回归与 16 项构建契约检查通过。

| 验证 | 结果 |
| --- | --- |
| 实际 Python 解释器：脚本同目录导入、中文及空格路径、空参数、`-c`、`-m`、`PYTHONPATH`、`-S`、`-E`、`-I`、`-P`、标准输入、退出码 | 12/12，通过原始素材转换和实际 Python 包解包两次验证 |
| 实际 HTTP/TCC 工具路径：exec/spawn 缺失程序及 cwd、临时文件执行 ACL 拒绝、错误可执行映像、已结束/运行中任务停止、迟后行拼写错误、缩进差异、多行歧义、失败整批不写入、无候选、中文与长行、CRLF、replace_all | 16/16 |
| 原有合法空参数、非法参数与审批、混合批量调用、真实本地 ssh-keygen 空密码 | 18/18 |
| 构建契约、SDK 源码 bundle 恢复与依赖校验、候选实际解包后逐文件核对源码及产品宏 | 通过 |

进程测试均为本机隔离的短期子进程；中断后使用 kill_tree 收尾。停止失败的历史根因未重现，本轮验证了停止正常受理及收尾，失败诊断保留路径通过源码核对；没有将历史停止失败归因于某个未经证实的系统限制。模型服务为本机夹具，没有真实模型或外部 SSH 流量。

详细收据位于 `D:\GIT\mdo\.build\tool-fixes-20261011`：`python-compatibility.json`、`python-package-compatibility.json`、`tool-diagnostics.json`、`exec-arguments.json`、`build.json` 和 `verification.json`。
