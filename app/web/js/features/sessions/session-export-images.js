import { api, ApiError, attachmentFileName, attachmentUrl, resourceId } from "../../api/client.js";
import { eventsToTimeline } from "../chat/timeline.js";
import { targetFetch } from "../../api/target.js";
import { createRequestRecovery } from "../../api/request-recovery.js";

const MAX_IMAGE_BYTES = 8 * 1024 * 1024;
const MAX_IMAGES = 16;
const MAX_BYTES = 32 * 1024 * 1024;

function base64(bytes) {
  const parts = [];
  for (let offset = 0; offset < bytes.length; offset += 32768)
    parts.push(String.fromCharCode(...bytes.subarray(offset, offset + 32768)));
  return btoa(parts.join(""));
}

// Do not trust blob()/arrayBuffer() to enforce the advertised response size.
// A stream overflow or short read leaves this image out of the portable file.
function cancelBody(body) {
  // Cancellation is cleanup, not another operation the export must wait for.
  try { void body?.cancel().catch(() => {}); } catch { /* Already closed/locked. */ }
}

async function readImage(response, size, mime, signal) {
  if (!response.ok) {
    cancelBody(response.body);
    throw new ApiError("Export image request failed", { status: response.status });
  }
  const type = (response.headers.get("Content-Type") ?? "").split(";")[0].trim();
  const length = response.headers.get("Content-Length");
  if (type !== mime || (length !== null &&
      (!/^\d+$/.test(length) || Number(length) !== size)) || !response.body) {
    cancelBody(response.body);
    throw new Error("Invalid export image response");
  }
  const reader = response.body.getReader();
  const bytes = new Uint8Array(size);
  let offset = 0;
  let complete = false;
  let released = false;
  function cleanup() {
    if (released) return;
    released = true;
    if (!complete) cancelBody(reader);
    try { reader.releaseLock(); } catch { /* A failed reader is still disposable. */ }
    signal.removeEventListener("abort", cleanup);
  }
  signal.addEventListener("abort", cleanup, { once: true });
  try {
    if (signal.aborted) throw new DOMException("Export cancelled", "AbortError");
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      if (value.length > size - offset) throw new Error("Export image exceeds its size");
      bytes.set(value, offset);
      offset += value.length;
    }
    if (offset !== size) throw new Error("Export image is incomplete");
    complete = true;
    return bytes;
  } catch (error) {
    if (error instanceof TypeError && !signal.aborted)
      throw new ApiError("Export image connection interrupted", { code: "network_error" });
    throw error;
  } finally {
    cleanup();
  }
}

// Export only images present in the projected transcript. Deduplicate repeated
// edit/retry references and bind every read to the original session, even when
// the user navigates away while exporting. Bounds match server upload quotas;
// smaller budgets are useful for deterministic boundary checks.
export async function loadSessionMarkdownImages(session, transcript,
  { maxImages = MAX_IMAGES, maxBytes = MAX_BYTES, timeoutMs = 30000,
    createRecovery = createRequestRecovery } = {}) {
  if (!Number.isInteger(maxImages) || maxImages < 1 || maxImages > MAX_IMAGES ||
      !Number.isInteger(maxBytes) || maxBytes < 1 || maxBytes > MAX_BYTES ||
      !Number.isFinite(timeoutMs) || timeoutMs <= 0 || timeoutMs > 30000)
    throw new TypeError("Invalid image export bounds");
  const ids = new Set(eventsToTimeline(transcript.events ?? [])
    .flatMap((item) => item.attachments ?? []));
  const images = new Map();
  if (!ids.size) return { ...transcript, images, imagesIncomplete: false };
  const owner = `/projects/${resourceId(session.project_id, "project")}` +
    `/sessions/${resourceId(session.id, "session")}`;
  // All metadata, backoff waits and binary bodies share the existing 30s cap.
  // The owner settles even if a fetch/reader ignores its cancellation signal.
  const recovery = createRecovery({ requestMs: timeoutMs, recoveryMs: timeoutMs });
  let reads = 0;
  let remaining = maxBytes;
  try {
    for (const id of ids) {
      if (reads >= maxImages) break;
      recovery.assertActive();
      reads += 1;
      try {
        const url = attachmentUrl(session.project_id, session.id, id);
        const { data } = await recovery.request(signal =>
          api.get(`${owner}/attachments/${id}/info`, { signal }));
        const name = data?.schema_version === 2 ? attachmentFileName(data.file_name) : id;
        if (data?.id !== id || ![1, 2].includes(data.schema_version) || !name ||
            !["image/png", "image/jpeg", "image/webp"].includes(data.mime_type) ||
            !Number.isInteger(data.size) || data.size < 1 ||
            data.size > MAX_IMAGE_BYTES || data.size > remaining)
          continue;
        // Reserve the budget before reading; failed downloads must not permit
        // unbounded additional transfers or allocations.
        remaining -= data.size;
        const bytes = await recovery.request(async signal => {
          let response;
          try {
            response = await targetFetch(url, { cache: "no-store",
              credentials: "same-origin", redirect: "error", signal });
          } catch (error) {
            if (error instanceof TypeError && !signal.aborted)
              throw new ApiError("Export image connection interrupted", { code: "network_error" });
            throw error;
          }
          if (signal.aborted) {
            cancelBody(response.body);
            throw new DOMException("Export cancelled", "AbortError");
          }
          return readImage(response, data.size, data.mime_type, signal);
        });
        images.set(id, { name, dataUrl: `data:${data.mime_type};base64,${base64(bytes)}` });
      } catch (error) {
        if (error?.name === "AbortError") throw error;
        // The formatter keeps the ID and an explicit incomplete notice.
      }
    }
  } finally { recovery.dispose(); }
  return { ...transcript, images, imagesIncomplete: images.size !== ids.size };
}
