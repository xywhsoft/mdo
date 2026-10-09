# 后台操作读取恢复验收

生产提交 `3940904af65f4804c352d4ecda895d87a452e722`，干净候选
4,546,409 字节；SHA-256
`27e93a6f4c8968566c41b50be544249eb603de665c88440ceecade1ec25ffa13`。
比上阶段增加 2,700 字节。xs/xrt 与上一阶段兼容构建相同，完整来源见 `build.json`。

## 场景与证据

原版 b2029627 对同一原生操作的第一条 GET 注入 503，页面立即显示错误，
但绕过故障代理读取的原生结果已经连接成功并发现一个工具。
`baseline-error.png`、`baseline-native-operation.json` 和原版读写记录展示该矛盾。
原版十项受限 Node 检查为一项通过、八项失败、一项取消用例超时；
最早未加测试时限的取消实验挂起，确认仅属本次测试子进程后终止，
随后使用每用例 1.5 秒保护得到这里保存的完整基线。修复后的十二项操作行为检查均通过。

实际浏览器访问未改写的打包程序，使用独立 Home。原生配置两个 stdio MCP：
本仓库 `tests/fixtures/mcp-2025.py` 与不存在的测试程序；无需外部账号或服务。
`tests/manual_extension_read_qa.py` 仅为选定 GET 注入至多六次 503 或暂扣一条原生正文；
所有写入和模型请求直接透传，写记录不保留请求正文、凭据或写入令牌。

| 场景 | 实际观察 |
| --- | --- |
| MCP 状态前两次 GET 503 | 第三次读取同一 ID 成功；没有面板错误，卡片就绪 |
| 成功原生正文暂扣，返回聊天 | 仅一条 GET，连接被取消；无迟到提示，原草稿保留 |
| 返回 MCP 后重新读取 | 原操作 ID 的纯 GET；没有再次启动 POST |
| 六次 GET 503 耗尽 | 一条“状态暂未确认”提示与重新读取入口，等待逐渐增长 |
| 主动重新读取 | 第七条 GET 确认原任务成功；提示消失，没有新任务 |
| 程序不存在 | 原生终止失败；一次连接失败提示，没有成功提示或非终止读取入口 |
| C 模块状态前两次 GET 503 | 同一 ID 第三次读取成功，重新编译按钮恢复 |
| SubAgent 漏填说明 | 一条可见的字段旁提示，焦点与 aria-describedby 关联，底部无重复提示 |
| 填写说明 | 校验提示及 ARIA 错误状态清除；无扩展 PUT |
| 原会话继续 | 草稿保留，正式模型请求一次，回复一次，一次成功 agent_done，零 error 事件 |

原生 MCP 启动三次、失败 MCP 一次、C 模块重编译一次。
两个“重新读取”没有新增 POST，也没有取消原生任务的 DELETE。
`read-records.jsonl` 与 `writes.jsonl` 可交叉复查，`native-operations.json`
是从上游直接读取的五个真实终止结果，不是代理合成的成功响应。

取消一次性编辑草稿触发原生 `window.confirm` 后，浏览器自动控制无法处理该标签页；
未把该步骤记为成功或改用注入脚本绕过确认。原域名的两个发送点击未产生请求。
随后独立的 localhost 标签页访问同一实际服务和会话，初始化后一次正式发送成功；
其 URL、界面、模型请求及事件单独记录。该环境限制不能证明桌面原生确认框或
Android WebView 已验证。一次性未保存草稿没有写入文件。

隔离的原版、候选进程均通过 stop 文件正常结束，exit 0。
独立的原生扩展执行门禁另有 16 次合成模型请求、零最终模型错误，覆盖
C 工具、委派、Skill、命令、C Agent 和 MCP 按需加载；见 `runtime.json`。
完整前端 946 项、161 可达模块与 124 契约检查通过。

## 重现与核对

从仓库根目录执行，选择一个尚不存在的一次性目录：

```powershell
python tests/manual_extension_read_qa.py --packed <候选mdo.exe> --directory <一次性目录> --duration 1500
python tests/acceptance/resource-operations/setup_fixture.py --directory <一次性目录>
```

根据目录中的实际 state.json 打开页面，设置 → MCP。
原子替换 control.json 为 `{"phase":"operation-retry","operation_reads":true,"failures":2}`，
点击 local 的测试连接一次；`failures:6` 验证耗尽后纯读取恢复。
`hold:true` 暂扣第一条真实正文，八秒内返回聊天后创建 release-同名phase 文件。
不通过故障代理补发人工读取来计数；原生核对使用 state.json 的 upstream_port。
结束创建 stop 文件，确认夹具退出成功。

```powershell
python tests/acceptance/resource-operations/check_evidence.py --exe .build/conversation-acceptance/resource-operations/clean/mdo.exe --running-copy .build/conversation-acceptance/resource-operations/packed-ui/upstream/mdo.exe
```

独立核对读写次数、ID、逐渐增长的等待、原生终止状态、实际字段位置与
对话结果，并比对干净候选、运行复制件和八份 VFS 生产文件。没有把夹具打入程序。

本阶段没有重试启动 POST、处理其 ACK 丢失、实测 Android 或重复外部 HTTP MCP。
原生 MCP 失败原因仍比较笼统，后续需保留安全且准确的阶段信息。
日用程序、APK、官网均未更新。没有压力或高负载测试，长期目标仍在进行。
