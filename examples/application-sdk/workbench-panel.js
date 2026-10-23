// Frontend for a C-registered type whose frontend is example.rich-plan.
// The host injects the resource owner's immutable SDK into this renderer.
export function activate(sdk) {
  return sdk.ui.registerRenderer("example.rich-plan", (root, plan, { sdk, context, signal, events }) => {
    const title = document.createElement("h2"); title.textContent = "插件计划"; root.append(title);
    for (const item of plan.items || []) {
      const line = document.createElement("p"); line.textContent = `${item.done ? "✓" : "○"} ${item.text}`; root.append(line);
    }
    const status = document.createElement("small"); status.textContent = `项目：${context.project}`; root.append(status);
    const onVisibility = event => { status.dataset.visible = String(event.detail); };
    events.addEventListener("visibility", onVisibility, { signal });
    return () => { /* Release plugin-owned listeners, connections and workers. */ };
  });
}
