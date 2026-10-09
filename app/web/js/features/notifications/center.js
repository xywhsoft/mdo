import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { currentLocale, subscribeLocale } from "../../i18n.js";
import { buildNotices } from "./model.js";
import { createNoticeIndicators, createNoticeIcon, noticeTone } from "./indicator.js";
import { createToolPanel } from "./tool-panel.js";

const words = {
  "zh-CN": { title: "通知", update: "应用更新", updateBody: "安装后保留配置、项目和会话。", toolsUpdate: "扩展工具包更新", install: "扩展工具包安装", login: "登录账号", toolsBody: "扩展工具包提供 BusyBox 常用命令、curl 网络下载、jq 数据处理和 SSH/SCP/SFTP 远程连接。Python 可用于脚本和数据处理。请从 设置 → 扩展能力 → 扩展工具包 进入安装和更新。", accountBody: "登录后可使用联网搜索、自己的设备中继、在线模型及用量查询。账号令牌保存在本机服务中。请从 设置 → 账号 进入登录。", android: "Android 不支持单独安装、更新或卸载扩展工具包。系统限制在可写目录中执行原生程序，因此工具必须随完整版 APK 安装和更新。请安装同签名、版本号更大的完整版，覆盖升级会保留配置、项目和会话。", details: "查看详情", backToList: "返回通知列表", later: "稍后提醒", dismiss: "不再显示", close: "关闭", tools: "扩展工具包", check: "检查更新", core: "常用工具包", python: "Python（可选）", full: "查看完整版更新", empty: "暂无通知", marketing: "显示优惠通知", busy: "正在处理…", missing: "尚未安装", installed: "已安装", failure: "操作失败，请重试", read: "已读" },
  "en-US": { title: "Notifications", update: "App update", updateBody: "Saved settings and conversations are preserved.", toolsUpdate: "Tool package update", install: "Install tool package", login: "Sign in", toolsBody: "BusyBox adds common commands, curl downloads, jq JSON processing, and SSH/SCP/SFTP remote access. Optional Python supports scripts. Install and update at Settings → Extended capabilities → Tool packages.", accountBody: "Sign in for web search, your own device relay, online models and usage. Tokens remain in the local service. Open Settings → Account.", android: "Android does not support separate tool package installation, updates or removal. Native tools must ship in the full APK because Android restricts execution in writable directories. Install a newer full APK with the same signer to preserve settings, projects and conversations.", details: "View details", backToList: "Back to notification list", later: "Remind later", dismiss: "Dismiss", close: "Close", tools: "Tool packages", check: "Check updates", core: "Core tools", python: "Python (optional)", full: "Full APK updates", empty: "No notifications", marketing: "Show offers", busy: "Working…", missing: "Not installed", installed: "Installed", failure: "Failed; please retry", read: "Read" },
  "ru-RU": { title: "Уведомления", update: "Обновление", updateBody: "Настройки и диалоги сохраняются.", toolsUpdate: "Обновление инструментов", install: "Установить инструменты", login: "Войти", toolsBody: "BusyBox, curl, jq и SSH/SCP/SFTP добавляют команды и удалённый доступ. Python устанавливается отдельно. Настройки → Дополнительные возможности → Инструменты.", accountBody: "Вход открывает веб-поиск, доступ к своим устройствам, онлайн-моделям и расходам. Настройки → Аккаунт.", android: "В Android нельзя отдельно устанавливать, обновлять или удалять пакеты инструментов. Система ограничивает запуск программ из записываемых каталогов, поэтому инструменты входят в полный APK. Установите более новую полную версию с той же подписью: настройки, проекты и диалоги сохранятся.", details: "Подробнее", backToList: "К списку уведомлений", later: "Напомнить позже", dismiss: "Скрыть", close: "Закрыть", tools: "Инструменты", check: "Проверить", core: "Основные инструменты", python: "Python (опция)", full: "Обновления полного APK", empty: "Нет уведомлений", marketing: "Показывать предложения", busy: "Выполняется…", missing: "Не установлено", installed: "Установлено", failure: "Ошибка; повторите", read: "Прочитано" }
};
const node = (tag, text, cls) => { const e = document.createElement(tag); if (text != null) e.textContent = text; if (cls) e.className = cls; return e; };
const serviceMessages = {
  "zh-CN": { "Online service unavailable; cached notices retained": "暂时无法连接在线服务，已保留缓存通知。", "Tool package verification or installation failed": "工具包校验或安装失败，请重试。" },
  "ru-RU": { "Online service unavailable; cached notices retained": "Онлайн-сервис недоступен; сохранённые уведомления доступны.", "Tool package verification or installation failed": "Не удалось проверить или установить инструменты." }
};
Object.assign(words["zh-CN"], { desktop: "Windows 支持独立安装和更新工具包，Python 可按需安装。安装后会自动检测工具并向 Agent 提供可用能力。", androidLite: "当前为 Android 精简版，未内置扩展工具包。", androidFull: "当前为 Android 完整版，扩展工具已随 APK 内置。", installFull: "安装完整版", updateBadge: "更新", newVersion: "有一个新版本发布了", allNotices: "查看所有通知" });
Object.assign(words["en-US"], { desktop: "Windows supports separate tool package installation and updates. Python is optional. Installed tools are checked and made available to the Agent.", androidLite: "This device runs Android lite without bundled tool packages.", androidFull: "This device runs Android full with tools bundled in the APK.", installFull: "Install full edition", updateBadge: "Update", newVersion: "A new version is available", allNotices: "View all notifications" });
Object.assign(words["ru-RU"], { desktop: "В Windows пакеты инструментов устанавливаются и обновляются отдельно. Python — по желанию. Установленные инструменты проверяются и становятся доступны Agent.", androidLite: "На устройстве установлена облегчённая версия Android без пакета инструментов.", androidFull: "На устройстве установлена полная версия Android с инструментами в APK.", installFull: "Установить полную версию", updateBadge: "Обн.", newVersion: "Доступна новая версия", allNotices: "Все уведомления" });
Object.assign(words["zh-CN"], { unifiedUpdate:"应用与扩展工具更新", updatePackage:"更新工具包", installPackage:"安装工具包", repair:"修复", rollback:"回退上一包", uninstall:"卸载", cleanup:"清理旧包", cancel:"取消", compatibility:"兼容版本", dependencies:"依赖", source:"来源", noToolUpdates:"扩展工具包已是最新版本", confirmUninstall:"卸载将停用这个工具包，正在运行的任务仍可使用旧目录。确定卸载？", confirmCleanup:"将删除重启前已停用的旧包目录，删除后不能回退到这些包。确定清理？", cleanupHelp:"本次运行中停用的目录会保留供当前任务使用，重启后可清理。", stages:{queued:"等待处理",downloading:"下载并校验",extracting:"提取并校验",probing:"检测工具能力",activating:"启用工具包",cleaning:"清理旧包",complete:"完成",cancelled:"已取消",failed:"失败"} });
Object.assign(words["en-US"], { unifiedUpdate:"App and tool updates",updatePackage:"Update package",installPackage:"Install package",repair:"Repair",rollback:"Restore previous package",uninstall:"Uninstall",cleanup:"Clean old packages",cancel:"Cancel",compatibility:"Compatible builds",dependencies:"Dependencies",source:"Source",noToolUpdates:"Tool packages are up to date",confirmUninstall:"Deactivate this package? Current tasks keep their existing paths.",confirmCleanup:"Delete packages retired before this restart? You cannot restore deleted packages.",cleanupHelp:"Packages retired during this run remain available to current tasks. Restart before cleaning them.",stages:{queued:"Queued",downloading:"Downloading and verifying",extracting:"Extracting and verifying",probing:"Checking capabilities",activating:"Activating package",cleaning:"Cleaning",complete:"Complete",cancelled:"Cancelled",failed:"Failed"} });
Object.assign(words["ru-RU"], { stages:{activating:"Активация пакета"},unifiedUpdate:"Обновления приложения и инструментов",updatePackage:"Обновить пакет",installPackage:"Установить пакет",repair:"Исправить",rollback:"Предыдущий пакет",uninstall:"Удалить",cleanup:"Очистить старые пакеты",cancel:"Отмена",compatibility:"Совместимые сборки",dependencies:"Зависимости",source:"Источник",noToolUpdates:"Инструменты обновлены",confirmUninstall:"Отключить пакет? Текущие задачи сохранят старые пути.",confirmCleanup:"Удалить пакеты, отключённые до перезапуска? Восстановить их будет нельзя.",cleanupHelp:"Перезапустите приложение перед удалением пакетов текущих задач." });
Object.assign(words["zh-CN"],{expectedHash:"预期 SHA-256",actualHash:"实际 SHA-256",diagnosticSaved:"已保留最近一次失败的下载文件及诊断记录；安装成功后自动清理。",errors:{metadata:"没有兼容的工具包或发布信息无效",network:"下载连接失败",response:"下载响应不完整或格式错误",download:"下载中断或超时",size:"下载大小与发布清单不一致",checksum:"下载文件 SHA-256 不一致",archive:"工具包无法解包",manifest:"包内清单与发布信息不一致",write:"无法写入或提取文件",file_checksum:"提取文件 SHA-256 不一致",probe:"工具运行能力检测失败",activate:"无法启用已校验的工具包"}});
Object.assign(words["en-US"],{expectedHash:"Expected SHA-256",actualHash:"Actual SHA-256",diagnosticSaved:"The last failed download and diagnostic report are retained until installation succeeds.",errors:{metadata:"No compatible package or invalid metadata",network:"Download connection failed",response:"Incomplete or invalid download response",download:"Download interrupted or timed out",size:"Downloaded size differs from the catalog",checksum:"Downloaded SHA-256 mismatch",archive:"Cannot open package archive",manifest:"Package manifest mismatch",write:"Cannot write or extract file",file_checksum:"Extracted file SHA-256 mismatch",probe:"Tool capability check failed",activate:"Cannot activate verified package"}});
Object.assign(words["ru-RU"],{expectedHash:"Ожидаемый SHA-256",actualHash:"Фактический SHA-256",diagnosticSaved:"Последняя неудачная загрузка и отчёт сохранены до успешной установки.",errors:{metadata:"Нет совместимого пакета или неверные метаданные",network:"Ошибка соединения",response:"Неполный или неверный ответ сервера",download:"Загрузка прервана или истекло время ожидания",size:"Размер загрузки не совпадает с каталогом",checksum:"SHA-256 загрузки не совпадает",archive:"Не удалось открыть архив",manifest:"Манифест пакета не совпадает",write:"Не удалось записать или извлечь файл",file_checksum:"SHA-256 извлечённого файла не совпадает",probe:"Проверка инструментов не пройдена",activate:"Не удалось активировать пакет"}});
export function createNotificationCenter({ updatePanel, navigation }) {
  const dialog = document.querySelector("#notifications-dialog"), toolsBody = document.querySelector("[data-settings-toolpacks]");
  const entries = document.querySelectorAll("[data-notifications-open]");
  let distribution = {}, update = null, account = null, preferences = {}, pending = false, timer, destroyed = false, epoch = 0, selected = null;
  let bodySignature = "";
  const copy = () => words[currentLocale()] ?? words["en-US"];
  const toolPanel=createToolPanel({copy,target:targetState,command,openFull:async()=>{
    try { await api.post("/update",{edition:"full"});await updatePanel?.refresh();updatePanel?.open(); }
    catch(error){distribution.message=copy().failure+" · "+error.message;renderTools();}
  }});
  updatePanel?.setToolManager({title:()=>copy().unifiedUpdate,render:body=>toolPanel.render(body,distribution,true),check:()=>command("check","")});
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
    dialog.querySelector("h2").textContent = c.title;
    const closeButton = dialog.querySelector("[data-notifications-close]");
    closeButton.setAttribute("aria-label", c.close); closeButton.title = c.close;
    if (selected) selected = list.find(n => n.id === selected.id && n.revision === selected.revision) ?? null;
    const signature = JSON.stringify([key(), currentLocale(), selected, list, preferences]);
    if (signature === bodySignature) { renderTools(); return; }
    bodySignature = signature; body.replaceChildren();
    if (selected) {
      const n = selected; body.append(node("h3", n.title), node("p", n.body, "notice-description"));
      if (n.action) body.append(button(c.details, () => openAction(n)));
      if (!n.persistent) body.append(button(n.online ? c.dismiss : c.later, () => { preferences[n.id] = { ...preferences[n.id], ...(n.online ? { dismissed: n.revision } : { snooze: Date.now() + 86400000 }) }; save(); selected = null; render(); }));
      body.append(button(c.backToList, () => { selected = null; render(); }));
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
    const data={...distribution,message:serviceMessages[currentLocale()]?.[distribution.message]??distribution.message};
    toolPanel.render(toolsBody,data);
    toolPanel.render(document.querySelector("[data-update-toolpacks]"),data,true);
  }
  function openTools() { indicators.hide(); if (dialog.open) close(); navigation.openSettings("toolpacks"); }
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
  dialog.querySelector("[data-notifications-close]").addEventListener("click", close);
  const locale = subscribeLocale(render), target = subscribeTarget(() => { epoch++; distribution = {}; update = account = selected = null; loadPreferences(); render(); void refresh(); });
  const route = navigation.subscribe(state => { if (state.view === "settings" && state.settingsSection === "toolpacks") void refresh(); });
  const foreground = () => { if (!document.hidden) void refresh(); }; document.addEventListener("visibilitychange", foreground);
  loadPreferences(); render(); void refresh();
  return { refresh, destroy() { destroyed = true; clearTimeout(timer); indicators.destroy(); locale(); target(); route(); document.removeEventListener("visibilitychange", foreground); } };
}
