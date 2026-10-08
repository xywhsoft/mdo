# 在线模型登录续期与对话恢复

2026-10-08。使用合成账号和回环服务，不访问线上用户数据库、付费模型，不进行压力测试。

## 已修复

原实现仅在一轮开头获取 bearer，401 后先让对话失败，再异步刷新账号；退避后的重试仍使用旧 bearer。
现在每个 HTTP 尝试获取新凭据，在尚未交付内容时最多进行一次认证恢复。所有尝试共用原来的六次尝试和恢复时限，
不嵌套第二轮重试。账号租约贯穿整个调用，退出或成功切换账号会取消旧请求，不能使用新账号继续旧请求。

续期等待遵守调用截止时间。服务商配置、额度等明确业务错误不触发用户令牌轮换。部分输出、回调拒绝及调用者取消
保持原有边界。清理部分租约时先释放账号锁，避免等待取消回调期间占用账号锁。

## 验证

- `adapter.json`：12 个真实 HTTP/TCC 场景，覆盖续期、再次失效、退避期间失效、业务错误、共享预算、取消、截止、部分输出、钩子拒绝和空错误输出。
- `account-worker.json`：真实账号工作线程的 5 个场景。100 ms 调用截止约 110 ms 返回，退出与切换都取消旧请求，所有获取者最终归零。
- 原有 `test_model_retry_runtime.py` 的 25 个场景通过；配置的第三方模型重试策略保持正常。
- 隔离 xadmin/原生客户端的 `test_account_runtime.py` 和统一 mdo 网关的 `test_online_model_runtime.py` 通过。验证登录/续期、临时及加密凭据、失败切换保留、余额/额度、VIP 模型、切换后历史、退出及凭据隔离。
- `browser.json` 与 `two-turns.jpg`：真实前端经完整原生后端完成两轮对话。首轮收到 401、续期、429、退避、成功；第二轮成功。4 次模型请求、1 次令牌续期、零最终模型错误。
- `packed-recovery.json`、`packed-edit.json`：独立候选包的恢复关联、拒绝过期操作、历史编辑重放及重启围栏通过，共 5 次模型调用。

页面第一次发送仍观察到一次“已有另一个项目的新任务待恢复”的误提示，随后任务正常执行。这是下一项独立排查，不属于模型恢复通过的结论。
后续已单独复现并修复，见 `../home-first-send` 的测试与打包页面记录。
最初不完整的 UI 夹具没有返回模型清单，出现正确的模型不可用提示；补全真实清单格式及 64 字符版本后完成上述验收。

## 复现

```powershell
python tests/test_online_model_auth_retry.py --host .build/conversation-acceptance/export-host/xs.exe
python tests/test_online_model_auth_account.py --host .build/conversation-acceptance/export-host/xs.exe
python tests/test_model_retry_runtime.py --host .build/conversation-acceptance/export-host/xs.exe
python tests/manual_online_auth_qa.py --source-root <已准备的干净源码> --host <匹配宿主>
```

手动夹具打印一次性目录、页面及停止文件。通过页面临时登录 `account-renew` / `Fixture-only-2026`，取消勾选记住设备，
发送两条消息。模型和身份均为本机合成服务。结束后创建打印出的停止文件，夹具会关闭自己创建的原生进程。

候选包 `.build/conversation-acceptance/mdo-auth-renewal-final.exe`：4,419,293 字节，
SHA256 `267beb6e4a5681908d3d82bfa06ef5f3ee62e09bd77db2c26cc36c73b84ca17a`。
基于干净 `c1e42bf` 加本阶段四个产品文件，固定 xs `907744aac65e093a48a4a3192780dc36fd2fe430`，
复用此前验证过的精简宿主，配置指纹 `8a35d04162302781601b4e8b8c173721bf0d92a6dedb7b1da0e3d6a39fac6d86`。
未纳入其他并行开发内容，未替换根目录程序、安装手机或发布官网；Android 真机与线上自然到期场景仍待验证。
