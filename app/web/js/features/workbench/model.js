import { t as uiText } from "../../i18n.js";
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
    throw new Error(uiText("ui.model.enterAnHttpOrHttpsUrlWithoutEmbeddedCredentials", {}, "请输入 HTTP 或 HTTPS 网址；地址中不能包含账号密码"));
  return url.href;
}
/** Shared omnibox rules: ports and IPv6 literals are URLs, words are search. */
export function browserLocation(value) {
  const text = String(value || "").trim();
  if (!text) throw new Error(uiText("ui.model.enterAUrlOrSearchQuery", {}, "请输入网址或搜索内容"));
  if (text.length > 8192) throw new Error(uiText("ui.model.urlOrSearchQueryIsTooLong", {}, "网址或搜索内容过长"));
  if (/^https?:\/\//i.test(text)) return safeBrowserUrl(text);
  const hostPort = /^(?:localhost|[\w.-]+|\[[a-f\d:]+\])(?::\d+)?(?:[/?#]|$)/i.test(text);
  if (hostPort && !/\s/.test(text) && (/[.:\[]/.test(text.split(/[/?#]/)[0]) || /^localhost(?:[/:?#]|$)/i.test(text))) {
    const local = /^(?:localhost|127(?:\.\d+){3}|\[::1\])(?::\d+)?(?:[/?#]|$)/i.test(text);
    return safeBrowserUrl(`${local ? "http" : "https"}://${text}`);
  }
  if (/^[a-z][a-z\d+.-]*:/i.test(text)) throw new Error(uiText("ui.model.enterAnHttpOrHttpsUrl", {}, "请输入 HTTP 或 HTTPS 网址"));
  return safeBrowserUrl(`https://www.bing.com/search?q=${encodeURIComponent(text)}`);
}
export function browserPhase(state) {
  if (state.error) return "error";
  if (!state.url || state.url === "about:blank") return "empty";
  return state.loading ? "loading" : "ready";
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

export function validateImageFrame(bytes) {
  const data = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let width = 0, height = 0;
  if (bytes.length >= 24 && [137,80,78,71,13,10,26,10].every((value,index) => bytes[index] === value)) {
    width = data.getUint32(16); height = data.getUint32(20);
  } else if (bytes.length >= 4 && bytes[0] === 255 && bytes[1] === 216) {
    let at = 2;
    while (at + 3 < bytes.length) {
      if (bytes[at++] !== 255) throw new Error(uiText("ui.model.invalidJpegFrameFormat", {}, "JPEG 画面帧格式无效"));
      while (bytes[at] === 255) at++;
      const marker = bytes[at++]; if ([0xd8,0xd9].includes(marker)) continue;
      if (at + 2 > bytes.length) break; const size = data.getUint16(at);
      if (size < 2 || at + size > bytes.length) break;
      if ([0xc0,0xc1,0xc2,0xc3,0xc5,0xc6,0xc7,0xc9,0xca,0xcb,0xcd,0xce,0xcf].includes(marker) && size >= 7) { height = data.getUint16(at + 3); width = data.getUint16(at + 5); break; }
      at += size;
    }
  }
  if (!width || !height || width > 8192 || height > 8192 || width * height > 16777216) throw new Error(uiText("ui.model.framesMustBePngJpegAndDecodeToAt", {}, "画面需为 PNG/JPEG，且解码尺寸不能超过 1600 万像素"));
  return { width, height };
}
