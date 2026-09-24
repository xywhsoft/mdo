const API_ROOT = "/api/v1";

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

function requestPath(path) {
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
  return requestPath(`/projects/${resourceId(projectId, "project")}` +
    `/sessions/${resourceId(sessionId, "session")}/attachments/${id}`);
}

async function readEnvelope(response) {
  let envelope = null;
  try { envelope = await response.json(); }
  catch {
    throw new ApiError("服务返回了无效响应", {
      status: response.status, code: "invalid_response",
    });
  }
  if (!response.ok || envelope?.ok !== true) {
    throw new ApiError(envelope?.error?.message || `请求失败 (${response.status})`, {
      status: response.status,
      code: envelope?.error?.code,
      requestId: envelope?.request_id,
      details: envelope?.error?.details,
    });
  }
  return {
    data: envelope.data,
    requestId: envelope.request_id ?? "",
    schemaVersion: envelope.schema_version,
    etag: response.headers.get("ETag") ?? "",
  };
}

export async function apiRequest(path, options = {}) {
  const method = options.method ?? "GET";
  const headers = new Headers({ Accept: "application/json" });
  if (options.body !== undefined) headers.set("Content-Type", "application/json");
  if (options.ifMatch) headers.set("If-Match", options.ifMatch);

  let response;
  try {
    response = await fetch(requestPath(path), {
      method,
      headers,
      body: options.body === undefined ? undefined : JSON.stringify(options.body),
      cache: "no-store",
      credentials: "same-origin",
      keepalive: options.keepalive ?? false,
      signal: options.signal,
    });
  } catch (error) {
    if (error?.name === "AbortError") throw error;
    throw new ApiError("无法连接本地 mdo 服务", { code: "network_error" });
  }

  return readEnvelope(response);
}

async function uploadImage(projectId, sessionId, file) {
  const url = requestPath(`/projects/${resourceId(projectId, "project")}` +
    `/sessions/${resourceId(sessionId, "session")}/attachments`);
  let response;
  try {
    response = await fetch(url, {
      method: "POST",
      headers: { Accept: "application/json", "Content-Type": file.type },
      body: file,
      cache: "no-store",
      credentials: "same-origin",
    });
  } catch {
    throw new ApiError("无法连接本地 mdo 服务", { code: "network_error" });
  }
  return (await readEnvelope(response)).data;
}

async function download(path) {
  let response;
  try {
    response = await fetch(requestPath(path), {
      cache: "no-store",
      credentials: "same-origin",
    });
  } catch {
    throw new ApiError("无法连接本地 mdo 服务", { code: "network_error" });
  }
  if (!response.ok) {
    let envelope = null;
    try { envelope = await response.json(); } catch { /* handled below */ }
    throw new ApiError(envelope?.error?.message || `下载失败 (${response.status})`, {
      status: response.status,
      code: envelope?.error?.code ?? "download_failed",
      requestId: envelope?.request_id,
    });
  }
  const disposition = response.headers.get("Content-Disposition") ?? "";
  const match = /filename="([A-Za-z0-9._-]+)"/.exec(disposition);
  return {
    blob: await response.blob(),
    filename: match?.[1] ?? "mdo-session-export.json",
  };
}

export const api = Object.freeze({
  get: (path, options = {}) => apiRequest(path, options),
  post: (path, body, options = {}) => apiRequest(path, { ...options, method: "POST", body }),
  put: (path, body, options = {}) => apiRequest(path, { ...options, method: "PUT", body }),
  patch: (path, body, options = {}) => apiRequest(path, { ...options, method: "PATCH", body }),
  delete: (path, options = {}) => apiRequest(path, { ...options, method: "DELETE" }),
  download,
  uploadImage,
});
