# 中断会话更换已不可用的模型

2026-10-10。检查 E 盘已有数据的隔离副本时，发现一个空闲但尚需恢复的会话无法更换已缺失的模型：模型选择返回 409，恢复读取又依赖旧模型。这会使用户无法从模型配置故障中恢复。副本未复制账号和模型配置，原模型缺失是本次隔离条件，不代表日用安装的线上模型已删除。没有向模型发送个人历史，也没有修改原 Home。

## 修复

空闲会话的配置更新仍验证目标模型、推理强度、权限、元数据版本和运行时独占，但不再要求先关闭中断轮次。更换模型保留未完成调用和账本，后续恢复仍使用原有决策流程。恢复入口复用明确的模型配置错误分类，不再将模型缺失误报为恢复服务不可用。

## 原生回归

`tests/test_interrupted_profile_runtime.py` 使用隔离源码、原生模型和事件回调分别留下一个未完成读取和写入，经过真实 HTTP 设置、模型选择与恢复入口：

- 修复前模型选择确定返回 `409/session_state_conflict`。
- 更换模型后中断调用和账本序号保留，工具没有执行。
- 删除当前模型，恢复入口准确返回 `422/model_profile_model_missing`。选择另一个不存在的模型被拒绝，当前配置和版本不变。
- 更换到有效模型后仍能检查原中断状态；写入不会自动重试。明确继续后，读取和写入各执行一次；写入内容读回正确。
- 回环模型监测到零提供方请求。此处验证恢复机制，不冒充真实线上模型对话。

同时通过历史清空/截断后的恢复、用户停止仍需手动继续，以及会话创建、恢复、配置变更、生命周期和持久化原生检查。严格 GCC（警告作为错误）、完整 Python 契约和 35 个相关前端行为检查通过；没有压力或高负载测试。

```powershell
python tests/test_interrupted_profile_runtime.py --host .build/conversation-acceptance/mcp-http-status/host/xs.exe --output .build/interrupted-profile-fixed.json
python tests/test_recovery_history_runtime.py --host .build/conversation-acceptance/mcp-http-status/host/xs.exe
python tests/test_session_runtime.py --host .build/conversation-acceptance/mcp-http-status/host/xs.exe
```

打包版和真实旧数据副本的复核记录在后续验收提交补充。此阶段没有将修改直接应用到用户会话。

## 成功选择模型后的页面状态

实际打包页面验证还发现：更换模型成功后，恢复卡片仍显示旧的模型缺失提示，必须手动重读。页面可见且服务已连接，这不是后台页面的定时暂停。成功确认当前会话的新配置后立即重读恢复状态，随后应直接显示“继续任务”；过期、取消、离开会话或被新版元数据覆盖的确认不会刷新另一个会话。相关前端行为及 982 项完整前端检查通过，原生修复的旧数据读取结果仍单独记录。
