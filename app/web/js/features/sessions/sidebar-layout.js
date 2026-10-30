import { t as uiErrorText } from "../../i18n.js";
import { api, currentPageWriteToken } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { createRequestRecovery } from "../../api/request-recovery.js";

export const emptySidebarLayout = () => ({ schema_version: 1, revision: 0, view: "workspace",
  project_sort: "name", groups: [], placements: [], collapsed: [] });
const id = value => typeof value === "string" && /^[A-Za-z0-9_-][A-Za-z0-9._-]{0,127}$/.test(value);
export const sidebarItemKey = value => typeof value === "string" &&
  (value.startsWith("p:") ? id(value.slice(2)) : value.startsWith("s:") && value.slice(2).split("/").length === 2 && value.slice(2).split("/").every(id));
const bytes = value => new TextEncoder().encode(value).length;
export function validSidebarLayout(value) {
  return value && Object.keys(value).sort().join() === "collapsed,groups,placements,project_sort,revision,schema_version,view" &&
    value.schema_version === 1 && Number.isSafeInteger(value.revision) && value.revision >= 0 && value.revision <= Number.MAX_SAFE_INTEGER &&
    ["workspace", "timeline"].includes(value.view) && ["name", "recent"].includes(value.project_sort) &&
    Array.isArray(value.groups) && value.groups.length <= 32 && value.groups.every(group => group &&
      Object.keys(group).sort().join() === "id,name" && /^[0-9a-f]{32}$/.test(group.id) && typeof group.name === "string" &&
      group.name.trim() && bytes(group.name) <= 256 && !/[\x00-\x1f\x7f]/.test(group.name)) &&
    new Set(value.groups.map(group => group.id)).size === value.groups.length &&
    Array.isArray(value.placements) && value.placements.length <= 2048 && value.placements.every(item => item &&
      Object.keys(item).sort().join() === "group,key" && sidebarItemKey(item.key) &&
      (item.group === "" || value.groups.some(group => group.id === item.group))) &&
    new Set(value.placements.map(item => item.key)).size === value.placements.length &&
    Array.isArray(value.collapsed) && value.collapsed.length <= 256 && value.collapsed.every(key => typeof key === "string" &&
      bytes(key) > 0 && bytes(key) <= 180 && !/[\x00-\x1f]/.test(key)) && new Set(value.collapsed).size === value.collapsed.length;
}

export function moveSidebarItem(layout, key, group = "", before = "", order = []) {
  const next = structuredClone(layout);
  if (!sidebarItemKey(key) || (group && !next.groups.some(item => item.id === group))) throw new TypeError(uiErrorText("error.detail.invalidSidebarDestination", {}, "Invalid sidebar destination"));
  const current = new Map(next.placements.map(item => [item.key, item.group]));
  const keys = [...new Set([...next.placements.map(item => item.key), ...order.filter(sidebarItemKey)])].filter(item => item !== key);
  if (before && (!keys.includes(before) || before === key)) throw new TypeError(uiErrorText("error.detail.invalidSidebarAnchor", {}, "Invalid sidebar anchor"));
  keys.splice(before ? keys.indexOf(before) : keys.length, 0, key); current.set(key, group);
  next.placements = keys.map(item => ({ key: item, group: current.get(item) || "" }));
  if (group) next.collapsed = next.collapsed.filter(key => key !== `section:custom-${group}`);
  if (!validSidebarLayout(next)) throw Object.assign(new TypeError(uiErrorText("error.detail.sidebarCapacityExceeded", {}, "Sidebar capacity exceeded")), { code: "sidebar_layout_invalid" });
  return next;
}

export function organizedSidebar(base, layout) {
  const places = new Map(layout.placements.map((item, index) => [item.key, { ...item, index }]));
  const rank = key => places.get(key)?.index ?? Infinity;
  const sort = (items, key) => [...items].sort((a, b) => rank(key(a)) - rank(key(b)));
  const skey = item => `s:${item.project_id}/${item.id}`;
  const custom = layout.groups.map(group => ({ ...group, entries: [] }));
  const groups = new Map(custom.map(group => [group.id, group]));
  function sessions(items) {
    return sort(items.filter(item => {
      const target = groups.get(places.get(skey(item))?.group);
      if (!target) return true;
      target.entries.push({ kind: "session", key: skey(item), item }); return false;
    }), skey);
  }
  const tasks = sessions(base.tasks);
  const projects = sort(base.projects.map(item => ({ ...item, sessions: sessions(item.sessions) })).filter(item => {
    const key = `p:${item.id}`, target = groups.get(places.get(key)?.group);
    if (!target) return true;
    target.entries.push({ kind: "project", key, item }); return false;
  }), item => `p:${item.id}`);
  for (const group of custom) group.entries = sort(group.entries, item => item.key);
  return { ...base, projects, tasks, custom, pinned: sort(base.pinned, skey) };
}

