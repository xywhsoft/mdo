import { t as uiErrorText } from "../i18n.js";
import { ApiError, api, currentPageWriteToken, resourceId } from "../api/client.js";
import { createRequestRecovery, clientActionId } from "../api/request-recovery.js";
import { isTransientReadError } from "../api/read-recovery.js";
import { targetState } from "../api/target.js";

const stamp = () => JSON.stringify([targetState().selected, targetState().runtimeChanged,
  currentPageWriteToken()]);
const fenced = error => ["home_restart_required", "write_token_required", "write_token_invalid",
  "write_token_conflict", "remote_runtime_changed"].includes(error?.code);
const transient = error => !fenced(error) && (isTransientReadError(error) ||
  ["invalid_response", "remote_result_unconfirmed", "session_result_unavailable"].includes(error?.code));
const pending = () => new ApiError(uiErrorText("error.detail.forkConfirmationIsPending", {}, "Fork confirmation is pending"), { code: "session_fork_pending", status: 425 });
const aborted = () => new DOMException("Fork cancelled", "AbortError");

// A reserved child ID makes repeated POSTs safe. After uncertain delivery,
// inspect that child first. A manual retry in this page keeps the same intent;
// a confirmed fork releases it so the next explicit fork can create a new one.
export function createSessionForker({ newId = clientActionId, epoch = stamp,
  token = currentPageWriteToken, createRecovery = createRequestRecovery,
  send = (path, body, options) => api.post(path, body, options),
  inspect = (path, options) => api.get(path, options) } = {}) {
  const intents = new Map();
  let generation;
  return async function fork(session, input, { signal } = {}) {
    if (signal?.aborted) throw aborted();
    const project = resourceId(session.project_id), source = resourceId(session.id);
    const revision = Number(session.revision), through = Number(input.through_sequence);
    const tag = `"mdo-session-${source}-${revision}"`;
    if (!Number.isSafeInteger(revision) || revision < 1 ||
        (session.etag && session.etag !== tag) || !Number.isSafeInteger(through) || through < 0 ||
        (input.title !== undefined && typeof input.title !== "string"))
      throw new TypeError(uiErrorText("error.detail.invalidForkBoundary", {}, "Invalid fork boundary"));
    const original = epoch(), writeToken = token();
    if (generation !== original) { intents.clear(); generation = original; }
    const key = JSON.stringify([project, source, tag, through, input.title || ""]);
    let intent = intents.get(key);
    if (!intent) {
      if (intents.size >= 64) throw new ApiError(uiErrorText("error.detail.tooManyUnconfirmedForks", {}, "Too many unconfirmed forks"), { code: "session_fork_pending_limit" });
      const id = newId();
      if (!/^[0-9a-f]{32}$/.test(id)) throw new TypeError(uiErrorText("error.detail.invalidForkIdentity", {}, "Invalid fork identity"));
      intent = { body: Object.freeze({ through_sequence: through, client_session_id: id,
        ...(input.title ? { title: input.title } : {}) }), uncertain: false, confirm: false };
      intents.set(key, intent);
    }
    const recovery = createRecovery(), cancel = () => recovery.dispose();
    signal?.addEventListener("abort", cancel, { once: true });
    function assertOwner() {
      recovery.assertActive();
      if (signal?.aborted || epoch() !== original) throw aborted();
    }
    function verified(reply) {
      assertOwner();
      if (writeToken && reply?.writeToken !== writeToken)
        throw new ApiError(uiErrorText("error.detail.forkServiceRestarted", {}, "Fork service restarted"), { code: "write_token_conflict" });
      const data = reply?.data, id = intent.body.client_session_id;
      if (data?.id !== id || data.project_id !== project || data.parent_session_id !== source ||
          data.forked_through_sequence !== through || !Number.isSafeInteger(data.revision) ||
          data.revision < 1 || reply.etag !== `"mdo-session-${id}-${data.revision}"`)
        throw new ApiError(uiErrorText("error.detail.invalidForkAcknowledgement", {}, "Invalid fork acknowledgement"), { code: "invalid_response" });
      if (data.status !== "active")
        throw new ApiError(uiErrorText("error.detail.theForkIsNoLongerActive", {}, "The fork is no longer active"), { code: "session_fork_inactive" });
      return { ...data, etag: reply.etag };
    }
    try {
      const result = await recovery.request(async attemptSignal => {
        assertOwner();
        if (intent.confirm) {
          let reply;
          try { reply = await inspect(`/projects/${project}/sessions/${intent.body.client_session_id}`, { signal: attemptSignal }); }
          catch (error) {
            assertOwner();
            if (!attemptSignal.aborted && error?.status === 404 && error.code === "session_not_found") {
              intent.confirm = false; throw pending();
            }
            throw error;
          }
          return verified(reply);
        }
        const wasUncertain = intent.uncertain;
        // Mark before awaiting: the deadline may finish before fetch settles.
        intent.uncertain = true; intent.confirm = true;
        try { return verified(await send(`/projects/${project}/sessions/${source}/fork`,
          intent.body, { ifMatch: tag, signal: attemptSignal })); }
        catch (error) {
          assertOwner();
          if (attemptSignal.aborted || fenced(error)) throw error;
          if (error?.code === "session_service_unavailable" && !wasUncertain) {
            intent.uncertain = false; intent.confirm = false;
          } else if (!transient(error) && wasUncertain && error?.name !== "AbortError") {
            throw pending();
          } else if (!transient(error) && !wasUncertain && error?.name !== "AbortError") {
            intent.uncertain = false;
          }
          throw error;
        }
      }, { retryable: transient });
      intents.delete(key); return result;
    } catch (error) {
      if (!intent.uncertain) intents.delete(key);
      if (transient(error)) {
        const final = new ApiError(uiErrorText("error.detail.forkCreationCouldNotBeConfirmed", {}, "Fork creation could not be confirmed"), {
          code: intent.uncertain ? "session_fork_unconfirmed" : "session_fork_unavailable" });
        final.cause = error; throw final;
      }
      throw error;
    } finally {
      signal?.removeEventListener("abort", cancel); recovery.dispose();
    }
  };
}

export const runSessionFork = createSessionForker();
