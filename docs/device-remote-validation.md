# 设备远程控制验证与发布

## 已完成的验收

- 生产 `ai.xywhsoft.com` 已启用设备中继；只要求会员登录，所有会员可用。设备所有权由服务端会话确定。生产部署已回滚备份并同步至本地 home 仓库，不覆盖数据库、密钥及既有搜索/更新插件。
- 两个独立 PC Home 经真实线上 HTTPS/WSS 验证目标对话、流式事件、设置、只读门禁、会话改名/分叉/回收站恢复、导出和下载。控制端本机会话与设置不随远控操作变化。
- Android 原生连接器控制 PC 已完成任务；PC 控制 Android 使用内建 Ornith 实际完成对话。验收只操作测试账号与新建验收会话，原有手机会话保留。
- 错误认证、跨账号隔离、票据单次消费、撤销、关闭、账号切换/退出、目标代际变化、重载、断线重连及写入去重通过有界测试。图片附件、实时 ask/权限、备份与恢复通过原生和前端验证。
- Windows 与 Linux 真实 TCC 宿主重载通过。完整发布检查分段执行，每个失败的陈旧测试夹具修正后重新执行相关部分；全部检查部分通过，并非一次完整命令成功退出。更新后的前端 396 项检查及 GCC `-Wall -Wextra -Werror` 编译通过。
- 单文件 GUI 启动、WebView 数据位于便携 Home、移动目录后复用、Home 独占和关闭检查通过。APK 同签名覆盖安装、16 KiB 对齐与签名校验通过；会话及加密设备身份跨升级保留，关闭远控后升级不重新授权。

## Android 后台状态

手机 RMX5090 的锁屏验收尚未通过。原版与加入 CPU 租约后的版本均出现锁屏离线，本机接口超时，解锁后同进程恢复。用户已手动允许后台活动；再次锁屏仍未通过。临时电池优化豁免对照未恢复响应，已撤销该临时设置。

诊断时前台服务仍在，标准 Doze 为 ACTIVE、设备正在充电，系统报告进程未冻结、网络有效限制为零。不能仅据离线现象归因于系统省电，也不能把持有 CPU 租约等同于后台请求可用。后续通过 `XsBackground` 的调用进入/完成记录继续定位；不记录账号、租约令牌或请求内容。

CPU 租约仅在允许远控期间申请：60 秒有效，每 10 秒续期，独立 token，有界数量，平台计时器及到期回调均可释放。关闭、撤销、账号变化、Unit 与服务停止回收；没有系统凭据保护的平台不持久化授权。此机制不会自动改变电池优化、亮屏、开机启动或被杀重启。

## 可重复的有界检查

```powershell
$env:MDO_XADMIN_ROOT='D:/GIT/x-admin'
python tests/test_remote_background.py --host .build/host/xs.exe --website-host D:/GIT/x-admin/xs.exe
node --test tests/test_frontend_i18n.mjs
python -m unittest tests/test_build_contract.py
```

xs 的 CPU 租约边界测试不需要设备或 Gradle：

```text
python tools/test_android_background.py --java-home <JDK>
python tools/test_credentials.py --host <matching-xs>
```

实机测试先记录 APK 版本、设备权限、目标选中状态和原有会话 ID；锁屏后经线上中继读取目标并发送一条只回复固定文字的消息。核对目标保存、控制端未创建任务、事件/终态一致。测试 CPU 租约停止时核对系统锁释放。结束后恢复测试账号与远控开关的初始状态，保留用户数据；只撤销测试创建的临时系统设置。

不进行压力、高负载测试，不将短时在线解释为无限后台保证。
