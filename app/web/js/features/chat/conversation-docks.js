import { approvalDecisionStatus, approvalDecisionStore, decideApproval } from "../../state/approvals.js";
import { answerAsk } from "../../state/asks.js";
import { subscribeLocale, t } from "../../i18n.js";
import { element, errorMessage, isImeKey, toast } from "../../utils/dom.js";
import { createCompositionTracker } from "../../utils/composition.js";
import { createAskCard } from "../asks/ask-card.js";
import { reconcileCards } from "../../utils/reconcile.js";
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

function taskCard(onOpenTasks) {
  const list = element("ul", { className: "conversation-dock-list" });
  const rows = new Map();
  const title = element("h3");
  const open = element("button", { text: t("dock.task.details"), attrs: {
    type: "button", "data-dock-focus": "tasks/open" } });
  open.addEventListener("click", onOpenTasks);
  const node = element("section", { className: "conversation-dock" }, [
    title, list,
    element("div", { className: "conversation-dock-actions" }, [open]),
  ]);
  return { node, sync(tasks) {
    const count = t("dock.task.count", { count: tasks.length });
    if (title.textContent !== count) title.textContent = count;
    const ordered = [];
    const live = new Set();
    for (const task of tasks.slice(0, 8)) {
      const key = String(task.id);
      live.add(key);
      let row = rows.get(key);
      if (!row) {
        const state = element("span", { className: "conversation-dock-state" });
        const label = element("span");
        row = { node: element("li", {}, [state, label]), state, label };
        rows.set(key, row);
      }
      const state = STATE_KEYS[task.state] && task.stop_requested
        ? t("task.stopping") : STATE_KEYS[task.state]
        ? Number(task.pending_questions) > 0 ? t("task.awaitingAnswer")
          : t(STATE_KEYS[task.state]) : task.state || "";
      const label = task.label || t("dock.task.fallback", { id: task.id });
      if (row.state.textContent !== state) row.state.textContent = state;
      if (row.label.textContent !== label) row.label.textContent = label;
      ordered.push(row.node);
    }
    for (const key of rows.keys()) if (!live.has(key)) rows.delete(key);
    reconcileCards(list, ordered);
  } };
}

function todoCard(focusKey, onToggle) {
  const toggle = element("button", {
    className: "todo-toggle", attrs: { type: "button",
      "data-dock-focus": focusKey },
  });
  toggle.addEventListener("click", onToggle);
  const list = element("ol", { className: "todo-dock-list" });
  const rows = new Map();
  const node = element("section", { className: "conversation-dock todo-dock" },
    [toggle, list]);
  return { node, sync(items, expanded) {
    const done = items.filter((item) => item.done).length;
    const count = t("dock.todo.count", { done, count: items.length });
    if (toggle.textContent !== count) toggle.textContent = count;
    toggle.setAttribute("aria-expanded", String(expanded));
    list.hidden = !expanded;
    const occurrences = new Map();
    const ordered = [];
    const live = new Set();
    const current = items.findIndex((item) => !item.done);
    for (const [index, item] of items.entries()) {
      // Todo snapshots have no item IDs. Match unchanged text by occurrence
      // so inserting an earlier item does not remount the one being read.
      const occurrence = occurrences.get(item.text) ?? 0;
      occurrences.set(item.text, occurrence + 1);
      const key = JSON.stringify([item.text, occurrence]);
      live.add(key);
      let row = rows.get(key);
      if (!row) {
        const mark = element("span", { className: "todo-dock-mark" });
        row = { node: element("li", {}, [mark,
          element("span", { text: item.text })]), mark };
        rows.set(key, row);
      }
      const className = item.done ? "done" : (index === current ? "current" : "");
      if (row.node.className !== className) row.node.className = className;
      const mark = item.done ? "✓" : "○";
      if (row.mark.textContent !== mark) row.mark.textContent = mark;
      ordered.push(row.node);
    }
    for (const key of rows.keys()) if (!live.has(key)) rows.delete(key);
    reconcileCards(list, ordered);
  } };
}

