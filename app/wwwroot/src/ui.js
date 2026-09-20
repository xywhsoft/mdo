// ui.js — 对话视图层（结构对齐 XHarness dsh 原版）
//   用户气泡右对齐（MessageItem L-d22q）· 助手全宽 16/28（_5vpFrG）
//   Think 折叠行（ReasoningRow U8JO7q）· 工具内联行（Dh215a/ANgngG）
//   Composer 圆角卡 + 停靠面板（审批 rQ88rq / 待办 Xe4JHW / 排队 Q8gw3G / 统计 PvwIvW）
// 渲染纪律不变：节点级 v 版本 diff、details 折叠态迁移、事件委托。

import { renderMarkdown } from './markdown.js';
import { el } from './dom.js';
import { t } from './i18n.js?v=1';
import { icon, appIcon, stateDot } from './icons.js?v=5';
import {
  isSettingsView, closeSettings,
  toast, confirmModal, closeModal, promptModal, openSettings, openSettingsSection, openHelp,
  applySettings, exportSessionMarkdown, SLASH_COMMANDS, MOCK_FILES,
} from './chrome.js?v=42';

const fmtClock = (t) => new Date(t).toTimeString().slice(0, 5);
const fmtK = (n) => n >= 1000 ? (n / 1000).toFixed(1).replace(/\.0$/, '') + 'K' : String(n);
const fmtSec = (ms) => ms < 60000 ? `${Math.round(ms / 100) / 10}s` : `${Math.floor(ms / 60000)}m${Math.round(ms / 1000) % 60}s`;

// ============ 消息图标行（AEC1ra MessageIconActions）============
function msgActions(n, { clock = 'end', extra = [] } = {}) {
  const parts = [];
  const time = el('span', { class: 'msg-time ' + clock }, fmtClock(n.time));
  parts.push(clock === 'start' ? time : null);
  parts.push(el('button', {
    class: 'msg-action', type: 'button', 'data-tip': t('act.copy'),
    dataset: { act: 'copy-msg' }, 'aria-label': t('act.copy'),
  }, icon('IconCopyOutline16', { size: 16 })));
  for (const a of extra) parts.push(a);
  parts.push(clock === 'end' ? time : null);
  return el('div', { class: 'msg-actions', 'data-time-hover': '' }, ...parts);
}

// ============ 用户：右对齐气泡（L-d22q）============
function renderUserNode(n) {
  return el('div', { class: 'user-row flow-item', dataset: { id: n.id, v: n.v } },
    el('div', { class: 'user-stack' },
      n.images?.length ? el('div', { class: 'user-imgs' },
        n.images.map((img) => el('div', { class: 'user-img' },
          el('img', { src: img.dataUrl, alt: img.name ?? '附件' })))) : null,
      n.text ? el('div', { class: 'user-bubble' }, n.text) : null),
    msgActions(n, {
      clock: 'start',
      extra: [
        el('button', {
          class: 'msg-action', type: 'button', 'data-tip': t('act.editResend'),
          dataset: { act: 'edit-user' }, 'aria-label': t('act.editResend'),
        }, icon('IconEditOutline16', { size: 16 })),
      ],
    }));
}

// ============ 助手：全宽正文（_5vpFrG）============
function renderAssistantNode(n, keep, stats) {
  const parts = [];
  for (const b of n.blocks ?? []) {
    if (b.type === 'reasoning') parts.push(renderThink(b, n, keep));
    else if (b.type === 'image') parts.push(el('div', { class: 'msg-imgs' }, el('img', { src: b.dataUrl, alt: b.name ?? '图片' })));
    else parts.push(el('div', { class: 'md', html: b.html || renderMarkdown(b.text) }));
  }
  return el('div', { class: 'asst-row flow-item', dataset: { id: n.id, v: n.v } },
    el('div', { class: 'asst-body' }, ...parts),
    n.interrupted ? el('span', { class: 'asst-stopped' }, t('msg.stopped')) : null,
    n.streaming ? null : msgActions(n, {
      extra: [
        el('button', { class: 'msg-action', type: 'button', 'data-tip': t('act.retry'), dataset: { act: 'retry' }, 'aria-label': t('act.retry') }, icon('IconRefreshOutline16', { size: 16 })),
        el('button', { class: 'msg-action', type: 'button', 'data-tip': t('act.fork'), dataset: { act: 'fork-sess' }, 'aria-label': '分叉会话' }, icon('IconBranchOutline16', { size: 16 })),
        el('button', { class: 'msg-action' + (n.feedback === 'good' ? ' on' : ''), type: 'button', 'data-tip': t('act.like'), dataset: { act: 'fb-good' }, 'aria-label': t('act.like') }, icon('IconLikeOutline16', { size: 16 })),
        el('button', { class: 'msg-action' + (n.feedback === 'bad' ? ' on' : ''), type: 'button', 'data-tip': t('act.dislike'), dataset: { act: 'fb-bad' }, 'aria-label': t('act.dislike') }, icon('IconDislikeOutline16', { size: 16 })),
        stats ? renderStatsSpan(stats) : null,
      ],
    }));
}

/** 会话级统计（原独立统计行），悬停回复时显示在操作行尾。 */
function renderStatsSpan(st) {
  const groups = [];
  if (st.steps > 0) {
    groups.push(t('msg.rounds', { turns: st.turns, steps: st.steps }));
    const dur = [];
    if (st.llmMs > 0) dur.push(`LLM ${fmtSec(st.llmMs)}`);
    if (st.toolMs > 0) dur.push(t('msg.toolTime', { sec: fmtSec(st.toolMs) }));
    if (dur.length) groups.push(dur.join(' · '));
    if (st.tps) groups.push(`${Number(st.tps).toFixed(1)} tok/s`);
  }
  if (!groups.length) return null;
  return el('span', { class: 'msg-stats' },
    groups.flatMap((g, i) => i ? [el('span', { class: 'stats-sep' }, '|'), g] : [g]));
}

// ============ Think 折叠行（U8JO7q ReasoningRow）============
function renderThink(b, n, keep) {
  const text = b.text ?? '';
  const running = !!n.streaming;
  const latestLine = (t) => { const v = t.trimEnd(); const i = v.lastIndexOf('\n'); return i === -1 ? v : v.slice(i + 1); };
  const open = running || !!keep?.thinkOpen;
  const dur = running ? null : (n.thinkMs != null ? fmtSec(n.thinkMs) : null);
  return el('div', { class: 'think-root', 'data-state': running ? 'running' : 'ok', 'data-open': open ? '' : null },
    el('div', { class: 'think-row', dataset: { act: 'toggle-think' } },
      el('span', { class: 'think-leading' }, running
        ? icon('IconLoadingOutline16', { size: 14 })
        : icon('IconThinkOutline14', { size: 14 })),
      el('span', { class: 'think-title' }, t('msg.think')),
      dur ? el('span', { class: 'think-dur' }, dur) : null,
      el('span', { class: 'think-summary' }, running ? latestLine(text) : ''),
      el('span', { class: 'think-chevron' }, icon('IconTriangleRightFill14', { size: 12 }))),
    open ? el('div', { class: 'think-body' },
      el('div', { class: 'node-body-card' },
        el('pre', { class: 'node-pre' }, text))) : null);
}

