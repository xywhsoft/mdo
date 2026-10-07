# 扩展能力：Agent、SubAgent、工具、Skill、MCP 与命令

管理入口：设置 → 扩展能力。资源分别有列表、新建、编辑、导入和导出；自定义资源可启停、删除，默认 Agent 可编辑、恢复默认，内建工具清单只读；MCP 另有测试连接。界面使用原生 HTML/JavaScript，手机端使用同一套页面。工具与主 Agent 的详细约定见 [本地工具与 Agent 配置](local-tools.md)。

## 文件与运行方式

所有持久文件都在当前设备的 `mdo-home` 中；外部文件覆盖打包内置资源。只查看资源列表不会写入资源配置。内置 Skill 等文件资源可停用或保存外部自定义版本，内建工具清单只读，默认 Agent 保持启用；删除外部覆盖后，内置资源会重新出现。

| 类型 | 路径 | 运行方式 |
| --- | --- | --- |
| Agent | `agents/<id>.md` | 系统生成普通提示词，可选同 ID C Agent |
| SubAgent | `subagents/<id>.md` | 转成现有 Agent 描述符，使用已有委派运行器 |
| 自定义工具 | `tools/<id>.c` | TCC 注册，按回调执行和收集结果 |
| Skill | `skills/<id>/SKILL.md` | 元数据发现；指令和附属文件按需读取 |
| MCP | `mcp/<id>.json` | 现有 stdio / Streamable HTTP 连接管理 |
| 命令 | `commands/<id>.md` | 展开普通聊天草稿，确认后发送 |

ID 使用 1–64 个小写字母、数字、点、短横线或下划线，不允许 Windows 保留文件名、连续点或路径分隔符。已有 C Agent、SubAgent 继续使用 `modules/agents`、`modules/subagents`。

## SubAgent

```yaml
---
name: Reviewer
description: Independently inspect changes and report concrete defects.
model: inherit
reasoning_effort: medium
tools: [read, grep, skill]
read_only: true
allow_delegation: false
---
Inspect the changed code. Report defects with supporting paths and evidence.
```

运行 ID 为 `subagent.reviewer`（文件为 `reviewer.md`）。模型留空或 `inherit` 时继承父任务；工具列表留空时继承父任务可用工具。所有子任务继续受父任务权限、并发和深度上限约束。允许再次委派最多增加一层，不能放大父任务权限。运行中的任务保留启动时的目录快照，新启动的任务使用刷新后的目录。

## Skill

```yaml
---
name: project-review
description: Inspect a repository before editing its code.
metadata:
  author: example
---
Read references/checklist.md when reviewing changes.
```

标准目录包含 `SKILL.md`，可附带 `references/`、`scripts/`、`assets/` 等目录。未知元数据保留；不执行 YAML、别名、钩子或安装脚本。旧版显式 scripts/templates/assets 清单仍限定可读取文件。

模型只增加一个共用工具：`skill`。空参数或 `query` 搜索已启用 Skill 的名称和说明，`name` 加载正文，`name` 和 `path` 读取列出的文本资源。正文/单个文本读取最多 48 KiB；二进制资源不会作为提示词输出。搜索最多返回 20 条元数据。安装 Skill 不会直接注入全部正文，也不会自动执行脚本或授予权限。

可导入单个 `SKILL.md`、完整文件夹，以及本程序导出的 `.skill.json` 文件包。浏览器小型文件夹导入/导出限定 128 个附属文件和 128 KiB 附属内容；文件包保留二进制资源。大资源或不支持目录选择的手机浏览器，可直接复制到 Home 后刷新，或者导入小型文件包。核心管理源文件最多 128 KiB；没有引入 ZIP 库、远程下载器或依赖安装器。

## MCP

表单提供 stdio（程序、JSON 参数、工作目录、环境变量）和 Streamable HTTP（HTTPS 地址、请求头）。stdio 所需程序必须已经安装在目标设备；Android 通常使用 HTTP MCP。保存不启动程序，测试连接和实际调用才连接。

支持导入常见 `{"mcpServers":{"name":{"command":"npx","args":["..."],"env":{"TOKEN":"..."}}}}` 或带 `url`、`headers` 的配置，也支持本程序的原生 JSON。多服务器导入逐个保存，不覆盖同名文件；失败会明确报告已经保存的数量。旧 SSE 传输不支持。

普通配置中的凭据进入现有本机密钥库，配置只存 `vault:` 引用。表单的环境变量/请求头支持普通 JSON 对象（如 `{"Authorization":"Bearer …"}`）和原生引用数组，也可手动填写 `env:` 和 Home 相对 `file:` 引用。导出不含凭据值，换设备后需要重新配置凭据。源文件模式可调整超时、工具过滤和权限等已有配置项；保存走现有完整校验。

## 命令

```yaml
---
description: Review a file
argument-hint: <path>
---
Review $ARGUMENTS and report concrete defects.
```

输入 `/` 后从菜单选择；带参数命令先填入 `/review `。输入参数并点击发送会展开为草稿，再次确认才提交。无参数模板选择后直接成为普通草稿。命令不运行 shell、不创建独立任务系统，不绕过附件、排队、会话恢复或权限规则。内置 `/stop`、`/new` 等名称不能被自定义资源覆盖。

## 保存与验证

编辑和删除必须带当前源文件 SHA-256 修订值，外部修改会返回冲突，不丢弃编辑草稿。普通更新原子替换文件；Skill 文件夹先准备隐藏目录，成功后整体发布。运行目录发布失败会回滚；无法完成回滚时走既有重启保护。编辑期间不能切换设备，以免将草稿写到错误设备。

主 Agent 和默认 Agent 可编辑。扩展中心支持将以上资源组成插件，保存在本地草稿、
导入或导出完整源码包，也可提交到 ai.xywhsoft.com，由管理员审核后公开。
安装后可直接跳转到资源编辑页，资源列表显示所属插件、作者和版本。
详细格式、联网接口和安装事务见 [扩展中心](ecosystem-store.md)。
暂不增加项目级资源覆盖、自动安装依赖、钩子或评分系统。

验证命令：

```text
python tests/test_extensions_runtime.py
python tests/test_extensions_runtime.py --packed mdo.exe
python tests/test_skill_runtime.py
python tests/test_module_runtime.py
node --test tests/test_extension_formats.mjs tests/test_frontend_i18n.mjs
node --experimental-vm-modules tools/check_web_modules.mjs
```