export function timelineSidebar(sessions, now = Date.now()) {
  const midnight = new Date(now); midnight.setHours(0, 0, 0, 0);
  const yesterday = new Date(midnight); yesterday.setDate(yesterday.getDate() - 1);
  const week = new Date(midnight); week.setDate(week.getDate() - 7);
  const groups = ["today", "yesterday", "week", "older"].map(id => ({ id, sessions: [] }));
  for (const item of sessions) {
    const date = Number(item.updated_at || 0) * 1000;
    groups[date >= midnight.getTime() ? 0 : date >= yesterday.getTime() ? 1 : date >= week.getTime() ? 2 : 3].sessions.push(item);
  }
  return groups.filter(group => group.sessions.length);
}

const targetEpoch = () => JSON.stringify([targetState().selected, targetState().runtimeChanged]);
const stamp = () => JSON.stringify([targetEpoch(), currentPageWriteToken()]);
const content = value => [value.view, value.project_sort, value.groups.map(item => [item.id, item.name]),
  value.placements.map(item => [item.key, item.group]), value.collapsed];
const same = (a, b) => JSON.stringify(content(a)) === JSON.stringify(content(b));
export function createSidebarLayoutStore({ changed = () => {}, failed = () => {}, read = (path, options) => api.get(path, options),
  write = (path, value, options) => api.put(path, value, options), recovery = createRequestRecovery } = {}) {
  let value = emptySidebarLayout(), ready = false, busy = false, disposed = false, loading = null, activeWrite = null, generation = 0;
  const path = "/sidebar-layout";
  async function load() {
    if (busy || disposed) return;
    const version = ++generation, epoch = targetEpoch(); ready = false;
    loading?.dispose(); const request = recovery(); loading = request; changed();
    try {
      const reply = await request.request(signal => read(path, { signal }));
      if (version !== generation || epoch !== targetEpoch()) return;
      if (!validSidebarLayout(reply.data)) throw new Error(uiErrorText("error.detail.invalidSidebarLayoutResponse", {}, "Invalid sidebar layout response"));
      value = reply.data; ready = true;
    } catch (error) { if (version === generation && error?.name !== "AbortError") failed(error); }
    finally { request.dispose(); if (version === generation) { loading = null; changed(); } }
  }
  const unsubscribe = subscribeTarget(() => { activeWrite?.dispose(); activeWrite = null;
    value = emptySidebarLayout(); busy = false; queueMicrotask(() => void load()); });
  return { get: () => value, writable: () => !disposed && ready && !busy && !targetState().runtimeChanged &&
      (!targetState().selected || (targetState().selected.mode === "control" && targetState().connected)),
    busy: () => busy, ready: () => ready, load,
    async update(change) {
      if (disposed || !ready || busy || targetState().runtimeChanged || (targetState().selected && (!targetState().connected || targetState().selected.mode !== "control"))) return false;
      const origin = stamp(), epoch = targetEpoch(), version = generation;
      const next = change(structuredClone(value));
      if (!validSidebarLayout(next) || bytes(JSON.stringify(next)) > 192 * 1024) throw Object.assign(new TypeError(uiErrorText("error.detail.invalidSidebarLayout", {}, "Invalid sidebar layout")), { code: "sidebar_layout_invalid" });
      if (same(next, value)) return true;
      busy = true; changed(); let confirmed = null;
      const request = recovery();
      activeWrite = request;
      function assertOwner() { if (stamp() !== origin || generation !== version) throw new DOMException("Sidebar target changed", "AbortError"); }
      try {
        try { confirmed = (await request.request(signal => { assertOwner(); return write(path, next, { signal }); }, { retry: false })).data; }
        catch (error) {
          if (stamp() !== origin || generation !== version) throw new DOMException("Sidebar target changed", "AbortError");
          // A lost ACK is read back. Never overwrite a peer by replaying PUT.
          const saved = (await request.request(signal => { assertOwner(); return read(path, { signal }); })).data;
          if (!validSidebarLayout(saved)) throw new Error(uiErrorText("error.detail.invalidSidebarLayoutResponse", {}, "Invalid sidebar layout response"));
          if (saved.revision === next.revision + 1 && same(saved, next)) confirmed = saved;
          else { value = saved; throw error; }
        }
        if (stamp() !== origin || version !== generation || epoch !== targetEpoch()) return false;
        if (!validSidebarLayout(confirmed) || confirmed.revision !== next.revision + 1 || !same(confirmed, next)) throw new Error(uiErrorText("error.detail.invalidSidebarWriteAcknowledgement", {}, "Invalid sidebar write acknowledgement"));
        value = confirmed; return true;
      } finally { request.dispose(); if (activeWrite === request) activeWrite = null;
        if (version === generation) { busy = false; changed(); } }
    }, destroy() { disposed = true; ++generation; loading?.dispose(); activeWrite?.dispose(); unsubscribe(); } };
}
