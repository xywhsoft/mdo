import { api } from "../../api/client.js";
import { isRemoteTarget, targetState, subscribeTarget } from "../../api/target.js";
import { errorMessage } from "../../utils/dom.js";
import { t, subscribeLocale } from "../../i18n.js";

export function createRemotePanel() {
  const panel = document.querySelector("#remote-panel"), form = panel.querySelector("form");
  const status = panel.querySelector("[data-remote-status]"), submit = form.querySelector("[type=submit]");
  let snapshot = null, pending = false, timer = 0, dirty = false, failure = "";
  function render() {
    if (!snapshot) return;
    const stages = { disabled: "已关闭", connecting: "正在连接", reconnecting: "正在重连", online: "在线", revoked: "授权已撤销" };
    status.textContent = t(`devices.stage.${snapshot.stage}`, {}, stages[snapshot.stage] || snapshot.stage);
    if (snapshot.error) status.textContent += ` · ${snapshot.error}`;
    if (failure) status.textContent += ` · ${failure}`;
    if (snapshot.allow_remote && !snapshot.persistent)
      status.textContent += " · " + t("devices.temporary", {}, "此平台只在本次运行中保持授权");
    panel.querySelector("[data-remote-target]").textContent = isRemoteTarget() ?
      t("devices.targetSettings", { name: targetState().selected.name }, `这些设置作用于目标设备 ${targetState().selected.name}。`) :
      t("devices.localSettings", {}, "开启后，此设备主动连接中继；同账号的其他设备可以控制它。");
    const readonly = isRemoteTarget() && (targetState().selected.mode === "view" ||
      !targetState().connected || targetState().runtimeChanged);
    for (const control of form.elements) control.disabled = pending || readonly;
    submit.disabled = pending || readonly;
  }
  async function refresh() {
    clearTimeout(timer);
    try {
      snapshot = (await api.get("/remote")).data;
      if (!dirty) { form.elements.device_name.value = snapshot.name || ""; form.elements.allow_remote.checked = snapshot.allow_remote; }
      render();
    } catch (error) { status.textContent = errorMessage(error); }
    finally { timer = setTimeout(refresh, pending ? 500 : snapshot?.allow_remote || !panel.hidden ? 10000 : 30000); }
  }
  form.addEventListener("input", () => { dirty = true; });
  form.addEventListener("submit", async event => {
    event.preventDefault(); if (pending) return;
    const allow = form.elements.allow_remote.checked, name = form.elements.device_name.value.trim();
    if (allow && (!name || new TextEncoder().encode(name).length > 96)) {
      status.textContent = t("devices.nameInvalid", {}, "设备名称须为 1–96 个 UTF-8 字节。"); return;
    }
    pending = true; failure = ""; render();
    try {
      const next = (await api.post("/remote", allow ? { allow_remote: true, name } : { allow_remote: false })).data;
      const until = Date.now() + 20000, job = next.job.id;
      while (Date.now() < until) {
        const state = (await api.get("/remote")).data;
        if (state.job.id !== job || state.job.state === "failed") throw new Error(state.error || "Device operation changed");
        if (state.job.state === "done") { snapshot = state; dirty = false; break; }
        await new Promise(resolve => setTimeout(resolve, 300));
      }
      if (dirty) throw new Error(t("devices.operationTimeout", {}, "设备操作超时，请重新读取状态后再操作。"));
    } catch (error) { failure = errorMessage(error); }
    finally { pending = false; render(); await refresh(); }
  });
  subscribeLocale(render); subscribeTarget(render); void refresh();
  window.addEventListener("pagehide", () => clearTimeout(timer), { once: true });
  return { hasPendingChanges: () => pending || dirty };
}
