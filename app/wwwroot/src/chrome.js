// chrome.js — 应用外壳（对齐 XHarness：AppFrame 三列 / SidebarRoot / ChatView 头部 / DetailsPanel）
// 与 ui.js 单向依赖：本模块只引 dom.js/icons.js；动作经 mountChrome 的 hooks 注入。

import { el } from './dom.js';
import { t, loadLang } from './i18n.js?v=1';
import { icon, appIcon, stateDot } from './icons.js?v=5';

// —— @ 文件补全的 mock 工作区（模板项：接后端换成 workspace/listFiles）——
export const MOCK_FILES = [
  'src/app.ts', 'src/core/engine.ts', 'src/cli/index.ts', 'src/dashboard/chart.ts',
  'scripts/build.mjs', 'package.json', 'tsconfig.json', 'tests/engine.test.ts', 'css/app.css', 'README.md',
];

// —— 斜杠命令表（数据；执行在 ui.js 的 composer）——
export const SLASH_COMMANDS = [
  { cmd: '/demo', descKey: 'cmd.demo' },
  { cmd: '/image', descKey: 'cmd.image' },
  { cmd: '/model', descKey: 'cmd.model' },
  { cmd: '/theme', descKey: 'cmd.theme' },
  { cmd: '/export', descKey: 'cmd.export' },
  { cmd: '/clear', descKey: 'cmd.clear' },
  { cmd: '/settings', descKey: 'cmd.settings' },
  { cmd: '/help', descKey: 'cmd.help' },
];

// ============ Toast ============
let toastRoot = null;
export function toast(msg) {
  if (!toastRoot) { toastRoot = el('div', { class: 'toast-root' }); document.body.append(toastRoot); }
  const t = el('div', { class: 'toast' }, msg);
  toastRoot.append(t);
  setTimeout(() => { t.classList.add('out'); setTimeout(() => t.remove(), 300); }, 2400);
}

// ============ 弹窗系统 ============
let overlayEl = null;
export function closeModal() { overlayEl?.remove(); overlayEl = null; }
export function openModal({ title, body, actions = [{ label: t('act.close') }], wide = false }) {
  closeModal();
  overlayEl = el('div', { class: 'overlay', onclick: (e) => { if (e.target === overlayEl) closeModal(); } },
    el('div', { class: 'modal' + (wide ? ' wide' : '') },
      el('div', { class: 'modal-head' },
        el('span', { class: 'modal-title' }, title),
        el('button', { class: 'modal-x', onclick: closeModal }, icon('IconCloseFill14', { size: 14 }))),
      el('div', { class: 'modal-body' }, body),
      actions.length ? el('div', { class: 'modal-foot' },
        actions.map((a) => el('button', {
          class: 'dsw-btn' + (a.primary ? ' primary' : '') + (a.danger ? ' danger' : ''),
          onclick: () => { if (a.onClick?.() !== true) closeModal(); },
        }, a.label))) : null));
  document.body.append(overlayEl);
  return overlayEl;
}
export function confirmModal({ title, message, okLabel = t('act.delete'), onOk }) {
  openModal({
    title,
    body: el('div', { class: 'confirm-body' }, message),
    actions: [{ label: t('act.cancel') }, { label: okLabel, danger: true, onClick: onOk }],
  });
}
export function promptModal({ title, label, value = '', onOk }) {
  const input = el('input', { class: 'modal-input', value });
  const submit = () => { if (input.value.trim()) { onOk(input.value.trim()); closeModal(); } };
  input.addEventListener('keydown', (e) => { if (e.key === 'Enter') submit(); });
  openModal({
    title,
    body: el('div', { class: 'confirm-body' }, el('label', null, label), input),
    actions: [{ label: t('act.cancel') }, { label: t('act.ok'), primary: true, onClick: () => { if (!input.value.trim()) return true; onOk(input.value.trim()); } }],
  });
  setTimeout(() => input.focus(), 30);
}

// ============ 全局 Tooltip：body 级单例（不受任何祖先裁剪，自动定向） ============
const tipEl = el('div', { class: 'gtip' });
document.body.append(tipEl);
let tipAnchor = null;
let tipTimer = 0;
const hideTip = () => { clearTimeout(tipTimer); tipEl.classList.remove('show'); tipAnchor = null; };
function showTip(target) {
  tipEl.textContent = target.getAttribute('data-tip');
  tipEl.classList.add('show');
  const r = target.getBoundingClientRect();
  const tw = tipEl.offsetWidth, th = tipEl.offsetHeight;
  let x = r.left + r.width / 2 - tw / 2;
  x = Math.max(8, Math.min(x, innerWidth - tw - 8));      // 水平夹紧不出屏
  const below = r.top < th + 12;                            // 贴近视口顶部 → 朝下
  tipEl.classList.toggle('below', below);
  tipEl.style.left = Math.round(x) + 'px';
  tipEl.style.top = Math.round((below ? r.bottom + 6 : r.top - th - 6)) + 'px';
}
document.addEventListener('pointerover', (e) => {
  const t = e.target instanceof Element ? e.target.closest('[data-tip]') : null;
  if (t === tipAnchor) return;
  clearTimeout(tipTimer);
  tipAnchor = t;
  if (!t) { tipEl.classList.remove('show'); return; }
  tipTimer = setTimeout(() => { if (tipAnchor === t) showTip(t); }, 100);
});
document.addEventListener('pointerdown', hideTip);
document.addEventListener('scroll', hideTip, true);

// ============ 图片放大（全局委托） ============
document.addEventListener('click', (e) => {
  const img = e.target.closest('.user-img img, .md img, .msg-imgs img');
  if (!img || !img.src) return;
  closeModal();
  overlayEl = el('div', { class: 'overlay lightbox', onclick: closeModal }, el('img', { src: img.src }));
  document.body.append(overlayEl);
});

// ============ 设置应用（主题：body[data-ds-dark-theme]） ============
/* 主题三态：auto=跟随系统（matchMedia 实时联动），light/dark 固定 */
export function applySettings(store) {
  const t = store.settings.theme === 'light' || store.settings.theme === 'dark'
    ? store.settings.theme
    : (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
  document.body.toggleAttribute('data-ds-dark-theme', t === 'dark');

  document.documentElement.dataset.theme = store.settings.theme === 'auto' ? 'auto' : t;
  document.documentElement.dataset.fs = store.settings.fontSize;
}
matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
  const s = window.__app?.store?.settings;
  if (s && s.theme === 'auto') applySettings(window.__app.store);
});

// ============ 导出会话 Markdown ============
export function exportSessionMarkdown(store, session) {
  if (!session) return;
  const lines = [`# ${session.title}`, '', `_导出于 ${new Date().toLocaleString()} · NativeHarness_`, ''];
  for (const n of session.nodes) {
    if (n.kind === 'user') {
      lines.push('## 用户', '', n.text || '', ...(n.images?.length ? ['', ...n.images.map((i) => `![${i.name ?? '附件'}](${i.dataUrl})`)] : []), '');
    } else if (n.kind === 'assistant') {
      lines.push('## 助手', '');
      for (const b of n.blocks ?? []) {
        if (b.type === 'text') lines.push(b.text, '');
        else if (b.type === 'image') lines.push(`![${b.name ?? '图片'}](${b.dataUrl})`, '');
      }
    } else if (n.kind === 'tool') {
      lines.push(`**${n.name}** \`${JSON.stringify(n.args)}\``, '', '```', String(n.result ?? ''), '```', '');
    }
  }
  const blob = new Blob([lines.join('\n')], { type: 'text/markdown;charset=utf-8' });
  const a = el('a', { href: URL.createObjectURL(blob), download: `${session.title.replace(/[\\/:*?"<>|]/g, '_')}.md` });
  a.click();
  URL.revokeObjectURL(a.href);
  toast('已导出 Markdown');
}

// ============ AppFrame：三列 + 拖拽（columns.js 契约几何）============
const FRAME = {
  SIDEBAR_MIN: 264, SIDEBAR_MAX: 420, SIDEBAR_DEFAULT: 280, SIDEBAR_COLLAPSED: 56,
  AUTO_COLLAPSE: 1024, DETAILS_MIN: 300, DETAILS_MAX: 520, DETAILS_DEFAULT: 360, CENTER_MIN: 640,
};
function clamp(px, min, max) { return Math.min(max, Math.max(min, Math.round(px))); }
function computeColumns(viewport, sidebar, details) {
  const s = sidebar === 0 ? FRAME.SIDEBAR_COLLAPSED : clamp(sidebar, FRAME.SIDEBAR_MIN, FRAME.SIDEBAR_MAX);
  const d0 = details === 0 ? 0 : clamp(details, FRAME.DETAILS_MIN, FRAME.DETAILS_MAX);
  if (s + d0 + FRAME.CENTER_MIN <= viewport) return { sidebar: s, center: viewport - s - d0, details: d0 };
  const d1 = d0 === 0 ? 0 : Math.max(FRAME.DETAILS_MIN, viewport - s - FRAME.CENTER_MIN);
  if (s + d1 + FRAME.CENTER_MIN <= viewport) return { sidebar: s, center: FRAME.CENTER_MIN, details: d1 };
  return { sidebar: s, center: Math.max(0, viewport - s), details: 0 };
}

