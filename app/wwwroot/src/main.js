// main.js — 启动装配（mdo 版：C 后端权威）
//
// 默认 MdoHost（mdo C 后端：xllm/xllm-session/xwork 进程内直调）。
// 离线（python -m http.server）或 ?host=fixture 切回 FixtureHost 演示。
// 参数：?host=fixture 离线演示   ?host=mdo 强制 mdo 后端（默认）
//       ?reset=1 忽略本地存档冷启动

import { Store } from './core.js';
import { FixtureHost, MdoHost } from './wire.js?v=7';
import { installScripts } from './fixture.js';
import { mountApp } from './ui.js?v=30';
import { applySettings, mountChrome, toast } from './chrome.js?v=42';
import { t, loadLang } from './i18n.js?v=1';

const params = new URLSearchParams(location.search);

const store = new Store();
store.models = [];
store.model = null;


// 主题默认跟随系统（原版语义），首次持久化后以存档为准
if (!localStorage.getItem('nativeharness:v1')) {
  store.settings.theme = matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
}

// 宿主选择：?host=fixture 强制离线；默认 mdo C 后端（连不上降级 fixture）
const hostParam = params.get('host') ?? '';
let host;
if (hostParam === 'fixture') {
  host = new FixtureHost(store, {
    scale: parseFloat(params.get('speed') ?? '1') || 1,
    autoApprove: params.get('autoapprove') === '1',
    instant: params.get('instant') === '1',
  });
  installScripts(host, store);
} else {
  host = new MdoHost(store, {});
  try {
    const probe = await fetch('api/health');
    if (!probe.ok) throw new Error('health ' + probe.status);
  } catch {
    host = new FixtureHost(store, {
      scale: parseFloat(params.get('speed') ?? '1') || 1,
      autoApprove: params.get('autoapprove') === '1',
      instant: params.get('instant') === '1',
    });
    installScripts(host, store);
    console.info('[host] mdo backend unreachable, falling back to fixture');
  }
}
store.host = host;
store.settings.autoApprove = params.get('autoapprove') === '1' || store.settings.autoApprove;

applySettings(store);

// 本地存档先恢复（拿回设置/主题/草稿），mdo 模式下会话随后以服务端为准重建
const restored = params.get('reset') === '1' ? false : store.restore();

// 语言包装载（restore 后、挂 UI 前）
try { await loadLang(store.settings.lang || 'zh'); } catch { /* 离线兑底 */ }

