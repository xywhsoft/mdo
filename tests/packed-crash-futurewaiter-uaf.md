# 崩溃呈报：打包版 mdo.exe 启动即崩——xrt future use-after-free（核心侧）

**日期**：2026-09-20 晚　**状态**：根因已定位到函数级，按「禁改 xrt 核心」约束呈报
**影响**：`mdo.exe`（`xsw pack` 打包版）双击/无控制台启动后约 10 秒崩溃，100% 复现（连续 3 次全新打包均崩）

## 崩溃点（符号级定位，两代构建一致）

- 故障指令：宿主 `.text` RVA `0x1a8031`：`movzwl 0xa(%rbx),%eax`——对 `pFuture + 0xA`（`xfuture::Lock`，Windows `CRITICAL_SECTION` 内部字段）读标志
- 函数：**`__xrtOwnershipBody_FutureWaiterDetach`**（lib/xrt.h:90776，`xrtFutureWatchRemove` 的实现体）
- 崩溃语义：传入的 `pFuture` 非 NULL 但内存已失效（`Lock` 字段为垃圾）——**use-after-free**：future 已被 `xrtFutureDestroy` 释放后，又有人对其调用 `xrtFutureWatchRemove`/Detach

## 复现

```
cd D:\GIT\mdo && ./xsw.exe pack app -o mdo.exe && ./mdo.exe
# 启动 ~10 秒（webview 导航 + TCC 编译 app 完成前后）崩溃，crash_*.txt 留痕
# 对照：dev 模式（xsw dev.json，无窗口）同宿主同 app 不崩
```

## 调用链推断

崩溃发生在宿主进程（gcc -O2 编译的 `main.c` TU，`XRT_MODULE_ALL + XRT_IMPLEMENTATION`）。
打包特有路径：webview 窗口 + 站点 VFS + TCC 资源编译 + reload controller。
可疑序列：webview/reload controller 持有的某 xrt future（导航完成信号之类）
在 `xllmCallDestroy` 式的「WatchRemove + Destroy」窗口期被并发 Detach；
或 Detach 与 Destroy 之间缺引用保障（修复提交 2a4f6811 中
`__xrtOwnershipBody_FutureWaiterDetach` 新增了 `xrtOwnershipMutationBegin/End`
包裹，崩溃指令位于 Mutation scope 内部对已释放 future 的 `Lock` 字段访问）。

## 已排除

- mdo 应用代码（app 层零 xrt future 调用；dev 同 app 不崩）
- 库同步状态（lib/xrt.h 与 xrt@2a4f6811 的 single/xrt.h **逐字节一致**）
- xrt 自身测试（`tls_stream_dial_proxy_tests` 全套含单头版 PASS）
- 打包陈旧性（21:44 全新 pack 仍崩，RVA 不变）

## 建议（供核心侧处理）

1. 审查 2a4f6811 中 `__xrtOwnershipBody_FutureWaiterDetach` 与
   `xrtFutureDestroy` 的竞态窗口：Detach 持 Mutation scope 锁定前，
   future 引用是否保证 ≥1？建议 WatchRemove 入口先 `xrtFutureRef` 再操作。
2. 复现增强：宿主进程启动后 ~10s 崩，与 TCC 编译完成/reload 的时序强相关，
   可在 `xs reload controller` 完成回调处加日志对齐崩溃时刻。
3. minidump 可提供：`D:\GIT\mdo` 下历史 crash_*.dmp 已清理，可按上文复现步骤重新生成。

## 关联

- xrt 用户修复提交：2a4f6811（收紧所有权边界并完善 TLS 代理拨号）
- 崩溃函数首次出现：用户重构后的 ownership-scope 版本；重构前同名 RVA 函数为
  旧版 Detach（崩溃行为在两版均存在，非本次修复引入，但修复未消除）
