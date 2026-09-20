// wire.js — 协议层骨架。宿主实现同一接口：
//
//   FixtureHost  —— 本地演示（脚本化事件流，离线可跑）
//   XsHttpHost   —— agent-demo 的脚本化后端（HTTP 轮询）
//   MdoHost      —— mdo C 后端（xllm/xllm-session/xwork 进程内直调；HTTP 轮询）
//   DshHttpHost  —— dsh 兼容线协议（HTTP POST /api 上行 + WebSocket 下行）。
//
// 宿主接口（UI 只依赖这组方法）：
//   prompt(sessionId, parts)              提交一轮输入（parts 含 text/image）
//   respondApproval(id, decision)         审批决议（allow-once / allow-session / rejected）
//   cancel(sessionId)                     中断运行
//   retry(sessionId)                      重跑最后一轮（截断重放后重发）
//   editUserMessage(sessionId, seq, text) 编辑历史用户消息并重发（分支）
//
// mdo 后端路由（会话/模型/项目由服务端持久化，前端为薄视图）：
//   GET/POST /api/models|projects|sessions   管理面
//   GET  /api/sessions/<id>/events           UI 事件日志全量重放
//   POST /api/prompt → {turnId}              轮询 /api/turn/<id>/events?since=N

export class FixtureHost {
  name = 'fixture';
  #cancelled = new Set();
  #waiters = new Map(); // approvalId -> resolve(decision)

  constructor(store, options = {}) {
    this.store = store;
    this.scale = options.scale ?? 1;      // 演示速度因子（截图/演示可调快）
    this.autoApprove = options.autoApprove ?? false;
    this.instant = options.instant ?? false; // 整块落账（无头/后台窗口截图用）
    this.effort = 'high';                 // 思考力度（演示：off 时不推 reasoning 流）
    this.ready = Promise.resolve();
  }

  async listSessions() { return []; }

  /** 用户提交输入：宿主记账 user/message（宿主是日志权威），再跑一轮脚本化回合。 */
  async prompt(sessionId, parts) { /* 由 fixture.js 注入实现 */ }
  async runShowcase(sessionId) { /* 同上 */ }
  async runMultimodal(sessionId) { /* 同上：图片+todo 推进+子代理演示 */ }
  async retry(sessionId) { /* 由 fixture.js 注入 */ }
  async editUserMessage(sessionId, seq, text) { /* 由 fixture.js 注入 */ }

  async respondApproval(id, decision) {
    const w = this.#waiters.get(id);
    if (w) { this.#waiters.delete(id); w(decision); }
  }
  async cancel(sessionId) { this.#cancelled.add(sessionId); }

  /** 脚本里等待审批：返回决议；autoApprove / 会话策略 auto 则自动放行。 */
  awaitApproval(sessionId, approval) {
    const s = this.store.sessions.get(sessionId);
    const auto = this.autoApprove || s?.policy === 'auto';
    this.store.append(sessionId, 'approval/requested', approval);
    if (auto) {
      return new Promise((resolve) => setTimeout(() => {
        this.store.append(sessionId, 'approval/resolved', { id: approval.id, decision: 'allow-session' });
        resolve('allow-session');
      }, 400 * this.scale));
    }
    return new Promise((resolve) => this.#waiters.set(approval.id, (decision) => {
      this.store.append(sessionId, 'approval/resolved', { id: approval.id, decision });
      resolve(decision);
    }));
  }