// ============ mdo 模式：模型表 + 会话全量水合（服务端权威，可重复调用） ============
async function hydrateMdo() {
  // 模型表（zcode 式配置；默认模型全局、每会话可覆盖）
  try {
    const md = await host.listModels();
    store.models = (md.models ?? []).map((m) => ({
      id: m.id, name: m.name || m.id,
      baseUrl: m.baseUrl, dialect: m.dialect, model: m.model,
      contextWindow: m.contextWindow ?? 128000,
    }));
    store.defaultModelId = md.defaultModel ?? store.models[0]?.id ?? null;
    if (!store.model || !store.models.some((m) => m.id === store.model.id)) {
      store.model = store.models.find((m) => m.id === store.defaultModelId) ?? store.models[0] ?? null;
    }
  } catch { /* 离线兜底：保持空表，fixture 不会走到这里 */ }

  // 项目表（面包屑切换器 + 侧栏分组数据源；含末尾「任务」默认分类）
  try {
    const pd = await host.listProjects();
    store._mdoProjects = pd.projects ?? [];
    store._mdoActiveProject = pd.active ?? '';
  } catch { store._mdoProjects = store._mdoProjects ?? []; }
  try { store._mdoAllSessions = await host.listAllSessions(); }
  catch { store._mdoAllSessions = store._mdoAllSessions ?? []; }

  // 清掉本地存档会话，换服务端会话列表 + 事件日志重放
  const resumeQueue = [];
  for (const id of [...store.order]) store.removeSession(id);
  try {
    const list = await host.listSessions();
    for (const item of list) {
      const s = store.createSession({ id: item.id, title: item.title, pinned: !!item.pinned });
      s.modelId = item.model || store.defaultModelId;
      s.turns = item.turns ?? 0;
      s.updatedAt = item.updatedAt ?? Date.now();
      const d = await host.fetchSessionEvents(item.id);
      let maxSeq = 0;
      for (const ev of d.events ?? []) {
        const local = s.log.append(ev.type, ev.data);
        local.time = ev.time ?? local.time;
        s.assembler.apply(local);
        if (ev.type === 'user/message') s.blank = false;
        if (ev.seq && ev.seq > maxSeq) maxSeq = ev.seq;
      }
      if (item.running && item.turnId) {   // D2：刷新前有活跃回合
        s.running = true;
        resumeQueue.push({ id: item.id, turnId: item.turnId, since: maxSeq });
      } else {
        s.running = false;
      }
      s.blank = s.log.events.length === 0;
      for (const n of s.nodes) n.streaming = false;
    }
  } catch { /* 后端瞬断：空列表起步 */ }
  if (!store.selectedId || !store.sessions.has(store.selectedId)) {
    const s = store.createSession();
    s.modelId = store.defaultModelId;
    s.lazy = true;
    store.select(s.id);
  }
  store.notifier.markDirty();
  store.persist();
  // D2：启动/刷新时有运行中的回合 → 选中并接管轮询（回答不丢）
  if (resumeQueue.length) {
    const first = resumeQueue[0];
    if (store.sessions.has(first.id)) {
      store.select(first.id);
      store.notifier.markDirty();
    }
    for (const r of resumeQueue) host.resumeTurn(r.id, r.turnId, r.since);
  }
}
/* 项目切换专用：会话列表显式点名目标 slug（服务端 activate 与列表扫描跨连接乱序免疫） */
async function hydrateProject(slug) {
  const resumeQueue = [];
  for (const id of [...store.order]) store.removeSession(id);
  try {
    const list = await host.listSessions(slug);
    for (const item of list) {
      const s = store.createSession({ id: item.id, title: item.title, pinned: !!item.pinned });
      s.modelId = item.model || store.defaultModelId;
      s.turns = item.turns ?? 0;
      s.updatedAt = item.updatedAt ?? Date.now();
      const d = await host.fetchSessionEvents(item.id);
      let maxSeq = 0;
      for (const ev of d.events ?? []) {
        const local = s.log.append(ev.type, ev.data);
        local.time = ev.time ?? local.time;
        s.assembler.apply(local);
        if (ev.type === 'user/message') s.blank = false;
        if (ev.seq && ev.seq > maxSeq) maxSeq = ev.seq;
      }
      if (item.running && item.turnId) {   // D2：刷新前有活跃回合
        s.running = true;
        resumeQueue.push({ id: item.id, turnId: item.turnId, since: maxSeq });
      } else {
        s.running = false;
      }
      s.blank = s.log.events.length === 0;
    }
  } catch { /* 瞬断 */ }
  if (!store.selectedId) {
    const s = store.createSession();
    s.modelId = store.defaultModelId;
    s.lazy = true;
    store.select(s.id);
  }
  store.notifier.markDirty();
  try { store._mdoAllSessions = await host.listAllSessions(); } catch { /* 瞬断 */ }
  // D2：优先选中运行中的会话并接管其回合轮询（刷新不丢进行中的回答）
  if (resumeQueue.length) {
    const first = resumeQueue[0];
    if (store.sessions.has(first.id)) {
      store.select(first.id);
      store.notifier.markDirty();
    }
    for (const r of resumeQueue) host.resumeTurn(r.id, r.turnId, r.since);
  }
}
if (host.name === 'mdo') {
  // 服务端设置优先（便携 config.json 是权威；失败沿用本地）
  try {
    const sd = await host.getSettings();
    const s = sd.settings ?? {};
    for (const k of ['theme', 'fontSize', 'lang', 'interactMode', 'sound', 'autoApprove', 'systemPrompt',
      'proxyEnabled', 'proxyHost', 'proxyPort', 'proxyUser', 'proxyPass',
      'proxyBypass', 'caCertPath', 'preventSleep']) {
      if (s[k] !== undefined) store.settings[k] = s[k];
    }
    applySettings(store);
    store.persist();
    const lgSrv = s.lang || 'zh';
    if (lgSrv !== (store.settings.lang || 'zh')) {
      store.settings.lang = lgSrv;
      store.persist();
      if (sessionStorage.getItem('mdoLangGo') !== lgSrv) {
        sessionStorage.setItem('mdoLangGo', lgSrv);   /* 环形保护：目标语言只重载一次 */
        try { await loadLang(lgSrv); location.reload(); } catch { /* 离线 */ }
      }
    }
  } catch { /* 离线 */ }
  // 变更观察：签名轮询对比，一动即防抖同步
  let lastSig = null;
  setInterval(() => {
    const s = store.settings;
    const sig = JSON.stringify([s.theme, s.fontSize, s.sound, s.autoApprove, s.systemPrompt,
      s.proxyEnabled, s.proxyHost, s.proxyPort, s.proxyUser, s.proxyPass, s.proxyBypass,
      s.caCertPath, s.preventSleep]);
    if (lastSig !== null && sig !== lastSig) store.scheduleSettingsSync();
    lastSig = sig;
  }, 1500);
  // 窗口尺寸上报（debounce 1.2s；xs 侧恢复在窗口状态批接入）
  let winTimer = null;
  const reportWin = () => {
    clearTimeout(winTimer);
    winTimer = setTimeout(() => {
      store.syncSettingsNow({
        winX: Math.round(window.screenX ?? 0), winY: Math.round(window.screenY ?? 0),
        winW: Math.round(window.outerWidth ?? 0), winH: Math.round(window.outerHeight ?? 0),
        winMax: (window.outerHeight ?? 0) >= window.screen.height * 0.92,
      });
    }, 1200);
  };
  window.addEventListener('resize', reportWin);
  window.addEventListener('move', reportWin);
  await hydrateMdo();
} else {
  if (!restored && host.name === 'fixture') host.seed();
  if (!store.selectedId) {
    const s = store.createSession();
    store.select(s.id);
  }
}

