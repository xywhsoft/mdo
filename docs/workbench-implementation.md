# 右侧内置工具长期任务

2026-10-23 启动。保持 master、mdo 0.5.0；每项完成并验证后单独提交。
不发布官网或自动更新，不覆盖其他任务改动，不使用用户日常 Home 测试。

## 已确定的产品约定

- 默认工具只有浏览器、文件、终端、变更。网页预览属于浏览器模式，计划以后再加入。
- 标签栏保留标签、加号、省略号；固定、并排、展开、收起进入菜单。
- 文件内容为主、右侧可折叠目录树；单击预览、编辑冲突检查、文件与行号引用。
- 终端是真实 PTY 会话；隐藏不结束进程，断线重新附着，服务重启不重放命令。
- 变更提供结构化文件与分段差异；工作区、分支和有真实基线的会话比较。
- 浏览器固定工具栏、明确空白/加载/失败状态；原生视图随布局和菜单正确显隐。
- C 和 JS 共用 Application SDK；上下文冻结在所属设备和项目；内容不自动注入模型。

## 实施顺序与完成标准

- [x] 共用菜单、图标按钮和浏览器状态；Windows/Android 具体导航错误与原生视图恢复。
- [x] 文件服务与文件面板：目录/搜索、文本和图片预览、编辑、冲突及引用。
- [ ] 文件面板后续完善：新建/重命名/删除、目录变化刷新及交互细节复查。
- [ ] 结构化 Git 变更：未跟踪/重命名/二进制、大差异、比较基线、文件定位。
- [ ] xrt PTY 终端：长期会话、C/JS 契约、读写/附着/resize/close、有界输出与断线恢复。
- [ ] 新标签页四入口、插件元信息和统一布局；项目/设备上下文与原有面板恢复。
- [ ] Windows 原生与外部浏览器回归、Android 编译；生成测试程序并记录验证边界。

当前基线：538db7d8。已存在未跟踪文件 tests/test_remote_asymmetric_pause.py，不能纳入提交。
新证据放 .build/workbench-builtin-20261023/。正式回执在完成各里程碑后补充。
原生宿主参考 C:/Users/Administrator/AppData/Local/Temp/mdo-window-state-host/xs.exe；修改原生代码后必须重建宿主。
SDK 缓存使用 C:/Users/Administrator/AppData/Local/Temp/mdo-sdk-overlays，锁定来源以 deps.lock 为准。

## 进度

- 已建立长期目标，并核对源码、文件 SDK、xrt PTY 和现有浏览器框架。
- 文件 SDK 已有分页目录/分段读取/保存前修订检查；尚无完整文件工作台。
- process.run 是有期限的一次执行；不能直接作为交互终端会话。
- 浏览器现有轮询忽略 loading/error 并固定上报 ready，须先修复。

### 里程碑一：浏览器状态与共用操作栏

已实现省略号工作区菜单、键盘操作、单行浏览器工具栏及窄面板溢出操作；主页和地址栏
共用 URL/搜索规则（含 localhost 端口与 IPv6）。失败、空白、加载、接管状态可见，
浏览器真实状态同步到 broker，标题随页面更新。重试支持重建退出的原生内容进程。
Windows 返回 WebErrorStatus/导航代次/真实可见性/尺寸；重新附着恢复 bounds 和 visibility。
Android 保留导航错误码和崩溃状态，重新附着恢复布局。

已验证：8 项 JS 模型测试、20 项前端合同、6 项 SDK 合同；C/HTTP 共享状态及恢复；
Edge 全应用桌面/窄屏、菜单与键盘、插件生命周期；实际 WebView2 导航/输入/截图、
失败码、隐藏后重新附着；完整应用菜单遮挡/恢复、错误页、错误后导航、关闭面板保留后台浏览器。
Android MdoBrowser.java 使用锁定工具链 android-36 实际编译通过，未做真机回归。
Windows 新宿主已编译；最终交付将在其余工具完成后重新打包。

下一项：文件面板与文件服务；四入口启动页待文件、终端均有真实实现后统一切换。

### 里程碑二准备：文件服务

新增 C/JS 同源的 files.directory/search/resolve/stat/read/write/mkdir/move/remove。
目录分页携带修订号，目录发生变化时要求重新读取；搜索不跟随目录链接，限制扫描时间、
条目及目录队列，并明确返回 incomplete/skipped。文件写入沿用 fs.write 的原子写入及
修订检查。真实 xs/TCC 已验证分页、隐藏文件、Unicode 搜索、链接循环、二进制范围及
外部修改后的保存拒绝；接口合同回归通过。文件页面尚待接入。

### 里程碑二：文件预览和编辑页面

