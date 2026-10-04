/* A browser entry's fullPath is relative to the dropped tree, not an OS path.
   Never register a workspace from that value. Native xs windows return the
   actual path of the user-dropped FileSystemHandle (or legacy File) through
   WebView2 AdditionalObjects, without uploading or reading its contents. */
function failure(code) { return Object.assign(new Error(code), { code }); }

export function absoluteFolderPath(value) {
  if (typeof value !== "string") return "";
  const path = value.trim().replace(/^"(.*)"$/, "$1");
  if (!path || /[\x00-\x1f\x7f]/.test(path) || new TextEncoder().encode(path).length > 2048)
    return "";
  return /^(?:[A-Za-z]:[\\/]|\\\\[^\\/]+[\\/]|\/)/.test(path) ? path : "";
}

function textPath(transfer) {
  const uri = transfer.getData?.("text/uri-list")?.trim();
  if (uri) {
    const entries = uri.split(/\r?\n/).filter((line) => line && !line.startsWith("#"));
    if (entries.length !== 1) throw failure("folder_drop_one");
    try {
      const url = new URL(entries[0]);
      if (url.protocol !== "file:" || url.search || url.hash) return "";
      let path = decodeURIComponent(url.pathname);
      if (url.hostname && url.hostname !== "localhost") path = `\\\\${url.hostname}${path.replaceAll("/", "\\")}`;
      else if (/^\/[A-Za-z]:\//.test(path)) path = path.slice(1).replaceAll("/", "\\");
      return absoluteFolderPath(path);
    } catch (error) {
      if (error.code) throw error;
      return "";
    }
  }
  return absoluteFolderPath(transfer.getData?.("text/plain"));
}

function nativePath(file, bridge, signal) {
  return new Promise((resolve, reject) => {
    const requestId = crypto.randomUUID().replaceAll("-", "");
    let timer;
    const cleanup = () => {
      clearTimeout(timer);
      bridge.removeEventListener("message", receive);
      signal?.removeEventListener("abort", abort);
    };
    const finish = (error, path) => { cleanup(); error ? reject(error) : resolve(path); };
    const abort = () => finish(new DOMException("Aborted", "AbortError"));
    const receive = (event) => {
      const data = event.data;
      if (data?.type !== "xs-directory-drop" || data.request_id !== requestId) return;
      const path = absoluteFolderPath(data.path);
      finish(path ? null : failure(data.error === "not-directory" ? "folder_drop_directory" : "folder_drop_unavailable"), path);
    };
    if (signal?.aborted) { abort(); return; }
    bridge.addEventListener("message", receive);
    signal?.addEventListener("abort", abort, { once: true });
    timer = setTimeout(() => finish(failure("folder_drop_unavailable")), 5000);
    try { bridge.postMessageWithAdditionalObjects(`xs-directory-drop:${requestId}`, [file]); }
    catch { finish(failure("folder_drop_unavailable")); }
  });
}

export async function droppedFolderPath(transfer, { signal,
  bridge = globalThis.chrome?.webview } = {}) {
  // Capture DataTransfer during the drop event; browsers protect it afterward.
  const files = [...(transfer?.files ?? [])];
  const items = [...(transfer?.items ?? [])].filter((item) => item.kind === "file");
  if (files.length > 1 || items.length > 1) throw failure("folder_drop_one");
  const entry = items[0]?.webkitGetAsEntry?.();
  if (entry?.isFile) throw failure("folder_drop_directory");
  const explicit = textPath(transfer ?? {});
  if (explicit) return explicit;
  const path = absoluteFolderPath(files[0]?.path);
  if (path) return path;
  let handleRequest;
  if (bridge?.postMessageWithAdditionalObjects)
    try { handleRequest = items[0]?.getAsFileSystemHandle?.(); } catch { /* Use the File fallback. */ }
  if (bridge?.postMessageWithAdditionalObjects && handleRequest) {
    let handle;
    try { handle = await handleRequest; } catch { /* Older engines may reject handles. */ }
    if (handle?.kind === "file") throw failure("folder_drop_directory");
    if (handle?.kind === "directory") {
      try { return await nativePath(handle, bridge, signal); }
      catch (error) {
        if (signal?.aborted || error.code !== "folder_drop_unavailable" || files.length !== 1) throw error;
      }
    }
  }
  if (files.length === 1 && bridge?.postMessageWithAdditionalObjects)
    return nativePath(files[0], bridge, signal);
  throw failure("folder_drop_unavailable");
}
