import { t as uiErrorText, currentLocale, subscribeLocale } from "../../i18n.js";
import { api } from "../../api/client.js";

import { subscribeTarget, targetState } from "../../api/target.js";
import { openLayer } from "../../components/layers.js";
import { element } from "../../utils/dom.js";

const labels = {
  "zh-CN": { title: "应用更新", description: "与线上安装包比较，确认后下载和安装。配置、项目和会话会保留。",
    check: "检查更新", download: "下载更新", install: "安装更新", restart: "安装并重启",
    cancel: "取消下载", close: "关闭", notice: "墨斗有可用更新", open: "查看更新", badge: "更新", exit: "退出墨斗",
    requiredTitle: "需要更新墨斗", requiredDescription: "此版本需要更新才能继续使用。正在执行的任务会停止，已保存的配置和会话会保留。",
    requiredError: "暂时无法完成更新，请重试；更新完成前不能继续使用。",
    disabled: "开发模式或当前平台不启用自动更新", checking: "正在检查更新…", current: "与线上版本一致",
    available: "有可用更新", downloading: "正在下载并校验…", ready: "更新包已校验，可以安装",
    installing: "正在安装更新；结束前暂停新任务", error: "更新检查失败，不影响正常使用",
    confirmLinux: "安装更新并重启墨斗？配置、项目和会话会保留，页面会短暂断开连接。",
    "no-package": "此平台尚未发布更新", failed: "操作失败，请重试", connection: "暂时无法连接目标服务",
    native: "请在目标设备的原生窗口中确认安装", devices: "切换设备", lite: "Android 精简版", full: "Android 完整版（含扩展工具）" },
  "en-US": { title: "App updates", description: "Compare with the published package. Install after confirmation; your data is preserved.",
    check: "Check for updates", download: "Download update", install: "Install update", restart: "Install and restart",
    cancel: "Cancel download", close: "Close", notice: "An mdo update is available", open: "View update", badge: "Update", exit: "Exit mdo",
    requiredTitle: "mdo update required", requiredDescription: "Install this update to continue. Active tasks will stop; saved settings and sessions are preserved.",
    requiredError: "Update unavailable. Please retry; install the required update to continue.",
    disabled: "Updates are disabled in development or on this platform", checking: "Checking…", current: "Matches the published package",
    available: "Update available", downloading: "Downloading and verifying…", ready: "Verified update ready to install",
    installing: "Installing update; new tasks are paused", error: "Update check failed; normal use is unaffected",
    confirmLinux: "Install the update and restart mdo? Your data is preserved; this page will briefly disconnect.",
    "no-package": "No update published for this platform", failed: "Operation failed; please retry", connection: "Target service is temporarily unavailable",
    native: "Confirm installation in the target device's native window", devices: "Switch device", lite: "Android lite", full: "Android full (with tools)" },
  "ru-RU": { title: "Обновления", description: "Сравнение с опубликованным пакетом. Установка после подтверждения; данные сохранятся.",
    check: "Проверить", download: "Скачать", install: "Установить", restart: "Установить и перезапустить",
    cancel: "Отменить загрузку", close: "Закрыть", notice: "Доступно обновление mdo", open: "Показать", badge: "Обновить", exit: "Выйти",
    requiredTitle: "Необходимо обновить mdo", requiredDescription: "Установите обновление, чтобы продолжить. Активные задачи остановятся; сохраненные данные останутся.",
    requiredError: "Обновление недоступно. Повторите попытку; для продолжения требуется обновление.",
    disabled: "Обновления отключены в режиме разработки или на этой платформе", checking: "Проверка…", current: "Соответствует опубликованному пакету",
    available: "Есть обновление", downloading: "Загрузка и проверка…", ready: "Пакет проверен и готов к установке",
    installing: "Установка обновления; новые задачи приостановлены", error: "Проверка не удалась; работа приложения не затронута",
    confirmLinux: "Установить обновление и перезапустить mdo? Данные сохранятся; соединение ненадолго прервётся.",
    "no-package": "Для этой платформы нет обновления", failed: "Не удалось; повторите попытку", connection: "Сервис устройства временно недоступен",
    native: "Подтвердите установку в окне приложения на целевом устройстве", devices: "Выбрать устройство", lite: "Android: облегчённая версия", full: "Android: полная версия с инструментами" },
};
export function updateActions(status, context = {}) {
  const writable = !context.selected || (context.connected &&
    !context.runtimeChanged && context.selected.mode !== "view");
  return {
    check: writable && !!status?.enabled && !status.busy,
    download: writable && !!status?.enabled && !status.busy && !status.ready && updateAvailable(status),
    install: writable && !context.selected && !!status?.enabled && !status.busy && !!status.ready,
    cancel: writable && !!status?.busy && status.status === "downloading",
    exit: !context.selected && !!status?.blocked && !status.busy,
  };
}
export function updateAvailable(status) {
  return !!status?.enabled && (status.available ?? ["available", "ready"].includes(status.status));
}
export function createUpdatePanel({ root, entries = [], transport = api, openDevices = () => {} }) {
  if (!root) return null;
  let status = null, pending = false, destroyed = false, timer, failure = "", connectionLost = false;
  const text = () => labels[currentLocale()] ?? labels["en-US"];
  const scope = () => targetState().selected?.id || "local";
  let forced = false, view = null, target = scope(), epoch = 0;
  const node = (tag, key, cls) => element(tag, { className: cls, attrs: key ? { [`data-update-${key}`]: "" } : {} });
  function open() {
    if (destroyed || view) return;
    const own = {};
    own.layer = openLayer({ title: text().notice, icon: "download", width: "40rem", height: "auto",
      beforeClose: () => !status?.blocked,
      onClosed() { if (view === own) view = null; } });
    own.layer.element.id = "update-dialog"; own.layer.element.classList.add("update-window");
    own.description = node("p", "description"); own.description.id = "update-dialog-description";
    own.layer.element.setAttribute("aria-describedby", own.description.id);
    own.status = node("p", "status"); own.status.setAttribute("role", "status"); own.status.setAttribute("aria-live", "polite");
    own.notes = node("p", "notes"); own.layer.body.append(own.description, own.status, own.notes);
    own.picker = node("button", null, "secondary-button"); own.picker.type = "button"; own.picker.id = "update-target";
    own.picker.setAttribute("aria-haspopup", "dialog"); own.picker.setAttribute("aria-controls", "devices-dialog");
    own.picker.addEventListener("click", () => { if (view === own) openDevices(); }, { signal: own.layer.signal });
    own.layer.footer.append(own.picker);
    own.buttons = [];
    for (const kind of ["exit", "check", "cancel", "download", "install"]) {
      const button = node("button", null, ["download", "install"].includes(kind) ? "primary-button" : "secondary-button");
      button.type = "button"; button.dataset.updateAction = kind; button.setAttribute("data-update-action", kind);
      button.addEventListener("click", () => { if (view === own) void action({ target: button }); }, { signal: own.layer.signal });
      own.buttons.push(button); own.layer.footer.append(button);
    }
    own.close = node("button", null, "secondary-button"); own.close.type = "button";
    own.close.addEventListener("click", () => { if (view === own) void own.layer.close(); }, { signal: own.layer.signal });
    own.layer.footer.append(own.close);
    own.layer.element.addEventListener("keydown", keyboard, { signal: own.layer.signal });
    view = own; render();
  }
  function render() {
    const words = text();
    for (const node of root.querySelectorAll("[data-update-label]"))
      node.textContent = words[node.dataset.updateLabel];
    const blocked = !!status?.blocked;
    let stateText = status ? (blocked && status.status === "error" ? words.requiredError : words[status.status]) ?? words.failed : words.checking;
    if (status?.available && words[status.edition]) stateText += " · " + words[status.edition] + (status.build_id ? " · " + status.build_id : "");
    if (status?.message) stateText += " · " + status.message;
    if (status?.last_install_message) stateText += " · " + status.last_install_message;
    if (status?.ready && targetState().selected) stateText += " · " + words.native;
    if (failure) stateText += " · " + failure;
    else if (connectionLost) stateText += " · " + words.connection;
    for (const surface of [root, ...(view ? [view.layer.body] : [])]) {
      surface.querySelector("[data-update-status]").textContent = stateText;
      const notes = surface.querySelector("[data-update-notes]");
      notes.textContent = status?.notes ?? ""; notes.hidden = !notes.textContent;
    }
    if (view) {
      view.layer.update({ title: blocked ? words.requiredTitle : words.notice });
      view.description.textContent = blocked ? words.requiredDescription : words.description;
      view.layer.element.dataset.required = String(blocked); view.layer.closeButton.hidden = blocked;
      view.close.hidden = blocked; view.close.textContent = words.close;
      const selected = targetState().selected;
      view.picker.hidden = !selected; view.picker.textContent = words.devices + (selected ? " · " + selected.name : "");
    }
    const actions = updateActions(status,targetState());
    for (const button of [...root.querySelectorAll("[data-update-action]"), ...(view?.buttons || [])]) {
      const action = button.dataset.updateAction;
      button.disabled = pending || !actions[action];
      if (action !== "check") button.hidden = !actions[action];
      button.textContent = words[action === "install" && (status?.platform === "windows-x86_64" || status?.platform?.startsWith("linux-")) ? "restart" : action];
    }
    for (const button of entries) {
      button.hidden = !updateAvailable(status);
      button.textContent = button.classList.contains("update-entry-badge") ? "1" : words.badge;
      button.setAttribute("aria-label", words.open); button.title = words.open;
    }
    if (blocked) {
      forced = true; open();
      if (view && (document.activeElement?.hidden || document.activeElement?.disabled ||
          (!view.layer.element.contains(document.activeElement) && !document.activeElement?.closest?.("dialog")?.open)))
        ([...view.buttons, view.picker].find(button => !button.hidden && !button.disabled) || view.layer.element).focus({ preventScroll: true });
    } else if (forced) {
      forced = false; void view?.layer.close("resolved");
    }
  }
  function schedule() {
    clearTimeout(timer);
    if (!destroyed) timer = setTimeout(refresh, status?.busy || status?.blocked || status?.status === "checking" ? 1000 : status ? 60000 : 3000);
  }
  async function refresh() {
    if (destroyed || pending) return schedule();
    pending = true; const generation = epoch;
    try {
      const { data: latest } = await transport.get("/update");
      if (!latest || typeof latest.enabled !== "boolean" || typeof latest.status !== "string")
        throw new Error(uiErrorText("error.detail.invalidUpdateStatus", {}, "Invalid update status"));
      if (!destroyed && generation === epoch) { status = latest; connectionLost = false; render(); }
    } catch { if (!destroyed && generation === epoch) connectionLost = true; }
    finally { pending = false; if (!destroyed) render(); if (!destroyed && generation !== epoch) void refresh(); else schedule(); }
  }
  async function action(event) {
    const kind = event.target.closest("[data-update-action]")?.dataset.updateAction;
    if (!kind || pending || !updateActions(status,targetState())[kind]) return;
    if (kind === "install" && status?.platform?.startsWith("linux-") && !window.confirm(text().confirmLinux)) return;
    pending = true; failure = ""; const generation = epoch; render();
    try {
      if (kind === "cancel") await transport.delete("/update/download");
      else if (kind === "exit" && status.platform === "android-arm64-v8a" && window.XsPlatform?.requestExit)
        window.XsPlatform.requestExit();
      else await transport.post(kind === "check" ? "/update" : "/update/" + kind, {});
    } catch (error) { if (!destroyed && generation === epoch) failure = text().failed + " · " + error.message; }
    finally { pending = false; if (!destroyed) await refresh(); }
  }
  // App-wide shortcuts must not create tasks behind the blocking modal.
  function keyboard(event) {
    if (view && (event.key === "Escape" || event.ctrlKey || event.metaKey)) event.stopPropagation();
  }
  function requiredKeyboard(event) {
    const foregroundDialog = document.activeElement?.closest?.("dialog");
    if (foregroundDialog?.open && foregroundDialog !== view?.layer.element) return;
    if (status?.blocked && (event.key === "Escape" || event.ctrlKey || event.metaKey)) {
      event.stopPropagation();
      if (event.key === "Escape") event.preventDefault();
    }
  }
  async function foreground() {
    if (document.visibilityState !== "visible" || pending || !updateActions(status,targetState()).check) return;
    pending = true;
    try { await transport.post("/update", {}); } catch { /* GET retains the known policy. */ }
    finally { pending = false; if (!destroyed) await refresh(); }
  }
  root.addEventListener("click", action);
  for (const button of entries) button.addEventListener("click", open);
  document.addEventListener("visibilitychange", foreground);
  document.addEventListener("keydown", requiredKeyboard, true);
  const unsubscribe = subscribeLocale(render);
  const unsubscribeTarget = subscribeTarget(() => {
    if (target !== scope()) { target = scope(); epoch++; forced = false; view?.layer.dispose(); status = null; failure = ""; connectionLost = false; void refresh(); }
    render();
  });
  render(); void refresh();
  return { refresh, open, destroy() {
    destroyed = true; clearTimeout(timer); unsubscribe(); unsubscribeTarget();
    root.removeEventListener("click", action);
    for (const button of entries) button.removeEventListener("click", open);
    document.removeEventListener("visibilitychange", foreground);
    document.removeEventListener("keydown", requiredKeyboard, true);
    view?.layer.dispose();
  }};
}
