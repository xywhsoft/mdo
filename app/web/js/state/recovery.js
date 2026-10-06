import { ApiError, api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { withSessionRuntime } from "./session-runtime.js";

const EMPTY = Object.freeze({ resume_required: false, total: 0, items: [] });
let selection = Object.freeze({ projectId: "", sessionId: "" });
let inspectionSequence = 0;

export const recoveryStore = createResourceStore(EMPTY);

export function selectRecovery(projectId, sessionId) {
  const next = projectId && sessionId
    ? { projectId: resourceId(projectId, "project"), sessionId: resourceId(sessionId, "session") }
    : { projectId: "", sessionId: "" };
  if ( next.projectId === selection.projectId && next.sessionId === selection.sessionId ) return;
  selection = Object.freeze(next);
  recoveryStore.reset(EMPTY);
}

export function readRecovery(projectId, sessionId) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  return withSessionRuntime(project, session, async () => {
    const data = (await api.get(`/projects/${project}/sessions/${session}/recovery`)).data;
    // Only a fresh successful inspection proves the previous runtime exited.
    // Cached snapshots and failed/busy reads must not unlock a submitted retry.
    return { ...data, inspection_id: ++inspectionSequence };
  });
}

export function loadRecovery() {
  if (!selection.sessionId) {
    recoveryStore.reset(EMPTY);
    return Promise.resolve(recoveryStore.get());
  }
  const current = selection;
  return recoveryStore.load(async () => {
    try {
      return await readRecovery(current.projectId, current.sessionId);
    } catch (error) {
      if (error instanceof ApiError && ["session_busy", "session_state_conflict", "recovery_state_conflict"].includes(error.code)) {
        return { ...EMPTY, unavailable: true };
      }
      throw error;
    }
  });
}

export async function resumeRecovery(data, choices) {
  const project = resourceId(data?.project_id, "project");
  const session = resourceId(data?.session_id, "session");
  const token = String(data?.recovery_token ?? "");
  if (!/^[0-9a-f]{64}$/.test(token)) throw new TypeError("recovery token is invalid");
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
  return withSessionRuntime(project, session, async () =>
    (await api.post(`/projects/${project}/sessions/${session}/resume`, {
      recovery_token: token,
      decisions,
    })).data);
}

export async function abandonRecovery(data) {
  const project = resourceId(data?.project_id, "project");
  const session = resourceId(data?.session_id, "session");
  const revision = Number(data?.revision);
  const lastSequence = Number(data?.last_sequence);
  if (!Number.isSafeInteger(revision) || revision < 1 ||
      !Number.isSafeInteger(lastSequence) || lastSequence < 1 ||
      data?.resume_required !== true)
    throw new TypeError("only an unchanged interrupted response can be ended");
  return withSessionRuntime(project, session, async () =>
    (await api.post(`/projects/${project}/sessions/${session}/abandon`, {
      revision, last_sequence: lastSequence,
    })).data);
}
