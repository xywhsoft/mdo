# 墨斗扩展中心

扩展中心是既有本地资源管理器的在线入口。一个插件可以组合 Agent、SubAgent、
工具、Skill、MCP 和命令；安装后仍在对应管理页编辑，不增加新的 Agent 工具，
不把商店目录放进模型上下文。网站业务全部属于统一的 xadmin `mdo` 插件。

## 用户流程

- 设置 → 扩展能力 → 扩展中心。浏览、安装、发布都要求登录 ai.xywhsoft.com。
- 发现：按作者、名称、用途搜索，按资源类型筛选，详情显示说明、许可证、平台及源码。
- 新建插件：勾选本地资源，填写名称、ID、版本、描述、Markdown 说明、许可证和实测平台；
  可填写更新说明、预览、导出源码包，或直接提交审核。默认 Agent 须先复制为自定义 Agent。
- 本地草稿：输入后自动保存，不完整表单也能保存，重新打开继续编辑。投稿后仍保留草稿。
- 我的发布：查看审核状态、退回原因、版本历史及更新说明，发布新版本或撤回指定版本。
- 已安装：管理资源、编辑插件包、检查更新、卸载。资源编辑页可返回所属插件。
  没有自动更新 C 代码，每次安装均需明确确认信任。
- 导入插件包：无需连接商店，安装本地 `.mdo-extension.json`，仍执行平台、冲突和代码信任检查。
  本地导入独立登记，不冒充已审核插件或网站作者；登录会话要求仍适用。
- 网站 `/mdo/ecosystem` 支持登录、账号显示、退出、查看、下载、上传、撤回及版本历史；
  首页和账号页均有入口。网站不会运行扩展代码。

## 最小源码包

文件名为 `<slug>-<version>.mdo-extension.json`，普通 UTF-8 JSON，可直接审阅或导出。
生态源码包没有另加 ZIP 解压器或依赖解析器；二进制工具包继续使用原来的 XRTPACK。

```json
{
  "format": "mdo.extension.v1",
  "manifest": {
    "slug": "project-review", "name": "项目审查", "version": "1.0.0",
    "description": "先理解项目，再提供有依据的审查意见。",
    "readme": "在命令菜单中选择 project-review，填写审查范围。",
    "license": "MIT", "platforms": ["windows-x86_64", "android-arm64-v8a"]
  },
  "resources": [{
    "kind": "commands", "id": "project-review",
    "content": "---\ndescription: 项目审查\n---\n审查 $ARGUMENTS，引用具体文件。"
  }]
}
```

支持的 kind 为 `agents/subagents/tools/skills/mcp/commands`，以及可选
`c-agents/c-subagents`。后两项对应 `modules/agents`、`modules/subagents` 的 C 文件，
保留已有 Module ABI。普通 Agent 配置无需 C。仅 Skill 允许 `files` 附件数组，
每项为 `{ "path": "references/example.md", "base64": "..." }`。

限制：源码包 JSON 至多 1 MiB，至多 16 项资源，每个文件 128 KiB，解码后的全部
源码和附件合计 512 KiB，每个 Skill 至多 128 个附件。ID 是 1–64 字节的便携小写
标识；禁止路径穿越、Windows 保留名、重复目标和隐含覆盖默认 Agent。
支持平台由作者声明，安装时检查当前设备平台；它不代替作者的实际兼容性测试。
暂不引入付费插件、运行时依赖自动安装、评分、钩子或复杂发布审批模型。

MCP 导出将凭据引用变成 `input:N` 占位符并移除工作目录；安装后保持停用，
在 MCP 管理页配置本机凭据和连接信息再启用。源码正文、命令参数等其他位置
仍由作者检查，发布界面明确提醒避免包含私密信息。

## 网站与 API

服务实现：`home/host/xywhsoft_ai/plugin/mdo/modules/ecosystem`。
共享格式校验器在 `app/include/mdo/ecosystem_package.h`，网站目录保存相同副本；
修改契约时同步两份，原生联调测试会核对字节一致性。

| 接口 | 用途 |
| --- | --- |
| GET `/api/v1/mdo/ecosystem/packages` | 已审核公开列表；q/kind/before 分页，mine=1 读取本人投稿 |
| GET `/api/v1/mdo/ecosystem/package?id=N` | 详情和源码；待审核、退回、下架版本仅作者可见；latest=1 返回同一作者、同一 ID 的最新公开版本 |
| POST `/api/v1/mdo/ecosystem/submit` | 上传源码包，初始状态 pending |
| GET `/api/v1/mdo/ecosystem/history?id=N&before=N` | 同一作者、同一 ID 的版本历史；其他会员只看到公开版本 |
| POST `/api/v1/mdo/ecosystem/withdraw` | 作者撤回指定版本；提交 id/revision，不能覆盖或删除源码历史 |
| GET/POST `/admin/api/mdo/ecosystem` | 查询、approve/reject/hide；提交 id/revision/action/reason |
| `/admin/mdo/ecosystem` | 后台“墨斗管理 → 生态资源”，要求 mdo.ecosystem.review 权限 |

