# 公共窗口弹层

`app/web/js/components/layers.js` 和 `app/web/css/layers.css` 是前端公共窗口基础设施。
外部工具、Agent 工具、Skill 的本机和商店详情使用同一实现；其他共用扩展浏览组件的
详情也继承此行为。“查看我的投稿”和全局下载任务窗口同样接入公共库；下载任务标题栏
和底部操作固定，关闭窗口不取消后台任务。后续新弹层沿用这个库，逐步替换既有的专用弹层。

## 尺寸与移动端

`width`、`height` 可传正数（CSS 逻辑像素），或正数加 `px`、`rem`、`%`，以及 `auto`。
默认 `width: "52rem", height: "auto"`。详情窗口使用 `height: "80%"`。
百分比按当前可见视口计算，包含软键盘占用后的空间，两个方向都强制限制在可用范围内。
过小的尺寸至少保留 18rem × 8rem（视口不足时以视口为准），确保窗口操作仍然可见。
超高内容只滚动正文，标题、窗口操作和底部按钮固定。

高 PPI 屏幕的物理像素不会直接用作 UI 尺寸；现有 viewport meta 保持设备逻辑宽度。
不要除以 `devicePixelRatio`。普通固定窗口推荐使用 `rem`，可随根字体大小变化；需要精确
逻辑尺寸时可传 `840`，它在手机上也不会按 840 个物理像素缩小。

视口宽度不超过 600 个逻辑像素时，默认 `mobile: "fullscreen"`，适应安全区及软键盘。
需要留出遮罩的短表单可用 `mobile: "fit"`，仍受视口边界限制。手机关闭最大化和拖动；
桌面可拖动标题栏、最大化和还原，旋转或缩放后重新约束边界，不把窗口留在屏幕外。

## 开发接口

```js
import { openLayer } from "/js/components/layers.js";

const window = openLayer({
  title: "扩展详情", icon: "terminal", width: "52rem", height: "80%",
  owner: pageRoot,
  render(body, { layer, signal }) {
    body.append(detailContent);
    layer.footer.append(saveButton, cancelButton);
    cancelButton.onclick = () => layer.close("cancel");
    // 异步读取使用 signal；关闭时中止读取，独立下载任务不绑定此 signal。
    return () => unsubscribe();
  },
  async beforeClose({ reason, value, layer }) {
    return !dirty || await confirmUnsavedChanges();
  },
  onClosed({ reason, value }) { /* 回收业务选择状态 */ },
});
await window.ready;
window.update({ title: "新标题", width: "85%" });
const result = await window.closed;
```

- 图标使用公共图标注册表中的名字；标题按纯文本处理。颜色继承应用主题变量，跟随系统
  或用户设置的白天/黑夜模式实时变化，不复制主题状态。
- 返回 `element`、`body`、`footer`、`closeButton`、`signal`、`ready`、`closed`、`update`、
  `close`、`dispose`。更新只改变窗口配置，不重建正文。
- `render` 可以异步返回清理函数；关闭后才返回的清理函数也会执行一次。渲染失败回收
  窗口并拒绝 `ready`；调用方可用 `onError` 处理错误，默认记录控制台。
- `beforeClose` 返回 false 拒绝关闭，支持 Promise，同一时间只执行一次。业务页或插件
  销毁用 `dispose()` 强制清理，不被未完成的保存确认阻塞。
- 原生 `showModal()` 提供顶层、焦点隔离和键盘可达性。关闭按钮和 Esc 走关闭守卫，嵌套时
  Esc 只关闭最上层。默认不点击遮罩关闭；可显式开启 `closeOnBackdrop`。正文选中拖到
  遮罩上不会误关。`method="dialog"` 或直接原生 `.close()` 已经关闭窗口，不能再由守卫撤销；
  需要守卫的按钮应调用 `layer.close(reason, value)`。
- 页面弹窗用 `owner: pageRoot`。公共窗口挂在 body，页面短暂刷新重挂载不会移走窗口；
  owner 最终被移除或被 hidden 隐藏时自动销毁。无需把 dialog 挂进列表容器。
- 背景滚动引用计数锁定，关闭恢复原来的 overflow、优先级和滚动位置；刷新期间仍保留详情
  页签及滚动位置。返回焦点使用 preventScroll；列表刷新替换入口后由扩展浏览组件定位新入口。

## Application SDK

前端插件可直接使用 `sdk.ui.layers.open(options)`，选项与上述库一致。插件停用、启动失败
或应用 UI 销毁时自动回收它拥有的窗口。`registerPanel` 的非停靠面板也改为公共窗口，
支持 descriptor 的 `title`、`icon`、`width`、`height`、`mobile`、`beforeClose`；既有 C SDK
通过 `ui.invoke` 的 `ui.panel` 动作打开已注册面板，继续使用同一套实现。工作台右侧停靠
面板保留工作台布局管理。此接口仍遵守当前可信前端插件执行模型，不新增权限沙箱。

测试：`test_layers.mjs` 覆盖尺寸输入、嵌套、关闭守卫、渲染失败、迟到清理、滚动锁和
视口变化；扩展及 SDK 回归覆盖页面接线、读取取消、后台安装和所有权清理。

账号登录和设备选择也使用同一库，入口直接创建窗口，不依赖隐藏的设置页面。登录组件
通过显式 request/scope 适配器区分目标账号与本机连接器；关闭前取消原生授权，成功后
返回结果供设备窗口接续。窗口关闭/销毁时清空密码并中止读取；设备层关闭不取消已交给
目标切换控制器的连接流程。相关前端回归见 `test_account_device_windows.mjs`。
