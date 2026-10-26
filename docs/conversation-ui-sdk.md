# 会话界面扩展入口

前端插件通过同一个 Application SDK 扩展当前会话，不依赖 mdo 内部 DOM 选择器，也不修改主页面源码。

```js
export function activate(sdk) {
  sdk.ui.registerConversationAction("example.open", {
    label: "查看扩展信息", icon: "branch",
    when: async ({ sdk, context, signal }) => Boolean(context.session),
    execute: ({ sdk }) => sdk.ui.openPanel("example.panel"),
  });
  sdk.ui.registerConversationView("example.summary", {
    when: ({ context }) => Boolean(context.session),
    mount(root, { sdk, context, signal }) {
      root.textContent = `此信息属于 ${context.project}/${context.session}`;
      return () => { /* 清理订阅、监听与该视图持有的资源。 */ };
    },
  });
}
```

操作入口显示在会话标题栏；视图显示在会话内容上方。`when` 可以异步读取功能状态，必须尊重 `signal`。`context` 包含 `project`、`session`、`view`，提供的 `sdk` 已绑定到该会话。不要在异步操作中重新读取全局当前会话再发送写入。

返回的注销函数清理该项贡献；插件停用会自动注销。导航切换立即取消旧视图的信号，旧的异步判断、渲染和按钮结果不能进入新会话。渲染返回的清理函数可以异步完成，插件退场会等待渲染、操作和清理结束后，再回收所属工作区面板。清理失败会沿用插件的失败状态，不提前宣告已释放。

后台任务不属于界面的生命周期。关闭面板只停止读取与渲染，不应默认取消用户已经授权的后台工作。远程目标仍使用现有目标传输；离线不能回落到本机执行。

页面重载只卸载前端挂载，保留已保存布局引用的原生面板记录。插件重新注册面板后，框架自动替换恢复期间的“渲染器未加载”占位。运行中停用或替换插件仍会关闭其所属面板，不能借重载规则保留已经退场的界面资源。

回归：`node --test tests/test_conversation_contributions.mjs tests/test_application_sdk_ui.mjs`，覆盖固定会话绑定、导航、延迟判断/渲染、异步退场和失败保留。
