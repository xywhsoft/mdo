# 成员的明确恢复决策

`instance.recovery({namespace,id})` 读取原生账本中尚未完成的工具调用。只允许所属会话或成员自己读取；活动运行期间拒绝查看。读取不执行模型或工具，不改变成员 revision，不重复任务。

结果包含 `available`、`state`、`revision`、`resume_required`、`last_sequence`、`recovery_token` 和 `items`。条目提供工具名、调用 ID、原始参数、效果标记、当前可用性和是否可安全重试。上限为 64 项，每项参数 16 KiB、合计 128 KiB；超出上限明确报错。`available:false` 表示尚无原生账本，不能把空列表解释成没有不确定操作。

用户选择后调用 `instance.resume`，传入当前 `expected_revision`、`message_id`、`expected_recovery_token` 和每项明确的 `retry` 或 `record_uncertain` 决策。后者保留未知结果并继续模型，不重新执行该工具。决策内容、账本尾部或当前工具描述变化时令牌失效，需要重新查看。为兼容既有调用，令牌字段目前可选；新界面应始终提供。

`instance.abandon` 直接关闭本轮，原生账本将未决工具记录为“不重试、结果未知”；不发起模型或重复进程。外部状态仍应由用户核实，不能将放弃称为回滚。

恢复读取和明确恢复使用 `PreserveRecoveryLedger` 打开已保存的系统上下文，避免构造角色时重写提示词、改变刚读取的账本序号。实际运行依旧经过常规权限、角色生命周期、工具限制和上下文刷新。

验证使用 `D:\GIT\mohists` 唯一运行目录：`tests/test_recovery.py` 和 `tests/test_pending_recovery.py`。真实终止宿主后检查模型中断和进程工具中断，覆盖稳定视图、越权拒绝、过期令牌、放弃和继续、不重放进程；不调用供应商模型。
