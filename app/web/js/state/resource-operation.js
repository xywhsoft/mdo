import { api, ApiError, resourceId, currentPageWriteToken } from "../api/client.js";
import { targetState, subscribeTarget } from "../api/target.js";
import { createRequestRecovery } from "../api/request-recovery.js";
import { isTransientReadError } from "../api/read-recovery.js";
import { t } from "../i18n.js";

export const resourceOperationEpoch = () => JSON.stringify([targetState().selected,
  targetState().runtimeChanged, currentPageWriteToken()]);
const invalid = () => new ApiError(t("resource.operationInvalid", {}, "操作状态响应不符，请重新读取状态。"), { code: "resource_operation_invalid" });
const aborted = () => new DOMException("Operation observation cancelled", "AbortError");
function validate(value, original) {
  if (!value || typeof value.id !== "string" || !/^[A-Za-z0-9_-][A-Za-z0-9._-]{0,127}$/.test(value.id) ||
      !["module_reload", "mcp_refresh"].includes(value.kind) || typeof value.target !== "string" ||
      !["pending", "running", "succeeded", "failed", "cancelled"].includes(value.state) ||
      value.terminal !== ["succeeded", "failed", "cancelled"].includes(value.state) ||
      typeof value.message !== "string" || original &&
      ["id", "kind", "target"].some(key => value[key] !== original[key])) throw invalid();
}

// Observe one accepted operation. Only its GET is retried; starting, cancelling
// or replacing the native job never happens here. Retain its last known ID on
// uncertainty so the page can explicitly recheck without repeating the POST.
export async function awaitResourceOperation(operation, { signal, isCurrent = () => true,
  epoch = resourceOperationEpoch, originalEpoch = epoch(), get = api.get,
  recoveryOptions = {}, expected = null } = {}) {
  let current = operation, polls = 0, accepted = false;
  const recovery = createRequestRecovery(recoveryOptions);
  const cancel = () => recovery.dispose();
  const unsubscribe = subscribeTarget(() => { if (epoch() !== originalEpoch) cancel(); });
  signal?.addEventListener("abort", cancel, { once: true });
  const assertOwner = () => {
    if (signal?.aborted || !isCurrent() || epoch() !== originalEpoch) throw aborted();
    recovery.assertActive();
  };
  const wait = (delay, requestSignal) => new Promise((resolve, reject) => {
    const setTimer = recoveryOptions.setTimer || setTimeout, clearTimer = recoveryOptions.clearTimer || clearTimeout;
    const stop = () => { clearTimer(timer); requestSignal.removeEventListener("abort", stop); reject(aborted()); };
    const timer = setTimer(() => { requestSignal.removeEventListener("abort", stop); resolve(); }, delay);
    requestSignal.addEventListener("abort", stop, { once: true });
    if (requestSignal.aborted) stop();
  });
  try {
    validate(operation);
    if (expected && Object.entries(expected).some(([key, value]) => operation[key] !== value)) throw invalid();
    accepted = true; assertOwner();
    while (!current.terminal) {
      const delay = Math.min(250 * 2 ** Math.min(polls++, 2), 1000);
      current = await recovery.request(async requestSignal => {
        assertOwner(); await wait(delay, requestSignal); assertOwner();
        const data = (await get(`/operations/${resourceId(operation.id, "operation")}`, { signal: requestSignal })).data;
        assertOwner(); validate(data, operation); return data;
      });
    }
    assertOwner();
    if (current.state === "cancelled") throw new ApiError(t("resource.operationCancelled", {}, "操作已取消。"), { code: "resource_operation_cancelled" });
    if (current.state !== "succeeded") {
      const message = current.kind === "mcp_refresh" ? t("resource.connectionFailed", {}, "连接测试失败，请检查服务器配置。") : t("resource.reloadFailed", {}, "目录刷新失败");
      throw new ApiError(message + (current.message ? ` ${current.message}` : ""), { code: "resource_operation_failed" });
    }
    return current;
  } catch (error) {
    if (isTransientReadError(error) || error?.code === "invalid_response") {
      const final = new ApiError(t("resource.operationUnconfirmed", {}, "状态暂未确认，操作可能仍在后台执行。恢复连接后可重新读取状态；没有重新执行。"), { code: "resource_operation_unconfirmed" });
      final.cause = error; error = final;
    }
    if (accepted && !current.terminal) error.operation = current;
    throw error;
  } finally { recovery.dispose(); signal?.removeEventListener("abort", cancel); unsubscribe(); }
}
