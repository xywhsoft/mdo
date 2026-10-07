# xge 像素图编辑器（32x32）

基于 `D:\GIT\xge` 引擎 SDK 的最小像素图编辑器，纯 C 实现，仅使用 xge 公共 API。

## 文件

- `src/editor.h` / `src/editor.c` — 共享编辑器逻辑（画布、画笔、橡皮、清空、PNG 导出、调色板），不依赖窗口
- `src/app.c` — 交互模式（窗口、鼠标/键盘输入、缩放、界面绘制）
- `src/main.c` — 入口，`--self-test` / `--smoke` 分流
- `src/paths.c/.h` — 从可执行文件位置解析资源路径
- `build.bat` — 可复现构建脚本
- `res_font_KaTeX_Main-Regular.ttf` — 界面文字字体（自引擎 res 目录复制，仅用于说明文字）
- `bin/pixel_editor.exe`、`bin/xge.dll`、`bin/res_font_KaTeX_Main-Regular.ttf` — 构建产物（字体等运行时资源与可执行文件同目录）
- `bin/selftest_sample.png` — 自测导出的示例图
- `bin/smoke_window.png` — 渲染烟雾测试截图

## 构建

需要 w64devkit 的 gcc（`E:\software\w64devkit\bin\gcc.exe`）在 PATH 中：

```
build.bat
```

编译命令等价于：

```
gcc -O2 -std=c99 -Wall -Isrc -ID:\GIT\xge ^
  src\main.c src\editor.c src\app.c src\paths.c ^
  D:\GIT\xge\build\xge.lib -o bin\pixel_editor.exe -lgdi32 -luser32 -lshell32 -lole32
copy /y D:\GIT\xge\build\xge.dll bin\
```

## 运行

```
bin\pixel_editor.exe              # 交互模式
bin\pixel_editor.exe --self-test  # 有界自测：画/擦/清空/工具语义校验 + 导出示例 PNG 后退出，不开窗口
bin\pixel_editor.exe --smoke      # 渲染烟雾测试：开真实窗口渲染 5 帧后用公开截屏 API 保存窗口图并自动退出
```

交互操作：
- 左键按当前工具绘制、右键临时擦除（不改变所选工具），拖动连续画线
- 1-8 选色，或点击调色板色块
- E 切换画笔/橡皮，C 清空，S 保存
- 上/下箭头缩放（4/6/8/9 倍），ESC 退出

文件位置：字体从可执行文件所在目录加载；`pixel_art.png`（S 键）、`selftest_sample.png`、`smoke_window.png` 均保存在可执行文件所在目录（即 `bin\`），与双击运行时的工作目录无关。

这份源码由运行中的墨斗生成并修正。仓库直接使用 xge SDK 的头文件，不重复保存 SDK 或程序二进制；验收截图为 `smoke_window.png`。`acceptance.json` 记录执行数量和源码 SHA-256，文本计算前统一 CRLF 为 LF，二进制按原始字节计算。
