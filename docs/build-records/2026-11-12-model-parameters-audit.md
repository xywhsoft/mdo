# 模型推荐参数全量审查（2026-11-12）

原目录 55 条；拆分 DeepSeek Flash / Pro 后 56 条。审查覆盖每条精确的服务地址、模型 ID 与协议组合。推荐目录不能代替供应商实际账号与渠道限制；缺少公开依据的字段保留未知，由 `/models` 补充或用户确认。

## 修正原则

- 模型最大输出与默认请求预算分离；官方默认/推荐优先，否则明确标记为能力上限。
- 图片支持由 input_modalities 派生；客户端同步图片附件与原生图片能力位。文件选择始终可用，文本提取与工具读取不依赖模型的原生文件输入能力。
- GLM-5.3 是文本模型，GLM-5.3-Flash 是视觉模型；DeepSeek Flash 支持视觉，Pro 不支持。
- 公开 OpenRouter `/api/v1/models` 已交叉核对 Sonnet 4.6 与 GPT-5.4 的上下文、最大输出和 text/image/file 输入；只保存选择的公开字段。
- Coding Plan、聚合渠道和动态别名不能机械继承官方直连接口上限。旧 Kimi Code 别名与 MiniMax M2.5 的输出上限缺少当前服务文档明确值，不臆造。

## 逐条结果