// ============ 工具内联行（Dh215a/ANgngG）============
const TOOL_LABEL = {
  bash: 'bash', read: 'read', write: 'write', edit: 'edit',
  search: 'search', glob: 'glob', fetch: 'fetch', task: 'task',
  exec: 'exec', spawn: 'spawn', poll: 'poll', wait: 'wait',
  stdin: 'stdin', stop: 'stop', agent: 'agent',
};
function toolSummaryText(n) {
  let a = n.args ?? {};
  if (typeof a === 'string') { try { a = JSON.parse(a || '{}'); } catch { a = {}; } }
  const arr = (v) => Array.isArray(v) ? v.join(' ') : (v ?? '');
  switch (n.name) {
    case 'bash': return a.command ?? '';
    case 'exec': case 'spawn': return arr(a.argv);
    case 'read': case 'write': case 'edit': return a.path ?? '';
    case 'poll': case 'stdin': case 'stop': return a.task_id ?? a.id ?? '';
    case 'wait': return Array.isArray(a.task_ids) ? a.task_ids.join(',') : (a.task_ids ?? '');
    case 'search': return `/${a.pattern ?? ''}/ in ${a.path ?? '.'}`;
    case 'glob': return a.pattern ?? '**/*';
    case 'fetch': return a.url ?? '';
    case 'task': case 'agent': return a.goal ?? a.type ?? '';
    default: {
      const j = JSON.stringify(a);
      return j.length > 80 ? j.slice(0, 80) + '…' : j;
    }
  }
}

function toolLeadIcon(n) {
  if (n.status === 'running') return icon('IconLoadingOutline16', { size: 16 });
  if (n.status === 'error') return icon('IconWarningOutline16', { size: 16 });
  return icon('IconCheckOutline14', { size: 14 });
}
function ioCard(sections) {
  return el('div', { class: 'io-card' },
    sections.map((s, i) => [
      i > 0 ? el('div', { class: 'io-divider' }) : null,
      el('div', { class: 'io-section' },
        el('span', { class: 'io-label' }, s.label),
        el('span', { class: 'io-text', 'data-error': s.error ? '' : null }, s.text)),
    ]).flat());
}
function copyBtn(getText) {
  return el('button', {
    class: 'node-copy', type: 'button', 'data-tip': t('act.copy'),
    onclick: (e) => {
      e.stopPropagation();
      navigator.clipboard?.writeText(getText()).then(() => toast(t('act.copied')));
    },
  }, icon('IconCopyOutline16', { size: 13 }));
}
function sectionCard(label, text, { error = false } = {}) {
  return el('div', { class: 'node-sec mono' },
    el('div', { class: 'node-sec-head' },
      el('span', { class: 'node-sec-label' }, label),
      copyBtn(() => text)),
    el('pre', { class: 'node-pre', 'data-error': error ? '' : null }, text));
}
function prettyArgs(n) {
  try {
    const a = typeof n.args === 'string' ? JSON.parse(n.args || '{}') : (n.args ?? {});
    return JSON.stringify(a, null, 2);
  } catch { return String(n.args ?? ''); }
}
function renderToolBody(n) {
  const out = [];
  if (n.name === 'task' && n.sub?.length) {
    out.push(el('div', { class: 'sub-calls' },
      n.sub.map((st) => el('div', {
        class: 'tool-root sub-call-row',
        'data-state': st.status === 'running' ? 'running' : 'ok',
      },
        el('div', { class: 'tool-row' },
          el('span', { class: 'tool-leading' },
            st.status === 'running' ? icon('IconLoadingOutline16', { size: 14 }) : icon('IconCheckOutline14', { size: 12 })),
          el('span', { class: 'tool-title' }, 'step'),
          el('span', { class: 'tool-sep' }),
          el('span', { class: 'tool-summary' }, st.name))))));
  }
  if (n.diff) {
    out.push(el('div', { class: 'diff-body' },
      n.diff.map((l) => el('span', { class: 'diff-line ' + ({ '+': 'add', '-': 'del' }[l[0]] ?? 'ctx') }, l))));
  } else {
    const argsText = prettyArgs(n);
    if (argsText !== '{}') {
      out.push(sectionCard(t('msg.params'), argsText));
    }
    if (n.result != null && n.result !== '') {
      out.push(sectionCard(n.status === 'error' ? t('msg.outputErr') : t('msg.output'),
        String(n.result), { error: n.status === 'error' }));
    } else if (n.status === 'running') {
      out.push(el('div', { class: 'node-sec mono' },
        el('pre', { class: 'node-pre' }, '···')));
    }
  }
  return out;
}

function renderToolNode(n, keep) {
  const open = n.status === 'running' || keep?.toolOpen;
  const summary = n.status === 'running' ? toolSummaryText(n) : toolSummaryText(n);
  const suffix = n.status !== 'running' && n.ms != null ? ` · ${fmtSec(n.ms)}` : '';
  return el('div', {
    class: 'tool-root flow-item', 'data-state': n.status === 'running' ? 'running' : 'ok',
    'data-open': open ? '' : null,
    'data-variant': n.name === 'task' ? 'cordis' : null,
    dataset: { id: n.id, v: n.v },
  },
    el('div', { class: 'tool-row', dataset: { act: 'toggle-tool' } },
      el('span', { class: 'tool-leading' }, toolLeadIcon(n)),
      el('span', { class: 'tool-title' }, TOOL_LABEL[n.name] ?? n.name),
      el('span', { class: 'tool-sep', 'aria-hidden': 'true' }),
      el('span', { class: 'tool-summary' + (n.status === 'error' ? ' error' : '') },
        summary + suffix),
      el('span', { class: 'tool-chevron' }, icon('IconTriangleRightFill14', { size: 12 }))),
    open ? el('div', { class: 'tool-body-wrap' }, ...renderToolBody(n)) : null);
}

