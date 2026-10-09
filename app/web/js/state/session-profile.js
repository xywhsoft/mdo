import { ApiError, api, currentPageWriteToken, resourceId } from "../api/client.js";
import { createRequestRecovery } from "../api/request-recovery.js";
import { isTransientReadError } from "../api/read-recovery.js";
import { targetState } from "../api/target.js";
import { withSessionRuntime } from "./session-runtime.js";

const stamp = () => JSON.stringify([targetState().selected, targetState().runtimeChanged,
  currentPageWriteToken()]);
const fenced = error => ["home_restart_required", "write_token_required", "write_token_invalid",
  "write_token_conflict", "remote_runtime_changed"].includes(error?.code);
const transient = error => !fenced(error) && (isTransientReadError(error) ||
  ["invalid_response", "remote_result_unconfirmed", "session_result_unavailable"].includes(error?.code));
const pending = () => new ApiError("Profile confirmation is pending", { code: "session_profile_pending", status: 425 });
const aborted = () => new DOMException("Profile update cancelled", "AbortError");
const same = (a, b) => ["model_id", "reasoning_effort", "permission_profile"].every(key => a[key] === b[key]);

// A profile is an absolute desired state, protected by the original revision.
// Read it after uncertain delivery; replay only while that revision is unchanged.
// This does not turn other, unguarded mutations into retryable operations.
export function createSessionProfileUpdater({ epoch = stamp, token = currentPageWriteToken,
  createRecovery = createRequestRecovery,
  send = (path, body, options) => api.put(path, body, options),
  inspect = (path, options) => api.get(path, options) } = {}) {
  return async function update(session, profile, { signal } = {}) {
    if (signal?.aborted) throw aborted();
    const project = resourceId(session.project_id), id = resourceId(session.id);
    const revision = Number(session.revision), tag = `"mdo-session-${id}-${revision}"`;
    const body = Object.freeze({ model_id: resourceId(profile.model_id),
      reasoning_effort: profile.reasoning_effort, permission_profile: profile.permission_profile });
    if (!Number.isSafeInteger(revision) || revision < 1 || (session.etag && session.etag !== tag) ||
        typeof body.reasoning_effort !== "string" ||
        !["read-only", "balanced", "full-access"].includes(body.permission_profile))
      throw new TypeError("Invalid session profile");
    const original = epoch(), writeToken = token(), path = `/projects/${project}/sessions/${id}`;
    const recovery = createRecovery(), cancel = () => recovery.dispose();
    signal?.addEventListener("abort", cancel, { once: true });
    let confirm = false, uncertain = false;
    function assertOwner() {
      recovery.assertActive();
      if (signal?.aborted || epoch() !== original) throw aborted();
    }
    function verified(reply) {
      assertOwner();
      if (writeToken && reply?.writeToken !== writeToken)
        throw new ApiError("Profile service restarted", { code: "write_token_conflict" });
      const data = reply?.data;
      if (data?.id !== id || data.project_id !== project || !Number.isSafeInteger(data.revision) ||
          data.revision < revision || reply.etag !== `"mdo-session-${id}-${data.revision}"` ||
          typeof data.model_id !== "string" || typeof data.reasoning_effort !== "string" ||
          !["read-only", "balanced", "full-access"].includes(data.permission_profile))
        throw new ApiError("Invalid profile acknowledgement", { code: "invalid_response" });
      if (data.status !== "active")
        throw new ApiError("Session is no longer active", { code: "session_state_conflict" });
      return { ...data, etag: reply.etag };
    }
    try {
      return await recovery.request(async attemptSignal => {
        assertOwner();
        if (confirm) {
          const fresh = verified(await inspect(path, { signal: attemptSignal }));
          if (same(fresh, body)) return fresh;
          if (fresh.revision !== revision) {
            const error = new ApiError("The session profile changed", { code: "session_profile_conflict" });
            error.currentSession = fresh; throw error;
          }
          // An earlier PUT may still arrive, but both carry the same original
          // ETag. The native manager admits at most one revision-changing write.
          confirm = false; throw pending();
        }
        const wasUncertain = uncertain;
        try {
          const fresh = verified(await withSessionRuntime(project, id, () => {
            assertOwner(); uncertain = true; confirm = true;
            return send(path + "/profile", body, { ifMatch: tag, signal: attemptSignal });
          },
          { signal: attemptSignal }));
          if (!same(fresh, body)) throw new ApiError("Profile result differs", { code: "invalid_response" });
          return fresh;
        } catch (error) {
          assertOwner();
          if (attemptSignal.aborted || fenced(error)) throw error;
          if (error?.code === "revision_conflict") throw pending();
          if (error?.code === "session_service_unavailable" && !wasUncertain) {
            uncertain = false; confirm = false;
          }
          throw error;
        }
      }, { retryable: transient });
    } catch (error) {
      if (!transient(error)) throw error;
      const final = new ApiError("Session profile could not be confirmed", {
        code: uncertain ? "session_profile_unconfirmed" : "session_profile_unavailable" });
      final.cause = error; throw final;
    } finally {
      signal?.removeEventListener("abort", cancel); recovery.dispose();
    }
  };
}

export const runSessionProfileUpdate = createSessionProfileUpdater();
