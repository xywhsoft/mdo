# 持久成员的可见会话

`instance.history` 为 Application SDK 的通用成员会话接口，主窗口、插件和 C 扩展共用。它读取实际原生执行事件，不调用模型，也不会恢复、重放工具或将简短结果拼成会话。C 入口为 `mdoInstanceHistory()`。

```json
{"namespace":"my-team","id":"researcher","after":0,"limit":32}
```

结果包含 `items`、`next_cursor`、`has_more`、`pending_tail`、`available`、`complete_from_creation` 和当前原生成员 `state`。`after` 必须是上一页返回的提交游标；`limit` 为 1～64，一页最多约 128 KiB。循环读取到 `has_more:false` 获得当前已提交记录；执行中继续以 `next_cursor` 读取新增内容。

每条记录含 `message_id`、`kind`、`run_id`、`source_event_id`、`text`、`text_offset`、`text_bytes`、`text_more`、`source` 和 `sender`，工具记录还包含工具名、调用 ID 与可用的产物引用。文本以不超过 8 KiB 的完整 UTF-8 片段保存，没有把 8 KiB 的成员结果限制当成完整正文。连续片段按消息、事件、运行 ID 和文本偏移合并。`source_truncated` 表示原生事件本身已经截短，界面必须保留该标记与产物引用。

来源由实际 SDK 调用上下文记录：Agent 调用为 `agent`，用户操作为 `user`，旧格式消息为 `unknown`。记录不能授予额外权限。系统提示词、隐藏推理和供应商请求体不进入这个可见记录。

已有父会话的成员，只能由同一项目的所属会话读取。成员自己只能读取本命名空间内自己的历史；创建者没有父会话的原生成员，沿用创建扩展的作用域。历史目录不可由调用参数指定。所有文件通过已锚定 Home 访问。

可见会话独立于模型上下文检查点，整理上下文不会删除它。记录逐行提交，读取不接受半行；在下一次明确启动成员时，仅恢复尚未提交的尾部。成员在本功能加入之前运行过时，`complete_from_creation:false` 明确表示较早的完整会话没有保存，不能静默拿快照或短结果替代。

开发验证在 `D:\GIT\mohists` 的唯一运行实例中进行：`python tests/test_history.py` 及重启后的 `python tests/test_history.py --check-durable`。覆盖长文本、中文与表情、实际工具输入/输出、隐藏推理排除、会话隔离、尾部中断和跨重启分页。
