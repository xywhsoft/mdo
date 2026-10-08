import { ApiError, api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { withSessionRuntime } from "./session-runtime.js";
import { createRequestRecovery } from "../api/request-recovery.js";
import { isTransientReadError } from "../api/read-recovery.js";

const EMPTY = Object.freeze({ resume_required: false, total: 0, items: [] });
let selection = Object.freeze({ projectId: "", sessionId: "" });
let inspectionSequence = 0;

export const recoveryStore = createResourceStore(EMPTY,
  { recoverRead: isTransientReadError, retainDataOnError: isTransientReadError });

export function selectRecovery(projectId, sessionId) {
  const next = projectId && sessionId
    ? { projectId: resourceId(projectId, "project"), sessionId: resourceId(sessionId, "session") }
    : { projectId: "", sessionId: "" };
  if ( next.projectId === selection.projectId && next.sessionId === selection.sessionId ) return;
  selection = Object.freeze(next);
  recoveryStore.reset(EMPTY);
}

export async function readRecovery(projectId, sessionId, { signal,
  createRecovery = createRequestRecovery } = {}) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  const inspect = signal => withSessionRuntime(project, session, async () => {
    const data = (await api.get(`/projects/${project}/sessions/${session}/recovery`, { signal })).data;
    if (data?.project_id !== project || data.session_id !== session ||
        typeof data.resume_required !== "boolean" || !Array.isArray(data.items) ||
        !Number.isSafeInteger(data.revision) || data.revision < 1 ||
        !Number.isSafeInteger(data.last_sequence) || data.last_sequence < 0 ||
        !Number.isSafeInteger(data.total) || data.total !== data.items.length)
      throw new ApiError("Invalid runtime inspection response", { code: "invalid_response" });
    // Only a fresh successful inspection proves the previous runtime exited.
    // Cached snapshots and failed/busy reads must not unlock a submitted retry.
    return { ...data, inspection_id: ++inspectionSequence };
  }, { signal });
  // A resource-store caller supplies its own attempt deadline and backoff.
  // Foreground send inspection includes waiting for this session's lock in
  // the same bounded read budget, before any new run is submitted.
  if (signal) return inspect(signal);
  const recovery = createRecovery();
  try { return await recovery.request(inspect); }
  finally { recovery.dispose(); }
}

export function loadRecovery({ retry = false } = {}) {
  if (!selection.sessionId) {
    recoveryStore.reset(EMPTY);
    return Promise.resolve(recoveryStore.get());
  }
  if (!retry && recoveryStore.isPending()) return Promise.resolve(recoveryStore.get());
  const current = selection;
  return recoveryStore.load(async signal => {
    try {
      return await readRecovery(current.projectId, current.sessionId, { signal });
    } catch (error) {
      if (error instanceof ApiError && ["session_busy", "session_state_conflict", "recovery_state_conflict"].includes(error.code)) {
        return { ...EMPTY, unavailable: true };
      }
      throw error;
    }
  }, { background: !retry });
}

export async function resumeRecovery(data, choices, options = {}) {
  const project = resourceId(data?.project_id, "project");
  const session = resourceId(data?.session_id, "session");
  const token = String(data?.recovery_token ?? "");
  if (!/^[0-9a-f]{64}$/.test(token)) throw new TypeError("recovery token is invalid");
  if (options.clientResumeId && !/^[0-9a-f]{32}$/.test(options.clientResumeId))
    throw new TypeError("resume correlation ID is invalid");
  const decisions = (data?.items ?? []).map((item) => {
    const action = choices.get(String(item.tool_call_id));
    if (!new Set(["retry", "record_uncertain"]).has(action)) {
      throw new TypeError("every pending call needs a recovery decision");
    }
    if (action === "retry" && !item.tool_available) {
      throw new TypeError("an unavailable tool cannot be retried");
    }
    return { tool_call_id: String(item.tool_call_id), action };
  });
  return withSessionRuntime(project, session, async () => {
    if (options.signal?.aborted) throw new DOMException("Recovery cancelled", "AbortError");
    return (await api.post(`/projects/${project}/sessions/${session}/resume`, {
      recovery_token: token,
      decisions,
      ...(options.clientResumeId ? { client_resume_id: options.clientResumeId } : {}),
    }, { signal: options.signal })).data;
  }, { signal: options.signal });
}

export async function abandonRecovery(data, options = {}) {
  const project = resourceId(data?.project_id, "project");
  const session = resourceId(data?.session_id, "session");
  const revision = Number(data?.revision);
  const lastSequence = Number(data?.last_sequence);
  if (!Number.isSafeInteger(revision) || revision < 1 ||
      !Number.isSafeInteger(lastSequence) || lastSequence < 1 ||
      data?.resume_required !== true)
    throw new TypeError("only an unchanged interrupted response can be ended");
  return withSessionRuntime(project, session, async () => {
    if (options.signal?.aborted) throw new DOMException("Recovery cancelled", "AbortError");
    return (await api.post(`/projects/${project}/sessions/${session}/abandon`, {
      revision, last_sequence: lastSequence,
    }, { signal: options.signal })).data;
  }, { signal: options.signal });
}