function mountFrame(store) {
  const app = document.getElementById('app');
  const state = { sidebarPref: FRAME.SIDEBAR_DEFAULT, detailsPref: 0, narrowExpanded: false, dragging: false };

  const sidebarHandle = el('div', { class: 'drag-handle', 'data-side': 'sidebar' });
  const detailsHandle = el('div', { class: 'drag-handle', 'data-side': 'details' });
  app.append(sidebarHandle, detailsHandle);

  function collapsed() {
    return innerWidth < FRAME.AUTO_COLLAPSE ? !state.narrowExpanded : state.sidebarPref === 0;
  }
  function apply() {
    const pref = collapsed() ? 0 : (state.sidebarPref || FRAME.SIDEBAR_DEFAULT);
    const cols = computeColumns(innerWidth, pref, state.detailsPref);
    app.style.gridTemplateColumns = `${cols.sidebar}px minmax(0,1fr) ${cols.details}px`;
    app.toggleAttribute('data-sidebar-collapsed', collapsed());
    app.toggleAttribute('data-details-collapsed', cols.details === 0);
    app.toggleAttribute('data-dragging', state.dragging);
    sidebarHandle.style.left = cols.sidebar + 'px';
    sidebarHandle.style.display = collapsed() ? 'none' : '';
    detailsHandle.style.left = (innerWidth - cols.details) + 'px';
    detailsHandle.style.display = cols.details > 0 ? '' : 'none';
    return cols;
  }
  apply();
  window.addEventListener('resize', () => { apply(); store.notifier.markDirty(); });

  function dragify(handle, side) {
    let origin = 0, base = 0, latest = 0, frame = null;
    handle.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      handle.setPointerCapture(e.pointerId);
      handle.setAttribute('data-dragging', '');
      origin = latest = e.clientX;
      base = side === 'sidebar' ? state.sidebarPref : state.detailsPref;
      state.dragging = true; apply();
    });
    handle.addEventListener('pointermove', (e) => {
      if (!handle.hasPointerCapture(e.pointerId)) return;
      latest = e.clientX;
      if (frame === null) {
        frame = requestAnimationFrame(() => {
          frame = null;
          const dx = latest - origin;
          if (side === 'sidebar') state.sidebarPref = clamp(base + dx, FRAME.SIDEBAR_MIN, FRAME.SIDEBAR_MAX);
          else state.detailsPref = clamp(base - dx, FRAME.DETAILS_MIN, FRAME.DETAILS_MAX);
          apply();
        });
      }
    });
    const up = (e) => {
      if (!handle.hasPointerCapture(e.pointerId)) return;
      handle.releasePointerCapture(e.pointerId);
      handle.removeAttribute('data-dragging');
      if (frame !== null) { cancelAnimationFrame(frame); frame = null; }
      state.dragging = false; apply(); store.persist();
    };
    handle.addEventListener('pointerup', up);
    handle.addEventListener('pointercancel', up);
  }
  dragify(sidebarHandle, 'sidebar');
  dragify(detailsHandle, 'details');

  function expand() {
    if (innerWidth < FRAME.AUTO_COLLAPSE) state.narrowExpanded = true;
    else state.sidebarPref = state.sidebarPref || FRAME.SIDEBAR_DEFAULT;
  }
  return {
    apply,
    toggleSidebar() {
      if (collapsed()) expand();
      else if (innerWidth < FRAME.AUTO_COLLAPSE) state.narrowExpanded = false;
      else state.sidebarPref = 0;
      apply(); store.notifier.markDirty();
    },
    expandSidebar: expand,
    collapseSidebar() {
      if (innerWidth < FRAME.AUTO_COLLAPSE) state.narrowExpanded = false;
      else state.sidebarPref = 0;
      apply(); store.notifier.markDirty();
    },
    setDetails(open) {
      state.detailsPref = open ? (state.detailsPref || FRAME.DETAILS_DEFAULT) : 0;
      apply(); store.notifier.markDirty();
    },
    detailsOpen: () => state.detailsPref > 0,
    collapsed,
  };
}

// ============ 侧栏（SidebarRoot + WorkspaceBrowser）============
const fmtClock = (t) => new Date(t).toTimeString().slice(0, 5);
/* Codex 式相对时间 */
const fmtRel = (t) => {
  const d = Date.now() - t;
  if (d < 60e3) return '刚刚';
  if (d < 3600e3) return `${Math.floor(d / 60e3)} 分钟前`;
  if (d < 86400e3) return `${Math.floor(d / 3600e3)} 小时前`;
  if (d < 2 * 86400e3) return t('time.yesterday');
  if (d < 7 * 86400e3) return `${Math.floor(d / 86400e3)} 天前`;
  const dt = new Date(t);
  return `${dt.getMonth() + 1}/${dt.getDate()}`;
};
const fmtDay = (t) => {
  const d = new Date(t);
  const today = new Date(); today.setHours(0, 0, 0, 0);
  if (d.getTime() >= today.getTime()) return t('time.today');
  if (d.getTime() >= today.getTime() - 86400000) return t('time.yesterday');
  if (d.getTime() >= today.getTime() - 7 * 86400000) return t('time.days7');
  return t('time.earlier');
};

