# mdo 配置合同

mdo 配置 schema v1 由一份内置基线、三份可选用户 patch 和一层进程期覆盖组成。内置基线在外部 Home 挂载之前从应用 VFS 读取，因此外部同名文件不能改变产品基线。

```text
内置 config/defaults.json
  + mdo-home/config/settings.json
  + mdo-home/config/models.json
  + mdo-home/config/permissions.json
  + MDO_CONFIG_OVERRIDE
  + --config-override <json>
  = effective config
```

后三个文件采用同一 envelope：

```json
{
  "schema_version": 1,
  "patch": {
    "appearance": {
      "theme": "dark"
    }
  }
}
```

对象按键递归合并，数组和标量整体替换。envelope 的未知键会被拒绝；`patch` 内的未知键会被保留并参与导出，便于后续版本和外接模块扩展。保存前会删去与内置基线相同的值，因此磁盘只记录用户修改。环境覆盖先应用，命令行覆盖后应用；两者只影响当前进程，不写入 Home。

所有读取、预览和提交经过同一 schema v1 验证器。保存使用同目录临时文件、flush 和原子替换；已有文件在替换或恢复之前复制为 `.bak`。写入失败时继续使用已发布的内存配置，原文件保持不变。只读介质上的导入失败，不会把 ephemeral 配置伪装成已保存。

普通 JSON 配置不能保存 `api_key`、token、password、client secret、private key 或 Authorization 等敏感值。模型凭据只能保存为 provider 的 `credential.secret_ref`，v1 接受 `env:`、`file:`、`keychain:` 和 `prompt:` 引用。secret resolver 在使用模型时解析引用，配置导入和导出始终只处理引用文本。

联网搜索只由账号登录和 `settings.agent.web_search` 决定，使用账号服务
`https://ai.xywhsoft.com/api/v1/search`。搜索平台和密钥由 xadmin 服务端管理。
客户端从原生账号管理器取得会员令牌，不接受配置、工具参数或环境变量传入令牌。
旧 `settings.web.enabled` 和 `settings.web.search` 在读取、导入和进程覆盖时清理；
正常保存只写入清理后的 patch，启动本身不为迁移写入 Home。
`settings.web` 内部仍保留超时、响应/文本/缓存预算与网页访问策略。URL、地址解析和
权限边界见 [Web 工具合同](web-tools.md)。

`settings.transport.ca_pem_path` 可在“网络”页配置模型 HTTPS 请求使用的自定义 CA。路径必须相对于外部 `mdo-home/`，以 `/` 分隔，不允许盘符、反斜杠或 `..`；例如把 PEM 放到 `mdo-home/certs/company.pem`，设置值为 `certs/company.pem`。空值使用系统证书；非空值将 PEM 中的信任锚与系统证书合并后交给 xllm，不会关闭 TLS 校验，也不影响 Web 搜索工具。运行前读取并解析 PEM，文件缺失、超过 1 MiB 或无有效证书时模型启动明确失败。证书文件随 Home 一起搬移，单文件首次启动不要求创建该目录。

`settings.transport.proxy` 管理模型 HTTPS 请求的可选 HTTP CONNECT 或 SOCKS5 代理，字段为 `kind`（`none`、`http-connect`、`socks5`）、`host`、`port`、`user`、`bypass`。启用时须填写主机和 1–65535 端口；`bypass` 用逗号分隔目标主机模式，支持模式开头或结尾的 `*`。代理密码只能写在 `credential.secret_ref`，引用 `env:`、`file:`、`keychain:` 或 `prompt:`，若使用密码还须填写用户名。`GET /api/v1/settings` 只返回 `credential_configured`，不回传引用文本；设置页输入新引用可替换，勾选清除可移除引用，其他字段的局部更新会保留已有引用。密码在创建模型客户端时解析，读取后即释放临时副本。`kind: none` 保留参数供日后再启用。代理只作用于模型请求，不作用于 Web 搜索、MCP 或应用服务；xllm 当前仅允许 HTTPS 模型端点使用代理。

`models.providers` 与 `models.items` 分开。provider 保存 endpoint、TLS 校验、超时和凭据引用；model 保存 provider ID、wire model、可选协议、默认协议、xllm 能力、上下文/输入/输出窗口、推理档位和附件类型。模型引用的每种协议必须在 provider 上有对应 endpoint，默认协议必须属于模型协议集。

`ornith-1.5-35b` 是内建默认模型，登录后通过 ai.xywhsoft.com 模型网关使用。内建模型不可编辑/删除；网站发布的其他可用模型（例如 VIP 的 GLM）随账号目录加载，退出登录后移除。个人配置的模型保持独立。网站目录提供窗口、协议、工具、图片和推理档位等能力，客户端不会把在线目录写入用户配置。

发布包不携带上游模型密钥，构建器会删除旧的内置密钥资源；旧 `--builtin-connection` 参数仅兼容命令行，不再读取或打包该文件。上游连接与密钥只由网站管理员维护。每次在线模型请求从原生账号服务取得当前登录凭证，自动等待续期，退出或切换账号会取消旧调用；模型生成请求不会因刷新而自动重发。JS 只能读取过滤后的账号、额度和模型信息。

旧 `ling-3.0-tiny` / `ling-gpu` 会话引用继续解析到 Ornith；历史内容不被改写。已有 `config/models.json` 中的旧内建数组在读取时升级，保留自定义条目；这一步不写文件。生产程序中的内建模型不会通过旧环境变量绕过网站登录与额度。

左栏显示用户组、VIP 头像标识和各模型剩余额度百分比；账号设置显示剩余/总量、请求中预留量和每日北京时间 00:00 重置说明。无限额度显示“无限制”，不会显示虚构百分比。后台快照每 30 秒更新，模型调用结束后主动刷新；过期快照显示正在更新。日 token 计量包含输入（缓存仅计一次）与输出（包含推理），由网站完成，与上游供应商的套餐额度独立。

模型目录是引用计数的不可变 generation。reload 构造完整候选后一次发布，已有运行可继续读取旧 generation。公开 provider 信息只返回 `HasCredentialReference`，不会返回 reference 文本或解析后的 key；`MdoModelCatalogProfile` 把选定协议映射为相应的 xllm provider，并生成经过 xllm 自身校验的非敏感 profile。

`MdoConfigPreviewImport`、`MdoConfigImport`、`MdoConfigPreviewRestore` 和 `MdoConfigRestore` 为设置页提供预览后提交流程。预览不创建 Home；导出结果仍是可导入 envelope。`MdoConfigEffectiveJson` 返回当前完整有效配置的拥有式快照，调用方用 `xrtFree` 释放。
