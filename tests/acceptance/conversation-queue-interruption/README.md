# 模型执行中断与队列重启

使用 `mdo-compaction-errors-final.exe`（构建及 SHA-256 见上一阶段 `conversation-compaction-failure/build.json`），一份一次性便携目录、一个回环合成模型和三次模型请求。没有外部服务、真实密钥或压力测试。本阶段不修改产品行为。

旧用例确认持久化启动后队列收据可恢复，但模型可能已因配置不全而结束，无法证明执行中的中断恢复。本阶段改为通过标准模型设置 API 登记回环模型，确认服务已收到生成请求且运行仍未结束，再终止测试进程。另保留一条未执行的排队输入。

离线夹具模拟启动已落盘、队列收据尚未提升的断电窗口，随后重启同一 Home。两条队列记录恢复；已启动条目的重复提交明确返回 `queue_run_started`，重启和重复提交都没有触发额外模型请求。用户明确继续后原任务成功，随后下一条队列任务成功；历史中只有两条原始输入，没有重复原任务，零最终错误。具体断言结果见 `packed.json`。

```powershell
python tests/test_packed_queue_start_recovery.py --packed .build/conversation-acceptance/mdo-compaction-errors-final.exe --record tests/acceptance/conversation-queue-interruption/packed.json
```

这项验证针对本地模型和队列启动收据，不代表设备互联、手机后台或已经产生外部副作用的工具调用都可自动重放；未知工具结果继续遵循原有明确决策流程。没有更新日常安装、手机或官网下载。
