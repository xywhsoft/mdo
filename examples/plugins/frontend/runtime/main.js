export function activate(sdk) {
  sdk.ui.registerPanel("example.frontend.hello", {
    title: "前端插件范例", width: "40rem", height: "auto",
    render(root) {
      const text = document.createElement("p");
      text.textContent = "这个窗口和菜单由插件创建，停用时由所属 SDK 作用域清理。";
      root.append(text);
    },
  });
  sdk.ui.registerMenu("example.frontend.open", {
    label: "插件范例窗口",
    execute: () => sdk.ui.openPanel("example.frontend.hello"),
  });
}
