# Android 更新真机验证

日期：2026-10-05。设备：ARM64 RMX5090。没有卸载应用或清除用户数据；
没有压力、高负载或自动循环安装测试。

源码：mdo beac156、xs d3fb78e。测试包把 MDO_UPDATE_ORIGIN 编译为本机
临时 HTTP 发布器，通过 adb reverse 接通。没有在生产程序加入切换源设置。
测试版本只为 adb run-as 读取日志启用 debuggable，结束后恢复正式包。

验证结果：

| 项目 | 结果 |
| --- | --- |
| 实际已安装 APK hash | 状态 API 返回 sourceDir 的完整 APK hash，与 APK 文件相同 |
| 下载 | 实际下载、大小/hash 校验、私有 Home 原子落盘后才开放安装 |
| 原生确认取消 | 保留安装包，回到原应用，状态恢复 ready |
| 安装期间写入 | POST 被拒绝为 409 update_installing；读取仍可用 |
| 系统安装取消 | OEM 系统安装页面取消后，结果回调解除暂停，原 APK hash 不变 |
| 低 versionCode | 已安装 20261006，候选 20261005 被原生校验拒绝，未进入系统安装 |
| 错误签名 | 用独立临时测试证书重签的合法 APK 被拒绝，原应用未替换 |
| 同签名原位更新 | 经原生确认、未知来源授权和系统安装完成，安装文件等于正式候选 APK |
| 数据保留 | 测试保留文件在升级前后 SHA-256 一致；没有 pm clear / uninstall |
| 重启 | 新包 xs/TCC 装配成功，聊天首页正常显示，更新检查失败不影响启动 |

正式候选 APK：.build/mdo-update-arm64-v8a.apk；versionCode 为 20261006，
debuggable=false，包名 org.xleaves.mdo，使用原来保留的签名文件。
SHA-256：86f9e1e1ee714a591fc375c4be7c73db89ddb247e799190d824396957bb8c9d9。
最后从手机拉取的实际 base.apk 字节 hash 与上述值一致。

本轮修复了两处真机发现的问题：

1. 早期包更新状态 API 的函数参数错误导致 TCC 启动编译失败。重新打包后
   确认本地服务与前端正常启动。后续发布必须实际启动验证，不能只看 APK 构建成功。
2. PackageInstaller 输出流被手工关闭后又由 try-with-resources 关闭，出现
   EBADF。改为嵌套资源作用域，仅关闭一次，关闭完成后再 commit。系统安装已通过。

REQUEST_INSTALL_PACKAGES 是特殊权限，原生请求检查 manifest 是否声明，
由 canRequestPackageInstalls 和系统页面引导授权；不能把 checkSelfPermission
的未授权结果作为拒绝打开授权界面的条件。

复测时使用同一个签名文件，显式选择不低于设备当前版本的 versionCode。
只通过私有原生入口申请安装；外部应用不能启动未导出的安装 Activity。
不要把测试更新源、临时签名包或 debuggable 包发布给用户。
