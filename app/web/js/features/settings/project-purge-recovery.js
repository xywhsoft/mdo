import { ApiError } from "../../api/client.js";

const EMPTY_ETAG = '"mdo-purge-intent-empty"';
const bindingFields = ["purge_request_id", "project_id", "revision", "created_at"];

function invalid() {
  return new ApiError("The saved purge request could not be verified", {
    code: "purge_intent_unavailable",
  });
}

function validBinding(value) {
  return value && typeof value.purge_request_id === "string" &&
    typeof value.project_id === "string" && /^[0-9a-f]{32}$/.test(value.purge_request_id) &&
    /^[A-Za-z0-9_-][A-Za-z0-9_.-]{0,63}$/.test(value.project_id) &&
    Number.isSafeInteger(value.revision) && value.revision > 0 &&
    Number.isSafeInteger(value.created_at) && value.created_at > 0;
}

function matches(a, b) {
  return bindingFields.every((field) => a?.[field] === b?.[field]);
}

function readIntent(response) {
  const value = response?.data?.intent;
  if (value === null && response.etag === EMPTY_ETAG) return null;
  if (!validBinding(value) || Object.keys(value).length !== 5 ||
      typeof value.name !== "string" || !value.name ||
      new TextEncoder().encode(value.name).length > 256 || value.name.includes("\0") ||
      response.etag !== `"mdo-purge-intent-${value.purge_request_id}"`)
    throw invalid();
  return Object.freeze({ ...value });
}

function readResult(value, intent) {
  if (!matches(value, intent) || !validBinding(value) ||
      typeof value.committed !== "boolean" ||
      typeof value.restart_required !== "boolean" ||
      !["pending", "committed", "aborted", "not_accepted", "unknown"].includes(value.outcome) ||
      (["pending", "committed", "aborted"].includes(value.outcome) && value.accepted !== true) ||
      (value.outcome === "committed" && !value.committed) ||
      (value.outcome === "aborted" && value.committed) ||
      (value.outcome === "not_accepted" && (value.accepted !== false || value.committed)) ||
      (value.outcome === "unknown" && value.accepted !== null) ||
      value.workspace_files_removed !== false || value.shared_records_retained !== true)
    throw invalid();
  for (const field of ["target_count", "file_count", "directory_count", "total_bytes", "schedule_count"])
    if (!Number.isSafeInteger(value[field]) || value[field] < 0) throw invalid();
  return Object.freeze({ ...value });
}

// Recovery never executes a purge. A missing receipt is not proof that a
// delayed execution cannot arrive: only a durable abort permits dismissal.
// Keep a locally known binding even if another page acknowledges its intent.
export function createProjectPurgeRecovery({ transport, timeoutMs = 8000 }) {
  let state = Object.freeze({ checked: false, intent: null, result: null,
    busy: false, error: null });
  let flight = null;
  const listeners = new Set();

  function publish(patch) {
    if (state.result?.committed && patch.result && !patch.result.committed &&
        (!Object.hasOwn(patch, "intent") || matches(patch.intent, state.intent)))
      throw invalid();
    state = Object.freeze({ ...state, ...patch });
    for (const listener of listeners) listener(state);
  }

  async function request(method, path, body, options = {}) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    try {
      const params = { ...options, signal: controller.signal };
      return await (method === "post" ? transport.post(path, body, params)
        : transport[method](path, params));
    } finally { clearTimeout(timer); }
  }

  async function query(intent) {
    try {
      const response = await request("get", `/project-purges/${intent.purge_request_id}`);
      return readResult(response.data, intent);
    } catch (error) {
      if (error?.status === 404 && error.code === "purge_request_not_found")
        return Object.freeze({ outcome: "not_accepted", committed: false,
          accepted: false, restart_required: false });
      throw error;
    }
  }

  function aborted(result) {
    return result?.outcome === "aborted" && result.accepted === true &&
      result.committed === false;
  }

  async function inspect() {
    const remote = readIntent(await request("get", "/project-purge-intent"));
    const known = state.intent;
    if (known && !matches(known, remote)) {
      // An immutable abort may safely retire this page's old binding. A
      // committed/pending/unknown result must first settle local page state.
      const result = await query(known);
      publish({ result, checked: true });
      if (!aborted(result)) return;
    }
    publish({ checked: true, intent: remote,
      result: matches(known, remote) ? state.result : null });
    if (remote) publish({ result: await query(remote) });
  }

  function singleFlight(operation) {
    if (flight) return flight;
    publish({ busy: true });
    flight = Promise.resolve().then(operation).then(() => {
      publish({ error: null });
    }).catch((error) => {
      publish({ error });
    }).finally(() => { flight = null; publish({ busy: false }); });
    return flight;
  }

  function refresh() { return singleFlight(inspect); }

  function cancel() {
    return singleFlight(async () => {
      const intent = state.intent;
      if (!intent || state.result?.committed || state.result?.outcome === "pending" ||
          aborted(state.result)) return;
      try {
        const response = await request("post", `/projects/${intent.project_id}/purge-cancel`, {
          purge_request_id: intent.purge_request_id, created_at: intent.created_at,
        }, { ifMatch: `"mdo-project-${intent.project_id}-${intent.revision}"` });
        publish({ result: readResult(response.data, intent) });
      } catch (error) {
        // Even an HTTP failure can carry an already committed result. Never
        // infer "cancelled" from the status or replace the original ID.
        if (error?.details) publish({ result: readResult(error.details, intent) });
        else publish({ result: await query(intent) });
        throw error;
      }
    });
  }

  function acknowledgeAbort() {
    return singleFlight(async () => {
      const intent = state.intent;
      if (!intent || !aborted(state.result)) return;
      try {
        const response = await request("delete", "/project-purge-intent", undefined,
          { ifMatch: `"mdo-purge-intent-${intent.purge_request_id}"` });
        if (readIntent(response) !== null) throw invalid();
        publish({ checked: true, intent: null, result: null });
      } catch (error) {
        // A lost acknowledgement may already have removed the intent. GET
        // verifies absence/new ownership; it never repeats the cancellation.
        await inspect();
        if (state.intent && matches(state.intent, intent)) throw error;
      }
    });
  }

  return Object.freeze({
    get: () => state, refresh, cancel, acknowledgeAbort,
    subscribe(listener) { listeners.add(listener); listener(state);
      return () => listeners.delete(listener); },
    isPaused: () => !state.checked || Boolean(state.intent) || Boolean(state.error),
    allowsWrite({ method, path, ifMatch, body }) {
      if (state.checked && !state.intent && !state.error) return true;
      // Stopping an existing run does not admit new work or recreate drafts.
      if (method === "DELETE" && /^\/runs\/[0-9a-f]{32}$/.test(path)) return true;
      const intent = state.intent;
      if (!intent) return false;
      return (method === "POST" && path === `/projects/${intent.project_id}/purge-cancel` &&
        ifMatch === `"mdo-project-${intent.project_id}-${intent.revision}"` &&
        body?.purge_request_id === intent.purge_request_id && body?.created_at === intent.created_at) ||
        (method === "DELETE" && path === "/project-purge-intent" && aborted(state.result) &&
        ifMatch === `"mdo-purge-intent-${intent.purge_request_id}"`);
    },
  });
}
