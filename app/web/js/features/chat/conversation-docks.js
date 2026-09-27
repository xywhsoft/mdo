import { approvalDecisionStatus, approvalDecisionStore, decideApproval } from "../../state/approvals.js";
import { answerAsk } from "../../state/asks.js";
import { subscribeLocale, t } from "../../i18n.js";
import { clear, element, errorMessage, isImeKey, toast } from "../../utils/dom.js";
import { taskBelongsToSession } from "../tasks/task-owner.js";

const STATE_KEYS = Object.freeze({ pending: "dock.task.pending", running: "dock.task.running" });
const EFFECT_KEYS = Object.freeze({
  read: "dock.effect.read", workspace_write: "dock.effect.workspaceWrite",
  process: "dock.effect.process", network: "dock.effect.network",
  external_service: "dock.effect.externalService", secrets: "dock.effect.secrets",
  schedule: "dock.effect.schedule", agent_delegation: "dock.effect.agentDelegation",
});
const RISK_KEYS = Object.freeze({
  low: "dock.risk.low", medium: "dock.risk.medium", high: "dock.risk.high",
});
const RESOURCE_KEYS = Object.freeze({
  path: "dock.resource.path", command: "dock.resource.command",
  process: "dock.resource.process", network: "dock.resource.network",
  external_service: "dock.resource.externalService", secret: "dock.resource.secret",
  schedule: "dock.resource.schedule", agent: "dock.resource.agent",
});

function taskCard(tasks, onOpenTasks) {
  const list = element("ul", { className: "conversation-dock-list" });
  for (const task of tasks.slice(0, 8)) list.append(element("li", {}, [
    element("span", { className: "conversation-dock-state", text: STATE_KEYS[task.state] ? t(STATE_KEYS[task.state]) : task.state }),
    element("span", { text: task.label || t("dock.task.fallback", { id: task.id }) }),
  ]));
  const open = element("button", { text: t("dock.task.details"), attrs: {
    type: "button", "data-dock-focus": "tasks/open" } });
  open.addEventListener("click", onOpenTasks);
  return element("section", { className: "conversation-dock" }, [
    element("h3", { text: t("dock.task.count", { count: tasks.length }) }), list,
    element("div", { className: "conversation-dock-actions" }, [open]),
  ]);
}

function todoCard(items, expanded, focusKey, onToggle) {
  const done = items.filter((item) => item.done).length;
  const toggle = element("button", {
    className: "todo-toggle", text: t("dock.todo.count", { done, count: items.length }),
    attrs: { type: "button", "aria-expanded": String(expanded),
      "data-dock-focus": focusKey },
  });
  toggle.addEventListener("click", onToggle);
  const card = element("section", { className: "conversation-dock todo-dock" }, [toggle]);
  if (expanded) {
    const list = element("ol", { className: "todo-dock-list" });
    const current = items.findIndex((item) => !item.done);
    for (const [index, item] of items.entries()) list.append(element("li", {
      className: item.done ? "done" : (index === current ? "current" : ""),
    }, [
      element("span", { className: "todo-dock-mark", text: item.done ? "✓" : "○" }),
      element("span", { text: item.text }),
    ]));
    card.append(list);
  }
  return card;
}

