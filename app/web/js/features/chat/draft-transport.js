import { ApiError, api, currentPageWriteToken, resourceId } from "../../api/client.js";
import { isTransientReadError } from "../../api/read-recovery.js";
import { createRequestRecovery } from "../../api/request-recovery.js";
import { targetState } from "../../api/target.js";

const stamp = () => JSON.stringify([targetState().selected,
  targetState().runtimeChanged, currentPageWriteToken()]);
const definite = new Set(["quota_exceeded", "daily_token_limit", "service_quota_exceeded", "insufficient_balance"]);
const uncertain = error => !definite.has(error?.code) && (isTransientReadError(error) ||
  ["invalid_response", "remote_result_unconfirmed"].includes(error?.code));
const retrySave = error => error?.code === "draft_save_unconfirmed"
  ? uncertain(error.confirmationError) || error.confirmationError?.code === "draft_confirmation_pending"
  : !definite.has(error?.code) && isTransientReadError(error);

export function draftEndpoint(key) {
  if (!key) return "/draft";
  if (key.startsWith("project:")) return `/projects/${resourceId(key.slice(8), "project")}/draft`;
  const [project, session, extra] = key.split("/");
  if (extra !== undefined) throw new TypeError("Invalid draft owner");
  return `/projects/${resourceId(project, "project")}/sessions/${resourceId(session, "session")}/draft`;
}

// A versioned draft PUT may be retried only after a read still shows its
// original revision, using the identical snapshot. The server's draft lock
// accepts at most one such update. Other mutations remain single attempts.
export function createDraftTransport({ matchesSnapshot, createRecovery = createRequestRecovery,
  epoch = stamp } = {}) {
  if (typeof matchesSnapshot !== "function" || typeof epoch !== "function")
    throw new TypeError("Invalid draft transport callbacks");
  const active = new Set(), pending = new Map();
  async function scoped(action, { signal, origin = epoch(), token = currentPageWriteToken() } = {}) {
    const recovery = createRecovery({ eventTarget: null });
    active.add(recovery);
    const cancel = () => recovery.dispose();
    signal?.addEventListener("abort", cancel, { once: true });
    if (signal?.aborted) cancel();
    function assertOwner(requestSignal) {
      recovery.assertActive();
      if (requestSignal?.aborted || origin !== epoch())
        throw new DOMException("Draft target changed", "AbortError");
    }
    function checked(response, requestSignal) {
      assertOwner(requestSignal);
      if (token && response.writeToken !== token)
        throw new ApiError("Draft service restarted", { code: "write_token_conflict" });
      return response;
    }
    try { return await action({ recovery, assertOwner, checked }); }
    finally { signal?.removeEventListener("abort", cancel); active.delete(recovery); recovery.dispose(); }
  }
  function read(key, { signal, retry = true, ...options } = {}) {
    return scoped(({ recovery, assertOwner, checked }) => recovery.request(async requestSignal => {
      assertOwner(requestSignal);
      return checked(await api.get(draftEndpoint(key), { ...options, signal: requestSignal }), requestSignal);
    }, { retry }), { signal });
  }
  function mutate(key, send, conflicts = []) {
    return scoped(async ({ recovery, assertOwner, checked }) => {
      try { return await recovery.request(async signal => {
        assertOwner(signal); return checked(await send(signal), signal);
      }, { retry: false }); }
      catch (error) {
        if (error?.name === "AbortError" || (!uncertain(error) && !conflicts.includes(error?.code))) throw error;
        return recovery.request(async signal => {
          assertOwner(signal); return checked(await api.get(draftEndpoint(key), { signal }), signal);
        });
      }
    });
  }
  async function save(key, body, { keepalive = false } = {}) {
    let previous = pending.get(key);
    const isNew = !previous;
    if (!previous) {
      previous = { body: JSON.parse(JSON.stringify(body)), keepalive,
        origin: epoch(), token: currentPageWriteToken() };
      pending.set(key, previous);
    }
    const snapshot = previous.body;
    const revision = data => {
      const value = Number(data?.revision);
      if (!Number.isSafeInteger(value) || value < 0)
        throw new ApiError("Invalid saved draft revision", { code: "invalid_response" });
      return value;
    };
    function accepted(response) {
      if (revision(response.data) <= snapshot.revision || !matchesSnapshot(key, response.data, snapshot))
        throw new ApiError("Invalid draft save acknowledgement", { code: "invalid_response" });
      pending.delete(key); return { response, body: snapshot };
    }
    return scoped(async ({ recovery, assertOwner, checked }) => {
      const write = async signal => {
        assertOwner(signal);
        return checked(await api.put(draftEndpoint(key), snapshot,
          { keepalive: previous.keepalive, signal }), signal);
      };
      if (isNew) {
        try { return accepted(await recovery.request(write, { retry: false })); }
        catch (error) {
          if (error?.name === "AbortError") throw error;
          if (!uncertain(error) && error?.code !== "draft_conflict") { pending.delete(key); throw error; }
        }
      }
      try {
        return await recovery.request(async signal => {
          assertOwner(signal);
          const response = checked(await api.get(draftEndpoint(key), { signal }), signal);
          const current = revision(response.data);
          if (current > snapshot.revision) {
            const matches = matchesSnapshot(key, response.data, snapshot);
            pending.delete(key); // The older CAS can no longer commit.
            if (matches) return { response, body: snapshot };
            throw Object.assign(new ApiError("The draft changed in another window",
              { code: "draft_conflict", status: 409 }), { peerDraftChanged: true });
          }
          if (current < snapshot.revision)
            throw new ApiError("Draft confirmation is pending", { code: "draft_confirmation_pending" });
          return accepted(await write(signal));
        }, { retryable: error => uncertain(error) || error?.code === "draft_confirmation_pending" ||
          (error?.code === "draft_conflict" && !error.peerDraftChanged) });
      } catch (error) {
        if (error?.name === "AbortError" || !pending.has(key)) throw error;
        throw Object.assign(new ApiError("The draft save could not be confirmed",
          { code: "draft_save_unconfirmed" }), { confirmationError: error });
      }
    }, previous);
  }
  return Object.freeze({ read, mutate, save,
    canRetrySave: retrySave,
    hasPendingSave: key => pending.has(key),
    observe(key, data) {
      const previous = pending.get(key);
      if (previous && previous.origin === epoch() && Number.isSafeInteger(Number(data?.revision)) &&
          Number(data.revision) > previous.body.revision) pending.delete(key);
    },
    cancel() { for (const recovery of active) recovery.dispose(); },
  });
}
