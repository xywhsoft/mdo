# MCP 最终失败原因验收

生产提交 `fde30c2c9def67fdd9b71f00b44cf3632281ec35`。
干净 Windows 候选 4,549,870 字节，比上一阶段增加 3,461 字节，SHA-256
`22a6033ed58463a0d8e3dbb73788be9f7449314df94cd137f239db8efefde53a`。
宿主与依赖仍按原锁定版本，来源见 build.json；日用程序、APK、官网没有更新。

后台操作之前丢弃 xwork 原因，前端拼接其 message。
四个新 Node 场景在原观察器下全失败；复现只在隔离的 web 源码副本中
替换为 2ccce3bf 的观察器，其余测试和语言文件保持新版本。
这是组件基线，不冒称整包基线。baseline-observer.txt 仅移除行末空白。

现在终态原子保存受限 error_code 和明确的 http_status。
分类只使用 xwork 枚举和严格匹配的库诊断，前端由三种语言的固定词条给出建议。
服务端原文不会出现在最终提示，未知原因不猜测认证、余额或限流。
原操作读取恢复、取消与期限保持，启动 POST 没有自动重放。

## 实际打包页

未改写的 mdo.exe 在独立 Home 运行。六个配置由 setup_fixture.py 通过标准
API 创建，保存不启动程序；测试连接的五种错误均由真实进程/协议触发，
不是代理合成。夹具只转发并记录写方法、路径和状态，不保存凭据或请求正文。

| 目标 | 实际触发 | 原生分类 | 页面提示方向 |
| --- | --- | --- | --- |
| missing | 程序不存在 | mcp_start_failed | 程序路径、工作目录和环境 |
| protocol | initialize 返回不同协议版本 | mcp_protocol_version | 支持的协议版本 |
| response | 返回 JSON 数组 | mcp_invalid_response | MCP 数据格式与连接方式 |
| remote | JSON-RPC error 含模拟敏感文本 | mcp_remote_error | 初始化/工具发现拒绝，配置和服务器日志 |
| timeout | initialize 超过 400 毫秒 | mcp_timeout | 服务器状态或超时设置 |
| working | 标准初始化和工具发现 | 成功、空 error_code | 就绪，一个工具 |

每个目标一次测试连接 POST，合计六个真实原生操作。五个明确终态失败各
只显示一条面板提示；按钮恢复，没有成功提示或“重新读取状态”入口。
remote 的模拟敏感文本没有出现在操作 message 或页面中。
这些配置错误没有阻止正常服务器连接，返回原会话后草稿仍在；正式发送
一次模型请求、一次回复、一个成功 agent_done、零 error 事件。
浏览器标签页关闭，夹具以 stop 文件正常结束，exit 0。

当前候选另通过原生扩展运行门禁：16 次合成模型请求、零最终模型错误，
覆盖 C 工具、委派、Skill、命令、C Agent 和 MCP 按需加载。
完整前端 950 项、161 可达模块、124 契约、严格 GCC、操作生命周期与
完整 API 运行检查通过；分类边界另有 16 项原生检查，包含远端文本不能
伪装成 HTTP 401、非法状态及带额外原文的状态不能作为可靠 HTTP 诊断。

## 重现与核对

仓库根目录执行，选择一个不存在的临时目录：

```powershell
python tests/manual_extension_form_qa.py --packed <候选mdo.exe> --directory <一次性目录> --duration 1200
python tests/acceptance/mcp-failure-hints/setup_fixture.py --directory <一次性目录>
```

打开该目录 state.json 的 URL，在设置 → MCP 依次点击六个服务器的
测试连接，等当前终态后再测试下一个；保留草稿并返回原会话继续。
不存在的程序只是一次性夹具名称，不访问用户实际 MCP 配置。

```powershell
node --test tests/test_resource_operation_errors.mjs tests/test_resource_operation.mjs
python tests/test_operation_runtime.py --host .build/conversation-acceptance/android-page-restore/host/xs.exe
python tests/test_api_runtime.py --host .build/conversation-acceptance/android-page-restore/host/xs.exe
python tests/acceptance/mcp-failure-hints/check_evidence.py --exe .build/conversation-acceptance/mcp-failure-hints/clean/mdo.exe --running-copy .build/conversation-acceptance/mcp-failure-hints/packed-ui/upstream/mdo.exe
```

核对器交叉检查实际页面、操作终态与分类、服务器状态、POST 数量、对话
事件和请求次数，并逐字节比对候选、运行复制件及七份 VFS 生产文件。
测试 MCP 脚本没有打入程序。

明确 HTTP 状态的认证、权限、限流、服务不可用建议经过组件及纯原生分类
检查，本轮没有把它们描述成实际 HTTP MCP 服务器验收。
现有 xwork 对部分 JSON 错误体不保留 HTTP 状态，这条路径仍需底层补全。
重新测试开始时旧面板提示仍会短暂保留，后续需要清除以免误解为新失败。
Android WebView 等移动端检查仍待进行。未做压力或高负载测试，长期目标继续。
