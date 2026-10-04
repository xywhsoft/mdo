import { api } from "../../api/client.js";
import { currentLocale, subscribeLocale } from "../../i18n.js";

const labels = {
  "zh-CN": { title: "应用更新", description: "与线上安装包比较，确认后下载和安装。配置、项目和会话会保留。",
    check: "检查更新", download: "下载更新", install: "安装更新", restart: "安装并重启",
    cancel: "取消下载", later: "稍后", notice: "墨斗有可用更新", open: "查看更新",
    disabled: "开发模式或当前平台不启用自动更新", checking: "正在检查更新…", current: "与线上版本一致",
    available: "有可用更新", downloading: "正在下载并校验…", ready: "更新包已校验，可以安装",
    installing: "请在原生窗口确认安装；结束前暂停新任务", error: "更新检查失败，不影响正常使用",
    "no-package": "此平台尚未发布更新", failed: "操作失败，请重试", connection: "暂时无法连接本地服务" },
  "en-US": { title: "App updates", description: "Compare with the published package. Install after confirmation; your data is preserved.",
    check: "Check for updates", download: "Download update", install: "Install update", restart: "Install and restart",
    cancel: "Cancel download", later: "Later", notice: "An mdo update is available", open: "View update",
    disabled: "Updates are disabled in development or on this platform", checking: "Checking…", current: "Matches the published package",
    available: "Update available", downloading: "Downloading and verifying…", ready: "Verified update ready to install",
    installing: "Confirm in the native window; new tasks are paused", error: "Update check failed; normal use is unaffected",
    "no-package": "No update published for this platform", failed: "Operation failed; please retry", connection: "Local service is temporarily unavailable" },
  "ru-RU": { title: "Обновления", description: "Сравнение с опубликованным пакетом. Установка после подтверждения; данные сохранятся.",
    check: "Проверить", download: "Скачать", install: "Установить", restart: "Установить и перезапустить",
    cancel: "Отменить загрузку", later: "Позже", notice: "Доступно обновление mdo", open: "Показать",
    disabled: "Обновления отключены в режиме разработки или на этой платформе", checking: "Проверка…", current: "Соответствует опубликованному пакету",
    available: "Есть обновление", downloading: "Загрузка и проверка…", ready: "Пакет проверен и готов к установке",
    installing: "Подтвердите в окне приложения; новые задачи приостановлены", error: "Проверка не удалась; работа приложения не затронута",
    "no-package": "Для этой платформы нет обновления", failed: "Не удалось; повторите попытку", connection: "Локальный сервис временно недоступен" },
};
export function updateActions(status) {
  return {
    check: !!status?.enabled && !status.busy,
    download: !!status?.enabled && !status.busy && status.status === "available",
    install: !!status?.enabled && !status.busy && !!status.ready,
    cancel: !!status?.busy && status.status === "downloading",
  };
}
export function createUpdatePanel({ root, notice, navigation, transport = api }) {
  if (!root || !notice) return null;
  const statusNode = root.querySelector("[data-update-status]");
  const notes = root.querySelector("[data-update-notes]");
  let status = null, pending = false, destroyed = false, dismissed = "", timer, failure = "", connectionLost = false;
  const text = () => labels[currentLocale()] ?? labels["en-US"];
  function render() {
    const words = text();
    for (const node of root.querySelectorAll("[data-update-label]"))
      node.textContent = words[node.dataset.updateLabel];
    statusNode.textContent = status ? words[status.status] ?? words.failed : words.checking;
    if (status?.message) statusNode.textContent += " · " + status.message;
    if (status?.last_install_message) statusNode.textContent += " · " + status.last_install_message;
    if (failure) statusNode.textContent += " · " + failure;
    else if (connectionLost) statusNode.textContent += " · " + words.connection;
    notes.textContent = status?.notes ?? "";
    notes.hidden = !notes.textContent;
    const actions = updateActions(status);
    for (const button of root.querySelectorAll("[data-update-action]")) {
      const action = button.dataset.updateAction;
      button.disabled = pending || !actions[action];
      if (action !== "check") button.hidden = !actions[action];
      button.textContent = words[action === "install" && status?.platform === "windows-x86_64" ? "restart" : action];
    }
    notice.hidden = status?.status !== "available" || status?.sha256 === dismissed;
    notice.querySelector("span").textContent = words.notice;
    notice.querySelector("[data-update-open]").textContent = words.open;
    notice.querySelector("[data-update-later]").textContent = words.later;
  }
  function schedule() {
    clearTimeout(timer);
    if (!destroyed) timer = setTimeout(refresh, status?.busy ? 1000 : status ? 60000 : 3000);
  }
  async function refresh() {
    if (destroyed || pending) return schedule();
    pending = true;
    try {
      const latest = await transport.get("/update");
      if (!destroyed) { status = latest; connectionLost = false; render(); }
    } catch { if (!destroyed) connectionLost = true; }
    finally { pending = false; if (!destroyed) render(); schedule(); }
  }
  async function action(event) {
    const kind = event.target.closest("[data-update-action]")?.dataset.updateAction;
    if (!kind || pending || !updateActions(status)[kind]) return;
    pending = true; failure = ""; render();
    try {
      if (kind === "cancel") await transport.delete("/update/download");
      else await transport.post(kind === "check" ? "/update" : "/update/" + kind, {});
    } catch (error) { if (!destroyed) failure = text().failed + " · " + error.message; }
    finally { pending = false; if (!destroyed) await refresh(); }
  }
  function open() { navigation.openSettings("general"); }
  function later() { dismissed = status?.sha256 ?? ""; render(); }
  root.addEventListener("click", action);
  notice.querySelector("[data-update-open]").addEventListener("click", open);
  notice.querySelector("[data-update-later]").addEventListener("click", later);
  const unsubscribe = subscribeLocale(render);
  render(); void refresh();
  return { refresh, destroy() {
    destroyed = true; clearTimeout(timer); unsubscribe();
    root.removeEventListener("click", action);
    notice.querySelector("[data-update-open]").removeEventListener("click", open);
    notice.querySelector("[data-update-later]").removeEventListener("click", later);
  }};
}
