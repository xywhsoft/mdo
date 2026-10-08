import { ApiError, api, currentPageWriteToken } from "../api/client.js";
import { isTransientReadError } from "../api/read-recovery.js";
import { createRequestRecovery } from "../api/request-recovery.js";
import { targetState } from "../api/target.js";

const uncertain = error => isTransientReadError(error) ||
  ["remote_result_unconfirmed", "invalid_response"].includes(error?.code);
const stamp = () => JSON.stringify([targetState().selected, targetState().runtimeChanged,
  currentPageWriteToken()]);
// Runtime IDs alone may be reused after a restart. Pin immutable task ownership
// as well as the page/target epoch before retrying its idempotent cancellation.
const identity = task => JSON.stringify([String(task.id), task.kind, task.owner_agent_id,
  task.owner_run_id, task.parent_task_id, task.owner_session, task.created_at,
  task.schedule_id, task.schedule_generation]);

export async function runTaskStopAction({ id, task = null,
  cancel = (value, options) => api.delete(`/tasks/${value}`, options),
  inspect = (value, options) => api.get(`/tasks/${value}`, options),
  epoch = stamp, token = currentPageWriteToken(), recoveryOptions } = {}) {
  id = String(id ?? "");
  if (!/^[1-9][0-9]*$/.test(id) || (task && String(task.id) !== id))
    throw new TypeError("task ID is invalid");
  const recovery = createRequestRecovery(recoveryOptions), original = epoch();
  let owner = task ? identity(task) : null, checking = false, submitted = false;
  function assertOwner() {
    recovery.assertActive();
    if (epoch() !== original) throw new DOMException("Task target changed", "AbortError");
  }
  function snapshot(reply) {
    assertOwner();
    if (token && reply?.writeToken !== token)
      throw new ApiError("Task service restarted", { code: "write_token_conflict" });
    const current = reply?.data;
    if (String(current?.id) !== id || typeof current?.terminal !== "boolean" ||
        typeof current?.stop_requested !== "boolean")
      throw new ApiError("Invalid task stop response", { code: "invalid_response" });
    const next = identity(current);
    if (owner !== null && next !== owner)
      throw new ApiError("Task ownership changed", { code: "task_stop_context_changed" });
    owner = next;
    return current;
  }
  try {
    return await recovery.request(async signal => {
      assertOwner();
      if (checking || owner === null) {
        const current = snapshot(await inspect(id, { signal }));
        if (current.terminal || current.stop_requested) return current;
      }
      // After an uncertain DELETE, inspect first. Repeat only when this exact
      // task is still active; never repeat task creation or tool execution.
      checking = true; submitted = true;
      return snapshot(await cancel(id, { signal }));
    }, { mutation: true });
  } catch (error) {
    if (submitted && uncertain(error)) {
      const final = new ApiError("Task stop acknowledgement could not be confirmed", {
        code: "task_stop_unconfirmed",
      });
      final.cause = error; throw final;
    }
    throw error;
  } finally { recovery.dispose(); }
}
