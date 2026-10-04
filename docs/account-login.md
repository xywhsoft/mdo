# 网站账号登录

墨斗的账号弹窗支持直接输入账号、已验证手机号或已验证邮箱和密码登录，无需切换浏览器。侧栏底部和「设置 → 账号」提供入口，展示当前账号、手机/邮箱验证状态和个人搜索额度。密码经受保护的本地写入 API 交给原生后台，通过 HTTPS 提交给固定的网站 `/api/v1/login`；输入框提交或关闭时清空，原生请求与临时任务缓冲区使用后清除，不写入存储、会话记录或模型上下文。密码错误或切换失败保留原账号。

「其他登录方式 / 注册」仍通过系统浏览器完成 GitHub、微信等网站授权，以及网站注册和账号管理。该流程保留以下 PKCE 交接；第三方密码不由墨斗收集。

## 交接与生命周期

1. 原生账号管理器生成一次性 state、PKCE S256 verifier/challenge。
2. 打开 `/api/v1/auth/authorize`，用户在网站登录并确认应用名称。
3. PC 通过当前实例的回环地址回调；Android 通过已验证的 HTTPS App Link 回调。
4. 原生后台线程交换授权码，保存独立的会员会话。网站浏览器会话与墨斗会话分别退出。
5. 搜索使用原生持有的访问令牌。只有明确的 HTTP 401 才刷新并重试一次；不重试已经发送但结果不明的收费搜索。

未登录的搜索在提交前等待最多五分钟，登录后继续，也可以只跳过这次搜索；任务取消和原有截止时间仍生效。退出账号会取消当前账号的请求。登录与搜索共用原生账号服务来源，不再允许配置独立的搜索地址。切换账号成功前保留当前账号，取消授权不会退出原账号。

网站会话默认采用 30 天闲置超时：认证成功的访问及有效令牌刷新延长登录期限，持续使用可持续保持登录；连续 30 天没有有效访问需重新登录。xadmin 通用配置 `session_idle_days` 和 `session_max_days` 可调整闲置期限和绝对上限，默认 30 / 0。访问令牌仍为 15 分钟；墨斗在搜索前、重启恢复时自动刷新，前端可见时也会恢复已过期的短期访问令牌。退出、撤销设备、账号禁用等仍立即失效，已过期的会话不能续期恢复。

刷新采用单一后台线程串行处理。发起刷新前删除旧的持久化刷新令牌；响应丢失时要求重新登录，避免再次使用可能已被服务器轮换的令牌。服务端成功后原子保存新令牌。

## 便携存储

「记住此设备」仅保存刷新令牌和必要的账号资料：`mdo-home/data/account/session.bin`。Windows 使用当前用户 DPAPI；Android 使用设备 Keystore AES-GCM。前端仅收到经过字段筛选的资料及额度，不收到令牌。密文绑定服务来源和相对文件路径，搬到其他设备后需重新登录。

Android 的授权过程另存短期加密的 `data/account/pending.bin`，支持浏览器切换导致进程被回收；完成、取消或过期后删除。PC 授权过程只在内存中。平台未提供安全加密时仅支持临时登录，不回退为明文保存。

## 服务端登记

登录和搜索统一使用 `https://ai.xywhsoft.com`；网络设置仅提供模型代理和证书选项。PC 客户端 ID 为 `mdo-desktop`，登记以下严格回环模板：

```text
http://127.0.0.1:{port}/api/v1/account/callback
http://[::1]:{port}/api/v1/account/callback
```

Android 客户端 ID 为 `mdo-android`，当前 APK 固定使用 `https://ai.xywhsoft.com/app/mdo/callback`。自建服务如需 Android 登录，应重新登记域名、构建 App Link 并发布对应签名关联。

xadmin 在私有 `db/identity.json` 的 `applications` 登记客户端；修改后重启站点宿主。home 仓库的 `tools/configure-ai-app-login.py` 只更新客户端条目和公开 APK 签名关联，保留原有身份密钥。不能用开发机的私有配置覆盖线上配置。

Android 要求 `/.well-known/assetlinks.json` 返回 JSON 200。xadmin 为这一个标准路径注册公开、大小受限的路由，其他隐藏文件仍受 xs 的隐藏文件规则保护。

