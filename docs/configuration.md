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

`settings.web` 配置联机搜索 provider、HTTPS endpoint、凭据引用、超时、响应/
文本/缓存上限以及是否允许 HTTP 或私网。默认 Brave 凭据使用
`env:MDO_BRAVE_SEARCH_API_KEY`，只在调用 `web_search` 时解析。URL、地址解析和
权限边界见 [Web 工具合同](web-tools.md)。

`settings.transport.ca_pem_path` 可在“联网与搜索”页配置模型 HTTPS 请求使用的自定义 CA。路径必须相对于外部 `mdo-home/`，以 `/` 分隔，不允许盘符、反斜杠或 `..`；例如把 PEM 放到 `mdo-home/certs/company.pem`，设置值为 `certs/company.pem`。空值使用系统证书；非空值将 PEM 中的信任锚与系统证书合并后交给 xllm，不会关闭 TLS 校验，也不影响 Web 搜索工具。运行前读取并解析 PEM，文件缺失、超过 1 MiB 或无有效证书时模型启动明确失败。证书文件随 Home 一起搬移，单文件首次启动不要求创建该目录。

`models.providers` 与 `models.items` 分开。provider 保存 endpoint、TLS 校验、超时和凭据引用；model 保存 provider ID、wire model、可选协议、默认协议、xllm 能力、上下文/输入/输出窗口、推理档位和附件类型。模型引用的每种协议必须在 provider 上有对应 endpoint，默认协议必须属于模型协议集。

`ling-3.0-tiny` 是内置、免费、不可编辑且不可删除的模型，其 `ling` provider 同样受保护。服务端验证器逐字段核对两个完整 descriptor，并确认默认模型仍存在；前端禁用控件只是交互提示，不承担保护职责。内置 provider 声明 OpenAI Chat Completions、OpenAI Responses 和 Anthropic Messages 三种线上接口，模型默认选择 Responses。程序内置服务地址和随程序分发的公共访问令牌，因此普通用户无需配置；部署环境仍可分别用 `MDO_LING_CHAT_COMPLETIONS_URL`、`MDO_LING_RESPONSES_URL`、`MDO_LING_ANTHROPIC_URL` 和 `MDO_LING_API_KEY` 覆盖。该令牌可从客户端程序中提取，服务端必须独立实施配额、滥用防护与轮换。三种协议的真实线上探针仍需明确记录实际测试结果，离线 fixture 不作为线上成功证据。

模型目录是引用计数的不可变 generation。reload 构造完整候选后一次发布，已有运行可继续读取旧 generation。公开 provider 信息只返回 `HasCredentialReference`，不会返回 reference 文本或解析后的 key；`MdoModelCatalogProfile` 把选定协议映射为相应的 xllm provider，并生成经过 xllm 自身校验的非敏感 profile。

`MdoConfigPreviewImport`、`MdoConfigImport`、`MdoConfigPreviewRestore` 和 `MdoConfigRestore` 为设置页提供预览后提交流程。预览不创建 Home；导出结果仍是可导入 envelope。`MdoConfigEffectiveJson` 返回当前完整有效配置的拥有式快照，调用方用 `xrtFree` 释放。