function approvalContentKey({ expires_in_ms, ...content }) {
  return JSON.stringify(content);
}

function approvalSummary(item) {
  return t("dock.approval.summary", {
    risk: RISK_KEYS[item.risk] ? t(RISK_KEYS[item.risk]) :
      item.risk || t("dock.approval.unknownRisk"),
    effects: (item.effects ?? []).map((effect) => EFFECT_KEYS[effect]
      ? t(EFFECT_KEYS[effect]) : effect).join(", ") || t("dock.approval.noEffects"),
    seconds: Math.ceil(Number(item.expires_in_ms || 0) / 1000),
  });
}

function decisionHeader(title, onExpand) {
  const expand = element("button", { className: "decision-dock-expand",
    text: t("dock.expandDecision"), attrs: { type: "button",
      "aria-expanded": "false", "aria-controls": "conversation-docks" } });
  if (title.dataset.dockFocus)
    expand.dataset.dockFocus = `${title.dataset.dockFocus}/expand`;
  expand.addEventListener("click", onExpand);
  return element("div", { className: "decision-dock-header" }, [title, expand]);
}

function approvalCard(item, argumentsOpen, onChanged, onExpand) {
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
  argumentsView.addEventListener("toggle", () => {
    if (argumentsView.isConnected) argumentsOpen.set(key, argumentsView.open);
  });
  const summary = element("p", { text: approvalSummary(item) });
  const title = element("h3", { text: t("dock.approval.title",
    { tool: item.tool || t("dock.approval.tool") }),
    attrs: { tabindex: "-1", "data-dock-focus": `approval/${key}/title` } });
  card.append(
    decisionHeader(title, () => onExpand(card)),
    summary,
    resources,
    argumentsView,
    element("div", { className: "conversation-dock-actions" },
      [deny, allow, allowRun]),
  );
  return { node: card, sync(next) {
    item = next;
    const nextSummary = approvalSummary(item);
    if (summary.textContent !== nextSummary) summary.textContent = nextSummary;
    const busy = String(approvalDecisionStatus(key) !== "idle");
    for (const button of [deny, allow, allowRun])
      button.setAttribute("aria-disabled", busy);
  } };
}

function askCard(item, projectId, sessionId, deciding, answered, drafts,
  onChanged, onSettled, onExpand) {
  return createAskCard({ item, key: `${projectId}/${sessionId}/${item.id}`,
    deciding, answered, drafts,
    onAnswer: (value) => answerAsk(projectId, sessionId, item.id, value),
    onChanged, onSettled,
    renderHeader: (title, card) => decisionHeader(title, () => onExpand(card)),
  });
}

