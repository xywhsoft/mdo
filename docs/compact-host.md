# mdo 专用精简宿主

Windows 与 Android 的构建默认使用 `tools/host-profile.json`，不再编入全量
xrt，也不再把与平台无关的 TCC CRT 或未使用的 Windows COM/数据库 SDK
全部内嵌。原生库版本、TCC、XRTPACK、WebView、图片解码与 mdo 界面保持原有功能。

配置由 mdo 维护，xs 提供通用的 profile 构建接口。xrt 自身的模块机制负责依赖
展开；没有修改生成的 xrt 头或底层算法。模型、搜索、文件/进程、异步任务、MCP、
会话存储、TLS 信任库、更新和外部 C 模块仍受支持。宿主与 TCC 公共函数登记来自
同一个声明闭包。Windows 的 xs、xsw 和打包后的 mdo 都使用 `assets/branding/mdo.ico`。

```powershell
python tools/build_mdo.py
# 指定匹配 deps.lock 的 xs checkout 也可以：
python tools/build_mdo.py --xserver-root D:/GIT/xserver-mdo-refactor
# 完整低层 SDK 的开发构建：
python tools/build_mdo.py --full-host
```

Android 继续使用 `docs/android.md` 的构建命令；同样默认启用精简 profile，
可加 `--full-host`。编译器具备 LTO 时自动启用；例如没有 LTO 的 w64devkit
也能构建，明确回退为 `-Os` + 函数垃圾回收。Android 使用 NDK Clang 大小优化、
LTO 与隐藏 ELF 符号，保留 JNI 入口和 TCC 的地址注册。

`--skip-host-build` 和 `--skip-native-build` 仅接受匹配 revision、profile、扩展、
二进制 SHA256 的构建；Windows 还校验墨斗图标。宿主旁的 `.build.json` 仅在
开发构建目录用于验证，不进入 mdo 的便携 Home 或最终发布包。构建前会检查
应用代码调用的 xrt API，缺少模块会给出函数列表，不会生成缺少依赖的发布版。

增加外部 C 插件时需注意：精简宿主提供当前根闭包内的 xrt API，Windows 头
保留 `windows.h`、`shellapi.h`、`ws2tcpip.h`、`wincrypt.h` 的实际依赖，以及完整
目标 CRT/导入库。使用其他低层 xrt 模块或 Windows COM SDK 的插件，需扩展
profile 的根和头列表，或者采用 `--full-host`；基本文件、shell 和 mdo 模块
API 不需要更改。不要通过定义 `XRT_MODULE_ALL` 绕过 profile。

测试原则：真实打包启动、TCC 编译/插件加载、HTTP/TLS、配置和会话 API、图片
与 Android 原生入口有界回归。不运行压力或高负载测试。

## 2026-10-05 构建结果

以下 MB 使用十进制字节；Windows 对比使用完全相同的 984,890 字节应用归档，
只替换宿主，已验证归档逐字节一致。APK 对比为本次开始时的完整宿主发布包。

| 文件 | 完整宿主 | 精简宿主 | 减少 |
|------|---------:|---------:|-----:|
| Windows xs / xsw | 5,655,040 | 3,154,432 | 44.2% |
| Windows mdo.exe | 6,639,930 | 4,139,322 | 37.7% |
| Android libxs.so | 5,308,552 | 2,612,256 | 50.8% |
| Android APK | 6,526,120 | 3,835,048 | 41.2% |
| Linux x86-64 xs | 5,370,192 | 2,699,712 | 49.7% |

Windows TCC SDK 从 305 项压缩到 179 项，资源约 1,140 KB → 603 KB。运行时
公共 xrt/xhttp 函数为 Windows 2,297 项、Linux/Android 2,298 项，包含信号量
等通用同步原语；没有把 mdo 现有功能换成空实现。Windows 保持异常展开信息。

已完成：xs 扩展构建契约、profile 闭包与 SDK 测试、mdo 构建指纹校验、严格
unity 编译、TCC 双文件系统、实际 Windows SDK 编译与 HTTPS 请求、Linux
HTTP 回归、Windows/Linux 外部 C 模块加载与 MCP、模型、会话、图片、登录搜索 API 和完整
API 回归。打包回归保持运行 20 秒，真实单文件 mdo 也验证了便携 Home、移动
目录、环境/命令行 Home 覆盖。xs/xsw/mdo 内嵌的九张图标位图逐字节匹配源 ICO。

Android APK 使用原有签名证书、versionCode 20261017 完成手机覆盖安装，并实测
启动与前端加载；已有数据保留。APK 通过签名及 16 KB zipalign 校验。发布构建
使用已提交应用源码快照，其他并行开发中的未提交文件没有混入此包。
