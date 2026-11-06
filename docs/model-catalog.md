# 服务器维护的模型目录

实现日期：2026-10-30。客户端位于 `D:\GIT\mdo`，服务端位于 `D:\GIT\home\host\xywhsoft_ai\plugin\mdo`。单线程原地开发、测试和打包，没有新增工作树、SDK 或应用副本。

## 使用流程

1. 点击“添加供应商”，客户端从服务器获取最新供应商、API 产品、接口地址及推荐模型。程序只内置“自定义供应商”入口。
2. 连接供应商读取实际可用模型。选中模型后读取参数；供应商 API 返回的明确能力值优先，缺项及请求默认配置向 mdo 服务器查询。按实际接口根地址、协议、精确模型 ID 匹配，不根据名称猜测。
3. 参数完整的模型默认折叠展示上下文；未知参数自动展开并显示橙色补全提示。可以填写缺项，也可以先添加参数完整的模型，其他草稿保留。
4. 上下文容量、模型最大输出和每次请求的输出预算分别保存。新模型优先采用目录中的官方默认预算或官方推荐预算；没有公布请求默认值时使用已确认的模型输出上限，并明确标注依据。预算始终受实际 API 上限及上下文限制约束，不再统一压到 8192。
5. 保存后按实际配置逐个验证流式回复及本机真实工具定义；工具不执行。“仅回复通过”与 Agent 可用区分，未通过工具验证不会自动设为 Agent 默认模型。

用户填过的配置不会随远程目录更新而改变。旧配置显示“来源未知”，在“模型参数”中重新识别、检查并应用。重新识别保留标记为手动填写的字段。参数来源、核对时间或目录版本的变化不会使已通过的实际请求验证失效；协议、有效窗口、输出预算、模型默认思考选择、凭据引用、代理等变化仍使验证失效。

## 后台入口与存储

入口：**墨斗·模型与服务 → 模型目录**，与“模型配置 / 上游渠道 / 调用记录”同级，沿用 `model-gateway.manage` 权限及后台 CSRF 防护。

页面使用现有 `_component` 和 layui 框架，提供“供应商 / API 产品与套餐 / 模型参数”三个表格视图，支持搜索、类型筛选、分页、表格内滚动及可滚动编辑弹窗；支持增删、保存和批量 JSON 编辑。保存前检查结构、数字范围、协议、唯一 ID、重复匹配组合、来源 URL 等；使用版本指纹防止覆盖他人的更新，保存采用原子写入。

| 数据 | 服务端位置 |
| --- | --- |
| 初始目录 | `modules/models/client-catalog.defaults.json` |
| 后台保存的目录 | `plugin_data/model-gateway/client-catalog.json` |
| 组件源码与脚本 | `page/models/client-catalog-component.html`、`page/models/client-catalog.js` |
| 独立页面 | `page/models/client-catalog.html`，由 `tools/generate_client_catalog_page.py` 从组件源码生成 |
| API 与校验 | `modules/models/src/client_catalog.c` |

存在已保存目录时优先使用它；后续更改初始文件不会覆盖后台维护结果。没有数据库结构变更，不读取或改写现有渠道密钥、计费模型或会话数据。

2026-11-07 核对后的线上目录含 11 个供应商、28 个 API 产品（其中 15 个套餐入口）及 55 条精确匹配记录。23 条具备已核实能力和请求预算，15 条部分可核实、16 条未公布对应端点限制、1 条为已退役旧模型。原有 78 个端点 / 协议 / 模型组合均保留；拆分记录是为了避免不同模型共享错误限制。每条记录保留来源 URL、核对日期、协议和 API 产品范围。未知字段保留为空；后台补全后无需发布新客户端，未验证的能力不从相近型号继承。

## 接口与缓存

| 接口 | 说明 |
| --- | --- |
| 线上 `GET /api/v1/ai/client-catalog` | 无需账号登录的公开接入目录，独立于计费模型网关目录 |
| 后台 `GET /admin/model-gateway/client-catalog` | 维护页 |
| 后台 `GET/POST /admin/model-gateway/client-catalog/data` | 读取目录与 revision；提交 `{expected, catalog}`，需要既有管理权限及 CSRF；版本冲突返回 409 |
| 本地 `GET /api/v1/models/providers` | 每次主动刷新服务器目录；失败时使用缓存并返回 `stale: true` |
| 本地 `POST /api/v1/models/parameters` | `{provider, key?, protocol, models}`，一次 1–32 个模型；即使供应商能力信息完整，也查询服务器的请求默认配置 |