function mountSidebar(root, store, frame, hooks) {
  root.classList.add('sidebar');
  let sessionFilter = '';
  const searchInput = el('input', { type: 'search', placeholder: t('sidebar.searchPh') });

  // —— 设置视图侧栏：返回 + 分组导航（替代会话列表区）——
  const stNav = el('div', { class: 'stnav' });
  const settingsPane = el('div', { class: 'sidebar-wide' },
    el('div', { class: 'sidebar-head' },
      el('span', { class: 'logo-mark' }, appIcon({ size: 22 })),
      el('div', { class: 'head-text' },
        el('div', { class: 'logo-title' }, 'mdo 墨斗'),
        el('div', { class: 'logo-sub' }, t('sidebar.settings'))),
      el('button', { class: 'icon-btn sb-toggle', 'data-tip': t('sidebar.collapse'), onclick: () => frame.toggleSidebar() },
        icon('IconPanelLeftOutline16', { size: 16 }))),
    el('button', { class: 'stnav-back', onclick: () => { settingsState.active = false; store.notifier.markDirty(); } },
      icon('IconChevronLeftOutline14', { size: 14 }), el('span', null, t('sidebar.backToWork'))),
    stNav);

  // —— 展开态骨架（旧版布局；搜索框常驻，不再做展开动画）——
  const listEl = el('div', { class: 'session-list' });
  const schedBox = el('div', { class: 'sched-box' });   // Codex 式：定时任务行 + 可展开清单
  const wide = el('div', { class: 'sidebar-wide' },
    el('div', { class: 'sidebar-head' },
      el('span', { class: 'logo-mark' }, appIcon({ size: 22 })),
      el('div', { class: 'head-text' },
        el('div', { class: 'logo-title' }, 'mdo 墨斗'),
        el('div', { class: 'logo-sub' }, t('app.logoSub'))),
      el('button', { class: 'icon-btn sb-toggle', 'data-tip': t('sidebar.collapse'), onclick: () => frame.toggleSidebar() },
        icon('IconPanelLeftOutline16', { size: 16 }))),
    el('button', { class: 'new-session', onclick: hooks.newSession },
      icon('IconNewChatOutline16', { size: 14 }), el('span', null, t('sidebar.newSession'))),
    schedBox,
    el('div', { class: 'session-search' }, searchInput),
    listEl,
    el('div', { class: 'sidebar-foot' },
      el('span', { class: 'conn-dot' }),
      el('span', { class: 'foot-text' }, t('sidebar.connected') + ' · ' + (store.host?.name ?? 'fixture')),
      el('span', { class: 'foot-spacer' }),
      el('button', { class: 'icon-btn foot-btn', 'data-tip': t('sidebar.help'), onclick: hooks.openHelp }, icon('IconQuestionOutline14', { size: 14 })),
      el('button', { class: 'icon-btn foot-btn', 'data-tip': t('sidebar.toggleTheme'), onclick: hooks.toggleTheme }, icon('IconDarkOutline16', { size: 16 })),
      el('button', { class: 'icon-btn foot-btn' + (settingsState.active ? ' on' : ''), 'data-tip': t('sidebar.settings'), onclick: () => { settingsState.active ? closeSettings(store) : hooks.openSettings(); } }, icon('IconSettingsOutline16', { size: 16 }))),
  );

  // —— 折叠 rail（zcode 式：内容整体切换，不靠 CSS 挤压）——
  const rail = el('div', { class: 'sidebar-rail' },
    el('button', { class: 'icon-btn rail-btn', 'data-tip': t('sidebar.expand'), onclick: () => frame.toggleSidebar() },
      appIcon({ size: 20 })),
    el('button', {
      class: 'icon-btn rail-btn', 'data-tip': t('sidebar.newSession'),
      onclick: () => { if (settingsState.active) { settingsState.active = false; } hooks.newSession(); },
    },
      icon('IconNewChatOutline16', { size: 18 })),
    el('span', { class: 'rail-gap' }),
    el('button', { class: 'icon-btn rail-btn', 'data-tip': t('sidebar.toggleTheme'), onclick: hooks.toggleTheme }, icon('IconDarkOutline16', { size: 16 })),
    el('button', {
      class: 'icon-btn rail-btn' + (settingsState.active ? ' on' : ''), 'data-tip': t('sidebar.settings'),
      onclick: () => { if (settingsState.active) { settingsState.active = false; store.notifier.markDirty(); } else hooks.openSettings(); },
    }, icon('IconSettingsOutline16', { size: 16 })));

  searchInput.addEventListener('input', () => {
    sessionFilter = searchInput.value;
    renderList();
  });

  /* —— Codex 式侧栏功能行：定时任务（页面入口 → 右侧主区管理界面）—— */
  let schedItems = [];
  function refreshSched() { store.host.listSchedules().then((x) => { schedItems = x; renderSchedBox(); }).catch(() => {}); }
  function renderSchedBox() {
    const active = schedViewState.active;
    const head = el('div', { class: 'sched-row' + (active ? ' on' : ''), onclick: () => {
      openSchedulesPage(store);
      refreshSched();
    } },
      icon('IconChecklistOutline14', { size: 15 }),
      el('span', { class: 'sched-row-text' }, t('sched.sideTitle')),
      el('span', { class: 'sched-row-count' }, String(schedItems.filter((s) => s.enabled).length)));
    schedBox.replaceChildren(head);
  }
  renderSchedBox();
  refreshSched();
  setInterval(refreshSched, 30000);

  /* 侧栏会话列表（zcode 式）：顶部跨项目置顶区 + 项目分组（会话=项目二级分类），
   * 末位是「任务」默认分类（不属于任何项目的会话）。 */
  function renderList() {
    const snap = store.snapshot();
    const q = sessionFilter.trim().toLowerCase();
    const projects = (store._mdoProjects ?? []);
    const activeSlug = store._mdoActiveProject || '_tasks';
    const all = (store._mdoAllSessions ?? []).filter((x) => !q || (x.title ?? '').toLowerCase().includes(q));
    const match = (t) => !q || t.toLowerCase().includes(q);

    const row = (opts) => el('div', {
      class: 'session-item sub' + (opts.active ? ' active' : ''),
      dataset: { sid: opts.id },
      onclick: opts.onclick,
    },
      el('span', { class: 's-dot' + (opts.running ? ' running' : opts.done ? ' done' : '') }),
      el('span', { class: 's-title' }, opts.title,
        opts.modelId ? el('span', { class: 's-model' }, opts.modelId) : null),
      el('span', { class: 's-time', 'data-tip': new Date(opts.updatedAt).toLocaleString() }, fmtRel(opts.updatedAt)),
      opts.acts ? el('span', { class: 's-acts' }, ...opts.acts) : null);

    const liveActs = (s) => [
      el('button', { class: 'icon-btn s-act', 'data-tip': t('sidebar.pin'), onclick: (e) => {
        e.stopPropagation();
        store.togglePin(s.id);
        window.__app.pinSessionRemote?.(s.id, s.pinned);
        window.__app.refreshAllSessions?.();
      } }, icon('IconPinTop14', { size: 14 })),
      el('button', { class: 'icon-btn s-act danger', 'data-tip': t('sidebar.delSession'), onclick: (e) => {
        e.stopPropagation();
        hooks.deleteSession(s);
        window.__app.refreshAllSessions?.();
      } }, icon('IconTrashOutline16', { size: 14 })),
    ];

    /* —— 顶部：跨项目置顶区 —— */
    const pinned = all.filter((x) => x.pinned)
      .sort((a, b) => (b.updatedAt ?? 0) - (a.updatedAt ?? 0));

    /* —— 分组：当前项目用实时会话表，其余项目用全库轻量清单 —— */
    const groups = [];
    for (const pj of projects) {
      const isActive = pj.slug === activeSlug;
      let items;
      if (isActive) {
        items = snap.sessions.filter((s) => !s.lazy && match(s.title))
          .map((s) => ({
            id: s.id, title: s.title, modelId: s.modelId, updatedAt: s.updatedAt,
            running: s.running, done: s.done, active: s.id === store.selectedId,
            onclick: () => store.select(s.id), acts: liveActs(s),
          }));
      } else {
        items = all.filter((x) => x.project === pj.slug)
          .sort((a, b) => (b.updatedAt ?? 0) - (a.updatedAt ?? 0))
          .map((x) => ({
            id: x.id, title: x.title, modelId: x.model, updatedAt: x.updatedAt,
            running: !!x.running, done: false, active: false,
            onclick: () => window.__app.openSessionInProject?.(pj.slug, x.id),
            acts: null,
          }));
      }
      groups.push({ pj, isActive, items });
    }

    listEl.replaceChildren(
      /* 顶部常驻：+ 添加项目（行内表单，Codex 式；无需进设置页） */
      el('div', { class: 'group-label' },
        el('span', { style: 'flex:1' }, t('sidebar.projectsLabel')),
        el('button', {
          class: 'icon-btn s-act', 'data-tip': t('sidebar.addProject'),
          onclick: () => {
            const exist = listEl.querySelector('.pj-add-inline');
            if (exist) { exist.querySelector('input').focus(); return; }
            const input = el('input', { placeholder: t('projects.inlinePh') });
            const rowEl = el('div', { class: 'pj-add-inline' }, input,
              el('button', { class: 'icon-btn s-act', 'data-tip': t('act.add'), onclick: async () => {
                const v = input.value.trim();
                if (!v) return;
                try {
                  await store.host.addProject(v);
                  await window.__app.refreshProjects?.();
                  await window.__app.refreshAllSessions?.();
                  toast(t('act.ok'));
                } catch (e) { toast(e.message.replace(/^.*→ /, '')); }
              } }, icon('IconNewChatOutline16', { size: 13 })));
            input.addEventListener('keydown', (e) => { if (e.key === 'Enter') rowEl.querySelector('button').click(); if (e.key === 'Escape') rowEl.remove(); });
            listEl.prepend(el('div', { class: 'group-label' }, rowEl));
            input.focus();
          },
        }, icon('IconNewChatOutline16', { size: 13 }))),
      ...(pinned.length ? [
        el('div', { class: 'group-label pin-label' },
          icon('IconPinTop14', { size: 12 }), t(' 置顶任务 · {n}', { n: pinned.length })),
        ...pinned.map((x) => {
          return row({
            id: x.id, title: x.title, modelId: x.model, updatedAt: x.updatedAt,
            running: !!x.running, done: false,
            active: x.id === store.selectedId && store.sessions.has(x.id),
            onclick: () => window.__app.openSessionInProject?.(x.project, x.id),
            acts: store.sessions.has(x.id)
              ? liveActs(store.sessions.get(x.id))
              : null,
          });
        }),
      ] : []),
      ...groups.flatMap((g) => [
        el('div', {
          class: 'group-label pj-group',
          onclick: () => window.__app.newSessionInProject?.(g.pj.slug),
        },
          icon(g.pj.tasks ? 'IconChecklistOutline14' : 'IconFolderClose16', { size: 13 }),
          el('span', { class: 'pj-group-name' }, g.pj.tasks ? t('sidebar.tasks') : g.pj.name),
          el('span', { class: 'pj-group-count' }, String(g.items.length)),
          el('span', { class: 'pj-group-acts' },
            el('button', {
              class: 'icon-btn s-act', 'data-tip': t('sidebar.newInProject'),
              onclick: (e) => { e.stopPropagation(); window.__app.newSessionInProject?.(g.pj.slug); },
            }, icon('IconNewChatOutline16', { size: 13 })),
            g.pj.tasks ? null : el('button', {
              class: 'icon-btn s-act', 'data-tip': t('sidebar.manageProject'),
              onclick: (e) => { e.stopPropagation(); window.__app.projectMenu?.(g.pj, e.currentTarget); },
            }, icon('IconSettingsOutline16', { size: 13 })))),
        ...g.items.map((it) => row(it)),
        ...(g.items.length === 0 ? [el('div', { class: 'no-hit sub' }, t('sidebar.noSession'))] : []),
      ]),
    );
  }

  function renderStNav() {
    stNav.replaceChildren(...ST_SECTIONS.flatMap((g) => [
      el('div', { class: 'group-label' }, t(NAV_LABEL[g.group] ?? g.group, null, g.group)),
      ...g.items.map((it) => el('div', {
        class: 'stnav-item' + (settingsState.section === it.id ? ' active' : ''),
        onclick: () => { settingsState.section = it.id; store.notifier.markDirty(); },
      },
        icon(it.icon, { size: 16 }),
        el('span', null, t(NAV_LABEL[it.id] ?? it.id, null, it.id)))),
    ]));
  }
  function render() {
    frame.apply();   // 渲染前同步框架几何（resize 事件丢失时自愈）
    if (settingsState.active && !frame.collapsed()) {
      root.replaceChildren(settingsPane);
      root.classList.remove('rail-mode');
      renderStNav();
      return;
    }
    // 设置态下折叠：沿用对话 rail（齿轮 on，点齿轮退出设置）
    const c = frame.collapsed();
    const cur = root.firstElementChild;
    const want = c ? rail : (settingsState.active ? settingsPane : wide);
    if (cur !== want) root.replaceChildren(want);
    root.classList.toggle('rail-mode', c);
    if (!settingsState.active) renderList();
  }
  render();
  return { render };
}

// ============ ChatView 头部（lvQYKa）============
function mountHeader(root, store, host, frame, hooks) {
  let menuOpen = false;
  let modelOpen = false;
  const crumbs = el('div', { class: 'cv-crumbs' });
  const tabs = el('div', { class: 'cv-tabs' });
  const actions = el('div', { class: 'cv-header-actions' });

  let projOpen = false;
  function renderCrumbs() {
    const s = store.snapshot().selected;
    const projects = store._mdoProjects ?? [];
    const active = projects.find((p) => p.slug === store._mdoActiveProject);
    const projLabel = active ? active.name : (store.host?.name === 'mdo' ? t('crumb.selectProject') : 'workspace');
    const menu = (projOpen && store.host?.name === 'mdo') ? el('div', { class: 'dropdown proj-menu' },
      ...projects.map((p) => el('button', {
        class: p.slug === store._mdoActiveProject ? 'on' : '',
        onclick: async () => {
          projOpen = false; renderCrumbs();
          if (p.slug !== store._mdoActiveProject) await window.__app.switchProject(p.slug);
        },
      }, icon('IconPanelLeftOutline16', { size: 14 }), p.name, p.slug === store._mdoActiveProject ? ' ✓' : '')),
      el('button', {
        onclick: () => { projOpen = false; renderCrumbs(); hooks.openSettings(); },
      }, icon('IconSettingsOutline16', { size: 14 }), t('crumb.manage'))) : null;
    crumbs.replaceChildren(
      el('button', {
        class: 'cv-crumb proj-crumb',
        'data-tip': projects.length ? t('crumb.switch') : t('crumb.none'),
        onclick: () => { projOpen = !projOpen; renderCrumbs(); },
      }, projLabel, el('span', { class: 'crumb-caret' }, '▾')),
      el('span', { class: 'cv-crumb-sep' }, '/'),
      el('button', { class: 'cv-crumb current' }, s?.title ?? '未选择'),
      ...(menu ? [menu] : []));
  }
  function renderTabs() {
    tabs.replaceChildren(
      el('button', { class: 'cv-tab' + (!frame.detailsOpen() ? ' active' : ''), onclick: () => frame.setDetails(false) }, t('tab.chat')),
      el('button', { class: 'cv-tab' + (frame.detailsOpen() ? ' active' : ''), onclick: () => frame.setDetails(!frame.detailsOpen()) }, t('tab.trace')));
  }
  function renderActions() {
    // D1：懒会话（未发首条消息）/ 无会话——会话级操作（导出/重命名/分叉/删除）无对象，整组隐藏
    const selS = store.snapshot().selected;
    if (!selS || selS.lazy) { actions.replaceChildren(); return; }
    // el() 会过滤 null 子节点，replaceChildren 不会——菜单关闭时传 null 会渲染出字面量 "null"
    const menu = menuOpen ? el('div', { class: 'dropdown' },
      el('button', { onclick: () => { menuOpen = false; hooks.renameSession(); } }, icon('IconEditOutline16', { size: 16 }), t('tab.rename')),
      el('button', { onclick: () => { menuOpen = false; hooks.togglePin(); } }, icon('IconPinTop14', { size: 16 }), t('sidebar.pinToggle')),
      el('button', { onclick: async () => {
        menuOpen = false;
        const s = store.snapshot().selected;
        if (!s || s.running) { toast(t('session.stopFirst')); return; }
        try {
          toast(t('act.forking'));
          const meta = await window.__app.forkSession(s.id);
          toast(t('act.forked') + meta.title);
        } catch (e) { toast(t('act.forkFail') + e.message); }
      } }, icon('IconBranchOutline16', { size: 16 }), t('act.forkMenu')),
      el('button', { class: 'danger', onclick: () => { menuOpen = false; hooks.deleteSession(store.snapshot().selected); } }, icon('IconTrashOutline16', { size: 16 }), t('sidebar.delSession'))) : null;
    actions.replaceChildren(
      el('button', { class: 'icon-btn', 'data-tip': t('act.export'), onclick: hooks.exportSession }, icon('IconDownloadOutline16', { size: 16 })),
      el('button', { class: 'icon-btn', 'data-tip': t('act.more'), onclick: () => { menuOpen = !menuOpen; renderActions(); } }, icon('IconEllipsisOutline16', { size: 16 })),
      ...(menu ? [menu] : []));
  }
  document.addEventListener('click', (e) => {
    if (menuOpen && !e.target.closest('.cv-header-actions')) { menuOpen = false; renderActions(); }
    if (projOpen && !e.target.closest('.proj-crumb') && !e.target.closest('.proj-menu')) {
      projOpen = false; renderCrumbs();
    }
  });

  function render() {
    renderCrumbs(); renderTabs(); renderActions();
  }
  root.replaceChildren(
    el('div', { class: 'cv-title-row' },
      el('div', { class: 'cv-title-cluster' }, crumbs),
      actions),
    tabs);
  render();
  return { render };
}

