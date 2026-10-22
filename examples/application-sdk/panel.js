export function activate(sdk) {
  const disposePanel = sdk.ui.registerPanel("example.application", {
    title: "Application SDK",
    async render(root, { signal }) {
      const info = await sdk.call("runtime.info", {}, { signal });
      const text = document.createElement("pre");
      text.textContent = JSON.stringify(info, null, 2); root.append(text);
    },
  });
  const disposeMenu = sdk.ui.registerMenu("example.application.menu", {
    label: "Application SDK", execute: () => sdk.ui.openPanel("example.application"),
  });
  return () => { disposeMenu(); disposePanel(); };
}
