# mdo 墨斗 — 原生 C 栈 Agent 工作台

极简 agent 工作台：xs C 后端进程内直调 [xllm / xllm-session / xwork](https://xrt.xywhsoft.com)
三件套，webview 前端。工具调用、审批闸门、多模态、记忆、多项目会话分桶、三语界面。

## 运行

```
# 开发模式（HTTP 9091，改 app/ 下 C 源重启即重编）
./xsw.exe dev.json

# 打包便携单文件（数据落 exe 旁 data\，整目录拷贝即迁移）
./xsw.exe pack app -o mdo.exe
```

`xs.exe` / `xsw.exe` 为宿主（从 xserver 构建拷入）；首次运行零配置——内置
Ling 模型开箱即用，其他模型在设置页添加。`tools/` 为随程序分发的
curl/git/python（gitignore，重建见 [tools/README.md](tools/README.md)）。

## 布局

```
app/        后端源（main.c + mdo_*.h 单 TU）+ wwwroot 前端
data/       运行数据（配置/会话/记忆/审计，gitignore）
tests/      测试与验证脚本（ling 工具调用测试、mock LLM、UI 走查记录）
dev.json    开发服务配置（9091）
生成程序.bat  打包 mdo.exe
```

## 依赖

上游三件套 vendored 在 xserver 仓 `lib/`；本仓只含应用层。工具表 17 件：
read/write/edit + ls/glob/grep + exec/spawn/poll/wait/stdin/stop + python 三态
+ ask_user + 搜索三件。