文件页已接入：右侧目录树、按需分页/展开/搜索、树宽度拖动与键盘调整、隐藏开关、
单击预览/双击固定文件标签、文本虚拟行号与语法着色、查找/跳转、Markdown 和图片
预览、代码行引用、1 MiB 编辑、2 MiB 文本预览缓存、8 MiB 图片上限及像素预算。
编辑草稿保留在展示端，刷新页面可恢复；外部修改提示、比较和明确放弃草稿后重读均已实现。
UTF-8/UTF-16 BOM、端序和原文件换行保留；范围读取使用流式解码，避免 Unicode 边界误判。

实际 Edge 页面已验证读写、外部冲突、重载草稿、安全 Markdown、UTF-16/CRLF 保存、
图片、26,000 行日志的范围读取和有界 DOM、搜索、查找、目录树宽度及桌面/窄屏布局。
窄屏检查发现旧 workbench 绝对定位仍指向第 3 个网格列导致仅 1 px 宽，已改为跨全部
网格列，增加实际内容宽度断言；不能只检验“宽度不超过屏幕”。

测试中曾两次出现启动失败页（一次首开、一次重载），没有 pageerror；已给文件浏览回归
添加 console/请求失败诊断和失败截图。随后流程通过，尚无具体错误原因，后续若再现需
读取 .build/workbench-builtin-20261023/files-browser/failure-diagnostics.json，不能当作已根治。

下一阶段：结构化 Git 变更与交互终端；文件管理动作继续完善，最终四入口待终端接入。

2026-10-24 后续回归修复：隐藏面板的零宽度不再触发窄屏收起目录树；从其他标签返回
保留原目录树显示状态。行号定位会在 2 MiB 预算内继续读页，而不是把超出首个 256 KiB
的目标静默钳制到已加载末行；超预算或文件没有该行给出明确提示。实际 Edge 已验证
跨标签返回及 26,000 行文件定位到第 25,000 行，原有读写/冲突/草稿回归通过。

实际完整应用→WebView2 已补测文件编辑：SDK 打开编辑器、输入/保存及 CRLF 保留通过。
相关 19 项前端单元测试、前端模块图（203 模块）及原有桌面/窄屏工作区回归通过。
文件服务提交 279ca0df，浏览器基础提交 52a23d2c；文件预览编辑提交后继续下一阶段。

后续执行参考：Windows 宿主 C:/Users/Administrator/AppData/Local/Temp/mdo-workbench-builtin-host/xs.exe。
本轮原生 overlay 为 5131a86e75ba59e587b74a2ef1aa1adcf565f0ba6a801cb086d183e065e52455。
运行文件页面：设置 MDO_PLAYWRIGHT_PATH 为
C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright，
执行 tests/test_workbench_browser.py --host <宿主> --evidence .build/workbench-builtin-20261023/files-browser --files。
原生完整应用使用 tests/test_workbench_native_browser.py --host <宿主> --evidence <目录> --application。

### 里程碑三准备：结构化 Git 读取

新增 changes.list/branches/diff，同源 C/JS 契约。读取使用 literal argv、NUL 分隔元数据，
禁用外部 diff/textconv/fsmonitor，GIT_OPTIONAL_LOCKS=0，不以用户索引作临时空间。
支持工作区/已暂存/未暂存/分支提交比较，分支明确比较选定提交到 HEAD；新仓库无 HEAD 可读。
差异按行流式分页，内存只保留请求页，服务返回准确的原/新行号、总行数和后续偏移；
长行/非 UTF-8 行明确标记。比较修订变化要求重新读取，避免把不同时刻的差异拼起来。
未跟踪文件在有界统计预算内计数，超过预算明确标记未统计，不能伪造零改动。

真实私有 Git 仓库测试覆盖 MM 的暂存/未暂存区分、重命名、二进制、中文未跟踪、literal
通配符和前导参数文件名、超过 16 KiB 的 6000 多行分页、分支、旧修订拒绝、新仓库。
读取前后核对 .git/index SHA-256 不变。会话基线尚未实现，session 范围明确报缺少基线；
不能因此将“变更”整项标记完成。

### 里程碑三：逐文件变更页面

已接入范围选择、分支下拉、增删统计、右侧可筛选/折叠/键盘操作的虚拟文件树，
逐文件分页、准确行号、统一/左右对照、差异块/行引用、打开工作区文件并定位。
比较修订过期会清空旧内容并要求刷新，不将另一修订的新页拼在旧页后面。引用正文携带
仓库、范围、提交和修订信息（不能仅放在输入上下文会丢弃的额外 metadata 字段中）。
读取使用独立 operation，视图销毁取消并释放自己的回执，不与其他标签共享取消句柄。

2026-10-24 实际验证：Edge 完整应用在 454 个变更文件中测试虚拟树、暂存/未暂存切换，
5,205 行差异翻页/远距离跳转、左右对照、引用来源、二进制、分支提交比较、外部修改后
拒绝旧修订/重新读取、窄屏及重载。Windows WebView2 实际页面验证两种范围、文件打开及
返回后目录树保持可见，实际宽度 479 px；读取前后真实 .git/index 内容不变。
22 项相关 JS 测试、20 项前端合同、6 项 SDK 合同和 206 个前端模块图检查通过。
文件服务/原生宿主无需额外修改；测试证据在 changes-browser、native-changes、files-browser。

