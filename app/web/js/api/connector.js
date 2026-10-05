// The outer connector is deliberately separate from the selected target API.
// It accesses only this installation's account and device connection metadata.
const endpoints = new Set(["/account", "/account/login", "/account/refresh", "/account/logout",
  "/connector/state", "/connector/devices", "/connector/ticket"]);
let nonce = null;
async function read(path, options = {}) {
  const response = await fetch(`/api/v1${path}`, { ...options, cache: "no-store", credentials: "same-origin", redirect: "error" });
  const value = await response.json();
  if (!response.ok || !value?.ok) {
    const error = new Error(value?.error?.message || "设备连接器暂时不可用");
    error.code = value?.error?.code || "connector_unavailable"; throw error;
  }
  return { data: value.data, response };
}
export async function connectorRequest(path, body, { signal } = {}) {
  if (!endpoints.has(path)) throw new TypeError("Outside the local connector scope");
  if (body === undefined) return (await read(path, { signal })).data;
  if (nonce === null) {
    const startup = await read("/project-purge-intent", { signal });
    const token = startup.response.headers.get("X-Mdo-Write-Token");
    if (!/^[0-9a-f]{32}-(0|[1-9][0-9]{0,19})$/.test(token || "")) throw new Error("Invalid connector startup token");
    nonce = token;
  }
  return (await read(path, { method: "POST", signal, body: JSON.stringify(body),
    headers: { "Content-Type": "application/json", "X-Mdo-Write-Token": nonce } })).data;
}
export async function connectorJob(body, { signal } = {}) {
  const queued = await connectorRequest("/connector/devices", body, { signal });
  const id = queued.job?.id, until = Date.now() + 20000;
  if (!/^[0-9a-f]{32}$/.test(id || "")) throw new Error("Invalid connector job");
  while (Date.now() < until) {
    if (signal?.aborted) throw new DOMException("Connection cancelled", "AbortError");
    await new Promise(resolve => setTimeout(resolve, 250));
    const state = await connectorRequest("/connector/state", undefined, { signal });
    if (state.job?.id !== id) throw new Error("Device connection operation changed");
    if (state.job.state === "done") return state;
    if (state.job.state === "failed") {
      const error = new Error("无法完成设备操作，请检查登录、设备状态和服务连接");
      error.code = state.error; error.status = state.status; throw error;
    }
  }
  throw new Error("Device connection deadline exceeded");
}
export async function connectorTicket(deviceId, mode, signal, expectedOwner) {
  const account = await connectorRequest("/account", undefined, { signal });
  if (account.state !== "signed_in" || !Number.isSafeInteger(account.profile?.id)) {
    const error = new Error("请先登录控制端账号"); error.code = "connector_login_required"; throw error;
  }
  if (expectedOwner && String(account.profile.id) !== expectedOwner) {
    const error = new Error("控制端账号已变化，请重新选择该账号的设备");
    error.code = "connector_account_changed"; throw error;
  }
  const state = await connectorJob({ action: "connect", device_id: deviceId, mode }, { signal });
  const ticket = await connectorRequest("/connector/ticket", { job_id: state.job.id }, { signal });
  return { origin: account.origin, owner: String(account.profile.id), ticket, deviceId, mode };
}
