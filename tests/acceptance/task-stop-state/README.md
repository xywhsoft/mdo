# 后台任务停止状态验收

2026-10-08，源码基线 `b5da436`，新增停止状态修复见本目录记录。

原生任务停止请求已经受理时，进程或子智能体可能尚未退出。旧任务 API
只能从 `cancelled` 终态推断停止，期间继续显示“运行中”；同时 xwork 在实际
进程停止失败前就发布停止事件，会留下错误的停止标记。

现在 xwork 3.8.1 的不可变任务快照在同一任务锁内捕获停止标记，并提供
`xworkTaskSnapshotStopRequested()`。公开 task-info 结构、ABI 7 与事件格式不变。
进程停止成功后才发布标记；失败保持准确错误和原状态。子智能体的取消回调
仍在任务锁外执行，重复取消不重复发布事件。

mdo 的列表、详情及停止响应读取同一快照，计划任务执行器的取消状态继续保留。
已经受理时界面显示“正在停止…”，禁用停止按钮，附加读取在后台进行。

## 实测

- xwork 的 521 项固定功能断言通过，包括实际进程停止失败注入、模型 stop 工具、
  不可变旧快照、仍在运行的已取消子智能体、重复取消和计划任务排序关联。
- xs 扩展测试 35 项通过，1 项 Linux 专用检查在 Windows 跳过；174 项 xwork
  公开接口均已导入 TCC。vendored xwork 的 20 个生产文件通过上游逐字节检查。
- 新 xs/xsw 宿主从源码重新编译；干净 mdo 工作树从普通 xs 仓库导入锁定 bundle
  并恢复正确的依赖源码。没有混入其他工作区的未提交改动。
- 真实打包/TCC 测试创建一个子智能体，用离线模型回调保持取消后的执行现场。
  停止响应为 `running`、`terminal=false`、`stop_requested=true`；重复取消不增加
  revision，列表与详情一致，释放回调后正常变为 `cancelled`。见 `native.json`。
- 正式任务组件的浏览器验收：停止确认后，代理挂起附加列表读取，界面仍立即
  显示“正在停止…”且停止按钮禁用。原生证明子任务已经收到取消且尚未退出。
  只有一次实际停止，一条中性确认，零错误提示。见 `browser-stopping.json` 和
  `stopping.jpg`。释放回调后正常显示已停止，见 `browser-stopped.json`。
- 25 项 Node 任务检查、4 项相关前端合约、25 项原生模型恢复用例通过。
  新候选的中断/重启队列与三种摘要失败恢复回归通过，记录在两个 regression 文件。

构建合约 12 项通过，另 1 项已有静态清单遗漏已提交的 `src/remote/lan.c`，
与这次变更无关；已有工作区的合约修正没有混入本提交。

此验收没有覆盖完整主页面 WebSocket 并发，也没有宣称 Android 真机或所有功能
已验收。没有进行压力或高负载测试。日常程序、官网和 APK 未更新。

## 复现

```powershell
python tools/build_mdo.py --output .build/conversation-acceptance/mdo-task-state-final.exe
python tests/test_task_stop_runtime.py --host .build/host/xsw.exe --directory .build/task-stop-state-fresh
python tests/test_task_stop_runtime.py --host .build/host/xsw.exe --directory .build/task-stop-state-ui-fresh --manual
```

手工模式打开打印的本地地址：停止任务、查看记录、释放附加读取、释放子智能体、
刷新列表并查看终态。回调有固定五分钟上限；创建打印的 stop 文件可提前退出，
夹具会释放回调、停止并等待自己的任务和进程。所有文件限于指定夹具目录。

`build.json` 记录候选、宿主和依赖哈希。对应库提交为 xrt 兼容分支 `221eee60`
及 `0e7df370`，xs 兼容分支 `0b7e041`；当前 xs 主线使用不同版本的 xwork，
未将旧 API 整库覆盖到主线。
