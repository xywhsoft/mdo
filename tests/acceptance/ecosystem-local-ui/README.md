# 本地生态与计划任务功能验收

本阶段使用 `mdo-recovery-ack-final.exe` 的独立便携目录和回环模型。没有
修改日常会话、手机或线上服务，也没有进行压力测试。

## 页面实际操作

- 从设置表单创建 SubAgent，选中 `read`、`grep`，开启只读。创建自定义
  C 工具后，将 `user.qa-echo` 加入工具组合，切换源文件/表单仍保持选择。
  修改成功，停用后从原生 Agent 目录移除。
- 创建 Skill 和带 `$ARGUMENTS` 的命令。输入 `/qa-review src/main.c`
  后按一次 Enter 展开为指令，等待用户确认发送；草稿未进入模型。
- C 工具从内置模板按文件 ID 生成，保存后编译注册，列出 `user.qa-echo`。
  内建工具只有目录，自定义工具才有管理操作，联网工具显示会员标签。
- 创建 MCP：保存时进程未启动，点击测试后完成标准握手和工具发现，
  列表显示就绪及一个工具；断开后恢复未连接状态。
- 在 390×844 视口创建未来执行的单次计划，手动运行一次。实际模型只
  收到一次请求，执行历史显示成功及完整结果。暂停后不再安排下一次。
- 原生服务重启后，工具、停用状态、Skill、命令、草稿和计划历史仍在。
  原页面按预期显示一次重新连接提示，旧页面不能继续写入新进程。
  浏览器控制台没有错误。

保存 MCP 参数时曾手工输入无效 JSON，表单保留内容并显示解析问题；
改成有效 JSON 后正常保存。该项是输入校验检查，不是连接故障。

## 真实模型工具链

补充了 MCP 的模型调用路径：`tool_search` → `tool_load` → 实际远端工具
→ 最终回复。前两次模型请求不携带 MCP 完整工具定义，加载后只出现所选
工具，结果正确回到模型。使用 [MCP 2025-11-25 生命周期](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle)
和 [工具协议](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)
约定的本地 stdio 服务器；实际顺序为 initialize、initialized、list、call。

同一 HTTP/TCC 检查还验证自定义 C 回调、Skill 按需读取附件、真实子 Agent
委派、父子工具组合、默认 Agent 编辑、代码生成提示词及普通模式恢复。
七次主运行共有 16 次合成模型请求，零最终模型错误；网络请求没有离开
回环接口。UI 的计划任务另有一次模型请求，结果在 receipt 中记录。

```powershell
python tests/test_extensions_runtime.py --packed .build/conversation-acceptance/mdo-recovery-ack-final.exe --record .build/conversation-acceptance/ecosystem-regression.json
python tests/test_packed_module_catalog.py --packed .build/conversation-acceptance/mdo-recovery-ack-final.exe
python tests/test_ecosystem_runtime.py --host .build/host/xs.exe
python tests/test_agent_runtime.py --host .build/host/xs.exe
python tests/test_skill_runtime.py --host .build/host/xs.exe
python tests/test_mcp_runtime.py --host .build/host/xs.exe
python tests/test_schedule_runtime.py --host .build/host/xs.exe
python tests/test_schedule_executor_runtime.py --host .build/host/xs.exe
node --test tests/test_extension_formats.mjs tests/test_composer_agent_defaults.mjs tests/test_tool_preview.mjs tests/test_tool_content.mjs
```

上述运行检查全部通过，Node 检查为 18/18。网站往返使用隔离 xadmin 服务，
验证登录、发布、审核、安装、附属文件、编译失败回滚、更新、卸载保留
用户修改和启动恢复。不是对线上账号、每个第三方 MCP 或安卓真机的
最终验收。HTTP MCP 的端到端调用另行验证，本记录只证明 stdio 路径。

`acceptance.json` 保存包哈希、请求次数、协议方法、资源哈希和界面检查。
