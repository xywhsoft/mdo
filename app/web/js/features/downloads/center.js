import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { downloadRegistry, downloadActive } from "../../state/downloads.js";
import { t, subscribeLocale } from "../../i18n.js";
import { element } from "../../utils/dom.js";

const text = (key, args = {}) => t(`downloads.${key}`, args, key);
const node = (tag, value, cls) => element(tag, { ...(value == null ? {} : { text: value }), className: cls });
const bytes = size => `${(size / 1048576).toFixed(1)} MiB`;
export function createDownloadCenter({ updatePanel, navigation, registry = downloadRegistry, transport = api } = {}) {
  const entry = document.querySelector("#open-downloads");
  if (!entry) return null;
  const dialog = element("dialog", { className: "download-center", attrs: { id: "downloads-dialog", "aria-labelledby": "downloads-title" } });
  const heading = node("header"), title = element("h2", { attrs: { id: "downloads-title" } }), close = node("button", "", "secondary-button");
  close.type = "button"; close.addEventListener("click", () => dialog.close()); heading.append(title, close);
  const message = element("p", { className: "download-error", attrs: { role: "alert" } }), list = node("div"), clear = node("button", "", "secondary-button");
  clear.type = "button"; dialog.append(heading, message, list, clear); document.body.append(dialog);
  let pending = false, error = "", timer = 0, destroyed = false, polling = false, signature = "";
  const target = () => targetState().selected?.id || "local";
  const writable = () => { const s = targetState(); return !s.selected || s.connected && !s.runtimeChanged && s.selected.mode !== "view"; };
  async function refresh() {
    if (destroyed || polling) return; polling = true;
    try { await Promise.allSettled([transport.get("/cli-tools"), transport.get("/update"), transport.get("/distribution"), transport.get("/ecosystem")]); }
    finally { polling = false; schedule(); }
  }
  function schedule() {
    clearTimeout(timer); if (!destroyed && dialog.open) timer = setTimeout(refresh, registry.list(target()).some(downloadActive) ? 1000 : 5000);
  }
  async function act(work) {
    if (pending || !writable()) return; const owner = target(); pending = true; error = ""; render();
    try { await work(); }
    catch (e) { if (owner === target()) error = e.message; }
    finally { pending = false; if (!destroyed) { render(); void refresh(); } }
  }
  function action(row, label, work) {
    const owner = target(), button = node("button", label, "secondary-button"); button.type = "button";
    button.disabled = pending || !writable(); button.addEventListener("click", () => { if (target() === owner) void act(work); }); return button;
  }
  function render() {
    const rows = registry.list(target()), active = rows.filter(downloadActive).length;
    entry.querySelector("[data-downloads-label]").textContent = text("title");
    const badge = entry.querySelector("[data-downloads-count]"); badge.textContent = String(active); badge.hidden = !active;
    entry.setAttribute("aria-label", text("entry", { count: active })); entry.title = text("title");
    title.textContent = text("title"); close.textContent = text("close"); clear.textContent = text("clear");
    message.textContent = error; message.hidden = !error;
    clear.disabled = pending || !writable() || !rows.some(r => !downloadActive(r) && r.state !== "ready");
    if (!dialog.open) return;
    const next = JSON.stringify([rows, error, pending, writable(), text("title")]); if (signature === next) return; signature = next;
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
      if (row.source === "cli") {
        if (downloadActive(row)) controls.append(action(row, text("cancel"), () => transport.post("/cli-tools", { action: "cancel", id: row.taskId })));
        else if (["failed", "cancelled", "interrupted"].includes(row.state)) controls.append(action(row, text("retry"), () => transport.post("/cli-tools", { action: "retry", id: row.taskId })));
      } else if (row.source === "legacy") {
        if (downloadActive(row)) controls.append(action(row, text("cancel"), () => transport.post("/distribution", { action: "cancel", id: row.resourceId })));
      } else if (row.source === "update") {
        if (row.state === "downloading") controls.append(action(row, text("cancel"), () => transport.delete("/update/download")));
        if (["ready", "failed", "cancelled", "unknown"].includes(row.state)) { const open = node("button", text("viewUpdate"), "secondary-button"); open.type = "button"; open.addEventListener("click", () => { dialog.close(); updatePanel?.open(); }); controls.append(open); }
      } else if (["failed", "unknown"].includes(row.state)) {
        const open = node("button", text("viewStore"), "secondary-button"); open.type = "button"; open.addEventListener("click", () => { dialog.close(); navigation?.openSettings("store"); }); controls.append(open);
      }
      item.append(controls); list.append(item);
    }
  }
  function open() { if (!dialog.open) dialog.showModal(); signature = ""; render(); void refresh(); }
  entry.addEventListener("click", open); dialog.addEventListener("close", () => { clearTimeout(timer); signature = ""; });
  clear.addEventListener("click", () => { const owner = target(); void act(async () => { await transport.post("/cli-tools", { action: "clear" }); if (owner === target()) registry.clear(owner); }); });
  const unsubscribe = registry.subscribe(render), locale = subscribeLocale(() => { signature = ""; render(); });
  const targets = subscribeTarget(() => { error = ""; signature = ""; dialog.close(); render(); });
  render();
  return { open, refresh, destroy() { destroyed = true; clearTimeout(timer); unsubscribe(); locale(); targets(); entry.removeEventListener("click", open); dialog.close(); dialog.remove(); } };
}
