# 历史编辑后的会话恢复

本阶段修复三项已经复现的问题：

- 清空或裁剪历史会重用回合号，页面却按会话和回合号永久去重，使新的中断不再自动继续。
- 调用 ID、参数和账本序号也可重用，旧恢复令牌和已审阅的重试选择可能对应到新历史。
- 首次打开中断会话时，恢复检查可先于启动写入检查完成，自动继续立即被拒绝并显示无关的项目清除错误。

恢复 GET 返回现有历史文件生成标识 `history_epoch`。v3 恢复令牌和页面选择绑定该标识；自动尝试绑定历史、会话和回合，同一历史的轮询不会追加自动尝试。没有稳定文件 ID 时仍可手动继续，但不自动继续；不新增持久化回执或侧车文件。普通未中断会话不额外扫描历史。

自动继续等待启动写入检查、连接及控制权限就绪。恢复后草稿仍在。用户主动停止始终需要明确继续，执行、写入等未知影响操作仍使用原有人工确认。

## 证据的边界

`native.json` 来自实际 xs/TCC 和原生账本、读取工具、清空、裁剪、恢复 HTTP 路由。仅模型回复及一次事件失败是本地回调注入，没有模型网络请求。清空和裁剪后的两次待恢复调用均为回合 1，但历史标识及令牌不同；旧令牌返回 409，持久化会话数据保持不变。三次正常恢复各执行一次读取；缺少稳定文件 ID 的同一调用不自动继续，但手动继续成功。

`ui.json` 和三个 PNG 来自相同隔离源码夹具的完整正式页面。第一次启动、清空后返回、裁剪后返回共自动读取三次；期间仅在设置和会话间导航，没有重新载入页面来清空去重状态。输入“下一步继续：保留我的输入 🌱”保留。随后用户停止不增加读取，一次手动继续才执行第四次读取。

故障夹具故意记录了持久化失败。`automatic-after-truncate.png` 中的历史存储错误来自这次注入，并非新的模型重试提示；已恢复存储错误的历史展示仍需进一步打磨。不能把这些源码夹具结果称为真机或未修改打包运行。

`candidate.json` 独立核对干净候选的构建来源、体积、SHA-256 及十一项包内生产源码。打包恢复确认、消息编辑和压缩等使用未修改的候选及独立 Home 另行验证。日常程序、Android 和官网本阶段未更新，没有压力或高负载测试。

生产提交 `14cff4e6`，候选 4,539,227 字节（增加 724 字节），SHA-256 `6fb891968d84fe35ce8e6f4677afe93efac89dc00bb28d84c48a995d4148c02f`。三组打包运行记录分别为 `packed-recovery.json`、`packed-edit.json` 和 `packed-compaction.json`，对应 3、2、12 次回环模型请求。故意拒绝的总结恢复成功，压缩检查最终模型错误为零；所有记录绑定同一候选 SHA。临时服务及本轮浏览器页已关闭。

## 复查

```powershell
python tests/test_recovery_history_runtime.py --host .build/conversation-acceptance/android-page-restore/host/xs.exe
node --test tests/test_automatic_recovery.mjs tests/test_recovery_decisions.mjs tests/test_recovery_action.mjs
python tests/acceptance/recovery-history/check_evidence.py --exe .build/conversation-acceptance/recovery-history/clean/mdo.exe
```

页面夹具使用 `tests/manual_recovery_history_qa.py` 的新目录、回环地址和单工作线程；停止时创建其 `stop` 文件。`/__probe/history` 和离线回调只在测试副本中存在，候选不包含它们。
