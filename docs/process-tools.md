# 命令输出与后台任务

`exec` 和 `spawn` 直接执行 `argv`，不增加 shell 展开。需要脚本语法时，显式调用
相应的解释器。`exec` 等待命令结束；`spawn` 返回任务编号及立即可读取的日志路径。

## 输出

stdout、stderr 默认分开保存。短输出直接返回；输出较长时，先保留已收到的前缀，
再按原始字节顺序写入文件，结果只带简短预览及文件位置，不使用尾部循环缓冲区。
日志可以在进程运行时读取；读取不会消费输出，也不会改变其他调用者的读取位置。

自动文件位于 `mdo-home/artifacts/process/<id>/stdout.log` 和 `stderr.log`。
工具返回 `@results/process/<id>/stdout.log` 等逻辑路径及 `native_path`：

- `read` 分页读取逻辑路径，继续使用返回的字节偏移。
- `grep` 可以搜索完整结果日志，不受普通工作区文本文件的 2 MiB 限制；仍有总扫描量、单行长度和结果数量限制，未完成时会明确提示。
- `ls` 和 `glob` 可以查看结果目录，返回可继续读取的逻辑路径。
- `@results` 为只读入口，`write/edit` 不接受它作为目的地。
- 外部程序或 SDK 文件接口使用 `native_path`。

结果中的每个流包含 `bytes`（收到的字节）、`saved_bytes`（实际保留的字节）、
`complete`、`preview_omitted`、`path` 和 `native_path`。`preview_omitted` 只说明内联
预览省略了内容，不代表日志丢失。只有 `output_complete` 为真时，才能断言收到的
stdout/stderr 均已完整保留；失败时还会返回 `output_error`。

可选 `stdout_path`、`stderr_path` 指定保存位置，相对路径以命令的 `cwd` 为基准。
它们必须是尚不存在的文件；不覆盖、不追加已有文件，并在启动命令前检查。
使用 `merge_stderr: true` 可将两个流合并到 stdout，此时不能再指定 `stderr_path`。
路径仍遵守当前 Agent 的访问范围及写入批准规则。

默认单次输出总量上限为 64 MiB，可用 `max_output_bytes` 调整为 1 字节至 1 GiB。
达到上限或磁盘写入失败后仍继续排空子进程的管道，防止死锁；结果明确标记不完整，
报告已保留范围，不能把这种结果当作命令没有执行而自动重跑。

## 状态与生命周期

`status(task_id)` 只返回任务状态、退出结果及输出文件位置。
`wait(task_ids, ...)` 等待结束并返回相同的状态资料，不再收割进程输出。
`stdin` 继续负责输入及关闭标准输入；`stop` 负责停止任务，默认保留状态收据和日志。
SubAgent 的状态与最终报告仍通过 `status/wait` 获取，大报告同样可以返回文件位置。
原有子任务报告保留预算若导致截断，会另外标记 `result_truncated` 和 `result_offset`，
说明保留范围，不把截断后的报告冒充完整结果。

后台进程退出并排空管道后，宿主自动释放进程句柄、读取线程和外部工具使用租约；
即使模型从未调用 `status/wait` 也会释放。应用关闭时会停止其拥有的后台进程并等待
读取结束。任务收据与日志的生命周期分开，释放收据不会删除日志。

自动日志总预算为 1 GiB。活跃任务预留其输出上限并持有生存锁，回收器不删除它们。
新日志预留空间时及应用运行期间的定期维护会回收已完成日志：超过七天，或为满足
容量限制而按时间删除最旧条目。目录扫描也有数量上限。显式输出文件不参与回收，
由调用者负责保管。日志是临时结果，不保证永久存档或断电时的持久化。

## 工具短名

| 原名称 | 新名称 |
| --- | --- |
| `poll` / `task_status` | `status` |
| `ask_user` | `ask` |
| `web_search` | `search` |
| `web_open` | `open` |
| `web_find` | `find` |
| `tool_search` | `tools` |
| `tool_load` | `load` |
| `mcp_read` | `fetch` |

新目录、提示词和调用使用短名。旧名称仅作为导入/执行别名保留，避免恢复已有会话或
读取原有 Agent 配置时失效；不重复向模型暴露两套工具。配置项 `agent.web_search`
仍表示联网搜索开关，它不是工具名。

## Application SDK

`process.run` 使用相同存储和路径规则，增加 `stdout_path`、`stderr_path`、
`max_output_bytes`、`merge_stderr`。短输出保留 `text/base64` 兼容字段；大输出返回
文件位置，不将整个文件再编码到 JSON。`stream: true` 继续发送进程输出事件。
`truncated` 表示实际保留不完整，预览省略本身不算截断。

非零退出仍是带 `exit_code` 的正常 SDK 结果。超时或取消返回对应错误，同时保留
结果对象及已收到的输出；HTTP SDK 调用可在错误的 `details.result` 中读取它。
