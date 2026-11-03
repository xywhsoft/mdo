# Agent 回复的 Markdown 渲染

语法由 xs 已有的 md4c 扩展解析。mdo 精简宿主将该扩展列入 `deps.lock` 的必需项，
校验锁定版本和库目录摘要，不修改 md4c 上游源码。许可证随 `app/web/licenses/md4c.txt` 打包。

## 显示链路

1. 聊天时间线将助手正文交给 `createMarkdownRenderer`，用户原文仍按普通文本显示。
2. 显示端调用本机 `/api/v1/application/call` 的 `markdown.parse`，返回结构节点，不返回 HTML。
   即使正在只读查看另一台设备，解析仍使用显示端写入令牌，不向目标设备发送解析写请求。
3. 前端用白名单节点、`createElement` 和 `textContent` 构造 DOM。
   HTML 原文作为可见文本显示；链接和图片地址另行校验，禁止 JavaScript 等执行协议。

支持 CommonMark 基本语法，以及表格、删除线、任务列表、裸链接；包括引用式链接、复杂括号链接、
嵌套列表、1—6 级标题、代码围栏和字符实体。代码复用工作台的有界基础高亮，提供复制按钮。
该高亮器不等同于完整语言语法分析。公式和 Mermaid 图仍显示为原文本或代码，需另加显示组件。

md4c 接收完整文档，不是增量输入解析器。输出期间按约 120 ms 合并请求，最多两个解析请求并行，
单请求最长 8 秒；同一消息最多一个在途请求，晚到结果不能覆盖更新的正文。
保留已解析内容并立即显示新增原文，稳定的图片与代码块复用 DOM，避免图片闪烁和重复加载。
完成后的结构缓存最多 32 条、约 4 MiB（以 JS 字符长度计），切换/移除消息会取消其请求。
解析失败或超限时保留可阅读的原文；不会让整条回复消失。

原生接口限 UTF-8 源文本 512 KiB、32768 个节点和 96 层嵌套。
返回格式为 `{engine:"md4c",schema_version:1,nodes:[...]}`；节点使用 `t`、子节点 `c`，
文本 `s`，链接/图片 `url,title`，以及标题 `level`、列表 `start,tight,task`、表格 `align`。

## 图片和文件

- HTTP(S) 图片可直接显示，支持点击预览、加载失败提示与重试；不发送来源页面 Referer。
  内嵌 Base64 只接受 PNG、JPEG、GIF、WebP，仍受正文和地址预算限制。
- 本地文件通过当前会话的 `markdown-resource` GET/HEAD 接口读取。
  仅允许会话创建时捕获的工作区和本会话目录，支持 `@uploads/` 别名；使用锚定目录打开，
  拒绝目录逃逸和符号链接/Windows junction 逃逸。
- 本地图片最多 8 MiB，按文件签名允许 PNG、JPEG、GIF、WebP，拒绝本地 SVG/HTML。
- 文件链接先验证资源存在，再复用工作台文件标签页。支持 `:行[:列]` 或 `#L行[-L末行]`，
  当前只定位起始行。空格路径应写在尖括号中，如 `[代码](<D:/work/my file.c:12>)`。
- 普通网页浏览器使用原生超链接；xs WebView 内通过显示端 `system.open` 打开系统浏览器。
  远程会话的图片、文件读取继续使用当前目标设备传输，不回退到本机同名文件。

资源接口：

```text
GET /api/v1/projects/{project}/sessions/{session}/markdown-resource?path=<百分号编码路径>&mode=image
GET /api/v1/projects/{project}/sessions/{session}/markdown-resource?path=<百分号编码路径>&mode=file
```

`mode=file` 返回规范文件路径及字节数，用于工作台跳转。此接口不运行程序、不提供任意目录服务。

## 回归

```powershell
node --test tests/test_markdown_boundaries.mjs
python tests/test_markdown_runtime.py --packed .build/md4c-20261104/mdo-md4c.exe
python tests/test_markdown_resources.py --packed .build/md4c-20261104/mdo-md4c.exe
python tests/test_markdown_browser.py --packed .build/md4c-20261104/mdo-md4c.exe
```

浏览器回归使用真实打包资源和原生解析服务，覆盖中/英/俄三种语言、1400/390 两种宽度、
动态语言切换、图片重试与预览、流式节点复用、危险协议、过期结果、忽略取消的传输和原文回退。
额外用完整 mdo 页面验证文件打开、行号定位、复用文件标签页。外部浏览器唤起只验证服务调用路由，
测试不真正启动用户的系统浏览器。跨平台代码路径尚未在 Linux/macOS/Android 上实机验收。
