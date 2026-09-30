import { ApiError } from "../../api/client.js";

export const EMPTY_PURGE_ETAG = '"mdo-purge-intent-empty"';
const bindingFields = ["purge_request_id", "project_id", "revision", "created_at"];

export function invalidPurge() {
  return new ApiError("The saved purge request could not be verified", { code: "purge_intent_unavailable" });
}

function validBinding(value) {
  return value && typeof value.purge_request_id === "string" &&
    typeof value.project_id === "string" && /^[0-9a-f]{32}$/.test(value.purge_request_id) &&
    /^[A-Za-z0-9_-][A-Za-z0-9_.-]{0,63}$/.test(value.project_id) &&
    Number.isSafeInteger(value.revision) && value.revision > 0 &&
    Number.isSafeInteger(value.created_at) && value.created_at > 0;
}

export function purgeBindingsMatch(a, b) { return bindingFields.every((field) => a?.[field] === b?.[field]); }
export function purgeProjectEtag(intent) { return `"mdo-project-${intent.project_id}-${intent.revision}"`; }

export function readPurgeIntent(response) {
  const value = response?.data?.intent;
  if (value === null && response.etag === EMPTY_PURGE_ETAG) return null;
  if (!validBinding(value) || Object.keys(value).length !== 5 ||
      typeof value.name !== "string" || !value.name ||
      new TextEncoder().encode(value.name).length > 256 || value.name.includes("\0") ||
      response.etag !== `"mdo-purge-intent-${value.purge_request_id}"`) throw invalidPurge();
  return Object.freeze({ ...value });
}

export function readPurgeResult(value, intent) {
  if (!purgeBindingsMatch(value, intent) || !validBinding(value) ||
      typeof value.committed !== "boolean" || typeof value.restart_required !== "boolean" ||
      !["pending", "committed", "aborted", "not_accepted", "unknown"].includes(value.outcome) ||
      (["pending", "committed", "aborted"].includes(value.outcome) && value.accepted !== true) ||
      (value.outcome === "committed" && !value.committed) || (value.outcome === "aborted" && value.committed) ||
      (value.outcome === "not_accepted" && (value.accepted !== false || value.committed)) ||
      (value.outcome === "unknown" && value.accepted !== null) ||
      value.workspace_files_removed !== false || value.shared_records_retained !== true) throw invalidPurge();
  for (const field of ["target_count", "file_count", "directory_count", "total_bytes", "schedule_count"])
    if (!Number.isSafeInteger(value[field]) || value[field] < 0) throw invalidPurge();
  return Object.freeze({ ...value });
}

// Preview is advisory. Validate its binding and inventory before offering a
// confirmation; the server repeats ownership and inventory checks at execution.
export function reviewedPurgeIntent(preview, id) {
  const intent = { purge_request_id: id, project_id: preview?.id, revision: preview?.revision,
    created_at: preview?.created_at, name: preview?.name };
  readPurgeIntent({ data: { intent }, etag: `"mdo-purge-intent-${id}"` });
  if (preview.etag !== purgeProjectEtag(intent) || preview.advisory !== true ||
      preview.workspace_files_removed !== false || preview.shared_records_retained !== true ||
      !Array.isArray(preview.targets) || preview.targets.length !== preview.target_count || !preview.targets.length)
    throw invalidPurge();
  for (const field of ["target_count", "file_count", "directory_count", "total_bytes", "schedule_count"])
    if (!Number.isSafeInteger(preview[field]) || preview[field] < 0) throw invalidPurge();
  for (const target of preview.targets)
    if (!["file", "directory"].includes(target?.type) || typeof target.path !== "string" ||
        !target.path || /[\0\\:]/.test(target.path) || target.path.split("/").some(part => !part || [".", ".."].includes(part)))
      throw invalidPurge();
  for (const field of ["session_runtime_count", "active_interactive_run_count", "active_scheduled_run_count_global",
    "session_diagnostic_count", "session_catalog_diagnostic_count_global", "schedule_catalog_diagnostic_count_global"]) {
    if (!Number.isSafeInteger(preview[field]) || preview[field] < 0) throw invalidPurge();
    if (preview[field]) throw new ApiError("Resolve active runs and diagnostics before removal", { code: "purge_client_busy" });
  }
  return Object.freeze(intent);
}

export function newPurgeRequestId() {
  return Array.from(crypto.getRandomValues(new Uint8Array(16)), byte => byte.toString(16).padStart(2, "0")).join("");
}
