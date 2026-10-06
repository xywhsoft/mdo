import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { currentLocale, subscribeLocale } from "../../i18n.js";
import { buildNotices } from "./model.js";
import { createNoticeIndicators, createNoticeIcon, noticeTone } from "./indicator.js";

const words = {
  "zh-CN": { title: "通知", update: "应用更新", updateBody: "安装后保留配置、项目和会话。", toolsUpdate: "扩展工具包更新", install: "扩展工具包安装", login: "登录账号", toolsBody: "扩展工具包提供 BusyBox 常用命令、curl 网络下载、jq 数据处理和 SSH/SCP/SFTP 远程连接。Python 可用于脚本和数据处理。请从 设置 → 常规 → 扩展工具包 进入安装和更新。", accountBody: "登录后可使用联网搜索、自己的设备中继、在线模型及用量查询。账号令牌保存在本机服务中。请从 设置 → 账号 进入登录。", android: "Android 原生工具通过完整版 APK 安装，解决应用数据目录中的执行限制。精简版与完整版使用相同应用 ID 和签名，覆盖升级保留数据。请安装版本号更大的完整版；工具更新也通过新版完整版 APK 分发。", open: "打开", later: "稍后提醒", dismiss: "不再显示", close: "关闭", tools: "扩展工具包", check: "检查更新", core: "常用工具包", python: "Python（可选）", full: "查看完整版更新", empty: "暂无通知", marketing: "显示优惠通知", busy: "正在处理…", missing: "尚未安装", installed: "已安装", failure: "操作失败，请重试", read: "已读" },
  "en-US": { title: "Notifications", update: "App update", updateBody: "Saved settings and conversations are preserved.", toolsUpdate: "Tool package update", install: "Install tool package", login: "Sign in", toolsBody: "BusyBox adds common commands, curl downloads, jq JSON processing, and SSH/SCP/SFTP remote access. Optional Python supports scripts. Install and update at Settings → General → Tool packages.", accountBody: "Sign in for web search, your own device relay, online models and usage. Tokens remain in the local service. Open Settings → Account.", android: "Native tools are installed through the full Android APK. Lite and full share an application ID and signer; a newer full APK preserves data. Update tools with a newer full APK.", open: "Open", later: "Remind later", dismiss: "Dismiss", close: "Close", tools: "Tool packages", check: "Check updates", core: "Core tools", python: "Python (optional)", full: "Full APK updates", empty: "No notifications", marketing: "Show offers", busy: "Working…", missing: "Not installed", installed: "Installed", failure: "Failed; please retry", read: "Read" },
  "ru-RU": { title: "Уведомления", update: "Обновление", updateBody: "Настройки и диалоги сохраняются.", toolsUpdate: "Обновление инструментов", install: "Установить инструменты", login: "Войти", toolsBody: "BusyBox, curl, jq и SSH/SCP/SFTP добавляют команды и удалённый доступ. Python устанавливается отдельно. Настройки → Общие → Инструменты.", accountBody: "Вход открывает веб-поиск, доступ к своим устройствам, онлайн-моделям и расходам. Настройки → Аккаунт.", android: "Инструменты устанавливаются через полный APK с тем же ID приложения и подписью. Установите более новую полную версию для сохранения данных и обновления инструментов.", open: "Открыть", later: "Напомнить позже", dismiss: "Скрыть", close: "Закрыть", tools: "Инструменты", check: "Проверить", core: "Основные инструменты", python: "Python (опция)", full: "Обновления полного APK", empty: "Нет уведомлений", marketing: "Показывать предложения", busy: "Выполняется…", missing: "Не установлено", installed: "Установлено", failure: "Ошибка; повторите", read: "Прочитано" }
};
const node = (tag, text, cls) => { const e = document.createElement(tag); if (text != null) e.textContent = text; if (cls) e.className = cls; return e; };
const serviceMessages = {
  "zh-CN": { "Online service unavailable; cached notices retained": "暂时无法连接在线服务，已保留缓存通知。", "Tool package verification or installation failed": "工具包校验或安装失败，请重试。" },
  "ru-RU": { "Online service unavailable; cached notices retained": "Онлайн-сервис недоступен; сохранённые уведомления доступны.", "Tool package verification or installation failed": "Не удалось проверить или установить инструменты." }
};
Object.assign(words["zh-CN"], { updateBadge: "更新", newVersion: "有一个新版本发布了", allNotices: "查看所有通知" });
Object.assign(words["en-US"], { updateBadge: "Update", newVersion: "A new version is available", allNotices: "View all notifications" });
Object.assign(words["ru-RU"], { updateBadge: "Обн.", newVersion: "Доступна новая версия", allNotices: "Все уведомления" });
export function createNotificationCenter({ updatePanel, navigation }) {
  const dialog = document.querySelector("#notifications-dialog"), toolsDialog = document.querySelector("#toolpacks-dialog");
  const entries = document.querySelectorAll("[data-notifications-open]");
  let distribution = {}, update = null, account = null, preferences = {}, pending = false, timer, destroyed = false, epoch = 0, selected = null;
  let bodySignature = "", toolsSignature = "";
  const copy = () => words[currentLocale()] ?? words["en-US"];
  const indicators = createNoticeIndicators({ hosts: entries, copy, onOpen(n) {
    if (n) showDetail(n); else { selected = null; render(); }
    if (!dialog.open) dialog.showModal();
  } });
  const key = () => "mdo.notices." + (targetState().selected?.id ?? "local");
  function loadPreferences() { try { preferences = JSON.parse(localStorage.getItem(key()) || "{}"); } catch { preferences = {}; } }
  function save() { try { localStorage.setItem(key(), JSON.stringify(preferences)); } catch {} }
  const notices = () => buildNotices({ distribution, update, account, locale: currentLocale(), preferences, copy: copy() });
  function button(text, action) { const e = node("button", text, "secondary-button"); e.type = "button"; e.addEventListener("click", action); return e; }
  function close() { indicators.hide(); selected = null; dialog.close(); }
  function openAction(n) { close(); if (n.action === "update") updatePanel?.open(); else if (n.action === "tools") openTools(); else if (n.action === "account") navigation.openSettings("account"); }
  function showDetail(n) {
    indicators.hide(); selected = n; preferences[n.id] = { ...preferences[n.id], read: n.revision }; save(); render(); indicators.hide();
  }
  function render() {
    const list = notices(), c = copy();
    indicators.render(list, preferences, key());
    const body = dialog.querySelector("[data-notification-body]");
    dialog.querySelector("h2").textContent = c.title; dialog.querySelector("[data-notifications-close]").textContent = c.close;
    if (selected) selected = list.find(n => n.id === selected.id && n.revision === selected.revision) ?? null;
    const signature = JSON.stringify([key(), currentLocale(), selected, list, preferences]);
    if (signature === bodySignature) { renderTools(); return; }
    bodySignature = signature; body.replaceChildren();
    if (selected) {
      const n = selected; body.append(node("h3", n.title), node("p", n.body, "notice-description"));
      if (n.action) body.append(button(c.open, () => openAction(n)));
      if (!n.persistent) body.append(button(n.online ? c.dismiss : c.later, () => { preferences[n.id] = { ...preferences[n.id], ...(n.online ? { dismissed: n.revision } : { snooze: Date.now() + 86400000 }) }; save(); selected = null; render(); }));
      body.append(button("← " + c.title, () => { selected = null; render(); }));
    } else {
      if (!list.length) body.append(node("p", c.empty));
      for (const n of list) {
        const row = button("", () => showDetail(n)); row.className = "secondary-button notification-row";
        if (/^[a-f0-9]{64}$/.test(n.icon_sha256 ?? "")) { const img = node("img"); img.src = "https://ai.xywhsoft.com/mdo/blob/" + n.icon_sha256; img.alt = ""; img.width = 20; img.height = 20; row.append(img); }
        else { const icon = node("span", null, "notification-icon"); icon.dataset.tone = noticeTone(n); icon.append(createNoticeIcon(n)); row.append(icon); }
        row.append(node("span", n.title), node("small", preferences[n.id]?.read === n.revision ? c.read : "●")); body.append(row);
      }
      const label = node("label", null, "notice-marketing"), input = node("input"); input.type = "checkbox"; input.checked = preferences.marketing !== false;
      input.addEventListener("change", () => { preferences.marketing = input.checked; save(); render(); }); label.append(input, node("span", c.marketing)); body.append(label);
    }
    renderTools();
  }
  function renderTools() {
    const c = copy(), body = toolsDialog.querySelector("[data-toolpacks-body]");
    const signature = JSON.stringify([key(), currentLocale(), distribution.edition, distribution.toolpacks, distribution.installed, distribution.tools, distribution.busy, distribution.message]);
    if (signature === toolsSignature) return;
    toolsSignature = signature; body.replaceChildren(node("p", c.toolsBody));
    toolsDialog.querySelector("h2").textContent = c.tools; toolsDialog.querySelector("[data-toolpacks-close]").textContent = c.close;
    if (distribution.edition === "lite" || distribution.edition === "full") {
      body.append(node("p", c.android), button(c.full, async () => {
        try { await api.post("/update", { edition: "full" }); toolsDialog.close(); await updatePanel?.refresh(); updatePanel?.open(); }
        catch (error) { distribution.message = copy().failure + " · " + error.message; renderTools(); }
      }));
    } else for (const id of ["core", "python"]) {
      const pack = distribution.toolpacks?.find(p => p.id === id), installed = distribution.installed?.[id], row = node("div", null, "toolpack-row");
      row.append(node("strong", c[id]), node("span", installed ? c.installed : c.missing));
      row.append(button(c.install, () => command("install", id)));
      row.querySelector("button").disabled = !pack || distribution.busy || (installed && pack.revision <= (distribution.installed?.[id + "_revision"] ?? 0)); body.append(row);
    }
    for (const tool of distribution.tools ?? []) body.append(node("p", tool.id + " · " + tool.version), node("code", tool.path));
    body.append(button(c.check, () => command("check", "")));
    if (distribution.busy) body.append(node("p", c.busy), button("×", () => command("cancel", "")));
    if (distribution.message) body.append(node("p", serviceMessages[currentLocale()]?.[distribution.message] ?? distribution.message, "notice-description"));
  }
  function openTools() { indicators.hide(); if (dialog.open) close(); renderTools(); if (!toolsDialog.open) toolsDialog.showModal(); }
  async function command(action, id) {
    try { await api.post("/distribution", { action, id }); await refresh(); }
    catch (error) { distribution.message = copy().failure + " · " + error.message; render(); }
  }
  async function refresh() {
    if (pending || destroyed) return; pending = true; const generation = epoch;
    try {
      const results = await Promise.allSettled([api.get("/distribution"), api.get("/update"), api.get("/account")]);
      if (generation !== epoch || destroyed) return;
      if (results[0].status === "fulfilled") distribution = results[0].value.data;
      if (results[1].status === "fulfilled") update = results[1].value.data;
      if (results[2].status === "fulfilled") account = results[2].value.data;
      render();
    } finally { pending = false; clearTimeout(timer); if (!destroyed) timer = setTimeout(refresh, distribution.busy ? 1000 : 30000); }
  }
  document.querySelector("[data-toolpacks-open]")?.addEventListener("click", openTools);
  dialog.querySelector("[data-notifications-close]").addEventListener("click", close);
  toolsDialog.querySelector("[data-toolpacks-close]").addEventListener("click", () => toolsDialog.close());
  const locale = subscribeLocale(render), target = subscribeTarget(() => { epoch++; distribution = {}; update = account = selected = null; loadPreferences(); render(); void refresh(); });
  const foreground = () => { if (!document.hidden) void refresh(); }; document.addEventListener("visibilitychange", foreground);
  loadPreferences(); render(); void refresh();
  return { refresh, destroy() { destroyed = true; clearTimeout(timer); indicators.destroy(); locale(); target(); document.removeEventListener("visibilitychange", foreground); } };
}
