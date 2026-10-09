import { api, ApiError, resourceId, currentPageWriteToken } from "../api/client.js";
import { targetState, subscribeTarget } from "../api/target.js";
import { createRequestRecovery } from "../api/request-recovery.js";
import { isTransientReadError } from "../api/read-recovery.js";
import { t } from "../i18n.js";

export const resourceOperationEpoch = () => JSON.stringify([targetState().selected,
  targetState().runtimeChanged, currentPageWriteToken()]);
const invalid = () => new ApiError(t("resource.operationInvalid", {}, "操作状态响应不符，请重新读取状态。"), { code: "resource_operation_invalid" });
const aborted = () => new DOMException("Operation observation cancelled", "AbortError");
const mcpFailures = Object.freeze({
  mcp_start_failed: ["mcpStartFailed", "无法启动 MCP 程序，请检查程序路径、工作目录和环境配置。"],
  mcp_timeout: ["mcpTimeout", "MCP 服务器未在时限内响应，请检查服务器状态或调整超时设置。"],
  mcp_io: ["mcpIo", "MCP 连接中断或无法读取响应，请检查网络或服务器进程。"],
  mcp_busy: ["mcpBusy", "MCP 服务器正在处理另一个请求，请稍后测试连接。"],
  mcp_disabled: ["mcpDisabled", "MCP 服务器已停用，请启用后再测试连接。"],
  mcp_protocol_version: ["mcpProtocolVersion", "MCP 协议版本不兼容，请检查服务器支持的版本。"],
  mcp_invalid_response: ["mcpInvalidResponse", "服务器返回的数据不符合 MCP 协议，请检查连接方式和服务器实现。"],
  mcp_remote_error: ["mcpRemoteError", "MCP 服务器拒绝了初始化或工具发现请求，请检查服务器配置及日志。"],
  mcp_limit: ["mcpLimit", "MCP 响应超出配置的大小或工具数量限制，请检查服务器和限制设置。"],
  mcp_memory: ["mcpMemory", "内存不足，无法完成 MCP 连接测试，请释放内存后重试。"],
  mcp_configuration: ["mcpConfiguration", "MCP 配置无效，请检查服务器配置后再测试连接。"],
  mcp_session_expired: ["mcpSessionExpired", "MCP 服务器会话已过期，请重新测试连接。"],
  mcp_not_found: ["mcpNotFound", "MCP 服务器配置已被移除，请刷新服务器列表。"],
  mcp_status_unavailable: ["mcpStatusUnavailable", "连接测试已结束，但服务器状态暂时不可用，请刷新服务器列表。"],
});
function failureMessage(operation) {
  if (operation.kind !== "mcp_refresh") return t("resource.reloadFailed", {}, "目录刷新失败");
  if (Object.hasOwn(mcpFailures, operation.error_code)) {
    const [key, fallback] = mcpFailures[operation.error_code];
    return t(`resource.${key}`, {}, fallback);
  }
  if (operation.error_code === "mcp_http_error" && operation.http_status >= 100) {
    const status = operation.http_status;
    if (status === 401) return t("resource.mcpAuthentication", {}, "MCP 服务器未接受登录凭据，请检查凭据是否有效。");
    if (status === 403) return t("resource.mcpForbidden", {}, "当前凭据没有访问 MCP 服务器的权限，请检查授权。");
    if (status === 429) return t("resource.mcpRateLimited", {}, "MCP 服务器暂时限流，请稍后重试连接。");
    if (status === 408 || status === 504) return t("resource.mcpTimeout", {}, mcpFailures.mcp_timeout[1]);
    if (status >= 500) return t("resource.mcpServiceUnavailable", {}, "MCP 服务器暂不可用，请稍后重试连接。");
    if (status === 404 || status === 405) return t("resource.mcpEndpoint", {}, "MCP 服务地址不可用，请检查是否填写了正确的 MCP 接口地址。");
    return t("resource.mcpHttpError", { status }, "MCP 服务器未接受请求（HTTP {status}），请检查服务器配置。");
  }
  // Legacy and unknown classifications must not expose arbitrary provider text.
  return t("resource.connectionFailed", {}, "连接测试失败，请检查服务器配置。");
}
function validate(value, original) {
  if (!value || typeof value.id !== "string" || !/^[A-Za-z0-9_-][A-Za-z0-9._-]{0,127}$/.test(value.id) ||
      !["module_reload", "mcp_refresh"].includes(value.kind) || typeof value.target !== "string" ||
      !["pending", "running", "succeeded", "failed", "cancelled"].includes(value.state) ||
      value.terminal !== ["succeeded", "failed", "cancelled"].includes(value.state) ||
      typeof value.message !== "string" ||
      value.error_code !== undefined && (typeof value.error_code !== "string" || value.error_code.length > 64) ||
      value.http_status !== undefined && (!Number.isInteger(value.http_status) ||
        value.http_status !== 0 && (value.http_status < 100 || value.http_status > 599)) || original &&
      ["id", "kind", "target"].some(key => value[key] !== original[key])) throw invalid();
}

// Observe one accepted operation. Only its GET is retried; starting, cancelling
// or replacing the native job never happens here. Retain its last known ID on
// uncertainty so the page can explicitly recheck without repeating the POST.
export async function awaitResourceOperation(operation, { signal, isCurrent = () => true,
  epoch = resourceOperationEpoch, originalEpoch = epoch(), get = api.get,
  recoveryOptions = {}, recovery: sharedRecovery = null, expected = null } = {}) {
  let current = operation, polls = 0, accepted = false;
  const recovery = sharedRecovery || createRequestRecovery(recoveryOptions);
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
      }, { retryable: error => isTransientReadError(error) ||
        current.acceptance_unknown === true && error?.status === 404 && error?.code === "operation_not_found" });
    }
    assertOwner();
    if (current.state === "cancelled") throw new ApiError(t("resource.operationCancelled", {}, "操作已取消。"), { code: "resource_operation_cancelled" });
    if (current.state !== "succeeded") {
      throw new ApiError(failureMessage(current), { code: "resource_operation_failed" });
    }
    return current;
  } catch (error) {
    if (isTransientReadError(error) || error?.code === "invalid_response" ||
        current.acceptance_unknown === true && error?.status === 404 && error?.code === "operation_not_found") {
      const final = new ApiError(t("resource.operationUnconfirmed", {}, "状态暂未确认，操作可能仍在后台执行。恢复连接后可重新读取状态；没有重新执行。"), { code: "resource_operation_unconfirmed" });
      final.cause = error; error = final;
    }
    if (accepted && !current.terminal) error.operation = current;
    throw error;
  } finally { recovery.dispose(); signal?.removeEventListener("abort", cancel); unsubscribe(); }
}
