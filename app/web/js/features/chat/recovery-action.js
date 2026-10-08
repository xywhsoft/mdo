import { ApiError, api, currentPageWriteToken, resourceId } from "../../api/client.js";
import { isTransientReadError } from "../../api/read-recovery.js";
import { createRequestRecovery, clientActionId } from "../../api/request-recovery.js";
import { targetState } from "../../api/target.js";
import { resumeRecovery, abandonRecovery } from "../../state/recovery.js";
import { withSessionRuntime } from "../../state/session-runtime.js";

const uncertain = error => isTransientReadError(error) ||
  ["invalid_response", "remote_result_unconfirmed", "run_result_unavailable",
    "recovery_result_unavailable"].includes(error?.code);
const stamp = () => JSON.stringify([targetState().selected, targetState().runtimeChanged,
  currentPageWriteToken()]);
const pending = () => new ApiError("Recovery acknowledgement is pending", {
  code: "recovery_confirmation_pending", status: 425,
});
function resumed(run, owner, id) {
  return run?.resume === true && run.client_resume_id === id &&
    run.project_id === owner.project_id && run.session_id === owner.session_id &&
    typeof run.id === "string" && /^[A-Za-z0-9_-][A-Za-z0-9._-]*$/.test(run.id) &&
    typeof run.terminal === "boolean";
}
function closed(data, owner, inspection = false) {
  return data?.resume_required === false &&
    (!inspection || (data.project_id === owner.project_id && data.session_id === owner.session_id)) &&
    Number.isSafeInteger(data.revision) && data.revision > owner.revision &&
    Number.isSafeInteger(data.last_sequence) && data.last_sequence > owner.last_sequence;
}

// One POST per explicit click. Lost acknowledgements are confirmed only by
// reads: a resume has an exact client ID, and ending requires a newer, closed
// recovery boundary. Neither path repeats model/tool execution or the close.
export async function runRecoveryAction({ kind, data, choices,
  id = clientActionId(), submitResume = resumeRecovery, submitAbandon = abandonRecovery,
  inspectRuns = options => api.get("/runs", options),
  inspectRecovery = (owner, options) => withSessionRuntime(owner.project_id, owner.session_id,
    () => api.get(`/projects/${resourceId(owner.project_id)}/sessions/${resourceId(owner.session_id)}/recovery`, options)),
  epoch = stamp, token = currentPageWriteToken(), recoveryOptions } = {}) {
  if (!["resume", "abandon"].includes(kind)) throw new TypeError("Unknown recovery action");
  const recovery = createRequestRecovery(recoveryOptions), original = epoch();
  let submitted = false, confirming = false;
  function assertOwner() {
    recovery.assertActive();
    if (epoch() !== original) throw new DOMException("Recovery target changed", "AbortError");
  }
  function verifyEpoch(reply) {
    assertOwner();
    if (token && reply?.writeToken !== token)
      throw new ApiError("Recovery service restarted", { code: "write_token_conflict" });
  }
  try {
    try {
      const result = await recovery.request(signal => {
        assertOwner(); submitted = true;
        return kind === "resume" ? submitResume(data, choices, { clientResumeId: id, signal }) :
          submitAbandon(data, { signal });
      }, { retry: false });
      assertOwner();
      if (kind === "resume" ? resumed(result, data, id) : closed(result, data)) return result;
      throw new ApiError("Invalid recovery acknowledgement", { code: "invalid_response" });
    } catch (error) {
      if (error?.name === "AbortError" || !uncertain(error) ||
          ["run_limit_reached", "recovery_state_conflict"].includes(error?.code)) throw error;
      confirming = true;
    }
    return await recovery.request(async signal => {
      assertOwner();
      let reply;
      try { reply = kind === "resume" ? await inspectRuns({ signal }) : await inspectRecovery(data, { signal }); }
      catch (error) { if (error?.code === "invalid_response") throw pending(); throw error; }
      verifyEpoch(reply);
      if (kind === "resume") {
        const run = reply?.data?.items?.find(item => resumed(item, data, id));
        if (run) return run;
      } else if (closed(reply?.data, data, true)) return reply.data;
      throw pending();
    });
  } catch (error) {
    // Preserve definite rejections. After a possibly accepted write, an
    // exhausted confirmation gets one accurate final message, never a replay.
    if (confirming && (uncertain(error) || error?.code === "recovery_confirmation_pending")) {
      const final = new ApiError("Recovery action acknowledgement could not be confirmed", {
        code: kind === "resume" ? "recovery_resume_unconfirmed" : "recovery_abandon_unconfirmed",
      });
      final.cause = error; final.recoveryActionUncertain = true; throw final;
    }
    if (confirming || (submitted && error?.name === "AbortError")) error.recoveryActionUncertain = true;
    throw error;
  } finally { recovery.dispose(); }
}
