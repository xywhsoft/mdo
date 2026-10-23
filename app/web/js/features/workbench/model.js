export const PANEL_LIMIT = 64;
export function panelIdentity(type, key, context, owner = "local-client") {
  return JSON.stringify([owner, context.device || "", context.project || "default", context.session || "", type, key || ""]);
}
export function contextSnapshot(navigation) {
  const route = navigation.workspace();
  return Object.freeze({ project: route.projectId || navigation.preferredProject() || "default", session: route.sessionId || "", run: "" });
}
export function safeBrowserUrl(value) {
  const url = new URL(value);
  if (!["http:", "https:"].includes(url.protocol) || url.username || url.password || url.href.length > 8192)
    throw new Error("请输入 HTTP 或 HTTPS 网址；地址中不能包含账号密码");
  return url.href;
}
export function reorderPanel(ids, source, before) {
  if (!ids.includes(source) || source === before || (before && !ids.includes(before))) return [...ids];
  const next = ids.filter(id => id !== source), position = before ? next.indexOf(before) : next.length;
  next.splice(position, 0, source); return next;
}
export function normalizeLayout(value = {}) {
  return { width: Math.max(320, Math.min(960, Number(value.width) || 480)), open: Boolean(value.open),
    maximized: Boolean(value.maximized), order: [...new Set(Array.isArray(value.order) ? value.order.filter(id => typeof id === "string").slice(0, PANEL_LIMIT) : [])],
    active: typeof value.active === "string" ? value.active : "", secondary: typeof value.secondary === "string" ? value.secondary : "" };
}