// ============ 详情列（JXxdLa + 轨迹占位）============
function mountDetails(root, store) {
  const body = el('div', { class: 'dt-body' });
  root.replaceChildren(
    el('div', { class: 'dt-root' },
      el('div', { class: 'dt-header' },
        el('span', { class: 'dt-title' }, t('tab.trace')),
        el('button', { class: 'icon-btn', 'data-tip': t('act.close') }, icon('IconCloseOutline16', { size: 14 }))),
      body));
  root.querySelector('.dt-header .icon-btn').onclick = () => { window.__app.frame.setDetails(false); };

  function render() {
    const s = store.snapshot().selected;
    if (!s) { body.replaceChildren(el('div', { class: 'dt-empty' }, t('trace.noSel'))); return; }
    const events = s.log.events.slice(-200).reverse();
    body.replaceChildren(
      el('div', { class: 'dt-section' },
        el('div', { class: 'dt-section-label' }, t('事件流（最近 {n} 条，倒序）', { n: events.length })),
        el('div', null, events.map((ev) => el('div', { class: 'traj-row' },
          el('span', { class: 'traj-time' }, fmtClock(ev.time)),
          el('span', { class: 'traj-type' }, ev.type),
          el('span', { class: 'traj-desc' }, describe(ev)))))));
  }
  function describe(ev) {
    const d = ev.data ?? {};
    if (ev.type === 'user/message') return (d.content ?? []).filter((p) => p.type === 'text').map((p) => p.text).join(' ');
    if (ev.type === 'tool/result') return `#${d.callId} ${String(d.output ?? '').split('\n')[0]}`;
    if (ev.type === 'tool/update') return `#${d.callId} 补丁`;
    if (ev.type === 'assistant/message') return (d.message?.content ?? []).map((b) => b.type).join('+');
    return '';
  }
  render();
  return { render };
}

const fmtK = (n) => n >= 1000 ? (n / 1000).toFixed(1).replace(/\.0$/, '') + 'K' : String(n);

// ============ 设置：ZCode 式整页视图（侧栏导航 + 主区卡片页） ============
const settingsState = { active: false, section: '外观', prevCollapsed: false };
const schedViewState = { active: false };   // Codex 式：侧栏「定时任务」= 独立页面入口（右侧主区）
const NAV_LABEL = {
  '外观': 'settings.nav.appearance', '常规': 'settings.nav.general',
  '模型设置': 'settings.nav.modelCfg', '模型管理': 'settings.nav.models',
  '项目管理': 'settings.nav.projects', '网络': 'settings.nav.network',
  '数据管理': 'settings.nav.data',
  '反馈': 'settings.nav.feedback',
  '计划任务': 'settings.nav.schedules',
  '基础设置': 'settings.group.basic', '数据': 'settings.group.data',
};
export function openSettings(store) {
  const frame = window.__app && window.__app.frame;
  schedViewState.active = false;             // 页面入口互斥
  settingsState.prevCollapsed = frame ? frame.collapsed() : false;
  settingsState.active = true;
  if (frame) frame.expandSidebar();          // 设置导航需要宽度：强制展开
  store.notifier.markDirty();
}
/** 直达指定设置节（hero 第五卡 → 计划任务） */
export function openSettingsSection(store, sectionId) {
  schedViewState.active = false;             // 页面入口互斥
  openSettings(store);
  settingsState.section = sectionId;
  store.notifier.markDirty();
}
/** 打开定时任务页面（右侧主区管理界面；设置态互斥） */
export function openSchedulesPage(store) {
  settingsState.active = false;
  schedViewState.active = true;
  const frame = window.__app && window.__app.frame;
  if (frame) frame.expandSidebar();
  store.notifier.markDirty();
}
export function isSchedulesView() { return schedViewState.active; }
export function closeSettings(store) {
  const frame = window.__app && window.__app.frame;
  settingsState.active = false;
  schedViewState.active = false;             // 任何退出路径都回对话视图
  if (frame) {
    if (settingsState.prevCollapsed) frame.collapseSidebar();   // 还原进入前的收起态
    else frame.expandSidebar();
  }
  store.notifier.markDirty();
}
export function isSettingsView() { return settingsState.active; }

const SEG = (opts, val, onch) => {
  const seg = el('div', { class: 'seg' });
  for (const [v, label] of opts) {
    seg.append(el('button', {
      class: 'seg-btn' + (v === val ? ' on' : ''),
      onclick: (e) => {
        for (const b of seg.querySelectorAll('.seg-btn')) b.classList.remove('on');
        e.currentTarget.classList.add('on');   // 即时高亮：状态回写由 onch 负责，不依赖整页重渲染
        onch(v);
      },
    }, label));
  }
  return seg;
};

const ST_ROW = (title, desc, control) => el('div', { class: 'st-row' },
  el('div', { class: 'st-row-text' },
    el('div', { class: 'st-row-title' }, title),
    desc ? el('div', { class: 'st-row-desc' }, desc) : null),
  control);

function stSection(store, id) {
  const s = store.settings;
  if (id === '外观') return [el('div', { class: 'st-card' },
    ST_ROW(t('settings.lang'), t('settings.langDesc'), SEG([['zh', t('settings.langZh')], ['en', t('settings.langEn')], ['ru', t('settings.langRu')]], s.lang || 'zh', (v) => { s.lang = v; store.persist(); Promise.resolve(store.syncSettingsNow ? store.syncSettingsNow() : null).catch(() => {}).finally(() => location.reload()); })),
    ST_ROW(t('settings.theme'), t('settings.themeDesc'), SEG([['auto', t('settings.themeAuto')], ['light', t('settings.themeLight')], ['dark', t('settings.themeDark')]], s.theme, (v) => { s.theme = v; applySettings(store); store.persist(); })),
    ST_ROW(t('settings.fontSize'), t('settings.fontDesc'), SEG([['sm', t('settings.sizeSm')], ['md', t('settings.sizeMd')], ['lg', t('settings.sizeLg')]], s.fontSize, (v) => { s.fontSize = v; applySettings(store); store.persist(); })))];
  if (id === '常规') return [el('div', { class: 'st-card' },
    ST_ROW(t('settings.interact'), t('settings.interactDesc'),
      SEG([['queue', t('settings.interactQueue')], ['guide', t('settings.interactGuide')]], s.interactMode || 'queue',
        (v) => { s.interactMode = v; store.persist(); store.scheduleSettingsSync?.(); })),
        ST_ROW(t('settings.sound'), t('settings.soundDesc'), SEG([['on', t('settings.on')], ['off', t('settings.off')]], s.sound ? 'on' : 'off', (v) => { s.sound = v === 'on'; store.persist(); store.scheduleSettingsSync?.(); })),
    ST_ROW(t('settings.autoApprove'), t('settings.autoApproveDesc'), SEG([['on', t('settings.on')], ['off', t('settings.off')]], s.autoApprove ? 'on' : 'off', (v) => { s.autoApprove = v === 'on'; store.persist(); store.scheduleSettingsSync?.(); })),
    ST_ROW(t('settings.preventSleep'), t('settings.sleepDesc'),
      SEG([['on', t('settings.on')], ['off', t('settings.off')]], s.preventSleep ? 'on' : 'off', (v) => { s.preventSleep = v === 'on'; store.persist(); store.scheduleSettingsSync?.(); })))];
  if (id === '模型设置') return [el('div', { class: 'st-card' },
    el('div', { class: 'st-row col' },
      el('div', { class: 'st-row-text' },
        el('div', { class: 'st-row-title' }, t('settings.sysPrompt')),
        el('div', { class: 'st-row-desc' }, t('settings.sysPromptDesc'))),
      (() => {
        const ta = el('textarea', { class: 'modal-input sysprompt', rows: '4', placeholder: t('settings.sysPromptPh') });
        ta.value = s.systemPrompt;
        ta.addEventListener('change', () => { s.systemPrompt = ta.value; store.persist(); });
        return ta;
      })()))];
  if (id === '反馈') return mdoFeedbackSection(store);
  if (id === '计划任务') return mdoSchedulesSection(store);
  if (id === '数据管理') return [el('div', { class: 'st-card' },
    ST_ROW(t('settings.clearAll'), t('settings.clearAllDesc'),
      el('button', {
        class: 'dsw-btn danger', onclick: () => confirmModal({
          title: t('settings.clearAll'), message: t('settings.clearAllDesc'),
          onOk: () => { store.clearPersisted(); location.reload(); },
        }),
      }, t('settings.clearAll'))))];
  if (id === '模型管理') return mdoModelsSection(store);
  if (id === '项目管理') return mdoProjectsSection(store);
  if (id === '网络') return [netSection(store)];
  return [];
}

