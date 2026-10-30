import { t as uiErrorText } from "../../i18n.js";
import { ApiError } from "../../api/client.js";
import { isTransientReadError } from "../../api/read-recovery.js";
import { createRequestRecovery } from "../../api/request-recovery.js";

// Only prepares the history boundary; it never writes or retries a fork/cut.
// One latest UI intent owns the read. Navigation cancels it before a hidden
// reply can open a dialog or create a branch in a different workspace.
export function createSessionHistoryRead({ navigation, readHistory,
  createRecovery = createRequestRecovery }) {
  let pending = null;
  const route = () => JSON.stringify(navigation.get());
  function cancel() {
    pending?.dispose(); pending = null;
  }
  return Object.freeze({
    cancel,
    async read(session, { isCurrent = () => true } = {}) {
      cancel();
      const original = route(), recovery = createRecovery();
      pending = recovery;
      function assertOwner() {
        if (route() !== original || !isCurrent()) recovery.dispose();
        recovery.assertActive();
      }
      const unsubscribe = navigation.subscribe(() => {
        if (route() !== original || !isCurrent()) recovery.dispose();
      });
      try {
        const history = await recovery.request(signal => {
          assertOwner(); return readHistory(session, { signal });
        });
        assertOwner(); return history;
      } catch (error) {
        if (!isTransientReadError(error)) throw error;
        const final = new ApiError(uiErrorText("error.detail.theSessionHistoryBoundaryCouldNotBeRead", {}, "The session history boundary could not be read"), {
          code: "session_history_unavailable", status: error.status });
        final.cause = error; throw final;
      } finally {
        unsubscribe(); recovery.dispose();
        if (pending === recovery) pending = null;
      }
    },
  });
}
