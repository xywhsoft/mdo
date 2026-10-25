# 简单的 Agent 协作

本目录是可选扩展范例，不会自动注入默认 Agent，也不增加内置 todo 工具。

将 `tools.c` 放入自己的 mdo Home 的 `modules/tools/`，即可得到 `members` 和 `shared_work` 两个 Agent 工具。将 `coordinator.md` 放入 `agents/`，或在自己的角色配置中选择这两个工具。文件可以按普通 Agent 工具流程自行编辑、打包和分享。

## 配置角色

成员仍使用已经安装的 Agent/SubAgent 定义。提示词来源、模型、MCP、Skill、知识库、工具和权限都由该定义及现有运行机制控制。

一次普通调用：

1. `members`：`{"action":"create","args":{"id":"researcher","agent_id":"subagent.researcher"}}`
2. `members`：`{"action":"send","args":{"id":"researcher","message_id":"question-1","prompt":"调查这个问题"}}`
3. `members`：`{"action":"run","args":{"id":"researcher"}}`

返回包含本次结果。同一成员可继续 send/run，保留多轮历史。`result` 按消息 ID 读取已保存的结果，`wait` 等待另一个执行者完成；等待本身不启动模型。不同成员可以并行执行，同一成员一次处理一条消息。

普通用户会话默认使用 `session.<会话 ID>` 协作空间；成员自动继承该空间。配置模板可引用 `{{instance.id}}`、`{{namespace}}`。无用户会话的脚本可以明确传入 namespace。

## C 开发

只需公共头 `mdo/instance.h`、`mdo/work.h`；参数和结果均为普通 xvalue。通过上下文中的 Values 构造参数，调用后 Release 自己持有的参数和结果。服务的 C、RPC、前端 SDK 使用同一方法和参数契约。

```c
const mdo_values_v1* V = C->Api->Values;
xvalue* P = V->Parse("{\"id\":\"researcher\",\"message_id\":\"question-2\",\"prompt\":\"继续分析\"}");
xvalue* Result = NULL;
mdo_error_v1 Error = {0};
mdo_result Code = mdoInstanceSend(C, P, &Result, &Error);
V->Release(Result);
V->Release(P);
```

成员内调用 state/mailbox/work 时可以省略 namespace；它们共用当前协作空间。`C->MemberId` 是当前成员，`C->Namespace` 是空间，父用户会话仍通过 `C->SessionId` 标识。`sdk.services/describe` 是既有接口目录，可查看具体参数。

需要 C 主控并发时，复用已有 `operation.start` 调用 `instance.step`，完成后按既有规则释放 operation；成员身份、邮箱和结果不依赖 operation 的寿命。无需额外团队运行时。模型路由使用角色现有 model/model_options；目录隔离使用现有项目/工作区能力。

## 共享任务

`work.create` 保存标题、说明和可选的已存在依赖；`work.list` 可只列 ready 项。

`work.claim` 使用 get/list 的 revision，以及 worker/claim_id；竞争领取只有一个成功。保存返回的 claim_token，完成时调用 `work.complete` 提交 JSON result。用相同 worker/claim_id 可取回仍有效的领取，便于保存回执失败后恢复。

明确重做时用 `work.release`，之后重新领取会得到新 token。旧执行者不能覆盖新结果。`work.cancel` 使用当前 revision；已完成结果不可改写。任务不会自动过期重派，不提供额外调度或验证体系。取消共享任务只改变领取状态；正在执行的成员可另用 instance.cancel 停止。拒绝迟到结果不会回滚已经发生的文件或外部操作。

## 中断

重启后遗留的运行标记为 requires_review，不自动重发请求。读取成员 get 返回的 revision/active_message 后，可明确选择 resume 或 abandon；这两个动作也按修订保护，旧请求不能重复启动恢复。

resume 沿用原生恢复规则。对结果未知的外部工具，默认不重放；如确实决定重试，在 decisions 中指定 tool_call_id/action=retry（可能重复产生效果），或 record_uncertain（不执行，记录不确定结果）。abandon 不调用模型，也不能跳过未处理的工具决策。用户取消和审批边界不变。

结果正文超过 8 KiB 会标记截断，完整对话和产物保存在成员自己的会话目录。容量/并发达到上限时返回明确错误；不要靠重复执行创建新身份来掩盖未完成状态。