export function createConversationDocks({ container, navigation, tasksStore, approvalsStore,
  asksStore, todoStore, runsStore, onOpenTasks, onChanged, onDecisionArrived }) {
  const composition = createCompositionTracker(container);
  const askDeciding = new Set();
  const askAnswered = new Set();
  const expanded = new Map();
  const argumentsOpen = new Map();
  const approvalCards = new Map();
  let todoView = null;
  let todoErrorView = null;
  let taskView = null;
  const drafts = new Map();
  const askNodes = new Map();
  const approvalRoot = element("div", { className: "conversation-dock-stack" });
  const askRoot = element("div", { className: "conversation-dock-stack" });
  const otherRoot = element("div", { className: "conversation-dock-stack" });
  container.append(approvalRoot, askRoot, otherRoot);
  const composerRegion = container.parentElement?.classList.contains("composer-region")
    ? container.parentElement : null;
  const conversation = container.closest(".workspace")?.querySelector(".conversation");
  const visibleDecisions = new Set();
  let availableHeight = 0;
  let overlapHeight = 0;
  let revealFrame = 0;
  let userMovedDock = false;
  let keyboardExpansionDismissed = false;
  let searching = false;
  function syncHistoryOverlap() {
    if (!conversation || !composerRegion) return;
    const history = conversation.getBoundingClientRect();
    const dock = container.getBoundingClientRect();
    const next = container.hidden || dock.bottom <= history.top ? 0 : Math.max(0,
      Math.ceil(history.bottom - Math.max(history.top, dock.top)));
    if (next === overlapHeight) return;
    const followTail = conversation.scrollHeight - conversation.scrollTop -
      conversation.clientHeight <= 1;
    overlapHeight = next;
    // The dock floats over history. Reserve its actual overlap at the end of
    // the scrollable content so the final message and queue actions can clear
    // it; scroll-padding also keeps keyboard focus above the covered area.
    conversation.style.setProperty("--conversation-dock-overlap", `${next}px`);
    if (followTail) conversation.scrollTop = conversation.scrollHeight;
  }
  function setDecisionExpanded(expanded) {
    if (!composerRegion) return;
    composerRegion.toggleAttribute("data-decision-expanded", expanded);
    for (const button of container.querySelectorAll(".decision-dock-expand")) {
      const label = t(expanded ? "dock.collapseDecision" :
        "dock.expandDecision");
      if (button.textContent !== label) button.textContent = label;
      if (button.getAttribute("aria-expanded") !== String(expanded))
        button.setAttribute("aria-expanded", String(expanded));
    }
    syncHistoryOverlap();
  }
  function toggleDecisionExpanded(card) {
    const expanded = !composerRegion?.hasAttribute("data-decision-expanded");
    keyboardExpansionDismissed = !expanded;
    setDecisionExpanded(expanded);
    if (expanded) {
      container.scrollTop += card.getBoundingClientRect().top -
        container.getBoundingClientRect().top;
      scheduleReveal(card, true);
    }
  }
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
  const collapseOnEscape = (event) => {
    if (event.key !== "Escape" || event.defaultPrevented ||
        isImeKey(event, composition.isComposing(event.target)) ||
        !composerRegion?.hasAttribute("data-decision-expanded")) return;
    event.preventDefault();
    keyboardExpansionDismissed = true;
    setDecisionExpanded(false);
  };
  container.addEventListener("keydown", collapseOnEscape);

  function revealDecision(arrived = null, force = false) {
    if (container.hidden) return;
    if (userMovedDock && !force) return;
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
    const decision = arrived ?? approvalRoot.querySelector("[data-approval-id]") ??
      askRoot.querySelector(".ask-dock");
    if (!decision) return;
    const viewport = container.getBoundingClientRect();
    const title = decision.querySelector("h3");
    const approvalAction = decision.querySelector(".conversation-dock-actions button");
    // On a very short screen, the title and even one action cannot share the
    // dock. Start at the decision context instead of its middle arguments row.
    const crampedApproval = approvalAction && title &&
      viewport.height < title.getBoundingClientRect().height +
        approvalAction.getBoundingClientRect().height + 24;
    const askOption = decision.querySelector(".ask-dock-options button");
    // If a long question and its first answer cannot fit together, show the
    // beginning of the question first. Otherwise scrolling to the answer
    // hides the context needed to choose it.
    const crampedAsk = askOption && title &&
      askOption.getBoundingClientRect().bottom -
        title.getBoundingClientRect().top + 4 > viewport.height;
    const content = (crampedApproval || crampedAsk ? title : null) ??
      askOption ??
      decision.querySelector(".approval-arguments summary") ?? title;
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
    const cramped = composerRegion?.hasAttribute("data-decision-pending") &&
      nextHeight < 120 && container.scrollHeight > nextHeight + 4;
    const hiddenFocus = !cramped &&
      document.activeElement?.matches?.(".decision-dock-expand")
      ? document.activeElement.closest(".conversation-dock")?.querySelector("h3")
      : null;
    composerRegion?.toggleAttribute("data-decision-cramped", Boolean(cramped));
    const keyboardAsk = cramped &&
      composerRegion?.closest(".app-shell")?.hasAttribute("data-compact-visual-viewport") &&
      document.activeElement?.matches?.(".ask-dock-input") &&
      container.contains(document.activeElement);
    if (!keyboardAsk) keyboardExpansionDismissed = false;
    // Keep the question and expansion control reachable while the software
    // keyboard leaves only enough room for the active answer field.
    if (keyboardAsk && !keyboardExpansionDismissed &&
        !composerRegion.hasAttribute("data-decision-expanded"))
      setDecisionExpanded(true);
    // A restored viewport has room for the dock again. Do not keep the
    // temporary full-screen decision layer or focus its now-hidden toggle.
    if (!cramped && composerRegion?.hasAttribute("data-decision-expanded"))
      setDecisionExpanded(false);
    hiddenFocus?.focus({ preventScroll: true });
    syncHistoryOverlap();
    if (revealOnResize && changed && !container.hidden) scheduleReveal();
  }
  syncAvailableHeight();

  function render() {
    const previousScroll = container.scrollTop;
    const focusedDock = (approvalRoot.contains(document.activeElement) ||
      otherRoot.contains(document.activeElement))
      ? document.activeElement?.dataset.dockFocus : "";
    const focusedAsk = askRoot.contains(document.activeElement);
    const editingAsk = focusedAsk &&
      document.activeElement.matches(".ask-dock-input");
    const editor = editingAsk ? document.activeElement : null;
    const editorTop = editor ? editor.getBoundingClientRect().top -
      container.getBoundingClientRect().top : 0;
    for (const details of approvalRoot.querySelectorAll("details[data-approval-arguments]"))
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
    const otherNodes = [];
    let newApproval = null;
    const nextDecisions = new Set();
    if (todoItems.length) {
      const key = `${selected.projectId}/${sessionId}`;
      const open = expanded.get(key) !== false;
      if (!todoView || todoView.key !== key)
        todoView = { key, ...todoCard(`todo/${key}`, () => {
          expanded.set(key, expanded.get(key) === false);
          render();
        }) };
      todoView.sync(todoItems, open);
      otherNodes.push(todoView.node);
    } else todoView = null;
    if (todoError) {
      const message = t("dock.todo.loadFailed", { error: errorMessage(todoStore.get().error) });
      const contentKey = JSON.stringify([selected.projectId, sessionId, message]);
      if (!todoErrorView || todoErrorView.contentKey !== contentKey)
        todoErrorView = { contentKey, node: element("p", {
          className: "todo-dock-error", text: message,
        }) };
      otherNodes.push(todoErrorView.node);
    } else todoErrorView = null;
    if (tasks.length) {
      const key = `${selected.projectId}/${sessionId}`;
      if (!taskView || taskView.key !== key)
        taskView = { key, ...taskCard(onOpenTasks) };
      taskView.sync(tasks);
      otherNodes.push(taskView.node);
    } else taskView = null;
    reconcileCards(otherRoot, otherNodes);
    const approvalNodes = [];
    const liveApprovals = new Set();
    for (const item of approvals) {
      const key = `approval/${selected.projectId}/${sessionId}/${item.id}`;
      liveApprovals.add(key);
      nextDecisions.add(key);
      const contentKey = approvalContentKey(item);
      let cached = approvalCards.get(key);
      if (!cached || cached.contentKey !== contentKey) {
        cached = { ...approvalCard(item, argumentsOpen, onChanged,
          toggleDecisionExpanded), contentKey };
        approvalCards.set(key, cached);
      }
      cached.sync(item);
      approvalNodes.push(cached.node);
      if (!visibleDecisions.has(key)) newApproval ??= cached.node;
    }
    for (const key of approvalCards.keys())
      if (!liveApprovals.has(key)) approvalCards.delete(key);
    reconcileCards(approvalRoot, approvalNodes);
    if (approvalsStore.get().status === "ready") {
      const live = new Set((approvalsStore.get().data?.items ?? [])
        .map((item) => String(item.id)));
      for (const key of argumentsOpen.keys()) if (!live.has(key)) argumentsOpen.delete(key);
    }
    if (focusedDock && !container.contains(document.activeElement)) {
      const replacement = [...container.querySelectorAll("[data-dock-focus]")]
        .find((node) => node.dataset.dockFocus === focusedDock);
      (replacement ?? document.querySelector("#prompt"))?.focus({ preventScroll: true });
    }
    const live = new Set();
    const askCardsInOrder = [];
    let newAsk = null;
    for (const item of asks) {
      const key = `${selected.projectId}/${sessionId}/${item.id}`;
      live.add(key);
      nextDecisions.add(`ask/${key}`);
      if (!askNodes.has(key)) {
        const card = askCard(item, selected.projectId, sessionId,
          askDeciding, askAnswered, drafts, onChanged, render,
          toggleDecisionExpanded);
        askNodes.set(key, card);
      } else askNodes.get(key).sync();
      askCardsInOrder.push(askNodes.get(key).node);
      if (!visibleDecisions.has(`ask/${key}`)) newAsk ??= askNodes.get(key).node;
    }
    for (const key of askNodes.keys()) {
      if (live.has(key)) continue;
      askNodes.delete(key);
    }
    reconcileCards(askRoot, askCardsInOrder);
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
    container.hidden = searching || (!todoItems.length && !todoError && !tasks.length &&
      !approvals.length && !asks.length);
    composerRegion?.toggleAttribute("data-decision-pending",
      Boolean(approvals.length || asks.length));
    setDecisionExpanded(Boolean(approvals.length || asks.length) &&
      composerRegion?.hasAttribute("data-decision-expanded"));
    syncAvailableHeight();
    const arrived = newApproval ?? newAsk;
    const keepEditor = editor?.isConnected;
    if (keepEditor) {
      // An approval is inserted before asks. Anchor the active answer field
      // rather than allowing the new card to push it out of the short dock.
      container.scrollTop += editor.getBoundingClientRect().top -
        container.getBoundingClientRect().top - editorTop;
    } else if (arrived) {
      userMovedDock = false;
      container.scrollTop += arrived.getBoundingClientRect().top -
        container.getBoundingClientRect().top;
      // A newly arrived decision takes priority over a focused, non-editing
      // status card. Do this before ResizeObserver can reschedule reveal.
      if (document.activeElement?.matches?.(".todo-toggle") ||
          document.activeElement?.dataset?.dockFocus === "tasks/open")
        arrived.querySelector("h3")?.focus({ preventScroll: true });
    } else container.scrollTop = previousScroll;
    visibleDecisions.clear();
    for (const key of nextDecisions) visibleDecisions.add(key);
    if (arrived && !searching) {
      onDecisionArrived?.(arrived, newApproval ? "approval" : "ask");
      if (!keepEditor) scheduleReveal(arrived, true);
    }
  }

  const unsubscribers = [
    navigation.subscribe(render), tasksStore.subscribe(render),
    approvalsStore.subscribe(render), runsStore.subscribe(render),
    approvalDecisionStore.subscribe(render),
    asksStore.subscribe(render),
    todoStore.subscribe(render),
    subscribeLocale(() => {
      approvalCards.clear();
      todoView = null;
      todoErrorView = null;
      taskView = null;
      render();
    }),
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
  function destroy() {
    cancelAnimationFrame(revealFrame);
    resizeObserver?.disconnect();
    window.removeEventListener("resize", onResize);
    container.removeEventListener("wheel", onDockWheel);
    container.removeEventListener("touchmove", noteUserScroll);
    container.removeEventListener("pointerdown", noteUserScroll);
    container.removeEventListener("keydown", noteKeyScroll);
    container.removeEventListener("keydown", collapseOnEscape);
    composition.dispose();
    conversation?.style.removeProperty("--conversation-dock-overlap");
    composerRegion?.removeAttribute("data-decision-pending");
    composerRegion?.removeAttribute("data-decision-cramped");
    composerRegion?.removeAttribute("data-decision-expanded");
    unsubscribers.forEach((unsubscribe) => unsubscribe());
  }
  return Object.freeze({
    setSearchActive(value) {
      const next = Boolean(value);
      if (next === searching) return;
      searching = next;
      if (next) {
        // Search needs the history beneath the floating cards. Keep their
        // state and answer drafts, but dismiss the expanded decision layer.
        cancelAnimationFrame(revealFrame);
        revealFrame = 0;
        keyboardExpansionDismissed = true;
        setDecisionExpanded(false);
      }
      render();
    },
    destroy,
  });
}
