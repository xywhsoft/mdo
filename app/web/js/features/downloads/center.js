import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { downloadRegistry, downloadActive } from "../../state/downloads.js";
import { t, subscribeLocale } from "../../i18n.js";
import { element } from "../../utils/dom.js";
import { openLayer } from "../../components/layers.js";

const text = (key, args = {}) => t(`downloads.${key}`, args, key);
const node = (tag, value, cls) => element(tag, { ...(value == null ? {} : { text: value }), className: cls });
const bytes = size => `${(size / 1048576).toFixed(1)} MiB`;
export function createDownloadCenter({ updatePanel, navigation, registry = downloadRegistry, transport = api } = {}) {
  const entry = document.querySelector("#open-downloads");
  if (!entry) return null;
  const message = element("p", { className: "download-error", attrs: { role: "alert" } }), list = node("div", null, "download-list"), clear = node("button", "", "secondary-button"), close = node("button", "", "secondary-button");
  clear.type = close.type = "button";
  let layer = null, pending = false, error = "", timer = 0, destroyed = false, polling = false, signature = "";
  const target = () => targetState().selected?.id || "local";
  const writable = () => { const s = targetState(); return !s.selected || s.connected && !s.runtimeChanged && s.selected.mode !== "view"; };
  async function refresh() {
    if (destroyed || polling) return; polling = true;
    try { await Promise.allSettled([transport.get("/extension-tasks"), transport.get("/update"), transport.get("/distribution")]); }
    finally { polling = false; schedule(); }
  }
  function schedule() {
    clearTimeout(timer); if (!destroyed && layer) timer = setTimeout(refresh, registry.list(target()).some(downloadActive) ? 1000 : 5000);
  }
  async function act(work) {
    if (destroyed || pending || !writable()) return; const owner = target(); pending = true; error = ""; render();
    try { await work(); }
    catch (e) { if (owner === target()) error = e.message; }
    finally { pending = false; if (!destroyed) { render(); void refresh(); } }
  }
  function action(row, label, work) {
    const owner = target(), view = layer, button = node("button", label, "secondary-button"); button.type = "button";
    button.dataset.downloadAction = `${row.key}:${label}`;
    button.disabled = pending || !writable(); button.addEventListener("click", () => { if (target() === owner && layer === view && layer) void act(work); }); return button;
  }
  function render() {
    if (destroyed) return;
    const rows = registry.list(target()), active = rows.filter(downloadActive).length;
    entry.querySelector("[data-downloads-label]").textContent = text("title");
    const badge = entry.querySelector("[data-downloads-count]"); badge.textContent = String(active); badge.hidden = !active;
    entry.setAttribute("aria-label", text("entry", { count: active })); entry.title = text("title");
    close.textContent = text("close"); clear.textContent = text("clear");
    message.textContent = error; message.hidden = !error;
    clear.disabled = pending || !writable() || !rows.some(r => !downloadActive(r) && r.state !== "ready");
    if (!layer) return;
    layer.update({ title: text("title") });
    const next = JSON.stringify([rows, error, pending, writable(), text("title")]); if (signature === next) return; signature = next;
    const scroll = layer.body.scrollTop, focused = list.contains(document.activeElement) ? document.activeElement.dataset.downloadAction : null;
    list.replaceChildren(); if (!rows.length) list.append(node("p", text("empty"), "download-empty"));
    for (const row of rows) {
      const item = node("article", null, "download-row"), top = node("div", null, "download-row-heading");
      const kind = row.source === "cli" ? text(`cli.${row.action}`) : text(row.source === "update" ? "app" : row.source === "legacy" ? "legacy" : "extension");
      top.append(node("strong", row.name || kind), node("span", text(`state.${row.state}`))); item.append(top, node("small", `${kind}${row.buildId ? ` · ${row.buildId}` : ""}`, "download-meta"));
      if (downloadActive(row)) {
        const progress = node("progress"); progress.setAttribute("aria-label", `${row.name || kind} · ${text(`state.${row.state}`)}`);
        if (row.total > 0 && typeof row.done === "number") { progress.max = row.total; progress.value = Math.max(0, Math.min(row.done, row.total)); item.append(node("small", `${bytes(row.done)} / ${bytes(row.total)}`)); }
        item.append(progress);
      }
      if (row.error && row.state !== "cancelled") item.append(node("p", row.error, "download-error"));
      if (row.state === "unknown") item.append(node("p", text("uncertain"), "download-meta"));
      if (row.submission) item.append(node("p", `${text("submission")} #${row.submission.id} · ${row.submission.state}`, "download-meta"));
      const controls = node("div", null, "download-actions");
      if (row.source === "archive") {
        if (!["complete", "cancelled", "expired"].includes(row.state)) controls.append(action(row, text("cancel"), () => transport.delete(`/extension-tasks/${row.taskId}`)));
        if (row.state === "ready" && row.action === "install") controls.append(action(row, "安装", async () => {
          if (!confirm("已检查归档内容并信任其来源，允许安装此扩展？")) return;
          let previous=null;if(row.store){try{previous=(await transport.get(`/extensions/${row.kind}/${encodeURIComponent(row.store.xid)}`)).data;}catch(e){if(e.status!==404)throw e;}}
          return transport.post(`/extension-tasks/${row.taskId}/install`, { trust_code: true, expected_revision:previous?.revision||"",...(row.store ? {} : { id: row.directoryId || `import-${row.taskId}` }) });
        }));
        if (row.state === "ready" && row.publication) controls.append(action(row, text("retry"), () => {
          const { upload, nonce, proof_upload, ...metadata } = row.publication;
          return transport.post("/ecosystem", { ...metadata, action: "submit", task: row.taskId, ...(proof_upload ? { proof_task: proof_upload } : {}) });
        }));
        if (["complete", "cancelled", "failed", "expired"].includes(row.state)) controls.append(action(row, "清除", async () => {
          if (row.state === "expired") await transport.delete(`/extension-tasks/${row.taskId}`);
          await transport.delete(`/extension-tasks/${row.taskId}/forget`);registry.hide?.(row.key);
        }));
      } else if (row.source === "cli") {
        if (downloadActive(row)) controls.append(action(row, text("cancel"), () => transport.post("/cli-tools", { action: "cancel", id: row.taskId })));
        else if (["failed", "cancelled", "interrupted"].includes(row.state)) controls.append(action(row, text("retry"), () => transport.post("/cli-tools", { action: "retry", id: row.taskId })));
      } else if (row.source === "legacy") {
        if (downloadActive(row)) controls.append(action(row, text("cancel"), () => transport.post("/distribution", { action: "cancel", id: row.resourceId })));
      } else if (row.source === "update") {
        if (row.state === "downloading") controls.append(action(row, text("cancel"), () => transport.delete("/update/download")));
        if (["ready", "failed", "cancelled", "unknown"].includes(row.state)) controls.append(destination(row, text("viewUpdate"), () => updatePanel?.open()));
      } else if (["failed", "unknown"].includes(row.state)) {
        controls.append(destination(row, text("viewStore"), () => navigation?.openSettings("store")));
      }
      item.append(controls); list.append(item);
    }
    layer.body.scrollTop = scroll;
    if (focused) ([...list.querySelectorAll("button")].find(button => button.dataset.downloadAction === focused && !button.disabled) || layer.closeButton).focus({ preventScroll: true });
  }
  function destination(row, label, navigate) {
    const owner = target(), view = layer, button = node("button", label, "secondary-button"); button.type = "button";
    button.dataset.downloadAction = `${row.key}:${label}`;
    button.addEventListener("click", () => { if (!destroyed && target() === owner && layer === view && layer) { void view.close(); navigate(); } });
    return button;
  }
  function open() {
    if (destroyed) return;
    if (!layer) {
      layer = openLayer({ title: text("title"), icon: "download", width: "48rem", height: "75%",
        onClosed() { layer = null; clearTimeout(timer); signature = ""; entry.setAttribute("aria-expanded", "false"); } });
      layer.element.id = "downloads-dialog"; layer.element.classList.add("download-center");
      layer.body.append(message, list); layer.footer.append(clear, close);
      entry.setAttribute("aria-expanded", "true");
    }
    signature = ""; render(); void refresh();
  }
  entry.addEventListener("click", open);
  close.addEventListener("click", () => { void layer?.close(); });
  clear.addEventListener("click", () => { if (!layer) return; const owner = target(); void act(async () => { await transport.post("/cli-tools", { action: "clear" }); if (owner === target()) registry.clear(owner); }); });
  const unsubscribe = registry.subscribe(render), locale = subscribeLocale(() => { signature = ""; render(); });
  let selectedTarget = target();
  const targets = subscribeTarget(() => {
    if (selectedTarget !== target()) { selectedTarget = target(); error = ""; signature = ""; layer?.dispose(); }
    render();
  });
  render();
  return { open, refresh, destroy() { destroyed = true; clearTimeout(timer); unsubscribe(); locale(); targets(); entry.removeEventListener("click", open); layer?.dispose(); } };
}
