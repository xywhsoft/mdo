import { api, ApiError, uploadBackupChunk } from "./client.js";
import { BACKUP_MAX_BYTES } from "./backup-download.js";
import { createSha256 } from "../utils/sha256.js";

const root = "/session-backups";
const id = (value) => {
  if (!/^[0-9a-f]{32}$/.test(value)) throw new TypeError("Invalid backup request ID");
  return value;
};
const check = (signal) => { if (signal?.aborted) throw new DOMException("Cancelled", "AbortError"); };

export function newBackupUploadId() {
  const bytes = new Uint8Array(16);
  globalThis.crypto.getRandomValues(bytes);
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

export async function uploadBackupFile(file, { signal, onProgress = () => {},
  onIdentity = () => {}, timeoutMs = 120000 } = {}) {
  if (!file || !Number.isSafeInteger(file.size) || file.size < 1 || file.size > BACKUP_MAX_BYTES)
    throw new ApiError("Backup file exceeds its byte budget", { code: "backup_upload_limit" });
  if (!Number.isFinite(timeoutMs) || timeoutMs <= 0 || timeoutMs > 120000)
    throw new TypeError("Invalid backup upload deadline");
  const controller = new AbortController(), uploadId = newBackupUploadId();
  const abort = () => controller.abort();
  signal?.addEventListener("abort", abort, { once: true });
  if (signal?.aborted) abort();
  let expired = false;
  const timer = setTimeout(() => { expired = true; abort(); }, timeoutMs);
  const active = controller.signal, hash = createSha256(), size = file.size;
  try {
    for (let offset = 0; offset < size; offset += 262144) {
      check(active);
      const bytes = new Uint8Array(await file.slice(offset, offset + 262144).arrayBuffer());
      check(active); hash.update(bytes);
      onProgress({ phase: "hashing", received: Math.min(size, offset + bytes.length), total: size });
      await new Promise((resolve) => setTimeout(resolve, 0));
    }
    check(active);
    const digest = hash.hex(), path = `${root}/uploads/${uploadId}`;
    onIdentity({ id: uploadId, sha256: digest, bytes: size });
    await api.post(`${root}/uploads`, { id: uploadId, bytes: size, sha256: digest }, { signal: active });
    for (let offset = 0; offset < size; offset += 262144) {
      check(active);
      const chunk = new Uint8Array(await file.slice(offset, offset + 262144).arrayBuffer());
      check(active);
      const result = (await uploadBackupChunk(uploadId, offset, chunk, { signal: active })).data;
      if (result?.received_bytes !== offset + chunk.length)
        throw new ApiError("Backup progress differs", { code: "backup_upload_conflict" });
      onProgress({ phase: "uploading", received: offset + chunk.length, total: size });
    }
    const result = (await api.post(path + "/seal", undefined, { signal: active })).data;
    check(active);
    if (result?.sha256 !== digest || result?.bytes !== size)
      throw new ApiError("Sealed backup digest differs", { code: "backup_upload_checksum" });
    return { id: uploadId, sha256: digest, bytes: size };
  } catch (error) {
    if (expired) throw new ApiError("Backup upload exceeded its deadline", { code: "backup_upload_timeout" });
    throw error;
  } finally { clearTimeout(timer); signal?.removeEventListener("abort", abort); }
}

export const backupImportApi = Object.freeze({
  upload: uploadBackupFile,
  preview: async (upload, options) => (await api.post(`${root}/uploads/${id(upload)}/preview`, undefined, options)).data,
  previews: async (options) => (await api.get(`${root}/previews`, options)).data.preview,
  readPreview: async (preview, options) => (await api.get(`${root}/previews/${id(preview)}`, options)).data,
  review: async (preview, project, options) => (await api.post(`${root}/previews/${id(preview)}/restore-review`,
    { project_id: project }, options)).data,
  current: async (options) => (await api.get(`${root}/restores`, options)).data,
  read: async (request, options) => (await api.get(`${root}/restores/${id(request)}`, options)).data,
  apply: async (request, options) => (await api.post(`${root}/restores/${id(request)}/apply`, undefined, options)).data,
  cancel: async (request, options) => (await api.delete(`${root}/restores/${id(request)}`, options)).data,
  discardPreview: async (preview, options) => (await api.delete(`${root}/previews/${id(preview)}`, options)).data,
  discardUpload: async (upload, options) => (await api.delete(`${root}/uploads/${id(upload)}`, options)).data,
});
