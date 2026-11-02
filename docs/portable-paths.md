# Home 路径持久化与 1.0 前的迁移合同

用户确认的范围是 mdo 自身管理的配置、会话、产物和任务记录。
项目在 Home 外的目录绑定保持原样，不导入、不搬动、不禁止外部项目。
模型 read/edit、进程 cwd 等相对参数仍以项目工作区为基准。

## 永久合同

默认桌面 Home 为 `{exepath}/mdo-home`。保留已有的显式 `--home` / `MDO_HOME`
覆盖入口用于服务端和测试；同一个进程的所有持久化路径使用同一个已选定的 Home。

- 保存 `1.txt` 表示 Home 下的 `1.txt`，不表示进程 cwd 或某个项目下的文件。
- Home 根表示为 `.`；目录分隔符保存为 `/`。
- 相对路径不允许 `..`、盘符、UNC 或越过 Home 的写入。
- `MdoPathStore` 将本 Home 内原生路径转换成相对引用。
- `MdoPathResolve` 仅在 OS/原生库边界生成当前原生路径。
- 外部项目绑定不受 Home 持久化合同约束，保留原有绝对绑定。
- SDK 中显式的 workspace/extension/application 空间继续保持各自语义。

会话、项目中的 Home 内工作区、定时任务、变更基线、终端记录、MCP 文件/目录参数、
设备传输任务和宿主管理的成员状态遵守这个合同。终端及 Agent 的内存运行对象保留原生路径，
写入副本再转换，避免保存操作改变正在运行的对象。

扩展目录内的程序相对路径不改为 Home 相对路径：它由所属扩展目录解释。
分发包、审批摘要、用户项目和第三方插件自有 state 值也不按字段名称猜测并重写。

## 临时兼容：必须在 1.0 删除

`path_compat_pre1.inc.c` 和 `path_migration_pre1.inc.c` 是临时兼容叶文件。
前者在语义版本达到 `00010000` 时直接拒绝编译，防止遗留代码进入 1.0。

旧绝对引用的解析顺序：

1. 原绝对路径存在时优先使用原位置。
2. 仅在原位置报告 NOT_FOUND 时，寻找完整的 `mdo-home` 目录分量。
3. 保留它之后的路径，改用当前 Home；不匹配 `mdo-home-backup`，拒绝 `..`。
4. 权限或 I/O 错误不会重定向到另一个文件；由实际读写返回原错误。

这不是对 JSON、正文或任意命令参数执行正则替换。
Home 内的相对工具参数仍由工作区解释，不经过持久化路径重定基准。

启动时在 Home 独占租约内、其他管理器启动前，迁移已知 JSON 字段：

- `projects/*.json[.bak]`、`sessions/*/*/meta.json[.bak]` 的 `workspace_root`。
- `schedules/*.json[.bak]` 的可选 `workspace_root`。
- `workbench-baselines/*/sessions/*.json[.bak]` 的 `workspace`、`repository`。
- `data/application/terminals.json` 中的 cwd、程序及上下文工作区。
- 旧 `config/mcp/*.json[.bak]` 的程序和工作目录。

每个文件原子替换；中断后重跑，已经转换的文件不重复写入。
不改变会话 revision、活动时间、消息序列、模型账本或历史正文。
未知/损坏文档保留给原读取和恢复路径处理，不做推测性修复。

MCP 私有连接先通过配方、目录和审批校验，再按声明为 file/directory 的参数迁移。
秘密、普通文本和分发目录字节保持不变。宿主管理的 SDK 成员状态在校验原 envelope 后迁移，
使用正常保存接口重算摘要；第三方插件状态保持不透明。
传输任务读取时恢复原生位置，保存副本时转成相对路径；设备/文件身份校验保留。

Windows 新更新参数保存可执行文件名及 `home-relative-v1`，不保存安装目录或 Home 的
绝对路径。安装器从身份校验后的原进程和自身所在 Home 推导位置。
旧格式只保留在安装脚本明确标记的 PRE-1.0 分支。

## 不改写的内容

历史模型回复、工具调用原文、stdout/stderr、记忆文档、用户脚本，以及 WebView2
内部配置/日志可能包含过去的路径文字。这些内容不作为 mdo 当前运行位置的权威来源。
尤其不能全局替换 snapshot/journal：它们有账本校验，原文也可能是用户代码。
新会话及恢复后的宿主能力信息由当前 Home 重建。

## 1.0 删除清单

1. 删除两个 `*_pre1.inc.c` 文件和启动迁移调用；移除当前编译阻挡。
2. 将 `MdoPathResolve` 的绝对输入分支改回普通原生解析，保留外部项目能力。
3. 删除 MCP 私有路径升级函数和加载后的升级分支；保留类型化输入的正常解析。
4. 删除 SDK `SdkStateUpgradePaths` 及加载升级分支；保留相对保存和摘要验证。
5. 删除安装脚本旧参数格式分支，并拒绝缺少 `home-relative-v1` 的旧文档。
6. 删除旧绝对 Home 兼容测试，保留相对路径、外部项目不变和真实移动验收。

## 验证

`tests/test_portable_paths_native.py` 使用现有 QA 宿主直接编译当前产品 C，验证原生
Agent 能在恢复的工作区执行 read，不调用模型。覆盖错误目录名、路径穿越、备份、
幂等迁移、MCP、终端、成员状态和传输任务。

`tests/test_portable_home_move.py --host .build/host/windows-server-glibc/xs.exe`
实际移动一个测试 Home，让旧位置不存在，重启后同时验收旧格式与相对格式会话的 HTTP
读取、工作区文件访问、备份和时间/版本字段。直接复用项目源码及既有宿主，不复制仓库或运行时。

加 `--packed <mdo.exe>` 则移动可执行文件和 Home 的整个目录，不传 `--home`，验证正式
单文件产物的默认 `{exepath}/mdo-home` 基准；二进制以硬链接复用。

`tests/test_update_installer.py` 验证新旧更新参数、实际替换、回滚保护和 Unicode 参数保留。
