# 扩展自定义设置

所有客户端扩展类型共用设置协议。本机、商店和内置来源均可声明表单，配置属于当前连接设备，不写入扩展目录或发布 ZIP。后续阶段的运行期、模板和界面契约在本文件继续补充。

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

`python tests/test_extension_settings_runtime.py --host .build/host-model-policy/xs.exe`：真实原目录产品 API，84 项检查通过，覆盖九类扩展、默认/0/false、显式凭据操作、预览无写入、失败与冲突无写入、重启、schema 升级、坏表单隔离及重置。不复制应用/SDK，不调用模型。

## 统一配置窗口

九类本机扩展行在更多操作前显示“配置”，只对声明 form 的扩展出现，停用仍可配置。所有窗口使用相同主题和公共弹层，手机全屏。前端保留配置输入直到得到成功回执；412 冲突支持读取最新表单、只保留用户实际修改的仍存在字段，再由用户检查并提交。默认值、远端修改、密钥保留/清除不会被未编辑的空控件覆盖。切换连接设备会销毁窗口、取消请求并清空密钥输入，不将原设备的配置提交到新设备。

`node --test tests/test_extension_settings.mjs` 通过 4 项状态/本地化检查。`python tests/test_extension_settings_runtime.py --browser "C:/Program Files/Google/Chrome/Application/chrome.exe"` 通过接口检查及英文 1400px、俄文 390px、中文 1400px 三组真实 DOM 保存、校验、冲突合并、取消、重开、按钮顺序和布局检查。截图和回执只保存到忽略的 `.build/extension-settings-browser`。
