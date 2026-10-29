# 模型请求使用系统代理（2026-10-30）

新增设置 → 网络 → 模型请求代理 → **使用系统代理**。默认仍为关闭，保留 HTTP CONNECT、SOCKS5 手动模式。Windows 读取当前用户系统设置和 PAC；Linux 读取进程代理环境变量；Android 读取系统网络代理选择。浏览器扩展独立设置不在读取范围内。完整支持范围见 [配置说明](../configuration.md)。

系统模式不要求手填地址、端口，不读取旧的手动密码引用；界面停用手动字段并在保存时保留服务端已有参数。每次模型请求重新解析系统设置；切回手动或关闭后也按新设置创建请求客户端。成员共享的旧客户端保留到运行排空，避免切换时释放其他成员正在使用的对象。模型列表发现通道同步接入，解析失败有单独的错误分类及中英俄提示，不将失败自动转换为直连。

原生实现使用系统组件，无新增外部依赖。`xsSystemProxyResolve` 通过 SDK overlay 导出给 TCC；发现结果中的临时凭据在创建传输后清除。Windows PAC 不自动发送登录凭据；Android 使用单工作线程、有界队列及超时等待。

## 验证

- Windows 模拟系统 API 和 Linux 实际环境变量解析测试通过：静态代理、PAC、DIRECT、绕过、IPv6 地址、协议选择、凭据解码及失败分类；未更改本机系统代理。
- 实际 Agent 路由的 Windows/Linux 测试通过：逐请求解析、模式切换、无可用配置、失败不回退到缓存客户端、共享客户端释放时机。
- xs/TCC 本地 HTTPS 回归通过：6 次本地模型响应完成，另 1 个解析失败场景确认没有发送请求；系统模式忽略不可读取的旧手动密码。
- 配置及模型运行时回归通过；修正配置测试中已过时的固定 `.tmp` 故障注入，改为在原子写入边界注入失败。
- 最终程序设置 API、重启恢复、敏感字段过滤，以及获取模型列表、凭据加密、三个模型协议的本地调用通过。
- 全量前端：1210 通过、2 项原有跳过；236 个模块图检查通过；配置契约 6 项通过。
- Android ARM64 原生库、JVM 代理选择/超时回归和独立 APK 编译通过，签名与对齐验证通过；核对 APK 中的原生库哈希及代理 Java 类。没有进行真机网络代理验收。

验证均使用本地模型夹具，没有调用线上计费模型。已有扩展依赖工作未覆盖、未合并进本次提交。具体命令、哈希及大小见 [机器记录](2026-10-30-system-proxy.json)。

## 测试程序

- Windows：`.build/system-proxy-20261030/mdo.exe`
- Android 独立测试包：`.build/system-proxy-20261030/mdo-system-proxy-qa.apk`（`org.xleaves.mdo.systemproxyqa`）

版本保留 0.8.0。本轮只生成本地测试程序，未修改平台下载和自动更新。

代码提交：`571ca476`（原生平台能力）、`35b8d2cb`（设置、模型通道及回归）。

Windows 平台接口参考：[读取当前用户代理配置](https://learn.microsoft.com/en-us/windows/win32/winhttp/setting-wininet-proxy-configurations-in-winhttp)、[WinHTTP 代理列表](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/ns-winhttp-winhttp_proxy_info)。Android 接口参考：[ProxySelector](https://developer.android.com/reference/java/net/ProxySelector)。
