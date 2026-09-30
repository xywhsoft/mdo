import { ApiError } from "../../api/client.js";

import { invalidPurge as invalid, purgeBindingsMatch as matches,
  readPurgeIntent as readIntent, readPurgeResult as readResult,
  reviewedPurgeIntent, purgeProjectEtag, newPurgeRequestId } from "./project-purge-contract.js";

// Reads never execute a purge. A missing receipt is not proof that a delayed
// execution cannot arrive: only a durable abort permits dismissal.
// Keep a locally known binding even if another page acknowledges its intent.
export function createProjectPurgeRecovery({ transport, timeoutMs = 8000,
  getWriteToken = () => null, onReload = () => {}, newId = newPurgeRequestId,
  beforePrepare = async () => {}, canComplete = () => true }) {
  let state = Object.freeze({ checked: false, intent: null, result: null,
    busy: false, error: null, writeConflict: false, intentSaved: false });
  let flight = null;
  let acknowledgingCommit = false;
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
    const response = await request("get", "/project-purge-intent");
    const remote = readIntent(response);
    if (getWriteToken() && response.writeToken && response.writeToken !== getWriteToken())
      publish({ writeConflict: true });
    const known = state.intent;
    if (known && !matches(known, remote)) {
      // An immutable abort may safely retire this page's old binding. A
      // committed/pending/unknown result must first settle local page state.
      const result = await query(known);
      publish({ result, checked: true, intentSaved: false });
      if (!aborted(result)) return;
    }
    publish({ checked: true, intent: remote, intentSaved: Boolean(remote),
      result: matches(known, remote) ? state.result : null });
    if (remote) publish({ result: await query(remote) });
  }

  function singleFlight(operation) {
    if (flight) return flight;
    publish({ busy: true });
    flight = Promise.resolve().then(operation).then((result) => {
      publish({ error: null });
      return result;
    }).catch((error) => {
      publish({ error });
      return false;
    }).finally(() => { flight = null; publish({ busy: false }); });
    return flight;
  }

  function refresh() { return singleFlight(inspect); }

  function tokenChanged(response) {
    if (getWriteToken() && response.writeToken && response.writeToken !== getWriteToken())
      publish({ writeConflict: true });
  }

  function prepare(preview) {
    return singleFlight(async () => {
      if (!state.checked || state.error || state.writeConflict) throw invalid();
      if (state.intent) {
        const reviewed = reviewedPurgeIntent(preview, state.intent.purge_request_id);
        if (!state.intentSaved || !matches(reviewed, state.intent) || state.result?.outcome !== "not_accepted")
          throw invalid();
        return true;
      }
      // Drain before publishing the pause: flushing an existing dirty draft
      // needs its ordinary write gate. No request ID exists on a drain failure.
      let timer;
      try {
        await Promise.race([beforePrepare(), new Promise((_, reject) => {
          timer = setTimeout(() => reject(new ApiError("Wait for local operations", { code: "purge_client_busy" })), timeoutMs);
        })]);
      } finally { clearTimeout(timer); }
      if (state.intent || state.error || state.writeConflict) throw invalid();
      const intent = reviewedPurgeIntent(preview, newId());
      publish({ intent, intentSaved: false, result: null });
      try {
        const response = await request("post", `/projects/${intent.project_id}/purge-intent`, {
          purge_request_id: intent.purge_request_id, created_at: intent.created_at,
        }, { ifMatch: purgeProjectEtag(intent) });
        tokenChanged(response);
        const saved = readIntent(response);
        if (!matches(saved, intent)) throw invalid();
        publish({ intent: saved, intentSaved: true, result: await query(saved) });
        return true;
      } catch (error) {
        // Keep the proposed original ID after a lost save. Reads may confirm
        // its publication, but never execute it or generate a replacement ID.
        try { await inspect(); } catch { /* Preserve the original save error. */ }
        throw error;
      }
    });
  }

  function execute(preview) {
    return singleFlight(async () => {
      const intent = state.intent;
      if (!intent || !state.intentSaved || state.writeConflict || state.error ||
          state.result?.outcome !== "not_accepted" ||
          !matches(reviewedPurgeIntent(preview, intent.purge_request_id), intent)) throw invalid();
      await inspect();
      if (!matches(state.intent, intent) || !state.intentSaved || state.writeConflict ||
          state.result?.outcome !== "not_accepted") throw invalid();
      try {
        const response = await request("post", `/projects/${intent.project_id}/purge`, {
          purge_request_id: intent.purge_request_id, created_at: intent.created_at,
        }, { ifMatch: purgeProjectEtag(intent) });
        tokenChanged(response);
        publish({ result: readResult(response.data, intent) });
        return true;
      } catch (error) {
        if (error?.details) publish({ result: readResult(error.details, intent) });
        try { await inspect(); } catch { /* Retain verified commit facts and the original failure. */ }
        throw error;
      }
    });
  }

  function completeCommitted() {
    return singleFlight(async () => {
      const intent = state.intent;
      if (!intent || state.result?.accepted !== true || state.result.outcome !== "committed" ||
          !state.result.committed || state.result.restart_required) return false;
      if (!canComplete()) throw new ApiError("Copy unsaved drafts before reloading", { code: "purge_draft_unsaved" });
      // Verify the terminal receipt immediately before ACK. Keep the local
      // pause through pagehide; clearing it would autosave purged memory.
      const receipt = await query(intent);
      if (!receipt.committed || receipt.outcome !== "committed" || receipt.restart_required) throw invalid();
      publish({ result: receipt, writeConflict: true });
      acknowledgingCommit = true;
      try {
        try {
          const response = await request("delete", "/project-purge-intent", undefined,
            { ifMatch: `"mdo-purge-intent-${intent.purge_request_id}"` });
          if (readIntent(response) !== null) throw invalid();
        } catch (error) {
          const remote = readIntent(await request("get", "/project-purge-intent"));
          if (matches(remote, intent)) throw error;
          // Absence/a different intent cannot erase the original commit fact.
          const settled = await query(intent);
          if (!settled.committed || settled.outcome !== "committed" || settled.restart_required) throw invalid();
        }
        onReload(intent.project_id);
        return true;
      } finally { acknowledgingCommit = false; }
    });
  }

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
    get: () => state, refresh, cancel, acknowledgeAbort, prepare, execute, completeCommitted,
    markWriteConflict(error) { publish({ writeConflict: true, error }); },
    reload() { onReload(state.intent?.project_id ?? null); },
    subscribe(listener) { listeners.add(listener); listener(state);
      return () => listeners.delete(listener); },
    isPaused: () => !state.checked || Boolean(state.intent) || Boolean(state.error) || state.writeConflict,
    allowsWrite({ method, path, ifMatch, body }) {
      if (state.checked && !state.intent && !state.error && !state.writeConflict) return true;
      // Stopping an existing run does not admit new work or recreate drafts.
      if (method === "DELETE" && /^\/runs\/[0-9a-f]{32}$/.test(path)) return true;
      const intent = state.intent;
      if (!intent) return false;
      return (method === "POST" && path === `/projects/${intent.project_id}/purge-cancel` &&
        ifMatch === purgeProjectEtag(intent) &&
        body?.purge_request_id === intent.purge_request_id && body?.created_at === intent.created_at) ||
        (method === "POST" && state.intentSaved === false && path === `/projects/${intent.project_id}/purge-intent` &&
        ifMatch === purgeProjectEtag(intent) && body?.purge_request_id === intent.purge_request_id &&
        body?.created_at === intent.created_at) ||
        (method === "POST" && state.intentSaved && state.result?.outcome === "not_accepted" && !state.writeConflict &&
        path === `/projects/${intent.project_id}/purge` && ifMatch === purgeProjectEtag(intent) &&
        body?.purge_request_id === intent.purge_request_id && body?.created_at === intent.created_at) ||
        (method === "DELETE" && path === "/project-purge-intent" && (aborted(state.result) || acknowledgingCommit) &&
        ifMatch === `"mdo-purge-intent-${intent.purge_request_id}"`);
    },
  });
}