/* ============ 网络设置：HTTP 代理（引擎接线批3完成后全效；配置先行持久化） ============ */
function netSection(store) {
  const f = (label, key, ph, type = 'text') => {
    const inp = el('input', { class: 'modal-input st-fld', type, placeholder: ph,
      value: String(store.settings[key] ?? '') });
    inp.addEventListener('change', () => { store.settings[key] = inp.value; store.persist(); store.scheduleSettingsSync?.(); });
    return el('label', { class: 'st-fld-row' }, el('span', { class: 'st-fld-label' }, label), inp);
  };
  const hostInp = el('input', { class: 'modal-input st-fld', placeholder: '127.0.0.1', value: store.settings.proxyHost ?? '' });
  const portInp = el('input', { class: 'modal-input st-fld', type: 'number', placeholder: '7890', value: String(store.settings.proxyPort ?? '') });
  const bypInp = el('input', { class: 'modal-input st-fld', placeholder: 'localhost,127.0.0.1,*.internal', value: store.settings.proxyBypass ?? '' });
  hostInp.addEventListener('change', () => { store.settings.proxyHost = hostInp.value.trim(); store.persist(); store.scheduleSettingsSync?.(); });
  portInp.addEventListener('change', () => { store.settings.proxyPort = parseInt(portInp.value, 10) || 0; store.persist(); store.scheduleSettingsSync?.(); });
  bypInp.addEventListener('change', () => { store.settings.proxyBypass = bypInp.value.trim(); store.persist(); store.scheduleSettingsSync?.(); });
  return el('div', { class: 'st-card' },
    ST_ROW(t('settings.proxyEnable'), t('settings.proxyDesc'),
      SEG([['on', t('settings.on')], ['off', t('settings.off')]], store.settings.proxyEnabled ? 'on' : 'off',
        (v) => { store.settings.proxyEnabled = v === 'on'; store.persist(); store.scheduleSettingsSync?.(); })),
    el('div', { class: 'st-row col' },
      el('div', { class: 'st-fld-row' }, el('span', { class: 'st-fld-label' }, t('settings.proxyHost')), hostInp),
      el('div', { class: 'st-fld-row' }, el('span', { class: 'st-fld-label' }, t('settings.proxyPort')), portInp),
      f(t('settings.proxyUser'), 'proxyUser', t('settings.proxyUserPh')),
      f(t('settings.proxyPass'), 'proxyPass', '', 'password'),
      el('div', { class: 'st-fld-row' }, el('span', { class: 'st-fld-label' }, t('settings.proxyBypass')), bypInp),
      el('div', { class: 'st-row-desc' }, t('settings.proxyBypassDesc'))),
    el('div', { class: 'st-row col' },
      el('div', { class: 'st-row-text' },
        el('div', { class: 'st-row-title' }, t('settings.caCert')),
        el('div', { class: 'st-row-desc' }, 'PEM 文件或目录（目录时拼接全部 .pem/.crt/.cer）；对所有模型的 TLS 校验生效，与模型自带 CA 叠加')),
      (() => {
        const caInp = el('input', { class: 'modal-input st-fld', placeholder: 'D:\certs\my-ca.pem 或 D:\certs\dir',
          value: store.settings.caCertPath ?? '' });
        caInp.addEventListener('change', () => { store.settings.caCertPath = caInp.value.trim(); store.persist(); store.scheduleSettingsSync?.(); });
        return el('div', { class: 'st-fld-row' }, el('span', { class: 'st-fld-label' }, t('settings.caPath')), caInp);
      })()));
}

// ============ mdo：模型管理（zcode 式双栏 master-detail） ============

const DIALECT_OPTS = [['openai', t('models.dialectOpenai')], ['responses', 'OpenAI Responses'], ['anthropic', 'Anthropic'], ['glm', t('models.dialectGlm')]];
const REASONING_OPTS = [['', t('models.default')], ['low', t('models.low')], ['medium', t('settings.sizeMd')], ['high', t('models.high')]];

const mmHost = (m) => { try { return new URL(m.baseUrl).host || m.baseUrl; } catch { return m.baseUrl || '未分组'; } };
const mmHue = (s) => { let h = 0; for (const c of String(s)) h = (h * 31 + c.codePointAt(0)) % 360; return h; };
const mmCtx = (n) => (n >= 1000 ? `${Math.round(n / 1000)}K` : String(n ?? '?'));
const mmAvatar = (m) => el('span', { class: 'mm-avatar', style: `background:hsl(${mmHue(m.id || m.name)} 48% 42%)` },
  (m.name || m.id || '?').trim().charAt(0).toUpperCase());

function mdoModelsSection(store) {
  const host = store.host;
  if (host?.name !== 'mdo') {
    return [el('div', { class: 'st-card' }, el('div', { class: 'st-row-desc' }, t('models.offline')))];
  }
  let sel = null;          // 选中模型 id
  let adding = false;      // 新增表单态
  const page = el('div', { class: 'mm-page' });

  function syncStoreModels() {   // 与 main.js hydrateMdo 同映射：保存/删除后模型选择器即时生效
    const list = store._mdoModels ?? [];
    store.models = list.map((m) => ({ id: m.id, name: m.name || m.id, baseUrl: m.baseUrl, dialect: m.dialect, model: m.model, contextWindow: m.contextWindow ?? 128000 }));
    store.defaultModelId = store._mdoDefault ?? store.models[0]?.id ?? null;
    if (!store.model || !store.models.some((m) => m.id === store.model.id)) {
      store.model = store.models.find((m) => m.id === store.defaultModelId) ?? store.models[0] ?? null;
    }
  }

  async function refresh(keepSel = true) {
    try {
      const d = await host.listModels();
      store._mdoModels = d.models ?? [];
      store._mdoDefault = d.defaultModel;
    } catch { store._mdoModels = store._mdoModels ?? []; }
    syncStoreModels();
    if (!keepSel || !store._mdoModels.some((m) => m.id === sel)) sel = store._mdoDefault ?? store._mdoModels[0]?.id ?? null;
    render();
  }

  const fld = (label, key, val, ph, opt = {}) => {
    const inp = el('input', { class: 'modal-input', type: opt.type ?? 'text', placeholder: ph,
      value: String(val ?? ''), disabled: opt.disabled ? '' : undefined });
    inp.dataset.key = key;
    return el('div', { class: 'mm-fld' + (opt.full ? ' full' : '') }, el('label', null, label), inp);
  };
  const selFld = (label, key, val, opts, opt = {}) => {
    const s = el('select', { class: 'modal-input' });
    s.dataset.key = key;
    for (const [ov, name] of opts) s.append(el('option', { value: ov, selected: ov === val ? '' : undefined }, name));
    return el('div', { class: 'mm-fld' + (opt.full ? ' full' : '') }, el('label', null, label), s);
  };

  function detail(m) {   // m=null → 新增表单
    const isEdit = !!m;
    if (isEdit && m.builtin) {
      // 内置模型：只展示名称与上下文窗口 + 一封给用户的信（参数与 key 一概不示人）
      const letterParas = (t('models.letter') || '').split('\n\n').filter(Boolean);
      return el('div', { class: 'mm-detail' },
        el('div', { class: 'mm-d-head' },
          appIcon({ size: 30 }),
          el('div', { class: 'mm-d-text' },
            el('div', { class: 'mm-d-title' }, m.name || m.id),
            el('div', { class: 'mm-d-sub' }, `${m.id} · ${mmCtx(m.contextWindow)} ctx`)),
          el('span', { class: 'mm-badge builtin' }, t('models.builtin'))),
        el('div', { class: 'mm-d-body' },
          el('div', { class: 'mm-sec' },
            el('div', { class: 'mm-grid' },
              el('div', { class: 'mm-fld' },
                el('label', null, t('models.nameField')),
                el('div', { class: 'mm-ro-value' }, m.name || m.id)),
              el('div', { class: 'mm-fld' },
                el('label', null, t('models.ctxWindow')),
                el('div', { class: 'mm-ro-value' }, `${mmCtx(m.contextWindow)} tokens`)))),
          el('div', { class: 'mm-letter' },
            el('div', { class: 'mm-letter-title' }, t('models.letterTitle')),
            el('div', { class: 'mm-letter-body' },
              ...letterParas.map((para) => el('p', null, para)))),
          m.id !== store._mdoDefault ? el('div', { class: 'mm-foot' },
            el('button', {
              class: 'dsw-btn', onclick: async () => {
                try { await host.saveModel(m, true); toast(t('models.setDefOk')); await refresh(); }
                catch (e) { toast(t('models.setDefFail') + e.message); }
              },
            }, t('models.setDef'))) : null));
    }
    const v = (k, d = '') => m?.[k] ?? d;
    const form = el('form', { class: 'mm-form' });
    form.addEventListener('submit', (e) => e.preventDefault());

    const keyInp = el('input', { class: 'modal-input', type: 'password', placeholder: t('models.apiKeyPh'), value: v('apiKey') });
    keyInp.dataset.key = 'apiKey';
    const eyeBtn = el('button', { class: 'mm-eye', type: 'button' }, t('models.show'));
    eyeBtn.addEventListener('click', () => {
      const show = keyInp.type === 'password';
      keyInp.type = show ? 'text' : 'password';
      eyeBtn.textContent = show ? t('models.hide') : t('models.show');
    });

    form.append(
      el('div', { class: 'mm-sec' }, el('div', { class: 'mm-sec-label' }, t('models.basicInfo')),
        el('div', { class: 'mm-grid' },
          fld(t('models.idField'), 'id', v('id'), 'ling-gpu', { disabled: isEdit }),
          fld(t('models.nameField'), 'name', v('name'), 'Ling-3.0 · GPU'))),
      el('div', { class: 'mm-sec' }, el('div', { class: 'mm-sec-label' }, t('models.conn')),
        el('div', { class: 'mm-grid' },
          fld('Base URL', 'baseUrl', v('baseUrl'), 'https://host/v1', { full: true }),
          el('div', { class: 'mm-fld full' }, el('label', null, 'API Key'), el('div', { class: 'mm-key-wrap' }, keyInp, eyeBtn)),
          selFld(t('models.apiFormat'), 'dialect', v('dialect') || 'openai', DIALECT_OPTS))),
      el('div', { class: 'mm-sec' }, el('div', { class: 'mm-sec-label' }, t('models.params')),
        el('div', { class: 'mm-grid' },
          fld(t('models.wireName'), 'model', v('model'), 'ling-3.0-tiny'),
          selFld(t('models.reasoning'), 'reasoning', v('reasoning'), REASONING_OPTS),
          fld(t('models.ctxWindow'), 'contextWindow', v('contextWindow', 128000), '131072'),
          fld(t('models.maxOut'), 'maxOutput', v('maxOutput', 0), '0'),
          fld(t('models.caPem'), 'caPem', v('caPem'), 'C:\\path\\ca.crt', { full: true }))));

    const saveBtn = el('button', {
      class: 'dsw-btn primary', type: 'button', onclick: async () => {
        const model = {};
        for (const inp of form.querySelectorAll('[data-key]')) {
          const k = inp.dataset.key;
          model[k] = k === 'contextWindow' || k === 'maxOutput' ? (parseInt(inp.value, 10) || 0) : inp.value.trim();
        }
        if (!model.id || !model.baseUrl || !model.model) { toast(t('models.required')); return; }
        if (!model.name) model.name = model.id;
        if (!model.contextWindow) model.contextWindow = 128000;
        if (!model.dialect) model.dialect = 'openai';
        try {
          await host.saveModel(model, store._mdoDefault === model.id);
          toast(isEdit ? t('models.updated') : t('models.added'));
          adding = false; sel = model.id;
          await refresh();
        } catch (e) { toast(t('models.saveFail') + e.message); }
      },
    }, isEdit ? t('act.save') : '添加模型');

    const defBtn = isEdit && m.id !== store._mdoDefault ? el('button', {
      class: 'dsw-btn', type: 'button', onclick: async () => {
        try { await host.saveModel(m, true); toast('models.setDefOk'); await refresh(); }
        catch (e) { toast(t('models.setDefFail') + e.message); }
      },
    }, t('models.setDef')) : null;

    const delBtn = isEdit && !m.builtin ? el('button', {
      class: 'dsw-btn danger', type: 'button', onclick: () => confirmModal({
        title: t('models.delTitle'), message: t('删除「{name}」？正在使用它的会话将无法继续。', { name: m.name || m.id }),
        onOk: async () => {
          try { await host.deleteModel(m.id); toast(t('models.deleted')); sel = null; await refresh(); }
          catch (e) { toast(t('models.delFail') + e.message); }
        },
      }),
    }, t('act.delete')) : null;

    return el('div', { class: 'mm-detail' },
      el('div', { class: 'mm-d-head' },
        adding ? el('span', { class: 'mm-avatar mm-avatar-add' }, '+') : mmAvatar(m),
        el('div', { class: 'mm-d-text' },
          el('div', { class: 'mm-d-title' }, adding ? t('models.new') : (m.name || m.id)),
          el('div', { class: 'mm-d-sub' }, adding ? t('models.newHint') : `${m.id} · ${m.dialect || 'openai'} · ${mmCtx(m.contextWindow)} ctx`)),
        el('span', { class: 'mm-d-badges' },
          m.id === store._mdoDefault ? el('span', { class: 'mm-badge' }, t('models.default')) : null,
          m.builtin ? el('span', { class: 'mm-badge builtin' }, t('models.builtin')) : null)),
      el('div', { class: 'mm-d-body' }, form),
      el('div', { class: 'mm-foot' }, saveBtn, defBtn, el('span', { class: 'mm-foot-sp' }), delBtn));
  }

  function render() {
    const list = store._mdoModels ?? [];
    const groups = new Map();
    for (const m of list) {
      const h = mmHost(m);
      if (!groups.has(h)) groups.set(h, []);
      groups.get(h).push(m);
    }
    const listEl = el('div', { class: 'mm-list' },
      ...[...groups.entries()].flatMap(([h, ms]) => [
        el('div', { class: 'mm-group' }, h),
        ...ms.map((m) => el('button', {
          class: 'mm-item' + (m.id === sel && !adding ? ' active' : ''),
          type: 'button',
          onclick: () => { sel = m.id; adding = false; render(); },
        },
          mmAvatar(m),
          el('span', { class: 'mm-item-text' },
            el('span', { class: 'mm-item-name' }, m.name || m.id),
            el('span', { class: 'mm-item-sub' }, `${m.model || '?'} · ${m.dialect || 'openai'} · ${mmCtx(m.contextWindow)} ctx`)),
          m.id === store._mdoDefault ? el('span', { class: 'mm-badge' }, t('models.default')) : null,
          m.builtin ? el('span', { class: 'mm-badge builtin' }, t('models.builtin')) : null)),
      ]),
      list.length === 0 ? el('div', { class: 'mm-list-empty' }, t('models.emptyList')) : null);

    const m = adding ? null : (list.find((x) => x.id === sel) ?? null);
    const detailEl = (adding || m)
      ? detail(m)
      : el('div', { class: 'mm-detail' }, el('div', { class: 'mm-empty' },
          list.length === 0 ? t('models.emptyDetail') : t('models.pickHint')));

    page.replaceChildren(
      el('div', { class: 'mm-head' },
        el('div', { class: 'mm-desc' },
          `管理自定义模型接入，配置后可在会话中选择使用。数据保存在程序目录 data/ 下（便携、可整目录备份）。当前默认：${store._mdoDefault ?? t('models.noneDef')}`),
        el('div', { class: 'mm-actions' },
          el('button', { class: 'icon-btn', 'data-tip': t('models.refresh'), onclick: () => refresh() }, icon('IconRefreshOutline16', { size: 15 })),
          el('button', { class: 'dsw-btn primary', onclick: () => { adding = true; render(); } },
            icon('IconPlusOutline16', { size: 13 }), t('models.btnNew')))),
      el('div', { class: 'mm-card' }, listEl, detailEl));
    /* 自渲染：不 markDirty（外层重渲染会换掉本节，形成空循环） */
  }

  refresh(false);
  return [page];
}

