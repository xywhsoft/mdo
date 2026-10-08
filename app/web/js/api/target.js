import { createRemoteTransport, remoteError } from "./remote-transport.js";
import { connectorTicket, connectorDirectSocket } from "./connector.js";

const KEY = "mdo.target.v1", ROUTES = "mdo.target.routes.v1";
const listeners = new Set();
function storageRead(key) {
  try { return JSON.parse(globalThis.sessionStorage?.getItem(key) || "null"); } catch { return null; }
}
function validSelection(value) {
  return value && /^[0-9a-f]{32}$/.test(value.id) && /^[1-9][0-9]*$/.test(value.owner) &&
    ["control", "view"].includes(value.mode) && typeof value.name === "string" && value.name.length <= 96;
}
let selected = storageRead(KEY); if (!validSelection(selected)) selected = null;
let transport = null, connecting = null, retry = 0, attempts = 0, switching = false, stopped = false;
let status = { selected, connected: !selected, error: null };
let switchGuard = async () => true;
let previousRuntime = "", runtimeChanged = false;
const publish = changes => {
  status = { ...status, ...changes, selected };
  for (const listener of listeners) listener(status);
};
export function targetState() { return status; }
export function isRemoteTarget() { return Boolean(selected); }
export function subscribeTarget(listener) { listeners.add(listener); listener(status); return () => listeners.delete(listener); }
export function setTargetSwitchGuard(guard) { switchGuard = guard; }
export async function requestTargetSwitch(next, options = {}) {
  if (!await switchGuard(options)) throw remoteError("target_switch_busy", "请等待操作完成，并保存或复制未保存的草稿后切换设备", { target: next });
  switchTarget(next);
}

export function targetFetch(path, options = {}) {
  const method = (options.method || "GET").toUpperCase();
  if (switching) return Promise.reject(remoteError("target_changing", "正在切换设备，请稍候"));
  if (selected && runtimeChanged && !["GET", "HEAD"].includes(method))
    return Promise.reject(remoteError("remote_runtime_changed", "目标设备运行代已变化，请先保留草稿并重新载入页面"));
  if (selected && ((path === "/api/v1/update/install" && method === "POST") ||
      (path === "/api/v1/account/login" && method === "POST" && !JSON.parse(options.body || "{}").identifier)))
    return Promise.reject(remoteError("remote_native_unavailable", "此操作需要在目标设备的原生窗口中完成"));
  if (selected) return transport ? transport.fetch(path, options) :
    Promise.reject(remoteError("remote_offline", "目标设备离线，操作不会转到本机"));
  return fetch(path, options);
}
export function targetLiveSocket(url, protocols) {
  if (switching || stopped) throw remoteError("target_changing", "正在切换设备，请稍候");
  if (!selected) return new WebSocket(url, protocols);
  if (!transport?.state()) throw remoteError("remote_offline", "目标设备离线");
  const token = protocols?.find(value => value.startsWith("mdo.token."))?.slice(10);
  if (!/^[0-9a-f]{32}-(0|[1-9][0-9]{0,19})$/.test(token || "")) throw new Error("Invalid target live token");
  return transport.liveSocket(token);
}
function schedule(error) {
  clearTimeout(retry);
  if (stopped || switching || !selected || ["remote_revoked", "connector_login_required", "connector_account_changed"].includes(error?.code)) return;
  const delay = Math.min(500 * 2 ** Math.min(attempts++, 5), 15000);
  retry = setTimeout(() => { retry = 0; void initializeTarget(); }, delay);
}
export function initializeTarget() {
  if (!selected || switching || stopped) return Promise.resolve();
  if (connecting) return connecting;
  if (transport?.state()) return Promise.resolve();
  if (!transport) transport = createRemoteTransport({ directSocket: connectorDirectSocket, onState(value) {
    if (value.connected) {
      if (previousRuntime && previousRuntime !== value.hello.runtime_id) runtimeChanged = true;
      previousRuntime = value.hello.runtime_id;
    }
    value.runtimeChanged = runtimeChanged;
    value.uncertain = transport?.uncertain() || [];
    publish(value);
    if (!value.connected) schedule(value.error);
    else { attempts = 0; clearTimeout(retry); retry = 0; }
  } });
  connecting = (async () => {
    try {
      const details = await connectorTicket(selected.id, selected.mode, undefined, selected.owner);
      if (details.owner !== selected.owner) throw remoteError("connector_account_changed", "控制端账号已变化，请重新选择该账号的设备");
      if (switching || stopped) return;
      await transport.connect(details);
    } catch (error) { publish({ connected: false, error }); schedule(error); }
    finally { connecting = null; }
  })();
  return connecting;
}

// Reload is the context boundary: existing subscriptions, dialogs, store
// closures and asynchronous action continuations cannot address the next target.
export function switchTarget(next) {
  if (next !== null && !validSelection(next)) throw new TypeError("Invalid target selection");
  const routes = storageRead(ROUTES) || {};
  const key = value => value ? `${value.owner}/${value.id}` : "local";
  routes[key(selected)] = location.hash;
  const entries = Object.entries(routes).slice(-17);
  sessionStorage.setItem(ROUTES, JSON.stringify(Object.fromEntries(entries)));
  if (next) sessionStorage.setItem(KEY, JSON.stringify(next)); else sessionStorage.removeItem(KEY);
  switching = true; stopped = true; clearTimeout(retry); transport?.close();
  history.replaceState({ ...history.state, mdoWorkspace: null }, "", routes[key(next)] || "#/");
  location.reload();
}
if (typeof window !== "undefined") window.addEventListener("pagehide", () => {
  stopped = true; clearTimeout(retry); transport?.close();
});
if (typeof window !== "undefined") window.addEventListener("pageshow", event => { if (event.persisted) location.reload(); });
