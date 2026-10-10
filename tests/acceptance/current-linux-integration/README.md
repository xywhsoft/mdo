# 当前应用的 Linux 服务版对话验收

2026-10-11。在 WSL 的 Debian 10 基线中重新编译锁定 xs f05f766，生成
x86_64 glibc 和 musl 两个独立服务版。431 个应用输入与 Windows、Android
[集成候选](../current-integration/README.md) 逐字节一致，摘要为
`82cb0b6354b30226a4d69dead949fe36df061ee86ed782d1e3c34d3b0a8945e2`。
包含工作区已有未提交源码，不能称为仅从干净提交构建。

## 实际服务和会话

两端分别使用全新便携 Home、原生 Linux xs/TCC、正常模型配置 API 和回环
合成模型。浏览器运行在 Windows，页面和 API 由 Linux 打包程序提供。
没有模拟页面请求，没有使用生产账号或模型额度，没有压力或高负载测试。

两个版本都通过：两次 429 后第三次自动成功；首段流式回复中断后第二次
恢复，失败草稿不混入最终回复；额度不足只发起一次请求，给出准确最终
提示；修正模拟额度后点击“继续任务”恢复原回复，不重复用户消息。
退避等待时停止任务，状态为 cancelled，再普通输入可续聊。

服务正常停止/重启后，中、英、俄三行草稿和原有历史保留；实际使用
“重新连接 → 重新载入页面”后恢复输入，再发送一次成功。重启期间没有
自动重放模型请求。这个流程有明确重载步骤，不能说成进程重启后全自动恢复。
每个版本共 11 次合成模型请求，唯一 error 事件是预期的每日额度不足，
恢复后最终页面就绪、队列和草稿为空，浏览器警告/错误均为零。

另外各执行一次 [实际执行中断探针](../../test_packed_queue_start_recovery.py)：
模型尚在回复时结束宿主，恢复两个队列项，重复原启动编号不重放，明确
继续原任务和后续队列任务成功。各三次合成请求，最终错误为零。
见 [动态版](glibc-interruption.json) 和 [静态版](musl-interruption.json)。

原始 API 状态见 [glibc-native.json](glibc-native.json)、
[musl-native.json](musl-native.json)，重启前后草稿、调用计数及运行状态见
[动态版重启](glibc-restart.json)、[静态版重启](musl-restart.json)。
[结果摘要](results.json) 区分预期额度拒绝与可恢复错误。
最终页面见 [动态版](glibc-final.png)、[静态版](musl-final.png)。

## 构建收尾修复和产物

直接把 Windows 日志文件句柄传入 WSL 的两次构建都完成了打包及 ELF 检查，
但正常卸载临时 workspace bind mount 时返回 target busy，退出码为 1。
改用管道转发日志后退出码为 0，字节与首次打包完全相同。66a86f34 将此
方式加入正式 Windows Linux 构建入口；使用普通文件日志重定向再次验证
退出码 0，临时 workspace/proc 挂载均已结束。没有强制或 lazy 卸载。
见 [首次构建](first-build.json)、[管道验证](pipe-build.json)、
[修复后](fixed-build.json) 和 [实际日志](fixed-build.txt)。

`tests/test_linux_build.py` 的九项检查：Windows 六项通过、三项 ELF 环境
跳过；Linux 八项通过、一项 Windows 路径跳过。实际子进程验证日志转发
及非零退出码传播，正式基线构建验证了真实目录卸载。

| 产物 | 字节 | SHA-256 |
| --- | ---: | --- |
| glibc 服务版 | 4,091,778 | `e4f53ebc4aa9638dfb92ed021104478d2a5c621821e5000ab13c3268fc78b5cf` |
| musl 服务版 | 5,929,401 | `d534229488fe84814a1f2750e5245b4233889d8124341351c235ebcf2a49cef3` |

实际 ELF 与宿主前缀再次核对：glibc 最高导入版本 2.28，musl 没有动态依赖
或解释器，两者没有 GUI 依赖；宿主实际字节与锁定回执一致。
见 [产物记录](products.json)。文件在
`.build/conversation-acceptance/current-linux-integration/products/`。

本阶段只验证 Linux 服务版；没有把 Windows 浏览器操作说成 Linux GTK
窗口、Linux 系统剪贴板或文件选择器验收。未替换日用程序、dist 或官网；
两个隔离模型服务、程序实例和浏览器页均已结束。新版 Android 真机与
Windows 原生系统交接仍按 [验收清单](../../../docs/conversation-stability-acceptance.md)
继续保留。
