import { ApiError, api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { withSessionRuntime } from "./session-runtime.js";
import { isTransientReadError } from "../api/read-recovery.js";
import { createRequestRecovery } from "../api/request-recovery.js";

export const runsStore = createResourceStore({ active_runs: 0, items: [] },
  { recoverRead: isTransientReadError, retainDataOnError: isTransientReadError });

export function loadRuns({ retry = false } = {}) {
  if (!retry && runsStore.isPending()) return Promise.resolve(runsStore.get());
  return runsStore.load(async signal => (await api.get("/runs", { signal })).data,
    { background: !retry });
}

export async function startRun(projectId, sessionId, prompt, attachments = [],
  queueItemId = "", { createRecovery = createRequestRecovery } = {}) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  const body = attachments.length ? { prompt, attachments } : { prompt };
  if (queueItemId) body.queue_item_id = queueItemId;
  const recovery = createRecovery();
  let submitted = false;
  const matches = run => /^run-[A-Za-z0-9_.-]+$/.test(run?.id ?? "") &&
    run.project_id === project && run.session_id === session && typeof run.terminal === "boolean";
  try {
    const run = await recovery.request(signal => withSessionRuntime(project, session, async () => {
      submitted = true;
      return (await api.post(`/projects/${project}/sessions/${session}/runs`, body, { signal })).data;
    }, { signal }), { retry: false });
    if (!matches(run)) throw new ApiError("Invalid run admission response", { code: "invalid_response" });
    return run;
  } catch (error) {
    // The server may have started the run before its response was lost.
    const uncertain = submitted && (["network_error", "invalid_response", "run_result_unavailable",
      "run_receipt_unavailable", "queue_run_starting",
      "run_start_uncertain", "remote_result_unconfirmed"].includes(error?.code) || error?.name === "AbortError");
    if (uncertain && /^[0-9a-f]{32}$/.test(queueItemId)) {
      // Repeat only reads. The immutable receipt binds this exact queued
      // prompt to one run; neither similar text nor the newest run is proof.
      let acceptedId = "";
      try {
        return await recovery.request(async signal => {
          if (!acceptedId) {
            let receipt;
            try { receipt = (await api.get(`/projects/${project}/sessions/${session}/queue/${queueItemId}`, { signal })).data; }
            catch (failure) {
              if (failure.code !== "queue_receipt_not_found" || failure.status !== 404) throw failure;
            }
            if (!receipt || (receipt.id === queueItemId && receipt.state === "starting"))
              throw new ApiError("The queued run outcome is not available yet", { code: "run_confirmation_pending" });
            if (receipt.id !== queueItemId || receipt.state !== "accepted" ||
                !/^run-[A-Za-z0-9_.-]+$/.test(receipt.run_id ?? ""))
              throw new ApiError("Invalid queued run receipt", { code: "invalid_response" });
            acceptedId = receipt.run_id;
          }
          const run = (await api.get(`/runs/${acceptedId}`, { signal })).data;
          if (!matches(run) || run.id !== acceptedId)
            throw new ApiError("Invalid confirmed run response", { code: "invalid_response" });
          return run;
        }, { retryable: failure => isTransientReadError(failure) || failure?.code === "run_confirmation_pending" });
      } catch (failure) { error.confirmationError = failure; }
    }
    if (uncertain) error.runAdmissionUncertain = true;
    throw error;
  } finally { recovery.dispose(); }
}

export async function readRun(runId, options = {}) {
  const run = resourceId(runId, "run");
  return (await api.get(`/runs/${run}`, options)).data;
}

export async function cancelRun(runId, options = {}) {
  const run = resourceId(runId, "run");
  return (await api.delete(`/runs/${run}`, options)).data;
}
