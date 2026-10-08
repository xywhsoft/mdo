# 历史读取的准确最终提示

基线 `44fdc41`。会话元数据已载入、历史同步失败时，session-load-notice 忽略历史错误，仍描述“正在读取”或“正在检查”。首次历史永久失败还同时显示顶部提示、时间线错误卡和两个重试入口。

现在顶部提示使用实际历史错误：短暂失败预算耗尽后说明对话记录暂不可读，保留已显示消息与草稿；永久错误保留准确原因。元数据错误仍优先说明并重读元数据。主页面显式让该提示负责历史读取错误及重试，时间线保留独立使用时原有默认行为；模型运行错误和旧消息分页按钮不受影响。中、英、俄三种语言补齐暂不可读说明，没有隐藏真正失败。

## 确定性验证

新增三项 notice 检查先有一项通过、两项失败，再全部通过：同步耗尽不能描述成仍在检查，冷历史拒绝显示精确原因，元数据错误保留优先级。重试历史的正常加载状态、恢复隐藏提示和输入焦点也验证通过。

以下十个实际文件共 63 项 Node 检查通过；四项前端契约通过。首次命令误列不存在的 test_session_reads.mjs，Node 没有执行该文件；随后使用实际 test_session_read_recovery.mjs 单独通过，计数不包含不存在的文件。

```powershell
node --test tests/test_session_load_notice.mjs tests/test_timeline_refresh_ownership.mjs tests/test_timeline_read_deadline.mjs tests/test_live_timeline.mjs tests/test_conversation_snapshot.mjs tests/test_session_composer_focus.mjs tests/test_model_errors.mjs
node --test tests/test_session_read_recovery.mjs tests/test_timeline_history_i18n.mjs tests/test_session_action_focus.mjs
```

## 真实打包页面

manual_timeline_deadline_qa.py 新增 --deny-cold，用回环代理拒绝首次 conversation GET，正式重试前一直保留拒绝。控制端点仅在代理存在，未注入产品 JS。两个独立便携 Home 分别验证旧候选和新候选。

baseline.png 与 baseline-browser.json 记录旧页面：顶部仍说正在读取，时间线另显示无权限卡，两个重试按钮。final-denial.png 与 browser.json 记录新页面：一处明确无权限提示、一个重试入口，零时间线读取错误卡，保存草稿不变。解除代理拒绝后点击正式重试，原历史恢复、提示消失、焦点返回输入框；发送保存草稿及另一会话的新输入均成功。一次准备输入和两次有意浏览器输入各一次模型调用，共三个成功终态、零最终模型错误，空草稿和队列，无遗留运行。警告与错误日志为空。

原生夹具验证永久拒绝及其恢复。暂时读取耗尽的说明由确定性测试验证，本阶段没有对暂时故障做逐帧原生采集。基线评价脚本的 textContent 编码不可靠，文本证据使用 DOM snapshot；保留可靠的错误卡计数和输入值。详见 proof.json、checks.json 与截图。

新候选在隔离树构建，xs 3232f7b8efb02fdd6bf63a508ed1e69654d6759c 及宿主收据匹配。七个实际 HTTP 服务文件与隔离源码逐字节一致，build.json 保存候选哈希和大小。三份语言文件只引入本阶段新增键，未纳入根目录其他工作区任务的语言修改。

没有压力或高负载测试，也未替换日用程序、APK 或官网下载。Android 原生恢复和系统剪贴板等尚待独立验证；长期任务继续进行。
