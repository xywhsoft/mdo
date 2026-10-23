# mdo 0.5.0 面板与浏览器 SDK

右侧工作区共用 Application SDK。C 使用 `<mdo/workbench.h>`，JS 使用
`sdk.panel/resource/browser` 或 `sdk.call`。接口描述、C 便捷函数与 JS 包装由
`tools/workbench_contracts.py` 同源生成，`sdk.describe` 返回参数契约。

## 界面与面板实例

窗口右上角「＋」或 Ctrl/⌘+Shift+B 打开新标签页。支持标签复用、拖动排序、
固定、宽度调整、并排、展开与折叠。窄窗口占满工作区，折叠后恢复对话输入区。
标签也可用方向键切换，Delete 关闭。未保存的编辑内容阻止关闭或切换设备，
隐藏的脏编辑器不会被保活预算回收。常规隐藏视图最多保活 8 个，实例上限 64。

新标签页目前提供浏览／搜索、网页预览、文件、项目 Git 变更及插件入口。
默认四入口（浏览器、文件、终端、变更）的实施进度见 workbench-implementation.md。
标签栏的固定、并排、展开和收起已合并到省略号菜单。
Git 视图展示 `git diff HEAD` 的已跟踪文件变更，依赖目标设备的 Git；超过
进程输出预算时明确显示前 16 KiB，不把截断视为完整审查。它不是文件树或 Git IDE。

面板身份由 owner、设备、项目、会话、type、key 与 client 组成。相同 key
重复打开返回同一实例；省略 key 创建独立实例。context 在创建时冻结，挂载回调的
SDK、后续子面板与文件编辑均继承该 context，不随对话导航改变。

`client` 必须明确提供，可从 `ui.clients` 选择。不能默认挑第一个窗口。
`panel.open` 的 `reveal: activate` 请求展示，`background` 保持当前焦点；
也可单独调用 `panel.reveal`。新实例及更新通过轮询同步，暂时断线保留已有画面。

布局保存到展示端 Home，按窗口配置和目标设备分开，使用修订前置条件；
不把布局写入远端项目。恢复只能接管已过期的 client，不能抢走另一个活跃窗口。
64 个恢复引用、宽度、排序和视图状态是轻量布局；实际内容保存在独立资源中。

## JS 接入

```js
export function activate(sdk) {
  const remove = sdk.ui.registerPanel("sample.plan", {
    title: "我的计划", placement: "right", renderer: "plan", scope: "session",
  });
  const open = async () => {
    const resource = await sdk.resource.put({
      revision: 0, title: "实施计划", kind: "plan",
      value: { items: [{ text: "准备", done: true }, { text: "验证", done: false }] },
    });
    return sdk.ui.openPanel("sample.plan", { key: "main", resource: { id: resource.id } });
  };
  const menu = sdk.ui.registerMenu("sample.plan.open", { label: "实施计划", execute: open });
  return () => { menu(); remove(); };
}
```

自定义面板使用 `mount(root, {sdk, context, params, resource, signal, events,
addContext, update, setDirty})`，返回清理函数或含 dispose/visible/dirty 的对象。
`events` 发送 visibility 和 resize。挂载 SDK 是固定上下文的访问接口。
不要在面板中改用全局 mdo 来执行属于旧会话的写入。

C 注册类型可以指定 `renderer: custom, frontend: sample.renderer`。
客户端插件只需 `sdk.ui.registerRenderer("sample.renderer", (root,value,options)=>...)`，
无需以另一个 owner 再次注册同名类型。宿主读取资源后调用渲染器，并传入资源 owner 的
绑定 SDK。更新资源时仍需提供修订号。

旧 `registerPanel` 不指定 `placement: right` 时保留模态窗口行为。
`openEditor` 已接入文件工作台，在打开时解析绝对路径，使用绑定设备的文件服务。
文件页支持目录树/搜索/分页、行号与着色、范围预览、Markdown/图片、代码引用与编辑草稿。
文本编辑上限 1 MiB，保存前核验文件修订并保留 UTF BOM/端序/换行；外部变化保留编辑并提示比较。
后台文件预览不自动添加模型上下文。大日志使用有界文本缓存和虚拟行视图。

## C 接入

```c
#include <mdo/workbench.h>
/* Context 来自 Command->Application、ToolContext->Application 或 Host->Application。 */
xvalue* Result = NULL;
mdo_error_v1 Error = {0};
mdo_result Code = mdoResourcePut(Context, Params, &Result, &Error);
/* Params 借用；Result 归调用方，通过 Context->Api->Values->Release 释放。 */
```

完整可编译命令见 `examples/application-sdk/workbench-plan.c`。没有 client 参数时
只创建资源；传入明确 client 时注册标准计划面板并打开。显示失败不作废已保存的资源。
自定义 JS 渲染器示例见 `examples/application-sdk/workbench-panel.js`。