// ============ mdo：项目管理（codex/zcode 式工作区分桶） ============

function mdoFeedbackSection(store) {
  const host = store.host;
  const card = el('div', { class: 'st-card' });
  card.replaceChildren(el('div', { class: 'st-row-desc' }, t('feedback.empty')));
  host.listFeedback().then((items) => {   // 同步骨架 + 异步填充（stSection 为同步 spread）
    if (!items.length) return;
      const rows = items.map((f) => el('div', { class: 'st-row' },
        el('div', { class: 'st-row-text' },
          el('div', { class: 'st-row-title' }, (f.value === 'good' ? '\u{1F44D} ' : '\u{1F44E} ') + (f.title || f.sessionId)),
          el('div', { class: 'st-row-desc' }, new Date(f.time).toLocaleString())),
        el('span', { class: 'mm-badge ' + (f.value === 'good' ? '' : 'warn') },
          f.value === 'good' ? t('act.like') : t('act.dislike'))));
      card.replaceChildren(
        el('div', { class: 'st-row col' },
          el('div', { class: 'st-row-desc' }, t('feedback.count', { n: items.length }))),
        ...rows);
  }).catch(() => { /* 瞬断：保留空态 */ });
  return [card];
}

/* ============ 计划任务（闹钟）：任务列表 + 新建/编辑表单（API 五件已就绪） ============ */
const CRON_PRESETS = [
  ['0 9 * * *', 'sched.preset.daily9'],
  ['0 9 * * 1-5', 'sched.preset.weekday9'],
  ['0 9 * * 1', 'sched.preset.monday9'],
  ['*/30 * * * *', 'sched.preset.every30'],
];

function schedText(s) {
  if (s.kind === 'interval') return t('sched.everyMin', { n: s.intervalMin });
  if (s.kind === 'once') return t('sched.onceIn', { n: s.delayMin });
  const hit = CRON_PRESETS.find((p) => p[0] === s.cron);
  return hit ? t(hit[1]) : t('sched.cronPrefix') + ' ' + s.cron;
}

function schedNextText(s) {
  if (!s.enabled) return t('sched.paused');
  if (!s.nextDue) return '';
  const d = s.nextDue - Date.now();
  if (d <= 0) return t('sched.dueNow');
  const m = Math.round(d / 60000);
  if (m < 60) return t('sched.inMin', { n: m });
  const h = Math.round(m / 60);
  if (h < 48) return t('sched.inHour', { n: h });
  return t('sched.inDay', { n: Math.round(h / 24) });
}

