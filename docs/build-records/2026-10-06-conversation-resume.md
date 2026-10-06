# AI 回复语言与中断恢复更新

本阶段实现提交：`c45f460`、`fa899f6`、`3e384ae`。
设置 → Agent 增加 AI 回复语言：跟随提问语言、中文、英文、俄文及手动输入。
偏好在每次打开运行时应用到主 Agent 和子 Agent，包括已有会话；保留原 Agent、Skill
和创建时的自定义指令。明确的翻译或语言请求仍按用户要求处理。

未完成回复可以继续，也可以直接发送新消息。直接发送和“结束本次回复”只为未决工具
追加结果不确定记录，不自动重跑工具；恢复授权绑定会话账本序列。成功重新检查空闲
会话后，连续失败的恢复请求可以再次提交。具体交互见 `../chat-interactions.md`。

## 依赖来源

- xwork：xrt `284a6a74` 修复普通聊天恢复触发写入验证；`7f31cbb4` 保留现有 SDK 的目录挂载支持。
- xllm-session：xrt `578b3922` 排除系统上下文对会话尾部的干扰，并允许在稳定的待工具状态更新上下文。
- xs：`b5ab39c926c282e8912d1488eed0be55ce16a360`，包含上述两个修复；依赖及生产文件散列已更新到 `deps.lock`。
- 上游与 SDK 的 xwork、xllm-session 生产文件散列一致，按仓库定义的 LF 标准化规则验证。

本机使用干净的 `D:\GIT\xrt-mdo-resume` 和 `D:\GIT\xserver-mdo-resume` 工作树，
分支均为 `codex/mdo-conversation-resume`。原仓库的其他未提交工作没有被覆盖。
其他机器应检出 `deps.lock` 指定的 xs revision，通过 `--xserver-root` 指定该目录。

## 已完成验证

- 前端 398 项 Node 测试、20 项前端合同、22 项 API 合同、4 项运行管理合同；116 个模块解析通过。
- Windows mdo unity：GCC C11 `-Wall -Wextra -Werror -fsyntax-only` 通过。
- 真实 xs/TCC/HTTP：语言保存和读取、重启回填、已有会话切换语言、连续失败后继续、新消息接续、
  未决工具收尾、明确结束未决回复、过期恢复拒绝；`test_conversation_resume_runtime.py` 通过。
- `test_agent_runtime.py`、`test_config_runtime.py`、`test_interrupt_runtime.py`、`test_session_runtime.py` 通过。
- 打包版队列启动恢复与内置 Agent/工具目录验证通过。
- xllm-session 的上下文尾部、状态机、工具配对在 Windows、Linux 均通过；Windows 不可变快照测试通过。
- xwork 的恢复效果和原有 Agent 写入验证回归在 Windows、Linux 均通过。
  Linux 单元探针采用 GCC C11 Debug（`-O0 -Wall -Wextra -Werror`）；Android 本体采用原发布配置编译。
- 390 px 实际打包版设置页面：自动保存、重开回填、手动语言字节限制和无横向溢出通过。
  本机截图：`.build/qa-reply-language-mobile.png`。
- Android ARM64 APK 编译、v2/v3 签名和 16 KiB zip 对齐校验通过。

未运行压力或高负载测试。完整 `test_api_runtime.py` 在旧记忆断言处停止：预期空列表，
当前返回内置 `MEMORY.md`；因此本记录不宣称完整发布门禁通过。

## 发布文件

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `mdo.exe` | 4185627 | `197f550049420c7d2dc99739a053b4e0570e1379e59338c97f0e5f66a0412b6d` |
| `mdo-arm64-v8a.apk` | 3810472 | `eb0c2a4e22e376615c22491c67a567b28877f8811816701dab3fa0a058e9b130` |

根目录 `xs.exe` 和 `xsw.exe` 已替换为本阶段锁定源码构建的 mdo 精简宿主。
Windows 原窗口正常关闭后覆盖发布文件，便携数据未改动。

手机首次连接时确认两个问题会话的历史仍在，均没有未决工具调用；末尾分别是
`model request failed` 和 `too many consecutive tool failures`。随后 USB 连接断开，
系统和 adb 均未再发现设备，因此尚未安装此 APK，也未完成两个原会话的真机恢复复测。
重新连接后使用 `adb install -r mdo-arm64-v8a.apk` 覆盖安装，保留登录和会话数据。
