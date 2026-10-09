# 设置保存与恢复默认的响应丢失恢复

本阶段补充早先 `conversation-settings-recovery` 明确未覆盖的 PATCH/DELETE 响应丢失。修复保留原界面与自动保存操作：短暂故障安静确认和退避，确实无法继续时才说明结果是否已保存、是否无法确认或未能提交。

- 两项初始用例在修复前均立即抛出 `network_error`；17 项恢复检查覆盖成功响应丢失、原 If-Match 重放、另一窗口冲突、完整代理凭据引用比较、请求超时但不理会 abort、取消/目标变化、迟到快照、已知未进入事务与永久错误。7 项自动保存、15 项设置状态和原语言初始化检查继续通过。
- native.json 来自真实 xs/TCC API，使用一次性源码副本在两个读取边界插入各一次受控事务。验证交叉修订返回 425，稳定预览能区分相同、替换及清除凭据；GET 不暴露引用，恢复默认清除用户补丁。不是并发负载或原始干净包。
- preview-ui.json 和两张 preview 截图来自提交前隔离包：暂扣成功响应，继续修改字号后按顺序保存；断开成功响应后恢复；再让五次读取失败，观察唯一最终反馈、保留的选择及点击保存重试后的恢复。浏览器可能重传断开响应的 PATCH，不把 HTTP 请求数等同于事务数。
- clean-ui.json 来自提交后的干净候选，实际页面分别修改主题与恢复默认，并在服务器完成后断开各自响应。两次操作均只增加一个配置修订和一次事务，界面确认成功；恢复默认确认是只读的，未把默认语言再次自动写回。
- candidate.json 核对干净构建标识、依赖版本、二进制哈希及十个改变的生产文件与 VFS 逐字节一致。UI 截图 `clean-restored.png` 对应干净候选最终恢复默认的页面。

验证通过：完整 API 运行门禁、严格 GCC C11 编译、901 项前端检查、158 模块图、142 项 Python（3 项环境跳过）与 124 项契约。本阶段未请求模型、未做压力或高负载测试，也不代表 Android 实机或全功能验收完成。日用 mdo.exe、手机和官网下载均未更新。

```powershell
python tests/test_settings_confirmation_runtime.py --host .build/conversation-acceptance/android-page-restore/host/xs.exe
node --test tests/test_settings_write_recovery.mjs tests/test_settings_state.mjs tests/test_settings_autosave.mjs tests/test_locale_initialization_gate.mjs
python tests/manual_packed_settings_qa.py --packed-path .build/conversation-acceptance/settings-write-recovery/clean/mdo.exe
python tests/acceptance/settings-write-recovery/check_evidence.py --exe .build/conversation-acceptance/settings-write-recovery/clean/mdo.exe
```

手动夹具支持 `POST /__qa/arm?mode=drop`，恢复默认加 `&method=DELETE`；`hold-fail` 在 `POST /__qa/release` 后返回暂时失败，`unconfirmed` 丢失成功响应并让后五次设置 GET 失败。控制接口只在一次性测试 HTTP 代理存在，未进入生产程序。
