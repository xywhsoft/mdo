# 对话模型与权限配置恢复验收

2026-10-09。生产修复提交 `27262bad`。干净 Windows 候选
`.build/conversation-acceptance/profile-recovery/mdo-clean.exe`，4,532,060 字节，
SHA-256 `bd87caf46bf247c1977fea4ab03216823dde8ba85f94f98f68266c9dbb837ecc`，
锁定 xs `f47b0de5037f458f6289483d1323dee417d99434`。

## 修复与边界

对话中的模型、思考强度和权限原先单次 PUT 保存。原生已经保存而确认断连时，
页面立即误报本地服务不可用，并回到旧选项。后台元数据偶尔能稍后纠正界面，
但不能消除已经弹出的错误，也不能保证手动重试不遇到旧修订冲突。

现在复用公共八秒单次/一分钟整体/最多六次恢复。提交不确定时先读当前配置；
模型、思考强度、权限与请求一致即可确认。只有原修订仍未变化时才允许重试
同正文、同 If-Match 的 PUT。已经变化且配置不同则停止，显示准确冲突并载入
当前配置，不覆盖另一窗口的修改。明确永久拒绝不重试，写入代/设备切换仍受
原有保护。没有新增服务端账本、持久化文件或通用“重试所有写入”开关。

等待同页 Agent 运行时也包含在请求期限内；没有实际发出 PUT 时，不宣称提交
结果未知。取消等待、离开对话或销毁配置视图会释放操作，不应用迟到结果，
不会在别的页面弹出后台保存错误。较新元数据不会被旧的确认响应覆盖。
最终读取失败时保留最后已知配置并显示一条“暂未确认”提示，可重新选择确认。
输入正文不清除。活跃运行的后续配置仍走原有独立草稿路径，未改成中途改写。

这是**对话配置**恢复；通用设置页所有类别的保存不属于本阶段验证范围。
模型 HTTP 为回环夹具，mdo 原生/xs/TCC、设置接口和浏览器页面真实。
没有消耗线上模型额度，未安装 Android 或更新日用程序/官网。

## 实际页面与原生结果

用普通模型设置 API 添加两个可编辑配置。代理有限丢确认、暂扣正文或拒绝
元数据读取，包内源码及原生 API 没有修改。CUA 选择真正的模型菜单、发送
原输入、切换设置页面及选择权限；JSON 保存代理和原生的实际结果。

|记录|故障与结果|
|---|---|
|`baseline.json`，干净 `f6f23df3`|PUT 原生 200 的确认丢失；真实页面提示“无法连接本地服务，请确认 mdo 仍在运行。”，回到旧模型。元数据随后纠正选择；没有把这个后台纠正误称为保存失败或重试 412 的实际 UI 复现|
|`drop.json`，干净修复包|相同确认丢失，等待中无错误，GET 确认模型/思考强度，控件释放、草稿保持；正式续聊成功。一次 PUT，修订 1→2|
|`refusal.json`，干净修复包|PUT 200 后确认丢失，读取持续 503；退避结束只有一条配置未确认提示，控件释放、草稿保持。恢复后手动选择先得到原生 412，再 GET 确认原配置，续聊成功；修订仍为 2，没有再次写入|
|`route.json`，干净修复包|PUT 200 正文暂扣 18.438 秒，立即进入设置；迟到响应不导航、不弹配置错误。返回显示已保存的模型与原草稿，随后正式改成只读，第二次不同正文的 PUT 200；修订 1→2→3，历史 snapshot 哈希不变|
|`wire.json`，同一干净修复包|补充验证不同的实际上游模型标识：丢确认后只一次 PUT，原输入保持、续聊成功；原生回调依次收到 `ornith-1.5-35b`、`profile-alternative`，修订 1→2|

初次夹具复制了模型的 `wire_model`，只验证不同本地配置，前三个成功运行的
实际请求仍是同一个标识。检查证据发现这个不足后，修正夹具字段并增加最后
一个新实例，原始记录保持，不把改名当作实际模型切换的证明。

五个页面实例共八次回环模型请求、零最终模型错误。基线和只改配置的页面
snapshot 哈希不变；三份续聊记录会追加历史，哈希理应变化，真实 DOM 中原
输入和旧回复仍在，原生各有两次 `model_done`/`agent_done`。

元数据同步器与配置恢复器各自有读取预算。持续拒绝记录共十一条 503，含
后台元数据读取；不能把这些 GET 都算成一次配置恢复的六次上限，也没有
宣称浏览器传输一共只有六次。单次配置恢复的写/读共享期限和六次上限由
可控时钟检查直接证明。本阶段没有负载或压力测试。

`ui-observations.json` 为真实页面只读观察，JPG 为实际截图。瞬时“已完成”或
成功提示消退后，检查当前页面和真实模型完成事件，没有因观察超时重新
执行用户输入。设置页曾显示独立的更新检查失败说明，本阶段未将其计为
配置错误，也未宣称该外部更新检查已经修复；它继续列入后续检查范围。

## 检查和复现

115 项相关 Node、157 个前端模块图、63 项会话/API/前端/构建契约通过。
新检查覆盖失去确认后的只读核对、原 ETag 安全重试、另一窗口冲突、永久
拒绝、明确未提交与结果未知、忽略 abort 的正文仍在一分钟/六次结束、运行时
等待期间取消/期限、页面切换、较新元数据和销毁视图。三语错误覆盖通过。
旧浏览器夹具补齐真实修订/ETag 并等待异步提交；本阶段没有执行该 toy 页面，
实际页面和 Node 视图检查提供功能证据。

`candidate.json` 验证干净回执、候选 SHA-256 与八个包内生产文件逐字节一致；
`baseline-candidate.json` 标识基线。此阶段没有修改 C 源码或更换宿主。

```powershell
node --test tests/test_session_profile_recovery.mjs tests/test_composer_agent_defaults.mjs tests/test_frontend_session_runtime.mjs tests/test_session_fork_recovery.mjs tests/test_session_history_read.mjs tests/test_session_read_recovery.mjs tests/test_session_metadata_sync.mjs tests/test_message_replacement.mjs tests/test_decision_action.mjs tests/test_frontend_i18n.mjs
node --experimental-vm-modules tools/check_web_modules.mjs
python -m unittest tests.test_session_contract tests.test_api_contract tests.test_frontend_contract tests.test_build_contract
python tests/acceptance/session-profile-recovery/check_evidence.py --exe .build/conversation-acceptance/profile-recovery/mdo-clean.exe
python tests/manual_session_history_qa.py --packed .build/conversation-acceptance/profile-recovery/mdo-clean.exe --directory .build/profile-ui-new --mode profile_refuse
```

最后一项打印真正页面 URL，用浏览器选择 `Profile alternative`；`healthy` 恢复
读取，`release` 提前释放暂扣正文，`stop` 保存记录并结束所属进程。
长期任务继续，通用设置保存、模型配置拒绝的准确分类和其余功能仍需检查。
