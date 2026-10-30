import { t as uiErrorText } from "../i18n.js";
import { api, ApiError, currentPageWriteToken, resourceId } from "../api/client.js";
import { createRequestRecovery } from "../api/request-recovery.js";
import { isTransientReadError } from "../api/read-recovery.js";
import { resourceOperationEpoch } from "./resource-operation.js";

const fenced = error => ["home_restart_required", "write_token_required", "write_token_invalid",
  "write_token_conflict", "remote_runtime_changed"].includes(error?.code);
const rejected = error => fenced(error) || ["schedule_persistence_failed",
  "schedule_service_unavailable", "schedule_update_failed", "schedule_limit_reached"].includes(error?.code);
const uncertain = error => !fenced(error) && (isTransientReadError(error) ||
  ["invalid_response", "remote_result_unconfirmed", "schedule_result_unavailable"].includes(error?.code));
const pending = () => new ApiError(uiErrorText("error.detail.scheduleConfirmationIsPending", {}, "Schedule confirmation is pending"), { code: "schedule_confirmation_pending", status: 425 });
const aborted = () => new DOMException("Schedule save cancelled", "AbortError");
const tag = (id, revision) => `"mdo-schedule-${id}-${revision}"`;
const signature = body => JSON.stringify(Object.entries(body).sort(([a], [b]) => a.localeCompare(b)));

