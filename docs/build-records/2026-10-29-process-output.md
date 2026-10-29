# 2026-10-29 进程输出与工具短名

本次完成 exec/spawn 的文件输出、自动进程资源释放、状态与输出读取分离、工具短名，
并将相同存储接入 Application SDK。产品规则见 [命令输出与后台任务](../process-tools.md)。

## 实现

- `93901e81`：统一输出存储，先保留前缀再顺序落盘；容量、保留期、活跃任务锁、显式路径与原始字节校验。
- `4af4f448`：exec/spawn 接线、后台监视与自动释放、status/wait 语义、取消和退出结果、保留 MCP 原有协议读取。
- `d82cb83d`：工具短名与执行别名、提示词/目录/历史恢复、结果目录上的 read/grep/ls/glob、不可变结束标记及原生回归。
- 后续同批提交：SDK process.run、Home 中不创建目录的维护入口、定期回收、子任务报告截断范围及既有回归脚本适配。

共享源码目录中存在另一项任务的扩展身份和依赖关系改动。本次只提交自己的修改；
共用文件按基线单独暂存修改，没有重置、覆盖或提交另一项任务的差异。
既有 `app/xs.json` 与 XRUDP 长测脚本哈希保持原值。

## 验证

| 检查 | 结果 |
| --- | --- |
| Windows 原生输出存储 | 111 项通过 |
| Linux 原生输出存储 | 111 项通过 |
| Windows 实际 exec/spawn/status/wait/文件工具 | 36 项通过；长 stdout/stderr 逐字节一致 |
| Linux 实际进程与 MCP stdio | 相同 36 项通过；复用 WSL 和现有 SDK 源码 |
| 完整 mdo 服务的公共 SDK | 31 项通过；直接使用 app 源码，独立临时 Home |
| 实际外部工具目录租约桥接 | 9 项通过；退出、启动失败、拒绝、停用后的占用及停止后释放 |
| Python 单元回归 | 202 项，196 通过、6 跳过 |
| 前端串行回归 | 1210 项，1208 通过、2 跳过 |
| 前端模块图 | 236 个模块解析且可达 |

针对性回归包含：短输出分流；超过 3 MB 的日志；靠近文件末尾的搜索；结果路径只读；
显式目的地批准、相对 cwd、拒绝覆盖及拒绝前不启动；无查询时自动释放；stdin 与句柄
自动释放的交接；退出父进程的后代管道；运行中的任务在 runtime 关闭时回收；原生错误
原因链；非零退出、超时、输出上限和 SDK 部分结果；旧 poll 执行别名；非进程报告及
触及报告保留预算后的截断范围；MCP stdio 通信。

所有调用均为本地合成子进程或本地 MCP 对端，没有真实模型、供应商或 SSH 流量。
Linux 额外探针只编译所需源码和一个临时可执行文件，测试后删除；未创建仓库副本、
WSL 发行版或新的运行时环境。

Windows 验证宿主在 `.build/process-tools-20261029/host/xs.exe`，SDK 锁定补丁哈希：
`4a09127727b100f15bcfa94c12ab75864188eab048bc139c4e8f681639b86903`。
复现示例：

```powershell
python tests/test_process_output_store_native.py --sdk .build/process-tools-20261029/sdk --cc E:/software/w64devkit/bin/gcc.exe
python tests/test_process_output_runtime.py --host .build/process-tools-20261029/host/xs.exe
python tests/test_process_output_sdk.py --host .build/process-tools-20261029/host/xs.exe --ownership
```

该宿主用于本地验证；本次没有执行产品版本发布或线上部署。
