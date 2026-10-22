import { createApplicationClient, applyApplicationAction } from "../../api/application.js";
import { isRemoteTarget } from "../../api/target.js";
import { toast } from "../../utils/dom.js";

export function createApplicationUi({ navigation, executeActions }) {
  let clientId = `ui-${crypto.randomUUID()}`;
  try {
    const saved = sessionStorage.getItem("mdo.sdk.client");
    if (/^ui-[a-f0-9-]{36}$/.test(saved || "")) clientId = saved;
    else sessionStorage.setItem("mdo.sdk.client", clientId);
  } catch { /* Effects still require the separate intent journal below. */ }
  const api = createApplicationClient(() => ({ project: navigation.preferredProject(), session: navigation.workspace().sessionId || "", client: clientId }));
  const panels = new Map(), menus = new Map(), renderers = new Map(), actions = new Map(), openDialogs = new Map(), disposers = [];
  let stopped = false, timer = null, registered = false;
  const register = (map, id, value) => {
    if (stopped) throw new Error("Application UI instance has been disposed");
    if (!/^[A-Za-z0-9_.-]+$/.test(id) || map.has(id)) throw new Error("Duplicate or invalid SDK contribution ID");
    map.set(id, value); const dispose = () => map.delete(id); disposers.push(dispose); return dispose;
  };
  const openPanel = async (id, params) => {
    if (stopped) throw new Error("Application UI instance has been disposed");
    const descriptor = panels.get(id); if (!descriptor) throw new Error(`Panel ${id} is not registered`);
    const dialog = document.createElement("dialog"), heading = document.createElement("h2"), body = document.createElement("div"), close = document.createElement("button");
    heading.textContent = descriptor.title || id; close.textContent = "关闭"; close.type = "button";
    const abort = new AbortController(); let dispose;
    const cleanup = () => { abort.abort(); openDialogs.get(id)?.delete(dialog); try { if (typeof dispose === "function") dispose(); } finally { dialog.remove(); } };
    close.onclick = () => dialog.close(); dialog.addEventListener("close", cleanup, { once: true });
    dialog.append(heading, body, close); document.body.append(dialog); dialog.showModal();
    if (!openDialogs.has(id)) openDialogs.set(id, new Set()); openDialogs.get(id).add(dialog);
    try { dispose = await descriptor.render(body, { sdk, params, signal: abort.signal }); if (abort.signal.aborted && typeof dispose === "function") dispose(); }
    catch (error) { dialog.close(); throw error; }
  };
  const ui = Object.freeze({
    registerPanel: (id, descriptor) => {
      const remove = register(panels, id, descriptor);
      const dispose = () => { for (const dialog of openDialogs.get(id) || []) dialog.close(); openDialogs.delete(id); remove(); };
      disposers.push(dispose); return dispose;
    },
    registerAction: (id, execute) => register(actions, id, execute),
    registerRenderer: (id, render) => register(renderers, id, render),
    render: (id, root, value, options = {}) => { const render = renderers.get(id); if (!render) throw new Error("Renderer is not registered"); return render(root, value, { sdk, ...options }); },
    registerMenu: (id, descriptor) => {
      const button = document.createElement("button"); button.type = "button"; button.textContent = descriptor.label || id;
      const root = document.querySelector(descriptor.mount || ".sidebar"); if (!root) throw new Error("Menu mount is unavailable");
      button.onclick = () => Promise.resolve(descriptor.execute({ sdk })).catch(error => toast(error.message, "error")); root.append(button);
      const remove = register(menus, id, button); const dispose = () => { button.remove(); remove(); }; disposers.push(dispose); return dispose;
    },
    registerKeybinding: (id, descriptor) => {
      const callback = event => {
        if (event.isComposing || event.repeat || event.code !== descriptor.code || Boolean(event.ctrlKey || event.metaKey) !== Boolean(descriptor.mod) || Boolean(event.altKey) !== Boolean(descriptor.alt) || Boolean(event.shiftKey) !== Boolean(descriptor.shift)) return;
        if (!descriptor.inInput && event.target?.closest?.("input,textarea,[contenteditable=true]")) return;
        event.preventDefault(); Promise.resolve(descriptor.execute({ sdk, event })).catch(error => toast(error.message, "error"));
      };
      const remove = register(actions, `keybinding.${id}`, callback); document.addEventListener("keydown", callback);
      const dispose = () => { document.removeEventListener("keydown", callback); remove(); }; disposers.push(dispose); return dispose;
    },
    openPanel,
    openWindow: ({ url, name = "_blank" }) => {
      const opened = window.open(url, name, "noopener");
      if (!opened) throw new Error("The OS or browser blocked the new window; open it from a user action");
    },
    openEditor: async ({ path, space = "workspace" }) => {
      const resource = await api.call("fs.read", { path, space, limit: 262144 });
      if (typeof resource.text !== "string" || resource.bytes !== resource.size) throw new Error("The editor supports complete UTF-8 files up to 256 KiB");
      const id = `editor.${crypto.randomUUID()}`;
      const dispose = ui.registerPanel(id, { title: path, render(root, { signal }) {
        const input = document.createElement("textarea"), save = document.createElement("button"); input.value = resource.text; input.style.cssText = "width:70vw;height:60vh"; save.textContent = "保存";
        save.onclick = async () => { save.disabled = true; try { const saved = await api.call("fs.write", { path, space, text: input.value, revision: resource.revision }, { signal }); resource.revision = saved.revision; save.textContent = "已保存"; } catch (error) { toast(error.message, "error"); } finally { save.disabled = false; } };
        root.append(input, save); return dispose;
      } }); await openPanel(id, {});
    },
  });
  const sdk = Object.freeze({ ...api, ui, clientId, context: () => navigation.get() });
  const execute = async action => {
    const scope = action.context || {}, selected = navigation.workspace();
    if ((scope.project && scope.project !== selected.projectId && !(scope.project === "default" && !selected.projectId)) || (scope.session && scope.session !== selected.sessionId))
      throw new Error("The target window changed conversation before the UI action ran");
    if (actions.has(action.action)) return actions.get(action.action)(action.params, { sdk });
    if (action.action === "ui.toast") { toast(String(action.params?.message || "")); return; }
    if (action.action === "ui.panel") return openPanel(action.params?.id, action.params);
    if (action.action === "ui.editor") return ui.openEditor(action.params);
    if (action.action === "ui.window") return ui.openWindow(action.params);
    if (action.action === "ui.navigate") { navigation.select(action.params.project, action.params.session); return; }
    return executeActions({ id: action.id, state: "succeeded", raw_arguments: "", actions: [{ type: action.action, params: action.params }] }, { projectId: selected.projectId || "default", sessionId: selected.sessionId || "", runId: "" });
  };
  const poll = async () => {
    try {
      if (isRemoteTarget()) return;
      if (!registered) { await api.call("client.hello", { client: clientId, context: navigation.get() }); registered = true; }
      const pending = await api.call("client.poll", { client: clientId });
      if (stopped) return;
      for (const action of pending) {
        if (stopped) break;
        let ack;
        try { ack = await applyApplicationAction(action, execute, sessionStorage, `mdo.sdk.actions.${clientId}`); }
        catch (error) { ack = { success: false, error: `UI intent journal unavailable: ${error.message}` }; }
        await api.call("client.ack", { client: clientId, id: action.id, ...ack });
      }
    } catch { registered = false; }
    finally { if (!stopped) timer = setTimeout(poll, document.hidden ? 10000 : 1500); }
  };
  void poll();
  // Installed local UI code runs in the displaying client's DOM. Its SDK calls
  // still use the explicitly selected target through the standard API adapter.
  const loadPlugins = async () => {
    if (isRemoteTarget()) return;
    const result = await api.call("plugin.list"); if (result.recovery_mode || stopped) return;
    for (const plugin of result.items) if (plugin.enabled && !plugin.error && plugin.frontend?.startsWith("web/")) {
      try {
        const module = await import(`/${plugin.frontend.slice(4)}?revision=${encodeURIComponent(plugin.revision)}`);
        if (stopped) return;
        if (typeof module.activate !== "function") throw new Error("Plugin frontend must export activate(sdk)");
        const scopedApi = createApplicationClient(() => ({ project: navigation.preferredProject(), session: navigation.workspace().sessionId || "", client: clientId, extension: `plugin.${plugin.id}` }));
        const dispose = await module.activate(Object.freeze({ ...sdk, ...scopedApi })); if (typeof dispose === "function") { if (stopped) dispose(); else disposers.push(dispose); }
      } catch (error) { console.error(`Plugin ${plugin.id} failed`, error); toast(`插件 ${plugin.id} 启动失败：${error.message}`, "error"); }
    }
  };
  void loadPlugins().catch(error => console.warn("Plugin directory unavailable", error));
  return Object.freeze({ sdk, destroy() { stopped = true; clearTimeout(timer); for (const dispose of disposers.reverse()) { try { dispose(); } catch { /* Finish other cleanup. */ } } } });
}
