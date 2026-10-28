# 外部工具

Python、rg、curl 等是由进程接口执行的程序；模型直接调用的结构化 C 工具属于 Agent 工具。

每项使用 `mdo-home/external-tools/<id>/`，内含 extension.json、tool.json 和全部程序/动态库/数据/许可。发布整个目录的普通 ZIP，安装不执行工具探测。根清单 description 面向用户；tool.json 的 prompt 面向模型，二者不得混用。

本机列表中的手工工具必须从来源整目录复制，入口和命令别名只保存相对路径；不管理工具列表之外的系统程序。商店下载项禁止编辑和再次分享。官方推荐仅是首次部署提示，点击按普通下载任务逐项安装；关闭或替换后不干预。

派生命令目录为 data/external-tools/bin。Windows 从 VFS 中释放通用小转发器为多个别名，按相对目录表转发原 argv、cwd、stdio、信号和退出码；Linux 使用同样受管理的链接入口。搬移 Home 不需要改写绝对配置。不修改系统 PATH。

目录快照持有已校验工具租约，SDK process/Git/terminal 和内置 exec/spawn 的整个进程树借用租约。停用立即退役新调用入口，但进程结束前不能更新/卸载目录。主进程退出但后代仍持有管道时，先清理拥有的进程树再等待 I/O，防止永久占用目录。

Android 仅完整版 APK 带程序，不支持动态安装或更新外部程序。每个工具以完整普通 ZIP 置于 APK assets；ELF 另交 PackageManager 安装。Home 仍保留唯一完整目录，派生链接映射到系统 ELF 和 Home 数据。APK 内部摘要验证后才登记；卸载后重启不自动恢复。精简版不包含程序。详见 [Android 分发](distribution.md)。

创作配额、审核、协议和文件限制见 [目录商店](ecosystem-store.md)。
