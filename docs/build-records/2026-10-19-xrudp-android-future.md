# xrudp Future 运行时的 Android 构建验收

本轮将 Android 原生库和独立 QA 包更新到 XRT 运行时 `b093114d3d78d9700a113518d9030ffe836c7602`、
产品 SDK `49dd88fac538ca3b4988975ea9d92ac45db223d4`。使用现有 NDK `30.0.16248370`、
ARM64/API 26、compile/target SDK 36 和 mdo compact profile，未增加产品运行时依赖。

SDK 的依赖树、原生 image 哈希、profile、实际库 SHA256 和构建收据通过核对；宿主
导出注册表包含 `xrtRudpOperationInit`、`xrtRudpPeerSubmitAsync`、
`xrtRudpEndpointAsyncState`。完整 C 编译和 `--no-undefined` 共享库链接成功。
这证明 ARM64 编译/链接与注册闭包，不代表 Android TCC/JNI 已在设备执行通过。

| 产物 | 字节 | SHA256 |
| --- | ---: | --- |
| `.build/xrudp-android-future/native/libxs.so` | 3,478,312 | `43b4aa34a43619f01b05f28a7a865fec8ad247d336c6c2f4f9035ba3a344f295` |
| `.build/xrudp-android-future/app.xrtpack` | 1,453,357 | `a31d152199327f58758672c99242592c69db264567ac4b36956e0015a35ac17d` |
| `.build/xrudp-android-future/mdo-xrudp-future-qa.apk` | 5,076,205 | `61fbe9264a09c586f0c58f42729f0efb57e5713765c959583cec7fb5cbbcd8f3` |

应用来源为 mdo `957ef3e5` 加工作区其他任务原有的未提交修改。复制时核对完整 app 树
前后指纹，冻结在 `.build/xrudp-android-future/source`。历史生成缓存仍带旧依赖标记，
因此仅在该私有快照重新生成 unity 和锁文件，与新 SDK 收据保持一致；没有覆盖主工作区
的生成缓存或其他任务源码。app 原树摘要为
`0a5f9b305c4adf916a1948ed835df80baab5b4eb279468e0d541061243071e38`。

QA 包使用独立包名 `org.xleaves.mdo.xrudp.futureqa`、版本名 `0.1.1-xrudp-qa`、
版本码 1、开发签名和 lite 工具配置。未安装、发布或替换正式版。最初指定私有签名文件
路径被打包器正确拒绝，因为该路径按正式签名规则要求密码环境变量；之后使用项目已有
的标准开发签名入口完成 QA 构建，未放宽签名检查。

验证通过：

- APK 内原生库、app pack 与输入文件逐字节一致，native ZIP 条目未压缩。
- APK v2/v3 签名通过；ZIP 原生数据偏移按 16 KiB 对齐。
- ELF 为 ARM64 共享库，所有 LOAD 段的对齐及文件/虚拟地址同余均满足 16 KiB。
- Manifest 的独立包名、min SDK 26 和 target SDK 36 与构建配置一致。

证据为 `.build/xrudp-android-future/validation.json` 及
`.build/xrudp-android-future-{native-build,prepare,apk-build-r2,manifest,elf,adb}.log`。
当前 ADB 列表为空。JNI 实际加载、Android TCC/Future 运行、原生直连及后台/休眠恢复
继续保留为实机门槛；此构建不能替代这些门槛，也不替代正在运行的实际 72 小时长稳。
