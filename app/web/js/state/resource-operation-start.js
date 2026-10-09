import { api } from "../api/client.js";
import { clientActionId, createRequestRecovery } from "../api/request-recovery.js";
import { isTransientReadError } from "../api/read-recovery.js";
import { subscribeTarget } from "../api/target.js";
import { awaitResourceOperation, resourceOperationEpoch } from "./resource-operation.js";

// Submit once. Knowing the ID before sending lets us recover a missing reply
// through GET, including when a remote write's receipt is uncertain. Never
// retry this POST or create a replacement ID after uncertain acceptance.
export async function startResourceOperation(path, expected, { signal, isCurrent = () => true,
  epoch = resourceOperationEpoch, recoveryOptions = {}, createId = () => `op-c-${clientActionId()}` } = {}) {
  const id = createId(), originalEpoch = epoch();
  if (!/^op-c-[0-9a-f]{32}$/.test(id)) throw new TypeError("Invalid operation request identity");
  const pending = { id, ...expected, state: "pending", terminal: false, message: "", acceptance_unknown: true };
  const recovery = createRequestRecovery(recoveryOptions);
  const cancel = () => recovery.dispose();
  const unsubscribe = subscribeTarget(() => { if (epoch() !== originalEpoch) cancel(); });
  signal?.addEventListener("abort", cancel, { once: true });
  const assertOwner = () => {
    if (signal?.aborted || !isCurrent() || epoch() !== originalEpoch)
      throw new DOMException("Operation observation cancelled", "AbortError");
    recovery.assertActive();
  };
  try {
    assertOwner();
    let operation;
    try {
      operation = await recovery.request(async requestSignal => {
        assertOwner();
        return (await api.post(path, { operation_id: id }, { signal: requestSignal })).data;
      }, { retry: false });
    } catch (error) {
      assertOwner();
      // Explicit host refusals say nothing was queued. Other temporary HTTP
      // failures and missing/malformed bodies may occur after submission.
      if (error.code === "operation_unavailable" || !(isTransientReadError(error) ||
          ["invalid_response", "remote_result_unconfirmed", "operation_result_unavailable"].includes(error.code))) throw error;
      operation = pending;
    }
    assertOwner();
    return await awaitResourceOperation(operation, { signal, isCurrent, epoch, originalEpoch,
      recoveryOptions, recovery, expected: { id, ...expected } });
  } catch (error) {
    // Leaving the page cancels observation, not native execution. Keep the
    // original identity so returning to settings can read, never resubmit.
    if (error?.name === "AbortError") error.operation = pending;
    throw error;
  } finally { recovery.dispose(); signal?.removeEventListener("abort", cancel); unsubscribe(); }
}
