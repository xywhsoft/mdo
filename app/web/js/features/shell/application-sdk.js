import { createApplicationClient, createLocalApplicationClient, applyApplicationAction } from "../../api/application.js";
import { isRemoteTarget, targetState } from "../../api/target.js";
import { toast } from "../../utils/dom.js";
import { createPanelManager } from "../workbench/panel-manager.js";

export function createApplicationUi({ navigation, executeActions, canCall = () => true, addContext }) {
  let clientId = `ui-${crypto.randomUUID()}`;
  try {
    const saved = sessionStorage.getItem("mdo.sdk.client");
    if (/^ui-[a-f0-9-]{36}$/.test(saved || "")) clientId = saved;
    else sessionStorage.setItem("mdo.sdk.client", clientId);
  } catch { /* Effects still require the separate intent journal below. */ }
  const api = createApplicationClient(() => ({ project: navigation.preferredProject(), session: navigation.workspace().sessionId || "", client: clientId }));
  const localApi = createLocalApplicationClient(() => ({ project: "default", client: clientId }));
  const panels = new Map(), menus = new Map(), renderers = new Map(), actions = new Map(), openDialogs = new Map(), disposers = [];
  const pluginScopes = new Map(); let pluginCursor = {};
  const shell = document.querySelector("#app-shell");
  const workbench = shell ? createPanelManager({ shell, api, layoutApi: isRemoteTarget() ? localApi : api, navigation, clientId, addContext, renderers }) : null;
  let stopped = false, timer = null, registered = false, pluginsLoaded = false, pluginLoading = null, registeredScope = "";
  const register = (map, id, value) => {
    if (stopped) throw new Error("Application UI instance has been disposed");
    if (!/^[A-Za-z0-9_.-]+$/.test(id) || map.has(id)) throw new Error("Duplicate or invalid SDK contribution ID");
    map.set(id, value); let active = true;
    return () => { if (active && map.get(id) === value) map.delete(id); active = false; };
  };
  const openPanel = async (id, params, callerApi) => {
    if (stopped) throw new Error("Application UI instance has been disposed");
    const descriptor = panels.get(id); if (!descriptor) throw new Error(`Panel ${id} is not registered`);
    if (descriptor.placement === "right" && workbench) {
      await descriptor.registration;
      return workbench.open(id, params, callerApi || descriptor.api, descriptor);
    }
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
  const createScope = clientApi => {
    let active = true, scopedSdk; const owned = [];
    const check = () => { if (!active || stopped) throw new Error("Application UI instance has been disposed"); };
    const own = cleanup => {
      let retained = true; const dispose = () => { if (retained) { retained = false; cleanup(); } };
      if (!active || stopped) dispose(); else owned.push(dispose); return dispose;
    };
    const viewSdk = options => !options.context ? scopedSdk : Object.freeze({ ...scopedSdk, ...options.sdk,
      context: () => Object.freeze({ ...options.context }),
      ui: Object.freeze({ ...ui, openPanel: (id, params) => openPanel(id, params, options.sdk),
        openEditor: args => ui.openEditor(args, options.sdk), newTab: () => workbench.open("mdo.new-tab", { title: "新标签页" }, options.sdk) }) });
    const ui = Object.freeze({
    registerPanel: (id, descriptor) => {
      check();
      const callback = descriptor.mount || descriptor.render;
      const contribution = { ...descriptor, api: clientApi,
        render: callback && ((root, options) => callback(root, { ...options, sdk: viewSdk(options) })),
        mount: descriptor.mount && ((root, options) => callback(root, { ...options, sdk: viewSdk(options) })) };
      const remove = register(panels, id, contribution);
      let removeDock;
      if (descriptor.placement === "right" && workbench) {
        removeDock = workbench.register(id, contribution);
        contribution.registration = clientApi.call("panel.register", { id, title: descriptor.title || id, renderer: callback ? "custom" : descriptor.renderer, scope: descriptor.scope || "session" });
        contribution.registration.catch(error => toast(`面板注册失败：${error.message}`, "error"));
      }
      const dispose = () => { for (const dialog of openDialogs.get(id) || []) dialog.close(); openDialogs.delete(id); removeDock?.(); remove(); };
      return own(dispose);
    },
    registerAction: (id, execute) => { check(); return own(register(actions, id, (params, options) => execute(params, { ...options, sdk: scopedSdk }))); },
    registerRenderer: (id, render) => {
      check(); const remove = register(renderers, id, (root, value, options) => render(root, value, { ...options, sdk: viewSdk(options) }));
      workbench?.rendererChanged(id);
      return own(() => { remove(); workbench?.rendererChanged(id); });
    },
    render: (id, root, value, options = {}) => { check(); const render = renderers.get(id); if (!render) throw new Error("Renderer is not registered"); return render(root, value, { ...options, sdk: scopedSdk }); },
    registerMenu: (id, descriptor) => {
      check();
      const button = document.createElement("button"); button.type = "button"; button.textContent = descriptor.label || id;
      const root = document.querySelector(descriptor.mount || ".sidebar"); if (!root) throw new Error("Menu mount is unavailable");
      const remove = register(menus, id, button);
      button.onclick = () => Promise.resolve().then(() => { check(); return descriptor.execute({ sdk: scopedSdk }); }).catch(error => toast(error.message, "error"));
      try { root.append(button); } catch (error) { button.remove(); remove(); throw error; }
      return own(() => { button.remove(); remove(); });
    },
    registerKeybinding: (id, descriptor) => {
      check();
      const callback = event => {
        if (event.isComposing || event.repeat || event.code !== descriptor.code || Boolean(event.ctrlKey || event.metaKey) !== Boolean(descriptor.mod) || Boolean(event.altKey) !== Boolean(descriptor.alt) || Boolean(event.shiftKey) !== Boolean(descriptor.shift)) return;
        if (!descriptor.inInput && event.target?.closest?.("input,textarea,[contenteditable=true]")) return;
        event.preventDefault(); Promise.resolve().then(() => { check(); return descriptor.execute({ sdk: scopedSdk, event }); }).catch(error => toast(error.message, "error"));
      };
      const remove = register(actions, `keybinding.${id}`, callback); document.addEventListener("keydown", callback);
      return own(() => { document.removeEventListener("keydown", callback); remove(); });
    },
    openPanel: (id, params) => { check(); return openPanel(id, params); },
    newTab: () => { check(); if (!workbench) throw new Error("Workbench is unavailable"); return workbench.newTab(); },
    openWindow: ({ url, name = "_blank" }) => {
      check();
      const opened = window.open(url, name, "noopener");
      if (!opened) throw new Error("The OS or browser blocked the new window; open it from a user action");
    },
    openEditor: async ({ path, space = "workspace" }, callerApi = clientApi) => {
      check(); const editorApi = callerApi.bind({});
      const resolved = await editorApi.call("fs.resolve", { path, space }); path = resolved.path; space = "native";
      const resource = await editorApi.call("fs.read", { path, space, limit: 262144 }); check();
      if (typeof resource.text !== "string" || resource.bytes !== resource.size) throw new Error("The editor supports complete UTF-8 files up to 256 KiB");
      if (workbench) {
        const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(JSON.stringify([space, path])));
        const key = [...new Uint8Array(digest)].map(value => value.toString(16).padStart(2,"0")).join("");
        return workbench.open("mdo.editor", { key, title: path, params: { path, space } }, editorApi);
      }
      const id = `editor.${crypto.randomUUID()}`;
      const dispose = ui.registerPanel(id, { title: path, render(root, { signal }) {
        const input = document.createElement("textarea"), save = document.createElement("button"); input.value = resource.text; input.style.cssText = "width:70vw;height:60vh"; save.textContent = "保存";
        save.onclick = async () => { save.disabled = true; try { const saved = await editorApi.call("fs.write", { path, space, text: input.value, revision: resource.revision }, { signal }); resource.revision = saved.revision; save.textContent = "已保存"; } catch (error) { toast(error.message, "error"); } finally { save.disabled = false; } };
        root.append(input, save); return dispose;
      } }); await openPanel(id, {});
    },
    });
    scopedSdk = Object.freeze({ ...clientApi, ui, clientId, context: () => navigation.get() });
    return { sdk: scopedSdk, own, dispose() { if (!active) return; active = false; for (const dispose of owned.reverse()) { try { dispose(); } catch { /* Finish every owned cleanup. */ } } if (!stopped && workbench) void workbench.retireOwner(clientApi).catch(error => toast(`插件资源清理未完成：${error.message}`, "error")); } };
  };
  const rootScope = createScope(api), sdk = rootScope.sdk, ui = sdk.ui; disposers.push(() => rootScope.dispose());
  const leaseTimer = setInterval(() => {
    if (stopped || !registered || !canCall()) return;
    void api.call("client.hello", { client: clientId, context: navigation.get() }).catch(() => { registered = false; });
  }, 15000);
  const execute = async action => {
    // Workbench actions address frozen instances, not the selected chat. Their
    // owner and context have already been checked by the workbench broker.
    if (action.action.startsWith("workbench.") && workbench) return workbench.execute(action);
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
      if (!canCall() || targetState().selected?.mode === "view") return;
      const scope = JSON.stringify(navigation.get());
      if (!registered || scope !== registeredScope) { await api.call("client.hello", { client: clientId, context: navigation.get() }); registered = true; registeredScope = scope; }
      if (workbench) await workbench.ensure();
      // Activation may await a browser/UI receipt. Keep servicing the window
      // while it runs, otherwise activation and the UI queue deadlock.
      if (!pluginsLoaded && !pluginLoading) pluginLoading = loadPlugins().catch(() => {}).finally(() => { pluginLoading = null; });
      if (stopped || !canCall()) return;
      if (workbench && pluginScopes.size) {
        const pluginApi = isRemoteTarget() ? localApi : api;
        const changes = await pluginApi.events({ ...pluginCursor, topic: "plugin.changed" });
        pluginCursor = { after: changes.cursor, epoch: changes.epoch };
        const ids = new Set((changes.items || []).map(item => item.data?.id));
        if (changes.resync_required) for (const id of pluginScopes.keys()) ids.add(id);
        if (ids.size) {
          const current = await pluginApi.call("plugin.list");
          for (const id of ids) {
            const active = pluginScopes.get(id), next = current.items.find(item => item.id === id);
            if (active && (!next?.enabled || next.error || next.revision !== active.revision)) {
              active.scope.dispose(); pluginScopes.delete(id);
              if (next?.enabled) toast(`插件 ${id} 已更新，请重新载入界面以启用新版`, "info");
            }
          }
        }
      }
      const pending = await api.call("client.poll", { client: clientId });
      if (stopped) return;
      for (const action of pending) {
        if (stopped) break;
        const current = await api.call("ui.result", { id: action.id });
        if (["cancelled", "failed", "succeeded"].includes(current.state)) continue;
        let ack;
        try { ack = await applyApplicationAction(action, execute, sessionStorage, `mdo.sdk.actions.${clientId}`); }
        catch (error) { ack = { success: false, error: `UI intent journal unavailable: ${error.message}` }; }
        await api.call("client.ack", { client: clientId, id: action.id, ...ack });
      }
    } catch { registered = false; }
    finally { if (!stopped) timer = setTimeout(poll, document.hidden ? 10000 : 1500); }
  };
  // Installed local UI code runs in the displaying client's DOM. Its SDK calls
  // still use the explicitly selected target through the standard API adapter.
  const loadPlugins = async () => {
    const result = await (isRemoteTarget() ? localApi : api).call("plugin.list"); if (result.recovery_mode || stopped) return;
    // Once activation starts, never automatically repeat its effects. Failed
    // directory reads can recover through poll; an activation failure cannot.
    pluginsLoaded = true;
    for (const plugin of result.items) if (plugin.enabled && !plugin.error && plugin.frontend?.startsWith("web/")) {
      let scope;
      try {
        const module = await import(`/${plugin.frontend.slice(4)}?revision=${encodeURIComponent(plugin.revision)}`);
        if (stopped) return;
        if (typeof module.activate !== "function") throw new Error("Plugin frontend must export activate(sdk)");
        const scopedApi = createApplicationClient(() => ({ project: navigation.preferredProject(), session: navigation.workspace().sessionId || "", client: clientId, extension: `plugin.${plugin.id}` }));
        scope = createScope(scopedApi); disposers.push(() => scope.dispose());
        const dispose = await module.activate(scope.sdk); if (typeof dispose === "function") scope.own(dispose);
        pluginScopes.set(plugin.id, { scope, revision: plugin.revision });
      } catch (error) { scope?.dispose(); console.error(`Plugin ${plugin.id} failed`, error); toast(`插件 ${plugin.id} 启动失败：${error.message}`, "error"); }
    }
  };
  void poll();
  return Object.freeze({ sdk, canLeave: () => workbench?.canLeave() ?? true, destroy() { if (stopped) return; stopped = true; clearTimeout(timer); clearInterval(leaseTimer); workbench?.destroy(); for (const dispose of disposers.reverse()) { try { dispose(); } catch { /* Finish other cleanup. */ } } } });
}
