import { element, errorMessage } from "../../utils/dom.js";
import { uiMessage, setUiText, subscribeLocale } from "../../i18n.js";
import { parsePromptContext } from "./prompt-context.js";
import { promptContextChips } from "./context-chips.js";
import { createSubmissionState } from "./submission-state.js";

// Presentation only. The existing Home draft, queue ID and run receipt remain
// the authoritative submission journal; displaying a bubble never sends it.
export function createSubmissionTimeline({ container, host, snapshot, scope,
  onPresentationChange = () => {}, onProjectionChange = null, onCancel = null, onEdit = null }) {
  const lifecycle = createSubmissionState(scope);
  let owner = scope(), errorBinding = null, projection = [];
  let presented = new Set(), presentedProject, presentedSession, presentedScope;
  function detachError() {
    if (!errorBinding) return;
    const { node, parent, next } = errorBinding;
    if (next?.parentElement === parent) parent.insertBefore(node, next);
    else parent.append(node);
    errorBinding = null;
  }
  function reset() {
    detachError(); lifecycle.clear(); owner = scope(); render();
  }
  function checkScope() {
    if (owner === scope()) return;
    detachError(); lifecycle.clear(); owner = scope();
  }
  function remember(value, projectId, sessionId) {
    const row = lifecycle.put(value, projectId, sessionId);
    if (!row.user) {
      const user = element("li", { className: "timeline-item submission-user", attrs: {
        "data-kind": "user", "data-submission-id": value.id } });
      const body = element("div", { className: "timeline-body" });
      user.append(body);
      const reply = element("li", { className: "timeline-item submission-reply", attrs: {
        "data-kind": "assistant", "data-submission-id": value.id } });
      const status = element("p", { className: "submission-status", attrs: { role: "status" } });
      reply.append(status);
      Object.assign(row, { user, body, reply, status });
      if (onEdit || onCancel) {
        row.actions = element("div", { className: "submission-actions" });
        for (const [name, callback] of [["edit", onEdit], ["cancel", onCancel]]) {
          if (!callback) continue;
          const button = element("button", { className: "secondary-button", attrs: { type: "button" } });
          button.onclick = () => callback(row);
          row[`${name}Action`] = button; row.actions.append(button);
        }
        reply.append(row.actions);
      }
    }
    return row;
  }
  function render() {
    checkScope();
    const state = snapshot();
    const { seen, head, visible } = lifecycle.reconcile(state);
    for (const row of visible) remember(row, row.projectId, row.sessionId);
    const nodes = [];
    for (const [index, row] of visible.entries()) {
      const paint = JSON.stringify([row.text, row.attachments]);
      if (row.painted !== paint) {
        const parsed = parsePromptContext(row.text);
        row.body.replaceChildren(element("span", { text: parsed.text }));
        if (parsed.contexts.length) row.body.append(promptContextChips(parsed.contexts));
        if (row.attachments?.length) row.body.append(element("small", { text:
          uiMessage("submission.images", { count: row.attachments.length }, "附加图片：{count}") }));
        row.painted = paint;
      }
      const error = row.error || (row.id === head && state.paused && state.error && state.error !== "queue_stopped"
        ? errorMessage({ code: state.error }) : "");
      const editPhase = row.edit && !row.durable && !row.error ? row.phase : "";
      const explicit = !error && { stopping: "stopping", cancelled: "cancelled", failed: "failed",
        unconfirmed: "confirming", accepted: "syncing", running: "syncing", completed: "syncing" }[row.displayPhase];
      setUiText(row.status, explicit ? uiMessage(`submission.${explicit}`) : editPhase ? uiMessage(`messageEdit.${editPhase}`) : error || !row.durable
        ? error || uiMessage("submission.saving", {}, "正在保存消息…")
        : row.priority || row.interrupt
          ? uiMessage("submission.steering", {}, "正在应用新指令…")
        : row.state === "sending" || index === 0 && !state.active
          ? uiMessage("submission.preparing", {}, "正在准备回复…")
          : uiMessage("submission.queued", {}, "已排队，等待上一条消息结束…"));
      row.reply.dataset.state = error ? "error" : row.displayPhase;
      for (const name of ["edit", "cancel"]) if (row[`${name}Action`]) {
        const button = row[`${name}Action`];
        setUiText(button, uiMessage(`submission.${name}`));
        button.disabled = row.displayPhase === "stopping" || row.edit && !row.durable && !["waiting", "preparing"].includes(editPhase);
        button.hidden = name === "cancel" && ["cancelled", "completed"].includes(row.displayPhase);
      }
      row.status.hidden = errorBinding?.id === row.id;
      if(row.cancelEdit&&!row.cancelButton){
        row.cancelButton=element("button",{className:"submission-edit-cancel secondary-button",text:uiMessage("messageEdit.cancelPending"),attrs:{type:"button"}});
        row.cancelButton.onclick=()=>row.cancelEdit();row.reply.append(row.cancelButton);
      }
      if(row.cancelButton)row.cancelButton.hidden=!["waiting","preparing"].includes(editPhase);
      nodes.push(row.user, row.reply);
    }
    if (errorBinding) {
      const row = visible.find(row => row.id === errorBinding.id);
      if (row) row.reply.append(errorBinding.node);
      else detachError();
    }
    // Reuse nodes so asynchronous status updates preserve focus and selection.
    if (!onProjectionChange) {
    for (const node of [...container.children]) if (!nodes.includes(node)) node.remove();
    for (let i = 0; i < nodes.length; ++i) if (container.children[i] !== nodes[i]) {
      if (container.children[i]) container.insertBefore(nodes[i], container.children[i]);
      else container.append(nodes[i]);
    }
    }
    projection = visible.flatMap(row => [row.user, row.reply].map((node, i) => ({
      key: `submission-${row.id}-${i}`, kind: "submission-slot", node, afterEventId: row.afterEventId || 0 })));
    container.hidden = !nodes.length;
    host.classList.toggle("has-submissions", Boolean(nodes.length));
    const nextPresented = new Set([...seen, ...visible.map(row => row.id)]);
    const changed = presentedProject !== state.projectId || presentedSession !== state.sessionId ||
      presentedScope !== owner || nextPresented.size !== presented.size ||
      [...nextPresented].some(id => !presented.has(id));
    presented = nextPresented; presentedProject = state.projectId;
    presentedSession = state.sessionId; presentedScope = owner;
    if (changed) onPresentationChange();
    onProjectionChange?.();
  }
  const unsubscribe = subscribeLocale(render);
  return {
    begin(value) {
      checkScope();
      const row = lifecycle.begin(value);
      if (!row) return false;
      // Retrying a failed provisional bubble retains its identity. Admission
      // and uncertain-delivery guards still run before any request is sent.
      const intent = row;
      if (errorBinding?.id === row.id) detachError();
      const follow = host.scrollHeight - host.scrollTop - host.clientHeight < 96;
      remember(intent, intent.projectId, intent.sessionId); render();
      if (follow) host.scrollTop = host.scrollHeight;
      return intent.id;
    },
    accepted(id) { const row = lifecycle.get(id); if (row) { row.durable = true; row.error = ""; } render(); },
    progress(id,phase) { const row=lifecycle.get(id);if(row){row.phase=phase;render();} },
    status(id, value) {
      if (value.cancel_requested && value.terminal && !value.executed && value.retained_input === false) lifecycle.delete(id);
      else lifecycle.status(id, value);
      render();
    },
    pending() { checkScope(); const state = snapshot(); return lifecycle.reconcile(state).visible; },
    projection() { return projection; },
    busy() { return lifecycle.reconcile(snapshot()).visible.some(row => !["failed", "cancelled", "completed"].includes(row.displayPhase) && !row.error); },
    bindProject(projectId, sessionId) {
      if (!sessionId) return;
      for (const row of lifecycle.values()) if (row.projectId === projectId && !row.sessionId) row.sessionId = sessionId;
      render();
    },
    fail(id, error) { const row = lifecycle.get(id); if (row) row.error = errorMessage(error); render(); },
    forget(id) { lifecycle.delete(id); render(); },
    attachError(node, id) {
      detachError();
      if (!lifecycle.get(id)) return;
      errorBinding = { node, id, parent: node.parentElement, next: node.nextSibling };
      render();
    },
    isPresented(id) {
      const state = snapshot();
      return owner === scope() && presentedProject === state.projectId &&
        presentedSession === state.sessionId && presented.has(id);
    },
    detachError, render, reset,
    destroy() { unsubscribe(); detachError(); lifecycle.clear(); presented.clear();
      container.replaceChildren(); host.classList.remove("has-submissions"); onPresentationChange(); },
  };
}