function mdoSchedulesSection(store) {
  const host = store.host;
  if (host?.name !== 'mdo') {
    return [el('div', { class: 'st-card' }, el('div', { class: 'st-row-desc' }, t('projects.offline')))];
  }
  const page = el('div', { class: 'mm-page' });
  let items = [];
  let projects = [];
  let editing = null;
  const form = { title: '', prompt: '', kind: 'once', delayMin: 10, intervalMin: 30, cron: '0 9 * * *', project: '', miss: 'skip' };

  async function refresh() {
    try {
      items = await host.listSchedules();
      if (!projects.length) {
        const d = await host.listProjects().catch(() => null);
        if (d?.projects) projects = d.projects;
      }
    } catch { /* 瞬断：保留旧表 */ }
    render();
  }

  function taskCard(s) {
    const badges = [];
    if (s.running) badges.push(el('span', { class: 'mm-badge' }, t('sched.runningBadge')));
    if (!s.enabled && !s.running) badges.push(el('span', { class: 'mm-badge warn' }, t('sched.paused')));
    const titleRow = el('div', { class: 'pj-name' }, s.title, ...badges);
    const info = el('div', { class: 'pj-info' },
      titleRow,
      el('div', { class: 'pj-path' }, schedText(s) + ' · ' + schedNextText(s)));
    const metaBits = [t('sched.ranCount', { n: s.runCount })];
    if (s.lastResult) metaBits.push(s.lastResult);
    const meta = el('div', { class: 'st-row-desc sched-meta' }, metaBits.join(' — '));
    const histBtn = (s.runs && s.runs.length)
      ? el('button', { class: 'dsw-btn', onclick: () => showHistory(s) }, t('sched.history', { n: s.runs.length }))
      : null;
    const toggle = el('button', {
      class: 'dsw-btn',
      onclick: async () => { try { await host.updateSchedule({ id: s.id, enabled: !s.enabled }); refresh(); } catch (e) { toast(e.message); } },
    }, s.enabled ? t('sched.pause') : t('sched.resume'));
    const runBtn = el('button', {
      class: 'dsw-btn',
      onclick: async () => { try { await host.runSchedule(s.id); toast(t('sched.runNowOk')); refresh(); } catch (e) { toast(e.message); } },
    }, t('sched.runNow'));
    const editBtn = el('button', {
      class: 'dsw-btn',
      onclick: () => {
        editing = s;
        form.title = s.title; form.prompt = s.prompt; form.kind = s.kind;
        form.delayMin = s.delayMin || 10; form.intervalMin = s.intervalMin || 30; form.cron = s.cron || '0 9 * * *';
        form.project = s.project || ''; form.miss = s.miss || 'skip';
        render();
      },
    }, t('act.edit'));
    const delBtn = el('button', {
      class: 'dsw-btn danger',
      onclick: () => confirmModal({
        title: t('sched.deleteTitle', { title: s.title }),
        message: t('sched.deleteDesc'),
        onOk: async () => {
          try { await host.deleteSchedule(s.id); refresh(); } catch (e) { toast(e.message); }
        },
      }),
    }, t('act.delete'));
    const bar = el('div', { class: 'pj-bar' }, toggle, runBtn, histBtn, editBtn, delBtn);
    return el('div', { class: 'pj-card' }, info, meta, bar);
  }

  function showHistory(s) {
    const rows = (s.runs || []).slice().reverse().map((r) =>
      el('div', { class: 'st-row' },
        el('div', { class: 'st-row-text' },
          el('div', { class: 'st-row-title' }, new Date(r.time).toLocaleString()),
          el('div', { class: 'st-row-desc' }, r.sessionId)),
        el('span', { class: 'mm-badge ' + (r.status === 'done' ? '' : 'warn') }, t('sched.st.' + r.status))));
    openModal({
      title: t('sched.historyTitle', { title: s.title }),
      body: el('div', { class: 'st-card sched-hist' }, ...rows),
    });
  }

  function formCard() {
    const isEdit = !!editing;
    const titleIn = el('input', { class: 'modal-input', placeholder: t('sched.titlePh') });
    titleIn.value = form.title;
    const promptTa = el('textarea', { class: 'modal-input', rows: '3', placeholder: t('sched.promptPh') });
    promptTa.value = form.prompt;
    const cronSel = el('select', { class: 'modal-input' });
    for (const [c, key] of CRON_PRESETS) cronSel.append(el('option', { value: c }, t(key)));
    cronSel.append(el('option', { value: 'custom' }, t('sched.preset.custom')));
    cronSel.value = CRON_PRESETS.some((p) => p[0] === form.cron) ? form.cron : 'custom';
    const cronIn = el('input', { class: 'modal-input', placeholder: '0 9 * * 1-5' });
    cronIn.value = form.cron;
    const cronCtl = el('div', { class: 'sched-cronrow' }, cronSel, cronIn);
    const syncKind = () => {
      cronCtl.style.display = form.kind === 'cron' ? '' : 'none';
      cronRow.style.display = form.kind === 'cron' ? '' : 'none';
      delayRow.style.display = form.kind === 'once' ? '' : 'none';
      intRow.style.display = form.kind === 'interval' ? '' : 'none';
    };
    const delayIn = el('input', { class: 'modal-input', type: 'number', min: '1', max: '525600' });
    delayIn.value = form.delayMin;
    const intIn = el('input', { class: 'modal-input', type: 'number', min: '1', max: '525600' });
    intIn.value = form.intervalMin;
    const projSel = el('select', { class: 'modal-input' });
    projSel.append(el('option', { value: '' }, t('sidebar.tasks')));
    for (const p of projects) projSel.append(el('option', { value: p.slug }, p.name));
    projSel.value = form.project;
    const kindSeg = SEG([
      ['once', t('sched.kind.once')],
      ['interval', t('sched.kind.interval')],
      ['cron', t('sched.kind.cron')],
    ], form.kind, (v) => { form.kind = v; syncKind(); });
    const missSeg = SEG([
      ['skip', t('sched.miss.skip')],
      ['catchup', t('sched.miss.catchup')],
    ], form.miss, (v) => { form.miss = v; });
    const delayRow = ST_ROW(t('sched.delayTitle'), t('sched.delayDesc'), delayIn);
    const intRow = ST_ROW(t('sched.intervalTitle'), t('sched.intervalDesc'), intIn);
    const cronRow = ST_ROW(t('sched.cronLabel'), t('sched.cronDesc'), cronCtl);
    const saveBtn = el('button', {
      class: 'dsw-btn primary',
      onclick: async () => {
        const payload = {
          title: titleIn.value.trim(),
          prompt: promptTa.value.trim(),
          kind: form.kind,
          miss: form.miss,
          project: projSel.value,
          cron: cronSel.value === 'custom' ? cronIn.value.trim() : cronSel.value,
          delayMin: Math.max(0, parseInt(delayIn.value, 10) || 0),
          intervalMin: Math.max(1, parseInt(intIn.value, 10) || 1),
        };
        if (!payload.title || !payload.prompt) { toast(t('sched.needTitlePrompt')); return; }
        try {
          if (isEdit) await host.updateSchedule({ id: editing.id, ...payload });
          else await host.createSchedule(payload);
          editing = null;
          form.title = ''; form.prompt = '';
          toast(isEdit ? t('sched.updated') : t('sched.created'));
          refresh();
        } catch (e) { toast(e.message); }
      },
    }, isEdit ? t('sched.saveEdit') : t('sched.create'));
    const cancelBtn = isEdit
      ? el('button', { class: 'dsw-btn', onclick: () => { editing = null; form.title = ''; form.prompt = ''; render(); } }, t('act.cancel'))
      : null;
    const card = el('div', { class: 'st-card pj-add' },
      el('div', { class: 'mm-d-badges' }, el('span', { class: 'st-row-title' }, isEdit ? t('sched.editTitle', { title: editing.title }) : t('sched.newTitle'))),
      ST_ROW(t('sched.titleLabel'), null, titleIn),
      el('div', { class: 'st-row col' },
        el('div', { class: 'st-row-title' }, t('sched.promptLabel')),
        promptTa),
      ST_ROW(t('sched.kindLabel'), t('sched.kindDesc'), kindSeg),
      cronRow, delayRow, intRow,
      ST_ROW(t('sched.projectLabel'), t('sched.projectDesc'), projSel),
      ST_ROW(t('sched.missLabel'), t('sched.missDesc'), missSeg),
      el('div', { class: 'pj-bar' }, saveBtn, cancelBtn));
    syncKind();
    return card;
  }

  function render() {
    const head = el('div', { class: 'mm-head' },
      el('div', { class: 'mm-head-text' },
        el('div', { class: 'mm-head-title' }, t('sched.title')),
        el('div', { class: 'mm-head-sub' }, t('sched.subtitle'))));
    const listCard = items.length
      ? el('div', { class: 'pj-list' }, ...items.map(taskCard))
      : el('div', { class: 'st-card pj-add' }, el('div', { class: 'st-row-desc' }, t('sched.empty')));
    page.replaceChildren(head, formCard(), listCard);
  }

  render();
  refresh();
  return [page];
}

/** 项目管理弹出菜单（侧栏项目头 ··· 按钮；Codex 式悬停管理的入口） */
export function openProjectMenu(store, pj, anchor) {
  closeMenu();
  const menu = el('div', { class: 'pj-menu' });
  const item = (label, fn, cls) => el('div', { class: 'pj-menu-item' + (cls ? ' ' + cls : ''), onclick: () => { closeMenu(); fn(); } }, label);
  menu.append(
    item(t('projects.unreg'), async () => {
      confirmModal({
        title: t('projects.unregTitle'),
        message: t('projects.unregBody', { name: pj.name }),
        onOk: async () => {
          try { await store.host.deleteProject(pj.slug, false); await window.__app.refreshProjects?.(); await window.__app.refreshAllSessions?.(); } catch (e) { toast(e.message); }
        },
      });
    }, 'danger'),
    item(t('projects.purge'), async () => {
      confirmModal({
        title: t('projects.purgeTitle'),
        message: t('projects.purgeBody', { name: pj.name }),
        onOk: async () => {
          try { await store.host.deleteProject(pj.slug, true); await window.__app.refreshProjects?.(); await window.__app.refreshAllSessions?.(); } catch (e) { toast(e.message); }
        },
      });
    }, 'danger'),
    item(t('projects.openSettings'), () => openSettingsSection(store, '项目管理')),
  );
  const r = anchor.getBoundingClientRect();
  menu.style.position = 'fixed';
  menu.style.top = (r.bottom + 4) + 'px';
  menu.style.left = Math.min(r.left, window.innerWidth - 220) + 'px';
  menu.style.zIndex = '300';
  document.body.append(menu);
  setTimeout(() => document.addEventListener('click', closeMenu, { once: true }), 0);
}
function closeMenu() { document.querySelectorAll('.pj-menu').forEach((m) => m.remove()); }

