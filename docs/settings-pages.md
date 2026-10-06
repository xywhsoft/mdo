# 设置页面开发

设置界面是独立的全屏工作区。桌面保留设置分类导航，隐藏主界面的会话菜单和任务面板；手机（宽度不超过 760px）使用原生下拉菜单。返回按钮和浏览器历史仍回到原来的项目／会话，打开设置不会修改保存的侧栏宽度或开关状态。

快捷键帮助位于设置页标题栏右上角，所有设置分类均可打开；手机上使用带有名称提示的帮助图标。“常规”页不再显示单独的快捷键选项，仍可用 `?` 打开、`Esc` 关闭。工作区侧栏的计划任务入口位于“新建任务”下方，打开右侧独立的 `#/schedules` 页面，左侧仍保留项目和会话列表。返回按钮或 `Esc` 返回原对话，输入草稿保留；刷新计划页面后也能返回。两个入口仅在新任务或计划页面激活时显示选中状态。计划管理组件在 `features/schedules/schedule-panel.js`，隐藏时停止轮询，重新显示时读取最新列表。

底部连接状态同行末尾只显示主题图标，太阳表示切换为浅色，月亮表示切换为深色，并跟随系统外观和设置预览更新。

## 代码分工

- `app/web/index.html`：分类按钮、页面 HTML、原生表单控件。
- `app/web/js/features/settings/settings-pages.js`：页面发现、桌面／手机导航同步、标题、页面显示、滚动和焦点。下拉选项由分类按钮自动生成，不需要再维护一份列表。
- `settings-view.js`：通用偏好表单的数据回填、校验、外观预览、自动保存和失败重试；`settings-autosave.js` 负责保存时序。
- 同目录的 `*-panel.js`：模型、项目、扩展等各页面的业务组件。独立管理页自行读取、保存数据，不受通用偏好保存按钮控制。
- `app/web/js/app.js`：进入／退出设置、页面业务组件初始化和按需读取。未知页面路由显示常规页。
- `app/web/css/app.css`：通用页面和控件样式，以及手机／键盘缩小视口的布局。

页面和配置项直接用 HTML、JavaScript 编写，没有配置式字段描述或表单生成器。

## 添加页面

在 `.settings-navigation` 中增加分类按钮，标识使用小写字母、数字、连字符：

```html
<button type="button" data-settings-section="example"
  data-i18n="settings.example">示例设置</button>
```

在 `.settings-content` 中增加匹配的页面：

```html
<section class="settings-section resource-section"
  data-settings-panel="example" aria-labelledby="settings-example-title" hidden>
  <div class="settings-section-heading">
    <h2 id="settings-example-title" data-i18n="settings.example">示例设置</h2>
    <p data-i18n="settings.exampleDescription">页面说明。</p>
  </div>
  <div id="settings-example-content"></div>
</section>
```

按钮和页面标识必须一一对应，不能重复。无需添加手机菜单、切换代码或保存按钮可见性判断。`#/settings/example` 会自动支持直接打开、前进和后退。

如果页面属于通用偏好设置，将 section 放进 `#settings-form`，使用 `settings-section` 即可；公共框架会自动显示保存状态和保存／放弃／重置按钮。独立管理页放在该 form **外面**，由自己的组件负责请求和保存，允许使用独立 form，避免嵌套表单。

需要业务逻辑时，新建 `example-panel.js`，在 `app.js` 中初始化。需要按需读取时，在设置路由分支里按 `selectedSection` 触发组件的 `refresh()`。框架只负责页面呈现，不替业务页面定义数据格式。

## 添加配置项

“设置 → Agent → AI 回复语言”保存 `agent.reply_language`，预设为 `zh-CN`、
`en-US`、`ru-RU`，也可输入单行语言名称（最多 128 个 UTF-8 字节）。留空表示
跟随最新提问的语言。它独立于界面语言，恢复已有会话时替换提示词中的
`mdo_reply_language` 段；Agent 和子 Agent 使用相同偏好。正在进行的回复不被
设置更改打断，下次运行开始生效。翻译、明确指定输出语言和原文引用仍按请求处理。

普通选项可直接复用 `setting-row`：

```html
<div class="setting-row">
  <div>
    <label for="setting-example" data-i18n="settings.exampleOption">选项名称</label>
    <p data-i18n="settings.exampleOptionDescription">解释这个选项会改变什么。</p>
  </div>
  <select id="setting-example" name="example_option">
    <option value="on">启用</option><option value="off">关闭</option>
  </select>
</div>
```

其他现成组件：

- `setting-switch`：`label` 内放说明 span、checkbox 和 `switch-control` span；可参照现有通知开关。
- `setting-row setting-row-wide`：长文本、路径、服务地址。
- `setting-row setting-row-wide setting-instructions`：多行文本，使用原生 textarea。
- `settings-grid`：一组数值输入，桌面两列、手机单列。

给原生控件添加适当的 `required`、`min`、`max`、`maxlength`，校验和键盘操作由浏览器提供。不要在未显示的 section 上另建提交按钮来绕开公共保存流程。

对于通用偏好字段，在 `settings-view.js` 的 `settingsPatch()` 中加入提交映射，在 `fill()` 中回填服务端值；复杂校验可并入现有校验函数。表单值差异、保存期间继续编辑、未保存提示会自动覆盖新增的命名字段。新增配置必须同时接入服务端配置验证、默认值和设置 API，不能只增加显示项。

静态文案用 `data-i18n`，动态文案用 `t()`，并补齐 `zh-CN`、`en-US`、`ru-RU`。手机分类标题会随着语言切换自动同步。

## 验证

运行 `node --test tests/test_settings_pages.mjs tests/test_settings_autosave.mjs tests/test_settings_state.mjs tests/test_settings_resource_loading.mjs`，以及 `python -m unittest discover -s tests -p test_frontend_contract.py`。

打包后用 `tests/manual_packed_settings_qa.py --packed-path <exe>` 启动隔离的设置页面，检查桌面全屏、320px／390px 手机、最后一个分类、返回会话、语言切换、无效输入和保存失败重试。该夹具使用独立 Home，不修改用户配置，不调用线上模型。
