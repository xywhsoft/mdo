# 客户端国际化

内置界面支持 `zh-CN`、`en-US`、`ru-RU`。`app/web/lang/` 三份语言包必须包含相同的键与插值参数。系统语言只决定初始选择，用户的显式选择优先；日期和数字格式使用 `currentLocale()`。

## 文案与内容

- 固定界面文案、操作提示及产品定义的错误使用语言键。模型回复、用户输入、作品元数据、代码、文档、文件名、命令、协议字段与操作系统原始诊断保持原样。
- 原生库、供应商或作者提供的错误详情不能靠替换显示文本来猜测含义。优先按稳定错误码展示本地化说明，保留实际详情；未知诊断不得伪装成已知问题。
- 不对整个 DOM 做文本查找替换。相同的“保存”既可能是按钮，也可能是用户文档，二者必须明确区分。历史日志中的固定标记属于数据识别规则，不能翻译其匹配常量。
- 编号、别名、路径、权限枚举和协议名称不是语言键。语言选择框保留语言自称，产品及第三方工具名称保留其身份。

## 渲染方式

静态纯文本节点继续使用 `data-i18n`；属性使用 `data-i18n-title`、`data-i18n-placeholder`、`data-i18n-aria-label`。普通字符串返回值、错误消息和创建时的默认内容使用 `t(key, params, fallback)`。

动态界面的产品文案优先使用 `uiMessage(key, params, fallback)`。它保留语言键，在需要字符串或序列化时生成当前语言文本。`utils/dom.js` 的 `element` 和工具工作区的 `element`、`iconButton` 会明确绑定这类文案；直接操作节点时使用 `setUiText`、`setUiAttribute`。

```js
const save = element("button", { text: uiMessage("sessionAction.save") });
setUiAttribute(search, "placeholder", uiMessage("ui.externalDirectoryTools.searchTools"));
setUiText(status, uiMessage("draft.saveFailed", { error: details }));
```

绑定以弱引用的节点状态保存；切换语言只修改受绑定的文字与属性。不会修改输入框的 `value`、选择、光标或滚动位置，也不重建编辑表单。文本节点后追加的图标会保留；如果其他渲染器替换了该文本节点，旧绑定不夺回它的内容。节点改为作者或用户文本时，通过同一 setter 写入普通字符串即可解除对应绑定。

参数可以包含另一个 `uiMessage`，例如商店名称；这样切换语言后外层句式与内部名称一起更新。不得在模块初始化时执行 `t` 后把译文永久保存在常量中。显示名表可以使用延迟读取的 getter；没有编辑状态的列表可订阅语言变化重新渲染，并在销毁时取消订阅。

弹窗标题、关闭/最大化按钮和提示使用同一规则。检查文案必须覆盖正文、按钮、空态、下拉选项、输入提示、`title`、无障碍名称、错误及恢复操作，不能只翻译页面标题。

## 验证

- `tests/test_frontend_i18n.mjs`：语言包键与参数一致性、静态标记及既有本地化行为。
- `tests/test_ui_locale_binding.mjs`：字面量语言键覆盖、三种语言、九类商店、外部工具、弹窗、输入保护、图标与渲染归属。
- `tests/test_timeline_history_i18n.mjs`：日志标记识别不受显示语言影响，用户、模型和自定义历史内容保持原样。
- `tests/test_warnings.mjs`：诊断说明与建议本地化，原始技术证据保留。
- `tests/test_android_package_install.py`：安卓安装器的三语提示、文件边界、真实 SDK 集成及漂移检查。

安卓安装器等系统原生界面跟随系统语言；未知系统错误保留原文。Java 策略的语言选择与提示不能改变路径、签名、大小及安装前复查规则。
