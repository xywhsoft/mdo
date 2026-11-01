# MCP 服务模板与生态流程

MCP 是独立分发的扩展类型。商店作品是整个服务目录的 ZIP，包含配方、程序和依赖文件；设备上的账号连接另行保存。安装确认同时授权默认服务在启用后自动连接和发现工具。需要业务参数或第三方授权时才要求用户继续配置。同一目录可以另外创建多个个人或项目连接。

## 客户端工作流程

「设置 → MCP」提供本机 MCP 与 MCP 商店。使用共享扩展列表、分页、作者版权、窗口式详情、全局下载任务和固定按钮栏；手机适配公共窗口。商店可按名称、状态、连接方式、鉴权和平台筛选。创作中心统一位于设置的账号分类。

1. 检查作者、服务运营者、许可及内容，确认信任并安装。
2. 宿主从已审核目录派生稳定默认连接，后台完成初始化和工具发现；零配置服务直接可用。
3. 缺参时列表显示「需要配置」和「服务配置」。弹窗只显示当前方案实际使用的业务参数，一次「保存并连接」即可继续。
4. OAuth 显示「需要登录」和「登录授权」，用户主动完成所属设备的授权后自动重新发现工具。后台不会弹出登录浏览器。
5. 额外账号、项目范围或高级权限方案在高级连接配置中管理，仍需要预览和明确确认。mdo 登录与第三方登录独立。

支持 HTTPS Streamable HTTP、API key / 自定义请求头、OAuth、本机 stdio。进程按 argv 启动，不增加 shell 展开或安装钩子。使用 npx/uvx 的作者应固定依赖版本并说明启动下载行为；也可填写已安装程序的路径。

商店目录不能原始编辑或再次投稿。连接名称、业务参数、凭据、启停及项目范围独立保存，不改写商店文件。作者发布自己的完整服务目录，必须先移除用户配置及秘密，使用输入参数声明替代；不能把程序编码到 JSON。

请求头、环境变量和 OAuth 客户端密钥作为私密参数存入系统凭据库，不允许内嵌值或秘密默认值。普通参数支持文字、整数、选项、布尔值、文件/目录路径。私密参数留空保留原值，可选私密参数可明确清除。系统凭据保护不可用时拒绝保存秘密，没有明文降级。

私有连接位于 `data/mcp/connections/<id>.json`，项目选择只控制可用范围，不建立第二个安装根目录。未修改的默认连接直接从目录派生，修改后才保存私有覆盖；任何已有私有连接（包括用户主动停用的连接）都会抑制重复默认创建。设备迁移后缺少凭据时需重新配置。实际工具受 Agent、项目与任务权限约束。

## 模板格式

每个 MCP 服务是 `mdo-home/mcp/<id>/` 整目录，根 extension.json 声明 kind=mcp、alias、entry=mcp.json 和平台。mcp.json 使用 mdo.mcp-template.v1，实际程序、脚本及依赖作为普通文件同包。发布普通 ZIP，下载后解压到作品 XID 目录；不得把文件编码进配置。详见 [目录商店](ecosystem-store.md)。

```json
{
  "format":"mdo.mcp-template.v1",
  "id":"example-service",
  "operator":"服务运营者",
  "homepage":"https://example.com/",
  "inputs":[{"id":"api-key","type":"text","label":"API key","description":"填写完整认证头值","required":true,"secret":true}],
  "variants":[{
    "id":"remote","label":"远程服务","default":true,
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

每个平台最多一个显式 `default:true` 方案。没有显式默认标记时选择第一个匹配平台的未标记方案；`default:false` 只允许主动选择。通用 Linux 声明覆盖 glibc/musl，不能与同平台的另一显式默认方案重叠。Computer Use 的 ordinary Windows 方案为默认；existing-profile 授权始终需要主动选择。

## 创作、审核与生命周期

- 创作中心上传整个扩展目录的普通 ZIP，并填写作品资料和审核附件。普通用户 3 件 / 每版本 10 MiB，创作者 20 件 / 每版本 100 MiB，包含私有审核附件；复用全局配额及扩容审核。
- 公布与新版本必须审核，期间旧公开版本继续可下载。支持历史、下架和删除。审核附件仅作者与管理员可读。
- 「墨斗·扩展与插件 → MCP 管理」沿用 `_component + layui table`，有待审核/已公布/未公布切换、连接/鉴权/平台/作者/时间筛选，以及批准、退回、下架、资料和记录。
- 审核详情提供表单预览、完整模板与相对上个公开版本的字段差异。源文件摘要不可变，操作使用修订条件。运营者、作品作者、提交者账号分别展示。平台不执行投稿程序、不代用户授权，也不在生产环境探测服务。
- 元数据从 hash 验证后的模板派生。历史原始配置保留为历史数据，不混入新模板商店。平台不保存设备连接或第三方凭据，不代理日常 MCP 调用。

安装新版本不覆盖私有连接。默认连接仅在新安装目录和依赖均通过摘要校验、目标及权限声明未变时沿用原参数和凭据；目标、方案或权限变更需要重新确认。未知来源修改不自动放行。停用会停止服务；卸载会回收运行时并删除对应私有绑定。修订过期或校验失败不覆盖已有数据。

## 接口与 SDK

- 统一商店代理：`POST /api/v1/ecosystem`，`single_kind=mcp`；线上 `packages/package/history` 使用 `single=1&kind=mcp`。
- 已安装模板：`GET /api/v1/mcp/templates`。
- 连接：`GET /api/v1/mcp/connections[/{id}]`，`POST /api/v1/mcp/connections`，`action=preview|save|remove`。请求体使用 `expected_revision`，`extension_id` 与 `variant` 选择目录和方案，`values` 传普通参数，`secrets` 或 `secret_refs` 传私密参数；`clear_secrets` 清除可选密钥。保存使用预览取得的 `approval_digest`。默认连接允许停用，不允许删除后又自动重建。
- SDK：通过 `product.mcp.templates.get`、`product.mcp.connections.post` 等产品路由调用同一套连接、授权、资源和提示词服务，遵循相同权限与修订规则。

下载复用摘要校验与安装计划。连接以单文件原子替换，注册失败恢复原文件。确认响应丢失时重读修订，不盲目重复写入。
