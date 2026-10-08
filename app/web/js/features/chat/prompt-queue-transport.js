import { ApiError, api, currentPageWriteToken, resourceId } from "../../api/client.js";
import { createRequestRecovery } from "../../api/request-recovery.js";
import { isTransientReadError } from "../../api/read-recovery.js";
import { targetState } from "../../api/target.js";

const epoch = () => JSON.stringify([targetState().selected,
  targetState().runtimeChanged, currentPageWriteToken()]);
const definite = new Set(["quota_exceeded", "daily_token_limit", "service_quota_exceeded", "insufficient_balance"]);
const uncertain = error => !definite.has(error?.code) && (isTransientReadError(error) ||
  ["invalid_response", "queue_unavailable", "remote_result_unconfirmed"].includes(error?.code));
const pending = () => new ApiError("Queue write confirmation is pending", { code: "queue_confirmation_pending" });

function path(key, id = "") {
  const [project, session] = key.split("/");
  return `/projects/${resourceId(project, "project")}/sessions/${resourceId(session, "session")}/queue` +
    (id ? `/${resourceId(id, "queue item")}` : "");
}

export function queueResponse(response) {
  const items = response?.data?.items;
  const ids = new Set();
  if (!Array.isArray(items) || items.length > 20 || items.some(item => {
    if (typeof item?.id !== "string" || !/^[A-Za-z0-9_-][A-Za-z0-9._-]*$/.test(item.id) ||
        ids.has(item.id) || typeof item.text !== "string" ||
        !["pending", "staged", "sending"].includes(item.state) ||
        typeof item.priority !== "boolean" || !Array.isArray(item.attachments) ||
        item.attachments.some(id => typeof id !== "string" || !/^[0-9a-f]{32}$/.test(id))) return true;
    ids.add(item.id); return false;
  })) throw new ApiError("Invalid prompt queue response", { code: "invalid_response" });
  return response;
}

async function receipt(key, id, signal, verify) {
  try {
    const reply = await api.get(path(key, id), { signal });
    verify?.(reply, signal);
    const value = reply.data;
    if (value?.id === id && value.state === "starting") return null;
    if (value?.id !== id || value.state !== "accepted" ||
        !/^run-[A-Za-z0-9_.-]+$/.test(value.run_id ?? ""))
      throw new ApiError("Invalid prompt queue receipt", { code: "invalid_response" });
    return value;
  } catch (error) {
    if (error?.status === 404 && error.code === "queue_receipt_not_found") return null;
    throw error;
  }
}

export function createPromptQueueTransport({ createRecovery = createRequestRecovery,
  stamp = epoch } = {}) {
  async function read(operation) {
    const recovery = createRecovery();
    try { return await recovery.request(operation); }
    finally { recovery.dispose(); }
  }

  // A lost write acknowledgement is not permission to repeat a write. An
  // absent item may have been removed by another page. Confirm by exact ID,
  // desired state or its immutable run receipt, sharing one bounded budget.
  async function mutate(key, id, send, proves, { origin = stamp(),
    runReceipt = false, conflicts = [] } = {}) {
    const recovery = createRecovery(), token = currentPageWriteToken();
    let submitted = false, confirming = false;
    function assertOwner(signal) {
      recovery.assertActive();
      if (signal?.aborted || stamp() !== origin)
        throw new DOMException("Queue write target changed", "AbortError");
    }
    function verify(reply, signal) {
      assertOwner(signal);
      if (token && reply?.writeToken !== token)
        throw new ApiError("Queue service restarted", { code: "write_token_conflict" });
    }
    function checked(reply, signal) {
      verify(reply, signal);
      return queueResponse(reply);
    }
    let original;
    try {
      try {
        const reply = await recovery.request(signal => {
          assertOwner(signal); submitted = true; return send(signal);
        }, { retry: false });
        checked(reply);
        if (proves(reply.data.items)) return reply;
        throw new ApiError("Invalid queue write acknowledgement", { code: "invalid_response" });
      } catch (error) {
        if (error?.name === "AbortError" || (!uncertain(error) && !conflicts.includes(error?.code))) throw error;
        original = error; confirming = true;
      }
      try {
        return await recovery.request(async signal => {
          assertOwner(signal);
          const reply = checked(await api.get(path(key), { signal }), signal);
          if (proves(reply.data.items)) return reply;
          if (runReceipt && !reply.data.items.some(item => item.id === id) &&
              await receipt(key, id, signal, verify)) {
            assertOwner(signal); return reply;
          }
          throw pending();
        }, { retryable: error => isTransientReadError(error) || error?.code === "queue_confirmation_pending" });
      } catch (failure) { original.confirmationError = failure; throw original; }
    } catch (error) {
      if (confirming || (submitted && (uncertain(error) || error?.name === "AbortError"))) {
        error.queueAdmissionUncertain = true; error.queueItemId = id;
      }
      throw error;
    } finally { recovery.dispose(); }
  }

  function state(key, id, next, states, runReceipt = false) {
    return mutate(key, id, signal => api.put(path(key, id), { state: next }, { signal }),
      items => items.some(item => item.id === id && states.includes(item.state)),
      { runReceipt, conflicts: ["queue_state_conflict"] });
  }

  return Object.freeze({
    stamp,
    readQueue: key => read(async signal => queueResponse(await api.get(path(key), { signal }))),
    readReceipt: (key, id) => read(signal => receipt(key, resourceId(id, "queue item"), signal)),
    post(key, body, { keepalive = false, origin = stamp() } = {}) {
      return mutate(key, body.id, signal => api.post(path(key), body, { keepalive, signal }), items => {
        const item = items.find(item => item.id === body.id);
        if (!item) return false;
        const sameProfile = (!item.profile && !body.profile) || (item.profile && body.profile &&
          ["model_id", "reasoning_effort", "permission_profile"].every(name => item.profile[name] === body.profile[name]));
        if (item.text !== body.text || item.priority !== body.priority ||
            JSON.stringify(item.attachments) !== JSON.stringify(body.attachments) || !sameProfile)
          throw new ApiError("The queued message ID has different content", { code: "queue_id_conflict", status: 409 });
        return true;
      }, { origin, runReceipt: true, conflicts: ["queue_item_consumed"] });
    },
    promote: (key, id) => state(key, id, "pending", ["pending", "sending"], true),
    markSending: (key, id) => state(key, id, "sending", ["sending"], true),
    retry: (key, id) => state(key, id, "pending", ["pending"]),
    remove: (key, id) => mutate(key, id, signal => api.delete(path(key, id), { signal }),
      items => !items.some(item => item.id === id)),
  });
}
