import { mountIcons } from "../../components/icons.js";

// Session-bound extension surfaces. Navigation retires the old mount before
// a new one starts; late predicates/renderers cannot attach to another chat.
export function createConversationContributions({ navigation, onError, sdkFor }) {
  const actions = new Map(), views = new Map();
  let stopped = false; const retiring = new Set(), failures = [];
  const context = () => {
    const route = navigation.get(), workspace = navigation.workspace();
    return Object.freeze({ project: workspace.projectId || "default", session: workspace.sessionId || "", view: route.view || "workspace" });
  };
  const dispose = async (row, callback) => {
    try { await callback?.(); } catch (error) { row.failure = error; failures.push(error); throw error; }
  };
  const release = row => {
    row.abort?.abort(); row.abort = null;
    const cleanup = row.cleanup;
    row.cleanup = null; row.node?.remove(); row.node = null; row.mounted = false;
    return dispose(row, cleanup);
  };
  const refresh = row => {
    const previous = row.task || Promise.resolve(), released = release(row), version = ++row.version;
    const scope = context();
    const abort = row.abort = new AbortController();
    const valid = () => !stopped && row.active && row.version === version && !abort.signal.aborted;
    row.task = (async () => {
    await Promise.allSettled([previous, released]);
    if (row.failure || !valid() || !scope.session || scope.view !== "workspace") return;
    const sdk = sdkFor(row.api.bind({ project: scope.project, session: scope.session }), scope);
    const options = { sdk, context: scope, signal: abort.signal };
    try {
      if (row.descriptor.when && !await row.descriptor.when(options)) return;
      if (!valid()) return;
      const host = document.querySelector(row.action ? "#sdk-conversation-actions" : "#sdk-conversation-views");
      if (!host) throw Error("The conversation extension surface is unavailable");
      const node = row.node = document.createElement(row.action ? "button" : "section");
      node.dataset.conversationContribution = row.id;
      if (row.action) {
        node.type = "button"; node.className = "icon-button";
        node.title = row.descriptor.label || row.id; node.setAttribute("aria-label", node.title);
        if (row.descriptor.icon) {
          const icon = document.createElement("span"); icon.className = "icon";
          icon.dataset.icon = row.descriptor.icon; icon.setAttribute("aria-hidden", "true"); node.append(icon); mountIcons(node);
        } else node.textContent = row.descriptor.label || row.id;
        node.onclick = async () => {
          if (!valid()) return;
          node.disabled = true;
          const execution = Promise.resolve().then(() => { if (valid()) return row.descriptor.execute(options); });
          row.executions.add(execution);
          try { await execution; }
          catch (error) { if (valid()) onError(error); }
          finally { row.executions.delete(execution); if (valid()) node.disabled = false; }
        };
      } else node.className = "sdk-conversation-view";
      host.append(node); row.mounted = true;
      if (!row.action) {
        const cleanup = await row.descriptor.mount(node, options);
        if (typeof cleanup === "function") {
          if (!valid()) await dispose(row, cleanup); else row.cleanup = cleanup;
        }
      }
    } catch (error) {
      if (valid()) { await release(row); onError(error); }
    }
    })();
    row.task.catch(onError); return row.task;
  };
  const remove = (map, row) => {
    if (row.retirement) return row.retirement;
    row.active = false; row.version++; const released = release(row); map.delete(row.id);
    row.retirement = Promise.allSettled([row.task, released, ...row.executions]).then(() => {
      if (row.failure) throw row.failure;
    });
    retiring.add(row.retirement);
    row.retirement.catch(onError).finally(() => retiring.delete(row.retirement));
    return row.retirement;
  };
  const add = (map, action, id, descriptor, api) => {
    if (stopped) throw Error("Conversation contributions have been disposed");
    if (!/^[A-Za-z0-9_.-]+$/.test(id) || map.has(id)) throw Error("Duplicate or invalid conversation contribution ID");
    if (typeof descriptor[action ? "execute" : "mount"] !== "function") throw TypeError("A conversation contribution needs a callback");
    const row = { id, action, descriptor, api, active: true, version: 0, executions: new Set() }; map.set(id, row); void refresh(row);
    return () => remove(map, row);
  };
  const unsubscribe = navigation.subscribe?.(() => { for (const row of [...actions.values(), ...views.values()]) void refresh(row); });
  return Object.freeze({
    registerAction: (id, descriptor, api) => add(actions, true, id, descriptor, api),
    registerView: (id, descriptor, api) => add(views, false, id, descriptor, api),
    async destroy() {
      if (!stopped) { stopped = true; unsubscribe?.(); for (const row of [...actions.values()]) remove(actions, row); for (const row of [...views.values()]) remove(views, row); }
      await Promise.allSettled([...retiring]); if (failures.length) throw Error("Conversation contribution cleanup did not complete");
    },
  });
}
