# 扩展自定义设置

所有客户端扩展类型共用设置协议。本机、商店和内置来源均可声明表单，配置属于当前连接设备，不写入扩展目录或发布 ZIP。原生运行期、模板和界面共用同一份表单。

```json
{"settings":{"form":"settings.form.json","templates":["prompts/role.md"],"environment":{"SERVICE_API_KEY":"api_key"}}}
```

省略 settings 或写 false 表示不提供配置。form 是包内 JSON；templates 是明确选择的 Markdown/纯文本资源，不渲染 C、JSON、前端代码。environment 将字符串/数字/布尔字段映射到子进程环境变量，不拼接命令。

表单为 `mdo.extension-settings.v1`，包含正整数 config_version、可选 default_locale，以及 groups 数组；每组包含唯一 id、title、fields。最多 16 组、128 字段、64 KiB。字段 name 是唯一标识符；支持 text、textarea、url、integer、number、switch、select、multiselect、secret。label/description/placeholder/组选项文案支持字符串或语言映射。选项用字符串或 `{value,label}`。限制包括 required、min/max、min_length/max_length；visible_if 使用 `{field,equals}`，条件源不能是密钥或另一个条件字段。

字段可声明 default、expose_to_prompt。密钥禁止默认值及提示词暴露。默认只用于缺失的字段；0、false 和合法空串不会被默认值替换。

## 配置服务

- GET `/api/v1/extensions/{kind}/{id}/settings`：表单、有效普通值、密钥配置状态、issues、revision、schema_digest；ETag 与 revision 对应。不返回密钥引用或明文。
- POST `.../settings/preview`：校验候选配置；不创建凭据、不写配置。
- PUT `.../settings`：原子保存；需要 If-Match、正文 revision 和 schema_digest。values 是普通字段更新，secrets 的字符串表示替换，null 表示清除，省略表示保留。未知键、非法类型和不完整配置拒绝写入。
- PUT 的 reset=true 恢复普通值默认；clear_secrets=true 才清除密钥。重置允许进入待配置状态。

按稳定的带类型扩展 ID 的 SHA-256 保存至 Home/config/extensions/<hash>.json，记录 extension_id、config_version、revision、values。密钥只保存 device-sealed vault 引用。更新时保留旧字段与凭据；新增默认字段立即可用，新增必填或类型变化通过 issues 要求用户修复。旧窗口遇到配置或表单变化返回 412。更高 config_version 的已存数据不被旧扩展覆盖。

## 阶段一验证

第一阶段使用真实原目录产品 API，84 项检查通过（当前源码请使用文末最终验收命令），覆盖九类扩展、默认/0/false、显式凭据操作、预览无写入、失败与冲突无写入、重启、schema 升级、坏表单隔离及重置。不复制应用/SDK，不调用模型。

## 统一配置窗口

九类本机扩展行在更多操作前显示“配置”，只对声明 form 的扩展出现，停用仍可配置。所有窗口使用相同主题和公共弹层，手机全屏。前端保留配置输入直到得到成功回执；412 冲突支持读取最新表单、只保留用户实际修改的仍存在字段，再由用户检查并提交。默认值、远端修改、密钥保留/清除不会被未编辑的空控件覆盖。切换连接设备会销毁窗口、取消请求并清空密钥输入，不将原设备的配置提交到新设备。

`node --test tests/test_extension_settings.mjs` 通过 4 项状态/本地化检查。`python tests/test_extension_settings_runtime.py --browser "C:/Program Files/Google/Chrome/Application/chrome.exe"` 通过接口检查及英文 1400px、俄文 390px、中文 1400px 三组真实 DOM 保存、校验、冲突合并、取消、重开、按钮顺序和布局检查。截图和回执只保存到忽略的 `.build/extension-settings-browser`。

## 运行期字典与模板

C 工具/Agent 的 context 尾部新增只读 ExtensionSettings；可使用 `<mdo/agent.h>` 的 mdoToolSettings / mdoAgentSettings 获取 `const xvalue*`，字段直接使用 XRT 读取。按 Size 检查尾字段，旧 ABI 调用方不受影响。SDK 的 mdoReadExtensionSettings 适用于命令和原生插件；身份由宿主绑定，不允许通过参数选择别的扩展。没有原生私有上下文的调用只返回非密钥视图。

