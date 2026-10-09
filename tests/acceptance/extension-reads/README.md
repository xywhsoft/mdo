# 扩展管理读取恢复验收

生产提交：`b242ca6611d2c0a9b082a89a49658005e873594e`。原生服务与模型均为隔离回环环境，未更新日用程序、手机或官网；未做压力或高负载测试。

## 先复现，再修复

旧候选 `95066ef9` 的实际页面复现两处问题：初始化完成后刷新 Agent，一次 503 就停留在错误状态；读取默认 Agent 的编辑内容时返回聊天，晚到的原生响应仍打开编辑窗口。`baseline-error.json`、`baseline-late.json`、对应 PNG 与 `baseline-read-records.jsonl` 保留观察；初始化期间另一次路由刷新产生的读取没有被当成正常退避验证。

新实现只处理 GET。目录、编辑、工具清单和导出复用公共八秒单次、一分钟整体、最多六次指数退避；可选插件归属信息只读一次。分类、设备、页面生命周期和新的编辑意图取消旧读取，取消安静退出；保存、删除、导入、启停和编译不经过此重放机制，取消目录读取也不会解除正在写入的忙碌状态。

九项新增行为检查、19 项相关 Node、928 项完整前端、159 个可达模块图及 124 项契约通过。纯前端改动未重复所有原生 API 门禁；补充运行了干净候选的完整扩展执行检查。

```powershell
node --test tests/test_extension_reads.mjs tests/test_request_recovery.mjs tests/test_settings_resource_loading.mjs tests/test_extension_formats.mjs
python -c "import sys;sys.path.insert(0,'tools');import qa_release;qa_release.frontend_checks()"
python -m unittest discover -s tests -p 'test_*contract.py'
python tests/test_extensions_runtime.py --packed .build/conversation-acceptance/extension-reads/clean/mdo.exe --record .build/conversation-acceptance/extension-reads/runtime.json
```

## 干净打包版实际页面

新候选为 4,541,013 字节，比上一阶段增加 1,121 字节。SHA-256：`0b0316c4676a8ae56237a4796d3f68cc7e62c46bac123651b4023d0dd8b6b95a`。`build.json` 标记工作树干净、xs `f47b0de5037f458f6289483d1323dee417d99434`；运行复制件与候选一致。三份生产 JS 与包内源码逐字节相同。

`manual_extension_read_qa.py` 的代理只故障注入一个精确 GET 路径；默认 Agent 的暂扣内容是原生接口实际产生的成功响应。写请求和模型请求不改写、不重放；候选内没有这些控制文件或注入路由。代理不提供 WebSocket，产品使用已有的安静 HTTP 同步路径。

- `catalog-retry`：完成初始化后点击刷新，两个 503 后第三次读取成功；等待和完成均无错误提示，默认 Agent 正常列出。
- `late-editor`：默认 Agent 的原生编辑内容已捕获后返回聊天。旧连接取消，释放后没有窗口或错误通知；在实际会话填写的 `EXTENSION_READ_FOLLOWUP` 保留。
- `editor-retry`：点击编辑，两次 503 后正常打开默认 Agent，零中途错误。
- `picker-retry`：新建 SubAgent 时工具清单两次 503 后正常出现。填写名称、说明、指令，只选择 `read` 并开启只读；实际保存配置包含 `tools: ["read"]` 和 `read_only: true`。
- `catalog-final`：MCP 目录连续六次 503，间隔递增，累计等待超过 15.5 秒；只出现一个面板最终提示，没有第二处 toast。主动刷新后第七次读取正常，提示消失。
- `new-intent-fast`：默认 Agent 编辑内容已捕获后立即点击新建，旧连接取消；释放后新 Agent 的 ID 和指令保留，最终正常保存。
- 返回原会话后直接发送已核对的原草稿，真实回环模型只收到一次请求、一个用户输入，成功回复一次、零最终模型错误；没有额外输入被发送。

首轮 `new-intent` 在检查代理文件时已经跨过八秒请求期限，第二次读取完成并打开旧编辑器，不算取消验收；对禁用的文件 ID 填写被拦住，未改写旧资源。重新以 `new-intent-fast` 在一次确定性 UI 操作内完成两个点击，原生暂扣、单次读取及连接取消均有记录。两轮原始记录一并保留，复查脚本只认可后者。

SubAgent 首次新建漏填说明，后端给出笼统拒绝，表单内容仍保留；补齐后成功。该观察说明必填项和客户端校验仍需改进，不能把本阶段当成全生态体验已经完成。额外的一次无说明原生探针没有创建资源，因未保留明确响应，不作为验收证据。

`read-records.jsonl` 保留实际时间、尝试编号与故障/转发结果；`held-*.json` 保留原生成功正文；`*-ui.json` 与 PNG 为浏览器页面记录；`subagent.json`、`new-agent.json` 为原生保存结果；`calls.json`、`requests.json`、`followup-events.json` 为正式续聊。

`runtime.json` 是单独的原生 HTTP/TCC 扩展运行检查：自定义 C 工具、主/子 Agent 工具组合、委派、代码提示词、Skill、命令及 MCP 按需工具加载；共 16 次合成 HTTP 模型请求、零最终错误，实际 stdio 顺序为 initialize、initialized、tools/list、tools/call。与完整页面的一次模型请求分开计数，不能替代外部服务、HTTP MCP、Android WebView 或可信输入法验收。

```powershell
python tests/manual_extension_read_qa.py --packed .build/conversation-acceptance/extension-reads/clean/mdo.exe --directory .build/conversation-acceptance/extension-reads/packed-ui-new --duration 1500
```

控制文件 `control.json` 示例：`{"path":"/api/v1/extensions/agents","phase":"catalog-retry","failures":2}`。写临时文件后原子替换，避免代理读到半个 JSON。`hold:true` 暂扣该阶段第一次 GET，创建 `release-<phase>` 后释放，最多 45 秒。反复实验需新隔离目录，结束时关闭临时页、创建 `stop`，代理与子服务正常退出。

```powershell
python tests/acceptance/extension-reads/check_evidence.py --exe .build/conversation-acceptance/extension-reads/clean/mdo.exe --running-copy .build/conversation-acceptance/extension-reads/packed-ui/upstream/mdo.exe
```

独立复查核对旧版复现、实际重试及退避、最终提示、旧连接取消、新表单与保存内容、正常续聊、原生扩展结果和候选字节。长期任务继续表单校验和其他交互路径。
