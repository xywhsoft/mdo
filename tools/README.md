# tools/ — 随程序分发的第三方工具

本目录被 .gitignore 忽略（约 112M 二进制）。程序启动时 `MdoStoreInit` 把
`tools`、`tools/python313`、`tools/git/cmd` 注入进程 PATH 最前，
`exec/spawn` 直接用程序名即可命中；python 工具的 REPL 解释器固定用
`tools/python313/python.exe`。

## 布局与重建

| 内容 | 来源 | 说明 |
|------|------|------|
| `curl.exe` | 本机 System32 拷贝（696K） | Windows 自带 curl 即可 |
| `git/` | MinGit-2.55.0.3 64-bit（GitHub Releases 解压，91M） | **必须从 `git/cmd` 调用**（PATH 注入的是该子目录）；MinGit 自带完整 mingw 运行时 |
| `python313/` | python-3.13.7-embeddable-amd64.zip 解压（34 文件） | 标准库即单文件 `python313.zip`（zip 部署形态）；`._pth` 隔离不读系统 site-packages；pip 默认关闭（需要时去掉 `._pth` 里 `#import site` 的注释 + get-pip） |

注意：下载 GitHub Releases 时 shell curl 可能报 SSL 35 错，用
`python -c "import urllib.request; urllib.request.urlretrieve(url, dst)"` 可成。

新增子目录程序时，须同步 `app/mdo_store.h` 里 MdoStoreInit 的 PATH 注入列表。