function renderNode(n, keep, stats) {
  if (n.kind === 'user') return renderUserNode(n);
  if (n.kind === 'tool') return renderToolNode(n, keep);
  return renderAssistantNode(n, keep, stats);
}
function nodeText(n) {
  if (n.kind === 'user') return n.text ?? '';
  if (n.kind === 'assistant') return (n.blocks ?? []).map((b) => b.type === 'text' ? b.text : t('msg.image')).join('\n');
  return `${n.name} ${n.result ?? ''}`;
}
function captureKeep(row) {
  return {
    /* matches 兜底：工具卡自身就是 flow-item 行，querySelector 找不到自身 */
    thinkOpen: !!(row.matches?.('.think-root[data-open]') || row.querySelector('.think-root[data-open]')),
    toolOpen: !!(row.matches?.('.tool-root[data-open]') || row.querySelector('.tool-root[data-open]')),
  };
}

// ============ 停靠面板 ============
function renderDocks(root, store, host, s) {
  const parts = [];
  // 审批（rQ88rq）
  for (const a of s.approvals.values()) {
    parts.push(el('div', { class: 'appr-root' },
      el('div', { class: 'appr-card' },
        el('div', { class: 'appr-strip' },
          el('span', { class: 'appr-dot' }),
          a.title),
        el('div', { class: 'appr-body' },
          el('div', { class: 'appr-headline' }, t('msg.approvalNeeded')),
          el('div', { class: 'appr-command' }, a.detail ?? '')),
        el('div', { class: 'appr-actions' },
          el('button', { class: 'dsw-btn danger', onclick: () => host.respondApproval(a.id, 'rejected') }, t('msg.deny')),
          el('button', { class: 'dsw-btn', onclick: () => host.respondApproval(a.id, 'allow-session') }, t('msg.approveAll')),
          el('button', { class: 'dsw-btn primary', onclick: () => host.respondApproval(a.id, 'allow-once') }, t('msg.approveOnce'))))));
  }
  // ask_user 卡片（人一件：模型结构化向用户提问）
  for (const a of s.asks.values()) {
    const opts = Array.isArray(a.options) ? a.options : [];
    const answerBtns = opts.map((o) => el('button', {
      class: 'dsw-btn', onclick: () => { host.respondAsk ? host.respondAsk(a.id, o) : null; },
    }, o));
    const inp = el('input', { class: 'modal-input', placeholder: t('ask.freePh') });
    const sendBtn = el('button', {
      class: 'dsw-btn primary',
      onclick: () => { const v = inp.value.trim(); if (v) host.respondAsk && host.respondAsk(a.id, v); },
    }, t('act.ok'));
    parts.push(el('div', { class: 'appr-root' },
      el('div', { class: 'appr-card' },
        el('div', { class: 'appr-strip' },
          el('span', { class: 'appr-dot' }),
          t('ask.title')),
        el('div', { class: 'appr-body' },
          el('div', { class: 'appr-headline' }, a.question),
          el('div', { class: 'ask-options' }, ...answerBtns),
          el('div', { class: 'ask-free' }, inp, sendBtn)))));
  }
  // 待办（Xe4JHW）
  const tp = s.todoPanel;
  if (tp && tp.items.length) {
    const done = tp.items.filter((x) => x.done).length;
    parts.push(el('div', { class: 'todo-root', 'data-open': tp.open ? '' : null },
      el('div', { class: 'todo-body' },
        el('button', { class: 'todo-header', dataset: { act: 'toggle-todo' } },
          el('span', { class: 'todo-lead' },
            done === tp.items.length ? icon('IconCheckOutline14', { size: 14 }) : icon('IconLoadingOutline16', { size: 14 })),
          el('span', { class: 'todo-title' }, '计划'),
          el('span', { class: 'todo-progress' }, `${done}/${tp.items.length}`),
          el('span', { class: 'todo-chevron' }, icon('IconTriangleRightFill14', { size: 12 }))),
        tp.open ? el('ul', { class: 'todo-list' },
          tp.items.map((it, i) => el('li', { class: 'todo-item' + (it.done ? ' done' : '') },
            el('span', { class: 'todo-glyph ' + (it.done ? 'done' : i === done ? 'running' : 'pending') },
              it.done ? icon('IconCheckOutline14', { size: 14 }) : icon('IconQueueOutline14', { size: 14 })),
            el('span', { class: 'todo-content' }, it.text)))) : null)));
  }
  // 排队（codex/zcode 式：用户气泡样式的堆叠卡片，带序号徽章）
  if (s.queue.length) {
    const open = s._queueOpen !== false;
    parts.push(el('div', { class: 'queue-dock', 'data-open': open ? '' : null },
      el('button', { class: 'queue-toggle', dataset: { act: 'toggle-queue' } },
        icon('IconQueueOutline14', { size: 13 }),
        el('span', null, t('msg.queued', { n: s.queue.length })),
        el('span', { class: 'queue-chevron' }, icon(open ? 'IconChevronUpOutline14' : 'IconChevronDownOutline14', { size: 12 }))),
      open ? el('div', { class: 'queue-stack' },
        s.queue.map((q, i) => el('div', { class: 'queue-card' },
          el('span', { class: 'queue-idx' }, String(i + 1)),
          el('div', { class: 'queue-card-body' },
            q.parts.map((p) => p.type === 'image'
              ? el('img', { class: 'queue-card-img', src: p.dataUrl, alt: p.name ?? 'img' })
              : el('span', { class: 'queue-card-text' }, p.text))),
          el('button', {
            class: 'msg-action queue-x', 'data-tip': t('msg.remove'),
            dataset: { act: 'drop-queue', idx: i },
          }, icon('IconCloseFill14', { size: 14 }))))) : null));
  }
  root.replaceChildren(...parts);
}


// ============ Composer（_7yzX1q）============
let composerInput = null;
export function focusComposer() { composerInput?.focus(); }
const estTokens = (t) => {
  const cjk = (t.match(/[\u4e00-\u9fff\u3000-\u303f]/g) ?? []).length;
  return Math.round(cjk + (t.length - cjk) / 4);
};

