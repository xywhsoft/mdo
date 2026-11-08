import { safeBrowserUrl } from "./model.js";
import { ensureNativeBrowser } from "./native-view.js";
import { browserScopeKey as scopeKey, browserViewKey as viewKey } from "./browser-ownership.js";

// The native message only schedules reconciliation. Ownership, URLs and request
// identities are read again from the original native opener and broker scope.
export function createBrowserPopups({ bridge, api, views, clientId, alive, open, closePanels, onError }) {
  const queued = new Set(), errors = new Map(); let disposed = false, draining;
  const live = () => !disposed && alive();
  const report = (browser, error) => { if (errors.get(browser) !== error.message) { errors.set(browser,error.message);onError?.(error); } };
  async function reconcile(browser) {
    const view = views.get(browser);if (!view || !live() || view.row.client !== clientId) return;
    const identity = viewKey(view.row), scope = scopeKey(view.row);
    const context = Object.freeze({ ...view.row.context, extension: view.row.owner });
    const current = () => live() && views.get(browser) === view && viewKey(view.row) === identity;
    try {
      const state = await view.execute("state");if (!current()) return;
      const sdk = api.bind(context);
      if (state.closed_by_page) {
        // The final release removes native ownership before client.ack commits
        // its UI receipt. Automatic closure must not cancel that same action.
        const pending = await sdk.call("client.poll", { client: clientId });
        if (!current() || pending.some(action => action.action === "workbench.browser" && action.params?.id === browser)) return;
        await sdk.call("browser.close", { id: browser });if (!current()) return;
        await bridge.call("close", { browser });if (!current()) return;views.delete(browser);
        await closePanels(browser, () => live() && !views.has(browser));return;
      }
      if (!state.popup_capability) return;
      const pending = await bridge.call("popups", { browser, action: "list" });
      if (!current()) return;
      if (!pending.popups?.length) errors.delete(browser);
      for (const request of (pending.popups || []).slice(0, 4)) {
        if (!current()) return;
        let child, adopted = false, childCurrent;
        try {
          const url = request.url === "about:blank" ? "about:blank" : safeBrowserUrl(request.url);
          child = await sdk.call("browser.popup.admit", { opener: browser, client: clientId, popup: request.id, url });
          if (!current()) return;
          if (!child?.id || child.id === browser || child.opener !== browser || child.client !== clientId || scopeKey(child) !== scope)
            throw new Error("弹出页未确认原任务和显示客户端的归属。");
          const childIdentity = viewKey(child);let attached = views.get(child.id);
          childCurrent = () => live() && views.get(child.id) === attached && (!attached || viewKey(attached.row) === childIdentity);
          if (!childCurrent()) return;
          if (!child.runtime?.popup_request) {
            // A settled admission is a receipt, never permission to recreate a
            // missing native window after a lost reply or restart.
            const existing = await bridge.call("state", { browser: child.id });
            if (!current() || !childCurrent()) return;
            if (existing.opener !== browser) throw new Error("弹出页的原生连接已失效，请核实标签页状态。");
          } else await ensureNativeBrowser({ bridge, sdk, browser: child, clientId, alive: () => current() && childCurrent(), requireDownloads:state.downloads_armed===true,
            bounds: { x: 0, y: 0, width: 1024, height: 768, scale: 1 } });
          if (!current() || !childCurrent()) return;adopted = true;
          if (!attached) { attached = { row: child,
            execute: (command, params = {}) => bridge.call(command, { browser: child.id, ...params }) };views.set(child.id, attached); }
          // After acknowledged adoption the child has its own lifetime. Its
          // original task/client must still own any asynchronous panel opening.
          await open(child, sdk, request.user_initiated ? "activate" : "background", childCurrent);
        } catch (error) {
          if (!current() || error.name === "AbortError" || childCurrent && !childCurrent()) return;
          if (!adopted) {
            // Dismiss only the still-pending native request. Keep any admitted
            // child receipt when its effect is uncertain; never create a new ID.
            try { await bridge.call("popups", { browser, action: "dismiss", popup: request.id }); } catch {}
          }
          if (current()) report(browser,new Error(adopted ? "弹出页已打开，但面板未能显示。可从已有浏览器标签中重新打开。" :
            child ? "弹出页执行结果未确认，请检查已有标签后再操作。" : `未能接管网页新窗口：${error.message}`));
        }
      }
    } catch (error) { if (current() && error.name !== "AbortError") throw error; }
  }
  function pump() {
    if (draining) return draining;
    draining = (async () => {
      while (live() && queued.size) {
        const browser = queued.values().next().value;queued.delete(browser);
        try { await reconcile(browser); } catch (error) { if (live() && views.has(browser)) report(browser,error); }
      }
    })().finally(() => { draining = null; });
    return draining;
  }
  const request = browser => { if (live() && views.has(browser)) queued.add(browser);return pump(); };
  return Object.freeze({ reconcile: request,
    scan() { if (live()) for (const browser of views.keys()) queued.add(browser);return pump(); },
    dispose() { disposed = true;queued.clear();errors.clear(); }
  });
}