本地参数查询默认刷新远程目录。显式 `refresh_catalog: false` 可复用 24 小时内缓存，适用于同一目录快照或离线验收。四步添加向导先复用 `models/discover` 返回的供应商参数，为缺失能力字段或 `request_defaults` 的选中模型调用 `catalog_only:true` 的参数查询，避免重复访问供应商模型列表；分批补查每批最多 32 项。公开目录请求限定到编译时的 mdo 服务地址，5 秒超时、不跟随重定向，不发送供应商 Key、凭据引用、账号 Token、Cookie 或会话。供应商请求仍沿用该供应商的代理、TLS 和本机凭据策略。

最后一次有效目录保存在 Home 的 `data/model-catalog.json`，最大 512 KiB。服务器超时、错误或响应结构无效时不覆盖缓存，页面提示旧缓存；无缓存时仍可手动添加。缺失与明确的 false 分开，API 提供的字段不会被服务器推荐值覆盖。自定义中转可显示上游推荐，并标明需要确认中转限制；已知厂商的其他 API 产品不会自动套用同厂商记录。多个上游推荐值冲突时留空，不任选一条。

## 请求默认配置

目录记录在 `fields` 同级新增可选 `request_defaults`，避免将“每次请求的默认预算”混入“模型的最大能力”。包含 `output_tokens`、可选 `output_tokens_by_effort`、`basis`、`source_url` 和 `checked_at`。依据可为 `official-default`、`official-recommendation`、`capability-limit`；最后一种明确表示能力上限，不表示官方发布了同样数值的请求默认值。服务端校验整数范围、模型上限、思考档位名称、HTTPS 来源与核对日期。

预算选择顺序：当前思考档位的预算 → 通用请求预算 → 已确认输出上限。选择“供应商默认”时按声明的默认思考档位解析；未知模型的暂存草稿仍使用 8192，但能力缺项会阻止直接保存。实际预算再按供应商 API 限制及上下文裁剪。用户手动编辑后不随思考选择自动变化。

旧模型保存的预算继续保留，重新识别不会直接将原来的 8192 改大。用户可在参数编辑器点击“使用推荐预算”后检查并保存。新模型和已主动恢复自动预算的模型，在切换思考档位或修改有效上限时重新计算预算。中转服务的能力字段或请求预算有歧义时不会猜选某条上游记录。

服务端预算字段、后台表格与校验已部署；配套客户端源码已完成和验证，未发布新安装包。旧客户端可继续读取兼容的目录，但统一 8K 的旧客户端行为需要客户端升级才能改变。线上 `?sampling_defaults=1` 接口保留温度默认值，无此参数仍兼容旧版字段校验。

本次核对与部署详见 [官方参数更新记录](build-records/2026-11-07-model-official-defaults.md)。

## 思考控制边界

“供应商默认”是本地的 `default` 选择，实际请求省略思考控制参数，和明确发送 `none` 不同。已确认的 OpenAI effort 档位经现有适配器发送。budget、toggle、adaptive 及当前 Anthropic 适配器无法表达的 effort 只记录声明能力，页面说明当前使用供应商默认，不生成假档位。旧配置中已有的 `none` 行为保留。

## 验收与交付

- 38 项前端测试、241 个模块依赖检查、6 项模型目录合约测试通过。
- 三种协议的原生流式回复、工具定义、请求所有权、供应商默认参数省略、重试进度和旧配置兼容验证通过。
- 原生 API 验证服务器目录更新无需客户端重启/发布、离线与错误响应保留缓存、完整供应商能力仍补查请求默认配置、API 能力字段优先及密钥不发送到目录服务。参数来源刷新不会抹掉原有验证结果。
- 服务端实际目录 C 模块在原目录编译，HTTP/鉴权使用测试替身，真实文件读写验证初始目录、匿名查询、CSRF、非法参数/重复配置、原子保存与旧 revision 冲突。没有启动或复制整个平台站点。
- 浏览器使用真实前端模块及页面、localhost 模拟接口，验证橙色补全提示、自动折叠和后台编辑保存。截图位于 `.build/model-catalog-ui-20261030/`；这是示例数据，不能称为生产后台验收。
- 本地打包程序 `.build/acceptance/model-catalog-20261030/mdo.exe` 为 6,590,562 字节。使用不带外部配置的 EXE 启动，完成内置资源 API、三个协议流式回复和工具验证；回执见同目录 `build.json`。该验收包基于当前共享工作区，含已有的扩展系统未提交修改，不能视为干净发布构建。

