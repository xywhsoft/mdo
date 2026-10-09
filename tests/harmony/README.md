# 原生鸿蒙平台探测

此目录是可重复的可行性实验，不是正式 mdo 鸿蒙产品。2026-10-09 已在
华为官方手机模拟器内运行探测 HAP；结果和投入评估见
[评估报告](../../docs/harmony-emulator-assessment.md)。

## 探测范围

- `host_capabilities.c`：Windows 实际 OpenGL 上下文、AES、虚拟化标志。
  不显示窗口，也不修改驱动、系统功能或安全选项。Hyper-V 启用时 SLAT
  标志可能被隐藏，不据此判断 CPU 不支持虚拟化。
- `tcc_probe.c`：使用锁定 xs 的 TCC，编译、重定位、查找并调用 C 函数；
  动态函数调用已注册的原生回调 `host_add(19, 23)`，校验结果为 42。
  Windows 运行仅作为对照。
- `hap/`、`napi_probe.c`：ArkWeb → ArkTS → Node-API → 预编译 C 的桥接。
- `platform_probe.c`：应用内私有文件读写删除、pthread、localhost HTTP、
  WebSocket、系统 shell 和官方 Native Child Process API。HTTP/WS 服务
  只监听回环地址，处理固定探测请求，随应用退出，不是可复用服务端。
- `child_probe.c`：官方子进程 API 的共享库入口，通过传入的 FD 返回 42。
- `xrt_compile_probe.c`：编译和链接 `tools/host-profile.json` 中的 xrt
  模块闭包。编译通过不能代替这些模块及完整 xs 的运行验收。

## 构建

需要官方 DevEco Studio、附带 SDK/Node/Java/Hvigor/ohpm，以及本机 GCC、
CMake 和 Git。以下命令在仓库根目录执行，默认大文件和缓存均放 D 盘。
HAP 使用 `.build/locked-xs/<deps.lock 的 xserver.commit>`；先确认该锁定
源码已准备好。源码恢复机制见 [依赖说明](../../tools/dependencies/README.md)。
不要直接指向版本不同的 xs 主分支。

```powershell
$lock = Get-Content deps.lock -Raw | ConvertFrom-Json
$xs = (Join-Path (Get-Location) ".build/locked-xs/$($lock.xserver.commit)").Replace('\', '/')

# Windows TCC 对照。
cmake -S tests/harmony -B .build/harmony-assessment/windows -G 'MinGW Makefiles' "-DXS_SOURCE_DIR=$xs"
cmake --build .build/harmony-assessment/windows
& .build/harmony-assessment/windows/tcc_probe.exe

# 同时生成 ARM64 和 x86_64 的未签名调试 HAP。
& tests/harmony/build_hap.ps1
```

独立交叉编译及 xrt 模块闭包检查：

```powershell
$sdkNative = 'D:/runtimes/harmony/DevEcoStudio/sdk/default/openharmony/native'
cmake -S tests/harmony -B .build/harmony-assessment/ohos-arm64 -G 'MinGW Makefiles' `
  "-DXS_SOURCE_DIR=$xs" "-DCMAKE_TOOLCHAIN_FILE=$sdkNative/build/cmake/ohos.toolchain.cmake" `
  -DOHOS_ARCH=arm64-v8a -DMDO_PROBE_XRT=ON
cmake --build .build/harmony-assessment/ohos-arm64
```

将构建目录改为 `ohos-x86_64`、`OHOS_ARCH` 改为 `x86_64` 可检查模拟器目标。
正式手机发行不使用 x86_64。改变 SDK 安装目录时使用 `build_hap.ps1` 的
`-DevEcoDir` 参数，并同步调整交叉编译的 toolchain 路径。

## 模拟器运行

官方 SDK 和软件服务协议须由使用者接受，手机镜像须通过官方工具下载。
本次已得到用户授权并创建 `mdoProbe7lite` 手机实例；其他机器应创建自己的
实例。主机提交内存紧张时，先停止模拟器再编译，编译完成后再启动。

```powershell
$emu = 'D:\runtimes\harmony\DevEcoStudio\tools\emulator\Emulator.exe'
$hdc = 'D:\runtimes\harmony\DevEcoStudio\sdk\default\openharmony\toolchains\hdc.exe'
& $emu -start mdoProbe7lite -instancePath 'D:\runtimes\harmony\emulator\instances' `
  -imageRoot 'D:\runtimes\harmony\emulator\images' -noWindow
```

等待 `hdc list targets` 检测到模拟器，然后在另一个终端执行：

```powershell
# 此端口是本次实际 target，其他实例以 list targets 输出为准。
& $hdc -t 127.0.0.1:5555 install 'tests/harmony/hap/entry/build/default/outputs/default/entry-default-unsigned.hap'
& $hdc -t 127.0.0.1:5555 shell aa start -a EntryAbility -b com.xywhsoft.mdoprobe --ps probeTcc 1
& $hdc -t 127.0.0.1:5555 shell hilog -x -T MdoProbe
& $emu -stop mdoProbe7lite -instancePath 'D:\runtimes\harmony\emulator\instances'
```

不传 `probeTcc` 时只自动运行平台探测；TCC 可以通过界面按钮单独触发。
HAP 仅声明 `ohos.permission.INTERNET`，没有请求特殊执行权限。

## 验收边界

已通过：模拟器应用内预编译 C、网页桥接、HTTP、WebSocket、私有文件和
TCC 回调调用；ARM64/x86_64 构建和 xrt 模块闭包编译链接。

未通过：应用启动 `/system/bin/sh` 返回 EACCES（13）；官方 Native 子进程
能力检查返回 false，启动返回 16010003，子进程入口未运行。

待验：ARM64 真机运行、正式签名/发布权限、完整 xs/mdo 后端、TLS 与账户、
后台生命周期、文件选择、命令工具和 stdio MCP、较早系统兼容性。
模拟器使用 `debug_hap` 安全域，调试 HAP 成功不代表商用手机正式应用成功。
通过 `hdc shell` 运行程序也不能代替应用沙箱验收。

构建产物、依赖、缓存不提交 Git；仅保存探测源码、报告、短日志和一张实测截图。
