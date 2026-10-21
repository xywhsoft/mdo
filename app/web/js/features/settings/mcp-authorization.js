import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, errorMessage } from "../../utils/dom.js";

export function createMcpAuthorization() {
  let dialog, controller, unsubscribe, timer;
  const close = () => {
    clearTimeout(timer); controller?.abort(); unsubscribe?.();
    dialog?.close(); dialog?.remove(); dialog = null;
  };
  function open(id) {
    close(); const owner = targetState().selected?.id || "local";
    controller = new AbortController(); const { signal } = controller;
    const current = () => dialog && !signal.aborted && owner === (targetState().selected?.id || "local");
    const path = `/mcp/${encodeURIComponent(id)}/oauth`;
    dialog = element("dialog", { className: "extension-editor" });
    const status = element("p", { attrs: { role: "status" } });
    let busy = false;
    const buttons = [];
    const run = async fn => {
      if (!current() || busy) return;
      busy = true; buttons.forEach(b => { b.disabled = true; });
      try { await fn(); await poll(); }
      catch (error) { if (current()) status.textContent = errorMessage(error); }
      finally { busy = false; if (current()) buttons.forEach(b => { b.disabled = false; }); }
    };
    const button = (text, fn) => {
      const node = element("button", { text, attrs: { type: "button" }, className: "secondary-button" });
      node.addEventListener("click", () => { void run(fn); }); buttons.push(node); return node;
    };
    async function poll() {
      clearTimeout(timer);
      try {
        const { data } = await api.get(path, { signal }); if (!current()) return;
        const labels = { idle: "等待授权", discovery: "正在发现授权服务…", ready: "已准备好，请打开授权网页", exchange: "正在完成授权…", error: "授权未完成" };
        status.textContent = `${data.authorized ? "已授权" : labels[data.state] || data.state}${data.http_status >= 400 ? ` · HTTP ${data.http_status}` : ""}${data.message ? `：${data.message}` : ""}`;
        if (data.busy || data.state === "ready") timer = setTimeout(poll, 1500);
      } catch (error) { if (current()) status.textContent = errorMessage(error); }
    }
    const done = element("button", { text: "关闭", attrs: { type: "button" } }); done.addEventListener("click", close);
    dialog.append(element("h2", { text: `MCP 授权 · ${id}` }),
      element("p", { text: "授权网页将在该 MCP 所属设备的浏览器中打开。完成后返回墨斗；凭据保存在该设备的系统凭据保护下。" }),
      status, button("开始授权", () => api.post(`${path}/start`, {}, { signal })),
      button("打开授权网页", () => api.post(`${path}/open`, {}, { signal })),
      button("撤销本机授权", async () => {
        if (window.confirm("移除此设备保存的 MCP 授权？")) await api.delete(path, { signal });
      }), done);
    dialog.addEventListener("cancel", event => { event.preventDefault(); close(); });
    document.body.append(dialog); dialog.showModal();
    unsubscribe = subscribeTarget(() => { if (!current()) close(); }); void poll();
  }
  return { open, destroy: close };
}
