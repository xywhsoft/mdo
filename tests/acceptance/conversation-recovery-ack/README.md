# 中断恢复请求的确认丢失验收

旧打包版 `mdo-start-final.exe` 中，点击“继续任务”返回 202 后人为断开
响应，模型已经继续生成，页面却立即提示“无法连接本地服务，请确认 mdo
仍在运行”。实时无障碍状态捕获了这条提示；基线截图拍摄较晚，只记录了
仍在继续的回复，不能当作提示截图。

修复后，每次明确点击只提交一次。继续请求携带随机确认标识，响应丢失
后从运行列表查找同一项目、会话及标识；结束回复则读取同一会话更新后的
关闭边界。只读确认采用有界退避，不重放模型、工具或结束操作。请求及
确认共用一分钟上限，离开页面可取消。永久拒绝仍保留准确原始分类。

## 真实打包页面

- 成功恢复的 202 响应丢失：页面安静确认，正常完成，没有错误提示。
- 结束回复的 200 响应丢失：关闭状态正常确认，输入草稿保留。
- 恢复请求在发出前被断开：只提交一次，确认期间没有新模型调用；六次
  只读确认后仅出现一次“暂时无法确认任务是否已继续”的最终提示。
- 新的状态检查后明确点击继续：使用新的确认标识，正常完成，草稿保留。

五次真实模型请求对应五次运行开始和结束，零最终模型错误。两个恢复
请求没有增加用户消息，未发送的草稿也没有进入模型。测试期间输入框有
额外字符输入，按实际最终内容保存并核对；没有删除或发送这些草稿。
`browser.json` 记录请求方法、确认标识、提示采集和最终草稿。截图中
确认耗尽的提示已消失，唯一最终提示以实时 DOM 观察记录为准。

## 接口与模块检查

```powershell
node --test tests/test_recovery_action.mjs tests/test_message_edit_recovery.mjs tests/test_recovery_decisions.mjs tests/test_frontend_session_runtime.mjs
python tests/test_run_completion_runtime.py
python tests/test_packed_recovery_ack.py --packed .build/conversation-acceptance/mdo-recovery-ack-final.exe
python tests/test_packed_message_edit_recovery.py --packed .build/conversation-acceptance/mdo-recovery-ack-final.exe
python -m unittest discover -s tests -p test_api_contract.py
python -m unittest discover -s tests -p test_frontend_contract.py
```

30 项 Node 检查验证有限重试、精确身份、永久拒绝、取消和服务重启。
22 项 API 合约、20 项前端合约及运行生命周期探针通过。真实 HTTP/TCC
检查验证无效标识不改历史、不启动模型，运行列表与详情返回同一标识，
忙碌/旧恢复拒绝，旧格式兼容，结束边界更新和旧进程请求保护。原有历史
编辑重放检查也通过。

包大小与 SHA-256、HTTP 结果及模型请求摘要见 `acceptance.json`。
所有请求仅访问回环测试模型，使用独立便携目录；没有压力测试、安卓
安装或官网发布。该记录不代表全部功能及安卓真机最终验收。
