import { connectorRequest, connectorJob } from "../../api/connector.js";
import { targetState, subscribeTarget, requestTargetSwitch, initializeTarget } from "../../api/target.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { t, subscribeLocale } from "../../i18n.js";

export function createDevices() {
  const button = document.querySelector("#open-devices"), dialog = document.querySelector("#devices-dialog");
  const settingsTarget = document.querySelector("#settings-target");
  const banner = document.querySelector("#target-connection-notice"), list = dialog.querySelector("[data-device-list]");
  const accountText = dialog.querySelector("[data-device-account]"), feedback = dialog.querySelector("[data-device-feedback]");
  const form = dialog.querySelector("form"), refreshButton = dialog.querySelector("[data-device-refresh]");
  let account = null, snapshot = null, busy = false, timer = 0, returnFocus = null;
  const copy = (key, params, fallback) => t(`devices.${key}`, params, fallback);
  const signedIn = () => account?.state === "signed_in" && account.profile?.id;
  function renderTarget() {
    const state = targetState(), name = state.selected?.name || copy("local", {}, "本机");
    const title = copy("target", { name }, `目标设备：${name}`);
    const readonly = state.selected?.mode === "view";
    button.querySelector("span").textContent = name + (readonly ? ` · ${copy("view", {}, "只读查看")}` : "");
    settingsTarget.hidden = !state.selected;
    settingsTarget.textContent = title + (readonly ? ` · ${copy("view", {}, "只读查看")}` : "");
    button.dataset.remote = String(Boolean(state.selected));
    button.title = state.selected ? copy(state.connected ? "target" : "offline", { name }, `目标设备：${name}`) : copy("choose", {}, "选择设备");
    banner.hidden = !state.selected || (state.connected && !state.runtimeChanged);
    banner.querySelector("p").textContent = copy("offline", { name }, `目标设备 ${name} 正在连接或已离线；操作不会转到本机。`) +
      (state.error ? ` ${errorMessage(state.error)}` : "");
    if (state.runtimeChanged) banner.querySelector("p").textContent = copy("runtimeChanged", {}, "目标设备已重启，请保留未保存草稿并重新载入页面；旧写入不会重放。");
    banner.querySelector("[data-target-retry]").textContent = copy(state.runtimeChanged ? "reload" : "retry", {}, state.runtimeChanged ? "重新载入" : "重新连接");
  }
  function busyState(value) {
    busy = value; form.querySelector("[type=submit]").disabled = value;
    refreshButton.disabled = value || !signedIn();
    for (const node of dialog.querySelectorAll("button[data-available]")) node.disabled = value || node.dataset.available === "false";
  }
  function actionButton(label, action, available = true) {
    const node = element("button", { text: label, className: "secondary-button", attrs: { type: "button" } });
    node.dataset.available = String(available); node.setAttribute("data-available", String(available)); node.disabled = busy || !available;
    node.addEventListener("click", () => void perform(action)); return node;
  }
  async function perform(action) {
    if (busy) return; busyState(true); feedback.textContent = "";
    try { await action(); }
    catch (error) {
      feedback.textContent = errorMessage(error);
      if (error.code === "target_switch_busy") feedback.append(actionButton(copy("copySwitch", {}, "复制未保存草稿并切换"),
        () => requestTargetSwitch(error.details.target, { copyDrafts: true })));
    }
    finally {
      busyState(false); clearTimeout(timer);
      if (dialog.open) timer = setTimeout(() => void perform(listDevices), 10000);
    }
  }
  function render() {
    const name = account?.profile?.nickname || account?.profile?.username || "";
    accountText.textContent = signedIn() ? copy("account", { name }, `连接器账号：${name}`) : copy("login", {}, "登录控制端账号，查看自己的设备。");
    form.hidden = Boolean(signedIn());
    form.elements.remember.disabled = account?.persistence_available === false;
    if (account?.persistence_available === false) form.elements.remember.checked = false;
    const local = element("div", { className: "device-row" });
    local.append(element("strong", { text: copy("local", {}, "本机") }),
      actionButton(copy(targetState().selected ? "returnLocal" : "current", {}, "返回本机"), () => requestTargetSwitch(null), Boolean(targetState().selected)));
    const rows = [local];
    for (const device of snapshot?.listing?.devices || []) {
      const thisDevice = device.id === snapshot.device_id;
      const selected = targetState().selected?.id === device.id;
      const available = device.online && device.allow_remote && !device.revoked;
      const row = element("div", { className: "device-row" });
      const labels = element("div", { className: "device-labels" });
      const state = thisDevice ? copy("current", {}, "当前设备") : device.revoked ? copy("revoked", {}, "已撤销") :
        !device.allow_remote ? copy("disabled", {}, "未允许远控") : device.online ? copy("online", {}, "在线") : copy("offlineShort", {}, "离线");
      labels.append(element("strong", { text: device.name }), element("small", { text: `${device.platform} · ${state}${selected ? " · ✓" : ""}` }));
      const actions = element("div", { className: "device-actions" });
      const choose = mode => requestTargetSwitch({ id: device.id, name: device.name, mode, owner: String(account.profile.id) });
      if (!thisDevice) {
        actions.append(actionButton(copy("connect", {}, "连接"), () => choose("control"), available && !device.controlled && !selected),
          actionButton(copy("view", {}, "只读查看"), () => choose("view"), available && !selected && device.viewers < 3));
      }
      if (!device.revoked) actions.append(actionButton(copy("revoke", {}, "撤销远控"), async () => {
        await connectorJob({ action: "revoke", device_id: device.id }); await refresh();
      }));
      else actions.append(actionButton(copy("remove", {}, "移除"), async () => {
        await connectorJob({ action: "remove", device_id: device.id }); await refresh();
      }));
      row.append(labels, actions); rows.push(row);
    }
    if (signedIn() && !snapshot?.listing?.devices?.length)
      rows.push(element("p", { text: copy("empty", {}, "在需要被控制的设备上登录同一账号，并在设置中开启“允许远程控制”。") }));
    list.replaceChildren(...rows); busyState(busy); renderTarget();
  }
  async function refresh() {
    account = await connectorRequest("/account");
    if (account.state === "refreshing" && account.refresh_available && !account.busy)
      await connectorRequest("/account/refresh", {});
    snapshot = await connectorRequest("/connector/state");
    render();
  }
  async function listDevices() { await refresh(); if (signedIn()) { snapshot = await connectorJob({ action: "refresh" }); render(); } }
  function open() {
    if (dialog.open) return;
    returnFocus = document.activeElement; dialog.showModal();
    void perform(listDevices);
  }
  form.addEventListener("submit", event => {
    event.preventDefault();
    const password = form.elements.password.value; form.elements.password.value = "";
    void perform(async () => {
      await connectorRequest("/account/login", { identifier: form.elements.identifier.value.trim(), password, remember: form.elements.remember.checked });
      const until = Date.now() + 20000;
      while (Date.now() < until) {
        await refresh();
        if (signedIn() && !account.busy) { await listDevices(); return; }
        if (!account.busy && account.state !== "signing_in") throw new Error(copy("loginFailed", {}, "登录失败，请检查账号和密码。"));
        await new Promise(resolve => setTimeout(resolve, 400));
      }
      throw new Error(copy("loginFailed", {}, "登录失败，请检查账号和密码。"));
    });
  });
  button.addEventListener("click", open);
  settingsTarget.addEventListener("click", open);
  document.querySelector("#update-target")?.addEventListener("click", open);
  refreshButton.addEventListener("click", () => void perform(listDevices));
  dialog.querySelector("[data-device-close]").addEventListener("click", () => dialog.close());
  dialog.addEventListener("close", () => { form.elements.password.value = ""; clearTimeout(timer); returnFocus?.focus(); });
  function switchFailed(error, next) {
    toast(errorMessage(error), "error"); open();
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
  subscribeTarget(renderTarget); subscribeLocale(render);
  return { open, refresh };
}
