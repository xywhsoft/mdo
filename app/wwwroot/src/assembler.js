// assembler.js — 会话事件 → 对话节点折叠器（dsh ConversationNodeAssembler 的移植+扩展）
//
// 输入是 append-only 事件流，输出是节点快照数组（每节点带 v 版本号，DOM 按版本增量更新）。
// 事件分类沿用 dsh 语义：
//   表面事件（折进模型上下文）：user/message | assistant/message | tool/result
//   非表面事件（仅影响 UI/状态）：assistant/chunk、assistant/reasoning、session/title、
//     session/running、session/stats、session/interrupted、session/error、approval/*、tool/update
//
// 本版扩展事件：
//   assistant/message / user/message 的 content 允许 {type:'image', dataUrl, name} 块（多模态）
//   tool/update {callId, patch} —— 原地更新已有工具卡（todo 勾选推进 / 子代理步骤 / 追加输出）
//   session/error {message}    —— 宿主错误横幅（可被后续事件或用户关闭覆盖）

export class ConversationAssembler {
  nodes = [];        // 渲染快照（顺序稳定，节点对象按 id 复用）
  #byId = new Map();
  #streaming = null; // 流式中的 assistant 节点
  #title = '';
  running = false;

  constructor(session) {
    this.session = session;
  }

  #bump() { this.session.v = (this.session.v || 0) + 1; }