  get cancelled() { return false; }
  isCancelled(sessionId) { return this.#cancelled.has(sessionId); }
  clearCancel(sessionId) { this.#cancelled.delete(sessionId); }
}

// ============ XsHttpHost — xs C 脚本后端（HTTP 轮询） ============
//
// 后端是「回合运行器」：POST /api/prompt 启动一个脚本化回合，
// 本宿主每 250ms 轮询 GET /api/turn/<id>/events?since=N 拉新事件
// 并 append 到 Store。历史由前端持有（事件溯源 + localStorage），
// 后端无会话状态，只管跑完当前回合。

export class XsHttpHost {
  name = 'xs';
  /** sessionId -> { turnId, since, done } */
  #turns = new Map();
  #pollTimer = null;
  #cancelled = new Set();
  #approvalTurn = null; // {sessionId, turnId}

  constructor(store, options = {}) {
    this.store = store;
    this.base = options.base ?? '';       // 同源：空串 = location.origin
    this.effort = 'high';
    this.ready = Promise.resolve();
  }

  async #api(path, opts = {}) {
    try {
      const res = await fetch(this.base + path, {
        headers: { 'content-type': 'application/json' },
        ...opts,
      });
      if (!res.ok) throw new Error(`${path} → HTTP ${res.status}`);
      const data = await res.json();
      if (this.store.connection !== 'ok') {
        this.store.connection = 'ok';
        this.store.notifier.markDirty();
      }
      return data;
    } catch (e) {
      if (this.store.connection !== 'down') {
        this.store.connection = 'down';
        this.store.notifier.markDirty();
      }
      throw e;
    }
  }

  // ---- 宿主接口 ----

  async prompt(sessionId, parts) {
    const text = parts.filter((p) => p.type === 'text').map((p) => p.text).join('\n');
    const data = await this.#api('/api/prompt', {
      method: 'POST',
      body: JSON.stringify({ sessionId, text }),
    });
    if (!data.ok) throw new Error(data.error ?? 'prompt failed');
    this.#turns.set(sessionId, { turnId: data.turnId, since: 0, done: false });
    this.#startPolling();
  }

  /** 空态演示卡片：复用 prompt 路由到 C 端的 tools / chart 脚本。 */
  async runShowcase(sessionId) {
    await this.prompt(sessionId, [{ type: 'text', text: '请用工具完整演示：构建并审批' }]);
  }
  async runMultimodal(sessionId) {
    await this.prompt(sessionId, [{ type: 'text', text: '画个趋势图分析一下' }]);
  }

  async respondApproval(id, decision) {
    const entry = this.#approvalTurn;
    if (!entry) return;
    await this.#api(`/api/turn/${entry.turnId}/approval`, {
      method: 'POST',
      body: JSON.stringify({ id, decision }),
    });
    // approval/resolved 事件会经轮询到达
  }

  async cancel(sessionId) {
    const entry = this.#turns.get(sessionId);
    if (!entry) return;
    this.#cancelled.add(sessionId);
    try {
      await this.#api(`/api/turn/${entry.turnId}/cancel`, { method: 'POST' });
    } catch { /* 后端可能已完成 */ }
  }

  async retry(sessionId) {
    const s = this.store.sessions.get(sessionId);
    if (!s) return;
    let lastUserIdx = -1, lastUserText = '';
    for (let i = 0; i < s.log.events.length; i++) {
      const ev = s.log.events[i];
      if (ev.type === 'user/message') {
        lastUserIdx = i;
        lastUserText = (ev.data.content ?? [])
          .filter((p) => p.type === 'text').map((p) => p.text).join('\n');
      }
    }
    if (lastUserIdx < 0) return;
    this.store.truncate(sessionId, lastUserIdx);
    try { await this.truncateSession(sessionId, lastUserIdx); } catch { /* 瞬断：重放略有残尾 */ }
    await this.prompt(sessionId, [{ type: 'text', text: lastUserText }]);
  }

  async editUserMessage(sessionId, seq, text) {
    const s = this.store.sessions.get(sessionId);
    if (!s) return;
    const idx = s.log.events.findIndex((e) => e.seq === seq);
    if (idx < 0) return;
    this.store.truncate(sessionId, idx);
    try { await this.truncateSession(sessionId, idx); } catch { /* 同上 */ }
    await this.prompt(sessionId, [{ type: 'text', text }]);
  }

  get cancelled() { return this.#cancelled.size > 0; }
  isCancelled(sessionId) { return this.#cancelled.has(sessionId); }
  clearCancel(sessionId) { this.#cancelled.delete(sessionId); }

  // ---- 轮询引擎 ----

  #startPolling() {
    if (this.#pollTimer) return;
    this.#pollTimer = setTimeout(() => this.#pollTick(), 100);
  }

  async #pollTick() {
    this.#pollTimer = null;
    const active = [...this.#turns.entries()].filter(([, t]) => !t.done);
    if (active.length === 0) return;

    for (const [sessionId, entry] of active) {
      try {
        const data = await this.#api(
          `/api/turn/${entry.turnId}/events?since=${entry.since}`);
        if (!data.ok) continue;
        for (const ev of data.events ?? []) {
          this.store.append(sessionId, ev.type, ev.data);
          entry.since = ev.seq;
          if (ev.type === 'approval/requested') {
            this.#approvalTurn = { sessionId, turnId: entry.turnId };
            // 自动放行：全局设置或会话策略 auto（对齐 fixture 宿主语义）
            const s = this.store.sessions.get(sessionId);
            if (this.store.settings.autoApprove || s?.policy === 'auto') {
              setTimeout(() => this.respondApproval(ev.data.id, 'allow-session'), 250);
            }
          }
          if (ev.type === 'approval/resolved') {
            this.#approvalTurn = null;
          }
        }
        if (data.done) entry.done = true;
      } catch { /* 网络瞬断：下一轮再试 */ }
    }

    if ([...this.#turns.values()].some((t) => !t.done)) {
      this.#pollTimer = setTimeout(() => this.#pollTick(), 250);
    }
  }
}

// ============ MdoHost — mdo C 后端（xllm/xwork 进程内直调，HTTP 轮询） ============
//
// 服务端是权威：会话/模型/项目持久化在 ~/.mdo；本宿主负责
// 启动/恢复/管理面调用 + 回合事件轮询（事件同时写穿服务端 UI 日志，
// 刷新后由 hydrate 全量重放）。

export class MdoHost {
  name = 'mdo';
  #turns = new Map();       // sessionId -> { turnId, since, done }
  #pollTimer = null;
  #cancelled = new Set();
  #approvalTurn = null;
  #askTurn = null;

  constructor(store, options = {}) {
    this.store = store;
    this.base = options.base ?? '';
    this.effort = 'high';
    this.ready = Promise.resolve();
  }

  async #api(path, opts = {}) {
    try {
      const res = await fetch(this.base + path, {
        headers: { 'content-type': 'application/json' },
        ...opts,
      });
      if (!res.ok) throw new Error(`${path} → HTTP ${res.status}`);
      const data = await res.json();
      if (this.store.connection !== 'ok') {
        this.store.connection = 'ok';
        this.store.notifier.markDirty();
      }
      return data;
    } catch (e) {
      if (this.store.connection !== 'down') {
        this.store.connection = 'down';
        this.store.notifier.markDirty();
      }
      throw e;
    }
  }

  // ---- 管理面 ----

  async getSettings() { return this.#api('/api/settings'); }
  async saveSettings(settings) {
    return this.#api('/api/settings', { method: 'POST', body: JSON.stringify({ settings }) });
  }
  async listModels() {
    const d = await this.#api('/api/models');
    return d.ok ? d : { models: [], defaultModel: null };
  }
  async saveModel(model, makeDefault) {
    return this.#api('/api/models', {
      method: 'POST',
      body: JSON.stringify({ model, default: makeDefault ? model.id : undefined }),
    });
  }
  async deleteModel(id) {
    return this.#api('/api/models/delete', { method: 'POST', body: JSON.stringify({ id }) });
  }
  async listProjects() { return this.#api('/api/projects'); }
  async addProject(path) {
    return this.#api('/api/projects', { method: 'POST', body: JSON.stringify({ path }) });
  }
  async activateProject(slug) {
    return this.#api('/api/projects/activate', { method: 'POST', body: JSON.stringify({ slug }) });
  }
  async deleteProject(slug, purge) {
    return this.#api('/api/projects/delete', {
      method: 'POST', body: JSON.stringify({ slug, purge: !!purge }),
    });
  }
  async setProjectDefaultModel(slug, defaultModel) {
    return this.#api('/api/projects/config', {
      method: 'POST', body: JSON.stringify({ slug, defaultModel }),
    });
  }
  /** @ 补全：活动项目工作区文件（相对路径，浅层优先，服务端过滤） */
  async listWorkspaceFiles(q = '') {
    const d = await this.#api(`/api/workspace/files${q ? `?q=${encodeURIComponent(q)}` : ''}`);
    return d.ok ? d : { files: [], root: '' };
  }
  async listSessions(project) {
    const d = await this.#api('/api/sessions' + (project ? `?project=${encodeURIComponent(project)}` : ''));
    return d.ok ? d.sessions : [];
  }
  /** 反馈记录聚合（当前项目桶；D3b 数据视图） */
  async listFeedback() {
    const d = await this.#api('/api/feedback');
    return d.ok ? d.items : [];
  }
  /** 全库会话轻量清单（跨项目分组用；含 project 字段） */
  async listAllSessions() {
    const d = await this.#api('/api/sessions?all=1');
    return d.ok ? d.sessions : [];
  }
  async createSession(title, modelId, systemPrompt) {
    const d = await this.#api('/api/sessions', {
      method: 'POST',
      body: JSON.stringify({ title, model: modelId, systemPrompt: systemPrompt || undefined }),
    });
    if (!d.ok) throw new Error(d.error ?? 'create session failed');
    return d;
  }
  /** 重置会话上下文（保留标题/模型/置顶；服务端日志与快照全清） */
  async clearSession(id) {
    return this.#api('/api/sessions/clear', { method: 'POST', body: JSON.stringify({ id }) });
  }
  /** UI 日志按行截断（重试/编辑重发后的服务端重放对齐；模型上下文保留完整历史） */
  async truncateSession(id, keepCount) {
    return this.#api('/api/sessions/truncate', { method: 'POST', body: JSON.stringify({ id, keepCount }) });
  }
  async pinSession(id, pinned) {
    return this.#api('/api/sessions/pin', { method: 'POST', body: JSON.stringify({ id, pinned }) });
  }
  /** 点赞/点踩写穿：追加 feedback/set 事件进服务端 UI 日志（value: good|bad|none） */
  async feedbackSession(id, nodeId, value) {
    return this.#api('/api/sessions/feedback', {
      method: 'POST', body: JSON.stringify({ id, nodeId, value }),
    });
  }
  /** 会话分叉：完整复制（LLM 状态+UI 日志+meta），返回新会话 {id,title,model} */
  async forkSession(id) {
    const d = await this.#api('/api/sessions/fork', { method: 'POST', body: JSON.stringify({ id }) });
    if (!d.ok) throw new Error(d.error ?? 'fork failed');
    return d;
  }
  async renameSession(id, title) {
    return this.#api('/api/sessions/rename', { method: 'POST', body: JSON.stringify({ id, title }) });
  }
  async deleteSession(id) {
    return this.#api('/api/sessions/delete', { method: 'POST', body: JSON.stringify({ id }) });
  }
  /** 全量重放一个会话的服务端 UI 事件日志（恢复视图用）。 */
  async fetchSessionEvents(id) {
    const d = await this.#api(`/api/sessions/${id}/events`);
    return d.ok ? d : { events: [] };
  }

  // ---- 宿主接口 ----

  async prompt(sessionId, parts, modelId, effort) {
    // 懒会话升级（zcode 式）：首条消息才真正建服务端会话，本地占位 id 原位重挂
    const draft = this.store.sessions.get(sessionId);
    if (draft?.lazy) {
      const meta = await this.createSession(null, draft.modelId || modelId,
        this.store.settings.systemPrompt);
      this.store.reattachId(sessionId, meta.id);
      sessionId = meta.id;
    }
    const text = parts.filter((p) => p.type === 'text').map((p) => p.text).join('\n');
    const images = parts.filter((p) => p.type === 'image').map((p) => p.dataUrl);
    const data = await this.#api('/api/prompt', {
      method: 'POST',
      body: JSON.stringify({
        sessionId, text,
        modelId: modelId || undefined,
        images,
        effort: effort && effort !== 'off' ? effort : undefined,
      }),
    });
    if (!data.ok) throw new Error(data.error ?? 'prompt failed');
    this.#turns.set(sessionId, { turnId: data.turnId, since: 0, done: false });
    this.#startPolling();
  }
  /** D2：刷新恢复——接管服务端仍在跑的回合（水合时已知 turnId 与已见 seq） */
  resumeTurn(sessionId, turnId, since) {
    this.#turns.set(sessionId, { turnId, since: since || 0, done: false });
    this.#startPolling();
  }

  async runShowcase(sessionId) {
    await this.prompt(sessionId, [{ type: 'text', text: '用 exec 工具运行 cmd /c echo mdo-ready 并汇报输出' }]);
  }
  async runMultimodal(sessionId) {
    await this.prompt(sessionId, [{ type: 'text', text: '读取工作目录里的一个文件并总结内容' }]);
  }

  async respondApproval(id, decision) {
    const entry = this.#approvalTurn;
    if (!entry) return;
    await this.#api(`/api/turn/${entry.turnId}/approval`, {
      method: 'POST',
      body: JSON.stringify({ id, decision }),
    });
    // approval/resolved 事件会经轮询到达
  }

  async respondAsk(askId, answer) {
    await this.#api('/api/ask', {
      method: 'POST',
      body: JSON.stringify({ id: askId, answer }),
    });
  }
  async cancel(sessionId) {
    const entry = this.#turns.get(sessionId);
    if (!entry) return;
    this.#cancelled.add(sessionId);
    try {
      await this.#api(`/api/turn/${entry.turnId}/cancel`, { method: 'POST' });
    } catch { /* 后端可能已完成 */ }
  }

  async retry(sessionId) {
    const s = this.store.sessions.get(sessionId);
    if (!s) return;
    let lastUserIdx = -1, lastUserText = '';
    for (let i = 0; i < s.log.events.length; i++) {
      const ev = s.log.events[i];
      if (ev.type === 'user/message') {
        lastUserIdx = i;
        lastUserText = (ev.data.content ?? [])
          .filter((p) => p.type === 'text').map((p) => p.text).join('\n');
      }
    }
    if (lastUserIdx < 0) return;
    this.store.truncate(sessionId, lastUserIdx);
    try { await this.truncateSession(sessionId, lastUserIdx); } catch { /* 瞬断：重放略有残尾 */ }
    await this.prompt(sessionId, [{ type: 'text', text: lastUserText }]);
  }

  async editUserMessage(sessionId, seq, text) {
    const s = this.store.sessions.get(sessionId);
    if (!s) return;
    const idx = s.log.events.findIndex((e) => e.seq === seq);
    if (idx < 0) return;
    this.store.truncate(sessionId, idx);
    try { await this.truncateSession(sessionId, idx); } catch { /* 同上 */ }
    await this.prompt(sessionId, [{ type: 'text', text }]);
  }

  get cancelled() { return this.#cancelled.size > 0; }
  isCancelled(sessionId) { return this.#cancelled.has(sessionId); }
  clearCancel(sessionId) { this.#cancelled.delete(sessionId); }

  // ---- 轮询引擎（同 XsHttpHost 语义） ----

  #startPolling() {
    if (this.#pollTimer) return;
    this.#pollTimer = setTimeout(() => this.#pollTick(), 100);
  }

  async #pollTick() {
    this.#pollTimer = null;
    const active = [...this.#turns.entries()].filter(([, t]) => !t.done);
    if (active.length === 0) return;

    for (const [sessionId, entry] of active) {
      try {
        const data = await this.#api(
          `/api/turn/${entry.turnId}/events?since=${entry.since}`);
        if (!data.ok) continue;
        for (const ev of data.events ?? []) {
          this.store.append(sessionId, ev.type, ev.data);
          entry.since = ev.seq;
          if (ev.type === 'approval/requested') {
            this.#approvalTurn = { sessionId, turnId: entry.turnId };
            // 自动放行：全局设置或会话策略 auto（对齐 fixture 宿主语义）
            const s = this.store.sessions.get(sessionId);
            if (this.store.settings.autoApprove || s?.policy === 'auto') {
              setTimeout(() => this.respondApproval(ev.data.id, 'allow-session'), 250);
            }
          }
          if (ev.type === 'approval/resolved') {
            this.#approvalTurn = null;
          }
          if (ev.type === 'ask_user/requested') {
            this.#askTurn = { sessionId, turnId: entry.turnId };
          }
          if (ev.type === 'ask_user/resolved') {
            this.#askTurn = null;
          }
        }
        if (data.done) {
          entry.done = true;
          this.clearCancel(sessionId);
        }
      } catch { /* 网络瞬断：下一轮再试 */ }
    }

    if ([...this.#turns.values()].some((t) => !t.done)) {
      this.#pollTimer = setTimeout(() => this.#pollTick(), 250);
    }
  }
}

/** dsh 线协议宿主（骨架；未验证连接，仅示意对接面）。 */
export class DshHttpHost {
  name = 'dsh-wire';
  constructor(base) {
    this.base = base.replace(/\/$/, ''); // 如 http://127.0.0.1:8443
  }
  async #post(path, payload) {
    const res = await fetch(this.base + path, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ rpcId: crypto.randomUUID(), payload }),
    });
    if (!res.ok) throw new Error(`${path} → HTTP ${res.status}`);
    const envelope = await res.json();
    if (envelope.payload?.kind === 'error') throw new Error(envelope.payload.error.message);
    return envelope.payload?.value;
  }
  request(method, params) {
    return this.#post('/api', { kind: 'request', method, params });
  }
  respond(approvalId, decision) {
    return this.#post('/api/respond', { kind: 'respond', approvalId, decision });
  }
  /** 下行帧流：/api/events.mux。async 迭代器逐帧产出 payload。 */
  async *events(signal) {
    const url = new URL('/api/events.mux', this.base);
    url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(url);
    signal?.addEventListener('abort', () => ws.close(), { once: true });
    const queue = []; let wake = null; let ended = false;
    ws.onmessage = (e) => {
      const full = JSON.parse(e.data);
      queue.push(full.payload); wake?.(); wake = null;
    };
    ws.onclose = () => { ended = true; wake?.(); wake = null; };
    await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
    while (true) {
      while (queue.length) yield queue.shift();
      if (ended) return;
      await new Promise((resolve) => { wake = resolve; });
    }
  }
}
