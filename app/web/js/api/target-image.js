import { isRemoteTarget, targetFetch } from "./target.js";

const images = new Map();
let observer = null;
function dispose(node) {
  const entry = images.get(node); if (!entry) return;
  images.delete(node); entry.cancel.abort();
  if (entry.url) URL.revokeObjectURL(entry.url);
}
function watch() {
  if (!observer && typeof MutationObserver === "function") {
    observer = new MutationObserver(() => {
      for (const [node, entry] of images) {
        if (node.isConnected) entry.mounted = true;
        else if (entry.mounted) dispose(node);
      }
    });
    observer.observe(document.body, { childList: true, subtree: true });
    window.addEventListener("pagehide", () => { for (const node of images.keys()) dispose(node); }, { once: true });
  }
}

// Application images must use the same target transport as commands. Bundle
// icons remain ordinary assets. A preview gets its own source lease, so removal
// of the old timeline row cannot revoke the open preview's blob URL.
export function bindTargetImage(node, path) {
  dispose(node); node.dataset.mdoImagePath = path;
  if (!isRemoteTarget()) { node.setAttribute("src", path); return; }
  if (!/^\/api\/v1\/projects\/[A-Za-z0-9._-]+\/sessions\/[A-Za-z0-9._-]+\/attachments\/[0-9a-f]{32}$/.test(path))
    throw new TypeError("Image is outside the selected target");
  node.removeAttribute("src"); watch();
  const entry = { cancel: new AbortController(), url: "", mounted: false }; images.set(node, entry);
  queueMicrotask(async () => {
    if (images.get(node) !== entry) return;
    entry.mounted = node.isConnected;
    let reader, response;
    try {
      response = await targetFetch(path, { cache: "no-store", redirect: "error", signal: entry.cancel.signal });
      const type = (response.headers.get("Content-Type") || "").split(";")[0];
      if (!response.ok || !["image/png", "image/jpeg", "image/webp"].includes(type) || !response.body)
        throw new Error("Invalid target image response");
      const length = response.headers.get("Content-Length");
      if (length !== null && (!/^[1-9][0-9]*$/.test(length) || Number(length) > 8388608)) throw new Error("Invalid target image length");
      reader = response.body.getReader(); const parts = []; let size = 0;
      for (;;) {
        const { value, done } = await reader.read(); if (done) break;
        if (value.length > 8388608 - size) throw new Error("Target image exceeds limit");
        parts.push(value); size += value.length;
      }
      if (!size || (length !== null && Number(length) !== size)) throw new Error("Incomplete target image");
      if (images.get(node) !== entry || entry.cancel.signal.aborted) return;
      entry.url = URL.createObjectURL(new Blob(parts, { type })); node.setAttribute("src", entry.url);
    } catch (error) {
      if (images.get(node) === entry && !entry.cancel.signal.aborted) node.dispatchEvent(new Event("error"));
    } finally {
      if (reader) { await reader.cancel().catch(() => {}); reader.releaseLock(); }
      else await response?.body?.cancel().catch(() => {});
    }
  });
}
export const releaseTargetImage = dispose;
export function targetImage(path, attrs) {
  const node = document.createElement("img");
  for (const [key, value] of Object.entries(attrs || {})) node.setAttribute(key, String(value));
  bindTargetImage(node, path); return node;
}
