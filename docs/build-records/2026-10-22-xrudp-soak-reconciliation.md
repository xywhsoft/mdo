# XRUDP 长稳复核与继续验收

2026-10-22，继续原 XRUDP 长期任务。SDK 改造已另行完成；本轮不修改产品版本号、安装程序、线上服务或用户会话。

## 复核结果

- Linux `3d55ec41` 的 IPv4/IPv6 × Select/Epoll 四组原生混合测试均完成实际连续 259200 秒，terminal 正常 join，读写字节相等、Future 请求全部完成且配额归零。重新核对原 systemd InvocationID、全部构建输入哈希、原生二进制和驱动；四组均匹配，原进程均已退出。
- Windows 原四组原生测试和一组业务测试均已明确失败，原 PID 不存在；不能将旧文档的“运行中”当作进程证据，也不能累计约 28.1 小时到新测试。
- 业务失败堆栈与原提交源码逐字核对，定位到墙钟/单调时钟偏差断言；终态保存的差值约 5.141 秒，超过五秒门槛。未确定具体系统校时/休眠机制，也没有将该断言误报成 XRUDP 网络故障。
- 原四组原生测试只记录 AssertionError，没有断言位置。相近失败时刻和约 6.1 秒差值只能作为关联线索，不能代替明确根因。

完整原生复核文档为 XRT `extlibs/xrudp/docs/long-soak-validation.md`，原验收矩阵同步修正。只读原始采集和进程核查保留在 `.build/xrudp-audit-20261022/`；脱敏汇总为 `tests/acceptance/xrudp-soak-20261022/validation.json`。原始状态文件未改写，历史控制索引只追加终态复核。

## 诊断修复

XRT `eb62cd44`、mdo `3344c560` 为观察器补充检查前的墙钟耗时、单调耗时、差值、tick 间隔、最大间隔，以及固定失败代码和脱敏堆栈位置。任意异常正文不写入收据，避免凭据或运行时输入泄漏。

原有 `abs(offset)<5`、`tick_gap<15` 门槛未放宽；边界相等、负向偏差和倒退采样都拒绝。失败状态不能因后续恢复而变成通过。

回归：原生观察器九项、业务观察器三项全部通过。新原生驱动复用已核对的旧冻结运行时，实际执行 25 秒混合 Future、故障、空闲和背压短测，正常退出并归零配额。mdo 当前 SDK 以工作区及独立已提交副本分别执行 25 秒业务短测，覆盖 HTTP/live、写入与跨路径回执、大文件、重连和完整生命周期。短测始终为 smoke_passed、acceptance_72h:false。

## 当前业务长稳

从主分支 `3344c560c9ed10779a5c0155bd767d66ff56a912` 创建独立普通主分支检出，构建输入及生成资源固定在 `.build/xrudp-audit-20261022/mdo-3344c560/`。没有创建 Codex 分支，也没有使用原主工作区随后的修改。

SDK 为 `216f5445163ca30c815ad7d5d2da3b71cb2f6401`，XRUDP 为 `0c46aca7519c5513c51b15de61d73d90ebca79b6`，核心仍为 `3d55ec41`。两个实际宿主已独立复制，mdo 宿主 SHA-256 为 `b04d1f09c2233d2d526457a3a7d0a3fef1b907e814345f1c2d4f52a8ead11d7f`，测试网站宿主为 `c88300224e3e56f6ada6623313e6b1cc88d7851f869b2d1055057ccd5475c0e1`。依赖、宿主收据及独立副本短测先通过，才启动长测。

新观察者 PID 为 80556，两个测试宿主启动时分别为 82924、87476；已通过 Win32_Process 实际查询核对映像和创建时间。进程 PID 只作为本次启动记录，后续必须重新验证，不能仅依赖本段文字。它们是有期限的隔离测试，账号、网站、项目和 Home 均为测试副本，不调用模型或公网服务。

状态：`.build/xrudp-audit-20261022/business-72h-3344c560-r1/state.json`；控制计划、宿主哈希及启动证据为同目录上级的 `business-long-plan.json`、`business-long-launch.json`。观察器通过隐藏的 pythonw/WMI 独立启动，不依赖当前工具 Job，无自动重启；最多执行 259200 秒业务和原有完整收尾。

取消时在该次 state.json 同目录创建 `stop` 文件，由观察器关闭自己拥有的测试通道和宿主，不操作用户程序。结果尚未达到 72 小时，不记为通过。后续应核对真实进程/状态、来源及最终排空，不能用观察超时认定退出或重新起算。

## 当前 XRT 主线的原生长稳

从 XRT 主分支提交 `1df26c6710114bd174d68404f167807bd0973957` 冻结当前核心、XRUDP、功能依赖头、测试夹具和观察器，在 Windows 与 Linux 分别重建。两平台各四种 IPv4/IPv6 × 后端组合的 25 秒混合短测全部通过、Future 终态配额归零，随后使用同一实际二进制启动八组独立 72 小时测试。

Windows 使用 Select/IOCP，观察者启动 PID 为 68292、77344、85168、88184；二进制 SHA-256 `70e43e072c3eff2495df6e4fed3f39ee0a7d7f023dbea4a2d91d0b50358e497f`。每组独立路径为 `.build/xrudp-audit-20261022/native-72h-1df26c67-{4,6}-{native,select}-r1/`，隐藏 WMI 启动，无自动重启。

Linux 使用 Select/Epoll，二进制 SHA-256 `b74dca205299f418b511f688c3177e460ad9d3cbb10f0015073511f4d6d1fb5c`。远端来源目录 `/home/xs/.cache/xrudp-native-soak-1df26c67-20261022/source`；四个 `xrudp-soak-1df26c67-*` 用户级单元各受 CPU 25%、512 MiB、16 任务、NoNewPrivileges、无自动重启及总期限约束。实际 InvocationID、主/子进程和二进制均核对，网站 PID 准备前后保持 17818，没有重启正式网站或替换其宿主/配置。

统一控制索引为 XRT `out/xrudp-native-soak/current-acceptance-runs-20261022.json`，本地副本 `.build/xrudp-audit-20261022/current-runs.json`；Linux 启动和实时状态收据为同目录 `linux-current-live.json`。九组目前均只是新的运行中测试，不能记为通过；此控制索引与旧 `congestion-long-runs.json` 分开保存，不改写旧成绩。

Android 物理睡眠/自然 Doze、双独立 NAT 正向直连、这些当前来源的实际 72 小时及最终交付验收仍按原长期目标继续；Linux 旧来源的通过结果不替代这些门槛。
