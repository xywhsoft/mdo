import { setUiText as writeUiText, setUiAttribute as writeUiAttribute, t as uiText, uiMessage, t, subscribeLocale } from "../../i18n.js";

import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { downloadRegistry, downloadActive } from "../../state/downloads.js";

import { element } from "../../utils/dom.js";
import { openLayer } from "../../components/layers.js";
import { extensionInstaller } from "../../state/extension-installs.js";

const text = (key, args = {}) => t(`downloads.${key}`, args, key);
const node = (tag, value, cls) => element(tag, { ...(value == null ? {} : { text: value }), className: cls });
const bytes = size => size>=1073741824?`${(size/1073741824).toFixed(1)} GiB`:size>=1048576?`${(size/1048576).toFixed(1)} MiB`:size>=1024?`${(size/1024).toFixed(1)} KiB`:`${size} B`;
export function createDownloadCenter({ updatePanel, navigation, registry = downloadRegistry, transport = api, installer = extensionInstaller } = {}) {
  const entry = document.querySelector("#open-downloads");
  if (!entry) return null;
  const message = element("p", { className: "download-error", attrs: { role: "alert" } }), list = node("div", null, "download-list"), clear = node("button", "", "secondary-button"), close = node("button", "", "secondary-button");
  clear.type = close.type = "button";
  let layer = null, pending = false, error = "", readError = "", timer = 0, destroyed = false, polling = false, signature = "";
  const target = () => targetState().selected?.id || "local";
  const writable = () => { const s = targetState(); return !readError && (!s.selected || s.connected && !s.runtimeChanged && s.selected.mode !== "view"); };
  const clearable = row => !downloadActive(row) && !["ready","paused"].includes(row.state) && !(row.source === "install" && row.state === "unknown");
  async function refresh() {
    if (destroyed || polling) return; polling = true; const owner=target();
    try {
      const results=await Promise.allSettled([transport.get("/extension-tasks"), transport.get("/download-tasks"), transport.get("/update"), transport.get("/distribution"),installer.refresh({discover:true})]);
      if(!destroyed && owner===target()){readError=results[1].status==="rejected"?text("loadFailed"):"";render();}
    }
    finally { polling = false; schedule(); }
  }
  function schedule() {
    clearTimeout(timer);const active=registry.list(target()).some(downloadActive);
    if (!destroyed && (layer||active)) timer = setTimeout(refresh, active ? 1000 : 5000);
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
    writeUiText(entry.querySelector("[data-downloads-label]"), text("title"));
    const badge = entry.querySelector("[data-downloads-count]"); writeUiText(badge, String(active)); badge.hidden = !active;
    writeUiAttribute(entry, "aria-label", text("entry", { count: active })); writeUiAttribute(entry, "title", text("title"));
    writeUiText(close, text("close")); writeUiText(clear, text("clear"));
    writeUiText(message, error || readError); message.hidden = !error && !readError;
    clear.disabled = pending || !writable() || !rows.some(clearable);
    if (!layer) return;
    layer.update({ title: text("title") });
    const next = JSON.stringify([rows, error, readError, pending, writable(), text("title")]); if (signature === next) return; signature = next;
    const scroll = layer.body.scrollTop, focused = list.contains(document.activeElement) ? document.activeElement.dataset.downloadAction : null;
    list.replaceChildren(); if (!rows.length) list.append(node("p", text("empty"), "download-empty"));
    for (const row of rows) {
      const item = node("article", null, "download-row"), top = node("div", null, "download-row-heading");
      const kind = row.source === "cli" ? text(`cli.${row.action}`) : text(row.nativeSource === "transfer" ? "file" : row.source === "update" || row.nativeSource === "update" ? "app" : row.source === "legacy" ? "legacy" : "extension");
      top.append(node("strong", row.name || kind), node("span", text(`state.${row.phase || row.state}`))); item.append(top, node("small", `${kind}${row.buildId ? ` · ${row.buildId}` : ""}`, "download-meta"));
      if(row.source === "native" || row.source === "install" && row.transfers?.length) {
        item.append(node("small",text("engine",{engine:row.engine === "aria2" ? "aria2" : text("nativeEngine")}),"download-meta"));
        if(row.fallbackReason) item.append(node("p",text(row.fallbackReason === "aria2_network_policy" ? "networkFallback" : "fallback"),"download-meta"));
      }
      if (downloadActive(row) || row.state === "paused") {
        const progress = node("progress"); writeUiAttribute(progress, "aria-label", `${row.name || kind} · ${text(`state.${row.state}`)}`);
        if (row.total > 0 && typeof row.done === "number") { progress.max = row.total; progress.value = Math.max(0, Math.min(row.done, row.total)); item.append(node("small", `${bytes(row.done)} / ${bytes(row.total)}`)); }
        item.append(progress);
        if(row.speed > 0) item.append(node("small",text("speed",{speed:bytes(row.speed)}),"download-meta"));
        if(row.speed > 0 && Number.isFinite(row.eta)) item.append(node("small",text("eta",{seconds:Math.ceil(row.eta)}),"download-meta"));
      }
      if (row.error && row.state !== "cancelled") {
        const summary=row.errorCode?t(`downloads.error.${row.errorCode}`,{},row.error):row.error;
        item.append(node("p",summary,"download-error"));
        if(summary!==row.error){const details=node("details");details.append(node("summary",text("errorDetails")),node("p",row.error,"download-meta"));item.append(details);}
      }
      if (row.state === "unknown") item.append(node("p", text("uncertain"), "download-meta"));
      if (row.cleanupPending) item.append(node("p", text("cleanupPending"), "download-error"));
      if (row.submission) item.append(node("p", `${text("submission")} #${row.submission.id} · ${row.submission.state}`, "download-meta"));
      const controls = node("div", null, "download-actions");
      if (row.source === "native") {
        const control = verb => transport.post(`/download-tasks/${row.taskId}/${verb}`,{});
        if(row.active && ["downloading","retrying"].includes(row.state)) controls.append(action(row,text("pause"),()=>control("pause")));
        if(row.state === "paused") controls.append(action(row,text("resume"),()=>control("resume")));
        if(row.state === "failed") controls.append(action(row,text("retry"),()=>control("retry")));
        if(row.cleanupPending) controls.append(action(row,text("retryCleanup"),()=>control("cleanup")));
        if(downloadActive(row) || row.state === "paused" || row.state === "failed") controls.append(action(row,text("cancelDelete"),()=>control("cancel")));
        if(row.nativeSource === "update" && ["ready","failed","paused"].includes(row.state)) controls.append(destination(row,text("viewUpdate"),()=>updatePanel?.open()));
        if(row.nativeSource === "extension" && row.state === "ready" && row.archiveId) controls.append(action(row,uiMessage("extensionBrowser.install",{},"安装"),()=>installArchive({...row,taskId:row.archiveId})));
        if(["complete","expired","cancelled","failed"].includes(row.state)) controls.append(action(row,text("clear"),async()=>{
          if(row.archiveId && row.state === "complete") await transport.delete(`/extension-tasks/${row.archiveId}/forget`);
          await control("forget");registry.hide(row.key);
        }));
      } else if (row.source === "install") {
        const transfers = row.transfers || [];
        const pausable = transfers.filter(child => child.active && ["downloading", "retrying"].includes(child.state));
        const paused = transfers.filter(child => child.state === "paused");
        const control = async (children, verb) => {
          for (const child of children) await transport.post(`/download-tasks/${child.taskId}/${verb}`, {});
        };
        if(downloadActive(row) && row.state !== "cancelling") {
          if(pausable.length)controls.append(action(row,text("pause"),()=>control(pausable,"pause")));
          if(paused.length)controls.append(action(row,text("resume"),()=>control(paused,"resume")));
        }
        if(downloadActive(row) || row.state === "unknown")controls.append(action(row,text("cancel"),()=>installer.cancel(row)));
        else controls.append(action(row,text("clear"),()=>registry.hide(row.key)));
      } else if (row.source === "archive") {
        if (!["complete", "cancelled", "expired"].includes(row.state)) controls.append(action(row, text("cancel"), () => transport.delete(`/extension-tasks/${row.taskId}`)));
        if (row.state === "ready" && row.action === "install") controls.append(action(row, uiMessage("extensionBrowser.install", {}, "安装"), async () => {
          return installArchive(row);
        }));
        if (row.state === "ready" && row.publication) controls.append(action(row, text("retry"), () => {
          const { upload, nonce, proof_upload, ...metadata } = row.publication;
          return transport.post("/ecosystem", { ...metadata, action: "submit", task: row.taskId, ...(proof_upload ? { proof_task: proof_upload } : {}) });
        }));
        if (["complete", "cancelled", "failed", "expired"].includes(row.state)) controls.append(action(row, uiMessage("ui.center.clear", {}, "清除"), async () => {
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
  async function installArchive(row) {
    if(!confirm(uiMessage("ui.center.haveYouReviewedTheArchiveAndTrustItsSource",{},"已检查归档内容并信任其来源，允许安装此扩展？"))) return;
    let previous=null;
    if(row.store)try{previous=(await transport.get(`/extensions/${row.kind}/${encodeURIComponent(row.store.xid)}`)).data;}catch(e){if(e.status!==404)throw e;}
    return transport.post(`/extension-tasks/${row.taskId}/install`,{trust_code:true,expected_revision:previous?.revision||"",...(row.store?{}:{id:row.directoryId||`import-${row.taskId}`})});
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
        onClosed() { layer = null; signature = ""; entry.setAttribute("aria-expanded", "false"); schedule(); } });
      layer.element.id = "downloads-dialog"; layer.element.classList.add("download-center");
      layer.body.append(message, list); layer.footer.append(clear, close);
      entry.setAttribute("aria-expanded", "true");
    }
    signature = ""; render(); void refresh();
  }
  entry.addEventListener("click", open);
  close.addEventListener("click", () => { void layer?.close(); });
  clear.addEventListener("click", () => { if (!layer) return; const owner = target(); void act(async () => {
    const failures=[];
    for(const row of registry.list(owner).filter(clearable)){
      if(owner!==target()||!writable())return;
      try{
        if(row.source==="native") {
          if(row.archiveId && row.state === "complete") await transport.delete(`/extension-tasks/${row.archiveId}/forget`);
          await transport.post(`/download-tasks/${row.taskId}/forget`,{});
        }else if(row.source==="archive"){
          let current;
          try{current=(await transport.get(`/extension-tasks/${row.taskId}`)).data;if(!current||typeof current.state!=="string")throw Error(uiText("ui.center.cannotConfirmTheTaskSCurrentStateTheTask", {}, "无法确认任务当前状态，已保留此任务"));}
          catch(e){if(e.status!==404)throw e;}
          if(owner!==target()||!writable())return;
          if(current){
            if(current.active||!["complete","cancelled","failed","expired"].includes(current.state))throw Error(uiText("ui.center.taskStateChangedTheTaskIsRetained", {}, "任务状态已变化，已保留此任务"));
            if(current.state==="expired")await transport.delete(`/extension-tasks/${row.taskId}`);
            if(owner!==target()||!writable())return;
            await transport.delete(`/extension-tasks/${row.taskId}/forget`);
          }
        }
        // Update and retired-manager rows are presentation history. Ready
        // downloads and active operations are never cleared by this action.
        registry.hide?.(row.key);
      }catch(e){failures.push(`${row.name||row.taskId}: ${e.message}`);}
    }
    if(failures.length)throw Error(failures.join("；"));
  }); });
  const unsubscribe = registry.subscribe(()=>{render();schedule();}), locale = subscribeLocale(() => { signature = ""; render(); });
  let selectedTarget = target();
  const targets = subscribeTarget(() => {
    if (selectedTarget !== target()) { selectedTarget = target(); error = ""; readError = ""; signature = ""; layer?.dispose(); }
    render();
  });
  render();
  return { open, refresh, destroy() { destroyed = true; clearTimeout(timer); unsubscribe(); locale(); targets(); entry.removeEventListener("click", open); layer?.dispose(); } };
}
