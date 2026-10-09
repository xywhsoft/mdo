# 会话分支确认恢复验收

2026-10-09。生产修复 `db951364`，取消后侧栏补充修复 `f6f23df3`。
最终干净 Windows 候选为
`.build/conversation-acceptance/fork-recovery/final/mdo-clean.exe`，
4,529,636 字节，SHA-256
`34ed5978cb00b3ebadf8fe636495a8addc56d6c4574ea2142e4f0f05f3582a5e`。
两代修复候选均锁定 xs `f47b0de5037f458f6289483d1323dee417d99434`。

## 修复行为和范围

原先分支 POST 没有稳定编号。服务已经保存分支而确认响应丢失时，页面立即
提示“无法连接本地服务”；再次点击会创建第二个相同标题的分支。

现在每次创建意图预留一个子会话编号。确认不确定时先 GET 该编号；只有明确
不存在时才允许提交同编号、同正文的 POST。已确认的编号释放，随后有意再
创建分支仍会得到一个新编号。共享八秒单次/一分钟整体/最多六次的恢复预算，
指数退避期间不弹错误，只有最终无法确认时给出准确提示。

后端复用已有子会话的项目、来源、序列边界和标题核对已完成请求，避免重复
复制历史。已有目录不覆盖，不完整目录和冲突分别返回准确错误；写入代校验
保留。读取一个已经创建的分支不要求来源会话继续保持原修订或活动状态。
没有新增账本、数据库或常驻服务。

取消窗口、切换页面或离开文档会取消等待，不应用迟到结果、不跳转、不弹错。
即使取消时服务已创建分支，侧栏也会安静刷新，让它可见。同一页面再次确认
会核对原编号。原会话及输入草稿保持。

**未完成意图仅保留在当前页面/当前服务写入代，最多 64 项。** 后端已知编号的
核对在服务重启后仍有效，但客户端刷新或进程重启不会自动找回未完成意图；
不能宣称跨页面重新点击已去重。用户可在列表查看实际分支。本阶段没有执行
浏览器截断/清空历史，也未验证 Android WebView 或外部付费模型。

## 真正的打包页面

候选内置代码和原生 API 未改写。普通设置 API 配置可编辑回环模型，正式 HTTP
代理有限地丢弃或暂扣确认、拒绝读取；CUA 操作真正的页面。原始记录保留
正常导航/元数据 GET，因此成功后的全部 GET 不等于恢复器请求数。

|记录|候选与故障|实际结果|
|---|---|---|
|`baseline-ui.json`|干净 `61a9027a`；第一次 POST 已返回原生 201，代理丢弃确认|立即误报本地服务断连；再次点击共两次 POST、两个分支，截图 `baseline-error.jpg`|
|`drop.json`|干净 `db951364`；丢弃原生 201 的确认|约 0.547 秒后 GET 原编号，期间没有错误；一次 POST/一个分支，正常续聊；返回来源仍有原草稿|
|`cancel-before-sidebar.json`|干净 `db951364`；201 正文暂扣 12.219 秒，关闭确认窗口|没有跳转/错误，手动确认 GET 原编号、没有第二次 POST；但侧栏没刷新，据此补修|
|`cancel.json`|干净 `f6f23df3`；201 正文暂扣 8.922 秒，关闭确认窗口|侧栏立即显示已创建分支；仍停在原会话、保留草稿；迟到正文不弹错，再确认 GET 原编号|
|`refusal.json`|干净 `f6f23df3`；首次确认丢失，子会话 GET 持续 503|首轮一次 POST+五次 GET 后唯一准确提示；明确手动重试仍不可用时再六次 GET，同样只有一条最终提示；恢复后只 GET 原编号，分支续聊成功|
|`route-cancel.json`|干净 `f6f23df3`；201 正文暂扣 11 秒，进入设置|迟到结果不离开设置、不打开弹层/错误；返回草稿保持，再点回复分支只 GET 原编号|

六个页面实例共八次回环模型请求，零最终模型错误；所有原会话 `snapshot.json`
哈希保持。五个修复实例各仅一次 POST/一个分支，边界均为序列 3。
持续拒绝的首次间隔为 0.594/1.156/2.156/4.110/8.281 秒；明确手动再试的
六次读取间隔为 0.579/1.171/2.375/4.235/9.281 秒。没有负载或压力测试。

`ui-observations.json`、真实 JPG、原生/代理记录共同保留证据。第一次最终提示
由真实 AX 观察确认；随后截图时提示已正常消退，所以又明确手动重试一次，
在同一 CUA 调用内记录第二轮唯一提示和截图，未伪造提示或重复创建请求。
`final-confirmation-notice.jpg` 可见准确提示和保留的草稿。

最终候选 provenance/十个 VFS 文件逐字节核对见 `candidate.json`；初次修复
候选回执见 `initial-candidate.json`，只对应丢确认及发现侧栏问题的两个记录。
基线和修复均使用隔离 Home，不修改用户日用会话。

## 原生与静态检查

`baseline-native.json` 复现相同旧 POST 创建两个不同子会话。
`native.json` 是最终干净包真正的 xs/TCC 七组场景：稳定编号核对 201/200；
标题/边界冲突拒绝；编号/边界/ETag 拒绝；普通/不完整目录不覆盖；两个有限
同时提交各 201/200 且只有一个子会话；来源变化/归档后读取已有分支；服务
重启后旧写入代 412、新代只读取原子会话且历史哈希不变。原生夹具一次模型
请求。两个并发请求属于功能验证，没有流量或负载试验。

76 项相关 Node、156 个模块图通过，覆盖精确结果核对、已知未提交拒绝、
未知确认只读、错误所属项目/来源/边界、服务切换、取消、忽略 abort 的正文
仍在一分钟/六次结束及侧栏刷新。生产 C 修复的严格 GCC、原独立 session
运行探针和 63 项会话/API/前端/构建契约通过；最终侧栏补充只改 JS，又通过
前述 Node/模块图及最终原生与浏览器验收。

```powershell
node --test tests/test_session_fork_recovery.mjs tests/test_session_history_read.mjs tests/test_session_read_recovery.mjs tests/test_session_metadata_sync.mjs tests/test_message_replacement.mjs tests/test_decision_action.mjs tests/test_frontend_i18n.mjs
node --experimental-vm-modules tools/check_web_modules.mjs
python tests/acceptance/session-fork-recovery/check_evidence.py --exe .build/conversation-acceptance/fork-recovery/final/mdo-clean.exe
python tests/test_packed_fork_recovery.py --packed .build/conversation-acceptance/fork-recovery/final/mdo-clean.exe --report .build/fork-native-new.json
python tests/manual_session_history_qa.py --packed .build/conversation-acceptance/fork-recovery/final/mdo-clean.exe --directory .build/fork-ui-new --mode fork_refuse
```

最后一项输出真正页面 URL，用浏览器操作回复分支；`healthy` 恢复子会话读取，
`release` 提前释放暂扣正文，`stop` 保存记录并结束所拥有的进程。
未替换根目录日用程序、未安装手机、未更新官网。长期任务继续。
