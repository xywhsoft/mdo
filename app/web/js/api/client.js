import { targetFetch } from "./target.js";

const API_ROOT = "/api/v1";
let writeGuard = null;
let pageWriteToken = null;
let writeConflictHandler = null;
let networkErrorHandler = null;
let pendingWrites = 0;
export function hasPendingApiWrites() { return pendingWrites !== 0; }

async function trackWrite(operation) {
  pendingWrites += 1;
  try { return await operation(); }
  finally { pendingWrites -= 1; }
}

export function currentPageWriteToken() { return pageWriteToken; }
export function setApiWriteConflictHandler(handler) { writeConflictHandler = handler; }
export function setApiNetworkErrorHandler(handler) { networkErrorHandler = handler; }

// A recovery gate is local to this page. The server remains authoritative;
// this prevents restored drafts/queues from writing before recovery is read.
export function setApiWriteGuard(guard) {
  writeGuard = guard;
  return () => { if (writeGuard === guard) writeGuard = null; };
}

function checkSignal(signal) {
  if (signal?.aborted) throw new DOMException("Request cancelled", "AbortError");
}

// A cancelled adapter or body may never settle. Release the public request
// and write tracking ourselves; late work cannot publish tokens or feedback.
async function withSignal(signal, operation) {
  if (!signal) return operation();
  checkSignal(signal);
  let cancel;
  const cancelled = new Promise((_, reject) => {
    cancel = () => reject(new DOMException("Request cancelled", "AbortError"));
    signal.addEventListener("abort", cancel, { once: true });
  });
  try { return await Promise.race([operation(), cancelled]); }
  finally { signal.removeEventListener("abort", cancel); }
}

export function allowsApiWrite(path, options = {}) {
  const method = (options.method ?? "GET").toUpperCase();
  return ["GET", "HEAD", "OPTIONS"].includes(method) || !writeGuard ||
    writeGuard({ ...options, path, method });
}

function checkWrite(path, options) {
  if (!allowsApiWrite(path, options))
    throw new ApiError("Review the saved project purge request before writing", {
      code: "purge_review_required",
    });
}

export class ApiError extends Error {
  constructor(message, options = {}) {
    super(message);
    this.name = "ApiError";
    this.status = options.status ?? 0;
    this.code = options.code ?? "request_failed";
    this.requestId = options.requestId ?? "";
    this.details = options.details ?? null;
  }
}

export function apiUrl(path) {
  if (typeof path !== "string" || !path.startsWith("/") || path.startsWith("//")) {
    throw new TypeError("API path must be an absolute path within /api/v1");
  }
  return `${API_ROOT}${path}`;
}

export function resourceId(value, label = "resource") {
  const text = String(value ?? "");
  if (!/^[A-Za-z0-9_-][A-Za-z0-9._-]*$/.test(text)) {
    throw new TypeError(`${label} ID is invalid`);
  }
  return text;
}

export function attachmentUrl(projectId, sessionId, id) {
  if (!/^[0-9a-f]{32}$/.test(id)) throw new TypeError("Attachment ID is invalid");
  return apiUrl(`/projects/${resourceId(projectId, "project")}` +
    `/sessions/${resourceId(sessionId, "session")}/attachments/${id}`);
}

// Display metadata only. Reject path separators, controls and malformed UTF-16
// before encoding a header; the server separately validates decoded UTF-8.
export function attachmentFileName(value) {
  if (typeof value !== "string" || /[\x00-\x1f\x7f/\\]/.test(value)) return "";
  try { encodeURIComponent(value); } catch { return ""; }
  return new TextEncoder().encode(value).length <= 1024 ? value : "";
}

async function readEnvelope(response, path = "", method = "GET", signal) {
  checkSignal(signal);
  let envelope = null;
  try { envelope = await response.json(); }
  catch (error) {
    checkSignal(signal);
    if (error?.name === "AbortError") throw error;
    if (error?.code) throw new ApiError(error.message, error);
    throw new ApiError("服务返回了无效响应", {
      status: response.status, code: "invalid_response",
    });
  }
  checkSignal(signal);
  if (!response.ok || envelope?.ok !== true) {
    const error = new ApiError(envelope?.error?.message || `请求失败 (${response.status})`, {
      status: response.status,
      code: envelope?.error?.code,
      requestId: envelope?.request_id,
      details: envelope?.error?.details,
    });
    if (["write_token_required", "write_token_invalid", "write_token_conflict"].includes(error.code))
      writeConflictHandler?.(error);
    throw error;
  }
  const writeToken = response.headers.get("X-Mdo-Write-Token") ?? "";
  // A page adopts exactly its first verified startup token. Later responses
  // never silently renew stale drafts or queued actions after removal/restart.
  if (path === "/project-purge-intent" && method === "GET" && pageWriteToken === null &&
      /^[0-9a-f]{32}-(0|[1-9][0-9]{0,19})$/.test(writeToken)) pageWriteToken = writeToken;
  return {
    data: envelope.data,
    requestId: envelope.request_id ?? "",
    schemaVersion: envelope.schema_version,
    etag: response.headers.get("ETag") ?? "",
    writeToken,
  };
}

export async function apiRequest(path, options = {}) {
  checkSignal(options.signal);
  checkWrite(path, options);
  const send = () => withSignal(options.signal, () => sendJson(path, options));
  return !["GET", "HEAD", "OPTIONS"].includes((options.method ?? "GET").toUpperCase())
    ? trackWrite(send) : send();
}

