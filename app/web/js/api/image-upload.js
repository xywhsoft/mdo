import { api, ApiError, resourceId } from "./client.js";
import { isTransientReadError } from "./read-recovery.js";
import { createRequestRecovery } from "./request-recovery.js";

function newId() {
  return Array.from(crypto.getRandomValues(new Uint8Array(16)),
    byte => byte.toString(16).padStart(2, "0")).join("");
}

// This PUT identity binds one file to one session. After an uncertain response
// inspect that same identity before retrying; never replay an unkeyed POST.
export async function uploadImageWithRecovery(projectId, sessionId, file, mime,
  { canContinue = () => true, id = newId(), now = Date.now, random = Math.random,
    setTimer = setTimeout, clearTimer = clearTimeout, eventTarget = globalThis.window,
    upload = (signal) => api.uploadImage(projectId, sessionId, file, mime,
      { uploadId: id, signal }),
    read = async (signal) => (await api.get(
      `/projects/${resourceId(projectId, "project")}/sessions/${resourceId(sessionId, "session")}` +
      `/attachments/${id}/info`, { signal })).data,
  } = {}) {
  if (!/^[0-9a-f]{32}$/.test(id)) throw new TypeError("Invalid image upload identity");
  const deadline = now() + 90000;
  const recovery = createRequestRecovery({ now, random, setTimer, clearTimer, eventTarget,
    requestMs: 30000, recoveryMs: 90000,
    timeoutError: () => new ApiError("Image transfer timed out", { code: "image_upload_timeout" }) });
  let inspect = false, lastError;
  const active = () => {
    recovery.assertActive();
    if (!canContinue()) throw new DOMException("Image editor changed", "AbortError");
  };
  const matches = data => data?.id === id && data.size === file.size &&
    data.mime_type === mime && (data.file_name ?? "") === (file.name ?? "");
  // Each attempt owns its wait even if transport/body ignores abort. The
  // image-specific loop still confirms this ID before any subsequent PUT.
  const bounded = operation => recovery.request(operation, { retry: false });
  try {
    for (let attempt = 0; attempt < 6 && now() < deadline; ++attempt) {
      active();
      try {
        if (inspect) {
          try {
            const stored = await bounded(read);
            if (!matches(stored)) throw new ApiError("Image acknowledgement differs",
              { code: "image_upload_conflict" });
            return stored;
          } catch (error) {
            if (error?.code !== "attachment_not_found" || error.status !== 404) throw error;
            inspect = false;
          }
        }
        active();
        const stored = await bounded(upload);
        if (!matches(stored)) throw new ApiError("Image acknowledgement differs",
          { code: "image_upload_conflict" });
        return stored;
      } catch (error) {
        if (error.status === 405) throw new ApiError("Update the target device to upload images",
          { code: "image_upload_unsupported" });
        if (!isTransientReadError(error) &&
            !["image_upload_timeout", "invalid_response", "remote_result_unconfirmed"].includes(error?.code))
          throw error;
        lastError = error; inspect = true;
        if (attempt === 5) break;
        const delay = Math.min(1000 * 2 ** attempt, 15000);
        const until = Math.min(deadline, now() + delay + Math.floor(delay * .2 * random()));
        while (now() < until) {
          active();
          await new Promise(resolve => setTimer(resolve, Math.min(100, until - now())));
        }
      }
    }
    active();
    throw Object.assign(new ApiError("The image could not be confirmed after recovery",
      { code: "image_upload_unavailable" }), { cause: lastError });
  } finally { recovery.dispose(); }
}
