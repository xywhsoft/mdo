# 自动恢复重试次数保存后回到 10

2026-11-09。源码修复，未打包或发布安装包。

## 原因与修改

`settingsPatch()` 会发送 `agent.recovery_retries`，配置层也能校验、保存及读取该值，但 `GET /api/v1/settings` 组装 Agent 数据时遗漏了该字段。自动保存后的确认读取因此丢失已保存值，页面通过 `?? 10` 回填默认值。此后修改其他设置并提交完整 Agent 配置，还可能将默认值写回用户配置。

设置查询现在从同一份受 revision 校验的有效配置中返回该字段，保留显式的 `0`；只有旧默认配置缺少此字段时才回退为 10。沿用现有 revision/ETag 和事务流程，不额外读取另一版本的配置快照。

范围仍为 0–100，默认 10；0 的含义仍是立即进入等待恢复，未改成无限重试。

## 验证

- 新增 `tests/test_settings_recovery_retries_runtime.py`。修改前在真实 `GET /settings` 中复现字段缺失；修改后通过。
- 使用现有 `.build/host/xs.exe` 在原目录编译当前应用，不复制应用或 SDK，不使用真实模型账号；只创建并清理小型测试 Home 和临时 HTTP 配置。
- 覆盖默认值、0/3/100 保存与读回、其他设置变化时再次提交完整 Agent 配置、非法类型与边界拒绝、100 和 0 的进程重启持久化、恢复默认后返回 10。
- 前端设置保存、确认读取及自动保存相关 44 项 Node 测试全部通过，新增表单保留 0/3/100 的检查。

命令：

```text
python tests/test_settings_recovery_retries_runtime.py
node --test tests/test_settings_proxy.mjs tests/test_settings_autosave.mjs tests/test_settings_state.mjs tests/test_settings_write_recovery.mjs
```