网站还必须提供精确的 `/app/mdo/callback` 回调页面。部分手机域名验证未成功或浏览器不自动唤起应用时，页面显示「返回墨斗」，通过用户点击的 HTTPS Intent 将一次性授权结果交给指定 APK；PKCE、state 与有效期继续由原生后台校验。页面不交换令牌、不保存授权数据，并立即清除地址栏中的授权查询。缺失此页面会在浏览器中显示 404；修复网站即可供现有 APK 使用。站点实现和回归见 home 仓库的 `docs/mdo-android-login.md`。

2026-10-05 已用原有 APK 在 RMX5090 手机上验证完整登录：Edge 153 网站确认 → 回调页面 → 点击「返回墨斗」→ 应用展示正确账号和搜索额度。设备域名验证状态为 `1024`，没有强制批准域名或重装应用。

## 功能验证

```powershell
python tests/test_account_runtime.py
python tests/test_search_api_runtime.py
python D:/GIT/x-admin/tests/identity_application_e2e.py
python D:/GIT/xserver-mdo-refactor/tools/test_credentials.py --host .build/host/xs.exe
```

测试使用隔离目录和临时测试账号，不访问付费搜索、不修改用户数据库、不执行压力测试。原生联调用例覆盖页面密码登录、失败切换保留账号、密文/临时凭据、严格请求字段，以及授权码、PKCE、重复回调拒绝、本地写入校验、重启恢复、刷新、独立退出、取消、搜索等待与跳过。

静态 musl 的脚本时间 API 必须绑定宿主 libc；独立重定位的 libc 没有进程初始化，首次 vDSO 查询会崩溃。xs 的 `tools/test_tcc_clock.py --host <xs>` 同时验证外层和嵌套 TCC 的首次/重复时钟调用。

## 线上联调

2026-10-05，页面内密码登录已在 RMX5090 真机通过：切换账号弹窗直接提交测试账号，应用内显示正确资料及搜索额度，未打开浏览器。APK 覆盖安装后原有设备凭据可恢复。线上重启至续期版本后，以独立测试会话验证 HTTPS 登录、30 天 Cookie/会话期限，以及跨过一分钟写入合并窗口后的真实刷新：刷新令牌轮换，到期时间向后延长，服务进程未重启；最后撤销测试会话，未调用付费搜索。xadmin 的隔离回归另外覆盖超过 30 天但仍活跃的会话续期、已过期会话拒绝恢复及可选绝对上限。

对应基础设施提交：xadmin `2685d79`，home `3782c474`。根目录 Windows 程序已重新打包；Android 构建编号 `20261007`，签名及 16 KiB 对齐验证通过，APK SHA-256 为 `00c8853c43267f1c2c3110854cf875aaade6e27c4140229a2d9ddadbf796a608`。

2026-10-05，`ai.xywhsoft.com` 已切换至服务器环境构建的 GCC 动态链接 xs。真实测试账号通过网站授权、原生 PKCE 交换、资料/额度读取，以及原生 Agent 线程执行的 `web_search`；博查返回三条结果。退出墨斗后，网站浏览器会话仍可独立使用。公开 Android 域名关联的包名与当前 APK 签名匹配；这些验证不替代手机上的 App Link / Keystore 实测。

可选联调用例只在显式调用时连接指定网站，密码通过交互提示输入，不写入夹具或仓库：

```powershell
python tests/test_account_online.py --origin https://ai.xywhsoft.com --username your-test-user
```

加 `--search` 会额外提交一次真实博查搜索，可能消耗平台额度；没有该参数只验证账号与额度接口。用例在隔离 app 和临时 RAM 凭据中运行，通过测试线程调用原生工具，不给生产 API 增加测试入口，也不会重试结果不明的收费搜索。

本次还修复 xadmin 的通用异步 HTTPS 发送：在写入排空前不能关闭接管的连接。服务器隔离测试覆盖入口 HTTPS、上游 HTTPS、CORS/缓存响应头、插件与宿主重载；Windows HTTP 回归通过。部署副本和动态程序已提交至 home 仓库，构建信息见该仓库的 `xs.build.json` 和 `docs/xs-server-build.md`。