原生工具、Agent 生命周期和插件提示词，按当前 Agent 一轮内首次使用固定各自配置；下一轮重新读取，子 Agent 有独立作用域。常驻原生插件在注册时取得的配置固定到模块重载；命令固定到该次调用，经有长度边界的私有 stdin 管道传入工作进程；不会经过普通的 JSON 请求/响应文件。Skill 在每次加载正文时读取最新配置，不缓存渲染结果。需要立即换配置时，应先完成当前流程再启动新流程；MCP 在其页面重新加载配置或重启 mdo 后应用环境变量。

模板只针对 settings.templates 明确列出的 `.md` / `.txt`。支持 Agent Markdown 正文、Agent context 引用的文本文件、插件 prompts.file、Skill 正文。配置字典在模板中的名称为 `$settings`，例如 `{{$settings.character_name}}`。只有 expose_to_prompt=true 的非密钥字段可用，未暴露/不存在的字段严格报错；替换值只渲染一次。不同扩展使用自身身份。插件模板失败只跳过其片段，并在上下文检查器标记 settings_template_invalid。

“预览提示词”使用当前未保存输入，不创建凭据、不执行扩展代码。每个模板最多 128 KiB，预览合计最多 256 KiB；模板有步数、循环、深度限制。预览和配置查询均不返回密钥。Agent 的原有上下文变量模板继续使用原有协议。

settings.environment 在 MCP stdio 启动时直接构建进程环境；同名普通连接变量由设置覆盖，MDO_* 宿主变量保留。密钥不写入生成的连接 JSON、启动命令或列表。其他原生程序可使用 MdoExtensionSettingsEnvironment 将自己的私有快照转为环境字典，再通过进程 API 传递；全局 PATH 启动的外部工具不会把密钥写入包装脚本。

宿主精简配置需要 TEMPLATE_CORE；源码构建已纳入该模块。验证：原目录产品 API/浏览器回归、`tests/test_extension_settings_native.py` 的密钥隔离、作用域固定、模板及 SDK 自身身份检查。不需要复制 app 或 SDK。

## 发布与验收

商店服务器同步使用同一表单校验器，在 ZIP 封存时校验表单、显式文本文件及环境字段绑定；保留服务器已有的第十类 local-models 支持。客户端配置只存在设备的 Home/config，不属于发布包。当前阶段已提交服务器代码，未进行线上部署或客户端发布。

三个可运行示例位于 examples/extensions/settings：角色 Agent、C 图片服务配置探针、插件提示词片段。图片探针只验证参数读取，不调用任何真实图片服务。新扩展配置入口无需另外编写设置页面。

最终验收命令：

```powershell
python tools/build_mdo.py --prepare-only
python tests/test_extension_settings_native.py --host .build/host/xs.exe
python tests/test_extension_settings_archives.py --host .build/host/xs.exe
python tests/test_extension_settings_runtime.py --host .build/host/xs.exe --browser "C:/Program Files/Google/Chrome/Application/chrome.exe"
node --test tests/test_extension_settings.mjs
```

测试直接编译 app 当前源码，只创建小型、临时的 Home / 文本探针 / 配置 / ZIP，不复制 app 或 SDK。真实 Agent 回合使用离线模型回调，覆盖普通 Agent 与插件文本渲染、C 工具密钥读取、运行中更新与下轮生效、项目规则及扩展 context 文件、MCP 私有环境，以及真实命令工作进程读取配置、拒绝跨扩展参数、在执行期间扫描确认密钥没有进入 IPC JSON；浏览器覆盖三种语言及 390px 触屏布局。宿主已按 TEMPLATE_CORE 配置实编译。

当前验收结果：104 项原生检查、94 项 API/浏览器检查、客户端和服务器各 23 项真实 ZIP 校验、4 项前端状态单元测试均通过。
