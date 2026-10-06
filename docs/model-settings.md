# 模型设置

模型设置分为供应商引导和高级编辑。中文界面统一使用“供应商”；配置文件中的 `provider` 字段和协议标识保持不变。

## 普通用户

1. 在“基础设置 → 模型”选择“添加供应商”。
2. 选择供应商和 API 产品，填写 API Key。标准 API 与 Coding Plan 使用各自的地址，不会自动混用。
3. 连接并读取模型列表，搜索、勾选需要的模型，再添加。供应商没有模型列表接口时，可以使用模板中的模型 ID，或手动填写。
4. 在供应商详情中测试回复；模型支持工具时，还可以单独测试工具调用。测试发送短请求，可能消耗少量 Token，不执行任何实际工具。

默认模型只在主动勾选“将首个新增模型设为默认”或点击“设为默认模型”时改变。内置 ornith-1.5-35b 保持只读。同一供应商可配置多个实例，同一 API 模型也可配置到不同实例。

PC 显示供应商列表和详情；手机先显示列表，选择后进入详情。聊天输入区的模型面板有“管理模型”入口。

## 专业配置

“高级编辑”保留多协议地址、凭据引用、TLS 校验、超时、模型能力、思考强度、附件类型和独立输入/输出限制。模板只填初始配置，刷新模型列表不会覆盖已有模型的名称或高级参数。

未知模型采用保守的上下文和输出限制，不推断图片或并行工具能力。模型目录通常只提供 ID，不足以证明实际能力。若供应商的限制更小，应在高级编辑中调整；回复和工具测试也分别验证，不把模型列表连接成功当作模型可用。

“恢复模板连接参数”只修改编辑中的地址、TLS 和超时，仍需保存。原有 `env:`、`file:` 等凭据引用继续可用。模型可停用后重新启用；默认模型不能直接停用或删除。

## 凭据与保存

直接输入的 Key 由本机 xs 凭据封装接口加密，保存为：

```
mdo-home/config/secrets/models/<random-id>.key
```

模型配置只有 `vault:<random-id>` 引用，不含明文 Key。浏览器不把 Key 写入 URL、localStorage 或 sessionStorage；草稿只保留在当前设置页内存中。移动 Home 到另一台设备后，加密 Key 可能无法使用，需要重新输入；环境变量和文件引用适用于自行管理跨设备凭据。

Key 更换使用新的不可变文件，旧文件继续为配置备份和正在运行的客户端保留。设置事务失败时清除本次新增的密钥文件，回滚无法确认时保留文件以便恢复。清理旧凭据必须同时考虑配置备份和活动客户端，不能仅按当前配置引用删除。

保存使用配置 ETag。提交前重新读取模型配置：无关的设置保存只更新版本号；真正的模型配置冲突会拒绝提交，并保留草稿。已保存但回读失败时，先刷新再继续写入。

## 接口与运行时

以下接口使用现有 API 的认证和错误结构：

| 接口 | 用途 |
| --- | --- |
| `GET /api/v1/models/config` | 非明文密钥的完整模型配置及 ETag |
| `POST /api/v1/models/setup` | `{patch, keys:[{provider,value}]}`，带 `If-Match`；凭据与配置在一次设置事务内提交 |
| `POST /api/v1/models/discover` | `{provider,key?}`，只读取草稿供应商的模型目录，不保存 Key |
| `POST /api/v1/models/test` | `{model_id,tools?:boolean}`，只测试已保存且启用的模型 |

发现请求按完整模型地址推导 `/models`，保持代理、证书设置，禁止携带凭据跟随重定向。目录限 256 KiB、2048 项、15 秒，浏览器列表显示前 100 个搜索匹配项。测试限 30 秒、最多 256 输出 Token。专用后台任务池为一个工作线程和一个等待项，避免在 xs 网络工作线程内同步阻塞；卸载前取消并等待任务结束。

当前 xs TCC SDK 没有提供全部 xhttp 扩展声明，`app/include/mdo/model_http_sdk.h` 补充锁定版本的公开声明，不包含第二份实现。升级 SDK 时运行 `python tools/model_http_sdk.py <locked-xs-root>`；CI/本地可用 `--check` 检查声明是否一致。

## 验证

```
node tests/test_provider_presets.mjs
node tests/test_frontend_i18n.mjs
python tests/test_model_setup_runtime.py --host .build/host/xs.exe
python tests/test_model_setup_runtime.py --packed mdo.exe
python tests/test_model_runtime.py --host .build/host/xs.exe
python tests/test_settings_runtime.py --host .build/host/xs.exe
python tools/model_http_sdk.py --check .build/implementation-xs
```

集成测试使用真实 xs/TCC 和本地 mock 供应商，覆盖目录、认证失败、不支持目录、拒绝重定向、配置冲突、加密保存、重启、回复、工具调用和启停。`tests/manual_model_settings_qa.py` 可启动隔离的浏览器测试站点，不读取实际用户 Key。

第三方供应商模板是默认值，模型可用性由用户的 Key、API 产品和地区决定；没有各供应商真实凭据时，不能将模板验证当作全平台线上联调通过。

模板地址参考：[ZCode 配置说明](https://zcode.z.ai/cn/docs/configuration)、[百炼 Coding Plan](https://help.aliyun.com/zh/model-studio/coding-plan)、[硅基流动快速开始](https://docs.siliconflow.cn/docs/userguide/quickstart)、[DeepSeek API](https://api-docs.deepseek.com/)。