// 持久化：任何渲染后防抖 500ms 落盘（大附件配额溢出时静默降级为内存态）
let persistTimer = null;
store.notifier.subscribe(() => {
  clearTimeout(persistTimer);
  persistTimer = setTimeout(() => store.persist(), 500);
});

window.__app = window.__app || {};
window.__app.store = store;
window.__app.host = host;
/* zcode 式懒会话：点新会话只建本地占位（空白窗），首条消息发送时才真正建服务端会话 */
window.__app.newSession = () => {
  const prev = store.snapshot().selected;
  const s = store.createSession();
  s.modelId = prev?.modelId || store.model?.id;
  s.lazy = true;
  store.select(s.id);
  store.notifier.markDirty();
  import('./ui.js?v=30').then((m) => m.focusComposer());
};
window.__app.deleteSessionRemote = async (id) => {
  if (host.name !== 'mdo') return;
  if (store.sessions.get(id)?.lazy) return;   /* 懒会话纯本地，无需服务端 */
  try { await host.deleteSession(id); } catch (e) { console.warn('[mdo] delete:', e); }
};
/* 会话分叉：服务端完整复制 → 本地建会话 + 事件重放 + 选中 */
window.__app.forkSession = async (id) => {
  if (host.name !== 'mdo') throw new Error('mdo only');
  const meta = await host.forkSession(id);
  const s = store.createSession({ id: meta.id, title: meta.title });
  s.modelId = meta.model || store.defaultModelId;
  try {
    const d = await host.fetchSessionEvents(meta.id);
    for (const ev of d.events ?? []) {
      const local = s.log.append(ev.type, ev.data);
      local.time = ev.time ?? local.time;
      s.assembler.apply(local);
      if (ev.type === 'user/message') s.blank = false;
    }
  } catch { /* 瞬断 */ }
  s.turns = s.log.events.filter((e) => e.type === 'user/message').length;
  s.running = false;
  s.blank = s.log.events.length === 0;
  s.title = meta.title;   /* 重放里的 session/title 是原名；分叉名以服务端 meta 为准 */
  store.select(s.id);
  store.notifier.markDirty();
  store.persist();
  return meta;
};
window.__app.pinSessionRemote = async (id, pinned) => {
  if (host.name !== 'mdo') return;
  if (store.sessions.get(id)?.lazy) return;
  try { await host.pinSession(id, pinned); } catch (e) { console.warn('[mdo] pin:', e); }
};
window.__app.renameSessionRemote = async (id, title) => {
  if (host.name !== 'mdo') return;
  if (store.sessions.get(id)?.lazy) return;
  try { await host.renameSession(id, title); } catch (e) { console.warn('[mdo] rename:', e); }
};
/* Codex 式项目切换：激活 + 全量再水合（免刷新） */
window.__app.switchProject = async (slug) => {
  if (host.name !== 'mdo') return;
  try {
    await host.activateProject(slug);
    store._mdoActiveProject = slug;
    await hydrateProject(slug);
    const p = (store._mdoProjects ?? []).find((x) => x.slug === slug);
    if (p) toast(t('session.switchedProject', { name: p.name }));
  } catch (e) { console.warn('[mdo] switch project:', e); }
};
window.__app.refreshProjects = async () => {
  if (host.name !== 'mdo') return;
  try {
    const pd = await host.listProjects();
    store._mdoProjects = pd.projects ?? [];
    store._mdoActiveProject = pd.active ?? '';
    store.notifier.markDirty();
  } catch { /* 瞬断忽略 */ }
};
/** 全库会话清单刷新（侧栏跨项目分组/置顶区数据源） */
window.__app.refreshAllSessions = async () => {
  if (host.name !== 'mdo') return;
  try {
    store._mdoAllSessions = await host.listAllSessions();
    store.notifier.markDirty();
  } catch { /* 瞬断忽略 */ }
};
/** 跨项目打开会话：同项目直接选中；跨项目先切换再选中 */
window.__app.openSessionInProject = async (slug, sid) => {
  const active = store._mdoActiveProject || '_tasks';
  if (slug === active) { store.select(sid); store.notifier.markDirty(); return; }
  await window.__app.switchProject(slug);
  // 切换重建后选中目标会话（若不存在则保持默认选择）
  if (store.sessions.has(sid)) { store.select(sid); store.notifier.markDirty(); }
};

// 外壳（三列框架 / 侧栏 / 头部 / 详情列）
mountChrome(store, host, {
  sidebar: document.getElementById('sidebar-col'),
  topbar: document.getElementById('topbar'),
  details: document.getElementById('details-col'),
  hooks: {
    newSession: () => window.__app.newSession(),
    openSettings: () => import('./chrome.js?v=42').then((m) => m.openSettings(store)),
    openHelp: () => import('./chrome.js?v=42').then((m) => m.openHelp()),
  },
});

// 对话视图
mountApp(store, host, {
  cvRoot: document.getElementById('cv-root'),
  chatScroll: document.getElementById('chat-scroll'),
  chatList: document.getElementById('chat-list'),
  docks: document.getElementById('docks'),
  composerWrap: document.getElementById('composer-wrap'),
  composerSeat: document.getElementById('composer-seat'),
});

if (params.get('autostart') === '1' && host.runShowcase) {
  setTimeout(() => host.runShowcase(store.selectedId), 400);
}
