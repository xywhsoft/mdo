import { ApiError } from "../../api/client.js";
import { createRequestRecovery } from "../../api/request-recovery.js";

// The scheduler owns quiet backoff. This adapter bounds one read and releases
// it when the selected run, page visibility or live connection changes.
export function createRunPollRead({ read, createRecovery = createRequestRecovery }) {
  let pending = null;
  function cancel() {
    const previous = pending;
    pending = null;
    previous?.dispose();
  }
  return Object.freeze({
    cancel,
    async read(owner) {
      cancel();
      const expected = { id: owner.id, project_id: owner.project_id, session_id: owner.session_id };
      const recovery = createRecovery();
      pending = recovery;
      try {
        const run = await recovery.request(signal => {
          recovery.assertActive();
          return read(expected.id, { signal });
        }, { retry: false });
        if (run?.id !== expected.id || run.project_id !== expected.project_id ||
            run.session_id !== expected.session_id || typeof run.terminal !== "boolean")
          throw new ApiError("Invalid polled run response", { code: "invalid_response" });
        return run;
      } finally {
        if (pending === recovery) pending = null;
        recovery.dispose();
      }
    },
  });
}
