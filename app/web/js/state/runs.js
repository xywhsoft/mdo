import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { withSessionRuntime } from "./session-runtime.js";
import { isTransientReadError } from "../api/read-recovery.js";

export const runsStore = createResourceStore({ active_runs: 0, items: [] },
  { recoverRead: isTransientReadError, retainDataOnError: isTransientReadError });

export function loadRuns({ retry = false } = {}) {
  if (!retry && runsStore.isPending()) return Promise.resolve(runsStore.get());
  return runsStore.load(async signal => (await api.get("/runs", { signal })).data,
    { background: !retry });
}

export async function startRun(projectId, sessionId, prompt, attachments = [],
  queueItemId = "") {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  const body = attachments.length ? { prompt, attachments } : { prompt };
  if (queueItemId) body.queue_item_id = queueItemId;
  try {
    return await withSessionRuntime(project, session, async () =>
      (await api.post(`/projects/${project}/sessions/${session}/runs`,
        body)).data);
  } catch (error) {
    // The server may have started the run before its response was lost.
    if (["network_error", "invalid_response", "run_result_unavailable",
      "run_receipt_unavailable", "queue_run_starting",
      "run_start_uncertain"]
      .includes(error?.code)) error.runAdmissionUncertain = true;
    throw error;
  }
}

export async function readRun(runId) {
  const run = resourceId(runId, "run");
  return (await api.get(`/runs/${run}`)).data;
}

export async function cancelRun(runId) {
  const run = resourceId(runId, "run");
  return (await api.delete(`/runs/${run}`)).data;
}
