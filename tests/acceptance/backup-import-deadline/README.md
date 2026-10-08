# 备份导入的有界等待与安静读取恢复

基线 `47f2a49`。导入控制器原来只在十秒后发送 abort，然后继续等待传输 Promise；适配器或正文读取忽略取消时，窗口可一直忙碌。上传有相同问题，观察阶段也可越过原有一分钟期限。一次临时 GET 失败会立即变成界面错误。

现在复用统一 request-recovery，由控制器自行结束等待。纯读取在原有观察期限内安静退避；轮询、正文解析、重试和等待共同使用本轮观察的剩余时间。上传保持两分钟期限，其他单次操作保持十秒期限。上传、预览、确认还原、取消及清理均不自动重放；结果未知时保留原请求、目标与摘要，后续读取同一结果。取消和销毁立即释放等待，旧上传回调及迟到的读取不能覆盖新状态。统一恢复器也在启动传输的微任务前再次检查取消，避免取消之后才发出请求。

这里的一分钟限制属于各观察阶段，不是整个导入的总期限。主模型的重试策略没有在本阶段改动。

## 确定性检查

九项新检查在基线全部失败，修复后全部通过，计数见 `deadline-checks.json`：

- 正文不响应取消时能超时、安静重试，迟到的错误身份无效。
- 销毁及显式取消释放旧读取，定时器清空，没有第二次还原。
- 还原确认超时只执行一次 POST，保留请求，查询原结果恢复。
- 取消卡住的上传后，旧回调不能重新安装上传或预览。
- 上传保持两分钟期限和清理身份，不允许盲目重新上传。
- 十二秒观察预算同时覆盖轮询、请求、正文和重试。
- 临时 503 在发布错误前恢复；传输微任务执行前取消不发送请求。

以下十三个实际测试文件共 121 项 Node 检查通过；四项相关前端契约通过。已有迟到上传检查先等待传输真正开始，再验证取消后回调无效；新检查独立覆盖请求尚未发出的取消窗口，没有放宽原断言。

```powershell
node --test tests/test_backup_import_deadline.mjs tests/test_backup_import_controller.mjs tests/test_backup_import_transport.mjs tests/test_backup_import_dialog.mjs tests/test_runtime_read_recovery.mjs tests/test_recovery_action.mjs tests/test_message_edit_recovery.mjs tests/test_run_stop_controller.mjs tests/test_run_poll_read.mjs tests/test_live_probe_deadline.mjs tests/test_live_connection.mjs tests/test_timeline_read_deadline.mjs tests/test_timeline_refresh_ownership.mjs
```

四项契约为 `test_local_module_graph_is_closed`、`test_run_controls_wait_for_server_authority`、`test_composer_profile_has_idle_update_and_queued_snapshot_paths`、`test_interrupted_calls_require_explicit_recovery_decisions`。

## 实际服务与生产控制器

隔离树使用 xs `3232f7b8efb02fdd6bf63a508ed1e69654d6759c` 的匹配宿主，执行正式 API、生产上传及导入控制器，没有使用线上账号或外部模型。先验证原有 HTTP/TLS 丢失确认场景通过，再使用新增可选故障模式：

```powershell
python tests/test_backup_import_runtime.py --host .build/host/xsw.exe --read-faults --evidence D:/GIT/mdo/tests/acceptance/backup-import-deadline/native-import.json
```

每种协议先完成实际还原，但故意丢失一次 apply 响应；重新创建控制器后只查询书签中的原请求。第一次实际 GET 的正文交付故意永久挂起并忽略 abort，之后两次交付 503，第四次读取真实结果。HTTP、TLS 均在十秒正文期限后继续退避，观察期间没有发布错误，最终显示已提交且不再忙碌。每种协议仅一次 apply，恢复过程没有启动新运行（准备源备份时，原生测试模型已生成历史），新增一个会话；原会话逐字节不变，历史、产物、草稿和暂存队列保持原内容。原始调用、状态和故障时间见 `native-import.json`。

故障注入只在测试的 fetch 适配器中，真正请求仍抵达回环原生服务。TLS 夹具沿用仅对自有回环证书关闭 Node 证书校验的方式，因此这里不证明生产证书信任配置。该检查运行实际控制器，不等同于原生文件选择器或安卓真机操作。

另外，带四阶段精确目录清单检查的 `test_backup_stage_runtime.py` 本轮 HTTP/TLS 复测通过；之前一次源 Home 清单变化仍未复现、原因未确定。没有删除或放宽完整性断言，不能据本次通过宣称旧问题已经修复。

Windows 候选及源码摘要见 `build.json`，隔离树只加入本阶段代码，保留根目录其他未提交工作。未替换日用 mdo、手机、官网安装包，没有压力或高负载测试。长期任务仍在继续，安卓原生恢复、系统剪贴板和实际导出落盘尚待验证。
