# 历史读取与分叉准备验收

2026-10-09。生产修复提交 `61a9027a`，干净 Windows 候选
`.build/conversation-acceptance/history-preparation/mdo-clean.exe`。
候选 SHA-256 为 `65ffd225a40b1457f1c5a939f79d9486597632d90606c65a195ce904cfb8ae5f`，
锁定 xs `f47b0de5037f458f6289483d1323dee417d99434`。

## 问题和行为

回复分叉、会话菜单分叉/截断先读取 `/history`。原先直接 GET，第一次临时失败
立即报错；正文挂起没有期限，菜单的迟到结果可能在其他页面打开弹层。

现在读取复用公共恢复器：八秒单次限制、一分钟总期限、最多六次指数退避。
读取结果必须属于原会话，修订号、序列边界和 ETag 必须一致。
会话切换、进入设置/计划任务、新读取意图和页面离开会取消旧准备。
取消不报错，也不允许旧响应打开弹层或继续分叉。
编辑/重试已有公共期限，传入 signal 后只读一次，避免叠加恢复预算。

本次只修复读取准备。分叉 POST 仍单次提交，其确认丢失尚未在这里解决；
不能宣称全部分叉故障均已恢复。截断弹层共用该读取，但没有通过浏览器
执行清空/截断历史。原始会话在以下验收中逐字节不变。

## 实际打包页面

程序内置源文件未修改。通过普通设置 API 配置可编辑的回环模型，正式 HTTP
代理仅制造有限读取故障；CUA 点击真实页面，不用代理调用产品操作。

|记录|历史读取|结果|
|---|---|---|
|`baseline.json`，干净 `b0a7722f` 旧包|一次 503|立即出现提示，零分支|
|`recover.json`，修复包|503、503、200，间隔 0.594/1.140 秒|期间无错误提示，一次 POST/一个分支；原草稿保留，分支续聊成功|
|`cancel.json`，修复包|200 响应体暂扣 12.266 秒；此时进入设置|迟到正文无弹层/错误/分支；返回后草稿保留，再读 200 正常弹层、创建一个分支|
|`refusal.json`，修复包|六次 503，间隔 0.562/1.203/2.172/4.422/9.438 秒|17.797 秒后一条准确最终提示，按钮释放、草稿保留；手动重试 200，只创建一个分支并正常续聊|

四个验收实例共六次回环模型请求，零最终模型错误。
成功分支均绑定原会话并保留到序列 3，原 `snapshot.json` 哈希不变。
`ui-observations.json` 记录实际 DOM 观察；JPG 为真实页面截图。
其中设置取消证明逻辑取消和不应用旧结果，没有把服务端一次写成功误称为
TCP 连接已关闭。真实一分钟挂起预算由下述可控时钟检查覆盖，本次浏览器
没有再次等待整分钟。

## 检查与复现

```powershell
node --test tests/test_session_history_read.mjs tests/test_message_replacement.mjs tests/test_session_read_recovery.mjs tests/test_session_metadata_sync.mjs tests/test_decision_action.mjs tests/test_frontend_i18n.mjs
node --experimental-vm-modules tools/check_web_modules.mjs
python tests/acceptance/session-history-recovery/check_evidence.py --exe .build/conversation-acceptance/history-preparation/mdo-clean.exe
python tests/manual_session_history_qa.py --packed .build/conversation-acceptance/history-preparation/mdo-clean.exe --directory .build/history-manual-new --mode recover
```

第一组 62 项 Node 通过，包括忽略 abort 的正文仍六次/一分钟结束、永久 403
不重试、错误历史不应用、切换路由与页生命周期取消。155 个模块图、16 项
构建契约和 20 项前端契约通过。更新两项过时的前端断言以匹配已有公共读取
signal 与决定提交委托，并未为通过检查削弱生产行为。

复现程序输出 URL，用浏览器点击回复分叉；在隔离目录创建 `stop` 保存结果。
其他模式为 `hold`（第一次正文挂起；`release` 释放）、`refuse`（创建
`healthy` 恢复正常读取）。每个实例最多运行九分钟后自动停止。
检查器验证真实 VFS 中七个生产 JS 文件与源码逐字节相同，同时核对记录中
请求数、退避间隔、分支归属、历史边界、原会话哈希和最终错误数。

没有访问真实模型服务、执行压力/高负载测试或更新日用程序、手机和官网。
Android 真机、原生输入法以及分叉写入确认恢复继续验证。
