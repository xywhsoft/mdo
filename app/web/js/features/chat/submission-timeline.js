import { element, errorMessage } from "../../utils/dom.js";
import { uiMessage, setUiText, subscribeLocale } from "../../i18n.js";
import { parsePromptContext } from "./prompt-context.js";
import { promptContextChips } from "./context-chips.js";

// Presentation only. The existing Home draft, queue ID and run receipt remain
// the authoritative submission journal; displaying a bubble never sends it.
export function createSubmissionTimeline({ container, host, snapshot, scope }) {
  const rows = new Map();
  let owner = scope(), serial = 0, errorBinding = null;
  function detachError() {
    if (!errorBinding) return;
    const { node, parent, next } = errorBinding;
    if (next?.parentElement === parent) parent.insertBefore(node, next);
    else parent.append(node);
    errorBinding = null;
  }
  function reset() {
    detachError(); rows.clear(); owner = scope(); render();
  }
  function checkScope() {
    if (owner === scope()) return;
    detachError(); rows.clear(); owner = scope();
  }
  function remember(value, projectId, sessionId) {
    let row = rows.get(value.id);
    if (!row) {
      const user = element("li", { className: "timeline-item submission-user", attrs: {
        "data-kind": "user", "data-submission-id": value.id } });
      const body = element("div", { className: "timeline-body" });
      user.append(body);
      const reply = element("li", { className: "timeline-item submission-reply", attrs: {
        "data-kind": "assistant", "data-submission-id": value.id } });
      const status = element("p", { className: "submission-status", attrs: { role: "status" } });
      reply.append(status);
      row = { ...value, projectId, sessionId, order: ++serial, user, body, reply, status };
      rows.set(value.id, row);
    } else Object.assign(row, value, { projectId, sessionId });
    return row;
  }
  function render() {
    checkScope();
    const state = snapshot();
    const seen = new Set((state.events ?? []).filter(e => e.kind === "agent_start")
      .map(e => e.queue_item_id).filter(Boolean));
    for (const id of seen) rows.delete(id);
    for (const item of state.entries ?? []) {
      if (seen.has(item.id)) continue;
      remember({ ...item, durable: true }, state.projectId, state.sessionId);
    }
    const visible = [...rows.values()].filter(row => row.projectId === state.projectId &&
      (row.sessionId === state.sessionId || !row.sessionId && !state.sessionId))
      .sort((a, b) => a.order - b.order);
    const nodes = [];
    for (const [index, row] of visible.entries()) {
      if (row.painted !== row.text) {
        const parsed = parsePromptContext(row.text);
        row.body.replaceChildren(element("span", { text: parsed.text }));
        if (parsed.contexts.length) row.body.append(promptContextChips(parsed.contexts));
        if (row.attachments?.length) row.body.append(element("small", { text:
          uiMessage("submission.images", { count: row.attachments.length }, "附加图片：{count}") }));
        row.painted = row.text;
      }
      const error = row.error || (index === 0 && state.paused && state.error
        ? errorMessage({ code: state.error }) : "");
      setUiText(row.status, error || !row.durable
        ? error || uiMessage("submission.saving", {}, "正在保存消息…")
        : row.state === "sending" || index === 0 && !state.active
          ? uiMessage("submission.preparing", {}, "正在准备回复…")
          : uiMessage("submission.queued", {}, "已排队，等待上一条消息结束…"));
      row.reply.dataset.state = error ? "error" : "preparing";
      row.status.hidden = errorBinding?.id === row.id;
      nodes.push(row.user, row.reply);
    }
    if (errorBinding) {
      const row = visible.find(row => row.id === errorBinding.id);
      if (row) row.reply.append(errorBinding.node);
      else detachError();
    }
    // Reuse nodes so asynchronous status updates preserve focus and selection.
    for (const node of [...container.children]) if (!nodes.includes(node)) node.remove();
    for (let i = 0; i < nodes.length; ++i) if (container.children[i] !== nodes[i]) {
      if (container.children[i]) container.insertBefore(nodes[i], container.children[i]);
      else container.append(nodes[i]);
    }
    container.hidden = !nodes.length;
    host.classList.toggle("has-submissions", Boolean(nodes.length));
  }
  const unsubscribe = subscribeLocale(render);
  return {
    begin(value) {
      checkScope();
      const pending = [...rows.values()].find(row => !row.durable && row.projectId === value.projectId &&
        row.sessionId === value.sessionId && row.text === value.text &&
        JSON.stringify(row.attachments) === JSON.stringify(value.attachments));
      if (pending && !pending.error) return false;
      // Retrying a failed provisional bubble retains its identity. Admission
      // and uncertain-delivery guards still run before any request is sent.
      const intent = { ...value, id: pending?.id || value.id, error: "" };
      if (pending && errorBinding?.id === pending.id) detachError();
      const follow = host.scrollHeight - host.scrollTop - host.clientHeight < 96;
      remember(intent, intent.projectId, intent.sessionId); render();
      if (follow) host.scrollTop = host.scrollHeight;
      return intent.id;
    },
    accepted(id) { const row = rows.get(id); if (row) { row.durable = true; row.error = ""; } render(); },
    bindProject(projectId, sessionId) {
      if (!sessionId) return;
      for (const row of rows.values()) if (row.projectId === projectId && !row.sessionId) row.sessionId = sessionId;
      render();
    },
    fail(id, error) { const row = rows.get(id); if (row) row.error = errorMessage(error); render(); },
    forget(id) { rows.delete(id); render(); },
    attachError(node, id) {
      detachError();
      if (!rows.has(id)) return;
      errorBinding = { node, id, parent: node.parentElement, next: node.nextSibling };
      render();
    },
    detachError, render, reset,
    destroy() { unsubscribe(); detachError(); rows.clear(); container.replaceChildren(); host.classList.remove("has-submissions"); },
  };
}