## 服务与持久化边界

| 服务 | 用途 |
| --- | --- |
| panel.types/register/unregister | 类型目录与显式移除 |
| panel.open/list/get/update/close/reveal | 实例与视图控制 |
| panel.attach/browser.attach | 旧 UI 租约过期后的恢复绑定 |
| panel.report | 绑定窗口上报 visibility/尺寸等临时状态 |
| panel.layout.get/put | 展示端布局与 CAS |
| resource.list/get/put/remove | 带 context 与 revision 的持久资源 |
| browser.capabilities/create/list/get/update/close | 指定显示窗口的浏览器会话 |
| browser.command/result/release/report | 动作回执、释放与临时状态 |

`resource.put` 创建时 revision 为 0，更新和删除必须携带当前 revision。
同 owner 的其他上下文可以显式读取资源，写入必须绑定原 context。
内置适配器包括 document、plan、diff、web、browser、surface、editor。
计划资源使用 `{items:[{text,done}]}`；文档为字符串或 `{text}`；差异为
`{files:[{path,patch}]}`。文档作为文本渲染，不执行资源中的 HTML。
选中内容可引用到对话；未选中时引用整个资源，仍受正常输入大小限制。
打开面板和资源更新本身不进入模型上下文。

工作区原子文件为 `data/workbench.json`，总预算 4 MiB；类型、资源各最多 128，
面板与浏览器记录各最多 64，窗口布局最多 32。单资源 256 KiB，单布局 128 KiB。
文件损坏时保留原文件并返回明确不可用状态，其他聊天能力继续运行。
运行状态 report 每条最多 4 KiB，只保留在内存，通过 workbench.changed 提示订阅方；
重启后需要重新挂载，不能把旧 report 当成活动连接。

插件停用、移除或更换 revision 后，旧 UI 贡献及其当前窗口的实例会清理；
新版 UI 代码需要重新载入。持久资源保留，避免卸载界面时删除用户内容。
C 原生模块应在自己的 Stop/Unregister 生命周期显式注销贡献。
原生插件仍是受信任进程内代码，面板错误处理不能隔离 C 崩溃或主线程死循环。

## 浏览器会话与动作

Windows 使用独立 WebView2 内容环境，浏览数据位于主界面 profile 的 `-content`
目录，复用系统运行时。内容页没有宿主绑定，普通导航限制 HTTP(S)。
`target=_blank` 目前导航到当前内容视图。主文档桥接核验原始 document 与消息来源。

Android 使用独立内容 WebView，通过只交给可信主文档的 WebMessagePort 控制，
不向 iframe 或内容页注入 Java 对象。Android 同进程 WebView 共享平台 Cookie 存储，
能力报告 `profile_isolation:false`，不能宣称提供 WebView2 那样的独立配置文件。
内容页与 mdo 的原生桥接隔离，网页登录数据由平台 WebView 持久化。

浏览器执行位置明确为 `display-client`。控制远端 mdo 时，原生浏览器仍位于显示窗口
所在设备。远端 localhost 地址不会被偷偷解释为本机同名端口；需插件提供服务映射
或可访问 URL。本版没有通用跨设备端口隧道。外部浏览器／当前无原生后端的平台会报告
不支持完整浏览；网页预览可用受限 iframe，仍受站点嵌入策略及 sandbox 限制。
远程 Application SDK 与工作区控制使用控制模式，查看模式不开放写服务。

```js
const session = await sdk.browser.create({ client: selectedClient, url: "https://example.com" });
await sdk.panel.open({ type: "mdo.browser", client: selectedClient,
  resource: { id: session.id }, reveal: "activate" });
const receipt = await sdk.browser.command({ id: session.id, command: "snapshot" });
// 读取 browser.result({id:receipt.id})，等待 succeeded/failed/cancelled；
// succeeded.result 才是窗口执行结果。不要把 pending 当成执行成功。
```

命令支持 navigate/back/forward/reload/stop/state/wait/snapshot/screenshot/click/type/scroll/
evaluate/takeover/release。snapshot/evaluate 返回 `{ok:true,value:...}`；
wait 等待页面就绪或 selector 可见，timeout_ms 最多 60000，并检查原操作的取消。
无需先打开标签即可创建隐藏浏览器执行操作；显示窗口及其原生后端仍必须在线。
截图返回 `{data:base64}`（JPEG）。选择器不存在、后端不可用、结果超限等明确失败。
读取终态结果后调用 browser.release（或 ui.release）释放回执；待执行动作不能释放。
UI 回执池最多 128，租约十分钟，不应通过持续堆积回执来管理长任务。
evaluate 接受 JS 表达式，适用于受信任扩展，不是网页可调用的宿主接口。