所有会员 API 都经过原有身份系统。Cookie 写入另须 X-CSRF-Token；原生客户端使用
现有账号 Bearer lease，令牌不进入前端。匿名只能看到网站登录页。作者来自已验证的
会员身份，不能上传冒充作者。每人最多 200 个版本、24 小时内最多 20 次有效投稿。
版本 `(owner,slug,version)` 不可覆盖；审批使用 revision 防止覆盖另一管理员的决定。
新版本审核期间，旧公开版本继续展示。下架针对单个版本，较旧公开版本仍可展示。

私有数据在 `plugin_data/mdo/ecosystem/{catalog.db,objects/}`。对象按 SHA-256 保存，
只经身份和可见性检查后的详情接口返回，不放到匿名 `/mdo/blob`。

## 安装事务与生命周期

客户端 `/api/v1/ecosystem` 将请求转给当前账号的网站：GET 返回本机安装记录；
POST action 为 catalog/mine/detail/latest/history/submit/withdraw/install/import/uninstall/export/c_sources，
以及 drafts/draft_read/draft_save/draft_delete。本地导入及草稿操作不会调用网站。
联网操作由有界线程池执行，拥有连接、请求和账号引用；卸载应用时先取消并等待。

安装按现有资源解析器校验 → 检查平台/哈希/明确 C 信任 → 检查本地冲突 →
保存回退日志 → 写入全部文件 → 重载 Module/Skill/MCP → 保存安装记录 → 删除日志。
失败恢复全部旧文件及记录；启动时先恢复未完成事务，再加载任何扩展代码。
资源管理器使用 Home 持有的应用 VFS，避免工作线程读到缺少便携覆盖层的进程 VFS。

安装记录为 `mdo-home/data/extensions/installed.json`，未完成事务日志为 `pending.json`。
草稿在 `mdo-home/data/extensions/drafts/<id>.json`，只保存表单及资源引用，源码仍由本地
资源管理器维护。每台设备最多 64 份、每份 64 KiB；更新和删除必须携带当前 SHA 修订值。
并发修改返回 412，界面允许另存为新草稿。尚未输入内容的新建空表单不会自动写文件。
本地包安装记录使用 `local:<slug>`，与网站数字 ID 分离，避免撞库或覆盖网站版本。
更新不覆盖本地修改或已删除的文件，也不覆盖其他插件的同名资源。
卸载保留修改过的文件；Skill 任一已登记文件有修改时保留整个 Skill，防止附件失去
SKILL.md。回退失败时要求重启，不继续发布可能不一致的资源状态。
用户批准 C 扩展意味着信任源码：它在墨斗进程中执行，编译注册阶段也可能执行代码。

## 验证与部署

```powershell
python tools/build_mdo.py --xserver-root .build/implementation-xs --prepare-only
python tests/test_ecosystem_runtime.py
node --test tests/test_store_formats.mjs
node --test tests/test_store_draft.mjs tests/test_source_markdown.mjs
python D:/GIT/home/tests/test_mdo_ecosystem.py
```

联调用独立临时 vhost，只读备份网站主库，不操作真实会员。覆盖登录、投稿、未审核
隔离、审核权限/CSRF、版本冲突、恶意路径、安装/更新、C 编译失败回退、Skill 附件、
本地修改保留及崩溃日志恢复。`--ui` 保留临时页面供人工检查桌面和手机布局。
不进行压力和高负载测试。

首个公开资源为“项目审查入门包”，源码位于
`examples/extensions/project-review.mdo-extension.json`。它组合只读 SubAgent、按需读取的
Skill 和命令，不含 C 或外部服务；作者显示会员昵称，示例可直接用于准备新的投稿。

部署更新统一插件源码、页面及网站入口。先用服务器实际 xs 在隔离站点运行联调，再停止服务、
备份相关代码、主库和生态库、原子替换选定文件并重启。保留线上身份密钥、账号、余额、
搜索/模型配置以及既有发布目录；生态库 v1→v2 为事务内添加字段，保留旧版本数据。
失败恢复代码和数据库。Markdown 仅用 DOM 创建文本与元素，不执行 HTML，不加载图片。
