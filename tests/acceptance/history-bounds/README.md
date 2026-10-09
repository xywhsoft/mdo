# 历史范围及取消读取验收

生产提交：`95066ef94a929de3e834a480a186931f1c8bb65f`。本阶段没有更新日用程序、Android 或官网，没有压力或高负载测试。

## 修复及门禁

- 正文加载到历史起点时同步摘要前缀，删除已经没有内容的“更早摘要”入口。
- 摘要与正文的已知前缀只向更早推进，乱序返回不能倒退已加载范围。
- 新的完整快照取消旧正文、摘要及跳转读取；旧历史返回不能进入新历史。
- 正常取消读取不显示错误，真正的最终读取拒绝仍显示准确提示。

五项新增行为检查在修复前全部失败，修复后全部通过；44 项相关 Node、919 项完整前端及 124 项契约通过。未修改原生 API，此次未重复全量原生运行门禁。干净包实际完整历史分组检查通过：五个回复、十对工具、长正文、继续及增量页。

```powershell
node --test tests/test_timeline_history_bounds.mjs tests/test_conversation_snapshot.mjs tests/test_conversation_history.mjs tests/test_timeline_read_deadline.mjs tests/test_timeline_refresh_ownership.mjs
python -c "import sys;sys.path.insert(0,'tools');import qa_release;qa_release.frontend_checks()"
python -m unittest discover -s tests -p 'test_*contract.py'
python tests/test_conversation_groups_runtime.py --packed .build/conversation-acceptance/history-bounds/clean/mdo.exe
```

## 干净包与完整页面

候选位于 `.build/conversation-acceptance/history-bounds/clean/mdo.exe`，4,539,892 字节，比上一候选增加 332 字节。SHA-256：`a1f8da520a0f2c40dfc6cc0215bfc28bc0a36a45feee7e5e9f83a05c1e7b0dee`；xs 为 `f47b0de5037f458f6289483d1323dee417d99434`。构建收据 `build.json` 标记工作树干净；实际启动的复制件与候选相同，两份变更的生产 JS 与 VFS 内嵌文件逐字节相同。

`manual_timeline_history_qa.py` 只在测试进程中代理真实原生 HTTP，使用独立 Home、五次回环模型请求；不使用用户账号或外部模型。代理刻意不提供 WebSocket，页面采用产品已有的安静 HTTP 同步路径。暂扣的正文是原生接口已经生成的响应，不是替换的假历史。生产包内没有此测试控制文件或路由，原生 `/__fixture/navigation-cancel` 返回 404。

完整应用页面依次验证：

1. `short-one`、`short-two` 各正常发送一次，刷新后正文和两个摘要都完整，原生索引 `has_more=false`，更早摘要隐藏。
2. `history-one`、`history-two` 各正常发送一次，长回复使刷新后只加载最新页。保存中文及表情草稿，再触发“加载更早的对话”的原生分页。
3. 代理扣留旧历史 `before=16` 的成功正文。`clear_when_held.py` 只清空这个隔离测试会话；页面轮询收到新的完整历史后取消原读取。晚到的旧正文发送时连接已经关闭，草稿保留、旧正文未混入、没有取消或加载失败提示。
4. 同一页面正常发送保留的草稿，模型得到一个用户输入，正常结束一次，零最终错误。原生新历史仅包含清空标记与这次新回合，旧四个问题没有重新出现。

测试控制器在按钮自动滚入视口时触发真实预取；历史随后被清空，原按钮移除，自动化 click 等待因此失去目标。这是刻意替换期间的测试定位器竞态，不是产品错误；`held-body.json`、新历史、旧连接关闭和完整页面观察共同核对了实际路径。

以下文件保留真实原生响应：`short-turns.json`、`long-before.json`、`held-body.json`、`session-before.json`、`clear-response.json`、`cleared-conversation.json`、`followup-conversation.json`、`followup-full.json`、`followup-turns.json`。`*-ui.json` 和 PNG 为浏览器 DOM 与页面观察；`calls.json`、`requests.json` 为五次实际回环 HTTP 模型请求，用户消息数依次为 1、2、3、4、1。

另一个页面通过代理加载同一候选的生产导航模块，用会抛出 `AbortError` 的合成回调检查两个导航 catch：跳转和摘要读取均零 toast；再改为普通拒绝，准确提示一次。该组件检查与上面的真实完整应用验收分开，不能当成外部服务或手机实测。

```powershell
python tests/manual_timeline_history_qa.py --packed .build/conversation-acceptance/history-bounds/clean/mdo.exe --directory .build/conversation-acceptance/history-bounds/packed-ui-new --duration 1500
# 按脚本输出 URL 打开完整页面，先发送四个上述提示并刷新、填写草稿。
python tests/acceptance/history-bounds/clear_when_held.py .build/conversation-acceptance/history-bounds/packed-ui-new
# 在页面触发“加载更早的对话”；核查新历史和草稿后创建 release-before 文件。
# 最后发送草稿；组件页是同端口 /__fixture/navigation-cancel。
python tests/acceptance/history-bounds/check_evidence.py --exe .build/conversation-acceptance/history-bounds/clean/mdo.exe --running-copy .build/conversation-acceptance/history-bounds/packed-ui/upstream/mdo.exe
```

复查脚本独立核对原生历史代与事件范围、五次模型请求、页面和组件记录、候选 SHA 与生产源码。重复实验需要新隔离目录；测试会覆盖本目录的证据文件，应与已有 Git 记录比较。关闭页面并创建代理目录的 `stop` 后，代理及其子服务正常退出。长期任务仍需继续其他功能及平台实测。
