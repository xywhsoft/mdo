# MCP HTTP 原因与连接恢复验收

本阶段生产改动为 `d4edd168`、`d0e84d0a`、`f189551c`、`ae74a153`。
底层源码提交为 xrt `0dc70638`，xs 同步提交为 `93ef1f61`，由 deps.lock
和源码 bundle 固定；兼容工作树保留，未混入主工作区其他未提交改动。
xwork 3.11.0 / ABI 8 扩大错误结构，宿主及 TCC SDK 已一起重建。

`xwork_error.uHttpStatus` 保存真实 MCP HTTP 状态。JSON 错误体、无效 JSON、
错误 ID 和错误响应中的 result 都不能覆盖 HTTP 失败；通知拒绝与会话
过期也保留状态。模型 HTTP 诊断仍使用模型错误结构。只有有效、匹配
请求的 HeaderMismatch 才保留原来的单次路由更新重试。

前端使用明确原因的固定提示，重新测试立即清除旧提示。新建表单、服务器
导入和原生配置允许明确的 localhost、127.0.0.1、::1 HTTP 服务；远程 HTTP、
用户信息、端口越界及各种归一化别名仍拒绝，远程服务使用 HTTPS。

等待验收还发现连接测试把整个操作限制为 request_timeout_ms，较长的
startup_timeout_ms 无法生效。原包实际在 5.01 秒超时。修复后总期限为
初始化与请求预算之和，xwork 保留各阶段的独立限制；同配置实际等待
18.953 秒后成功，等待期间无旧错误、无误报超时、无重复启动。

## 实际打包页

最终干净候选为 4,551,134 字节，较上一 MCP 原因阶段增加 1,264 字节。
SHA-256：`26268006b047c0ec371b22fb9ae2dc729ba77099976d2512c25a0385b13a1d0b`。
宿主 3,182,080 字节，比上一宿主增加 512 字节。build.json 和 report.json
保存完整来源与哈希；日用程序、APK 和官网未更新。

未改写的打包程序在独立 Home 运行，真实回环 HTTP 服务器分别返回：

| 目标 | 响应 | 实际最终提示 |
| --- | --- | --- |
| auth | 401，匹配 JSON-RPC error | 登录凭据未被接受 |
| permission | 403，JSON-RPC error 的 ID 为 null | 当前凭据无访问权限 |
| limit | 429，JSON-RPC error | 服务器暂时限流 |
| service | 503，无效 JSON | 服务器暂不可用 |

每次明确失败只显示一条面板提示，按钮恢复，没有误报成功；模拟敏感原文
没有进入操作 message 或页面。再次连接 auth 时，原生操作保持运行，超过
旧的 5 秒限时后页面仍无错误；释放响应后初始化、通知和发现正常完成。
共五次连接测试 POST：四次失败和一次用户发起的重试，没有额外启动或取消
写入。HTTP peer 记录七次请求，序列与原生操作一致。

原会话草稿完整保留；返回后一次正式模型请求、一个完整回复、一个成功
agent_done、零 error 事件。独立原生扩展门禁另有 16 次合成模型请求、零
最终模型错误，覆盖工具、委派、Skill、命令、C Agent 与 MCP 按需加载。
两者均使用回环模型夹具，没有外部服务或付费模型请求。

完整前端 952 项、161 个可达模块、124 项契约、严格 GCC、完整 API、MCP
和操作生命周期检查通过；操作检查包含 21 个分类边界及初始化预算。
库级真实 HTTP 检查覆盖 12 个失败、12 次明确重试恢复和 3 个禁止重放的
信号；正常 modern/legacy HTTP、SSE 续读及有效路由更新通过。
HTTP 原实现、旧初始化预算、旧表单和导入分别有红绿回归证据。
日志只统一行末空白；候选、运行复制件及九份 VFS 生产文件逐字节核对。
两个夹具正常 exit 0，测试页关闭。未做压力或高负载测试。

## 重现

选择新的临时目录运行第一条，再依次运行后两条。HTTP peer 与打包夹具
共同使用该目录；创建 stop 文件可正常结束两者。

```powershell
python tests/manual_extension_form_qa.py --packed <候选mdo.exe> --directory <临时目录> --duration 1200
python tests/fixtures/mcp-http-status-peer.py --directory <临时目录>
python tests/acceptance/mcp-http-status/setup_fixture.py --directory <临时目录>
```

打开 state.json 中的页面，保留草稿，进入 MCP，分别测试四个服务器。
创建 hold-auth 后再次测试 auth，等待超过五秒，确认没有旧错误；创建
release-auth 释放响应。返回原会话，正式发送草稿并确认只回复一次。
夹具保留不会超过 45 秒，配置中的初始化限时为 60 秒。

库级检查需 xrt 源码 `0dc70638` 和 xs `93ef1f61` 的隔离工作树。脚本只
读取这两棵树，在新目录编译，使用 xs 配套的 xhttp/xrt，避免旧桥接核心
缺失文件发布 API；不执行 test_xwork 的宽泛默认套件。

```powershell
python tests/acceptance/mcp-http-status/build_library_probe.py --xrt-root <xrt工作树> --xs-root <xs工作树> --directory <新目录> --cc <gcc路径>
python tests/acceptance/mcp-http-status/check_evidence.py --exe .build/conversation-acceptance/mcp-http-status/clean/mdo.exe --running-copy .build/conversation-acceptance/mcp-http-status/acceptance-final/upstream/mdo.exe
```

本阶段连接测试保持明确、有限的手动启动；不声称所有 MCP 工具调用都能
安全自动重放。启动 ACK 丢失、Android WebView/输入/后台恢复以及其他功能
仍属于长期任务，未据此宣布整体完成。
