# 自定义配置示例

分别将三个子目录中的文件压成 ZIP，根部直接包含 extension.json，在对应的 Agent / Agent 工具 / 插件页面导入。配置按钮出现在更多操作前面；布尔字段显示为胶囊开关。

- `roleplay` 是可运行的 Markdown Agent。无需密钥，修改名称、语气后开启新会话；“预览提示词”可先检查模板。`show_examples` 展示供自定义 C 程序读取的布尔配置，本示例 Markdown 未使用它。
- `image-api` 是可运行的 C 工具配置探针。配置服务地址、模型和 API 密钥后，调用 `example_image_configuration`，只报告是否就绪，不请求网络、不返回字段值。它为 VLX 定位或多模态解析适配器展示字典访问方式；真实图片请求需要按所选服务协议编写。
- `prompt-plugin` 展示插件私有配置如何注入提示词片段。安装启用后，在 Agent 的插件提示词选择中显式选择 `plugin:example.configurable-prompt`；无需给它提供工具或执行代码。

密钥只存到设备凭据库。普通值和密钥配置不在扩展 ZIP 中；不要把你的真实配置填成表单 default。新语言直接加入 label/title 等语言映射，不改变字段 name。字段 name 是稳定配置键，升级应保留；增加必填字段后用户会看到待补全提示。

完整协议见 [扩展设置文档](../../../docs/extension-settings.md)。