function approvalCard(item, argumentsOpen, onChanged) {
  const key = String(item.id);
  const card = element("section", { className: "conversation-dock",
    attrs: { "data-approval-id": key } });
  const resources = element("ul", { className: "conversation-dock-list" });
  for (const resource of item.resources ?? []) resources.append(element("li", {}, [
    element("span", { className: "conversation-dock-state",
      text: RESOURCE_KEYS[resource.kind] ? t(RESOURCE_KEYS[resource.kind]) :
        resource.kind || t("dock.approval.resource") }),
    element("span", { text: resource.resource }),
  ]));
  const deny = element("button", { text: t("dock.approval.deny"), attrs: {
    type: "button", "data-dock-focus": `approval/${key}/deny` } });
  const allow = element("button", { text: t("dock.approval.allowOnce"), attrs: {
    type: "button", "data-dock-focus": `approval/${key}/allow` } });
  const allowRun = element("button", { text: t("dock.approval.allowRun"), attrs: {
    type: "button", "data-dock-focus": `approval/${key}/allow_run` } });
  for (const [button, decision] of [[deny, "deny"], [allow, "allow"],
    [allowRun, "allow_run"]]) {
    button.setAttribute("aria-disabled", String(approvalDecisionStatus(key) !== "idle"));
    button.addEventListener("click", async () => {
      if (approvalDecisionStatus(key) !== "idle") return;
      try {
        if (!await decideApproval(item.id, decision)) return;
        await onChanged();
      } catch (error) {
        toast(approvalDecisionStatus(key) === "submitted"
          ? t("dock.approval.refreshPending") : errorMessage(error), "error");
      }
    });
  }
  const argumentsView = element("details", { className: "approval-arguments",
    attrs: { "data-approval-arguments": key,
      open: argumentsOpen.get(key) ? "" : null } }, [
    element("summary", { text: t("dock.approval.arguments"), attrs: {
      "data-dock-focus": `approval/${key}/arguments` } }),
    element("pre", { text: item.arguments_json || "{}" }),
  ]);
  argumentsView.addEventListener("toggle", () =>
    argumentsOpen.set(key, argumentsView.open));
  card.append(
    element("h3", { text: t("dock.approval.title", { tool: item.tool || t("dock.approval.tool") }),
      attrs: { tabindex: "-1", "data-dock-focus": `approval/${key}/title` } }),
    element("p", { text: t("dock.approval.summary", {
      risk: RISK_KEYS[item.risk] ? t(RISK_KEYS[item.risk]) :
        item.risk || t("dock.approval.unknownRisk"),
      effects: (item.effects ?? []).map((effect) => EFFECT_KEYS[effect]
        ? t(EFFECT_KEYS[effect]) : effect).join(", ") || t("dock.approval.noEffects"),
      seconds: Math.ceil(Number(item.expires_in_ms || 0) / 1000),
    }) }),
    resources,
    argumentsView,
    element("div", { className: "conversation-dock-actions" },
      [deny, allow, allowRun]),
  );
  return card;
}

function askCard(item, projectId, sessionId, deciding, answered, drafts,
  onChanged, onSettled) {
  const key = `${projectId}/${sessionId}/${item.id}`;
  const card = element("section", { className: "conversation-dock ask-dock" });
  const actions = element("div", { className: "ask-dock-options" });
  const hint = element("p", { className: "ask-dock-validation",
    attrs: { id: `ask-answer-hint-${key}`, "aria-live": "polite" } });
  const input = element("input", { className: "ask-dock-input",
    attrs: { type: "text", maxlength: "1024", placeholder: t("dock.ask.placeholder"),
      "aria-label": t("dock.ask.answerLabel"), "aria-describedby": hint.id } });
  input.value = drafts.get(key) ?? "";
  const submit = element("button", { text: t("dock.ask.submit"),
    attrs: { type: "button" } });
  const buttons = [submit];
  const encoder = new TextEncoder();
  let composing = false;
  function updateValidity() {
    const answer = input.value.trim();
    const tooLong = encoder.encode(answer).length > 1024;
    const pending = deciding.has(key);
    const submitted = answered.has(key);
    input.setAttribute("aria-invalid", String(tooLong));
    input.readOnly = pending || submitted;
    hint.textContent = pending ? t("dock.ask.submitting") :
      submitted ? t("dock.ask.submitted") :
        tooLong ? t("dock.ask.tooLong") : "";
    hint.dataset.state = pending || submitted ? "pending" :
      tooLong ? "error" : "";
    submit.setAttribute("aria-disabled", String(!answer || tooLong ||
      pending || submitted));
    for (const button of buttons.slice(1))
      button.setAttribute("aria-disabled", String(pending || submitted));
  }
  input.addEventListener("input", () => {
    drafts.set(key, input.value);
    updateValidity();
  });
  async function respond(value) {
    if (deciding.has(key) || answered.has(key)) return;
    deciding.add(key);
    updateValidity();
    try {
      await answerAsk(projectId, sessionId, item.id, value);
      answered.add(key);
      drafts.delete(key);
      await onChanged();
    } catch (error) {
      toast(answered.has(key) ? t("dock.ask.refreshPending") :
        errorMessage(error), "error");
    } finally {
      deciding.delete(key);
      if (input.isConnected) updateValidity();
      onSettled();
    }
  }
  for (const option of item.options ?? []) {
    const button = element("button", { text: option,
      attrs: { type: "button" } });
    button.addEventListener("click", () => void respond(option));
    buttons.push(button);
    actions.append(button);
  }
  submit.addEventListener("click", () => {
    if (submit.getAttribute("aria-disabled") === "false") void respond(input.value);
  });
  input.addEventListener("compositionstart", () => { composing = true; });
  input.addEventListener("compositionend", () => { composing = false; });
  input.addEventListener("blur", () => { composing = false; });
  input.addEventListener("keydown", (event) => {
    if (event.key === "Enter" && !isImeKey(event, composing)) {
      event.preventDefault();
      if (submit.getAttribute("aria-disabled") === "false") void respond(input.value);
    }
  });
  updateValidity();
  const title = element("h3", { text: t("dock.ask.title"), attrs: { tabindex: "-1" } });
  card.append(title,
    element("p", { className: "ask-dock-question", text: item.question }),
    actions, element("div", { className: "ask-dock-free" }, [input, submit]),
    hint);
  return { node: card, sync() {
    title.textContent = t("dock.ask.title");
    input.placeholder = t("dock.ask.placeholder");
    input.setAttribute("aria-label", t("dock.ask.answerLabel"));
    submit.textContent = t("dock.ask.submit");
    updateValidity();
  } };
}