/** 自绘下拉（替代原生 select：弹层配色可控，向上弹出避开视口底）。 */
function dropdown({ aria }) {
  const btn = el('button', { class: 'cmp-select', type: 'button', 'aria-label': aria });
  const menu = el('div', { class: 'dropdown cmp-dd' });
  const root = el('div', { class: 'cmp-dd-root' }, btn, menu);
  const state = { options: [], value: null, open: false };
  let onChange = null;
  const setOpen = (v) => { state.open = v; menu.classList.toggle('open', v); if (v) renderMenu(); };
  const renderMenu = () => menu.replaceChildren(...state.options.map((o) => el('button', {
    class: state.value === o.v ? 'on' : '',
    onclick: () => { setOpen(false); if (state.value !== o.v) { state.value = o.v; renderBtn(); onChange && onChange(o.v); } },
  }, el('span', null, o.label), o.sub ? el('span', { class: 'sub' }, o.sub) : null)));
  const renderBtn = () => {
    const cur = state.options.find((o) => o.v === state.value);
    btn.replaceChildren(cur ? cur.label : '', icon('IconChevronDownOutline14', { size: 12, className: 'model-pill-chevron' }));
  };
  btn.addEventListener('click', (e) => { e.stopPropagation(); setOpen(!state.open); });
  menu.addEventListener('click', (e) => e.stopPropagation());
  document.addEventListener('click', () => setOpen(false));
  return {
    root,
    set(options, value) { state.options = options; state.value = value; renderBtn(); if (state.open) renderMenu(); },
    onChange(fn) { onChange = fn; },
  };
}

/** 上下文容量圆环（模型选择器旁）：悬停显示分段条与分类占比。 */
function contextMeter(store) {
  const svgNS = 'http://www.w3.org/2000/svg';
  const root = el('div', { class: 'meter ctx-meter' });
  const btn = el('button', { class: 'meter-trigger', type: 'button', 'aria-label': t('msg.ctxCapacity') });
  const panel = el('div', { class: 'meter-panel' });
  root.append(btn, panel);
  const R = 9, CIRC = 2 * Math.PI * R;

  function ring(pct) {
    const svg = document.createElementNS(svgNS, 'svg');
    svg.setAttribute('viewBox', '0 0 22 22');
    svg.setAttribute('width', '22'); svg.setAttribute('height', '22');
    const mk = (cls) => {
      const c = document.createElementNS(svgNS, 'circle');
      c.setAttribute('cx', '11'); c.setAttribute('cy', '11'); c.setAttribute('r', String(R));
      c.setAttribute('class', cls);
      return c;
    };
    const fill = mk('meter-fill');
    fill.setAttribute('stroke-dasharray', `${(CIRC * pct).toFixed(1)} ${CIRC.toFixed(1)}`);
    fill.setAttribute('transform', 'rotate(-90 11 11)');
    svg.append(mk('meter-track'), fill);
    return svg;
  }

  function update() {
    const s = store.snapshot().selected;
    const win = store.model?.contextWindow ?? 128000;
    const used = Math.min(win, s?.ctxTokens ?? 0);
    btn.replaceChildren(ring(used / win));
    // 分类估算：消息（对话文本）/ 工具结果 / 系统提示词 / 其他
    // CJK 感知粗估：中日韩字符 ≈1 token，其余 ≈4 字符/token
    const tok = (s2) => {
      if (!s2) return 0;
      const cjk = (s2.match(/[一-鿿　-〿]/g) ?? []).length;
      return Math.round(cjk + (s2.length - cjk) / 4);
    };
    let msg = 0, tool = 0;
    for (const n of s?.nodes ?? []) {
      if (n.kind === 'user') msg += tok(n.text) + 600 * (n.images?.length || 0);
      else if (n.kind === 'assistant') for (const b of n.blocks ?? []) if (b.type === 'text') msg += tok(b.text);
      else if (n.kind === 'tool') tool += tok(n.result);
    }
    const sys = 3200 + (store.settings.systemPrompt || '').length;
    // 三类实算内容按比例缩放填满已用量；差额并入「其他」
    const raw = Math.max(1, msg + tool + sys);
    const scale = used / raw;
    const parts = [
      ['消息', Math.round(msg * scale), 'var(--dsw-static-blue-450)'],
      [t('msg.toolResult'), Math.round(tool * scale), '#a78bfa'],
      [t('settings.sysPrompt'), Math.round(sys * scale), 'var(--dsw-static-green-500)'],
      [t('msg.other'), Math.max(0, used - Math.round(msg * scale) - Math.round(tool * scale) - Math.round(sys * scale)), 'var(--dsw-alias-label-caption)'],
    ];
    const sum = Math.max(1, parts.reduce((a, p) => a + p[1], 0));
    panel.replaceChildren(
      el('div', { class: 'meter-header' },
        el('span', { class: 'meter-headline' }, t('msg.ctxCapacity')),
        el('span', { class: 'meter-figures' }, `${fmtK(used)} / ${fmtK(win)}（${Math.round(used / win * 100)}%）`)),
      el('div', { class: 'meter-bar' },
        ...parts.map(([label, tok, color]) => el('span', {
          class: 'meter-segment',
          style: `background:${color};flex:none;width:${Math.max(1.5, tok / sum * 100).toFixed(1)}%`,
        }))),
      el('div', { class: 'ctx-rows' },
        ...parts.map(([label, tok, color]) => el('div', { class: 'ctx-row' },
          el('span', { class: 'ctx-dot', style: `background:${color}` }),
          el('span', null, label),
          el('span', { class: 'ctx-pct' }, Math.round(tok / sum * 100) + '%')))));
  }
  store.notifier.subscribe(update);
  update();
  return root;
}

