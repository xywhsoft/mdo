# 公共目录浏览器

`createDirectoryPicker()` 位于 `app/web/js/features/sessions/directory-picker.js`。
创建项目和编辑项目的工作区目录共用此入口，浏览目录时收起项目表单，返回后保留
项目名称、标识和模型；任一时刻只显示一个窗口。

```js
const picker = createDirectoryPicker();
await picker.open(initialPath, selectedPath => { workspace.value = selectedPath; }, {
  onClosed({ reason }) { /* cancel/button/escape 为用户返回；dispose 不重开旧页面 */ },
});
picker.close();
// 页面彻底销毁时调用 picker.destroy()，释放设备订阅。
```

窗口使用 `openLayer`：桌面 64rem × 42rem，可最大化；手机全屏，将快捷位置收进
“位置”按钮，正文独立滚动，确认区域固定。路径和文件夹名均作为纯文本显示。
桌面单击选择、双击/Enter 进入；手机点击进入，再确认当前目录。
Ctrl+L 编辑地址，Enter 前往，Alt+左右切换历史，Alt+上箭头返回父目录。
路径补全和当前层筛选分别取消过期读取。读取错误保留旧列表与待访问路径，禁止确认，
重试仍访问失败的目标。确认前再次检查目标目录，避免返回已删除的子目录。

收藏和最近选择保存在当前前端的 localStorage，按控制端账号和目标设备 ID 区分；
浏览不会写入最近记录，只有确认成功才记录。存储不可用时仍可浏览和选择目录。
切换设备会销毁旧窗口，不把远端访问失败降级为本机访问。

## 服务与 SDK

- `GET /api/v1/workspace/locations`：用户目录、启动目录、桌面/文档/下载和磁盘根目录。
  Windows 通过系统 Known Folder API 读取，可跟随桌面等位置的重定向。
- `GET /api/v1/workspace/directories?path=…&filter=…&prefix=…&after=…&limit=…`：只读当前层；
  按稳定的自然名称顺序分页。`filter` 为包含匹配，`prefix` 用于路径补全，ASCII 大小写
  不敏感，其余 UTF-8 字节按原文匹配。`limit` 为 1–256，默认为 128。
  `next_cursor` 是最后一个名称，`has_more` 表示可继续读取。前端不重新排序各页。
- `POST /api/v1/workspace/directories`，`{path, name}`：在现有父目录下创建单个目录，
  不递归创建、不覆盖同名文件或目录；遵循普通写入令牌和远程控制权限。
- SDK 同时提供 `workspace.locations`、`workspace.directories.list` 和
  `workspace.directories.create`，使用相同业务实现和契约。
- 项目菜单的“在资源管理器中打开”调用 `POST /api/v1/projects/{project}/open-directory`
  （SDK：`project.open_directory`）。请求体为 `{}`，服务端解析该项目绑定的工作区，
  默认项目使用 Home 下的 `workspace`；通过系统目录打开能力启动目标设备的文件管理器。
  请求遵循项目生命周期锁和远程控制权限，不接收浏览器指定的原生路径或命令。

分页只保留当前页加一个名称，内存不随目录总量增长。扫描最多一百万项/约五秒，
超限返回明确错误，绝不把截断结果当成完整列表。目录变化期间的分页是名称游标语义，
刷新会重置游标并重新读取；未建立长寿命目录快照或递归索引。

Android 的原生窗口提供“存储访问权限”入口，由用户在系统界面授权共享存储。
首次位置使用应用私有 Home（尚未创建时使用其父目录），不使用系统返回的不可浏览 `/data`。
权限未授予时仍可使用应用可访问的目录；不把 content URI 伪装成可供工具执行的文件路径。
手机远程控制电脑时，盘符、目录和记录属于目标电脑，不触发本机存储授权。