export function createConversationDocks({ container, navigation, tasksStore, approvalsStore,
  asksStore, todoStore, runsStore, onOpenTasks, onChanged, onDecisionArrived }) {
  const askDeciding = new Set();
  const askAnswered = new Set();
  const expanded = new Map();
  const argumentsOpen = new Map();
  const drafts = new Map();
  const askNodes = new Map();
  const otherRoot = element("div", { className: "conversation-dock-stack" });
  const askRoot = element("div", { className: "conversation-dock-stack" });
  container.append(otherRoot, askRoot);
  const composerRegion = container.parentElement?.classList.contains("composer-region")
    ? container.parentElement : null;
  const conversation = container.closest(".workspace")?.querySelector(".conversation");
  const visibleDecisions = new Set();
  let availableHeight = 0;
  let revealFrame = 0;
  let userMovedDock = false;
  const noteUserScroll = () => { userMovedDock = true; };
  const onDockWheel = (event) => {
    noteUserScroll();
    if (event.ctrlKey || event.metaKey ||
        Math.abs(event.deltaX) >= Math.abs(event.deltaY) ||
        container.scrollHeight <= container.clientHeight) return;
    const unit = event.deltaMode === 0 ? 1 : event.deltaMode === 1
      ? (parseFloat(getComputedStyle(container).lineHeight) || 16)
      : event.deltaMode === 2 ? container.clientHeight : 0;
    if (!unit) return;
    const delta = event.deltaY * unit;
    const step = Math.max(1, Math.floor(container.clientHeight * 0.8));
    if (Math.abs(delta) <= step) return;
    // A single mouse-wheel tick can exceed a short dock's entire viewport.
    // Keep adjacent lines reachable while leaving small trackpad deltas native.
    event.preventDefault();
    container.scrollTop += Math.sign(delta) * step;
  };
  const noteKeyScroll = (event) => {
    if (["ArrowUp", "ArrowDown", "PageUp", "PageDown", "Home", "End"].includes(event.key))
      noteUserScroll();
  };
  container.addEventListener("wheel", onDockWheel, { passive: false });
  container.addEventListener("touchmove", noteUserScroll, { passive: true });
  container.addEventListener("pointerdown", noteUserScroll);
  container.addEventListener("keydown", noteKeyScroll);

  function revealDecision(arrived = null, force = false) {
    if (container.hidden) return;
    const focused = container.contains(document.activeElement)
      ? document.activeElement : null;
    if (focused) {
      const bounds = focused.getBoundingClientRect();
      const viewport = container.getBoundingClientRect();
      if (bounds.bottom > viewport.bottom)
        container.scrollTop += bounds.bottom - viewport.bottom;
      else if (bounds.top < viewport.top)
        container.scrollTop += bounds.top - viewport.top;
      return;
    }
    if (userMovedDock && !force) return;
    const decision = arrived ?? askRoot.querySelector(".ask-dock") ??
      otherRoot.querySelector("[data-approval-arguments]")?.closest(".conversation-dock");
    if (!decision) return;
    const viewport = container.getBoundingClientRect();
    const content = decision.querySelector(".ask-dock-options button") ??
      decision.querySelector(".approval-arguments summary") ??
      decision.querySelector("h3");
    if (!content) return;
    const bounds = content.getBoundingClientRect();
    // Use the least scroll that exposes the whole action; when the dock grows
    // again, move back up so more of the question is readable. A tall option
    // cannot fit in an extremely short dock, so expose its beginning instead.
    const delta = bounds.height > viewport.height - 4
      ? bounds.top - viewport.top - 4 : bounds.bottom - viewport.bottom + 4;
    if (Math.abs(delta) > 1) container.scrollTop += delta;
  }

  function scheduleReveal(decision = null, force = false) {
    cancelAnimationFrame(revealFrame);
    revealFrame = requestAnimationFrame(() => {
      revealFrame = 0;
      revealDecision(decision, force);
    });
  }

  function syncAvailableHeight(revealOnResize = false) {
    if (!conversation) return;
    const nextHeight = Math.max(0, Math.floor(conversation.clientHeight - 8));
    const changed = nextHeight !== availableHeight;
    availableHeight = nextHeight;
    container.style.setProperty("--dock-available-height", `${nextHeight}px`);
    if (revealOnResize && changed && !container.hidden) scheduleReveal();
  }
  syncAvailableHeight();

  function render() {
    const previousScroll = container.scrollTop;
    const focusedDock = otherRoot.contains(document.activeElement)
      ? document.activeElement?.dataset.dockFocus : "";
    const focusedAsk = askRoot.contains(document.activeElement);
    for (const details of otherRoot.querySelectorAll("details[data-approval-arguments]"))
      argumentsOpen.set(details.dataset.approvalArguments, details.open);
    const selected = navigation.get();
    const sessionId = selected.view === "workspace" ? selected.sessionId : "";
    const tasks = sessionId ? (tasksStore.get().data?.items ?? []).filter((item) =>
      !item.terminal && taskBelongsToSession(item, selected.projectId, sessionId)) : [];
    const runIds = new Set((runsStore.get().data?.items ?? [])
      .filter((run) => run.session_id === sessionId && run.project_id === selected.projectId)
      .map((run) => String(run.agent_run_id)));
    const approvals = sessionId ? (approvalsStore.get().data?.items ?? []).filter((item) =>
      runIds.has(String(item.run_id))) : [];
    const askData = asksStore.get().data;
    const asks = askData?.projectId === selected.projectId &&
      askData?.sessionId === sessionId ? askData.items : [];
    const todo = todoStore.get().data;
    const todoError = todoStore.get().status === "error" &&
      todo?.projectId === selected.projectId && todo?.sessionId === sessionId;
    const todoItems = todo?.projectId === selected.projectId &&
      todo?.sessionId === sessionId ? todo.items : [];
    clear(otherRoot);
    let newApproval = null;
    const nextDecisions = new Set();
    if (todoItems.length) {
      const key = `${selected.projectId}/${sessionId}`;
      const open = expanded.get(key) !== false;
      otherRoot.append(todoCard(todoItems, open, `todo/${key}`, () => {
        expanded.set(key, !open);
        render();
      }));
    }
    if (todoError) otherRoot.append(element("p", {
      className: "todo-dock-error",
      text: t("dock.todo.loadFailed", { error: errorMessage(todoStore.get().error) }),
    }));
    if (tasks.length) otherRoot.append(taskCard(tasks, onOpenTasks));
    for (const item of approvals) {
      const key = `approval/${selected.projectId}/${sessionId}/${item.id}`;
      nextDecisions.add(key);
      const card = approvalCard(item, argumentsOpen, onChanged);
      otherRoot.append(card);
      if (!visibleDecisions.has(key)) newApproval ??= card;
    }
    if (approvalsStore.get().status === "ready") {
      const live = new Set((approvalsStore.get().data?.items ?? [])
        .map((item) => String(item.id)));
      for (const key of argumentsOpen.keys()) if (!live.has(key)) argumentsOpen.delete(key);
    }
    if (focusedDock) {
      const replacement = [...otherRoot.querySelectorAll("[data-dock-focus]")]
        .find((node) => node.dataset.dockFocus === focusedDock);
      (replacement ?? document.querySelector("#prompt"))?.focus({ preventScroll: true });
    }
    const live = new Set();
    let newAsk = null;
    for (const item of asks) {
      const key = `${selected.projectId}/${sessionId}/${item.id}`;
      live.add(key);
      nextDecisions.add(`ask/${key}`);
      if (!askNodes.has(key)) {
        const card = askCard(item, selected.projectId, sessionId,
          askDeciding, askAnswered, drafts, onChanged, render);
        askNodes.set(key, card);
        askRoot.append(card.node);
      } else askNodes.get(key).sync();
      if (!visibleDecisions.has(`ask/${key}`)) newAsk ??= askNodes.get(key).node;
    }
    for (const [key, card] of askNodes) {
      if (live.has(key)) continue;
      card.node.remove();
      askNodes.delete(key);
    }
    if (askData?.loaded && askData.projectId === selected.projectId &&
        askData.sessionId === sessionId) {
      const prefix = `${selected.projectId}/${sessionId}/`;
      for (const key of drafts.keys())
        if (key.startsWith(prefix) && !live.has(key)) drafts.delete(key);
      for (const key of askAnswered)
        if (key.startsWith(prefix) && !live.has(key)) askAnswered.delete(key);
    }
    if (focusedAsk && !askRoot.contains(document.activeElement))
      document.querySelector("#prompt")?.focus({ preventScroll: true });
    container.hidden = !todoItems.length && !todoError && !tasks.length &&
      !approvals.length && !asks.length;
    composerRegion?.toggleAttribute("data-decision-pending",
      Boolean(approvals.length || asks.length));
    syncAvailableHeight();
    const arrived = newAsk ?? newApproval;
    if (arrived) {
      userMovedDock = false;
      container.scrollTop += arrived.getBoundingClientRect().top -
        container.getBoundingClientRect().top;
    } else container.scrollTop = previousScroll;
    visibleDecisions.clear();
    for (const key of nextDecisions) visibleDecisions.add(key);
    if (arrived) {
      onDecisionArrived?.(arrived, newAsk ? "ask" : "approval");
      scheduleReveal(arrived, true);
    }
  }

  const unsubscribers = [
    navigation.subscribe(render), tasksStore.subscribe(render),
    approvalsStore.subscribe(render), runsStore.subscribe(render),
    approvalDecisionStore.subscribe(render),
    asksStore.subscribe(render),
    todoStore.subscribe(render),
    subscribeLocale(render),
  ];
  const resizeObserver = conversation && typeof ResizeObserver === "function"
    ? new ResizeObserver((entries) => {
      syncAvailableHeight(true);
      if (entries.some((entry) => entry.target === container) &&
          !container.hidden && !userMovedDock) scheduleReveal();
    }) : null;
  resizeObserver?.observe(conversation);
  resizeObserver?.observe(container);
  const onResize = () => syncAvailableHeight(true);
  window.addEventListener("resize", onResize);
  return () => {
    cancelAnimationFrame(revealFrame);
    resizeObserver?.disconnect();
    window.removeEventListener("resize", onResize);
    container.removeEventListener("wheel", onDockWheel);
    container.removeEventListener("touchmove", noteUserScroll);
    container.removeEventListener("pointerdown", noteUserScroll);
    container.removeEventListener("keydown", noteKeyScroll);
    composerRegion?.removeAttribute("data-decision-pending");
    unsubscribers.forEach((unsubscribe) => unsubscribe());
  };
}
