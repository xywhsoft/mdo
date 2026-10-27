# 九类扩展统一目录与 ZIP 分发：源码核对和实施清单

日期：2026-10-27。状态：正在分阶段实施，进展与验证见 `docs/build-records/2026-10-27-extension-directory-rebuild.md`。本文完整替代上一版方案。用户已明确不保留旧发布格式、旧目录及旧客户端兼容机制，不以版本号和能力构建号阻碍此次改造。

## 固定规则

- 扩展唯一的安装位置是当前 mdo Home 下的“类型/扩展目录”。
- 本机创作目录可以使用名称；平台作品以 XID 作为安装目录名。
- 发布物是整个扩展目录的标准 ZIP。ZIP 条目相对于扩展目录，根部直接是扩展文件。
- 安装到 Home/类型/XID，卸载删除这个完整目录。
- 不增加 payload、files 或 revisions/hash 之类的发布/安装包装层，不将文件散写到其他类型或 modules 目录。
- 发布和传输真实 ZIP 字节；JSON 仅用于配置、元数据和小型控制请求，不用作文件内容或压缩包的 Base64 容器。
- 不保留 mdo.extension.v1/v2、MDOCLI1、旧组合包分发、min_build_id 和格式能力协商分支。平台/架构匹配、文件完整性和代码信任仍是运行所需的检查。

建议固定九个类型目录：agents、subagents、tools、mcp、skills、commands、external-tools、knowledge、plugins。external-tools 在本机、SDK、平台和网页中使用同一类别名称，取消 cli/toolpacks/modules/c-agents 等作为额外生态分类的混用；C Agent 和 C SubAgent 仍属于 Agent/SubAgent。

示例：

~~~text
mdo-home/
  mcp/
    cua-driver/                 # 本机创作目录
      extension.json
      mcp.json
      bin/driver.exe
      lib/...
    <平台作品XID>/              # 从平台安装
      extension.json
      mcp.json
      bin/driver.exe
      lib/...
~~~

ZIP 根直接包含 extension.json、mcp.json、bin/、lib/。不再套一层 cua-driver 或 XID，否则会错误解包为 mcp/XID/cua-driver/。

## 检查发现的现状

| 类别 | 当前存储/加载方式 | 必须改变的内容 |
| --- | --- | --- |
| Agent | agents/id.md、modules/agents/id.c，附件还可在 agents/id/ | 配置、C 程序和提示词归入 agents/目录；从目录入口加载 |
| SubAgent | subagents/id.md、modules/subagents/id.c，附件独立 | 同 Agent；消除配置与代码分属两个目录的方式 |
| Agent 工具 | tools/id.c 或 modules/tools/id.c | tools/目录中放 C 入口、头文件和资源；编译时提供本目录 |
| MCP | mcp/id.json 是连接，商店模板在 data/ecosystem/mcp/id/hash/template.json | mcp/目录中保存服务模板与程序依赖；连接实例不再混作扩展目录的单文件定义 |
| Skill | skills/id/SKILL.md 已有目录，但还有项目及其他 Agent 兼容扫描 | 安装目录接受 XID；调用名称独立；主动导入来源和运行时安装位置分开 |
| 编辑器命令 | commands/id.c、项目 .mdo/commands/id.c；子进程只复制入口源码 | commands/目录中保存入口和依赖；子进程能访问目录内头文件/资源 |
| 外部工具 | data/cli-tools/installed/任务ID，MDOCLI1 归档，独立状态/转发器 | external-tools/XID；标准 ZIP；转发器和 PATH 登记按扩展归属更新 |
| 知识库 | knowledge/id/knowledge.json 与文档 | 基本目录形态可沿用；统一 XID、发布、索引及卸载接口 |
| 插件 | plugins/id/revisions/hash/files，另存配置和选中修订 | plugins/XID 的单一目录；覆盖映射仍走 VFS；删除旧实体修订树依赖 |

源码入口：

- app/src/api/extensions.c: ExtensionPath、ExtensionReload、ExtensionWrite、目录导入/删除。
- app/src/modules/manager.c: MdoModulesDiscoverDirectory/Discover、编译来源、SourceHash、Agent 注册。
- app/src/commands/catalog.c、manager.c 和 app/command-worker.c: 发现、别名、源码解析、子进程与编译。
- app/src/mcp/manager.c、scopes.inc.c、app/src/api/mcp_connections.inc.c: 服务发现、项目连接、模板绑定和秘密引用。
- app/src/skills/manager.c、scope.inc.c、app/src/knowledge/manager.c: 文件目录发现和作用域。
- app/src/application/plugins*.c、plugins_*.inc.c 和 bootstrap/plugin_bootstrap.inc.c: 插件文件根、覆盖映射、加载和重启。
- app/src/api/ecosystem*.c、ecosystem_*.inc.c、extension_store.inc.c: 发布、安装事务、来源管理和卸载。
- app/src/distribution/cli*.c、cli_*.inc.c: 外部工具归档、发布、下载、状态与转发器。
- 两个仓库的 ecosystem_package.h、cli_package.h，以及前端 store-formats.js: 旧包类型、版本门槛和内嵌文件协议。

