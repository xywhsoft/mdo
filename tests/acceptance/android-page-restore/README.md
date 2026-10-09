# Android 页面恢复：离线验收（2026-10-09）

代码检查确认旧 xs Android 外壳在 `onCreate` 恢复 WebView 时立即访问保存的地址；
若原本地服务已经退出，会访问过期端口。随后服务就绪回调发现端口改变，只打开
首页而丢掉已选择的会话。基线代码在新的延迟导航检查中失败（`baseline.txt`）。

xs `f47b0de5037f458f6289483d1323dee417d99434` 修复此问题：页面等服务就绪后再恢复；
同源可恢复历史，换源把原本地路径、查询和片段接到新源。销毁的渲染器不再接受
页面更新、授权投递或返回操作。外部地址不会作为本地恢复路径使用。
mdo `d2731af1ffd9145d04983474031d4c66a4abb2e9` 锁定该提交及源代码 bundle。

实际完成的检查：

- 22 项生产 `PageLocation` JVM 检查：原会话、设置页、同/不同端口、编码、中文、
  IPv6、空值、损坏和非本地地址；生产 Android 外壳在 Android 36 API 上编译通过。
- 原生产 CPU 租约检查及真实 Linux 软链接路径检查通过。
- 新 Windows xs/TCC：模型失败后新输入、待执行调用的中断/重启恢复通过；工具
  文本/Markdown 产物在重启、移动 Home 后读取及路径/会话隔离检查通过。
- 16 项构建契约、153 个前端模块图通过；未指定 xs 路径的依赖恢复也通过。
- 正常打包候选的默认 Agent/工具清单通过，7 个实际 VFS 文件与源代码逐字节相同。
- 全新 ARM64 原生库及 APK 构建通过；APK CRC、原生库依赖闭包、16 KB ELF/ZIP
  对齐、v2/v3 签名通过。Dex 包含生产地址恢复类，APK 的应用 pack 与 Windows
  干净候选的末尾 pack 完全相同，原生库与锁定提交、profile 和编译回执一致。

候选路径：

```text
.build/conversation-acceptance/android-page-restore/mdo-clean.exe
.build/conversation-acceptance/android-page-restore/mdo-page-restore-qa.apk
```

测试 APK 的独立包名为 `org.xleaves.mdo.qa.page_restore`，开发签名、可调试，
build ID 为 `30000031`。没有覆盖日用 mdo、安装手机或发布官网。
`candidate.json` 记录确切提交和字节哈希；不要把该测试 APK 当作正式升级包。

复测（JDK/SDK 路径显式传入；先按锁定提交重建对应候选）：

```sh
python <locked-xs>/tools/test_android_page_location.py --java-home <jdk> --android-jar <sdk>/platforms/android-36/android.jar
python tests/test_conversation_resume_runtime.py --host <candidate-host>/xs.exe
python tests/test_session_artifact_runtime.py --host <candidate-host>/xs.exe
python tests/test_packed_module_catalog.py --packed <candidate>/mdo-clean.exe
<sdk>/build-tools/36.0.0/aapt2 dump badging <candidate>/mdo-page-restore-qa.apk > <badging.txt>
python tests/acceptance/android-page-restore/check_candidate.py --exe <candidate>/mdo-clean.exe --apk <candidate>/mdo-page-restore-qa.apk --native-receipt <native>/libxs.so.build.json --badging <badging.txt> --report <report.json>
```

本阶段 adb 没有连接设备。这里验证的是恢复规则、真实编译和桌面原生后端，
没有声称完成 Android WebView 生命周期、输入法、剪贴板或系统文件选择器实测。
它也不能解释前一阶段测试包后来不在设备上的原因。真机验收仍待完成；
未做压力或高负载测试。