function mountComposer(root, store, host) {
  let attachments = [];
  const menu = { open: false, kind: null, items: [], idx: 0, start: 0 };

  const card = el('div', { class: 'cmp-card' });

  // 项目选择 pill（zcode 式：新建对话时可选项目；进入对话后隐藏）
  const projPill = el('button', { class: 'cmp-proj-pill', type: 'button' },
    icon('IconFolderClose16', { size: 14 }),
    el('span', { class: 'cmp-proj-name' }, ''),
    icon('IconChevronDownOutline14', { size: 12 }));
  const projMenu = el('div', { class: 'dropdown cmp-proj-menu' });
  const projRow = el('div', { class: 'cmp-proj-row' }, projPill);
  let projMenuOpen = false;
  function syncProjPill() {
    const projects = store._mdoProjects ?? [];
    const active = store._mdoActiveProject || '_tasks';
    const cur = projects.find((p) => p.slug === active);
    projPill.querySelector('.cmp-proj-name').textContent = cur ? cur.name : t('sidebar.tasks');
  }
  function closeProjMenu() {
    projMenuOpen = false;
    projMenu.remove();
  }
  projPill.addEventListener('click', () => {
    if (projMenuOpen) { closeProjMenu(); return; }
    const projects = store._mdoProjects ?? [];
    const active = store._mdoActiveProject || '_tasks';
    projMenu.replaceChildren(
      ...projects.map((p) => el('button', {
        class: p.slug === active ? 'on' : '',
        type: 'button',
        onclick: async () => {
          closeProjMenu();
          if (p.slug !== active) {
            await window.__app.switchProject(p.slug);
            store.notifier.markDirty();
          }
        },
      }, icon(p.tasks ? 'IconChecklistOutline14' : 'IconFolderClose16', { size: 14 }), p.name)),
      el('div', { class: 'proj-menu-sep' }),
      el('button', {
        type: 'button',
        onclick: () => { closeProjMenu(); hooks?.openSettings?.(); window.__app.openSettings?.(); },
      }, icon('IconPlusOutline16', { size: 13 }), t('crumb.manage')),
      el('button', {
        type: 'button',
        onclick: async () => {
          closeProjMenu();
          if (active !== '_tasks') {
            await window.__app.switchProject('_tasks');
            store.notifier.markDirty();
          }
        },
      }, icon('IconCloseOutline16', { size: 13 }), t('composer.workNoProject')));
    projRow.append(projMenu);
    projMenuOpen = true;
  });
  document.addEventListener('click', (e) => {
    if (projMenuOpen && !projRow.contains(e.target)) closeProjMenu();
  });
  const menuEl = el('div', { class: 'cmp-menu' });
  const attachRow = el('div', { class: 'cmp-attach' });
  const inputEl = composerInput = el('textarea', {
    class: 'cmp-textarea', rows: '1',
    placeholder: t('composer.ph'),
  });
  const tokenEst = el('span', { class: 'cmp-token' });
  const addBtn = el('button', { class: 'cmp-add', type: 'button', 'data-tip': t('composer.attach') }, icon('IconPaperclipOutline16', { size: 16 }));
  const sendBtn = el('button', { class: 'cmp-send', type: 'button', 'aria-label': t('composer.send') }, icon('IconSendOutline14', { size: 16 }));
  const fileInput = el('input', { type: 'file', accept: 'image/*', multiple: '', style: 'display:none' });
  const EFFORT_OPTS = [
    { v: 'off', label: t('composer.thinkOff') }, { v: 'low', label: t('composer.thinkLow') },
    { v: 'medium', label: t('composer.thinkMed') }, { v: 'high', label: t('composer.thinkHigh') }];
  const POLICY_OPTS = [{ v: 'ask', label: t('composer.policyAsk') }, { v: 'auto', label: t('composer.policyAuto') }];

  const modelDD = dropdown({ aria: t('composer.model') });
  modelDD.onChange((id) => {
    const m = store.models.find((x) => x.id === id);
    if (m) {
      store.model = m;
      const sel = store.snapshot().selected;   // 每会话模型（zcode 式）：选中即绑定当前会话
      if (sel) { sel.modelId = id; store.persist(); }
      store.notifier.markDirty();
    }
  });
  const effortDD = dropdown({ aria: t('models.reasoning') });
  effortDD.onChange((v) => { host.effort = v; toast(t('composer.think') + v); });
  const policyDD = dropdown({ aria: t('composer.policy') });
  policyDD.onChange((v) => {
    const s = store.snapshot().selected;
    if (s) { s.policy = v; store.notifier.markDirty(); }
  });

  function syncSelects() {
    const s = store.snapshot().selected;
    const curId = s?.modelId && store.models.some((m) => m.id === s.modelId) ? s.modelId : store.model?.id;
    modelDD.set(store.models.map((m) => ({ v: m.id, label: m.name })), curId);
    effortDD.set(EFFORT_OPTS, host.effort ?? 'high');
    policyDD.set(POLICY_OPTS, s ? s.policy : 'ask');
  }

  function addFiles(files) {
    for (const f of files ?? []) {
      if (!f.type.startsWith('image/')) { toast(`跳过非图片文件：${f.name}`); continue; }
      if (f.size > 2 * 1024 * 1024) { toast(`图片过大（>2MB）：${f.name}`); continue; }
      const reader = new FileReader();
      reader.onload = () => { attachments.push({ dataUrl: reader.result, name: f.name }); syncAttach(); };
      reader.readAsDataURL(f);
    }
  }
  function syncAttach() {
    attachRow.replaceChildren(...attachments.map((a, i) =>
      el('div', { class: 'attach-chip' },
        el('span', { class: 'attach-thumb' }, el('img', { src: a.dataUrl, alt: a.name })),
        el('span', { class: 'attach-name' }, a.name),
        el('button', { class: 'attach-x', onclick: () => { attachments.splice(i, 1); syncAttach(); } }, '✕'))));
    card.classList.toggle('has-attach', attachments.length > 0);
  }
  fileInput.addEventListener('change', () => { addFiles(fileInput.files); fileInput.value = ''; });
  inputEl.addEventListener('paste', (e) => {
    const imgs = [...(e.clipboardData?.items ?? [])].filter((it) => it.type.startsWith('image/'));
    if (imgs.length) { e.preventDefault(); addFiles(imgs.map((it) => it.getAsFile()).filter(Boolean)); }
  });
  card.addEventListener('dragover', (e) => { e.preventDefault(); card.classList.add('drag'); });
  card.addEventListener('dragleave', () => card.classList.remove('drag'));
  card.addEventListener('drop', (e) => { e.preventDefault(); card.classList.remove('drag'); addFiles(e.dataTransfer?.files); });

  function closeMenu() { menu.open = false; menuEl.classList.remove('open'); }
  function renderMenu() {
    if (!menu.open) return;
    menuEl.replaceChildren(...menu.items.map((it, i) => el('div', {
      class: 'menu-item' + (i === menu.idx ? ' sel' : ''),
      onmousedown: (e) => { e.preventDefault(); pickMenuItem(); },
    }, el('code', null, it.label), el('span', { class: 'menu-desc' }, it.desc ?? ''))));
    menuEl.classList.add('open');
  }
  /* @ 补全数据源：mdo=真实工作区文件（30s 缓存），fixture=mock */
  let mdoWsFiles = null, mdoWsAt = 0;
  async function mdoEnsureWsFiles() {
    if (host.name !== 'mdo') return false;
    if (mdoWsFiles && Date.now() - mdoWsAt < 30000) return false;
    try {
      const d = await host.listWorkspaceFiles('');
      mdoWsFiles = d.files ?? [];
      mdoWsAt = Date.now();
    } catch { mdoWsFiles = mdoWsFiles ?? []; }
    return true;    /* 只有真发生了拉取才让调用方重渲染（防循环） */
  }
  function updateMenu() {
    const v = inputEl.value;
    const upto = v.slice(0, inputEl.selectionStart ?? v.length);
    const slash = v.startsWith('/') && !v.includes(' ') ? v : null;
    const atM = upto.match(/(?:^|\s)@(\S*)$/);
    if (slash) {
      const hits = SLASH_COMMANDS.filter((c) => c.cmd.startsWith(slash));
      if (hits.length && hits.some((h) => h.cmd !== slash)) {
        Object.assign(menu, { open: true, kind: 'slash', items: hits.map((h) => ({ label: h.cmd, desc: t(h.descKey), run: h.cmd })), idx: 0, start: 0 });
        renderMenu(); return;
      }
    }
    if (atM && !slash) {
      const q = atM[1].toLowerCase();
      if (!q) { closeMenu(); return; }   // D3a：裸 @ 不拉全量列表，输入 ≥1 字符再搜
      const pool = mdoWsFiles ?? MOCK_FILES;
      const hits = pool.filter((f) => f.toLowerCase().includes(q));
      if (host.name === 'mdo') mdoEnsureWsFiles().then((fetched) => { if (fetched) updateMenu(); });
      if (hits.length) {
        Object.assign(menu, { open: true, kind: 'file', items: hits.slice(0, 8).map((f) => ({ label: '@' + f, run: f })), idx: 0, start: upto.length - atM[1].length - 1 });
        renderMenu(); return;
      }
    }
    closeMenu();
  }
  function pickMenuItem() {
    const it = menu.items[menu.idx];
    if (!it) return;
    if (menu.kind === 'slash') { inputEl.value = ''; closeMenu(); execSlash(it.run); }
    else {
      const v = inputEl.value;
      const pos = inputEl.selectionStart ?? v.length;
      inputEl.value = v.slice(0, menu.start) + it.run + ' ' + v.slice(pos);
      closeMenu();
      inputEl.focus();
      inputEl.setSelectionRange(inputEl.value.length, inputEl.value.length);
    }
    autoGrow(); syncEst(); saveDraft();
  }
  function execSlash(cmd) {
    const sid = store.selectedId;
    switch (cmd) {
      case '/demo': host.runShowcase(sid); break;
      case '/image': host.runMultimodal(sid); break;
      case '/model': {
        const i = store.models.findIndex((m) => m.id === store.model?.id);
        store.model = store.models[(i + 1) % store.models.length];
        store.notifier.markDirty(); syncSelects();
        toast(t('session.modelSwitched') + store.model.name);
        break;
      }
      case '/theme':
        store.settings.theme = store.settings.theme === 'dark' ? 'light' : 'dark';
        applySettings(store); store.persist();
        break;
      case '/export': exportSessionMarkdown(store, store.sessions.get(sid)); break;
      case '/clear':
        confirmModal({
          title: t('session.clearTitle'), message: t('session.clearBody'),
          okLabel: t('session.clear'),
          onOk: async () => {
            store.truncate(sid, 0);
            if (host.name === 'mdo') {
              try { await host.clearSession(sid); } catch (e) { toast(t('session.clearFail') + e.message); }
            }
            store.persist();
            toast(t('session.cleared'));
          },
        });
        break;
      case '/settings': openSettings(store); break;
      case '/help': openHelp(); break;
      default: toast(t('session.unknownCmd') + cmd);
    }
  }
  function parts() {
    return [
      ...(inputEl.value.trim() ? [{ type: 'text', text: inputEl.value.trim() }] : []),
      ...attachments.map((a) => ({ type: 'image', dataUrl: a.dataUrl, name: a.name })),
    ];
  }
  function sendNow(s, p) {
    inputEl.value = ''; attachments = []; syncAttach(); autoGrow(); syncEst(); saveDraft();
    host.prompt(s.id, p, s.modelId || store.model?.id, host.effort);
  }
  function send() {
    const s = store.snapshot().selected;
    if (!s) return;
    if (!inputEl.value.trim() && !attachments.length) return;
    const t = inputEl.value.trim();
    if (t.startsWith('/') && SLASH_COMMANDS.some((c) => c.cmd === t)) {
      inputEl.value = ''; attachments = []; syncAttach(); autoGrow(); syncEst(); saveDraft();
      execSlash(t); return;
    }
    if (s.running) {
      s.queue.push({ parts: parts() });
      inputEl.value = ''; attachments = []; syncAttach(); autoGrow(); syncEst(); saveDraft();
      store.notifier.markDirty();
      toast(t('session.queued'));
      return;
    }
    sendNow(s, parts());
  }
  /* 交互行为·引导：中断当前回合，聊天框内容立即发出 */
  function interruptAndSend() {
    const s = store.snapshot().selected;
    if (!s) return;
    if (!inputEl.value.trim() && !attachments.length) return;
    if (s.running) host.cancel(s.id);
    const p = parts();
    sendNow(s, p);
  }
  const autoGrow = () => {
    inputEl.style.height = 'auto';
    inputEl.style.height = Math.min(336, inputEl.scrollHeight) + 'px';
  };
  const syncEst = () => { tokenEst.textContent = inputEl.value ? `~${estTokens(inputEl.value)} tok` : ''; };
  const saveDraft = () => { const sid = store.selectedId; if (sid) store.setDraft(sid, inputEl.value); };
  inputEl.addEventListener('input', () => { autoGrow(); syncEst(); saveDraft(); updateMenu(); });
  inputEl.addEventListener('keydown', (e) => {
    if (menu.open) {
      if (e.key === 'ArrowDown') { e.preventDefault(); menu.idx = (menu.idx + 1) % menu.items.length; renderMenu(); return; }
      if (e.key === 'ArrowUp') { e.preventDefault(); menu.idx = (menu.idx - 1 + menu.items.length) % menu.items.length; renderMenu(); return; }
      if (e.key === 'Enter' || e.key === 'Tab') { e.preventDefault(); pickMenuItem(); return; }
      if (e.key === 'Escape') { e.preventDefault(); closeMenu(); return; }
    }
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      // 交互行为（设置-常规）：queue=队列（默认）| guide=引导
      // 队列：Enter 入队/发送；Ctrl+Enter 中断当前并立即发送
      // 引导：Enter 中断当前并立即发送；Ctrl+Enter 入队/发送
      const guide = store.settings.interactMode === 'guide';
      const wantsInterrupt = guide ? !e.ctrlKey && !e.metaKey : (e.ctrlKey || e.metaKey);
      const s0 = store.snapshot().selected;
      if (wantsInterrupt && s0 && s0.running) { interruptAndSend(); return; }
      send();
    }
  });

  card.append(menuEl, attachRow, inputEl,
    el('div', { class: 'cmp-row' },
      addBtn,
      el('div', { class: 'cmp-tools' }, policyDD.root),
      el('div', { class: 'cmp-trailing' }, tokenEst, contextMeter(store), modelDD.root, effortDD.root, sendBtn)));
  root.replaceChildren(el('div', { class: 'cmp-stack' }, projRow, card), fileInput);
  syncProjPill();

  root._sync = () => {
    const s = store.snapshot().selected;
    const hero = !s || s.nodes.length === 0;
    projRow.style.display = hero ? '' : 'none';
    if (hero) syncProjPill();
    if (!s) return;
    syncSelects();
    if (s.running) {
      sendBtn.classList.add('stop');
      sendBtn.replaceChildren(icon('IconStopFill16', { size: 16 }));
      sendBtn.onclick = () => host.cancel(store.selectedId);
    } else {
      sendBtn.classList.remove('stop');
      sendBtn.replaceChildren(icon('IconSendOutline14', { size: 16 }));
      sendBtn.onclick = send;
    }
  };
  addBtn.onclick = () => fileInput.click();
  root._sync();
  root._restoreDraft = (sid) => {
    inputEl.value = store.drafts.get(sid) ?? '';
    attachments = []; syncAttach(); autoGrow(); syncEst(); closeMenu();
  };
}

