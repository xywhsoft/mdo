// UI instance identity only. Broker/native permission checks remain authoritative.
export function browserScopeKey(row) {
  return JSON.stringify([row?.owner || "", ...["device","project","session","run","workspace","namespace","member"]
    .map(key => row?.context?.[key] || (key === "project" ? "default" : ""))]);
}
export function browserViewKey(row) {
  return JSON.stringify([row?.resource?.id || row?.id, row?.client, browserScopeKey(row)]);
}

// UI claims are separate from task dispatchers: an initializing pane must not
// advertise a ready native browser to Agent actions or popup reconciliation.
const mountOwners = new WeakMap();

/** The UI owns one mount; the task's native command dispatcher outlives it. */
export function createBrowserViewLifetime({ row, signal, mount, mounts, entries, views, alive, cleanupAlive = alive, close, reconcile }) {
  const browser = row.resource?.id, entry = entries.get(row.id), original = views.get(browser);
  if (!mountOwners.has(views)) mountOwners.set(views, new Map());
  const owners = mountOwners.get(views), originalOwner = owners.get(browser);
  const scope = browserScopeKey(row);
  const panelKey = value => JSON.stringify([browserViewKey(value), value?.type, value?.params?.url || ""]);
  const identity = panelKey(entry);
  let registered, claimed, boundExecute, ended = false, reconciled = false;
  const samePanel = () => entry &&
    browserScopeKey(entries.get(row.id)) === scope && panelKey(entries.get(row.id)) === identity;
  const mounted = () => alive() && !signal.aborted && mounts.get(row.id) === mount && samePanel();
  const owns = () => owners.get(browser) === (claimed || originalOwner);
  const current = () => !ended && Boolean(mounted()) && owns() && (registered ? views.get(browser) === registered :
    views.get(browser) === original && (!original || browserViewKey(original.row) === browserViewKey(row)));
  const bind = view => {
      if (!current() || !browser || view.id !== browser || typeof view.execute !== "function") return false;
      if (claimed) return boundExecute === view.execute;
      claimed = Object.freeze({}); boundExecute = view.execute; owners.set(browser, claimed);
      return true;
  };
  return Object.freeze({ current, bind,
    canHide: () => !ended && cleanupAlive() && Boolean(samePanel()) && claimed && owns() &&
      views.get(browser) === (registered || original) &&
      (!mounts.has(row.id) || mounts.get(row.id) === mount),
    release: () => { if (claimed && owns()) owners.delete(browser); },
    canRemount: () => !ended && Boolean(mounted()) && views.has(browser) &&
      browserViewKey(views.get(browser).row) === browserViewKey(row),
    register(view) {
      if (!bind(view)) return false;
      if (!registered) { registered = { execute: view.execute, row }; views.set(browser, registered); }
      if (!reconciled) { reconciled = true; void reconcile(browser); }
      return true;
    },
    async end(id) {
      if (id !== browser || !current() || !registered && views.has(id)) return;
      ended = true;if (registered) views.delete(id);if (claimed && owns()) owners.delete(browser);
      await close(row.id, () => Boolean(mounted()) && !views.has(id));
    }
  });
}