接管后 Agent 的变更操作被阻止，用户的地址栏和直接输入继续可用；交还后恢复。
关闭面板只隐藏浏览器，后台会话保留；「结束浏览」或 browser.close 才销毁实例。
显示窗口退出会销毁原生实例；持久记录重启后为 suspended，重新挂载重建页面。

UI 发送前记录意图，执行后 ACK，结果不确定时不自动重放。动作继承原调用的取消与
期限；已开始的副作用无法撤销，调用方必须检查实际状态。SDK HTTP 请求预算 2 MiB、
响应 6 MiB；单 UI 结果最多 1 MiB，页面意图日志不保存大截图，实际结果保留在服务器回执。
取消页面请求不代表取消已执行的点击。能力必须向指定 client 查询，不能根据操作系统猜测。

## 操作画面与输入通道

surface 参数包括 stream、width、height，可选 input_stream。画面协议为
uint32 little-endian 编码字节数，随后是一张浏览器支持的编码图片；单帧最多 1 MiB。
流句柄和字节预算沿用 stream.*。生产者应在 panel.report 可见性变化后降频或暂停，
处理 would-block；不在模型回调里阻塞等待。隐藏视图暂停解码，关闭视图不释放插件拥有的流。

用户点击「开始控制」后，输入通道发送 NDJSON，包括 acquire、pointerdown/move/up/
cancel、keydown/up、wheel、heartbeat、release_all。每条包含 epoch、递增 sequence
与 1500ms lease_ms；坐标映射到声明的画面尺寸，pointermove 可以合并，其他输入有界排队。
退出、隐藏、失焦和断线停止控制；驱动必须校验租约、拒绝过期 epoch/sequence，并在租约
过期时释放所有按键与指针。该接口不代替插件的系统授权和实际输入驱动。

## 内置变更与文件服务

文件页通过 `files.directory/search/read/write` 读取所属设备的项目目录；目录按页读取，
搜索有时间与条目预算，保存携带文件修订。`mdo.files` 打开目录，`mdo.editor` 的 params
可传 `path`、`line`；定位按需继续读取，达到预览预算后明确提示，不能伪称已到达目标行。

变更页 `mdo.changes` 可传 params.repository；其 SDK 为 `changes.list/branches/diff`，
C 的 `mdoChangesList/Branches/Diff` 与 JS 的 `sdk.changes.list/branches/diff` 共用线协议。
比较范围：workspace=HEAD 到当前工作区（含未跟踪），staged=HEAD 到暂存区，
unstaged=暂存区到工作区，branch=选定基准提交到 HEAD；分支比较不混入未提交内容。
session 仅允许真实已捕获的会话基线，目前缺少基线返回 git_baseline_missing，不能退化成 HEAD。

列表返回 repository/head/base/scope/files/revision，文件包括 path、old_path、status、
additions/deletions、stats_available、binary。统计超出预算时显式标记，不将未知计作零。
diff 传 path、相同比较参数、revision、offset、limit（最多 2000 行）；返回准确原/新
行号、总行数、next/more 和所请求页。跨页前后比较修订变化返回 409，调用者应刷新列表。
单页最多 512 KiB、每行最多 16 KiB，长行和非 UTF-8 有明确标记。读取超时 15 秒；
不调用外部 diff/textconv，不以用户索引作临时空间。大量结果可通过 operation.start/result
异步读取；视图取消和回执释放只针对自己的操作，不能取消其他面板。

暂存删除后在原路径重建的文件合并为一个工作区条目（worktree_recreated=true，status=D?），
比较 HEAD 到实际重建内容，而非只展示删除。此边界使用 Home 中独占目录内的临时 HEAD
索引，完成或失败均清理；不替换用户索引或修改暂存状态。重建为相同内容时净差异为零。
该临时比较也限制 HEAD 条目不超过 20,000、条目元数据不超过 4 MiB，并共用 15 秒期限。

变更页面提供右侧可筛选/折叠的虚拟文件树、逐文件分页、统一/左右对照、差异块/行引用，
打开工作区文件时定位到所选新行。引用正文携带仓库、范围、提交和比较修订，明确其来源。
分支历史差异中的“打开工作区文件”显示当前磁盘内容，不冒充历史文件。

## 交互终端服务

`terminal.*` 由 xrt PTY 执行真实程序；C 的 `mdoTerminalCreate/Attach/Read/Write/...` 与
JS 的 `sdk.terminal.create/attach/read/write/...` 共用同一契约。`capabilities` 返回实际
PTY 支持、默认 Shell 和预算。`create` 接受稳定 `key`、可选 program/args/cwd/title/尺寸；
直接 argv，不拼 shell 命令。默认项目首次使用时创建自己的工作目录；项目租约在目录
创建前取得，持续到 PTY 与全部 I/O 清理完毕，避免运行期间项目被替换。