|序号|模型|协议|上下文|最大输出|默认预算|输入类型|待确认|来源|
|---|---|---|---:|---:|---:|---|---|---|
|1|gpt-6-luna|openai-responses|1050000|128000|128000|text, image|无|[官方依据](https://developers.openai.com/api/docs/models/gpt-6-luna)|
|2|gpt-6-luna|openai-chat-completions|1050000|128000|128000|text, image|无|[官方依据](https://developers.openai.com/api/docs/models/gpt-6-luna)|
|3|gpt-6.1-terra|openai-responses|1050000|128000|128000|text, image|无|[官方依据](https://developers.openai.com/api/docs/models/gpt-6.1-terra)|
|4|gpt-6.1-terra|openai-chat-completions|1050000|128000|128000|text, image|无|[官方依据](https://developers.openai.com/api/docs/models/gpt-6.1-terra)|
|5|gpt-6.1-sol|openai-responses|1050000|128000|128000|text, image|无|[官方依据](https://developers.openai.com/api/docs/models/gpt-6.1-sol)|
|6|gpt-6.1-sol|openai-chat-completions|1050000|128000|128000|text, image|无|[官方依据](https://developers.openai.com/api/docs/models/gpt-6.1-sol)|
|7|gpt-5.4, gpt-5.4-2026-03-05|openai-chat-completions, openai-responses|1050000|128000|128000|text, image|无|[官方依据](https://developers.openai.com/api/docs/models/gpt-5.4)|
|8|gpt-4.1-mini, gpt-4.1-mini-2025-04-14|openai-chat-completions, openai-responses|1047576|32768|32768|text, image|无|[官方依据](https://developers.openai.com/api/docs/models/gpt-4.1-mini)|
|9|glm-5.3|openai-chat-completions|1000000|131072|65536|text|无|[官方依据](https://docs.bigmodel.cn/cn/guide/models/text/glm-5.3)|
|10|glm-5.1|openai-chat-completions|200000|131072|65536|text|无|[官方依据](https://docs.bigmodel.cn/cn/guide/models/text/glm-5.1)|
|11|glm-5.3-flash|openai-chat-completions|1000000|131072|65536|text, image, video, file|无|[官方依据](https://docs.bigmodel.cn/cn/guide/models/vlm/glm-5.3-flash)|
|12|glm-5.3|openai-chat-completions|1000000|131072|65536|text|无|[官方依据](https://docs.z.ai/guides/llm/glm-5.3)|
|13|glm-5.1|openai-chat-completions|200000|131072|65536|text|无|[官方依据](https://docs.z.ai/guides/llm/glm-5.1)|
|14|glm-5.3-flash|openai-chat-completions|1000000|131072|65536|text, image, video, file|无|[官方依据](https://docs.z.ai/guides/vlm/glm-5.3-flash)|
|15|deepseek-flash, deepseek-v4-flash, deepseek-v4-flash-vision-exp|openai-chat-completions, openai-responses|1048576|393216|65536|text, image|无|[官方依据](https://api-docs.deepseek.com/quick_start/pricing/)|
|16|deepseek-chat, deepseek-reasoner|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://api-docs.deepseek.com/updates/)|
|17|kimi-k2.5|openai-chat-completions|262144|未知|未知|text, image, video|max_output_tokens|[官方依据](https://huggingface.co/moonshotai/Kimi-K2.5)|
|18|MiniMax-M2.5|anthropic-messages|未知|未知|未知|text|context_tokens, max_output_tokens, reasoning|[官方依据](https://huggingface.co/MiniMaxAI/MiniMax-M2.5)|
|19|qwen3.6-plus, qwen3.6-plus-2026-04-02|openai-chat-completions|1000000|65536|65536|text, image, video|无|[官方依据](https://docs.modelstudio.console.alibabacloud.com/en/model-studio/qwen3-6-plus)|
|20|qwen3.6-plus|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://www.alibabacloud.com/help/en/model-studio/coding-plan)|
|21|kimi-k2.5|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://www.alibabacloud.com/help/en/model-studio/coding-plan)|
|22|qwen3-coder-plus|openai-chat-completions|1000000|65536|65536|text|无|[官方依据](https://www.alibabacloud.com/help/zh/model-studio/qwen3-coder-plus)|
|23|deepseek-ai/DeepSeek-V3.2|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, input_modalities|[官方依据](https://docs.siliconflow.cn/docs/api/chat-completions-post)|
|24|Pro/zai-org/GLM-5.1|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, input_modalities|[官方依据](https://docs.siliconflow.cn/docs/api/chat-completions-post)|
|25|claude-sonnet-4-6|anthropic-messages|1000000|128000|128000|text, image, file|无|[官方依据](https://platform.claude.com/docs/en/models/sonnet-4-6/overview)|
|26|anthropic/claude-sonnet-4.6|openai-chat-completions|1000000|128000|128000|text, image, file|无|[官方依据](https://openrouter.ai/docs/api/api-reference/models/list-all-models-and-their-properties)|
|27|openai/gpt-5.4|openai-chat-completions|1050000|128000|128000|text, image, file|无|[官方依据](https://openrouter.ai/docs/api/api-reference/models/list-all-models-and-their-properties)|
|28|kimi-for-coding|openai-chat-completions|1048576|未知|未知|未知|max_output_tokens, input_modalities, 动态别名/套餐能力需实际 API 确认|[官方依据](https://www.kimi.com/code/docs/en/kimi-code/models.html)|
|29|k3|openai-chat-completions|262144|未知|未知|未知|max_output_tokens, input_modalities, 动态别名/套餐能力需实际 API 确认|[官方依据](https://www.kimi.com/code/docs/en/kimi-code/models.html)|
|30|kimi-for-coding|openai-responses|1048576|未知|未知|未知|max_output_tokens, input_modalities, 动态别名/套餐能力需实际 API 确认|[官方依据](https://www.kimi.com/code/docs/en/kimi-code/models.html)|
|31|k3|openai-responses|262144|未知|未知|未知|max_output_tokens, input_modalities, 动态别名/套餐能力需实际 API 确认|[官方依据](https://www.kimi.com/code/docs/en/kimi-code/models.html)|
|32|kimi-for-coding|anthropic-messages|1048576|未知|未知|未知|max_output_tokens, input_modalities, 动态别名/套餐能力需实际 API 确认|[官方依据](https://www.kimi.com/code/docs/en/kimi-code/models.html)|
|33|k3|anthropic-messages|262144|未知|未知|未知|max_output_tokens, input_modalities, 动态别名/套餐能力需实际 API 确认|[官方依据](https://www.kimi.com/code/docs/en/kimi-code/models.html)|
|34|kimi-for-coding|openai-chat-completions|1048576|未知|未知|未知|max_output_tokens, input_modalities, 动态别名/套餐能力需实际 API 确认|[官方依据](https://www.kimi.com/code/docs/en/kimi-code/models.html)|
|35|k3|openai-chat-completions|262144|未知|未知|未知|max_output_tokens, input_modalities, 动态别名/套餐能力需实际 API 确认|[官方依据](https://www.kimi.com/code/docs/en/kimi-code/models.html)|
|36|kimi-for-coding|openai-responses|1048576|未知|未知|未知|max_output_tokens, input_modalities, 动态别名/套餐能力需实际 API 确认|[官方依据](https://www.kimi.com/code/docs/en/kimi-code/models.html)|
|37|k3|openai-responses|262144|未知|未知|未知|max_output_tokens, input_modalities, 动态别名/套餐能力需实际 API 确认|[官方依据](https://www.kimi.com/code/docs/en/kimi-code/models.html)|
|38|kimi-for-coding|anthropic-messages|1048576|未知|未知|未知|max_output_tokens, input_modalities, 动态别名/套餐能力需实际 API 确认|[官方依据](https://www.kimi.com/code/docs/en/kimi-code/models.html)|
|39|k3|anthropic-messages|262144|未知|未知|未知|max_output_tokens, input_modalities, 动态别名/套餐能力需实际 API 确认|[官方依据](https://www.kimi.com/code/docs/en/kimi-code/models.html)|
|40|MiniMax-M3|anthropic-messages|1000000|524288|131072|text, image, video|无|[官方依据](https://platform.minimax.io/docs/api-reference/text-chat-anthropic)|
|41|MiniMax-M3|openai-chat-completions|1000000|524288|131072|text, image, video|无|[官方依据](https://platform.minimax.io/docs/api-reference/text-chat-openai)|
|42|MiniMax-M3.1-Flash|anthropic-messages|1000000|524288|131072|text, image, video|无|[官方依据](https://platform.minimax.io/docs/api-reference/text-chat-anthropic)|
|43|MiniMax-M3.1-Flash|openai-chat-completions|1000000|524288|131072|text, image, video|无|[官方依据](https://platform.minimax.io/docs/api-reference/text-chat-openai)|
|44|ark-code-latest|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://console.volcengine.com/ark/region:cn-beijing/docs/coding-plan-personal-ai-other-tools)|
|45|doubao-seed-evolving|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://console.volcengine.com/ark/region:cn-beijing/docs/coding-plan-personal-ai-other-tools)|
|46|glm-5.3|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://console.volcengine.com/ark/region:cn-beijing/docs/coding-plan-personal-ai-other-tools)|
|47|kimi-k3|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://console.volcengine.com/ark/region:cn-beijing/docs/coding-plan-personal-ai-other-tools)|
|48|deepseek-v4-pro|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://console.volcengine.com/ark/region:cn-beijing/docs/coding-plan-personal-ai-other-tools)|
|49|qwen3.7-plus|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://www.alibabacloud.com/help/en/model-studio/qwen-code)|
|50|qwen3.6-plus|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://www.alibabacloud.com/help/en/model-studio/qwen-code)|
|51|kimi-k2.5|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://www.alibabacloud.com/help/en/model-studio/qwen-code)|
|52|glm-5|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://www.alibabacloud.com/help/en/model-studio/qwen-code)|
|53|MiniMax-M2.5|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://huggingface.co/MiniMaxAI/MiniMax-M2.5)|
|54|glm-5.1|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://docs.bigmodel.cn/cn/coding-plan/overview)|
|55|glm-5.1|openai-chat-completions|未知|未知|未知|未知|context_tokens, max_output_tokens, reasoning, input_modalities|[官方依据](https://docs.z.ai/devpack/overview)|
|56|deepseek-v4-pro|openai-chat-completions, openai-responses|1048576|393216|65536|text|无|[官方依据](https://api-docs.deepseek.com/quick_start/pricing/)|

## 验证与发布

- C 编译通过 `-Wall -Wextra -Werror`；前端模型参数、添加流程及三语言测试通过。
- 本地供应商桩验证真实请求：384K 最大输出与 64K 默认预算分别生效；max 档位 128K、自定义 4K、旧预算 2K 分别遵循相应策略。验证和普通 Agent 调用均覆盖，不调用付费模型。
- 原生添加/验证回归覆盖保存、重启、图片标记规范化及验证指纹；元数据 API 回归覆盖 OpenRouter 与直接 API 的输入类型读取。
- 浏览器 1320px / 390px 验证单一图片开关、保存启用/禁用、独立预算、推荐配置自动刷新，以及用户覆盖保护。
- 服务器原生验证器、本地管理表格及三语言迁移测试通过；线上现有 Linux 运行时已用仅监听回环的独立实例通过新目录、字段校验和新旧客户端投影测试。
- 上线仅更新公开推荐目录、验证器和后台入口；客户端版本仍为 0.9.6，安装包不在本轮发布。

## 配置约定

`window.max_output_tokens` 表示能力上限；`output_budget` 保存 `mode`（recommended/custom/legacy）与 `tokens`。recommended 在每次实际请求时按所选思考档位解析预算；custom 保持手动设置。没有该字段的旧模型保留其窗口输出预算，并在参数页提示“使用推荐预算”。

`parameters.fields.input_modalities` 区分未知与明确的 text/image/audio/video/file。当前客户端仅自动派生图片输入；普通文件仍走文本提取或工具，不虚构原生文件、音频或视频适配。配置/API/草稿均规范化 image 附件与 media-input 能力，验证指纹包含规范化能力与实际预算。

打开模型管理时异步刷新公开推荐：API 字段和手动字段优先于服务器建议，ETag 保护并发修改，每批最多 32 个模型，失败不影响管理页面。手动图片选择记入字段来源，后续刷新不覆盖。

公开目录新增能力字段通过 `capability_metadata=1` 请求，旧客户端投影会移除这些字段，避免旧版本严格校验拒绝保存。后台保留输入类型、原生文件能力与 max_completion_tokens 标记的编辑及校验。

## 上线与最终验收收据

- 客户端提交 `47ac7bf3`；平台提交 `8fdd9819`。
- 公开推荐目录已上线：56 条；目录指纹 `981a65da34bf97af8f98697d36ad43431c0990837ddaddbda54929fb123125c5`。
- 匿名 GET 验证通过：新能力字段、DeepSeek Flash / Pro 差异、默认预算、旧客户端投影；后台 JS 资源线上 SHA-256 与提交文件一致。
- 线上 `xs.service` 正常；原生程序与 `xs.json` 哈希未变。仅更新本轮四个源码/资源文件及公开目录数据。私有备份保留于 `/home/xs/mdo-model-policy-20261112T032803Z/backup`。
- 最终验收通过：15 项 JS 测试、16 项原生添加验证检查、16 项打包运行检查、上下文统计回归，以及 1320 / 390 宽度浏览器测试。平台 C 验证器与 JS / 多语言测试均通过。
- 打包时另一任务开始修改扩展设置核心，引入未完成的 `MdoExtensionHash` 声明及生成 SDK 变化；普通当前工作区打包因此失败。本轮在既有打包暂存目录恢复已提交文件、重新生成该目录的 SDK 导出后完成验收，没有覆盖对方文件，也没有另建仓库或运行时。该任务的未提交代码不在本轮验收范围内。
- 可审查收据：`tests/acceptance/model-input-output-policy/proof.json`。未调用付费模型、未修改真实账号/会话数据；未发布新的客户端安装包。
