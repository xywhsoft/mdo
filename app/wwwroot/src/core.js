// core.js — 事件溯源会话模型（dsh 移植：Notifier 节流 + append-only 日志 + 表面投影）
//
// 对应 dsh 原件：
//   dsh-client-runtime/sessions/notifier.js  → Notifier（微任务批处理 / rAF 合帧）
//   dsh-session surface.js                   → deriveMessages（三种表面事件折叠成模型消息）
//
// 本版扩展：localStorage 持久化（事件重放）、截断重放（重试/编辑重发的根基）、
//           置顶 / 排队消息 / 草稿 / 全局设置。

import { ConversationAssembler } from './assembler.js';

const raf = typeof requestAnimationFrame === 'function'
  ? requestAnimationFrame
  : (fn) => setTimeout(fn, 16);

/** 订阅 + 批量通知。markDirty 微任务批处理；markFrameDirty 每帧至多发布一次（流式）。 */
export class Notifier {
  #listeners = new Set();
  #dirty = false;
  #scheduled = 'none'; // none | microtask | frame

  subscribe(fn) {
    this.#listeners.add(fn);
    return () => this.#listeners.delete(fn);
  }

  #flush = () => {
    this.#scheduled = 'none';
    if (!this.#dirty) return;
    this.#dirty = false;
    for (const fn of [...this.#listeners]) fn();
  };

  /** 状态变更：同一微任务内的 N 次变更合并为一次发布。 */
  markDirty() {
    this.#dirty = true;
    if (this.#scheduled !== 'none') return;
    this.#scheduled = 'microtask';
    queueMicrotask(this.#flush);
  }

  /** 流式变更：一帧内至多发布一次；若已有更早的微任务调度则保持。
   *  隐藏/后台窗口中 rAF 冻结，附 setTimeout 兜底（#flush 幂等，双触发无害）。 */
  markFrameDirty() {
    this.#dirty = true;
    if (this.#scheduled !== 'none') return;
    this.#scheduled = 'frame';
    raf(this.#flush);
    setTimeout(this.#flush, 128);
  }

  /** 同步发布（dsh：受控输入写回场景；本实现保留语义备用）。 */
  notifyNow() {
    this.#dirty = true;
    this.#flush();
  }
}

/** append-only 会话事件日志。seq 单调（截断后不复用），事件永不改写。 */
export class SessionLog {
  events = [];
  #seq = 0;

  append(type, data, extra = {}) {
    const ev = { type, seq: this.#seq++, time: Date.now(), data, ...extra };
    this.events.push(ev);
    return ev;
  }

  /** 截断到前 keepCount 个事件（重试/编辑重发：丢弃其后的分支）。 */
  truncate(keepCount) {
    this.events = this.events.slice(0, keepCount);
  }

  indexOfSeq(seq) {
    return this.events.findIndex((e) => e.seq === seq);
  }
}

const SURFACE_TYPES = new Set(['user/message', 'assistant/message', 'tool/result']);

/** 表面投影：只有三种消息型事件折进“模型可见表面”，与 UI 渲染解耦。 */
export function deriveMessages(events) {
  const out = [];
  for (const ev of events) {
    if (!SURFACE_TYPES.has(ev.type)) continue;
    switch (ev.type) {
      case 'user/message': out.push({ role: 'user', content: ev.data.content }); break;
      case 'assistant/message': out.push({ role: 'assistant', content: ev.data.message.content }); break;
      case 'tool/result': out.push({ role: 'tool', callId: ev.data.callId, content: ev.data.content }); break;
    }
  }
  return out;
}

/** 会话仓库：每个会话一份 日志 + 折叠器；任何变更走帧合并通知。 */
export class Store {
  notifier = new Notifier();
  sessions = new Map();
  order = [];
  selectedId = null;
  model = null;   // {id, name, contextWindow}
  models = [];
  host = null;

  // —— 全局设置（持久化）——
  settings = {
    theme: 'auto',        // auto | light | dark（auto=跟随系统）
    fontSize: 'md',       // sm | md | lg
    sound: false,         // 完成提示音（叮咚双音）
    systemPrompt: '',     // 新会话注入系统提示词（建会话快照）
    autoApprove: false,   // 审批自动放行（也可按会话覆盖，见 session.policy）
    proxyEnabled: false,  // HTTP 代理
    proxyHost: '', proxyPort: 0, proxyUser: '', proxyPass: '',
    proxyBypass: '',      // 不走代理列表（逗号分隔，* 通配）
    caCertPath: '',       // 全局自定义 CA（文件或目录）
    preventSleep: false,  // 阻止系统休眠
  };

  drafts = new Map();     // sessionId -> 未发送文本
  connection = 'ok';      // ok | connecting | down（宿主上报，UI 呈现）

  createSession(meta = {}) {
    const s = {
      id: meta.id || 's' + Math.random().toString(36).slice(2, 9),
      title: meta.title || '新的会话',
      blank: true,
      running: false,
      done: false,            // 运行完成且未被查看（dsh completedNotifications）
      pinned: !!meta.pinned,
      updatedAt: Date.now(),
      log: new SessionLog(),
      approvals: new Map(),   // approvalId -> {id,kind,title,detail}
      asks: new Map(),        // askId -> {id,question,options}
      ctxTokens: 0,
      turns: 0,               // 完成轮数（导出/统计用）
      queue: [],              // 运行中排队的待发消息 [{parts}]
      policy: 'ask',          // ask | auto（本会话审批策略）
      lastError: null,        // {message, time} —— session/error 横幅
      todoPanel: null,        // todo/plan 投影（停靠面板，dsh TodoPanel）
      toolMs: 0,              // 工具耗时累计（StatsLine）
      statsAcc: null,         // 会话级统计投影（StatsLine）
      nodes: null,            // 由 assembler 维护
      assembler: null,
    };
    s.assembler = new ConversationAssembler(s);
    s.nodes = s.assembler.nodes;
    this.sessions.set(s.id, s);
    this.order.push(s.id);
    return s;
  }

  select(id) {
    const s = this.sessions.get(id);
    if (!s) return;
    this.selectedId = id;
    s.done = false; // 查看即清除提醒
    this.notifier.markDirty();
  }

  removeSession(id) {
    this.sessions.delete(id);
    this.drafts.delete(id);
    this.order = this.order.filter((x) => x !== id);
    if (this.selectedId === id) this.selectedId = this.order[0] ?? null;
    this.notifier.markDirty();
  }

  togglePin(id) {
    const s = this.sessions.get(id);
    if (!s) return;
    s.pinned = !s.pinned;
    this.notifier.markDirty();
  }

  /** 重命名走事件（session/title），持久化与 UI 自动一致。 */
  renameSession(id, title) {
    const t = (title ?? '').trim().slice(0, 60);
    if (!t) return;
    this.append(id, 'session/title', { title: t });
  }

  setDraft(id, text) { this.drafts.set(id, text); }

  /** 设置变更 → 防抖同步服务端（config.json settings 节）；窗口尺寸一并上报 */
  scheduleSettingsSync() {
    clearTimeout(this._settingsTimer);
    this._settingsTimer = setTimeout(() => this.syncSettingsNow(), 800);
  }
  async syncSettingsNow(win) {
    const h = this.host;
    if (!h || h.name !== 'mdo' || !h.saveSettings) return;
    const s = this.settings;
    const body = {
      theme: s.theme, fontSize: s.fontSize, lang: s.lang, sound: s.sound, autoApprove: s.autoApprove, interactMode: s.interactMode,
      systemPrompt: s.systemPrompt,
      proxyEnabled: s.proxyEnabled, proxyHost: s.proxyHost, proxyPort: s.proxyPort,
      proxyUser: s.proxyUser, proxyPass: s.proxyPass, proxyBypass: s.proxyBypass,
      caCertPath: s.caCertPath, preventSleep: s.preventSleep,
    };
    if (win) Object.assign(body, win);
    try { await h.saveSettings(body); } catch { /* 瞬断：下次变更再试 */ }
  }

  /** 懒会话升级：首个真实服务端 id 换掉本地占位 id（Map/order/drafts/selected 同步重挂） */
  reattachId(oldId, newId) {
    const s = this.sessions.get(oldId);
    if (!s || oldId === newId) return s;
    this.sessions.delete(oldId);
    s.id = newId;
    s.lazy = false;
    this.sessions.set(newId, s);
    const i = this.order.indexOf(oldId);
    if (i >= 0) this.order[i] = newId;
    if (this.drafts.has(oldId)) this.drafts.set(newId, this.drafts.get(oldId)), this.drafts.delete(oldId);
    if (this.selectedId === oldId) this.selectedId = newId;
    this.notifier.markDirty();
    return s;
  }

  /** 追加一个事件并驱动折叠器（宿主权威日志；UI 只读快照）。 */
  append(sessionId, type, data, extra) {
    const s = this.sessions.get(sessionId);
    if (!s) throw new Error('store.append: unknown session ' + sessionId);
    const ev = s.log.append(type, data, extra);
    s.assembler.apply(ev);
    s.updatedAt = ev.time;
    if (type === 'session/running' && data.value === false) s.turns++;
    this.notifier.markFrameDirty();
    return ev;
  }

  /** 截断日志到 keepCount 并整卷重放（分支语义：重试/编辑重发的基础）。 */
  truncate(sessionId, keepCount) {
    const s = this.sessions.get(sessionId);
    if (!s) return;
    s.log.truncate(keepCount);
    const assembler = new ConversationAssembler(s);
    for (const ev of s.log.events) assembler.apply(ev);
    s.assembler = assembler;
    s.nodes = assembler.nodes;
    s.running = false;
    s.approvals.clear();
    s.asks.clear();
    s.queue = [];
    s.lastError = null;
    s.todoPanel = null;
    s.toolMs = 0;
    s.statsAcc = null;
    if (s.log.events.length === 0) { s.blank = true; s.title = '新的会话'; s.ctxTokens = 0; s.turns = 0; }
    this.notifier.markDirty();
  }

  snapshot() {
    return {
      sessions: this.order.map((id) => this.sessions.get(id)),
      selected: this.sessions.get(this.selectedId) ?? null,
    };
  }

  // ============ 持久化（localStorage · 事件全量重放） ============
  // 事件溯源的好处：存档 = 原始事件数组；恢复 = 重放折叠器，UI 零特判。

  persist() {
    try {
      const data = {
        v: 1,
        selectedId: this.selectedId,
        modelId: this.model?.id ?? null,
        settings: this.settings,
        drafts: [...this.drafts],
        sessions: this.order
          .filter((id) => !this.sessions.get(id)?.lazy)   /* 懒会话=临时草稿，不落盘 */
          .map((id) => {
          const s = this.sessions.get(id);
          return {
            id: s.id, title: s.title, pinned: s.pinned, policy: s.policy, modelId: s.modelId ?? null,
            ctxTokens: s.ctxTokens, turns: s.turns, updatedAt: s.updatedAt,
            events: s.log.events.map((e) => ({ seq: e.seq, time: e.time, type: e.type, data: e.data })),
          };
        }),
      };
      localStorage.setItem('nativeharness:v1', JSON.stringify(data));
    } catch { /* 配额溢出（大图片附件）等情况静默降级为内存态 */ }
  }

  clearPersisted() {
    try { localStorage.removeItem('nativeharness:v1'); } catch { /* noop */ }
  }

  /** 从存档恢复；返回是否命中存档。 */
  restore() {
    let data = null;
    try { data = JSON.parse(localStorage.getItem('nativeharness:v1') || 'null'); } catch { /* 损坏则重置 */ }
    if (!data || !Array.isArray(data.sessions) || !data.sessions.length) return false;
    for (const saved of data.sessions) {
      const s = this.createSession({ id: saved.id, title: saved.title, pinned: saved.pinned });
      s.policy = saved.policy ?? 'ask';
      s.modelId = saved.modelId ?? null;
      s.ctxTokens = saved.ctxTokens ?? 0;
      s.turns = saved.turns ?? 0;
      s.updatedAt = saved.updatedAt ?? Date.now();
      for (const e of saved.events ?? []) {
        const ev = s.log.append(e.type, e.data);
        // 保留原始时间戳/seq（append 会重新编号，这里手工对齐用于显示）
        ev.time = e.time ?? ev.time;
        s.assembler.apply(ev);
      }
      // 恢复后强制收敛到“已完结”状态：宿主已不在，running/流式不应悬挂
      s.running = false;
      s.blank = s.log.events.length === 0;
      for (const n of s.nodes) n.streaming = false;
    }
    this.drafts = new Map(data.drafts ?? []);
    if (data.settings) Object.assign(this.settings, data.settings);
    if (data.modelId) this.model = this.models.find((m) => m.id === data.modelId) ?? this.model;
    this.selectedId = this.sessions.has(data.selectedId) ? data.selectedId : this.order[0];
    this.notifier.markDirty();
    return true;
  }
}
