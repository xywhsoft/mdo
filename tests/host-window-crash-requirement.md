# 需求：xserver 宿主（含 xrt 核心）修复无控制台窗口应用的启动崩溃

**呈报日期**：2026-09-20　**呈报人**：agent（按「禁止直改 xrt 核心」约束呈报）
**影响**：mdo 打包版（class=app + window）双击启动 100% 崩溃

## 现象

- 打包 exe（xsw pack）以**无控制台句柄**方式启动（双击 / PowerShell Start-Process）→ 启动后约 2 秒 c0000005 崩溃，稳定复现（4/4）
- 同一 exe 从 bash 带 stdout 重定向（`>/dev/null`）启动 → 存活（50-75 秒观测窗口内 100% 存活）
- dev 模式（xsw dev.json，有控制台）→ 完全正常（含全部功能与调度器）
- 极简应用（同宿主、同窗口配置、同图标、任意体积 200B-249KB）→ 无控制台启动也正常

## 崩溃点（已定位到符号）

- 故障指令：宿主 `.text` RVA `0x1a8531`（各次崩溃仅 ASLR 基址不同，RVA 恒定）
- 反汇编：`movzwl 0xa(%rbx)` —— 对非法指针读结构体 +0xA 字段
- 字节模式匹配 + nm 符号解析：函数 = **`__xrtOwnershipBody_FutureWaiterDetach`**（xserver main.o 内联的 xrt ownership-scope 机制）
- 26 个线程存活着（webview 正常态），崩溃线程为窗口/服务路径

## 已排除

| 排除项 | 证据 |
|---|---|
| mdo 应用代码（调度器/printf/体积） | 禁用调度器、移除 printf、249KB 假应用对照均复现/均存活如上 |
| lib/xrt.h 版本 | 新旧两版（含/不含 ownership 新作）同 RVA 崩溃 |
| 端口/数据冲突 | dev 关闭单跑仍崩；干净目录仍崩 |
| WebView2/图标/LZMA 静态服务 | mini 应用全通过；mdo-http（无窗口）无控制台待复测 |

## 需求

1. 定位 `__xrtOwnershipBody_FutureWaiterDetach` 在窗口服务启动路径的调用方，查明传入的 waiter 指针为何非法（怀疑：无控制台句柄时某初始化分支提前失败，留下悬空 waiter；或 stdio/句柄错误处理路径误入 ownership 清理）。
2. 修复后验收标准：
   - mdo 打包版双击启动，窗口正常、`data/audit.log` 有 `schedule` 之外无异常、进程存活 ≥10 分钟
   - 无控制台/有控制台/重定向三种启动方式行为一致
   - xrt 测试套（若有 ownership 相关）全绿
3. 复现包：`D:\GIT\mdo\mdo.exe`（2026-09-20 15:1x 构建）+ `powershell Start-Process D:\GIT\mdo\mdo.exe` 即可 2 秒内复现；对照存活法：bash `./mdo.exe >/dev/null 2>&1 &`

## 关联

- xserver 库一致集：f8e0a82；崩溃 RVA 对应构建：`D:\GIT\xserver\.build\windows\xllm+xllm-session+xwork+md4c+webview\main.o`
- mdo 侧已做的配合规避：ServiceInit 移除 printf（提交见 mdo 仓 log）
