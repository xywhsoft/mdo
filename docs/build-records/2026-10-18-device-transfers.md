# 2026-10-18 设备间会话和项目传输

本轮实施直接设备传输，范围、操作和限制见[设备传输](../device-transfers.md)。
源码及测试留在当前工作树；没有更新官网、更新清单或生产服务。
工作树已有其他任务的改动，本轮没有重置、暂存或统一提交这些变更。

## 实施

- 新增 `app/src/transfers/`：持久化队列、固定副本、原生远控 RPC、分块接收、校验和与恢复提交、双后台线程。
- 远控 hello 声明版本能力，原生发送复用同账号控制票据及可验证的 LAN TLS offer，并保留中转路线。
- 新增会话/项目发送菜单、接收策略、传输任务弹窗和三语文案。
- 恢复层支持离线捕获未打开的会话、只含 journal 的会话、生命周期保留与父会话 ID 映射。
- 暂存 ID 在项目/会话列表和打开入口均不可见；重复确认、未知结果查询和重启恢复沿用原任务。
- 项目目录身份及已准备会话校验收据阻止覆盖；接收端提交持有账号取消租约。
- 复用防休眠与 Android 后台租约，结束后回收传输载荷。

## 验证

证据目录：`.build/device-transfer-20261018/`。所有故障注入只修改测试副本；
账号、中转网站和模型端点都是本机一次性夹具，没有生产模型调用。

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| Windows 真实双设备、65 MiB 文件、草稿/暂存队列/回收站/记忆、源数据不变、目标上下文继续 | 通过 | `runtime10.log` |
| 上传中强制退出两端并恢复；目标发布前强制退出并恢复；不重复生成项目/会话 | 通过 | `runtime10.log` |
| 强制服务器中转，禁用测试副本的直连选择 | 通过 | `runtime9.log` |
| Linux 原生文件系统双设备及中转、重启后收据、目标会话继续 | 通过 | `linux-transfer-final.log` |
| 非法路径、保留设备名、文件父子冲突、清单类型不匹配；重复/冲突创建和确认 | 通过 | 上述双设备日志 |
| 默认关闭接收；远控只读授权、TLS 信任、撤权与路线隔离 | 通过 | `remote-lan.log` |
| 原有会话备份私有暂存及提交中断恢复 HTTP/TLS | 通过 | `backup-stage.log`、`restore-worker.log` |
| Linux 会话运行探针 | 通过 | `linux-runtime.log` |
| 完整前端检查 | 1005 通过、1 条件跳过、0 失败 | `frontend.log` |
| 弹窗丢失回执复用 ID、切换任务丢弃旧响应、语言和设备路由 | 通过 | `tests/test_device_transfer_panel.mjs` 等 |
| Windows/Linux GCC 严格 C11 unity；Android ARM64 NDK Clang | 通过，Android 说明见下 | 编译日志 |
| 最终 Windows 单文件启动、能力/任务端点、JS/CSS 资源、只读启动不创建 Home | 通过 | `packed-smoke.log` |

Linux 初测放在 WSL 的 Windows 挂载目录。收到的 419 字节与源 SHA-256 完全一致；
实际失败为 `XERR_UNSUPPORTED: atomic no-replace root rename is unavailable`。
已分开校验错误和存储错误，并在接收前验证存储能力；原生 ext4 上通过。
没有采用可能覆盖现有文件的非原子降级。

Android ARM64 编译保留 `-Wall -Wextra -Werror`，仅关闭现有平台分支的 `unused-function` 诊断：
`XS_ClassIsHttp`、`MdoWebXrtFailure`、`MdoDistDownloadProgress`、`MdoDistExtract`。
没有调整这些既有功能；没有 Android 真机跨设备传输验收。
Android 的受限后台、系统强杀，以及其他 Linux 文件系统仍需实际设备验收。

## 测试产物

- Windows 测试包：`.build/device-transfer-20261018/candidate/mdo.exe`
- 大小：4,626,855 字节。
- SHA-256：`d5c0b010005e2d18c6f1df606b3312955102a4e36dcdb73e9c2d5551584be732`。
- 精确宿主：`4b0dddaa541243c0513ec1d2f9aba86dd1b5d37a`，复用已验证的隔离宿主。
- 构建收据：同目录 `build.json`，记录打包源码树及宿主哈希。
- 产品版本号维持当前工作树值；测试包不作为官网正式更新发布。

同时修正既有 Linux 更新实现的一处分行格式，使 GCC 严格检查不再因误导缩进失败；行为未变。