## 容易遗漏且必须一起改的行为

### 安装标识与调用名称分开

当前 MdoExtensionIdValid 仅允许小写字母等字符；Skill 还要求首字符为小写字母。XRT XID 是 32 字符标识，不能继续用这些名称校验器代替 XID 校验。

命令元数据还要求源码中的 id 与文件名 id 相等，Agent ID、Skill 名称及路径也从文件/目录名派生。安装目录改名为 XID 后，不能把 /命令名、模型工具名或角色名一起机械替换成随机目录名。

平台生成作品 XID，并绑定作者及类别；更新沿用作品 XID。每次上传/审核的摘要和任务 ID 用于区分内容与操作，不再产生一棵永久修订目录。平台身份不能由 ZIP 内自报的作者或 XID 决定。

### 所有读写入口使用同一目录解析

需要一个按“类型、安装标识”返回扩展根、入口及归属的共用层，九个运行时、编辑器、SDK、远程资源调用都复用它。新建、编辑、复制、导入、发布、更新、卸载不能各自拼接历史路径。

可在目录根使用一个小型 extension.json 描述类别、显示信息、调用别名、入口及许可；类型配置和程序文件保持普通文件。目录内资源引用以扩展根解析；本机绝对路径不进入发布物。

启停、设备绑定、安装来源、秘密和宿主业务状态存放在宿主数据/凭据区域，不混入可分发源码目录。SDK 应提供当前扩展根和相对资源访问，不能要求每位作者自己猜 Home 路径。

### C 代码与资源需要真正跑通

现有命令执行器只将入口内容写到临时 command.c，并把编译包含目录设为子进程工作目录；仅把文件解压进去不足以支持 #include "helper.h" 和读取同目录资源。命令的编译来源、包含目录和执行上下文要共同调整。

模块 SourceHash 目前主要包含入口源文、SDK 头和编译标识。目录内辅助头文件修改也必须使编译/信任/加载缓存失效，不能继续执行旧内容。目录摘要或依赖摘要需要绑定整个扩展实际使用的内容。

内置命令、默认 Agent、C 实现和 skill-creator 等资源也要改成单扩展目录，并调整打包脚本。不能只让商店采用新目录，内置继续走散文件扫描。

### 项目作用域与第三方导入

当前存在项目 .mdo/commands、.mdo/mcp，以及 .claude/.agents 等多个 Skill 扫描来源。按新的安装规则，作用域只决定哪些 Home 扩展对某项目启用及其参数，不能继续形成第二套安装根。

导入其他程序已有配置/Skill 是有用的功能，可保留为显式导入：导入完成写入统一 Home 目录。旧路径不能作为不经安装管理的常驻运行时后门或启动迁移来源。

### 卸载与更新必须满足完整目录语义

当前安装器逐文件写入，还把每个文件的新旧内容 Base64 放入回滚日志；卸载会保留被本地修改的 Skill 文件以及插件修订、被连接引用的 MCP 模板。这些逻辑均不符合整目录卸载。

改为先注销扩展的运行时入口、关闭相关进程或等待必要重启，再删除完整根目录并清理目录对应的登记、转发器和派生缓存。用户聊天、项目和账号数据不属于扩展安装文件。不能显示卸载成功后还保留可发现、可执行的旧扩展。

更新在宿主任务区暂存新目录，完成校验后替换同一个 类型/XID 目录。事务日志只记录归属、路径、摘要及阶段，不再内嵌所有文件。旧目录仅在未完成事务中暂存以便失败恢复，完成即清理，不建立永久版本树。

需要取消启动时旧命令迁移、外部工具 legacy 自动补登等逻辑，验证卸载/停用后重启不会自动复活。

### 插件和外部工具的特殊接线

插件的前端入口、原生入口和覆盖源文件均来自 plugins/XID。对核心文件的覆盖继续通过 VFS 映射完成，不能物理散写到其他扩展或 app 文件夹。互斥、依赖存在和安全加载机制继续服务于功能，旧修订回滚/版本区间兼容机制不纳入新方案。

外部工具手工添加应把所需实际文件导入自己的扩展目录。原来只登记任意系统 EXE 绝对路径的方式不能作为可发布扩展；普通系统命令仍可由 exec 尝试使用。转发器全局入口按扩展登记生成和注销，目标使用相对路径。

Android 完整版的原生执行器目前依赖 APK 预装位置，需要同时检查构建、注册和调用链；统一资源目录不能通过下载 ELF 后直接在 Home 执行来实现。必须保留 Android 既有执行限制并单独验收，不能用桌面结果替代安卓结果。

## 平台与创作中心改造