// Submit a revisioned mutation once. Missing acknowledgement only starts GET
// confirmation, never a second mutation. New forms reserve their ID before
// saving; an explicit retry of an uncertain form keeps that ID and intention.
// This page-local registry is not a durable journal across app restarts.
export function createScheduleWriter({ client = api, epoch = resourceOperationEpoch,
  token = currentPageWriteToken, createRecovery = createRequestRecovery } = {}) {
  const intents = new Map();
  let owner;
  async function write(kind, id, etag, definition, { signal } = {}) {
    id = resourceId(id, "schedule");
    if (id.length > 64 || signal?.aborted) {
      if (signal?.aborted) throw aborted();
      throw new TypeError(uiErrorText("error.detail.invalidScheduleIdentity", {}, "Invalid schedule identity"));
    }
    const match = /^"mdo-schedule-(.+)-([1-9][0-9]*)"$/.exec(etag || "");
    const revision = kind === "create" ? 0 : Number(match?.[2]);
    if (kind !== "create" && (match?.[1] !== id || !Number.isSafeInteger(revision)))
      throw new TypeError(uiErrorText("error.detail.invalidScheduleRevision", {}, "Invalid schedule revision"));
    const body = JSON.parse(JSON.stringify(definition ?? {}));
    const original = epoch(), writeToken = token();
    if (owner !== original) { intents.clear(); owner = original; }
    const key = `${kind}/${id}/${etag || ""}`;
    let intent = intents.get(key);
    if (intent && signature(intent.body) !== signature(body))
      throw new ApiError(uiErrorText("error.detail.anEarlierScheduleSaveIsNotConfirmed", {}, "An earlier schedule save is not confirmed"), { code: "schedule_save_pending_changed" });
    if (!intent) {
      if (intents.size >= 64)
        throw new ApiError(uiErrorText("error.detail.tooManyUnconfirmedScheduleSaves", {}, "Too many unconfirmed schedule saves"), { code: "schedule_save_pending_limit" });
      intent = { body, confirm: false };
      intents.set(key, intent);
    }
    const recovery = createRecovery(), cancel = () => recovery.dispose();
    signal?.addEventListener("abort", cancel, { once: true });
    function assertOwner() {
      recovery.assertActive();
      if (signal?.aborted || epoch() !== original) throw aborted();
    }
    function verifyToken(reply) {
      assertOwner();
      if (writeToken && reply?.writeToken !== writeToken)
        throw new ApiError(uiErrorText("error.detail.scheduleServiceRestarted", {}, "Schedule service restarted"), { code: "write_token_conflict" });
    }
    function verify(reply) {
      verifyToken(reply);
      const data = reply?.data;
      if (kind === "remove" && data?.id === id && data.removed === true) return reply;
      if (data?.id !== id || !Number.isSafeInteger(data.revision) || data.revision < 1 ||
          reply.etag !== tag(id, data.revision))
        throw new ApiError(uiErrorText("error.detail.invalidScheduleAcknowledgement", {}, "Invalid schedule acknowledgement"), { code: "invalid_response" });
      if (data.revision < revision) throw pending();
      if (kind === "remove" || Object.entries(intent.body).some(([name, value]) => data[name] !== value)) {
        if (kind !== "create" && data.revision <= revision) throw pending();
        throw new ApiError(uiErrorText("error.detail.theScheduleChangedWhileSaving", {}, "The schedule changed while saving"), { code: "revision_conflict", status: 412 });
      }
      return reply;
    }
    try {
      if (!intent.confirm) {
        // Set before awaiting; abort/timeout can precede native completion.
        intent.confirm = true;
        try {
          const reply = await recovery.request(attemptSignal => {
            assertOwner();
            const options = { ifMatch: etag, signal: attemptSignal };
            if (kind === "create") return client.post("/schedules", intent.body, options);
            if (kind === "remove") return client.delete(`/schedules/${id}`, options);
            return client.put(`/schedules/${id}${kind === "enabled" ? "/enabled" : ""}`, intent.body, options);
          }, { retry: false });
          const result = verify(reply); intents.delete(key); return result;
        } catch (error) {
          assertOwner();
          // Browsers themselves may repeat an idempotent transport request.
          // A duplicate create/PUT can return conflict after the first saved;
          // read the reserved ID before interpreting that response as failure.
          if (rejected(error) || !uncertain(error) && !["schedule_conflict", "revision_conflict"].includes(error?.code)) {
            intent.confirm = false;
            intents.delete(key); throw error;
          }
        }
      }
      const result = await recovery.request(async attemptSignal => {
        assertOwner();
        try { return verify(await client.get(`/schedules/${id}`, { signal: attemptSignal })); }
        catch (error) {
          assertOwner();
          if (error?.code === "schedule_not_found" && error.status === 404) {
            if (kind === "remove") {
              // Error envelopes do not carry an adopted write token. Verify
              // the readable catalog belongs to this same native generation
              // before treating absence as completed removal.
              const catalog = await client.get("/schedules", { signal: attemptSignal });
              verifyToken(catalog);
              if (!Array.isArray(catalog.data?.items) || catalog.data.items.some(item => item.id === id))
                throw pending();
              return { data: { id, removed: true } };
            }
            if (kind === "create") throw pending();
          }
          throw error;
        }
      }, { retryable: error => uncertain(error) || error?.code === "schedule_confirmation_pending" });
      intents.delete(key); return result;
    } catch (error) {
      assertOwner();
      if (intent.confirm && !fenced(error) && !["revision_conflict", "schedule_not_found"].includes(error?.code)) {
        const final = new ApiError(uiErrorText("error.detail.scheduleSaveCouldNotBeConfirmed", {}, "Schedule save could not be confirmed"), {
          code: "schedule_save_unconfirmed", details: { schedule_id: id } });
        final.cause = error; throw final;
      }
      throw error;
    } finally { signal?.removeEventListener("abort", cancel); recovery.dispose(); }
  }
  async function run(id, revision, { signal } = {}) {
    id = resourceId(id, "schedule");
    if (id.length > 64) throw new TypeError(uiErrorText("error.detail.invalidScheduleIdentity", {}, "Invalid schedule identity"));
    if (!Number.isSafeInteger(revision) || revision < 1) throw new TypeError(uiErrorText("error.detail.invalidScheduleRevision", {}, "Invalid schedule revision"));
    const original = epoch(), writeToken = token(), recovery = createRecovery();
    const cancel = () => recovery.dispose();
    signal?.addEventListener("abort", cancel, { once: true });
    function assertOwner() {
      recovery.assertActive();
      if (signal?.aborted || epoch() !== original) throw aborted();
    }
    try {
      const result = await recovery.request(attemptSignal => {
        assertOwner();
        return client.post(`/schedules/${id}/run`, undefined, { ifMatch: tag(id, revision), signal: attemptSignal });
      }, { retry: false });
      assertOwner();
      if (writeToken && result?.writeToken !== writeToken)
        throw new ApiError(uiErrorText("error.detail.scheduleServiceRestarted", {}, "Schedule service restarted"), { code: "write_token_conflict" });
      if (result?.data?.schedule_id !== id || !Number.isSafeInteger(result.data.task_id) ||
          result.data.task_id < 1 || !Number.isSafeInteger(result.data.agent_run_id) || result.data.agent_run_id < 1)
        throw new ApiError(uiErrorText("error.detail.invalidScheduleRunAcknowledgement", {}, "Invalid schedule run acknowledgement"), { code: "invalid_response" });
      return result;
    } catch (error) {
      assertOwner();
      if (!rejected(error) && error?.code !== "schedule_run_unavailable" &&
          (uncertain(error) || error?.code === "revision_conflict")) {
        const final = new ApiError(uiErrorText("error.detail.scheduleRunCouldNotBeConfirmed", {}, "Schedule run could not be confirmed"), {
          code: "schedule_run_unconfirmed", status: error?.status });
        final.cause = error; throw final;
      }
      throw error;
    } finally { signal?.removeEventListener("abort", cancel); recovery.dispose(); }
  }
  return Object.freeze({
    run,
    create: (body, options) => write("create", body.id, "", body, options),
    replace: (id, etag, body, options) => write("replace", id, etag, body, options),
    enabled: (id, revision, enabled, options) => write("enabled", id, tag(id, revision), { enabled }, options),
    remove: (id, revision, options) => write("remove", id, tag(id, revision), {}, options),
  });
}
