import { t as uiErrorText, t as uiText } from "../i18n.js";

// The outer connector is deliberately separate from the selected target API.
// It accesses only this installation's account and device connection metadata.
import { connectorJobError } from "./connector-error.js";
const endpoints = new Set(["/account", "/account/login", "/account/refresh", "/account/logout", "/account/cancel", "/account/callback",
  "/connector/state", "/connector/devices", "/connector/ticket"]);
let nonce = null;
// TLS certificate/grant are delivered by the authenticated target, retained
// only for this attempt. A native loopback gateway handles LAN TLS so WebView
// never needs certificate exceptions, private-network CORS or a machine CA.
export async function connectorDirectSocket(offer, { signal } = {}) {
  const rudp = offer?.transport === "xrudp", payload = rudp ? offer.offer : offer;
  if (rudp ? (!payload || payload.version !== 2 || !/^[0-9a-f]{32}$/.test(payload.peer_id) ||
      !/^[1-9][0-9]{0,19}$/.test(payload.owner_id) || !/^[0-9a-f]{32}$/.test(payload.nonce)) :
      (!offer || offer.version !== 1 || !/^[0-9a-f]{32}$/.test(offer.peer_id) ||
      !/^[0-9a-f]{64}$/.test(offer.token) || typeof offer.certificate !== "string" || offer.certificate.length > 1536 ||
      !Number.isInteger(offer.port) || offer.port < 1 || offer.port > 65535 ||
      !Array.isArray(offer.addresses) || !offer.addresses.length || offer.addresses.length > 4))
    throw new Error(uiErrorText("error.detail.invalidDirectOffer", {}, "Invalid direct offer"));
  const startup = await read("/project-purge-intent", { signal });
  if (signal?.aborted) throw new DOMException("Direct startup was canceled", "AbortError");
  const token = startup.response.headers.get("X-Mdo-Write-Token");
  if (!/^[0-9a-f]{32}-(0|[1-9][0-9]{0,19})$/.test(token || "")) throw new Error(uiErrorText("error.detail.invalidGatewayToken", {}, "Invalid gateway token"));
  const url = new URL("/api/v1/connector/direct", location.href);
  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  const socket = new WebSocket(url.href,["mdo.direct.v1",`mdo.token.${token}`]);
  socket.onopen = () => socket.send(JSON.stringify(offer));
  return socket;
}
async function read(path, options = {}) {
  const response = await fetch(`/api/v1${path}`, { ...options, cache: "no-store", credentials: "same-origin", redirect: "error" });
  const value = await response.json();
  if (!response.ok || !value?.ok) {
    const error = new Error(value?.error?.message || uiText("ui.connector.deviceConnectorIsTemporarilyUnavailable", {}, "设备连接器暂时不可用"));
    error.code = value?.error?.code || "connector_unavailable"; error.status = response.status; throw error;
  }
  return { data: value.data, response };
}
export async function connectorRequest(path, body, { signal, timeoutMs } = {}) {
  if (!endpoints.has(path)) throw new TypeError(uiErrorText("error.detail.outsideTheLocalConnectorScope", {}, "Outside the local connector scope"));
  if (timeoutMs !== undefined) {
    const deadline = jobDeadline(signal, jobBudget(timeoutMs));
    try { return await connectorRequest(path, body, { signal: deadline.signal }); }
    catch (error) { if (deadline.expired()) throw jobTimeout(); throw error; }
    finally { deadline.dispose(); }
  }
  if (body === undefined) return (await read(path, { signal })).data;
  if (nonce === null) {
    const startup = await read("/project-purge-intent", { signal });
    const token = startup.response.headers.get("X-Mdo-Write-Token");
    if (!/^[0-9a-f]{32}-(0|[1-9][0-9]{0,19})$/.test(token || "")) throw new Error(uiErrorText("error.detail.invalidConnectorStartupToken", {}, "Invalid connector startup token"));
    nonce = token;
  }
  return (await read(path, { method: "POST", signal, body: JSON.stringify(body),
    headers: { "Content-Type": "application/json", "X-Mdo-Write-Token": nonce } })).data;
}
const jobIdValid = id => /^[0-9a-f]{32}$/.test(id || "");
const jobBudget = ms => Number.isFinite(ms) && ms > 0 ? Math.min(ms, 40000) : 40000;
function jobTimeout() {
  const error = new Error(uiErrorText("error.detail.deviceConnectionDeadlineExceeded", {}, "Device connection deadline exceeded"));
  error.code = "connector_job_timeout"; error.outcomeUnknown = true; return error;
}
function jobDeadline(signal, ms) {
  const controller = new AbortController(); let expired = false;
  const abort = () => controller.abort();
  signal?.addEventListener("abort", abort, { once: true });
  if (signal?.aborted) abort();
  const timer = setTimeout(() => { expired = true; controller.abort(); }, ms);
  return { signal: controller.signal, expired: () => expired,
    dispose() { clearTimeout(timer); signal?.removeEventListener("abort", abort); } };
}
function jobPause(signal) {
  return new Promise((resolve, reject) => {
    const done = () => { signal?.removeEventListener("abort", abort); resolve(); };
    const timer = setTimeout(done, 250);
    const abort = () => { clearTimeout(timer); signal?.removeEventListener("abort", abort); reject(new DOMException("Connection cancelled", "AbortError")); };
    signal?.addEventListener("abort", abort, { once: true });
    if (signal?.aborted) abort();
  });
}
// Resume a known receipt by reading only. Reopening a dialog or a lost response
// must never create a replacement revoke/remove job.
export async function connectorWatchJob(id, { signal, timeoutMs = 40000, onState, action } = {}) {
  if (!jobIdValid(id)) throw new Error(uiErrorText("error.detail.invalidConnectorJob", {}, "Invalid connector job"));
  const budget = jobBudget(timeoutMs), until = Date.now() + budget, deadline = jobDeadline(signal, budget);
  let terminal = false;
  try {
    while (Date.now() < until) {
      if (deadline.signal.aborted) throw new DOMException("Connection cancelled", "AbortError");
      const state = await connectorRequest("/connector/state", undefined, { signal: deadline.signal });
      if (deadline.signal.aborted) throw new DOMException("Connection cancelled", "AbortError");
      if (state.job?.id !== id) {
        const error = new Error(uiErrorText("error.detail.deviceConnectionOperationChanged", {}, "Device connection operation changed"));
        error.code = "connector_job_changed"; error.outcomeUnknown = true; throw error;
      }
      onState?.(state);
      if (state.job.state === "done") return state;
      if (state.job.state === "failed") { terminal = true; throw connectorJobError(state, action || state.job.action); }
      await jobPause(deadline.signal);
    }
    throw jobTimeout();
  } catch (error) {
    if (deadline.expired()) error = jobTimeout();
    error.jobId = id;
    if (!terminal && error.name !== "AbortError") error.outcomeUnknown = true;
    throw error;
  } finally { deadline.dispose(); }
}
export async function connectorJob(body, { signal, onQueued, onState, timeoutMs } = {}) {
  const budget = jobBudget(timeoutMs), until = Date.now() + budget, deadline = jobDeadline(signal, budget);
  let queued;
  try { queued = await connectorRequest("/connector/devices", body, { signal: deadline.signal }); }
  catch (error) {
    if (deadline.expired()) throw jobTimeout();
    if (!error.status && error.name !== "AbortError") error.outcomeUnknown = true; throw error;
  } finally { deadline.dispose(); }
  const id = queued.job?.id;
  if (!jobIdValid(id)) {
    const error = new Error(uiErrorText("error.detail.invalidConnectorJob", {}, "Invalid connector job")); error.outcomeUnknown = true; throw error;
  }
  onQueued?.(id, queued);
  return connectorWatchJob(id, { signal, onState, timeoutMs: Math.max(1, until - Date.now()), action: body?.action });
}
export async function connectorTicket(deviceId, mode, signal, expectedOwner) {
  let account = await connectorRequest("/account", undefined, { signal });
  const checkOwner = () => {
    if (expectedOwner && account.profile?.id && String(account.profile.id) !== expectedOwner) {
      const error = new Error(uiText("ui.target.theControllerAccountChangedSelectADeviceForThe", {}, "控制端账号已变化，请重新选择该账号的设备"));
      error.code = "connector_account_changed"; throw error;
    }
  };
  checkOwner();
  // The controller account is separate from target account polling. Renew a
  // retained refresh token before treating an expired access token as logout.
  if (account.state === "refreshing" && account.refresh_available) {
    if (!account.busy) await connectorRequest("/account/refresh", {}, { signal });
    const until = Date.now() + 20000;
    while (account.state === "refreshing" && Date.now() < until) {
      if (signal?.aborted) throw new DOMException("Connection cancelled", "AbortError");
      await new Promise(resolve => setTimeout(resolve, 250));
      account = await connectorRequest("/account", undefined, { signal });
      checkOwner();
    }
    if (account.state === "refreshing") {
      const error = new Error(uiText("ui.connector.controllerSignInIsRefreshingReconnectShortly", {}, "控制端登录状态正在刷新，请稍后重新连接"));
      error.code = "connector_refresh_pending"; throw error;
    }
  }
  if (account.state !== "signed_in" || !Number.isSafeInteger(account.profile?.id)) {
    const error = new Error(uiText("ui.connector.signInOnTheControllerFirst", {}, "请先登录控制端账号")); error.code = "connector_login_required"; throw error;
  }
  checkOwner();
  const state = await connectorJob({ action: "connect", device_id: deviceId, mode }, { signal });
  const ticket = await connectorRequest("/connector/ticket", { job_id: state.job.id }, { signal });
  return { origin: account.origin, owner: String(account.profile.id), ticket, deviceId, mode };
}
