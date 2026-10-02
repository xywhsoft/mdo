import { ApiError, apiUrl, resourceId } from "./client.js";
import { createSha256 } from "../utils/sha256.js";

export const BACKUP_MAX_BYTES = 96 * 1024 * 1024;
const failure = (code, message) => new ApiError(message, { code });

// No blob()/JSON materialization before enforcing transfer bounds. Retained
// chunks are copied, hashed as received, and released on every failure/cancel.
export async function downloadSessionBackup(projectId, sessionId, {
  signal, onProgress = () => {}, maxBytes = BACKUP_MAX_BYTES, timeoutMs = 30000,
} = {}) {
  if (!Number.isSafeInteger(maxBytes) || maxBytes < 1 || maxBytes > BACKUP_MAX_BYTES ||
      !Number.isFinite(timeoutMs) || timeoutMs <= 0 || timeoutMs > 30000)
    throw new TypeError("Invalid backup download limits");
  const project = resourceId(projectId, "project"), session = resourceId(sessionId, "session");
  const controller = new AbortController();
  let timedOut = false, reader = null, response = null, complete = false;
  const cancel = () => controller.abort();
  signal?.addEventListener("abort", cancel, { once: true });
  if (signal?.aborted) cancel();
  const timer = setTimeout(() => { timedOut = true; cancel(); }, timeoutMs);
  const check = () => {
    if (controller.signal.aborted) throw new DOMException("Backup download cancelled", "AbortError");
  };
  try {
    check(); onProgress({ phase: "preparing", received: 0, total: 0 });
    response = await fetch(apiUrl(`/projects/${project}/sessions/${session}/backup`), {
      cache: "no-store", credentials: "same-origin", redirect: "error", signal: controller.signal,
    });
    check();
    if (!response.ok) {
      // Error envelopes are also bounded; do not trust an arbitrary json() body.
      const text = await readError(response, controller.signal);
      let envelope = null;
      try { envelope = JSON.parse(text); } catch { /* preserve status below */ }
      throw new ApiError(envelope?.error?.message || `Backup download failed (${response.status})`, {
        status: response.status, code: envelope?.error?.code ?? "backup_download_failed",
        requestId: envelope?.request_id,
      });
    }
    const length = response.headers.get("Content-Length") ?? "";
    const hash = /^"mdo-backup-sha256-([0-9a-f]{64})"$/.exec(response.headers.get("ETag") ?? "");
    if (response.status !== 200 || !/^[1-9][0-9]*$/.test(length) ||
        !Number.isSafeInteger(Number(length)) || Number(length) > maxBytes ||
        (response.headers.get("Content-Type") ?? "").split(";")[0].trim() !== "application/json" ||
        !hash || !response.body) throw failure("backup_download_invalid", "Invalid backup response headers");
    const total = Number(length), chunks = [], digest = createSha256();
    let received = 0, workBytes = 0;
    reader = response.body.getReader();
    onProgress({ phase: "receiving", received, total });
    for (;;) {
      check();
      const { value, done } = await reader.read();
      check();
      if (done) break;
      if (!(value instanceof Uint8Array) || value.length === 0 || value.length > total - received)
        throw failure("backup_download_invalid", "Backup response exceeds its declared length");
      const owned = value.slice();
      for (let offset = 0; offset < owned.length; offset += 65536) {
        const part = owned.subarray(offset, offset + 65536);
        digest.update(part); workBytes += part.length;
        if (workBytes >= 262144) {
          workBytes = 0;
          await new Promise((resolve) => setTimeout(resolve, 0)); check();
        }
      }
      chunks.push(owned); received += owned.length;
      onProgress({ phase: "receiving", received, total });
    }
    if (received !== total) throw failure("backup_download_invalid", "Backup response is incomplete");
    onProgress({ phase: "verifying", received, total });
    if (digest.hex() !== hash[1]) throw failure("backup_download_checksum", "Backup response checksum mismatch");
    check();
    const match = /filename="([A-Za-z0-9._-]+)"/.exec(response.headers.get("Content-Disposition") ?? "");
    const blob = new Blob(chunks, { type: "application/json" });
    complete = true;
    return { blob, filename: match?.[1] ?? `mdo-session-${session}.backup.json`, sha256: hash[1], bytes: total };
  } catch (error) {
    if (timedOut) throw failure("backup_download_timeout", "Backup download deadline exceeded");
    if (controller.signal.aborted || error?.name === "AbortError")
      throw new DOMException("Backup download cancelled", "AbortError");
    if (error instanceof ApiError) throw error;
    throw failure("network_error", "Cannot read backup from the local mdo service");
  } finally {
    clearTimeout(timer); signal?.removeEventListener("abort", cancel);
    if (!complete) controller.abort();
    if (reader) { if (!complete) await reader.cancel().catch(() => {}); reader.releaseLock(); }
    else await response?.body?.cancel().catch(() => {});
  }
}

async function readError(response, signal) {
  if (!response.body) return "";
  const reader = response.body.getReader(), parts = [];
  let bytes = 0;
  try {
    for (;;) {
      if (signal.aborted) throw new DOMException("Cancelled", "AbortError");
      const { value, done } = await reader.read();
      if (done) return new TextDecoder().decode(concat(parts, bytes));
      if (value.length > 16384 - bytes) return "";
      parts.push(value.slice()); bytes += value.length;
    }
  } finally { await reader.cancel().catch(() => {}); reader.releaseLock(); }
}

function concat(parts, bytes) {
  const result = new Uint8Array(bytes);
  let offset = 0;
  for (const part of parts) { result.set(part, offset); offset += part.length; }
  return result;
}
