# mdo 0.5.0 原生窗口位置记忆

实现提交：`89686cec`，保持版本 0.5.0。本次没有发布官网或自动更新。

Windows 和 Linux 原生桌面窗口将正常尺寸、位置、最大化状态保存到当前 Home 的
`data/ui/main-window.state`。Home 的命令行、环境变量、默认路径优先级与浏览器 profile
一致。状态独立于浏览器缓存；不通过网页 localStorage、显示器分辨率或窗口内尺寸推算
原生位置。没有增加生产依赖。

Windows 在 UI 线程订阅原生移动／尺寸消息，350ms 合并保存，关闭和结束拖动时及时保存。
记录正常窗口边界，最小化不覆盖最大化标志；下次启动恢复可见的正常／最大化窗口。
使用临时文件、flush 和原子替换；保存失败保留旧文件并继续运行。损坏、超限和跨平台
记录只影响位置恢复，不能阻止窗口打开。

恢复时按当前显示器工作区和 DPI 限制大小、调整位置，允许虚拟桌面的负坐标。
Win32 的正常窗口边界按 [WINDOWPLACEMENT 坐标规则](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-windowplacement)
在屏幕／工作区坐标之间转换。GTK/X11 恢复位置；[Wayland 不允许应用指定绝对位置](https://docs.gtk.org/gtk3/method.Window.move.html)，
因此该后端仅恢复尺寸和最大化状态。

## 验证

- 真实 Windows WebView2 窗口连续启动七次：移动／尺寸恢复、最大化及还原边界、正常窗口最小化后重启、最大化后最小化再重启、超大／屏幕外记录、损坏记录、中文 Home 路径及 CLI 优先于环境变量全部通过。
- Windows／Linux 原生 C 几何策略测试通过，覆盖记录边界、整数溢出、嵌入 NUL、平台隔离、负坐标、DPI 换算及可用区域小于最小窗口时的限制。
- Linux GTK/X11 真窗口跨进程恢复、屏幕区域限制与损坏记录恢复通过；独立 Xvfb，不加载 WebKit、不访问账号。
- 锁定 SDK 的完整 GTK/WebKit UI 源码 `-Wall -Wextra -Werror` 语法检查通过。
- Python 单元／契约共 166 项：161 通过、5 跳过，无失败。GTK 用例在 Windows 跳过，另在 WSL 实际执行通过。
- 新 Windows 宿主的完整应用→SDK→原生浏览器回归通过，包含导航、输入、截图及背景浏览器保留；没有破坏既有原生桥接。
- 最终打包程序的默认工作区、路径补全、恢复及非目录目标保护回归通过。

全部使用独立测试 Home，无模型调用、无日常 mdo 数据修改。Linux 尚未重新生成本轮完整
发行包；Linux 最大化需窗口管理器，Wayland 需实际合成器，均未以 Xvfb 测试冒充验收。

## 交付

- Windows：`.build/window-state-20261023/mdo-0.5.0-windows.exe`
- 字节数：`5871098`
- SHA-256：`c6c1ab713301550877acdd8193044fef65ee94d7bf501ee3ffbc567ed21d7445`
- `build.json` 的 `mdo_commit` 为 `89686cecb3f3246665799ee41c41b0b051e5049f`，产品源码无未提交改动。
- SDK overlay：`66763de56c719e0cc7276e1e83042a63dfa3175e7a7288a907a71a999a3a38f2`；基础 xs 提交和 ABI 不变，重新编译了 Windows 宿主。
- 证据目录：`.build/window-state-20261023/native/`、`browser-regression/`，测试入口为 `tests/test_window_state_windows.py`、`test_window_state_gtk.py`、`test_window_geometry.py`。

最初构建时 D 盘空间不足，改用 `MDO_SDK_OVERLAY_CACHE` 指向 C 盘临时 SDK 缓存，并将
原生对象放在同一磁盘；本轮没有删除旧缓存。既有未跟踪的
`tests/test_remote_asymmetric_pause.py` 未改动、未提交。