function mdoProjectsSection(store) {
  const host = store.host;
  if (host?.name !== 'mdo') {
    return [el('div', { class: 'st-card' }, el('div', { class: 'st-row-desc' }, t('projects.offline')))];
  }
  let adding = false;
  const page = el('div', { class: 'mm-page' });
  let lastData = null;

  async function refresh() {
    try { lastData = await host.listProjects(); } catch { lastData = lastData ?? { projects: [] }; }
    render();
  }

  function projectCard(p, isActive) {
    const avatar = el('span', { class: 'pj-avatar' },
      icon(isActive ? 'IconFolderOpen16' : 'IconFolderClose16', { size: 16 }));
    avatar.style.background = 'hsl(' + mmHue(p.path || p.slug) + ' 42% 40%)';
    const badges = [];
    if (isActive) badges.push(el('span', { class: 'mm-badge' }, t('sidebar.current')));
    if (p.valid === false) badges.push(el('span', { class: 'mm-badge warn', 'data-tip': t('projects.pathInvalid') }, t('projects.pathInvalidBadge')));
    const nameRow = el('div', { class: 'pj-name' }, p.name, ...badges);
    const info = el('div', { class: 'pj-info' }, nameRow, el('div', { class: 'pj-path' }, p.path));
    const enterBtn = isActive ? null : el('button', {
      class: 'dsw-btn',
      onclick: async () => {
        try { await window.__app.switchProject(p.slug); refresh(); }
        catch (e) { toast(t('projects.switchFail') + e.message); }
      },
    }, t('projects.enter'));
    const main = el('div', { class: 'pj-main' }, avatar, info, enterBtn);

    const modelSel = el('select', { class: 'modal-input pj-model' },
      el('option', { value: '' }, t('projects.followGlobal')),
      ...store.models.map((m) => el('option', {
        value: m.id, selected: p.defaultModel === m.id ? '' : undefined,
      }, m.name)));
    modelSel.addEventListener('change', async () => {
      try { await host.setProjectDefaultModel(p.slug, modelSel.value); toast('projects.defModelSaved'); }
      catch (e) { toast(t('models.saveFail') + e.message); }
    });
    const unregBtn = el('button', {
      class: 'icon-btn pj-icon', 'data-tip': t('projects.unregTip'),
      onclick: () => confirmModal({
        title: t('projects.unregTitle'),
        message: t('projects.unregBody', { name: p.name }),
        okLabel: t('projects.unreg'),
        onOk: async () => {
          try { await host.deleteProject(p.slug, false); await window.__app.refreshProjects(); refresh(); }
          catch (e) { toast(t('projects.unregFail') + e.message); }
        },
      }),
    }, icon('IconCloseOutline16', { size: 14 }));
    const purgeBtn = el('button', {
      class: 'icon-btn pj-icon danger', 'data-tip': t('projects.purgeTip'),
      onclick: () => confirmModal({
        title: t('projects.purgeTitle'),
        message: t('projects.purgeBody', { name: p.name }),
        okLabel: t('projects.purge'),
        onOk: async () => {
          try { await host.deleteProject(p.slug, true); await window.__app.refreshProjects(); refresh(); }
          catch (e) { toast(t('models.delFail') + e.message); }
        },
      }),
    }, icon('IconTrashOutline16', { size: 14 }));
    const foot = el('div', { class: 'pj-foot' },
      el('span', { class: 'pj-foot-label' }, t('projects.defModel')),
      modelSel,
      el('span', { class: 'pj-sp' }),
      unregBtn,
      purgeBtn);
    return el('div', { class: 'pj-card' + (isActive ? ' active' : '') }, main, foot);
  }

  function render() {
    const d = lastData ?? {};
    const list = d.projects ?? [];
    const active = d.active ?? '';
    const pathInp = el('input', { class: 'modal-input', placeholder: 'D:\\path\\to\\workspace' });
    const addBtn = el('button', {
      class: 'dsw-btn primary',
      onclick: async () => {
        const p = pathInp.value.trim();
        if (!p) { toast('projects.pathPh'); return; }
        try {
          await host.addProject(p);
          await window.__app.refreshProjects();
          const last = (store._mdoProjects ?? []).slice(-1)[0];
          if (last) await window.__app.switchProject(last.slug);
          adding = false; refresh();
        } catch (e) { toast(t('projects.addFail') + e.message); }
      },
    }, t('act.add'));
    const cancelBtn = el('button', { class: 'dsw-btn', onclick: () => { adding = false; render(); } }, t('act.cancel'));
    const addCard = adding
      ? el('div', { class: 'pj-add' }, el('div', { class: 'pj-add-row' }, pathInp, addBtn, cancelBtn))
      : null;
    const head = el('div', { class: 'mm-head' },
      el('div', { class: 'mm-desc' },
        t('projects.desc')),
      el('div', { class: 'mm-actions' },
        el('button', {
          class: 'dsw-btn primary', onclick: () => { adding = true; render(); },
        }, icon('IconPlusOutline16', { size: 13 }), t('projects.btnNew'))));
    const cards = list.map((p) => projectCard(p, p.slug === active));
    if (list.length === 0) {
      cards.push(el('div', { class: 'pj-empty' }, t('projects.empty')));
    }
    page.replaceChildren(head, addCard, el('div', { class: 'pj-list' }, ...cards));
  }

  refresh();
  return [page];
}
const ST_SECTIONS = [
  { group: '基础设置', items: [
    { id: '外观', icon: 'IconDarkOutline16' },
    { id: '常规', icon: 'IconSettingsOutline16' },
    { id: '网络', icon: 'IconSettingsOutline16' },
    { id: '计划任务', icon: 'IconChecklistOutline16' },
    { id: '模型设置', icon: 'IconSparkle16' },
  ] },
  { group: '数据', items: [
    { id: '模型管理', icon: 'IconSparkle16' },
    { id: '项目管理', icon: 'IconDataOutline16' },
    { id: '数据管理', icon: 'IconDataOutline16' },
    { id: '反馈', icon: 'IconLikeOutline16' },
  ] },
];

function mountSettingsPage(root, store) {
  function render() {
    root.hidden = !(settingsState.active || schedViewState.active);
    if (root.hidden) return;
    if (schedViewState.active) {
      // Codex 式：定时任务 = 独立页面（右侧主区，复用管理节）
      root.replaceChildren(el('div', { class: 'st-wrap wide sched-page' },
        el('div', { class: 'sched-page-head' },
          el('h1', { class: 'st-title' }, t('sched.title')),
          el('button', { class: 'dsw-btn', onclick: () => { schedViewState.active = false; store.notifier.markDirty(); } },
            icon('IconChevronLeftOutline14', { size: 14 }), el('span', null, t('sidebar.backToWork')))),
        el('div', { class: 'st-row-desc sched-page-sub' }, t('sched.subtitle')),
        ...mdoSchedulesSection(store)));
      return;
    }
    root.replaceChildren(el('div', { class: 'st-wrap' + (['模型管理', '项目管理', '计划任务'].includes(settingsState.section) ? ' wide' : '') },
      el('h1', { class: 'st-title' }, t(NAV_LABEL[settingsState.section] ?? settingsState.section, null, settingsState.section)),
      ...stSection(store, settingsState.section)));
  }
  store.notifier.subscribe(render);
  render();
}
// ============ 帮助弹窗 ============

export function openHelp() {
  const kbd = (k) => el('kbd', null, k);
  const row = (k, d) => el('tr', null, el('td', null, k), el('td', null, d));
  openModal({
    title: '快捷键与命令',
    body: el('div', { class: 'help-body' },
      el('h4', null, '快捷键'),
      el('table', { class: 'help-table' }, el('tbody', null,
        row(el('span', null, kbd('Enter'), ' 发送'), 'Shift+Enter 换行'),
        row(kbd('Esc'), '中断运行 / 关闭弹层'),
        row(el('span', null, kbd('Ctrl'), '+', kbd('K')), t('sidebar.newSession')),
        row(el('span', null, kbd('Ctrl'), '+', kbd('F')), '会话内搜索'),
        row(el('span', null, kbd('Ctrl'), '+', kbd('E')), '导出 Markdown'),
        row(el('span', null, kbd('Ctrl'), '+', kbd('J')), t('sidebar.toggleTheme')),
        row(kbd('?'), '本帮助（输入框外）'))),
      el('h4', null, '斜杠命令'),
      el('table', { class: 'help-table' }, el('tbody', null, SLASH_COMMANDS.map((c) => row(el('code', null, c.cmd), c.desc)))),
      el('h4', null, '输入增强'),
      el('div', { style: 'color:var(--dsw-alias-label-tertiary);font-size:12.5px' },
        '输入 / 唤起命令菜单 · 输入 @ 补全文件路径 · 拖拽 / 粘贴 / 📎 添加图片附件；运行中发送自动排队。')),
    actions: [{ label: '知道了', primary: true }],
  });
}

// ============ 外壳挂载 ============
export function mountChrome(store, host, { sidebar, topbar, details, hooks }) {
  const frame = mountFrame(store);
  window.__app = window.__app || {};
  window.__app.frame = frame;

  const h = {
    newSession: hooks.newSession,
    deleteSession: (s) => confirmModal({
      title: t('sidebar.delSession'),
      message: t('session.delBody', { title: s.title }),
      onOk: () => {
        window.__app.deleteSessionRemote?.(s.id).finally(() => {
          store.removeSession(s.id);
          store.persist();
          toast(t('session.deleted'));
        });
      },
    }),
    renameSession: () => {
      const s = store.snapshot().selected;
      if (!s) return;
      promptModal({ title: t('tab.rename'), label: t('session.newName'), value: s.title, onOk: (v) => {
        store.renameSession(s.id, v);
        window.__app.renameSessionRemote?.(s.id, v);
      } });
    },
    togglePin: () => {
      const s = store.snapshot().selected;
      if (!s) return;
      store.togglePin(s.id);
      window.__app.pinSessionRemote?.(s.id, s.pinned);
    },
    exportSession: () => exportSessionMarkdown(store, store.snapshot().selected),
    openSettings: () => openSettings(store),
    openHelp: () => openHelp(),
    toggleTheme: () => {
      store.settings.theme = store.settings.theme === 'dark' ? 'light' : 'dark';
      applySettings(store); store.persist();
    },
  };

  const sb = mountSidebar(sidebar, store, frame, h);
  const hd = mountHeader(topbar, store, host, frame, h);
  const dt = mountDetails(details, store);
  mountSettingsPage(document.getElementById('settings-root'), store);

  // 标签页徽标 + 提示音
  let lastDone = 0;
  function notify() {
    const doneCount = [...store.sessions.values()].filter((s) => s.done && s.id !== store.selectedId).length;
    document.title = (doneCount > 0 ? `(${doneCount}) ` : '') + t('app.title');
    if (doneCount > lastDone && store.settings.sound) beep();
    lastDone = doneCount;
  }
  store.notifier.subscribe(notify);

  function render() { sb.render(); hd.render(); dt.render(); }
  store.notifier.subscribe(render);
  render();
  return { frame };
}

/* 叮咚提示音：B5→G5 双音铃（正弦+指数衰减，WebAudio 合成，零素材） */
function beep() {
  try {
    const ctx = new AudioContext();
    const note = (freq, t0, dur, vol) => {
      const o = ctx.createOscillator(), g = ctx.createGain();
      o.type = 'sine'; o.frequency.value = freq;
      const at = ctx.currentTime + t0;
      g.gain.setValueAtTime(0.0001, at);
      g.gain.exponentialRampToValueAtTime(vol, at + 0.015);
      g.gain.exponentialRampToValueAtTime(0.0001, at + dur);
      o.connect(g); g.connect(ctx.destination);
      o.start(at); o.stop(at + dur + 0.05);
    };
    note(988, 0, 0.30, 0.11);     /* 叮 B5 */
    note(784, 0.18, 0.45, 0.09);  /* 咚 G5 */
    setTimeout(() => ctx.close().catch(() => {}), 1400);
  } catch { /* 无声环境静默 */ }
}