服务端已于 2026-10-30T19:29:40+08:00 部署线上，4 个文件已核对摘要。后台菜单实际注册到“墨斗·模型与服务”，匿名公开目录正常，管理页和管理接口拒绝匿名访问；既有模型/渠道/路由、扩展目录、XS/配置及 9 个客户端更新/官网信息响应保持原状。生产后台没有进行登录或目录试写，认证后的编辑保存仍以本地模块/页面验收为依据。

配套客户端已实际连接线上目录，获取 10 个供应商及 gpt-6-luna、gpt-6.1-terra、gpt-6.1-sol 的参数成功，没有发送模型对话或供应商凭据。部署记录见 [2026-10-30-client-model-catalog-deployment.md](build-records/2026-10-30-client-model-catalog-deployment.md)。

本次未替换已安装程序，也未切换任何客户端正式发布指针。已安装客户端需要使用配套实现才能采用新目录流程；后续客户端正式发布单独进行。旧服务端没有新接口时，客户端仍能显示目录不可用并提供手动添加。

## 表格与套餐更新

2026-10-30 第二轮已上线，“模型与服务”下菜单标题为“模型目录”，路径和原有权限不变。套餐类型支持 `api`、`coding-plan`、`token-plan`、`subscription`；每个产品可维护 `key_note`、`source_url`、`checked_at`。套餐可能有独立端点，也可能与按量接口共用地址；不据地址自动推断 Key 及计费权益。客户端供应商选择页源码已显示套餐说明和官方文档链接，这部分客户端源码改动未发布安装包。

目录包含 GLM Coding Plan、Z.ai、百炼国内/国际 Coding Plan、Kimi Code、MiniMax Token Plan / M Plan，以及火山方舟 Coding Plan。尚未确认的套餐专属模型限制留空，避免沿用标准 API 的错误上限。后续参数可在表格中维护。

组件复用现有加载器的 ID 命名空间、请求取消、关闭保护和表格清理，不嵌套 HTML/body/iframe 或重复加载 layui。默认表格分页，数据区独立滚动；弹窗滚动区域限制在视口内。显示保存通知时重新计算表格高度，分页栏仍在可视区域。旧后台缓存需对整个后台 Ctrl+F5；兼容提示不会自动重载或丢弃其他标签草稿。

第二轮部署及边界见 [模型目录表格部署记录](build-records/2026-10-30-model-directory-tables-deployment.md)。首轮部署记录为历史状态，当前数量以本节为准。

## Catalog languages

Suppliers and API products carry an optional `i18n` map keyed by registered locale
IDs. `default_locale` and `locales` travel through the native providers API and
are cached with the complete public catalog. Existing plain display strings are
retained for schema-1 clients. Three complete content languages ship initially:
Simplified Chinese, English and Russian. The server owns supplier/product text;
fixed client UI copy continues to use the bundled language packs.

`catalog-localization.js` validates maps, follows explicit enabled-language
fallbacks, then uses the catalog default and legacy text. Disabled translations
are admin drafts and never selected for normal client display. Remote strings are
plain text. Known legacy product-label keys remain supported.

Names and key guidance are resolved at render time, including already-open
wizards. `presetProvider` writes the canonical name, never the currently translated
one. Existing custom names win over catalog text. Changing language or refreshing
translations therefore does not mutate saved endpoint/model/budget/credential
configuration or invalidate verification evidence.

`locale-registry.js` centralizes client language definitions and text direction.
To add a whole-client language, add its pack and definition, check placeholders
and layout, then ship the client. Server-only content translations can be updated
without a client release. A server catalog language alone does not supply the
whole client's interface pack.

The admin model catalog has independent interface/content selectors, per-language
edit tabs, coverage filters and a language-registry editor. New content languages
start disabled; fill their translations before enabling them. Current admin
components and standalone HTML share source and translation bindings.

## 2026-11-03 验证范围

验证合同升级为 `runtime-tools-roundtrip-v4`，旧证据自动失效。测试携带当前运行时启用的工具描述（含 MCP，而不再只过滤内置工具），其内容纳入指纹。工具测试验证调用参数、按实际协议回放 assistant/工具结果、再次获得可见回复；不执行任何真实工具。保存的模型参数、供应商、网络设置或工具描述变化会要求重新测试。

界面区分基础连通与当前配置验证。此证据只覆盖保存的默认模型参数及通用运行时工具，不保证每一个自定义 Agent、图片输入、其他思考档位或历史会话组合；这些场景仍需实际运行验收。测试验证了 Chat Completions、Responses 和 Anthropic 三种协议及工具结果被拒绝时不能标记通过。
