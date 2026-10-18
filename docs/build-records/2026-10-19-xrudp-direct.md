# xrudp 候选切换与远控直连接线

## 同端口候选切换

`MdoRudpDialCandidates` 在同一个已绑定端点上依次尝试最多八个经过可信会合授权的地址，整体期限不超过 15 秒。每个未确认握手最多三秒，尝试间至少间隔 500 毫秒，失败 peer 在所属 worker 的后续轮次释放和替换。固定公钥、scope、应用绑定和 UDP 端口保持不变，不重新发现或改变 NAT 映射。

只有业务尚未受理的未确认握手可以切换。已经确认的连接失败会终止传输，不建立替代流，不重放写操作。旧 peer 的迟到回调不能影响当前 peer。原生 Connect 在返回前同步报告的本地发送错误会保留原始原因，不被误报为稍后的通用超时。地址校验与原生端点的单播、IPv4 尾部/作用域、IPv6 作用域约束对齐。

Windows/Linux 真实 TCC 的同端口探测套件增加到 26 组端点场景，包含 IPv4/IPv6 首地址黑洞后连接成功、端口与身份保持、确认后不重连、整体期限不续期。Linux 原生 ASan/UBSan 与泄漏检查通过同一套 26 组场景。既有消息传输 20 组场景仍通过。

证据日志：`.build/rendezvous-candidates-{windows,linux,sanitizers}.log`。候选切换本身不构成账号授权，不代表已完成公网或实机验收。

## 账号会合与业务路由

复用现有认证中转的 peer 身份及控制消息，增加独立的 `hello.rudp`，保留原局域网 TLS 授权格式。目标端在有效的同 peer 初始化消息到达后才创建 UDP 端点；每个 offer 有独立的 X25519 密钥、scope、绑定种子和 nonce，60 秒过期且只能使用一次。Noise 应用上下文同时绑定账号、peer、runtime、nonce、只读权限和地址族，篡改权限不能完成握手。密钥只在本机内存保存。

本地网关核对账号 lease，通过当前浏览器认证中转交换有限的会合控制消息；双方在同一端口执行可信 STUN 映射、候选发布、过滤器预热和确认握手。最多八个同族单播候选、4096 字节信令、四个 peer、十秒总体协商期限。STUN 目的地来自本地账号服务配置，远端不能指定。DNS 解析、UDP 打开和连接均为异步，探测失败时中转仍可使用。

确认后原 HTTP、二进制分片、实时订阅和收据协议直接走加密 UDP。桥接请求与排队回复持有单调不复用的内部路由号；上传、ACK、取消和实时命令只能影响原路由的请求，收据仍可经中转查询。路由关闭会取消所属任务并丢弃迟到输出，不把已受理写入自动搬到另一条连接。前端优先尝试 xrudp，然后局域网 TLS，最后使用中转，并显示实际路径。

验证：Windows 原生账号、中转、本地网关和真实 mdo HTTP/live/收据在 IPv4/IPv6 NAT 端口过滤模拟下通过；包含同 ID 冲突与跨路由取消隔离、只读拒绝、同账号不同 peer 不可冒用、一次性授权、退出/撤销和中转回退。原 LAN TLS、静默握手槽超时及 manager 生命周期回归通过。纯会合核心在 Windows/Linux 真实 TCC 和 Linux ASan/UBSan/泄漏检查通过五组加密场景，包含权限篡改拒绝。前端协商、信令边界、迟到回调及写入不重放测试通过。全应用 GCC 语法与警告检查通过。

证据：`.build/rendezvous-e2e-final-nat{4,6}.log`、`rendezvous-final-lan.log`、`rendezvous-core-{windows,linux,sanitizers}.log`、`rendezvous-final-syntax.log`。所有账号和服务均为本地隔离测试实例。尚未部署可信公网 STUN 服务，也未完成 Android、公网双机和长期稳定性验收；空闲保活另行修复，本记录不宣称生产发布完成。

## 空闲连接与当前 SDK

此前 mdo 原生连接协商 60 秒 idle，但没有主动保活；用户不操作时直连会过期。XRT 主分支 `7c82e311d314e8fc2eb3521c234b7a0c5df9a13c` 增加可选的会话层保活，复用既有加密 PING/ACK、拥塞/pacing、包记录与 PTO。mdo 设为 20 秒，不发送额外 RPC，不延长业务停滞期限，也不让失联连接永久存活。

保留产品 API 的 xs SDK 更新至 `c806e08dec676469cb622df9399f75dc96c93802`；核心 XRT 字节与主线该提交相同，未带入其他任务的未提交文件改动。可复现 bundle 为 4,416,530 字节，SHA256 `4a08b24a5851f0b4a2f48753e9865797c16ac9ec1fbce05d86720919a204e6c4`。核心库保活、发送拒收、配置边界、失联和业务停滞在 Windows -O2 及 Linux -O2 ASan/UBSan/泄漏检查通过，记录见 XRT 的 implementation-status。

Windows 实际账号、中转、原生网关、两条独立直连在 65 秒没有业务请求后仍可正常调用 HTTP；期间只处理浏览器/中转自身的 WebSocket PING/PONG，未通过 RPC 维持 xrudp。SDK 的 IPv4/IPv6 原生、取消、关闭、外层/嵌套 TCC 与 562 个公共导出检查通过；Linux 实际会合核心的新版本 sanitizer 回归通过。证据为 `.build/rendezvous-native-idle-windows.log`、`rendezvous-keepalive-sanitizers.log` 和 SDK 的 `.build/keepalive-sdk-windows.log`。

Linux 原生宿主重建后，SDK 外层/嵌套导出、IPv4/IPv6 原生连接与真实 TCC 五组会合场景均通过（SDK `.build/keepalive-sdk-linux.log`、mdo `.build/rendezvous-keepalive-linux.log`）。这是本机 WSL 的原生测试构建，未宣称完成 Linux 发行版 ABI、Android 或实机公网验收。

## 本机网关启动计入协商期限

新增回归在修复前稳定复现：本机 `directSocket` 工厂尚未返回时没有 12 秒协商计时器，本机启动请求挂起会阻止后续 LAN 尝试。修复后 xrudp 的 12 秒、LAN 的 6 秒期限从本机请求之前开始，涵盖请求、WebSocket 打开和远端授权，工厂返回不续期。超时或账号连接代终止会取消启动 fetch、立即结算可选探测并继续既有回退；不支持取消的工厂即使迟到返回，连接也会被关闭，不能替换新路径。

`test_remote_rendezvous.mjs` 覆盖工厂永久挂起后仍进入 LAN、取消信号、迟到连接清理、预算不续期、退出后的迟到结果；`test_connector_direct.mjs` 验证真实 connector 将 AbortSignal 传给 fetch，并拒绝已取消但 fetch 同时完成的原生 WebSocket 受理。原远控传输及账号刷新回归通过。修复前失败证据 `.build/rendezvous-startup-before.log`，修复后 `.build/rendezvous-startup-after.log`。中转业务在可选探测挂起期间仍可使用。
