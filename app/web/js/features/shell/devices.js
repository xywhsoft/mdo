import { connectorRequest, connectorJob } from "../../api/connector.js";
import { targetState, subscribeTarget, requestTargetSwitch, initializeTarget } from "../../api/target.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { t, subscribeLocale } from "../../i18n.js";
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
  const copy = (key, params, fallback) => t(`devices.${key}`, params, fallback);
  const signedIn = () => isAccountSignedIn(account) && account.profile?.id;
  const login = createLoginWindow({ scope: () => "controller", allowBrowser: () => true,
    request: { get: async (path, options) => ({ data: await connectorRequest(path, undefined, options) }),
      post: async (path, body, options) => ({ data: await connectorRequest(path, body, options) }) },
    onSnapshot(next) { account = next; } });
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
  }
  function busyState(value) {
    busy = value;
    refreshButton.disabled = value || !signedIn();
    for (const node of list.querySelectorAll("button")) node.disabled = value || node.dataset.available === "false";
  }
  function actionButton(label, action, available = true) {
    const node = element("button", { text: label, className: "secondary-button", attrs: { type: "button" } });
    node.dataset.available = String(available); node.setAttribute("data-available", String(available)); node.disabled = busy || !available;
    const owner = layer;
    node.addEventListener("click", () => { if (owner && owner === layer && !disposed) void perform(action); }); return node;
  }
  async function perform(action) {
    if (busy || disposed || !layer) return; const owner = layer; busyState(true); feedback.textContent = "";
    try { await action(); }
    catch (error) {
      if (layer !== owner || error.name === "AbortError") return;
      feedback.textContent = errorMessage(error);
      if (error.code === "target_switch_busy") feedback.append(actionButton(copy("copySwitch", {}, "复制未保存草稿并切换"),
        () => requestTargetSwitch(error.details.target, { copyDrafts: true })));
    }
    finally {
      if (layer === owner) { busyState(false); clearTimeout(timer); timer = setTimeout(() => void perform(listDevices), 10000); }
    }
  }
  function render() {
    if (disposed) return;
    layer?.update({ title: copy("choose", {}, "选择设备") });
    description.textContent = copy("connectorDescription", {}, "连接器账号只用于选择设备；目标设备内的账号和所有设置均在目标上操作。");
    refreshButton.textContent = copy("refresh", {}, "刷新设备列表"); closeButton.textContent = copy("close", {}, "关闭");
    const name = account?.profile?.nickname || account?.profile?.username || "";
    accountText.textContent = signedIn() ? copy("account", { name }, `连接器账号：${name}`) : copy("login", {}, "登录控制端账号，查看自己的设备。");
    const local = element("div", { className: "device-row" });
    local.append(element("strong", { text: copy("local", {}, "本机") }),
      actionButton(copy(targetState().selected ? "returnLocal" : "current", {}, "返回本机"), () => requestTargetSwitch(null), Boolean(targetState().selected)));
    const rows = [local];
    if (!signedIn()) rows.push(actionButton(t("account.login"), async () => { layer?.dispose(); await open(); }));
    for (const device of signedIn() ? snapshot?.listing?.devices || [] : []) {
      const thisDevice = device.id === snapshot.device_id;
      const selected = targetState().selected?.id === device.id;
      const currentMode = selected ? targetState().selected.mode : null;
      const available = device.online && device.allow_remote && !device.revoked;
      const row = element("div", { className: "device-row" });
      const labels = element("div", { className: "device-labels" });
      const state = thisDevice ? copy("current", {}, "当前设备") : device.revoked ? copy("revoked", {}, "已撤销") :
        !device.allow_remote ? copy("disabled", {}, "未允许远控") : device.online ? copy("online", {}, "在线") : copy("offlineShort", {}, "离线");
      labels.append(element("strong", { text: device.name }), element("small", { text: `${device.platform} · ${state}${selected ? " · ✓" : ""}` }));
      const actions = element("div", { className: "device-actions" });
      const choose = mode => requestTargetSwitch({ id: device.id, name: device.name, mode, owner: String(account.profile.id) });
      if (!thisDevice) {
        actions.append(actionButton(copy("connect", {}, "连接"), () => choose("control"), available && !device.controlled && currentMode !== "control"),
          actionButton(copy("view", {}, "只读查看"), () => choose("view"), available && currentMode !== "view" && device.viewers < 3));
      }
      if (!device.revoked) actions.append(actionButton(copy("revoke", {}, "撤销远控"), async () => {
        await connectorJob({ action: "revoke", device_id: device.id }, { signal: layer.signal }); await refresh();
      }));
      else actions.append(actionButton(copy("remove", {}, "移除"), async () => {
        await connectorJob({ action: "remove", device_id: device.id }, { signal: layer.signal }); await refresh();
      }));
      row.append(labels, actions); rows.push(row);
    }
    if (signedIn() && !snapshot?.listing?.devices?.length)
      rows.push(element("p", { text: copy("empty", {}, "在需要被控制的设备上登录同一账号，并在设置中开启“允许远程控制”。") }));
    list.replaceChildren(...rows); busyState(busy); renderTarget();
  }
  async function refresh() {
    const owner = layer, options = { signal: owner?.signal };
    const next = await connectorRequest("/account", undefined, options);
    if (disposed || owner !== layer) return;
    account = next;
    if (account.state === "refreshing" && account.refresh_available && !account.busy)
      await connectorRequest("/account/refresh", {}, options);
    const state = await connectorRequest("/connector/state", undefined, options);
    if (disposed || owner !== layer) return;
    snapshot = state;
    render();
  }
  async function listDevices() { const owner = layer; await refresh(); if (owner && owner === layer && signedIn()) { const state = await connectorJob({ action: "refresh" }, { signal: owner.signal }); if (!disposed && owner === layer) { snapshot = state; render(); } } }
  function open() {
    if (disposed || layer) return Promise.resolve();
    if (opening) return opening;
    opening = (async () => {
      try {
        account = await connectorRequest("/account");
        if (disposed) return;
        if (!signedIn()) {
          if (!await login.open(account) || disposed) return;
          window.dispatchEvent(new Event("mdo-controller-account-changed"));
        }
        if (disposed) return;
        layer = openLayer({ title: copy("choose", {}, "选择设备"), icon: "monitor", width: "48rem", height: "75%",
          onClosed() { layer = null; busy = false; clearTimeout(timer); } });
        layer.element.id = "devices-dialog";
        feedback.textContent = ""; list.replaceChildren();
        layer.body.append(accountText, description, feedback, list); layer.footer.append(refreshButton, closeButton);
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
  const targetSubscription = subscribeTarget(renderTarget), locale = subscribeLocale(render);
  return { open, refresh, dispose() { disposed = true; clearTimeout(timer); login.dispose(); layer?.dispose(); targetSubscription(); locale(); button.removeEventListener("click", open); settingsTarget.removeEventListener("click", open); } };
}
