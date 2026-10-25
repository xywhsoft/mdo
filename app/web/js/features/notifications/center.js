import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { currentLocale, subscribeLocale } from "../../i18n.js";
import { buildNotices } from "./model.js";
import { createNoticeIndicators } from "./indicator.js";
import { openLayer } from "../../components/layers.js";
import { createNoticePreferences } from "./preferences.js";
import { createExternalToolsPanel } from "./external-tools.js";
import { createToolPanel } from "./tool-panel.js";
import { createWarningPanel } from "./warning-panel.js";

const words = {
  "zh-CN": { title: "通知", update: "应用更新", updateBody: "安装后保留配置、项目和会话。", toolsUpdate: "扩展工具包更新", install: "扩展工具包安装", login: "登录账号", toolsBody: "扩展工具包提供 BusyBox 常用命令、curl 网络下载、jq 数据处理和 SSH/SCP/SFTP 远程连接。Python 可用于脚本和数据处理。请从 设置 → 扩展能力 → 扩展工具包 进入安装和更新。", accountBody: "登录后可使用联网搜索、自己的设备中继、在线模型及用量查询。账号令牌保存在本机服务中。点击账号即可直接登录。", android: "Android 不支持单独安装、更新或卸载扩展工具包。系统限制在可写目录中执行原生程序，因此工具必须随完整版 APK 安装和更新。请安装同签名、版本号更大的完整版，覆盖升级会保留配置、项目和会话。", later: "稍后提醒", close: "关闭", tools: "扩展工具包", check: "检查更新", core: "常用工具包", python: "Python（可选）", full: "查看完整版更新", busy: "正在处理…", missing: "尚未安装", installed: "已安装", failure: "操作失败，请重试" },
  "en-US": { title: "Notifications", update: "App update", updateBody: "Saved settings and conversations are preserved.", toolsUpdate: "Tool package update", install: "Install tool package", login: "Sign in", toolsBody: "BusyBox adds common commands, curl downloads, jq JSON processing, and SSH/SCP/SFTP remote access. Optional Python supports scripts. Install and update at Settings → Extended capabilities → Tool packages.", accountBody: "Sign in for web search, your own device relay, online models and usage. Tokens remain in the local service. Select the account entry to sign in.", android: "Android does not support separate tool package installation, updates or removal. Native tools must ship in the full APK because Android restricts execution in writable directories. Install a newer full APK with the same signer to preserve settings, projects and conversations.", later: "Remind later", close: "Close", tools: "Tool packages", check: "Check updates", core: "Core tools", python: "Python (optional)", full: "Full APK updates", busy: "Working…", missing: "Not installed", installed: "Installed", failure: "Failed; please retry" },
  "ru-RU": { title: "Уведомления", update: "Обновление", updateBody: "Настройки и диалоги сохраняются.", toolsUpdate: "Обновление инструментов", install: "Установить инструменты", login: "Войти", toolsBody: "BusyBox, curl, jq и SSH/SCP/SFTP добавляют команды и удалённый доступ. Python устанавливается отдельно. Настройки → Дополнительные возможности → Инструменты.", accountBody: "Вход открывает веб-поиск, доступ к своим устройствам, онлайн-моделям и расходам. Нажмите на аккаунт для входа.", android: "В Android нельзя отдельно устанавливать, обновлять или удалять пакеты инструментов. Система ограничивает запуск программ из записываемых каталогов, поэтому инструменты входят в полный APK. Установите более новую полную версию с той же подписью: настройки, проекты и диалоги сохранятся.", later: "Напомнить позже", close: "Закрыть", tools: "Инструменты", check: "Проверить", core: "Основные инструменты", python: "Python (опция)", full: "Обновления полного APK", busy: "Выполняется…", missing: "Не установлено", installed: "Установлено", failure: "Ошибка; повторите" }
};
const node = (tag, text, cls) => { const e = document.createElement(tag); if (text != null) e.textContent = text; if (cls) e.className = cls; return e; };
const serviceMessages = {
  "zh-CN": { "Online service unavailable; cached notices retained": "暂时无法连接在线服务，已保留缓存通知。", "Tool package verification or installation failed": "工具包校验或安装失败，请重试。" },
  "ru-RU": { "Online service unavailable; cached notices retained": "Онлайн-сервис недоступен; сохранённые уведомления доступны.", "Tool package verification or installation failed": "Не удалось проверить или установить инструменты." }
};
Object.assign(words["zh-CN"], { desktop: "Windows 支持独立安装和更新工具包，Python 可按需安装。安装后会自动检测工具并向 Agent 提供可用能力。", androidLite: "当前为 Android 精简版，未内置扩展工具包。", androidFull: "当前为 Android 完整版，扩展工具已随 APK 内置。", installFull: "安装完整版", updateBadge: "更新", newVersion: "有一个新版本发布了" });
Object.assign(words["en-US"], { desktop: "Windows supports separate tool package installation and updates. Python is optional. Installed tools are checked and made available to the Agent.", androidLite: "This device runs Android lite without bundled tool packages.", androidFull: "This device runs Android full with tools bundled in the APK.", installFull: "Install full edition", updateBadge: "Update", newVersion: "A new version is available" });
Object.assign(words["ru-RU"], { desktop: "В Windows пакеты инструментов устанавливаются и обновляются отдельно. Python — по желанию. Установленные инструменты проверяются и становятся доступны Agent.", androidLite: "На устройстве установлена облегчённая версия Android без пакета инструментов.", androidFull: "На устройстве установлена полная версия Android с инструментами в APK.", installFull: "Установить полную версию", updateBadge: "Обн.", newVersion: "Доступна новая версия" });
Object.assign(words["zh-CN"], { unifiedUpdate:"应用与扩展工具更新", updatePackage:"更新工具包", installPackage:"安装工具包", repair:"修复", rollback:"回退上一包", uninstall:"卸载", cleanup:"清理旧包", cancel:"取消", compatibility:"兼容版本", dependencies:"依赖", source:"来源", noToolUpdates:"扩展工具包已是最新版本", confirmUninstall:"卸载将停用这个工具包，正在运行的任务仍可使用旧目录。确定卸载？", confirmCleanup:"将删除重启前已停用的旧包目录，删除后不能回退到这些包。确定清理？", cleanupHelp:"本次运行中停用的目录会保留供当前任务使用，重启后可清理。", stages:{queued:"等待处理",downloading:"下载并校验",extracting:"提取并校验",probing:"检测工具能力",activating:"启用工具包",cleaning:"清理旧包",complete:"完成",cancelled:"已取消",failed:"失败"} });
Object.assign(words["en-US"], { unifiedUpdate:"App and tool updates",updatePackage:"Update package",installPackage:"Install package",repair:"Repair",rollback:"Restore previous package",uninstall:"Uninstall",cleanup:"Clean old packages",cancel:"Cancel",compatibility:"Compatible builds",dependencies:"Dependencies",source:"Source",noToolUpdates:"Tool packages are up to date",confirmUninstall:"Deactivate this package? Current tasks keep their existing paths.",confirmCleanup:"Delete packages retired before this restart? You cannot restore deleted packages.",cleanupHelp:"Packages retired during this run remain available to current tasks. Restart before cleaning them.",stages:{queued:"Queued",downloading:"Downloading and verifying",extracting:"Extracting and verifying",probing:"Checking capabilities",activating:"Activating package",cleaning:"Cleaning",complete:"Complete",cancelled:"Cancelled",failed:"Failed"} });
Object.assign(words["ru-RU"], { stages:{activating:"Активация пакета"},unifiedUpdate:"Обновления приложения и инструментов",updatePackage:"Обновить пакет",installPackage:"Установить пакет",repair:"Исправить",rollback:"Предыдущий пакет",uninstall:"Удалить",cleanup:"Очистить старые пакеты",cancel:"Отмена",compatibility:"Совместимые сборки",dependencies:"Зависимости",source:"Источник",noToolUpdates:"Инструменты обновлены",confirmUninstall:"Отключить пакет? Текущие задачи сохранят старые пути.",confirmCleanup:"Удалить пакеты, отключённые до перезапуска? Восстановить их будет нельзя.",cleanupHelp:"Перезапустите приложение перед удалением пакетов текущих задач." });
Object.assign(words["zh-CN"],{expectedHash:"预期 SHA-256",actualHash:"实际 SHA-256",diagnosticSaved:"已保留最近一次失败的下载文件及诊断记录；安装成功后自动清理。",errors:{metadata:"没有兼容的工具包或发布信息无效",network:"下载连接失败",response:"下载响应不完整或格式错误",download:"下载中断或超时",size:"下载大小与发布清单不一致",checksum:"下载文件 SHA-256 不一致",archive:"工具包无法解包",manifest:"包内清单与发布信息不一致",write:"无法写入或提取文件",file_checksum:"提取文件 SHA-256 不一致",probe:"工具运行能力检测失败",activate:"无法启用已校验的工具包"}});
Object.assign(words["en-US"],{expectedHash:"Expected SHA-256",actualHash:"Actual SHA-256",diagnosticSaved:"The last failed download and diagnostic report are retained until installation succeeds.",errors:{metadata:"No compatible package or invalid metadata",network:"Download connection failed",response:"Incomplete or invalid download response",download:"Download interrupted or timed out",size:"Downloaded size differs from the catalog",checksum:"Downloaded SHA-256 mismatch",archive:"Cannot open package archive",manifest:"Package manifest mismatch",write:"Cannot write or extract file",file_checksum:"Extracted file SHA-256 mismatch",probe:"Tool capability check failed",activate:"Cannot activate verified package"}});
Object.assign(words["ru-RU"],{expectedHash:"Ожидаемый SHA-256",actualHash:"Фактический SHA-256",diagnosticSaved:"Последняя неудачная загрузка и отчёт сохранены до успешной установки.",errors:{metadata:"Нет совместимого пакета или неверные метаданные",network:"Ошибка соединения",response:"Неполный или неверный ответ сервера",download:"Загрузка прервана или истекло время ожидания",size:"Размер загрузки не совпадает с каталогом",checksum:"SHA-256 загрузки не совпадает",archive:"Не удалось открыть архив",manifest:"Манифест пакета не совпадает",write:"Не удалось записать или извлечь файл",file_checksum:"SHA-256 извлечённого файла не совпадает",probe:"Проверка инструментов не пройдена",activate:"Не удалось активировать пакет"}});
export function createNotificationCenter({ updatePanel, navigation, openAccount = () => navigation.openSettings("account"), openDownloads = () => {} }) {
  const toolsBody = document.querySelector("[data-settings-toolpacks]");
  const entries = document.querySelectorAll("[data-notifications-open]");
  let cli = {}, distribution = {cli:{}}, update = null, account = null, diagnostics = null, diagnosticsFailed = false, pending = false, timer, destroyed = false, epoch = 0, view = null;
  const choices = createNoticePreferences();
  const copy = () => words[currentLocale()] ?? words["en-US"];
  const toolPanel=createToolPanel({copy,target:targetState,command,openFull:async()=>{
    try { await api.post("/update",{edition:"full"});await updatePanel?.refresh();updatePanel?.open(); }
    catch(error){distribution.message=copy().failure+" · "+error.message;renderTools();}
  }});
  const externalPanel=createExternalToolsPanel({refresh,openDownloads,signedIn:()=>account?.state==="signed_in",isMain:()=>navigation.get().view==="workspace",openFull:async()=>{
    await api.post("/update",{edition:"full"});await updatePanel?.refresh();updatePanel?.open();
  }});
  const indicators = createNoticeIndicators({ hosts: entries, copy, onOpen: openNotice });
  const key = () => targetState().selected ? `${targetState().selected.owner}/${targetState().selected.id}` : "local";
  const preferences = () => choices.forRun(key(), distribution.notification_session);
  const notices = () => buildNotices({ distribution, update, account, diagnostics, diagnosticsFailed, locale: currentLocale(), preferences: preferences(), copy: copy() });
  const warningPanel=createWarningPanel({scope:()=>`${key()}/${distribution.notification_session||""}`,refresh,
    onLater:n=>{choices.record(key(),distribution.notification_session,n,true);render();},onSettings:section=>navigation.openSettings(section)});
  function button(text, action) { const e = node("button", text, "secondary-button"); e.type = "button"; e.addEventListener("click", action); return e; }
  function close() { const own = view; view = null; own?.layer.dispose(); warningPanel.close(); }
  function openNotice(n) {
    if (destroyed || !n) return;
    indicators.hide(); close(); choices.record(key(), distribution.notification_session, n); render(); indicators.hide();
    if (!n.online) {
      if (n.action === "warnings") { warningPanel.open(n); return; }
      if (n.action === "update") { updatePanel?.open(); return; }
      if (n.action === "tools") { openTools(); return; }
      if (n.action === "account") { openAccount(); return; }
    }
    const own = { scope: key(), runtime: distribution.notification_session, notice: n };
    own.layer = openLayer({ title: n.title, icon: ["warning", "error"].includes(n.icon) ? "shield-alert" : "help", width: "38rem", height: "auto",
      onClosed() { if (view === own) view = null; } });
    own.layer.element.id = "notice-content-dialog"; own.layer.element.classList.add("notice-content-window");
    own.body = node("p", n.body, "notice-description"); own.layer.body.append(own.body);
    own.later = button(copy().later, () => {
      if (view !== own || own.scope !== key() || own.runtime !== distribution.notification_session) return;
      choices.record(own.scope, own.runtime, own.notice, true);
      void own.layer.close("later").then(closed => {
        if (!closed || destroyed || view || own.scope !== key() || own.runtime !== distribution.notification_session) return;
        render();
        const host = [...entries].find(entry => !entry.hidden && entry.getClientRects().length);
        (host?.querySelector("button") || document.querySelector("#prompt"))?.focus({ preventScroll: true });
        indicators.hide();
      });
    });
    own.close = button(copy().close, () => { if (view === own) void own.layer.close(); });
    own.layer.footer.append(own.later, own.close); view = own;
  }
  function render() {
    const list = notices(), c = copy();
    indicators.render(list, preferences(), `${key()}/${distribution.notification_session || ""}`);
    warningPanel.sync(list.find(n=>n.id==="system-warnings"));
    if (view) {
      const n = list.find(n => n.id === view.notice.id && n.revision === view.notice.revision);
      if (!n || view.scope !== key() || view.runtime !== distribution.notification_session) close();
      else {
        view.notice = n; view.layer.update({ title: n.title }); if (view.body.textContent !== n.body) view.body.textContent = n.body;
        view.later.textContent = c.later; view.close.textContent = c.close;
      }
    }
    renderTools();
  }
  function renderTools() {
    const data={...distribution,message:serviceMessages[currentLocale()]?.[distribution.message]??distribution.message};
    if(cli.platform){externalPanel.render(toolsBody,cli);}
    else {toolPanel.render(toolsBody,data);}
  }
  function openTools() { indicators.hide(); close(); navigation.openSettings("toolpacks"); }
  async function command(action, id) {
    try { await api.post("/distribution", { action, id }); await refresh(); }
    catch (error) { distribution.message = copy().failure + " · " + error.message; render(); }
  }
  async function refresh() {
    if (pending || destroyed) return false; pending = true; const generation = epoch;
    try {
      const results = await Promise.allSettled([api.get("/distribution"), api.get("/update"), api.get("/account"), api.get("/cli-tools"), api.get("/diagnostics")]);
      if (generation !== epoch || destroyed) return;
      if (results[0].status === "fulfilled") {
        const next=results[0].value.data;
        if(distribution.notification_session&&next.notification_session&&distribution.notification_session!==next.notification_session)diagnostics=null;
        distribution=next;
      }
      if (results[1].status === "fulfilled") update = results[1].value.data;
      if (results[2].status === "fulfilled") account = results[2].value.data;
      if (results[3].status === "fulfilled") cli=results[3].value.data;distribution.cli=cli;
      diagnosticsFailed=results[4].status!=="fulfilled";
      if(!diagnosticsFailed)diagnostics=results[4].value.data;
      render();
      return !diagnosticsFailed;
    } finally { pending = false; clearTimeout(timer); if (!destroyed && generation!==epoch) queueMicrotask(refresh);else if (!destroyed) timer = setTimeout(refresh, distribution.busy || update?.busy || (cli.tasks??[]).some(t=>["queued","running","installing","uploading"].includes(t.state)) ? 1000 : 30000); }
  }
  let scope = key();
  const locale = subscribeLocale(render), target = subscribeTarget(() => {
    if (scope === key()) { render(); return; }
    scope = key(); epoch++; close(); externalPanel.reset(); cli={}; distribution = {cli:{}}; update = account = diagnostics = null; diagnosticsFailed=false; render(); void refresh();
  });
  const route = navigation.subscribe(state => { renderTools();if (state.view === "settings" && state.settingsSection === "toolpacks") void refresh(); });
  const foreground = () => { if (!document.hidden) void refresh(); }; document.addEventListener("visibilitychange", foreground);
  render(); void refresh();
  return { refresh, destroy() { destroyed = true; clearTimeout(timer); close(); indicators.destroy(); externalPanel.destroy(); locale(); target(); route(); document.removeEventListener("visibilitychange", foreground); } };
}