尚需真实会话基线、连续多文件浏览及最终完整检查；“结构化 Git 变更”复选框保持未完成。
下一阶段推进 xrt PTY，随后收尾基线、文件管理和四入口主页；不发布或更改 0.5.0 版本号。

### Git 边界修复：暂存删除后原路径重建

额外检查实际复现 Git 会输出 D 与 ?? 两个同名条目，普通 diff HEAD 仅显示删除、忽略
重建内容。现合并为 D? / worktree_recreated 条目，使用独占 Home 临时目录中的 HEAD
索引比较实际工作区；用户索引不写入，临时 index/index.lock 及目录在所有退出路径清理。
Windows 不支持 POSIX chmod，直接沿用 Home 目录创建的权限/ACL；不能因重复 chmod 拒绝读取。
新增真实仓库测试验证净增删、两个版本内容、重建为原内容的零差异及临时文件清理，
并补入实际页面的单条目、正确差异和可打开文件回归。没有把 session 比较范围当作已实现。
临时索引建立前检查 HEAD 元数据（4 MiB / 20,000 条目），共用服务总期限，避免边界处理
生成没有预算的大索引；完成后再次确认用户索引 SHA-256 不变、Home 无残留临时目录。

### 里程碑四准备：真实 PTY 与同源 C/JS 服务

已新增 terminal.capabilities/create/list/get/attach/detach/read/write/resize/close/release。
会话独立于面板、请求和取消；程序直接 argv，8 个运行 / 32 个保留会话，输出 1 MiB、
输入队列 64 KiB + 单次在写 8 KiB，读游标和明确 gap。独占输入租约、序号及内容去重，
不将接收队列等同于命令成功。持久元信息与独立创建收据防止重启及 release 后重放，
不保存 argv、输入/输出、控制令牌。默认 Windows Shell 使用系统目录的完整路径。

真实测试发现并修正 POSIX 满 PTY 写入在子进程退出后仍不返回、造成退出等待的问题。
使用 xrt 暴露的借用原生描述符，保留原标志并为本会话主端启用非阻塞；处理 AGAIN/
短写，观察结束后退出泵；全部回调停止后关闭原生 PTY、清理遗留普通子进程、释放项目租约。
服务 Stop 在 ProjectLifecycleUnit / 脚本代码释放前完成线程排空；eof 与 drained 明确区分。
取消 starting 记录会阻止尚未发生的启动；启动失败保留终态记录并返回实际原因。

2026-10-24 Windows ConPTY、WSL Linux POSIX PTY 的同一真实测试均通过：
C 与 HTTP 共享终端、实际 TTY、归属/上下文、项目租约生命周期、尺寸变化、Unicode /
Ctrl+C 字节、输入去重/乱序拒绝、只读多观察者、取消读取保留进程、超过 1 MiB 输出、
输入背压、阻塞输入时关闭/排空、Shell 退出后普通子进程回收、崩溃重启防重放、
release 后旧意图拒绝、元信息隐私、损坏收据、启动失败、取消尚未启动的程序、
服务停止清理及损坏元信息保留。测试脚本 tests/test_workbench_terminal_runtime.py。
证据在 terminal-runtime/result.json 和 terminal-posix/result.json。

Linux QA 宿主重新编译自已锁定的 216f5445 / 5131a86e overlay，
.build/workbench-builtin-20261023/linux-host/xs；使用 glibc native，供开发测试，不能作为
正式 Linux 兼容发行包。附带 QA server 可执行文件也不是最终客户端交付物。
Windows 宿主仍是 mdo-workbench-builtin-host/xs.exe，本阶段未改原生 overlay。
20 项前端合同、6 项 SDK 合同、206 模块图及相关 15 项 JS 测试通过。

尚未挂载终端前端，也未提供四入口新版主页。下一步打包终端渲染组件、接入视图，
验证真实 Shell、输入法/粘贴、断线/隐藏/服务重启；还需 Git 会话基线/连续浏览、文件
管理动作、Android 编译和最终 Windows 测试程序。终端整项保持未完成，不发布。
其他任务在本轮并行提交了 CLI 修复，已保留，未覆盖；tests/test_remote_asymmetric_pause.py
仍是他人未跟踪文件，提交时须排除。每次操作以即时 git status/差异为准。
收尾时还应核对工具包 PATH/运行环境，以及项目删除/迁移后结束记录和布局的清理行为。

## 验证要求

测试必须验证实际行为：浏览器加载和失败、菜单显隐；文件外部修改和大目录；Git 的
未跟踪/特殊文件名/重命名/大差异；PTY 交互/中断/resize/大量输出/关闭和附着；
上下文隔离、断线、服务重启。不能把外部网站截图成功当作原生窗口可见性的唯一证据。
Android 无真机时如实记录，不标记真机通过。没有会话基线时不提供伪造的“本会话变更”。