async function sendJson(path, options) {
  const method = options.method ?? "GET";
  const headers = new Headers({ Accept: "application/json" });
  if (options.body !== undefined) headers.set("Content-Type", "application/json");
  if (options.ifMatch) headers.set("If-Match", options.ifMatch);
  if (pageWriteToken && !["GET", "HEAD", "OPTIONS"].includes(method.toUpperCase()))
    headers.set("X-Mdo-Write-Token", pageWriteToken);

  let response;
  try {
    response = await targetFetch(apiUrl(path), {
      method,
      headers,
      body: options.body === undefined ? undefined : JSON.stringify(options.body),
      cache: "no-store",
      credentials: "same-origin",
      keepalive: options.keepalive ?? false,
      signal: options.signal,
    });
  } catch (error) {
    checkSignal(options.signal);
    if (error?.name === "AbortError") throw error;
    if (error?.code) throw new ApiError(error.message, error);
    networkErrorHandler?.();
    throw new ApiError("无法连接本地 mdo 服务", { code: "network_error" });
  }

  return readEnvelope(response, path, method, options.signal);
}

async function uploadImage(projectId, sessionId, file, mime = file.type, options = {}) {
  if (options.uploadId && !/^[0-9a-f]{32}$/.test(options.uploadId))
    throw new TypeError("Invalid image upload identity");
  const path = `/projects/${resourceId(projectId, "project")}` +
    `/sessions/${resourceId(sessionId, "session")}/attachments` +
    (options.uploadId ? `/${options.uploadId}` : "");
  const method = options.uploadId ? "PUT" : "POST";
  checkSignal(options.signal);
  checkWrite(path, { method });
  return trackWrite(() => withSignal(options.signal,
    () => sendImage(path, file, mime, method, options.signal)));
}

async function sendImage(path, file, mime, method, signal) {
  const url = apiUrl(path);
  const headers = { Accept: "application/json", "Content-Type": mime,
    ...(pageWriteToken ? { "X-Mdo-Write-Token": pageWriteToken } : {}) };
  if (file.name) {
    const name = attachmentFileName(file.name);
    if (!name) throw new ApiError("Image filename is invalid", { code: "image_name_invalid" });
    headers["X-Mdo-File-Name"] = encodeURIComponent(name);
  }
  let response;
  try {
    response = await targetFetch(url, {
      method,
      headers,
      body: file,
      cache: "no-store",
      credentials: "same-origin",
      signal,
    });
  } catch (error) {
    checkSignal(signal);
    if (error?.name === "AbortError") throw error;
    if (error?.code) throw new ApiError(error.message, error);
    networkErrorHandler?.();
    throw new ApiError("无法连接本地 mdo 服务", { code: "network_error" });
  }
  return (await readEnvelope(response, path, method, signal)).data;
}

function deleteImage(projectId, sessionId, id) {
  if (!/^[0-9a-f]{32}$/.test(id))
    throw new TypeError("Attachment ID is invalid");
  return apiRequest(`/projects/${resourceId(projectId, "project")}` +
    `/sessions/${resourceId(sessionId, "session")}/attachments/${id}`,
    { method: "DELETE" });
}

function markImageDiscard(projectId, sessionId, id) {
  if (!/^[0-9a-f]{32}$/.test(id))
    throw new TypeError("Attachment ID is invalid");
  return apiRequest(`/projects/${resourceId(projectId, "project")}` +
    `/sessions/${resourceId(sessionId, "session")}/queue/discard-images/${id}`,
    { method: "POST" });
}

export const api = Object.freeze({
  get: (path, options = {}) => apiRequest(path, options),
  post: (path, body, options = {}) => apiRequest(path, { ...options, method: "POST", body }),
  put: (path, body, options = {}) => apiRequest(path, { ...options, method: "PUT", body }),
  patch: (path, body, options = {}) => apiRequest(path, { ...options, method: "PATCH", body }),
  delete: (path, options = {}) => apiRequest(path, { ...options, method: "DELETE" }),
  uploadImage,
  deleteImage,
  markImageDiscard,
});

// Dedicated bounded binary transport, sharing the page's write token, purge
// admission and pending-write tracking. Never JSON stringify a file chunk.
export async function uploadBackupChunk(id, offset, chunk, options = {}) {
  if (!/^[0-9a-f]{32}$/.test(id) || !Number.isSafeInteger(offset) || offset < 0 ||
      !(chunk instanceof Uint8Array) || !chunk.byteLength || chunk.byteLength > 262144)
    throw new TypeError("Invalid backup chunk");
  const path = `/session-backups/uploads/${id}/chunks/${offset}`;
  checkSignal(options.signal);
  checkWrite(path, { method: "PUT" });
  return trackWrite(() => withSignal(options.signal, async () => {
    let response;
    try {
      response = await targetFetch(apiUrl(path), { method: "PUT", body: chunk,
        headers: { Accept: "application/json", "Content-Type": "application/octet-stream",
          ...(pageWriteToken ? { "X-Mdo-Write-Token": pageWriteToken } : {}) },
        cache: "no-store", credentials: "same-origin", redirect: "error", signal: options.signal });
    } catch (error) {
      checkSignal(options.signal);
      if (error?.name === "AbortError") throw error;
      if (error?.code) throw new ApiError(error.message, error);
      networkErrorHandler?.();
      throw new ApiError("Cannot upload backup chunk", { code: "network_error" });
    }
    return readEnvelope(response, path, "PUT", options.signal);
  }));
}
