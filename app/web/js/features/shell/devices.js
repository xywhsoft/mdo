import { connectorRequest, connectorJob, connectorWatchJob } from "../../api/connector.js";
import { targetState, subscribeTarget, requestTargetSwitch, initializeTarget, revokeTarget } from "../../api/target.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { t, subscribeLocale, currentLocale } from "../../i18n.js";
import { openLayer } from "../../components/layers.js";
import { createLoginWindow } from "../account/login-window.js";
import { isAccountSignedIn } from "../account/session-state.js";

export function createDevices() {
  const button = document.querySelector("#open-devices");
  const settingsTarget = document.querySelector("#settings-target");
  const banner = document.querySelector("#target-connection-notice"), list = element("div", { attrs: { "data-device-list": "" } });
  const accountText = element("p"), feedback = element("p", { className: "device-feedback", attrs: { role: "status" } });
  const description = element("p", { className: "account-note" }), refreshButton = element("button", { className: "secondary-button", attrs: { type: "button" } });
  const closeButton = element("button", { className: "secondary-button", attrs: { type: "button" } });
  let account = null, snapshot = null, busy = false, timer = 0, layer = null, opening = null, disposed = false;
  let operation = null, selection = null, generation = 0, lifetime = new AbortController(), lastSync = 0, message = "", tone = "";
  const notices = new Map(), rowsById = new Map(), confirmedChanges = new Map();
  const synced = element("small", { className: "device-synced", attrs: { role: "status" } });
  const ownerId = () => account?.profile?.id ? String(account.profile.id) : "";
  const pendingJob = () => ["queued", "running"].includes(snapshot?.job?.state);
  const current = task => !disposed && task.owner === ownerId();
  const copy = (key, params, fallback) => t(`devices.${key}`, params, fallback);
  const signedIn = () => isAccountSignedIn(account) && account.profile?.id;
  const login = createLoginWindow({ scope: () => "controller", allowBrowser: () => true,
    request: { get: async (path, options) => ({ data: await connectorRequest(path, undefined, options) }),
      post: async (path, body, options) => ({ data: await connectorRequest(path, body, options) }) },
    onSnapshot: applyAccount });
  function applyAccount(next) {
    const owner = next?.profile?.id ? String(next.profile.id) : "";
    if (owner !== ownerId()) {
      generation++; lifetime.abort(); lifetime = new AbortController();
      snapshot = null; operation = null; selection = null; lastSync = 0; busy = false; message = "";
      notices.clear(); confirmedChanges.clear(); rowsById.clear(); list.replaceChildren();
    }
    account = next;
  }
  function renderTarget() {
    const state = targetState(), name = state.selected?.name || copy("local", {}, "本机");
    const title = copy("target", { name }, `目标设备：${name}`);
    const readonly = state.selected?.mode === "view";
    const mobileTarget = document.querySelector("#mobile-target-label");
    mobileTarget.hidden = !state.selected;
    mobileTarget.textContent = state.selected ? name + (readonly ? ` · ${copy("view", {}, "只读查看")}` : "") : "";
    mobileTarget.title = title;
    button.querySelector("span").textContent = name + (readonly ? ` · ${copy("view", {}, "只读查看")}` : "");
    settingsTarget.hidden = !state.selected;
    settingsTarget.textContent = title + (readonly ? ` · ${copy("view", {}, "只读查看")}` : "");
    button.dataset.remote = String(Boolean(state.selected));
    const route = state.route === "xrudp" ? copy("xrudp", {}, "设备直连") :
      state.route === "lan" ? copy("lan", {}, "局域网直连") : copy("relay", {}, "服务器中转");
    button.title = state.selected ? copy(state.connected ? "target" : "offline", { name }, `目标设备：${name}`) +
      (state.connected ? ` · ${route}` : "") : copy("choose", {}, "选择设备");
    button.dataset.route = state.selected && state.connected ? state.route || "relay" : "local";
    if (state.selected && state.connected) settingsTarget.textContent += ` · ${route}`;
    banner.hidden = !state.selected || (state.connected && !state.runtimeChanged);
    banner.querySelector("p").textContent = copy("offline", { name }, `目标设备 ${name} 正在连接或已离线；操作不会转到本机。`) +
      (state.error ? ` ${errorMessage(state.error)}` : "");
    if (state.runtimeChanged) banner.querySelector("p").textContent = copy("runtimeChanged", {}, "目标设备已重启，请保留未保存草稿并重新载入页面；旧写入不会重放。");
    banner.querySelector("[data-target-retry]").textContent = copy(state.runtimeChanged ? "reload" : "retry", {}, state.runtimeChanged ? "重新载入" : "重新连接");
    banner.querySelector("[data-target-retry]").disabled = Boolean(state.permissionRevoked);
  }
  function busyState(value) {
    busy = value;
    refreshButton.disabled = value || !signedIn();
    refreshButton.setAttribute("aria-busy", String(value || pendingJob()));
    for (const node of list.querySelectorAll("button")) node.disabled = value || node.dataset.available === "false" || Boolean((operation || pendingJob()) && node.dataset.inspect !== "true");
  }
  function actionButton(label, action, available = true, key = "", deviceId = "", inspect = false) {
    const own = generation, owner = ownerId();
    const node = element("button", { text: label, className: "secondary-button", attrs: { type: "button" } });
    node.dataset.available = String(available); node.dataset.action = key; node.dataset.deviceId = deviceId; node.dataset.inspect = String(inspect);
    node.disabled = busy || !available || Boolean(operation && !inspect);
    const active = operation?.device.id === deviceId && operation.action === key && operation.phase !== "unknown" || selection?.id === deviceId && selection.action === key;
    node.setAttribute("aria-busy", String(active));
    if (active) node.prepend(element("span", { className: "device-spinner", attrs: { "aria-hidden": "true" } }));
    node.addEventListener("click", () => { if (layer && !disposed && node.isConnected && !node.disabled && own === generation && owner === ownerId()) void perform(action); }); return node;
  }
  function schedule() {
    clearTimeout(timer);
    if (!layer || disposed || document.hidden || operation?.phase === "unknown" && operation.attempts >= 2) return;
    timer = setTimeout(() => void perform(listDevices), pendingJob() ? 1000 : 10000);
  }
  async function perform(action) {
    if (busy || disposed || !layer) return;
    const own = generation; clearTimeout(timer); busyState(true); render();
    try { await action(); }
    catch (error) {
      if (own !== generation || disposed || error.name === "AbortError") return;
      if (operation && !operation.confirmed) {
        operation.jobId ||= error.jobId;
        if (error.outcomeUnknown) {
          operation.phase = "unknown";
          notices.set(operation.device.id, { text: copy("outcomeUnknown", {}, "操作结果尚未确认，请核对结果；不会自动重复提交。") + " " + errorMessage(error), tone: "warning" });
        } else {
          notices.set(operation.device.id, { text: errorMessage(error), tone: "error", retry: operation.action }); operation = null;
        }
      } else { message = errorMessage(error); tone = "error"; }
      if (error.code === "target_switch_busy") switchFailure = error;
    }
    finally {
      if (own === generation && !disposed) { busyState(false); render(); schedule(); }
    }
  }
  let switchFailure = null;
  const progress = action => copy(`${action}Progress`, {}, ({ revoke: "正在撤销…", remove: "正在移除…", connect: "正在连接…", view: "正在连接…" })[action]);
  async function choose(device, mode, options = {}) {
    if (operation || pendingJob()) return;
    selection = { id: device?.id || "local", action: mode === "view" ? "view" : "connect" };
    switchFailure = null; message = progress(selection.action); tone = ""; render();
    try { await requestTargetSwitch(device ? { id: device.id, name: device.name, mode, owner: ownerId() } : null, options); }
    finally { selection = null; }
  }
  function confirmed(state, task) {
    if (!current(task)) return;
    confirmedChanges.set(task.device.id, task.action);
    // The success receipt confirms this change even if the subsequent full-list
    // synchronization is temporarily unavailable. Do not roll it back to cache.
    takeState(state);
    task.confirmed = true; task.phase = "confirmed";
    message = copy(task.action === "remove" ? "removedDevice" : "revokedDevice", { name: task.device.name }, task.action === "remove" ? "已移除「{name}」" : "已撤销「{name}」的远控授权");
    tone = "success"; notices.set(task.device.id, { text: message, tone });
    revokeTarget(task.device.id, task.owner); toast(message);
    render();
  }
  function takeState(state, fresh = false) {
    if (fresh) {
      for (const id of confirmedChanges.keys()) if (notices.get(id)?.tone === "warning") notices.delete(id);
      if (confirmedChanges.size && tone === "warning") { message = ""; tone = ""; }
      confirmedChanges.clear();
    }
    const listing = Array.isArray(state?.listing?.devices) ? state.listing : snapshot?.listing;
    snapshot = state;
    if (listing && confirmedChanges.size) snapshot = { ...state, listing: { ...listing, devices: listing.devices
      .filter(d => confirmedChanges.get(d.id) !== "remove")
      .map(d => confirmedChanges.get(d.id) === "revoke" ? { ...d, revoked: true, allow_remote: false } : d) } };
  }
  async function finishMutation(state, task, alreadySynced = false) {
    confirmed(state, task);
    if (!current(task)) return;
    try { if (!alreadySynced) await synchronize(false); }
    catch (error) {
      if (current(task) && error.name !== "AbortError") {
        message = copy("changedSyncFailed", {}, "操作已成功，设备列表暂未同步。可刷新列表核对。") + " " + errorMessage(error); tone = "warning";
        notices.set(task.device.id, { text: message, tone });
      }
    } finally { if (operation === task) operation = null; }
  }
  async function mutate(action, device) {
    if (operation || pendingJob() || !signedIn()) return;
    const task = operation = { action, device, owner: ownerId(), phase: "running", attempts: 0, signal: lifetime.signal };
    notices.delete(device.id); message = ""; switchFailure = null; render();
    const state = await connectorJob({ action, device_id: device.id }, { signal: task.signal,
      onQueued(id) { if (current(task)) task.jobId = id; } });
    await refreshAccount(task.signal);
    await finishMutation(state, task);
  }
  async function inspectOperation() {
    const task = operation; if (!task || !current(task)) return;
    task.attempts++; task.phase = "checking"; render();
    try {
      await refresh(); if (!current(task)) return;
      const job = snapshot?.job;
      if (job?.id === task.jobId || !task.jobId && job?.action === task.action && job.device_id === task.device.id) {
        task.jobId = job.id;
        try {
          const state = await connectorWatchJob(job.id, { signal: task.signal, action: task.action });
          await refreshAccount(task.signal); await finishMutation(state, task); return;
        } catch (error) {
          if (!error.outcomeUnknown) {
            notices.set(task.device.id, { text: errorMessage(error), tone: "error", retry: task.action }); operation = null; throw error;
          }
        }
      }
      // A terminal/overwritten receipt can be reconciled by a fresh authoritative
      // list. An active different job must finish before using the shared lane.
      await refresh(); if (!current(task)) return;
      if (pendingJob()) { task.phase = "unknown"; return; }
      await synchronize(false); if (!current(task)) return;
      const devices = snapshot?.listing?.devices;
      if (!Array.isArray(devices)) throw Error(copy("listUnavailable", {}, "暂时无法读取设备列表"));
      const device = devices.find(d => d.id === task.device.id);
      if (task.action === "remove" ? !device : device?.revoked) await finishMutation(snapshot, task, true);
      else {
        notices.set(task.device.id, { text: copy("changeNotApplied", {}, "已核对设备状态，操作未生效，可以重试。"), tone: "error", retry: task.action }); operation = null;
      }
    } catch (error) { if (current(task) && operation === task) error.outcomeUnknown = true; throw error; }
  }
  function render() {
    if (disposed) return;
    renderTarget(); if (!layer) return;
    const scroll = layer.body.scrollTop, focused = document.activeElement;
    layer?.update({ title: copy("choose", {}, "选择设备") });
    description.textContent = copy("connectorDescription", {}, "连接器账号只用于选择设备；目标设备内的账号和所有设置均在目标上操作。");
    refreshButton.textContent = operation?.phase === "unknown" ? copy("inspect", {}, "核对操作结果") : busy ? copy("refreshing", {}, "正在更新…") : copy("refresh", {}, "刷新设备列表"); closeButton.textContent = copy("close", {}, "关闭");
    feedback.textContent = message; feedback.dataset.tone = tone; feedback.hidden = !message && !switchFailure;
    if (switchFailure) feedback.append(actionButton(copy("copySwitch", {}, "复制未保存草稿并切换"), () => {
      const next = switchFailure.details.target; return choose(next, next?.mode, { copyDrafts: true });
    }));
    synced.textContent = lastSync ? copy("syncedAt", { time: new Date(lastSync).toLocaleTimeString(currentLocale()) }, "最近同步：{time}") : "";
    const name = account?.profile?.nickname || account?.profile?.username || "";
    accountText.textContent = signedIn() ? copy("account", { name }, `连接器账号：${name}`) : copy("login", {}, "登录控制端账号，查看自己的设备。");
    const local = rowsById.get("local") || element("div", { className: "device-row" }); rowsById.set("local", local); local.replaceChildren();
    local.append(element("strong", { text: copy("local", {}, "本机") }),
      actionButton(copy(targetState().selected ? "returnLocal" : "current", {}, "返回本机"), () => choose(null), Boolean(targetState().selected)));
    const rows = [local];
    if (!signedIn()) rows.push(actionButton(t("account.login"), async () => { layer?.dispose(); await open(); }));
    for (const device of signedIn() ? snapshot?.listing?.devices || [] : []) {
      const thisDevice = device.id === snapshot.device_id;
      const selected = Boolean(targetState().selected && targetState().selected.id === device.id);
      const currentMode = selected ? targetState().selected.mode : null;
      const available = device.online && device.allow_remote && !device.revoked;
      const row = rowsById.get(device.id) || element("div", { className: "device-row" }); rowsById.set(device.id, row); row.dataset.deviceId = device.id;
      const note = notices.get(device.id), task = operation?.device.id === device.id ? operation : null;
      const target = targetState(), stateKey = JSON.stringify([device, thisDevice, selected, target.connected, target.error?.code, target.runtimeChanged, task?.phase, task?.action, selection, busy, note, copy("connect", {}, "连接")]);
      if (row.deviceStateKey === stateKey) { rows.push(row); continue; } row.deviceStateKey = stateKey;
      const labels = element("div", { className: "device-labels" });
      const state = device.revoked ? copy("revoked", {}, "已撤销") :
        !device.allow_remote ? copy("disabled", {}, "未允许远控") : device.online ? copy("online", {}, "在线") : copy("offlineShort", {}, "离线");
      const connection = selected ? target.connected ? copy(currentMode === "view" ? "view" : "controlling", {}, currentMode === "view" ? "只读查看" : "正在控制") : target.error ? copy("disconnected", {}, "已断开") : copy("connecting", {}, "连接中") : "";
      labels.append(element("strong", { text: device.name }), element("small", { text: [device.platform, state, thisDevice ? copy("current", {}, "当前设备") : "", connection].filter(Boolean).join(" · ") }));
      const actions = element("div", { className: "device-actions" });
      if (!thisDevice) {
        actions.append(actionButton(selection?.id === device.id && selection.action === "connect" ? progress("connect") : copy("connect", {}, "连接"), () => choose(device, "control"), available && !device.controlled && !(currentMode === "control" && target.connected), "connect", device.id),
          actionButton(selection?.id === device.id && selection.action === "view" ? progress("view") : copy("view", {}, "只读查看"), () => choose(device, "view"), available && !(currentMode === "view" && target.connected) && device.viewers < 3, "view", device.id));
      }
      const action = device.revoked ? "remove" : "revoke";
      actions.append(actionButton(task?.phase === "running" && task.action === action ? progress(action) : copy(action, {}, action === "remove" ? "移除" : "撤销远控"), () => mutate(action, device), true, action, device.id));
      if (task?.phase === "unknown") actions.append(actionButton(copy("inspect", {}, "核对操作结果"), inspectOperation, true, "inspect", device.id, true));
      else if (note?.retry) actions.append(actionButton(copy("retryAction", {}, "重试"), () => mutate(note.retry, device), true, "retry", device.id));
      const status = task && task.phase !== "confirmed" ? element("small", { className: "device-operation", text: task.phase === "unknown" ? note?.text : task.phase === "checking" ? copy("checking", {}, "正在核对结果…") : progress(task.action), attrs: { role: "status" } }) : note ? element("small", { className: "device-operation", text: note.text, attrs: { role: note.tone === "error" ? "alert" : "status" } }) : null;
      if (status) { status.dataset.tone = task?.phase === "unknown" ? "warning" : note?.tone || ""; labels.append(status); }
      row.setAttribute("aria-busy", String(Boolean(task && task.phase !== "unknown" && task.phase !== "confirmed")));
      row.replaceChildren(labels, actions); rows.push(row);
    }
    if (signedIn() && !snapshot?.listing?.devices?.length)
      rows.push(element("p", { text: copy("empty", {}, "在需要被控制的设备上登录同一账号，并在设置中开启“允许远程控制”。") }));
    const keep = new Set(rows);
    for (const row of [...list.children]) if (!keep.has(row)) row.remove();
    for (let i = 0; i < rows.length; i++) if (list.children[i] !== rows[i]) {
      if (list.children[i]) list.insertBefore(rows[i], list.children[i]); else list.append(rows[i]);
    }
    for (const [id, row] of rowsById) if (!keep.has(row)) rowsById.delete(id);
    busyState(busy); layer.body.scrollTop = scroll;
    if (focused?.dataset?.action && !focused.isConnected) {
      const replacement = [...list.querySelectorAll("button")].find(n => n.dataset.action === focused.dataset.action && n.dataset.deviceId === focused.dataset.deviceId);
      if (replacement && !replacement.disabled) replacement.focus({ preventScroll: true });
    }
  }
  async function refreshAccount(signal = lifetime.signal) {
    const next = await connectorRequest("/account", undefined, { signal, timeoutMs: 10000 });
    if (disposed || signal.aborted) throw new DOMException("Device list closed", "AbortError");
    applyAccount(next); return next;
  }
  async function refresh() {
    await refreshAccount(); const owner = ownerId(), options = { signal: lifetime.signal, timeoutMs: 10000 };
    if (account.state === "refreshing" && account.refresh_available && !account.busy)
      await connectorRequest("/account/refresh", {}, options);
    const state = await connectorRequest("/connector/state", undefined, options);
    if (disposed || owner !== ownerId() || options.signal.aborted) return;
    takeState(state);
    render();
  }
  async function synchronize(read = true) {
    if (read) await refresh(); if (!signedIn() || pendingJob()) return;
    const owner = ownerId(), signal = lifetime.signal;
    const state = await connectorJob({ action: "refresh" }, { signal });
    await refreshAccount(signal);
    if (!disposed && owner === ownerId()) {
      takeState(state, true); lastSync = Date.now();
      if (tone === "error" && !switchFailure) { message = ""; tone = ""; }
      render();
    }
  }
  async function listDevices() {
    if (operation) { if (operation.phase === "unknown") await inspectOperation(); return; }
    await refresh();
    if (!signedIn()) return;
    const job = snapshot?.job;
    if (pendingJob() && ["revoke", "remove"].includes(job.action) && job.device_id) {
      const device = snapshot.listing?.devices?.find(d => d.id === job.device_id) || { id: job.device_id, name: job.device_id };
      operation = { action: job.action, device, owner: ownerId(), jobId: job.id, signal: lifetime.signal, phase: "unknown", attempts: 0 };
      await inspectOperation(); return;
    }
    await synchronize(false);
  }
  function open() {
    if (disposed || layer) return Promise.resolve();
    if (opening) return opening;
    opening = (async () => {
      try {
        applyAccount(await connectorRequest("/account"));
        if (disposed) return;
        if (!signedIn()) {
          if (!await login.open(account) || disposed) return;
          window.dispatchEvent(new Event("mdo-controller-account-changed"));
        }
        if (disposed) return;
        layer = openLayer({ title: copy("choose", {}, "选择设备"), icon: "monitor", width: "48rem", height: "75%",
          onClosed() {
            layer = null; clearTimeout(timer); generation++;
            // Discard listing reads and detached actions without cancelling a
            // submitted remote mutation, whose receipt still needs reconciliation.
            if (!operation) {
              lifetime.abort(); lifetime = new AbortController(); busy = false; selection = null;
            }
          } });
        layer.element.id = "devices-dialog";
        layer.body.append(accountText, description, feedback, list); layer.footer.append(synced, refreshButton, closeButton);
        render(); void perform(listDevices);
      } catch (error) { if (!disposed) toast(errorMessage(error), "error"); }
      finally { opening = null; }
    })();
    return opening;
  }
  button.addEventListener("click", open);
  settingsTarget.addEventListener("click", open);
  refreshButton.addEventListener("click", () => void perform(listDevices));
  closeButton.addEventListener("click", () => { void layer?.close(); });
  async function switchFailed(error, next) {
    toast(errorMessage(error), "error"); await open();
    if (!layer) return;
    feedback.textContent = errorMessage(error);
    if (error.code === "target_switch_busy") feedback.append(actionButton(copy("copySwitch", {}, "复制未保存草稿并切换"),
      () => requestTargetSwitch(next, { copyDrafts: true })));
  }
  banner.querySelector("[data-target-local]").addEventListener("click", () => void requestTargetSwitch(null).catch(error => switchFailed(error, null)));
  banner.querySelector("[data-target-retry]").addEventListener("click", () => {
    if (targetState().runtimeChanged) {
      const next = targetState().selected;
      void requestTargetSwitch(next).catch(error => switchFailed(error, next));
    } else void initializeTarget();
  });
  banner.querySelector("[data-target-choose]").addEventListener("click", open);
  const wake = () => { if (layer && !document.hidden && !busy) void perform(listDevices); };
  const visibility = () => { if (document.hidden) clearTimeout(timer); else wake(); };
  const accountChanged = () => { if (layer || operation) void refreshAccount().then(() => { render(); wake(); }).catch(() => {}); };
  window.addEventListener("focus", wake); document.addEventListener("visibilitychange", visibility); window.addEventListener("mdo-controller-account-changed", accountChanged);
  const targetSubscription = subscribeTarget(render), locale = subscribeLocale(render);
  return { open, refresh, dispose() { disposed = true; lifetime.abort(); clearTimeout(timer); login.dispose(); layer?.dispose(); targetSubscription(); locale(); window.removeEventListener("focus", wake); document.removeEventListener("visibilitychange", visibility); window.removeEventListener("mdo-controller-account-changed", accountChanged); button.removeEventListener("click", open); settingsTarget.removeEventListener("click", open); } };
}