每个终端的 owner/project/session/run/workspace 固定。隐藏或取消读取不终止进程；
`attach` 可以只读或为明确 client 取得 15 秒输入租约，同一 client 可续期。写入、resize
和 detach 要携带 id、服务 epoch、client、control_token、input_epoch。附着返回
next_sequence；write 按该序号提交最多 8192 字节 text 或 base64。输入收到的是队列接收
确认，不代表命令执行成功。最多 32 条输入收据用于相同 epoch/序号/字节的去重；不同
内容复用序号、跳序、过期收据或租约均拒绝，不换新序号盲目重发不确定输入。

输入队列 64 KiB，另有最多 8 KiB 正在写入；队列满返回 429，尚未接收该块。消费者应
保留未接收数据并等待，不能无限堆积。终端 Enter 通常是 `\r`，Ctrl+C 是 `\x03`，
不要将终端输入当成文本文件换行来转换。VT 二进制输入可原样使用 base64。

输出是每会话 1 MiB 的有界环形缓存；最多 8 个运行会话、32 个保留记录。`read` 按字节
cursor 读取（最多 64 KiB，等待最多 1 秒），互不消耗其他观察者的数据。返回 offset/next、
oldest、gap、eof、drained。慢消费者的旧内容会覆盖，gap 必须明确告知；不能把任意尾部
VT 数据声称为完整屏幕恢复。前端应等待终端解析/渲染确认后再拉取下一块。

`close({id,epoch})` 明确结束终端，`get` 的 drained 为真后可 `release` 释放缓存和记录。
eof 只表示输出读取结束，drained 才表示全部回调、原生 PTY、项目租约已清理。
服务停止先中止并等待终端回调，再释放项目管理器和脚本代码；POSIX 主端采用非阻塞
I/O，处理 AGAIN 和短写，避免满输入在子进程退出后仍阻塞。

只持久化元信息，不保存 argv、输入、输出或控制令牌。服务重启将原记录标为 ended；
reason 区分 service_stopped / service_restarted / process_exited / closed /
not_started / cancelled_before_start。不会重放原程序或输入。启动失败返回具体错误，
已接收的创建记录仍可用 list 按 key 查找。独立哈希创建收据在 release 后仍保留，旧 key
返回 terminal_intent_retired，不把迟到重试当成新终端。新终端必须用新 key 明确创建。
损坏的元信息或收据保留原文件、拒绝不确定的创建，不自动清空并重启程序。

内置终端页面使用离线打包的 xterm.js 6.0.0、fit/search/Unicode11 插件。来源、精确版本、
归档完整性、发行文件 SHA-256 和 MIT 许可见 app/web/assets/terminal/manifest.json。
`python tools/vendor_terminal.py` 离线核对；显式 `--fetch` 可复现这些文件，不安装 Node/npm。
[输入、二进制输入和写入回调](https://xtermjs.org/docs/api/terminal/classes/terminal/)
与序号、字节游标和背压配合，渲染确认后继续读；本机不会持久化终端屏幕或输入队列。

页面提供会话选择、新建、重新连接、明确结束，以及菜单中的查找、复制/粘贴、引用选择
内容、字体大小、刷新列表和释放已结束记录。隐藏/关闭标签只释放自己的输入租约；
进程保留。输入租约使用每次附着独立的身份，其他页面只能只读观察。布局仅记住 ID 和
字体大小，重新打开从服务保留缓存读取；输出缺口明确提示，不能冒充完整 TUI 恢复。

前端输入事件先整体编码成 UTF-8，再分块顺序发送；本地未确认队列上限 64 KiB。
仅对明确未接收的背压回执重试原块。网络错误、租约变化、回执不一致均暂停输入，
清除待发尾部，要求用户检查输出并重新连接；不在新租约下自动重发不确定命令。
测试粘贴使用隔离 ClipboardEvent，不读写用户系统剪贴板。真实系统输入法仍需设备回归。

## 验证范围

默认项目尚无任务时，`files.resolve({path:".", prepare_default:true})` 可显式初始化
Home 内的默认 workspace。只接受默认项目的当前根目录，持共享项目租约且使用 Home
锚定目录接口；不创建其他项目或任意子路径。普通 resolve 始终只解析路径。

自动回归覆盖 C 命令工作者与 HTTP 共享资源、修订冲突、归属隔离、UI 回执、布局恢复、
损坏文件保留，以及真实浏览器的桌面／窄窗口交互。独立 WebView2 用例验证导航、输入、
点击、截图、后退、接管及内容页无宿主 SDK。Android 的 Java 外壳及 ARM64 C 编译单独检查；
真机导航、输入法、系统回收后的恢复仍需设备回归，不能用桌面结果替代。
