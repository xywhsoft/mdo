# 连续对话完成状态验收

## 问题与修复

此前真实 HTTP 验收遇到：运行已经显示成功，下一轮却收到 `session_busy`。
旧版本在锁内发布终态、移走指针并释放活动名额，锁外才销毁运行与会话。
另一个请求因此能在会话仍占用时观察到成功。极快的模型也可能在启动
返回时提前暴露终态。

现在先领取内部收尾状态，保持运行中及活动名额，在锁外释放资源后再按
不可变运行 ID 发布终态。列表移动不影响定位，关闭管理器等待收尾完成。
已经结束执行、正在释放的运行接受幂等取消，但不把成功误标为取消。
没有增加前端延迟或自动重放模型启动。

## 可控生命周期检查

`tests/test_run_completion_runtime.py` 只在隔离 TCC 探针中包装销毁和条件
等待，产品没有测试开关。使用合成模型回调，覆盖：

- 确保模型在启动返回前完成，启动仍报告运行中。
- 暂停释放时终态不可见、活动名额保留，第二个 Pump 不重复领取。
- 此时取消不误改结果；释放后同一会话立即启动下一轮。
- 收尾过程中保留列表淘汰旧记录并移动元素，原运行按 ID 正确完成。
- 手动 Pump 收尾时关闭管理器，关闭必须等待释放，所有 owner 引用平衡。

旧源 `3990e0d` 使用同一探针复现提前完成和活动名额提前释放。
修复前后结果保存在 `acceptance.json`，不需要概率循环或高负载。

```powershell
python tests/test_run_completion_runtime.py
python tests/test_run_manager_runtime.py
python -m unittest discover -s tests -p test_runs_contract.py
node --test tests/test_submission_controller.mjs tests/test_queue_gate.mjs tests/test_run_stop_controller.mjs
python tests/test_packed_message_edit_recovery.py --packed .build/conversation-acceptance/mdo-run-final.exe
```

## 打包版与浏览器

Windows 候选包 `.build/conversation-acceptance/mdo-run-final.exe` 为 4,391,234
字节；SHA-256 记录在 JSON。真实 HTTP/TCC 检查在完成后直接验证会话关闭，
随后开始下一轮；原有等待 `runtime_open` 的绕过已移除。历史修改重放、
修订保护及重启写入围栏仍正常。

真实浏览器完成两轮连续发送，再用可控流式回复检查下一条消息排队及
自动发送。四轮各一次模型请求，四次开始/完成，零最终错误、零控制台
错误；结束时队列、输入草稿及活动运行均为空。截图分别记录排队和完成
状态。回环模型与独立 Home 没有访问付费模型，也未修改日常会话。

本阶段未安装或发布 APK；Android 签名沿用上一阶段记录的升级限制，
真机功能检查仍未完成。本记录不代表压力测试或全部功能最终验收。

## 启动失败收尾补充

继续审查发现：启动失败会先移除未公开记录、减少启动计数，然后才释放
局部运行和会话。关闭流程可能据此过早返回。可控探针包装一次 Agent
Start 失败并暂停释放，在 `7fdef2b` 上复现 `starting:0`；修复后保持
`starting:1`，关闭进入真实条件等待，释放后启动线程和关闭线程正常退出，
owner 引用平衡。没有改变启动请求的执行不确定标记或增加启动重放。

新增结果见 `failed-start.json`。此后 Windows 候选为
`.build/conversation-acceptance/mdo-start-final.exe`，实际 HTTP/TCC 的两轮
连续发送、历史编辑重放和重启围栏验证通过。上面的四轮浏览器截图及
`acceptance.json` 对应先前的 `mdo-run-final.exe`，两个包的哈希分别记录。
两批候选文件均保留，方便复核。
