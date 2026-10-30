import { t as uiErrorText } from "../i18n.js";
import { api, ApiError, currentPageWriteToken } from "../api/client.js";
import { targetState } from "../api/target.js";
import { createRequestRecovery } from "../api/request-recovery.js";
import { isTransientReadError } from "../api/read-recovery.js";

const stamp = () => JSON.stringify([targetState().selected, targetState().runtimeChanged,
  currentPageWriteToken()]);
const permanent = error => ["home_restart_required", "write_token_required", "write_token_invalid",
  "write_token_conflict", "remote_runtime_changed", "runtime_inconsistent", "settings_unavailable",
  "runtime_rejected", "configuration_persistence_failed"].includes(error?.code);
const transient = error => !permanent(error) && (isTransientReadError(error) ||
  ["invalid_response", "remote_result_unconfirmed", "settings_result_unavailable"].includes(error?.code));
const invalid = () => new ApiError(uiErrorText("error.detail.invalidSettingsResponse", {}, "Invalid settings response"), { code: "invalid_response" });
const pending = () => new ApiError(uiErrorText("error.detail.settingsConfirmationIsPending", {}, "Settings confirmation is pending"), { status: 425 });
const aborted = () => new DOMException("Settings write cancelled", "AbortError");
const tag = revision => `"mdo-config-${revision}"`;

// A lost acknowledgement is not a failed save. Inspect first, then replay only
// with the original If-Match. Preview compares the full stored patch, including
// credential references that the public snapshot deliberately does not expose.
export function createSettingsWriter({ client = api, epoch = stamp, token = currentPageWriteToken,
  createRecovery = createRequestRecovery } = {}) {
  return async function write(patch, etag, { restore = false, signal } = {}) {
    if (signal?.aborted) throw aborted();
    const revision = Number(/^"mdo-config-([1-9][0-9]*)"$/.exec(etag)?.[1]);
    if (!Number.isSafeInteger(revision) || revision < 1) throw new TypeError(uiErrorText("error.detail.invalidSettingsEtag", {}, "Invalid settings ETag"));
    // Freeze the submitted intent even while the caller continues editing.
    const body = restore ? null : JSON.parse(JSON.stringify({ schema_version: 1, patch }));
    const original = epoch(), writeToken = token(), recovery = createRecovery();
    const cancel = () => recovery.dispose();
    signal?.addEventListener("abort", cancel, { once: true });
    let confirm = false, uncertain = false, accepted = null;
    function assertOwner() {
      recovery.assertActive();
      if (signal?.aborted || epoch() !== original) throw aborted();
    }
    function verifyToken(reply) {
      assertOwner();
      if (writeToken && reply?.writeToken !== writeToken)
        throw new ApiError(uiErrorText("error.detail.settingsServiceRestarted", {}, "Settings service restarted"), { code: "write_token_conflict" });
    }
    function snapshot(reply) {
      verifyToken(reply);
      const data = reply?.data;
      if (!Number.isSafeInteger(data?.revision) || data.revision < revision ||
          reply.etag !== tag(data.revision) || typeof data.user_patches?.settings !== "boolean" ||
          typeof data.transaction_service?.runtime_consistent !== "boolean") throw invalid();
      if (!data.transaction_service.runtime_consistent)
        throw new ApiError(uiErrorText("error.detail.settingsRuntimeRequiresARestart", {}, "Settings runtime requires a restart"), { code: "runtime_inconsistent", status: 503 });
      return { ...data, etag: reply.etag };
    }
    try {
      return await recovery.request(async attemptSignal => {
        assertOwner();
        if (!confirm) {
          // The request can time out before its handler returns. Both a delayed
          // first request and a retry retain the same original revision guard.
          confirm = true;
          const wasUncertain = uncertain;
          uncertain = true;
          try {
            const reply = restore
              ? await client.delete("/settings/settings", { ifMatch: etag, signal: attemptSignal })
              : await client.patch("/settings/settings", body, { ifMatch: etag, signal: attemptSignal });
            verifyToken(reply);
            const result = reply?.data;
            if (result?.domain !== "settings" || result.previous_revision !== revision ||
                !Number.isSafeInteger(result.revision) || result.revision < revision ||
                reply.etag !== tag(result.revision) || result.restored !== restore ||
                typeof result.changed !== "boolean") throw invalid();
            accepted = result;
          } catch (error) {
            assertOwner();
            if (error?.code === "revision_conflict") throw pending();
            // A failed merge has not reached the transaction. Other uncertain
            // replies must be inspected before any mutation can be repeated.
            if (error?.code === "configuration_merge_unavailable") {
              confirm = false;
              uncertain = wasUncertain;
            }
            throw error;
          }
        }
        const fresh = snapshot(await client.get("/settings", { signal: attemptSignal }));
        if (accepted) {
          if (fresh.revision < accepted.revision) throw invalid();
          return { result: accepted, snapshot: fresh };
        }
        let matches = !fresh.user_patches.settings;
        if (!restore) {
          const reply = await client.patch("/settings/settings/preview", body, { signal: attemptSignal });
          verifyToken(reply);
          const check = reply?.data;
          if (check?.domain !== "settings" || check.valid !== true || typeof check.changes !== "boolean" ||
              !Number.isSafeInteger(check.current_revision)) throw invalid();
          if (check.current_revision !== fresh.revision) throw pending();
          matches = !check.changes;
        }
        if (matches) return { snapshot: fresh, result: {
          domain: "settings", restored: restore, changed: fresh.revision !== revision,
          previous_revision: revision, revision: fresh.revision, recovered: true,
        } };
        if (fresh.revision !== revision)
          throw new ApiError(uiErrorText("error.detail.settingsChangedWhileSaving", {}, "Settings changed while saving"), { code: "revision_conflict", status: 412 });
        confirm = false;
        throw pending();
      }, { retryable: transient });
    } catch (error) {
      assertOwner();
      if (error?.name === "AbortError") throw error;
      if (permanent(error)) throw error;
      if (accepted || transient(error)) {
        const final = new ApiError(accepted
          ? "Settings were saved, but the latest snapshot could not be read"
          : uncertain ? "The settings save could not be confirmed" : "Settings could not be submitted", {
          code: accepted ? "settings_confirmation_failed" :
            uncertain ? "settings_save_unconfirmed" : "settings_save_unavailable",
          status: error?.status, details: { read_error: error?.code ?? "request_failed" },
        });
        final.cause = error; throw final;
      }
      throw error;
    } finally {
      signal?.removeEventListener("abort", cancel); recovery.dispose();
    }
  };
}
