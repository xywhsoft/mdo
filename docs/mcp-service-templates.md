# MCP 服务模板与生态流程

MCP 是第五个独立分发的扩展类型。商店作品是一份服务模板，设备上的账号连接是另一份数据。安装模板不启动程序、不访问 MCP 服务，也不授权第三方账号。同一模板可以创建多个个人或项目连接，并分别选择已审核的版本。

## 客户端工作流程

「设置 → MCP」提供本机 MCP 与 MCP 商店。使用共享扩展列表、分页、作者版权、窗口式详情、全局下载任务和固定按钮栏；手机适配公共窗口。商店可按名称、状态、连接方式、鉴权和平台筛选。创作中心统一位于设置的账号分类。

1. 检查作品作者、服务运营者、许可、模板与历史，下载安装。
2. 创建连接，填写独立 ID、名称、个人或项目范围，选择连接方案及参数。
3. 预览实际服务地址或完整启动参数，再保存。stdio 方案还须确认本机代码信任，新连接默认停用。
4. 需要时启用并明确测试；OAuth 在所属设备上打开浏览器，显示授权目的地和请求权限。mdo 登录与第三方登录独立。

支持 HTTPS Streamable HTTP、API key / 自定义请求头、OAuth、本机 stdio。进程按 argv 启动，不增加 shell 展开或安装钩子。使用 npx/uvx 的作者应固定依赖版本并说明启动下载行为；也可填写已安装程序的路径。

商店模板不能原始编辑、导出或再次投稿，但连接名称、参数、密钥、启停和工具 allow/deny 仍可配置。自定义个人 MCP 和选定项目 MCP 可生成脱敏模板投稿。导出将地址、参数、路径及 OAuth 值改成空白参数声明，作者可以在公开预览中补回经过检查的公开常量。

请求头、环境变量和 OAuth 客户端密钥作为私密参数存入系统凭据库，不允许内嵌值或秘密默认值。普通参数支持文字、整数、选项、布尔值、文件/目录路径。私密参数留空保留原值，可选私密参数可明确清除。系统凭据保护不可用时拒绝保存秘密，没有明文降级。

项目配置沿用 `.mdo/mcp/<id>.json` 的覆盖、租约和 no-follow 规则。`mdo_template` 是原生编辑器维护的绑定元数据，包含固定模板、归属、方案和参数引用，不注入模型提示词。设备迁移后缺少凭据时需重新配置。实际工具仍通过实时 MCP 发现，并受 Agent、项目与任务权限约束。

## 模板格式

单资源作品恰好包含一个 `kind=mcp` 资源，content 是 `mdo.mcp-template.v1` JSON。外层仍使用 `mdo.extension.v1/v2`，模板格式与 MCP 协议版本独立。

```json
{
  "format":"mdo.mcp-template.v1",
  "id":"example-service",
  "operator":"服务运营者",
  "homepage":"https://example.com/",
  "inputs":[{"id":"api-key","type":"text","label":"API key","description":"填写完整认证头值","required":true,"secret":true}],
  "variants":[{
    "id":"remote","label":"远程服务",
    "platforms":["windows-x86_64","linux-x86_64","android-arm64-v8a"],
    "requirements":"说明账号申请、服务费用与数据去向",
    "config":{
      "schema_version":1,"name":"Example service","description":"面向用户的说明",
      "protocol_version":"2025-11-25","startup_timeout_ms":10000,"request_timeout_ms":30000,
      "limits":{"message_bytes":4194304,"tools":256},"tools":{"allow":[],"deny":[]},
      "security":{"default_effects":["external-service"],"permission_profile":"balanced","trust_read_only_annotations":false},
      "auto_reconnect":true,
      "transport":{"type":"streamable-http","endpoint":"https://example.com/mcp","headers":[{"name":"Authorization","secret_ref":"input:api-key"}]}
    }
  }]
}
```

普通字符串位置可用 `{"input":"参数ID"}`，私密引用位置用 `input:参数ID`。参数只在使用它的方案中要求填写，最多 32 项；方案最多 8 个。原生层校验字段白名单、类型、长度、枚举与整数范围。公开定义不接受令牌、vault/file 凭据引用、本机工作目录或安装脚本。常量服务 URL 不含用户信息、查询串或片段，需要这些设备信息时改用输入参数。

## 创作、审核与生命周期

- 创作中心可上传单资源 JSON，也可网页填写作品资料、生成无鉴权/API key/OAuth/stdio 初始模板并预览安装表单。普通用户 3 件 / 每版本 10 MiB，创作者 20 件 / 每版本 100 MiB，包含私有审核附件；复用全局配额及扩容审核。
- 公布与新版本必须审核，期间旧公开版本继续可下载。支持历史、下架和删除。审核附件仅作者与管理员可读。
- 「墨斗·扩展与插件 → MCP 管理」沿用 `_component + layui table`，有待审核/已公布/未公布切换、连接/鉴权/平台/作者/时间筛选，以及批准、退回、下架、资料和记录。
- 审核详情提供表单预览、完整模板与相对上个公开版本的字段差异。源文件摘要不可变，操作使用修订条件。运营者、作品作者、提交者账号分别展示。平台不执行投稿程序、不代用户授权，也不在生产环境探测服务。
- 元数据从 hash 验证后的模板派生。历史原始配置保留为历史数据，不混入新模板商店。平台不保存设备连接或第三方凭据，不代理日常 MCP 调用。

安装新版本不改写现有连接。连接配置可选择版本，保留普通参数并补齐新参数；更换模板/目标时重填私密参数，目标改变前撤销原本机 OAuth 授权。修订过期或校验失败不覆盖原连接。移除模板记录不会破坏已有连接，模板及密钥在最后一个引用移除后清理，范围不可可靠读取时保守保留。

## 接口与 SDK

- 统一商店代理：`POST /api/v1/ecosystem`，`single_kind=mcp`；线上 `packages/package/history` 使用 `single=1&kind=mcp`。
- 已安装模板：`GET /api/v1/mcp/templates`。
- 连接：`POST /api/v1/mcp/connections[?project=<id>]`，`action=preview|save|delete`，修订通过 `If-Match`；用 `receipt_id` / `receipt_key` 选模板、`values` 传参数、`clear_secrets` 清除可选密钥。
- SDK：`mcp.templates.list`、`mcp.connections.manage`，并保留既有连接、授权、诊断、资源和提示词服务。

下载复用摘要校验与安装计划。连接以单文件原子替换，注册失败恢复原文件。确认响应丢失时重读修订，不盲目重复写入。