// ============ Hero（旧版空状态：居中标识 + 入口卡片）============
function renderHero(listEl, store, host) {
  if (listEl.dataset.hero === '1') return;
  listEl.dataset.hero = '1';
  const chip = (title, sub, fn) => el('div', { class: 'chip', onclick: fn },
    el('div', { class: 'c-title' }, title), el('div', { class: 'c-sub' }, sub));
  const ask = (text) => () => host.prompt(store.selectedId, [{ type: 'text', text }]);
  /* 四个经典问题 = agent 工作四象限：读（理解）/ 聊（认识）/ 修（闭环）/ 造（交互创造） */
  listEl.replaceChildren(
    el('div', { class: 'empty-state' },
      el('div', { class: 'empty-logo' }, appIcon({ size: 56 })),
      el('div', { class: 'empty-title' }, t('hero.start')),
      el('div', { class: 'empty-sub' }, '墨斗 mdo —— 原生 C 栈 agent 工作台 · 工具调用 · 审批闸门 · 多模态 · 记忆'),
      el('div', { class: 'chips' },
        chip(t('hero.q1'), t('hero.q1sub'), ask(t('hero.q1prompt'))),
        chip(t('hero.q2'), t('hero.q2sub'), ask(t('hero.q2prompt'))),
        chip(t('hero.q3'), t('hero.q3sub'), ask(t('hero.q3prompt'))),
        chip(t('hero.q4'), t('hero.q4sub'), ask(t('hero.q4prompt'))),
        chip(t('hero.q5'), t('hero.q5sub'), () => openSettingsSection(store, '计划任务')))));
}

