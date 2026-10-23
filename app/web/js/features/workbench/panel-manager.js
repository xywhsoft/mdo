import { normalizeLayout, reorderPanel, safeBrowserUrl, validateImageFrame } from "./model.js";
import { createNativeBrowserBridge } from "./native-browser.js";
import { mountBrowserView } from "./browser-view.js";
import { mountFilesView } from "./files-view.js";
import { mountChangesView } from "./changes-view.js";
import { mountTerminalView } from "./terminal-view.js";
import { mountStartView } from "./start-view.js";
import { filePathKey, relocatedPath } from "./file-actions.js";
import { createFileDrafts } from "./file-drafts.js";
import { iconButton, popupMenu } from "./controls.js";
import { createSurfaceInput } from "./surface-input.js";
import { renderSourceMarkdown } from "../../utils/source-markdown.js";
import { toast } from "../../utils/dom.js";
import { isRemoteTarget, targetState } from "../../api/target.js";

const make = (tag, className, text) => { const node = document.createElement(tag); if (className) node.className = className; if (text !== undefined) node.textContent = text; return node; };
function button(label, execute, className = "wb-button") {
  const node = make("button", className, label); node.type = "button"; node.title = label;
  node.onclick = () => Promise.resolve().then(execute).catch(error => toast(error.message, "error")); return node;
}
export function createPanelManager({ shell, api, layoutApi = api, navigation, clientId, addContext = () => {}, renderers = new Map() }) {
  const entries = new Map(), definitions = new Map(), discovered = new Set(), mounts = new Map(), bridge = createNativeBrowserBridge(), fileEvents = new EventTarget();
  const dock = make("aside", "workbench"), top = make("header", "wb-top"), tabs = make("div", "wb-tabs"), content = make("div", "wb-content");
  dock.setAttribute("aria-label", "工具工作区"); tabs.setAttribute("role", "tablist");
  const resize = make("div", "wb-resize"); resize.tabIndex = 0; resize.setAttribute("role", "separator"); resize.setAttribute("aria-label", "调整工具工作区宽度"); resize.setAttribute("aria-orientation", "vertical");
  let layout = normalizeLayout(), revision = 0, stopped = false, ready = false, edited = false, saving = false, dirty = false, saveTimer = 0, dragId = "", syncTimer = 0, syncing = null;
  let profile;
  try { profile = localStorage.getItem("mdo.workbench.profile"); if (!/^wb-[a-f0-9-]{36}$/.test(profile || "")) { profile = `wb-${crypto.randomUUID()}`; localStorage.setItem("mdo.workbench.profile", profile); } }
  catch { profile = clientId; }
  if (isRemoteTarget()) profile += `.${targetState().selected.id}`;
  const displayTarget = isRemoteTarget() ? targetState().selected.id : "local";
  const bound = row => api.bind({ ...row.context, extension: row.owner });
  const title = row => row.title || definitions.get(row.type)?.title || row.type;
  const fail = (root, error, retry) => { root.replaceChildren(make("p", "wb-error", error.message || String(error)), button("重新读取", retry)); };
  const save = async () => {
    if (!ready || saving || stopped) return;
    saving = true;
    try { while (dirty && !stopped) {
      dirty = false;
      const restore = layout.order.map(id => entries.get(id)).filter(Boolean).map(row => ({ id: row.id, owner: row.owner, context: row.context }));
      const result = await layoutApi.call("panel.layout.put", { id: profile, revision, value: { ...layout, restore } }); revision = result.revision;
    } }
    catch (error) { dirty = true; toast(`工作区布局未保存：${error.message}`, "error"); }
    finally { saving = false; }
  };
  const remember = () => { edited = true; dirty = true; clearTimeout(saveTimer); saveTimer = setTimeout(save, 300); };
  const disposeMount = id => {
    const mount = mounts.get(id); if (!mount) return; mounts.delete(id); mount.abort.abort(); mount.observer?.disconnect(); mount.resizeObserver?.disconnect();
    try { mount.dispose?.(); } catch (error) { console.error("Panel cleanup failed", error); } mount.root.remove();
  };
  const close = async id => {
    const row = entries.get(id); if (!row) return;
    if (mounts.get(id)?.dirty?.() && !confirm("此面板有未保存修改，仍要关闭吗？")) return;
    await bound(row).call("panel.close", { id }); disposeMount(id); entries.delete(id); layout.order = layout.order.filter(item => item !== id);
    if (layout.active === id) layout.active = layout.order.at(-1) || ""; if (layout.secondary === id) layout.secondary = "";
    remember(); render();
  };
  const reveal = id => { if (!entries.has(id)) return; layout.active = id; if (layout.secondary === id) layout.secondary = ""; layout.open = true; remember(); render(); };
  const update = async (id, patch) => { const row = entries.get(id); const saved = await bound(row).call("panel.update", { id, revision: row.revision, ...patch }); entries.set(id, saved); remember(); render(); return saved; };
  const handle = id => Object.freeze({ id, reveal: () => reveal(id), close: () => close(id), update: patch => update(id, patch) });
  let fileRecordChanges = Promise.resolve();
  fileEvents.addEventListener("changed", event => {
    if (event.detail.kind !== "move") return;
    fileRecordChanges = fileRecordChanges.then(async () => {
      const change = event.detail, move = path => path ? relocatedPath(path, change.path, change.destination, change.directory) : path;
      for (const [id, row] of entries) {
        if (stopped || !["mdo.editor", "mdo.files"].includes(row.type) || row.context.project !== change.project || mounts.get(id)?.edit) continue;
        // Mounted ready views retain their buffers themselves. Hidden/unready
        // records still need their persisted path and private draft migrated.
        const files = row.view_state?.files || {}, previous = row.params?.path || files.selected, next = move(previous), root = move(files.root), parameterRoot = move(row.params?.root);
        if (next === previous && root === files.root && parameterRoot === row.params?.root) continue;
        if (mounts.has(id)) disposeMount(id);
        const drafts = createFileDrafts({ context: { ...row.context, target: displayTarget, extension: row.owner }, panel: id });
        let draftSource = files.draft_path || "", copied = false;
        if (previous && next !== previous) {
          try { drafts.write(next, drafts.own(previous, draftSource) || { cleared: true }); copied = true; }
          catch { draftSource ||= previous; }
        }
        const saved = await bound(row).call("panel.update", { id, revision: row.revision,
          ...(row.params?.path ? { title: next.split(/[\\/]/).at(-1) } : {}),
          params: { ...row.params, ...(row.params?.path ? { path: next } : {}), ...(row.params?.root ? { root: parameterRoot } : {}) },
          view_state: { ...row.view_state, files: { ...files, selected: next, root, draft_path: copied ? "" : draftSource, expanded: (files.expanded || []).map(move) } },
        });
        entries.set(id, saved);
        if (copied) try { drafts.clear(previous); if (draftSource && draftSource !== previous) drafts.clear(draftSource); } catch { /* Keep redundant working copies. */ }
      }
      remember(); render();
    }).catch(error => { if (!stopped) toast(`文件已改名，但面板状态未保存：${error.message}`, "error"); });
  });
  const rect = node => { const r = node.getBoundingClientRect(); return { x: Math.round(r.x), y: Math.round(r.y), width: Math.round(r.width), height: Math.round(r.height), scale: window.devicePixelRatio || 1 }; };
  const browserViews = new Map();
  async function browserView(root, row, signal, mount) {
    const view = await mountBrowserView(root, { row, sdk: bound(row), signal, mount, bridge, clientId,
      shown: () => !root.hidden && layout.open && !document.hidden && navigation.get().view !== "settings",
      updateTitle: title => update(row.id, { title }), openExternal: target => layoutApi.call("system.open", { target }), addContext,
      onEnd: async id => { browserViews.delete(id); await close(row.id); } });
    if (!signal.aborted) browserViews.set(view.id, { execute: view.execute, row });
  }
  async function mount(row) {
    const root = make("section", "wb-view"); root.setAttribute("role", "tabpanel"); root.tabIndex = 0; root.hidden = !layout.open || (row.id !== layout.active && row.id !== layout.secondary); content.append(root);
    const abort = new AbortController(), value = { root, abort, revision: row.revision, resourceRevision: 0, events: new EventTarget() }; mounts.set(row.id, value);
    value.resizeObserver = new ResizeObserver(() => value.events.dispatchEvent(new CustomEvent("resize", { detail: rect(root) }))); value.resizeObserver.observe(root);
    try {
      const definition = definitions.get(row.type); if (!definition) throw new Error(`面板插件未加载：${row.type}`);
      const sdk = bound(row), signal = abort.signal, context = Object.freeze({ ...row.context, target: displayTarget, extension: row.owner });
      if (definition.mount || definition.render) {
        const events = value.events;
        const result = await (definition.mount || definition.render)(root, { panelId: row.id, sdk, api: sdk, context, params: row.params || {}, resource: row.resource, signal, events,
          addContext, viewState: row.view_state || {}, update: patch => update(row.id, patch), setDirty: callback => { value.dirty = callback; } });
        if (typeof result === "function") value.dispose = result; else if (result) Object.assign(value, result);
        if (signal.aborted) value.dispose?.(); return;
      }
      if (definition.renderer === "custom") {
        const render = renderers.get(definition.frontend || row.type);
        if (!render) throw new Error(`插件渲染器尚未加载：${definition.frontend || row.type}`);
        const resource = row.resource?.id ? await sdk.call("resource.get", { id: row.resource.id }, { signal }) : null;
        value.resourceRevision = resource?.revision || 0;
        const dispose = await render(root, resource?.value ?? row.params ?? {}, { sdk, context, signal, events: value.events, addContext });
        if (typeof dispose === "function") value.dispose = dispose; if (signal.aborted) value.dispose?.(); return;
      }
      if (definition.renderer === "browser") { await browserView(root, row, signal, value); return; }
      if (definition.renderer === "editor") {
        const { path, space = "workspace" } = row.params || {};
        const file = await sdk.call("fs.read", { path, space, limit: 262144 }, { signal });
        if (typeof file.text !== "string" || file.bytes !== file.size) throw new Error("编辑器支持完整的 UTF-8 文本文件，大小上限 256 KiB");
        const editor = make("textarea", "wb-document-editor"); editor.value = file.text; editor.setAttribute("aria-label", path);
        let dirty = false; value.dirty = () => dirty; editor.oninput = () => { dirty = true; };
        const saveButton = button("保存", async () => {
          saveButton.disabled = true;
          try { const saved = await sdk.call("fs.write", { path, space, text: editor.value, revision: file.revision }, { signal }); file.revision = saved.revision; dirty = false; saveButton.textContent = "已保存"; }
          finally { saveButton.disabled = false; }
        });
        root.append(editor, saveButton); return;
      }
      if (definition.renderer === "web") {
        const url = safeBrowserUrl(row.params?.url || row.resource?.url);
        /* Never let preview content share the host origin or native bridge. */
        if (new URL(url).origin === location.origin) throw new Error("预览页必须使用独立源，不能指向 mdo 主界面");
        if (bridge.available()) {
          const browser = await sdk.call("browser.create", { client: clientId, key: `preview-${row.id}`, url }, { signal });
          await browserView(root, { ...row, resource: { id: browser.id } }, signal, value); return;
        }
        const toolbar = make("div", "wb-toolbar"), frame = make("iframe", "wb-preview");
        frame.setAttribute("sandbox", "allow-scripts allow-forms"); frame.referrerPolicy = "no-referrer"; frame.title = title(row);
        toolbar.append(button("刷新", () => { frame.src = url; }), button("手机", () => { frame.style.width = "390px"; }), button("自适应", () => { frame.style.width = "100%"; }), button("横竖屏", () => { frame.style.width = frame.style.width === "844px" ? "390px" : "844px"; }));
        root.append(toolbar, frame); frame.src = url; value.dispose = () => { frame.src = "about:blank"; }; return;
      }
      if (definition.renderer === "surface") {
        const toolbar = make("div", "wb-toolbar"), canvas = make("canvas", "wb-surface"); canvas.width = Math.max(1,Math.min(4096,row.params?.width || 1280)); canvas.height = Math.max(1,Math.min(4096,row.params?.height || 720)); root.append(toolbar, canvas);
        const input = createSurfaceInput({ canvas, toolbar, sdk, stream: row.params?.input_stream, signal, onError: error => toast(error.message, "error") });
        value.dispose = () => input.dispose(); value.visible = visible => { if (!visible) input.stop(); };
        const stream = row.params?.stream; if (!stream) throw new Error("操作画面需要插件提供 stream 句柄");
        let frame = [], bytes = 0;
        const pump = async () => { while (!signal.aborted) {
          if (root.hidden || document.hidden) { await new Promise(resolve => setTimeout(resolve, 150)); continue; }
          let chunk;
          try { chunk = await sdk.call("stream.read", { handle: stream, limit: 16384, timeout_ms: 500 }, { signal }); }
          catch (error) { if (error.code === "sdk_stream_would_block") continue; throw error; }
          if (!chunk.base64) { if (chunk.eof) break; await new Promise(resolve => setTimeout(resolve, 25)); continue; }
          /* Image frames use uint32 little-endian length + encoded image. */
          for (const byte of Uint8Array.from(atob(chunk.base64), c => c.charCodeAt(0))) frame.push(byte);
          while (frame.length >= 4) {
            bytes = new DataView(Uint8Array.from(frame.slice(0, 4)).buffer).getUint32(0, true);
            if (!bytes || bytes > 1048576) throw new Error("画面帧长度无效"); if (frame.length < bytes + 4) break;
            const encoded = Uint8Array.from(frame.slice(4, bytes + 4)); validateImageFrame(encoded);
            const image = await createImageBitmap(new Blob([encoded])); frame = frame.slice(bytes + 4);
            if (!signal.aborted) canvas.getContext("2d").drawImage(image, 0, 0, canvas.width, canvas.height); image.close();
          }
          if (chunk.eof) { if (frame.length) throw new Error("画面流在帧结束前中断"); break; }
        } };
        void pump().catch(error => { input.stop(); if (!signal.aborted) fail(root, error, () => remount(row.id)); }); return;
      }
      const resource = row.resource?.id ? await sdk.call("resource.get", { id: row.resource.id }, { signal }) : { value: row.params || {} };
      value.resourceRevision = resource.revision || 0;
      const data = resource.value;
      if (definition.renderer === "diff") {
        const files = Array.isArray(data?.files) ? data.files : [{ path: data?.path || "变更", patch: data?.patch || String(data || "") }];
        for (const file of files.slice(0, 256)) {
          const details = make("details", "wb-diff"), summary = make("summary", "", file.path), pre = make("pre"); details.open = true;
          for (const line of String(file.patch || "").split("\n")) pre.append(make("div", line.startsWith("+") ? "wb-added" : line.startsWith("-") ? "wb-removed" : "", line));
          details.append(summary, pre); root.append(details);
        }
      } else if (definition.renderer === "plan" && Array.isArray(data?.items)) {
        for (const [index, item] of data.items.entries()) {
          const label = make("label", "wb-plan-item"), input = make("input"); input.type = "checkbox"; input.checked = Boolean(item.done); input.disabled = Boolean(row.params?.readonly || !resource.id);
          input.onchange = async () => {
            input.disabled = true;
            try {
              const items = data.items.map((entry, at) => at === index ? { ...entry, done: input.checked } : entry);
              await sdk.call("resource.put", { id: resource.id, revision: resource.revision, kind: resource.kind, title: resource.title, value: { ...data, items } }, { signal });
              remount(row.id);
            } catch (error) { input.checked = Boolean(item.done); input.disabled = false; toast(`计划未保存：${error.message}`, "error"); }
          };
          label.append(input, document.createTextNode(item.text || "")); root.append(label);
        }
      } else {
        const text = typeof data === "string" ? data : data?.text || JSON.stringify(data, null, 2);
        root.append(renderSourceMarkdown(text));
        if (resource.id && !row.params?.readonly) root.append(button("编辑", () => {
          const editor = make("textarea", "wb-document-editor"); editor.value = text; let dirty = false;
          value.dirty = () => dirty; editor.oninput = () => { dirty = true; };
          const saveButton = button("保存", async () => {
            saveButton.disabled = true;
            try {
              const saved = await sdk.call("resource.put", { id: resource.id, revision: resource.revision, kind: resource.kind, title: resource.title,
                value: typeof data === "string" ? editor.value : { ...data, text: editor.value } }, { signal });
              resource.revision = saved.revision; dirty = false; remount(row.id);
            } finally { saveButton.disabled = false; }
          });
          root.replaceChildren(editor, saveButton); editor.focus();
        }));
      }
      root.append(button("引用到对话", () => {
        const selection = document.getSelection(), selected = selection?.toString();
        const text = selected && root.contains(selection.anchorNode) && root.contains(selection.focusNode) ? selected : typeof data === "string" ? data : JSON.stringify(data);
        addContext({ title: title(row), content: text, source: row.resource?.id || row.id, context: row.context });
      }));
    } catch (error) { value.status = { status:"error", message:String(error.message || error).slice(0,512) }; if (!abort.signal.aborted) fail(root, error, () => remount(row.id)); }
    finally {
      if (!abort.signal.aborted) {
        value.status ||= { status:"ready" };
        void bound(row).call("panel.report", { id:row.id, client:clientId, value:{ ...value.status, visible:!root.hidden } }).catch(() => {});
      }
    }
  }
  const remount = id => { disposeMount(id); const row = entries.get(id); if (row) { void mount(row).then(render); } };
  function render() {
    if (stopped) return;
    shell.dataset.workbench = layout.open ? "open" : "closed"; shell.toggleAttribute("data-workbench-maximized", layout.maximized && layout.open);
    const sidebar = Number.parseFloat(shell.style.getPropertyValue("--sidebar-column")) || 0;
    shell.style.setProperty("--workbench-width", `${Math.min(layout.width, Math.max(320, innerWidth - sidebar - 360))}px`); dock.hidden = !layout.open;
    tabs.replaceChildren();
    for (const id of layout.order) {
      const row = entries.get(id); if (!row) continue;
      const tab = make("div", "wb-tab"), select = button(`${row.pinned ? "● " : ""}${title(row)}`, () => reveal(id), "wb-tab-select");
      tab.draggable = true; tab.dataset.panel = id; select.setAttribute("role", "tab"); select.setAttribute("aria-selected", String(id === layout.active));
      tab.append(select, button("×", () => close(id), "wb-tab-close"));
      tab.ondragstart = event => { dragId = id; event.dataTransfer?.setData("text/mdo-panel", id); };
      tab.ondragover = event => { if (dragId) event.preventDefault(); };
      tab.ondrop = event => { if (!dragId) return; event.preventDefault(); layout.order = reorderPanel(layout.order, dragId, id); dragId = ""; remember(); render(); };
      select.onkeydown = event => {
        if (["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) { event.preventDefault(); const offset = event.key === "ArrowLeft" ? -1 : 1, index = layout.order.indexOf(id); reveal(event.key === "Home" ? layout.order[0] : event.key === "End" ? layout.order.at(-1) : layout.order[(index + offset + layout.order.length) % layout.order.length]); tabs.querySelector('[aria-selected="true"]')?.focus(); }
        if (event.key === "Delete") { event.preventDefault(); void close(id); }
      }; tabs.append(tab);
    }
    for (const [id, mounted] of mounts) {
      const visible = layout.open && !document.hidden && (id === layout.active || id === layout.secondary); mounted.root.hidden = !visible;
      mounted.visible?.(visible);
      if (visible && entries.get(id)?.type === "mdo.new-tab") mounted.refresh?.();
      if (mounted.lastVisible !== visible) {
        mounted.lastVisible = visible; mounted.events?.dispatchEvent(new CustomEvent("visibility", { detail: visible }));
      }
      const row = entries.get(id), bounds = rect(mounted.root), report = { ...mounted.status, visible, width: bounds.width, height: bounds.height };
      const identity = JSON.stringify(report);
      if (row && mounted.lastReport !== identity) { mounted.lastReport = identity; void bound(row).call("panel.report", { id, client: clientId, value: report }).catch(() => { mounted.lastReport = ""; }); }
    }
    content.classList.toggle("wb-split", Boolean(layout.secondary));
    for (const id of [layout.active, layout.secondary]) if (id && entries.has(id) && !mounts.has(id)) void mount(entries.get(id)).then(render);
    /* Bound expensive hidden views while keeping their lightweight records. */
    const hidden = [...mounts].filter(([id, mounted]) => id !== layout.active && id !== layout.secondary && !mounted.dirty?.());
    for (const [id] of hidden.slice(0, Math.max(0, mounts.size - 8))) disposeMount(id);
  }
  const more = iconButton("more", "工作区更多操作", () => {}), menu = popupMenu(more, () => [
    { label: entries.get(layout.active)?.pinned ? "取消固定" : "固定", icon: "pin", checked: Boolean(entries.get(layout.active)?.pinned), disabled: !entries.has(layout.active), execute: () => { const row = entries.get(layout.active); return row && update(row.id, { pinned: !row.pinned }); } },
    { label: layout.secondary ? "退出并排" : "并排", icon: "split", checked: Boolean(layout.secondary), disabled: layout.order.length < 2, execute: () => { layout.secondary = layout.secondary ? "" : layout.order.find(id => id !== layout.active) || ""; remember(); render(); } },
    { label: layout.maximized ? "恢复宽度" : "展开", icon: "expand", checked: layout.maximized, execute: () => { layout.maximized = !layout.maximized; remember(); render(); } },
    { label: "收起", execute: () => { layout.open = false; remember(); render(); } },
  ], { onError: error => toast(error.message, "error") });
  top.append(tabs, iconButton("plus", "新建标签页", () => newTab(), error => toast(error.message, "error")), more);
  dock.append(resize, top, content); shell.append(dock);
  let pointerX, startWidth;
  resize.onpointerdown = event => { pointerX = event.clientX; startWidth = layout.width; resize.setPointerCapture(event.pointerId); event.preventDefault(); };
  resize.onpointermove = event => { if (!resize.hasPointerCapture(event.pointerId)) return; layout.width = Math.max(320, Math.min(960, startWidth + pointerX - event.clientX)); render(); };
  resize.onpointerup = event => { if (resize.hasPointerCapture(event.pointerId)) { resize.releasePointerCapture(event.pointerId); remember(); } };
  resize.onkeydown = event => { if (!["ArrowLeft", "ArrowRight"].includes(event.key)) return; event.preventDefault(); layout.width = Math.max(320, Math.min(960, layout.width + (event.key === "ArrowLeft" ? 24 : -24))); remember(); render(); };
  const resizeWindow = () => render(); window.addEventListener("resize", resizeWindow);
  const sidebarResize = new ResizeObserver(resizeWindow); const sidebarNode = shell.querySelector(".sidebar"); if (sidebarNode) sidebarResize.observe(sidebarNode);
  let overlayFrame = 0;
  const overlays = new MutationObserver(() => {
    if (!browserViews.size || overlayFrame || stopped) return;
    overlayFrame = requestAnimationFrame(() => { overlayFrame = 0; for (const mounted of mounts.values()) mounted.visible?.(layout.open && !document.hidden && !mounted.root.hidden); });
  });
  overlays.observe(document.body, { subtree:true, childList:true, attributes:true, attributeFilter:["open","hidden","aria-expanded","data-settings-open"] });
  document.addEventListener("visibilitychange", resizeWindow);
  const beforeUnload = event => { if (bridge.busy() || [...mounts.values()].some(mounted => mounted.dirty?.())) { event.preventDefault(); event.returnValue = ""; } };
  window.addEventListener("beforeunload", beforeUnload);
  async function open(type, params = {}, scopedApi = api, definition) {
    if (stopped) throw new Error("工作区已退出");
    if (definition) definitions.set(type, definition);
    const client = scopedApi.bind({});
    if (type === "mdo.editor" && params.params?.path) {
      const scope = await client.call("sdk.context"); if (stopped) throw new Error("工作区已退出");
      const same = (a, b) => (a || "") === (b || "");
      const existing = [...entries.values()].find(row => row.type === type && filePathKey(row.params?.path) === filePathKey(params.params.path) && row.owner === scope.extension && ["project", "session", "run"].every(key => same(row.context[key], scope[key])));
      if (existing) {
        reveal(existing.id);
        const mounted = mounts.get(existing.id);
        if (params.params.edit && mounted?.edit) await mounted.edit();
        if (params.params.line && mounted?.locate) await mounted.locate(params.params.line);
        if ((!mounted?.edit && params.params.edit) || (!mounted?.locate && params.params.line)) {
          await update(existing.id, { params: { ...existing.params, ...params.params } }); if (!mounts.get(existing.id)?.dirty?.()) remount(existing.id);
        }
        return handle(existing.id);
      }
    }
    const row = await client.call("panel.open", { ...params, type, client: clientId });
    entries.set(row.id, row); if (!layout.order.includes(row.id)) layout.order.push(row.id);
    if (params.reveal !== "background" || !layout.active) reveal(row.id); else { remember(); render(); }
    return handle(row.id);
  }
  async function newTab() { return open("mdo.new-tab", { title: "新标签页" }); }
  definitions.set("mdo.new-tab", { title: "新标签页", renderer: "custom", mount(root, { sdk, signal, panelId, context }) {
    // An explicit click owns one admission key. Retrying an uncertain open
    // finds its original resource rather than creating another browser/shell.
    const keys = new Map(), history = [];
    const key = type => { if (!keys.has(type)) keys.set(type, `start-${crypto.randomUUID()}`); return keys.get(type); };
    const launch = async type => {
      if (signal.aborted) return;
      let handle; const pinned = Boolean(entries.get(panelId)?.pinned);
      if (type === "mdo.browser") {
        const browser = await sdk.call("browser.create", { client: clientId, key: key(type) });
        handle = await open(type, { key: browser.id, title: "浏览器", resource: { id: browser.id }, pinned }, sdk);
      } else if (type === "mdo.terminal") {
        const terminal = await sdk.call("terminal.create", { key: key(type) });
        handle = await open(type, { key: terminal.id, title: "终端", params: { terminal: terminal.id }, pinned }, sdk);
      } else {
        const descriptor = definitions.get(type);
        await descriptor?.registration;
        const client = descriptor?.api?.bind(context) || sdk.bind(descriptor?.owner ? { extension: descriptor.owner } : {});
        handle = await open(type, { key: "workspace", title: descriptor?.title || type, pinned }, client);
      }
      if (!signal.aborted && handle.id !== panelId) await close(panelId);
      return handle;
    };
    const view = mountStartView(root, { signal, context, launch,
      contributions: () => [...definitions].filter(([id, definition]) => !id.startsWith("mdo.") && (definition.renderer !== "custom" || definition.mount || definition.render || renderers.has(definition.frontend || id))).map(([id, definition]) => ({ ...definition, id })),
      opened: () => [...entries.values()].filter(row => row.type !== "mdo.new-tab").map(row => ({ id: row.id, title: title(row), type: row.type })).concat(history.filter(row => ![...entries.values()].some(panel => panel.resource?.id === row.browser?.id))),
      resume: async id => { const item = history.find(row => row.id === id); if (item && !entries.has(id)) { const browser = item.browser; await open("mdo.browser", { key: browser.id, resource: { id: browser.id }, title: item.title }, api.bind({ ...browser.context, extension: browser.owner })); } else reveal(id); if (!signal.aborted) await close(panelId); },
    });
    void sdk.call("browser.list", { client: clientId }, { signal }).then(browsers => {
      if (signal.aborted) return;
      for (const browser of browsers) history.push({ id: `resume-${browser.id}`, title: browser.title || browser.url || "浏览器", type: "mdo.browser", browser });
      view.refresh();
    }).catch(() => {});
    return view;
  } });
  for (const [id, renderer, label] of [["mdo.browser", "browser", "浏览器"], ["mdo.document", "document", "文档"], ["mdo.plan", "plan", "计划"], ["mdo.diff", "diff", "变更"], ["mdo.preview", "web", "网页预览"], ["mdo.editor", "editor", "文件编辑"]]) definitions.set(id, { title: label, renderer });
  const openFile = async (path, client, line = 1) => {
    const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(JSON.stringify(["native", path])));
    const key = [...new Uint8Array(digest)].map(value => value.toString(16).padStart(2, "0")).join("");
    const panel = await open("mdo.editor", { key, title: path.split(/[\\/]/).at(-1), params: { path, space: "native", line } }, client);
    const locating = mounts.get(panel.id)?.locate?.(line); if (locating) await locating; return panel;
  };
  for (const [id, title] of [["mdo.files", "文件"], ["mdo.editor", "文件"]]) definitions.set(id, { title, renderer: "custom", mount: (root, options) => mountFilesView(root, { ...options, fileEvents, openFile: path => openFile(path, options.sdk) }) });
  definitions.set("mdo.task-plan", { title: "会话计划", renderer: "custom", mount(root, { sdk, context, signal }) {
    if (!context.session) { root.append(make("p", "", "请先选择一个会话，查看它的任务计划。")); return; }
    let timer, previous = "";
    const refresh = async () => {
      try {
        const plan = await sdk.call("product.projects.by_project.sessions.by_session.todo.get", { path: { project: context.project, session: context.session } }, { signal });
        const identity = JSON.stringify(plan);
        if (identity !== previous && !signal.aborted) {
          previous = identity; root.replaceChildren(make("h2", "", "会话计划"));
          if (!plan.items?.length) root.append(make("p", "", "这个会话尚未生成任务计划。"));
          for (const item of plan.items || []) { const line = make("label", "wb-plan-item"), checked = make("input"); checked.type = "checkbox"; checked.checked = Boolean(item.done); checked.disabled = true; line.append(checked, document.createTextNode(item.text || "")); root.append(line); }
        }
      } catch (error) { if (!signal.aborted && !previous) fail(root, error, refresh); }
      finally { if (!signal.aborted) timer = setTimeout(refresh, 2500); }
    };
    void refresh(); return () => clearTimeout(timer);
  } });
  definitions.set("mdo.changes", { title: "变更", renderer: "custom", mount: (root, options) => mountChangesView(root, { ...options, openFile: (path, line) => openFile(path, options.sdk, line) }) });
  definitions.set("mdo.terminal", { title: "终端", renderer: "custom", mount: mountTerminalView });
  async function synchronize() {
    if (stopped) return;
    try {
      const types = await api.call("panel.types");
      const currentTypes = new Set(types.map(type => type.id));
      for (const id of discovered) if (!currentTypes.has(id)) { discovered.delete(id); definitions.delete(id); }
      for (const type of types) if (!definitions.has(type.id) || discovered.has(type.id)) { definitions.set(type.id, type); discovered.add(type.id); }
      const rows = await api.call("panel.list", { client: clientId }); if (stopped) return;
      const live = new Set(rows.map(row => row.id));
      for (const row of rows) {
        const previous = entries.get(row.id); entries.set(row.id, row);
        if (!layout.order.includes(row.id)) layout.order.push(row.id);
        if (!previous && row.reveal === "activate") { layout.active = row.id; layout.open = true; }
        if (previous && previous.revision !== row.revision && !mounts.get(row.id)?.dirty?.()) disposeMount(row.id);
        const mounted = mounts.get(row.id);
        if (mounted?.resourceRevision && !mounted.root.hidden && row.resource?.id) {
          const resource = await bound(row).call("resource.get", { id: row.resource.id });
          if (resource.revision !== mounted.resourceRevision && !mounted.dirty?.()) disposeMount(row.id);
        }
      }
      for (const id of entries.keys()) if (!live.has(id)) { disposeMount(id); entries.delete(id); }
      for (const [id, view] of browserViews) {
        try { await bound(view.row).call("browser.get", { id }); }
        catch (error) {
          if (error.status !== 404) continue;
          await bridge.call("close", { browser: id }).catch(() => {}); browserViews.delete(id);
          for (const [panelId, panel] of entries) if (panel.type === "mdo.browser" && panel.resource?.id === id) remount(panelId);
        }
      }
      layout.order = layout.order.filter(id => live.has(id)); if (!entries.has(layout.active)) layout.active = layout.order.at(-1) || "";
      render();
    } catch { /* Existing panels remain visible during a transient disconnect. */ }
    finally { if (!stopped) syncTimer = setTimeout(sync, document.hidden ? 10000 : 2000); }
  }
  function sync() {
    if (syncing) return syncing;
    clearTimeout(syncTimer); syncing = synchronize().finally(() => { syncing = null; }); return syncing;
  }
  async function load() {
    const saved = await layoutApi.call("panel.layout.get", { id: profile }); revision = saved.revision;
    if (!edited) layout = normalizeLayout(saved.value); ready = true;
    for (const item of (Array.isArray(saved.value?.restore) ? saved.value.restore : []).slice(0, 64)) {
      try {
        const client = api.bind({ ...item.context, extension: item.owner });
        let row = await client.call("panel.get", { id: item.id });
        if (row.client !== clientId) row = await client.call("panel.attach", { id: row.id, client: clientId, revision: row.revision });
        entries.set(row.id, row);
      } catch { /* Missing plugins/resources or a live other window stay detached. */ }
    }
    for (const [id, definition] of definitions) await api.call("panel.register", { id, title: definition.title, renderer: definition.renderer });
    await sync(); if (dirty) void save();
  }
  let loading;
  const ensure = () => loading ||= load().catch(error => { loading = null; throw error; });
  const opener = button("＋", async () => { await ensure(); return newTab(); }, "icon-button"); opener.id = "new-workbench-tab"; opener.title = "新建标签页（Ctrl/⌘+Shift+B）"; opener.setAttribute("aria-label", opener.title);
  shell.querySelector(".header-actions")?.append(opener);
  const shortcut = event => { if ((event.ctrlKey || event.metaKey) && event.shiftKey && event.code === "KeyB" && !event.isComposing && !event.repeat) { event.preventDefault(); void ensure().then(newTab).catch(error => toast(error.message, "error")); } };
  document.addEventListener("keydown", shortcut); render();
  const nativeShortcut = event => {
    if (event.detail.key === "new-tab") { void ensure().then(newTab).catch(error => toast(error.message,"error")); return; }
    for (const [id, mounted] of mounts) if (mounted.browserId === event.detail.browser) {
      if (event.detail.key === "address") { const input = mounted.root.querySelector(".wb-address"); input?.focus(); input?.select(); }
      else if (event.detail.key === "close-tab") void close(id).catch(error => toast(error.message,"error"));
    }
  };
  window.addEventListener("mdo-browser-shortcut", nativeShortcut);
  return Object.freeze({ ensure, open: async (...args) => { await ensure(); return open(...args); }, newTab: async () => { await ensure(); return newTab(); },
    register(id, descriptor) { discovered.delete(id); definitions.set(id, descriptor); return () => {
      if (definitions.get(id) !== descriptor) return; definitions.delete(id);
      for (const [key, row] of entries) if (row.type === id) { disposeMount(key); void bound(row).call("panel.close", { id: key }).catch(error => toast(error.message, "error")); }
    }; },
    rendererChanged(id) { for (const [key, row] of entries) if (row.type === id || definitions.get(row.type)?.frontend === id) remount(key); },
    canLeave: () => !bridge.busy() && ![...mounts.values()].some(mounted => mounted.dirty?.()),
    async retireOwner(client) {
      const panels = await client.call("panel.list"), browsers = await client.call("browser.list");
      for (const row of panels) if (row.client === clientId) { await client.bind(row.context).call("panel.close", { id: row.id }); disposeMount(row.id); entries.delete(row.id); }
      for (const row of browsers) if (row.client === clientId) { await client.bind(row.context).call("browser.close", { id: row.id }); await bridge.call("close", { browser: row.id }).catch(() => {}); browserViews.delete(row.id); }
      layout.order = layout.order.filter(id => entries.has(id)); remember(); render();
    },
    async execute(action) {
      await ensure();
      if (action.action === "workbench.reveal") { await sync(); reveal(action.params.id); return { id: action.params.id }; }
      const { command, id, ...params } = action.params;
      if (command === "capabilities") return bridge.available() ? bridge.call("capabilities") : { native: false, embedded: false, snapshot: false, screenshot: false, input: false };
      let view = browserViews.get(id);
      if (!view) {
        const client = api.bind(action.context || {}), browser = await client.call("browser.get", { id });
        if (isRemoteTarget() && browser.url && ["localhost","127.0.0.1","[::1]"].includes(new URL(browser.url).hostname)) throw new Error("目标设备的本地网址需要预览服务映射");
        await bridge.call("create", { browser: id, url: browser.url || "about:blank", visible: false, bounds: { x:0,y:0,width:1024,height:768,scale:1 } });
        view = { row: browser, execute: (operation, parameters = {}) => bridge.call(operation, { browser:id, ...parameters }) }; browserViews.set(id, view);
      }
      if (command === "wait") {
        const deadline = Date.now() + Math.min(60000, Math.max(1, params.timeout_ms || 20000));
        let lastReceipt = 0;
        while (!stopped && Date.now() < deadline) {
          if (Date.now() - lastReceipt > 500) {
            const receipt = await api.call("ui.result", { id: action.id }); lastReceipt = Date.now();
            if (receipt.state === "cancelled") throw new Error(receipt.error || "浏览器等待已取消");
          }
          const state = await view.execute("state"); if (state.error) throw new Error(state.error);
          if (!state.loading) {
            const script = params.selector ? `Boolean(document.querySelector(${JSON.stringify(params.selector)})?.getClientRects().length)` : "document.readyState==='complete' && /^https?:$/.test(location.protocol)";
            const result = await view.execute("evaluate", { script });
            if (result.value) return { ready:true, url:state.url };
          }
          await new Promise(resolve => setTimeout(resolve,100));
        }
        throw new Error("等待浏览器页面或元素超时");
      }
      return view.execute(command, { ...params, agent: true });
    },
    destroy() { stopped = true; menu.dispose(); clearTimeout(syncTimer); clearTimeout(saveTimer); sidebarResize.disconnect(); overlays.disconnect(); cancelAnimationFrame(overlayFrame); for (const id of [...mounts.keys()]) disposeMount(id); bridge.dispose(); window.removeEventListener("mdo-browser-shortcut",nativeShortcut); window.removeEventListener("resize", resizeWindow); window.removeEventListener("beforeunload", beforeUnload); document.removeEventListener("visibilitychange", resizeWindow); document.removeEventListener("keydown", shortcut); opener.remove(); dock.remove(); },
  });
}