  #node(partial) {
    const node = { v: 0, ...partial };
    this.nodes.push(node);
    this.#byId.set(node.id, node);
    return node;
  }

  apply(ev) {
    const d = ev.data ?? {};
    switch (ev.type) {
      case 'session/title':
        this.#title = d.title;
        this.session.title = d.title;
        this.session.blank = false;
        break;

      case 'session/running':
        this.running = !!d.value;
        if (!d.value && this.session.running) this.session.done = this.session.id !== undefined;
        this.session.running = this.running;
        break;

      case 'session/error':
        this.session.lastError = { message: d.message ?? '宿主错误', time: ev.time };
        break;

      case 'user/message': {
        this.session.blank = false;
        const parts = d.content ?? [];
        const text = parts.filter((p) => p.type === 'text').map((p) => p.text).join('\n');
        const images = parts.filter((p) => p.type === 'image');
        this.#node({ id: 'u' + ev.seq, kind: 'user', text, images, time: ev.time });
        break;
      }

      // —— 流式（非表面）：增量块，最终以 assistant/message 落账 ——
      case 'assistant/chunk': {
        const n = this.#ensureStreaming(ev);
        const last = n.blocks[n.blocks.length - 1];
        if (last && last.type === 'text') last.text += d.delta;
        else n.blocks.push({ type: 'text', text: d.delta });
        n.v++;
        break;
      }
      case 'assistant/reasoning': {
        const n = this.#ensureStreaming(ev);
        if (!n.reasonStart) n.reasonStart = ev.time;   // 思考时长起点（首增量）
        const last = n.blocks[n.blocks.length - 1];
        if (last && last.type === 'reasoning') last.text += d.delta;
        else n.blocks.push({ type: 'reasoning', text: d.delta });
        n.v++;
        break;
      }
      case 'assistant/usage': {
        const n = this.#streaming;
        if (n) { n.usage = d.usage; n.v++; }
        break;
      }

      // —— 表面事件：assistant 落账。content 块按序拆成 UI 节点 ——
      case 'assistant/message': {
        const m = d.message ?? {};
        const blocks = m.content ?? [];
        // 展示块 = 文本/思考/图片（保留原序）；tool-call 单独成卡
        const viewBlocks = blocks.filter((b) => b.type !== 'tool-call');
        const callBlocks = blocks.filter((b) => b.type === 'tool-call');

        if (this.#streaming) {
          // 同轮合并：chunk 是预览，final 落账为权威内容
          const n = this.#streaming;
          if (viewBlocks.length) n.blocks = viewBlocks;
          n.model = m.model ?? n.model;
          n.usage = m.usage ?? n.usage;
          n.thinkMs = ev.time - (n.reasonStart ?? n.time);   // 思考收口
          n.streaming = false;
          n.v++;
          this.#streaming = null;
        } else if (viewBlocks.length) {
          // 历史回放/纯文本落账：直接成节点
          this.#node({
            id: 'a' + ev.seq, kind: 'assistant', time: ev.time,
            blocks: viewBlocks, model: m.model ?? '', usage: m.usage, streaming: false,
          });
        }
        // 纯 tool-call 的落账不产生可见助手行（dsh：空 assistant/message 只承载 usage）

        // 工具调用块 → 独立工具卡节点（callId 与 tool/result / tool/update 配对）
        for (const c of callBlocks) {
          // todo/plan 不进对话流：折进 session.todoPanel（dsh：TodoPanel 停靠面板）
          if (c.name === 'todo' || c.name === 'plan') {
            this.session.todoPanel = { items: [], open: true };
            continue;
          }
          this.#node({
            id: 't' + c.callId,
            kind: 'tool',
            callId: c.callId,
            name: c.name,
            args: c.input ?? {},
            status: 'running',
            result: null,
            v0: ev.time,
          });
        }
        break;
      }

      case 'tool/result': {
        const t = this.#byId.get('t' + d.callId);
        if (t) {
          t.status = d.isError ? 'error' : 'ok';
          t.result = d.output ?? '';
          t.ms = d.ms;
          t.diff = d.diff; // edit/write 类工具的行级 diff（宿主计算视图，dsh ToolEventView 角色）
          t.v = (t.v || 0) + 1;
        }
        // 计划落账 / 工具耗时累计（StatsLine 投影）
        if (d.plan) this.session.todoPanel = { items: d.plan, open: true };
        if (typeof d.ms === 'number' && !d.isError) this.session.toolMs = (this.session.toolMs ?? 0) + d.ms;
        break;
      }

      // 原地补丁：todo 项推进 / 子代理步骤上报 / 流式追加输出。不新增节点。
      case 'tool/update': {
        const t = this.#byId.get('t' + d.callId);
        if (t) {
          const p = d.patch ?? {};
          if (p.plan) t.plan = p.plan;
          if (p.sub) t.sub = p.sub;
          if (p.append) t.result = (t.result ?? '') + p.append;
          if (p.status) t.status = p.status;
          if (p.doneCount != null) t.doneCount = p.doneCount;
          t.v = (t.v || 0) + 1;
        }
        if (d.patch?.plan) this.session.todoPanel = { items: d.patch.plan, open: this.session.todoPanel?.open ?? true };
        if (d.patch?.sub && !this.#byId.get('t' + d.callId)) {
          // todo 类（无对话节点）的子步骤上报也容忍
        }
        break;
      }

      case 'session/stats': {
        // 附加到最近的 assistant 节点 + 会话级累计（StatsLine 停靠行）
        for (let i = this.nodes.length - 1; i >= 0; i--) {
          const n = this.nodes[i];
          if (n.kind === 'assistant') { n.stats = d; n.v++; break; }
        }
        this.session.ctxTokens = d.ctxTokens ?? this.session.ctxTokens;
        const steps = this.nodes.filter((n) => n.kind === 'assistant').length;
        this.session.statsAcc = {
          turns: this.session.turns + (this.running ? 1 : 0),   /* stats 时本轮未收账，补计 */
          steps,
          llmMs: d.ms ?? 0,
          tps: d.tps,
          in: d.promptTokens, out: d.completionTokens,
          toolMs: this.session.toolMs ?? 0,
        };
        break;
      }

      case 'session/interrupted': {
        const n = this.#streaming ?? [...this.nodes].reverse().find((x) => x.kind === 'assistant');
        if (n) { n.interrupted = true; n.streaming = false; n.v++; }
        this.#streaming = null;
        break;
      }

      case 'feedback/set': {
        // 点赞/点踩落账：原地更新节点态（重渲染保真，重放恢复）
        const fb = this.#byId.get(d.nodeId);
        if (fb) { fb.feedback = d.value; fb.v++; }
        break;
      }

      case 'approval/requested':
        this.session.approvals.set(d.id, d);
        break;
      case 'approval/resolved':
        this.session.approvals.delete(d.id);
        break;
      case 'ask_user/requested':
        this.session.asks.set(d.id, d);
        break;
      case 'ask_user/resolved':
        this.session.asks.delete(d.id);
        break;

      default:
        break; // 未知事件：宽信封语义，忽略不影响折叠
    }
    this.#bump();
  }

  #ensureStreaming(ev) {
    if (this.#streaming) return this.#streaming;
    const n = this.#node({
      id: 'a' + ev.seq, kind: 'assistant', time: ev.time,
      blocks: [], model: '', streaming: true,
    });
    this.#streaming = n;
    return n;
  }

  get title() { return this.#title; }
}
