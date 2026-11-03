import { t } from "../../i18n.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { targetFetch } from "../../api/target.js";
import { bindTargetImage } from "../../api/target-image.js";
import { createApplicationClient } from "../../api/application.js";
import { callDisplayService } from "./markdown-transport.js";

export function markdownAddress(input, image = false) {
  let value = String(input || "").trim();
  if (!value || /[\x00-\x1f\x7f]/.test(value)) return null;
  if (image && value.length <= 1024 * 1024 && /^data:image\/(?:png|jpeg|gif|webp);base64,[A-Za-z0-9+/]+={0,2}$/i.test(value))
    return { kind: "web", url: value };
  if (/^https?:\/\//i.test(value) || value.startsWith("//")) {
    try { const url = new URL(value, "https://mdo.invalid/"); return { kind: "web", url: url.href }; } catch { return null; }
  }
  if (!image && /^mailto:/i.test(value) && !/%0[ad]/i.test(value)) return { kind: "web", url: value };
  if (!image && value.startsWith("#")) return { kind: "anchor", url: value.slice(1) };
  if (/^file:/i.test(value)) {
    try { const url = new URL(value); if (url.hostname && url.hostname !== "localhost") return null;
      value = decodeURIComponent(url.pathname).replace(/^\/([a-z]:\/)/i, "$1"); } catch { return null; }
  } else {
    if (/^[a-z][a-z\d+.-]*:/i.test(value) && !/^[a-z]:[\\/]/i.test(value)) return null;
    try { value = decodeURIComponent(value); } catch { return null; }
  }
  value = value.replaceAll("\\", "/");
  if (/^[a-z][a-z\d+.-]*:/i.test(value) && !/^[a-z]:\//i.test(value)) return null;
  if (!value || value.startsWith("//") || /[\x00-\x1f\x7f]/.test(value) || value.length > 1024) return null;
  let line = 1;
  const suffix = /(?::(\d+)(?::\d+)?|#L(\d+)(?:-L?\d+)?)$/.exec(value);
  if (!image && suffix) { line = Number(suffix[1] || suffix[2]); value = value.slice(0, suffix.index); }
  if (image) value = value.split("#")[0];
  return value && Number.isSafeInteger(line) && line > 0 ? { kind: "file", path: value, line } : null;
}
export function markdownResourceUrl(owner, path, mode = "image") {
  if (!owner?.projectId || !owner?.sessionId) return null;
  return `/api/v1/projects/${encodeURIComponent(owner.projectId)}/sessions/${encodeURIComponent(owner.sessionId)}` +
    `/markdown-resource?path=${encodeURIComponent(path)}&mode=${mode}`;
}
export async function openMarkdownFile(address, owner, { signal, onOpenFile } = {}) {
  const path = markdownResourceUrl(owner, address.path, "file");
  if (!path) throw new Error(t("markdown.fileUnavailable"));
  const response = await targetFetch(path, { signal, cache: "no-store" });
  const body = await response.json();
  if (!response.ok || !body.ok) throw new Error(body.error?.message || t("markdown.fileUnavailable"));
  if (signal?.aborted) throw new DOMException("Cancelled", "AbortError");
  const open = onOpenFile || ((data) => {
    const client = createApplicationClient(() => ({ project: owner.projectId, session: owner.sessionId }));
    if (!globalThis.mdo?.ui?.openFile) throw new Error(t("markdown.fileUnavailable"));
    return globalThis.mdo.ui.openFile(data, client);
  });
  return open({ path: body.data.path, line: address.line });
}
export function nativeExternalLink(event, url) {
  if (!globalThis.chrome?.webview && !globalThis.external?.invoke &&
      !globalThis.webkit?.messageHandlers?.external && !globalThis.XsBrowser) return;
  event.preventDefault();
  const cancel = new AbortController(), timer = setTimeout(() => cancel.abort(), 8000);
  callDisplayService("system.open", { target: url }, { signal: cancel.signal })
    .catch(error => toast(errorMessage(error), "error")).finally(() => clearTimeout(timer));
}
export function markdownImage(address, alt, title, owner, { inLink = false, onLayout = () => {} } = {}) {
  const url = address?.kind === "file" ? markdownResourceUrl(owner, address.path) : address?.url;
  if (!url) return element("span", { className: "md-image-status", text: alt || t("markdown.imageUnavailable") });
  const image = element("img", { attrs: { alt, loading: "lazy", decoding: "async", referrerpolicy: "no-referrer" } });
  const preview = element(inLink ? "span" : "button", { className: "md-image-preview", attrs: inLink ? {} : {
    type: "button", "data-image-preview": "", "aria-label": t("markdown.viewImage", { alt: alt || t("markdown.image") }) } }, [image]);
  if (title) preview.title = title;
  const note = element("span", { className: "md-image-status", attrs: { role: "status" } });
  const retry = element("button", { className: "md-image-retry", text: t("image.previewRetry"), attrs: { type: "button" } });
  const root = element("span", { className: "md-image", attrs: { "data-md-image-key": JSON.stringify([url, alt, title, inLink]) } }, [preview, note, retry]);
  retry.hidden = true;
  const load = () => { note.textContent = ""; retry.hidden = true;
    if (address.kind === "file") bindTargetImage(image, url); else image.src = url; };
  image.addEventListener("error", () => { note.textContent = t("image.previewFailed"); retry.hidden = false; onLayout(); });
  image.addEventListener("load", () => { note.textContent = ""; retry.hidden = true; onLayout(); });
  retry.addEventListener("click", event => { event.preventDefault(); event.stopPropagation(); load(); });
  load(); return root;
}