1. 统一九类作品的 XID、归档对象、文件目录、审核和安装数据模型。当前 package/tool 两条链路及 source=cli/package 分支要收敛，不再分别维护 JSON 包和 MDOCLI 包。
2. 服务端提交与封存校验改为真正读取 ZIP。旧 CreatorPackageFile 直接 JSON 解析及 CliCheckArchive 的 MDOCLI_MAGIC 校验退出新链路。
3. 上传分片目前仍是 JSON data/btoa/Base64，需要客户端、创作中心和服务端同时改为原始二进制分片。仅把归档改为 ZIP 而继续塞入 JSON 不算完成。
4. 客户端下载可复用现有带进度、取消及摘要核对的文件传输能力；目录和详情接口只返回小元数据与文件索引，归档下载是 application/zip。
5. 创作中心、网页商店、九类后台页和客户端详情不再假设 resources[].content/files[].base64，不再 await artifact.text() 后解析整个作品 JSON。文本源码按文件预览，原文件按文件下载，大归档不阻塞详情。
6. 网页 MCP/知识库构建器同样输出 ZIP 或让服务端从受控目录生成 ZIP，不能继续产生一个特例 JSON 作品。
7. 保留账号、创作者资格、数量/容量额度、审核权限和审核资料的功能；旧协议兼容、旧组合包入口、隐藏 modules 分类及对应迁移分支直接退出。

生产只读核对（2026-10-27 12:02，上海时间）：三个平台各有 13 个已公布外部工具，共 39 个归档，头部全部为 MDOCLI1，没有 ZIP；对象均存在。两个目录库还保留旧组合资源记录，其中有一个已公布组合包。没有使用者不代表没有需要处理的官方分发内容。

上线时应以官方文件原件重新生成独立目录 ZIP，核对每个文件内容和许可，重新登记 XID；旧组合包和旧格式作品/下载入口下线。不建设旧客户端兼容通道，也不直接把旧记录的后缀改成 .zip。旧数据先做针对性备份；不清空包含账号、会话或计费的主库。

## ZIP 与目录校验

客户端和平台共用归档、遍历及路径规则，避免重复出现“本机能保存但平台不能打包”的分歧。现有普通路径校验拒绝点开头的目录，会挡住 .bin/.venv 等依赖；隐藏文件、空文件、空目录、多层目录和可执行位需要明确支持。

用原生归档能力结合现有 xrt 压缩、CRC、摘要和文件接口完成，不依赖用户预先安装 Python、7-Zip 或 unzip。必须校验真实 ZIP 结构、路径归属、重名/Unicode 冲突、文件与目录冲突、条目类型、解压总量、大小、文件数、校验和及权限，限制超量解压并避免整包驻留 JSON 对象。

链接不能成为读写扩展根之外文件的方式。目录内链接需要明确展开规则和循环检测；无法安全打包的条目须报出具体位置，不能静默漏文件。上传、解压和卸载均使用受根约束的文件接口。

## 实施顺序与完成标准

这是同一轮改造，四步是验证和提交边界，不是长期并存的新旧体系。

1. 统一类型、XID/别名、目录/入口解析、目录遍历与 ZIP 能力；定下 SDK 扩展根及文件归属契约。
2. 九类本机编辑、发现、真实运行、信任、状态和整目录安装/更新/卸载全部接入，内置资源和构建流程同步。
3. 平台、原始二进制上传下载、创作中心、商店及后台一起切换；重新打包官方内容，旧协议和路径正式退出。
4. 完成下面矩阵并完成一次线上投稿、审核、下载、解压、使用及卸载核对后，才宣布完成。

验收必须覆盖：

- 九类分别完成新建、编辑、打包、投稿、审核、商店安装、真实调用、更新、卸载。
- ZIP 用普通工具打开；归档根与目录文件逐一对应；安装后只有 类型/XID 一套有效扩展文件。
- 命令和 C Agent 工具能包含本目录头文件、读取附属资源；只修改辅助文件也会更新实际执行内容。
- MCP 自带程序及依赖完成协议交互；同一服务多连接的参数、凭据不会进入 ZIP；卸载后进程和旧路径引用清理正确。
- Skill 名称、命令别名、工具名称和角色引用在物理目录改为 XID 后仍能使用。
- 隐藏/空/深层文件、较多文件、大文件、路径与 Unicode 冲突、损坏归档及超量解压的边界。
- 卸载后目录不存在、列表和 SDK 入口消失；重启后不复活；插件覆盖和外部工具转发器不再残留。
- 更新中断、取消、磁盘不足、进程占用和重启恢复不留下半安装目录。
- Windows、Linux 和 Android 按各自实际运行机制验证；Android 完整版的预装程序和相关资源单独回归。
- 对旧格式和旧入口的拒绝测试，确保没有后台继续接收 JSON/MDOCLI、客户端继续读老目录的第二条路径。
- Home 迁移/恢复、现有远程管理、SDK 使用本地资源及 Mohists 的既有功能回归，不增加调试追踪平台。

本次核对收据位于 .build/extension-directory-audit-20261027。生产切换须在九类接线和全链路验收之后完成。