// ============ 总装 ============
export function mountApp(store, host, refs) {
  const { chatScroll, chatList, docks, composerWrap, cvRoot, composerSeat } = refs;

  // 会话内搜索
  const find = { open: false, q: '' };
  const findBar = el('div', { class: 'find-bar' },
    el('input', { type: 'search', placeholder: t('search.inSession') }),
    el('span', { class: 'find-count' }),
    el('button', { class: 'icon-btn', 'data-tip': t('search.closeEsc') }, icon('IconCloseFill14', { size: 14 })));
  findBar.querySelector('.icon-btn').onclick = closeFind;
  findBar.querySelector('input').addEventListener('input', (e) => {
    find.q = e.target.value.trim().toLowerCase();
    store.notifier.markDirty();
  });
  function openFind() { find.open = true; findBar.classList.add('open'); findBar.querySelector('input').focus(); }
  function closeFind() { find.open = false; find.q = ''; findBar.classList.remove('open'); store.notifier.markDirty(); focusComposer(); }

  // 回到底部（sticky 槽居中悬浮；已在底部时自动隐藏）
  const toBottom = el('button', { class: 'to-bottom' }, icon('IconChevronDownOutline14', { size: 14 }));
  const toBottomSlot = el('div', { class: 'to-bottom-slot' }, toBottom);
  const updateToBottom = () => {
    const away = chatScroll.scrollHeight - chatScroll.scrollTop -
      chatScroll.clientHeight > 120;
    toBottomSlot.toggleAttribute('data-hide', !away);
  };
  chatScroll.addEventListener('scroll', updateToBottom, { passive: true });
  toBottom.onclick = () => { chatScroll.scrollTop = chatScroll.scrollHeight; };
  chatScroll.parentElement.append(findBar);

  // composer 高度发布（浮动件让位，dsh --dsh-composer-height 同义）
  const seatObserver = new ResizeObserver(() => {
    chatScroll.style.setProperty('--composer-height', composerSeat.offsetHeight + 'px');
  });
  seatObserver.observe(composerSeat);

  mountComposer(composerWrap, store, host);

  // 事件委托：复制 / 折叠切换 / 重试 / 编辑 / 反馈 / 排队移除
  // （对话行在 chatList，停靠面板在 docks —— 两处共用同一处理器）
  const onAct = (e) => {
    const copyBtn = e.target.closest('[data-copy-id]');
    if (copyBtn) {
      const pre = document.getElementById(copyBtn.dataset.copyId);
      if (pre) navigator.clipboard?.writeText(pre.textContent).then(() => {
        copyBtn.textContent = t('act.copied');
        setTimeout(() => { copyBtn.textContent = t('act.copy'); }, 1200);
      });
      return;
    }
    const act = e.target.closest('[data-act]');
    if (!act) return;
    const s = store.snapshot().selected;
    if (!s) return;
    const kind = act.dataset.act;
    const row = act.closest('[data-id]');
    const node = row ? s.nodes.find((n) => n.id === row.dataset.id) : null;
    switch (kind) {
      case 'toggle-think': {
        const r = act.closest('.think-root');
        if (r) r.toggleAttribute('data-open');
        if (node) { node.v++; store.notifier.markDirty(); }
        break;
      }
      case 'toggle-tool': {
        const r = act.closest('.tool-root');
        if (r) r.toggleAttribute('data-open');
        if (node) { node.v++; store.notifier.markDirty(); }
        break;
      }
      case 'toggle-todo': {
        if (s.todoPanel) { s.todoPanel.open = !s.todoPanel.open; store.notifier.markDirty(); }
        break;
      }
      case 'toggle-queue': {
        s._queueOpen = s._queueOpen === false; store.notifier.markDirty();
        break;
      }
      case 'drop-queue': {
        s.queue.splice(+act.dataset.idx ?? 0, 1); store.notifier.markDirty();
        break;
      }
      case 'copy-msg':
        if (node) navigator.clipboard?.writeText(nodeText(node)).then(() => toast(t('act.copied')));
        break;
      case 'retry':
        if (!s.running) host.retry(s.id);
        else toast('session.stopFirst');
        break;
      case 'fork-sess': {
        if (!window.__app.forkSession) { toast('session.noForkHost'); break; }
        if (s.running) { toast('运行中不能分叉'); break; }
        toast('正在创建分叉…');
        window.__app.forkSession(s.id).then((meta) => {
          toast('已分叉：' + meta.title);
        }).catch((e) => toast('分叉失败：' + e.message));
        break;
      }
      case 'edit-user': {
        if (s.running) { toast('session.noEditRunning'); break; }
        const seq = +node.id.slice(1);
        promptModal({
          title: t('act.edit'), label: t('act.editHint'), value: node.text ?? '',
          onOk: (v) => host.editUserMessage(s.id, seq, v),
        });
        break;
      }
      case 'fb-good':
      case 'fb-bad': {
        // 事件溯源：互斥切换/取消；本地立即生效，mdo 模式写穿服务端 UI 日志
        const which = act.dataset.act === 'fb-good' ? 'good' : 'bad';
        const value = node?.feedback === which ? 'none' : which;
        if (!node) break;
        store.append(s.id, 'feedback/set', { nodeId: node.id, value });
        if (host.name === 'mdo') {
          host.feedbackSession(s.id, node.id, value).catch(() => {});
        }
        break;
      }
    }
  };
  chatList.addEventListener('click', onAct);
  docks.addEventListener('click', onAct);

  // 全局快捷键
  document.addEventListener('keydown', (e) => {
    const inInput = /input|textarea|select/i.test(e.target.tagName);
    if (e.key === 'Escape') {
      if (document.querySelector('.overlay')) { closeModal(); return; }
      if (isSettingsView()) { closeSettings(store); return; }
      if (find.open) { closeFind(); return; }
      const s = store.snapshot().selected;
      if (s?.running) { host.cancel(s.id); toast(t('msg.stopped2')); }
      return;
    }
    const k = e.key.toLowerCase();
    if ((e.ctrlKey || e.metaKey) && k === 'k') { e.preventDefault(); window.__app.newSession(); return; }
    if ((e.ctrlKey || e.metaKey) && k === 'f') { e.preventDefault(); openFind(); return; }
    if ((e.ctrlKey || e.metaKey) && k === 'e') { e.preventDefault(); exportSessionMarkdown(store, store.snapshot().selected); return; }
    if ((e.ctrlKey || e.metaKey) && e.key === ',') { e.preventDefault(); openSettings(store); return; }
    if ((e.ctrlKey || e.metaKey) && k === 'j') {
      e.preventDefault();
      store.settings.theme = store.settings.theme === 'dark' ? 'light' : 'dark';
      applySettings(store); store.persist(); return;
    }
    if (e.key === '?' && !inInput) { e.preventDefault(); openHelp(); return; }
  });
  window.addEventListener('focus', () => { document.title = 'NativeHarness — agent 前端'; });

  let lastSid = null;
  function render() {
    const settingsOn = isSettingsView();
    cvRoot.hidden = settingsOn;
    if (settingsOn) return;
    composerWrap._sync?.();
    const s = store.snapshot().selected;
    if (!s) return;
    if (s.id !== lastSid) { lastSid = s.id; composerWrap._restoreDraft?.(s.id); }

    // 阶段：hero（空） / active（有内容）
    const phase = s.nodes.length === 0 && !s.lastError ? 'hero' : 'active';
    if (cvRoot.dataset.phase !== phase) {
      cvRoot.dataset.phase = phase;
      if (phase === 'hero') chatList.replaceChildren();
    }
    composerWrap.querySelector('.cmp-stack')?.classList.toggle('hero', phase === 'hero');

    // 排队派发
    if (!s.running && !s.approvals.size && s.queue.length && !s._queueBusy) {
      const item = s.queue.shift();
      s._queueBusy = true;
      store.notifier.markDirty();
      host.prompt(s.id, item.parts, s.modelId || store.model?.id, host.effort).finally(() => { s._queueBusy = false; store.notifier.markDirty(); });
      return;
    }

    // hero 分支
    if (phase === 'hero') { renderHero(chatList, store, host); renderDocks(docks, store, host, s); return; }
    if (chatList.dataset.hero === '1') { chatList.dataset.hero = '0'; chatList.replaceChildren(); }

    const pinned = chatScroll.scrollHeight - chatScroll.scrollTop - chatScroll.clientHeight < 90;

    // 错误横幅
    const banner = chatList.querySelector('.error-banner');
    if (s.lastError && !banner) {
      chatList.prepend(el('div', { class: 'error-banner' },
        el('span', null, `⚠ ${s.lastError.message}`),
        el('button', { class: 'icon-btn', onclick: () => { s.lastError = null; store.notifier.markDirty(); } }, icon('IconCloseFill14', { size: 14 }))));
    } else if (!s.lastError && banner) banner.remove();

    // 节点对账（流列结构：error-banner + flow-scroll > flow-column > rows + toBottomSlot）
    let scroll = chatList.querySelector('.flow-scroll');
    if (!scroll) {
      scroll = el('div', { class: 'flow-scroll' }, el('div', { class: 'flow-column' }));
      chatList.append(scroll);
    }
    const col = scroll.querySelector('.flow-column');
    const q = find.open && find.q ? find.q : null;
    let hits = 0;
    const nodes = s.nodes;
    let lastAsstId = null;
    for (let i = nodes.length - 1; i >= 0; i--) {
      if (nodes[i].kind === 'assistant') { lastAsstId = nodes[i].id; break; }
    }
    const rows = [...col.children].filter((c) => c.classList?.contains('flow-item'));
    for (let i = 0; i < nodes.length; i++) {
      const n = nodes[i];
      let cur = rows[i];
      if (!cur || cur.dataset.id !== n.id || +cur.dataset.v !== n.v) {
        const keep = cur?.dataset.id === n.id ? captureKeep(cur) : undefined;
        const fresh = renderNode(n, keep, n.id === lastAsstId ? (s.statsAcc ?? null) : null);
        if (cur) { cur.replaceWith(fresh); rows[i] = fresh; }
        else { col.append(fresh); rows.push(fresh); }
        cur = fresh;
      }
      if (q) {
        const hit = nodeText(n).toLowerCase().includes(q);
        cur.classList.toggle('find-hide', !hit);
        if (hit) hits++;
      } else cur.classList.remove('find-hide');
    }
    while (rows.length > nodes.length) rows.pop()?.remove();
    if (!col.contains(toBottomSlot)) col.append(toBottomSlot);
    updateToBottom();
    if (find.open) findBar.querySelector('.find-count').textContent = q ? t('{n} 处匹配', { n: hits }) : '';

    renderDocks(docks, store, host, s);
    if (pinned) chatScroll.scrollTop = chatScroll.scrollHeight;
  }

  store.notifier.subscribe(render);
  render();
  applySettings(store);
}
