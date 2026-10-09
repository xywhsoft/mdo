# 扩展编辑必填与格式提示验收

生产提交：`b202962766c6fc151a12a4e64992610f175de4b7`。只使用隔离 Home、回环原生服务和模型，没有修改日用程序、手机或官网，也没有压力或高负载测试。

上一阶段实际创建 SubAgent 时漏填说明，只得到笼统英文后端拒绝；观察见相邻 `extension-reads/README.md`。本阶段补上常用表单的必填标记和本地校验，失败聚焦字段并保留内容；主 Agent 的默认提示词和 Skill 空正文仍允许，高级源文件仍由原生服务决定是否合法，不增加第二套 YAML 引擎。

六项新增行为检查、20 项相关 Node、934 项完整前端、160 个可达模块图和 124 项契约通过。没有重复全部原生 API 门禁；干净候选的扩展执行门禁另外通过，包含自定义 C 工具、主/子 Agent、委派、Skill、命令及 MCP 按需加载，共 16 次合成 HTTP 模型请求、零最终模型错误。

干净候选为 4,543,709 字节，增加 2,696 字节。SHA-256：`d6b49316ef607010112b653fdb332fa755bc0ffd98348c991572b8d31171c4ee`。候选、运行复制件和六份改动的 VFS 文件逐字节一致；`build.json` 确认工作树干净，xs 为 `f47b0de5037f458f6289483d1323dee417d99434`。

`manual_extension_form_qa.py` 只被动记录写请求的方法、路径、状态和时间，不改写、重试或注入表单数据；凭据与内容不进入写记录。代理不提供 WebSocket，页面使用产品已有的 HTTP 同步路径。所有资源和凭据占位符均为测试数据。

实际打包版页面验证：

- SubAgent 漏填说明和仅填空格都显示“请填写说明”，说明栏得到焦点并标记错误，ID、名称和指令不丢；这些失败没有扩展 PUT。补齐后只保存一次，只读和 `tools: ["read"]` 正确保留。
- 主 Agent 不填提示词、Skill 不填正文均能保存；命令只要求指令，不强迫说明。保留命令名 `new` 与空指令在本地定位，改成 `qa-review` 并补齐指令后正常保存。
- MCP 分别检查缺少程序、错误 JSON、非字符串参数数组、非 HTTPS 地址和空凭据，逐项定位到字段，无错误 toast；JSON 异常不回显输入的凭据片段。
- 改为 HTTP 时，隐藏的 stdio 参数仍是无效 `[1]`，但隐藏字段禁用、不参与校验，正常保存；源文件模式的 JSON 错误保留原文并聚焦源文件，改正后保存；再转回 stdio 也可保存。
- HTTP 与 stdio 测试配置始终停用，未启动程序或访问 `example.invalid`。这项只证明编辑和原生保存，不当成 HTTP MCP 联机验收。
- 扩展写入总共七次且全部成功：SubAgent、Agent、Skill、命令各一次，MCP 新建和两次更新。错误输入没有额外写入、没有重放。
- 原会话的 `EXTENSION_FORM_FOLLOWUP` 在设置往返后保持，正常发送一次并成功回复一次，原生五条运行事件无错误；页面模型只调用一次。与独立扩展执行门禁的 16 次模型请求分开计数。

`writes.jsonl` 为实际服务写记录，`*-ready.json` 与错误场景 JSON 为 DOM 字段、焦点和提示，五类原生响应为实际保存结果，`followup-*.json`、模型记录与截图为原会话续聊。读取运行事件时的一次手动 `limit=100` 探针被合法拒绝，按接口上限改为 32 后取得完整五条事件；不属于产品界面失败或模型错误。

```powershell
node --test tests/test_extension_validation.mjs tests/test_extension_formats.mjs tests/test_extension_reads.mjs
python -c "import sys;sys.path.insert(0,'tools');import qa_release;qa_release.frontend_checks()"
python -m unittest discover -s tests -p 'test_*contract.py'
python tests/test_extensions_runtime.py --packed .build/conversation-acceptance/extension-forms/clean/mdo.exe --record .build/conversation-acceptance/extension-forms/runtime.json
python tests/acceptance/extension-forms/check_evidence.py --exe .build/conversation-acceptance/extension-forms/clean/mdo.exe --running-copy .build/conversation-acceptance/extension-forms/packed-ui/upstream/mdo.exe
```

实际截图也显示长弹窗的底部状态文字可能滚出视野，字段旁的就近提示仍需改善。Android WebView、实际输入法、外部 MCP 服务、运行操作的读取恢复和其他管理流程继续验证；本阶段不表示全项目已经稳定。
