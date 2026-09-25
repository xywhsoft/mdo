import { approvalDecisionStatus, approvalDecisionStore, decideApproval } from "../../state/approvals.js";
import { answerAsk } from "../../state/asks.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";

const STATE_TEXT = Object.freeze({ pending: "等待中", running: "进行中" });
const EFFECT_TEXT = Object.freeze({
  read: "读取", workspace_write: "修改工作区", process: "运行进程",
  network: "访问网络", external_service: "外部服务", secrets: "使用凭据",
  schedule: "计划任务", agent_delegation: "子 Agent",
});

function taskCard(tasks, onOpenTasks) {
  const list = element("ul", { className: "conversation-dock-list" });
  for (const task of tasks.slice(0, 8)) list.append(element("li", {}, [
    element("span", { className: "conversation-dock-state", text: STATE_TEXT[task.state] || task.state }),
    element("span", { text: task.label || `任务 #${task.id}` }),
  ]));
  const open = element("button", { text: "查看任务详情", attrs: {
    type: "button", "data-dock-focus": "tasks/open" } });
  open.addEventListener("click", onOpenTasks);
  return element("section", { className: "conversation-dock" }, [
    element("h3", { text: `后台任务 · ${tasks.length} 项` }), list,
    element("div", { className: "conversation-dock-actions" }, [open]),
  ]);
}

function todoCard(items, expanded, focusKey, onToggle) {
  const done = items.filter((item) => item.done).length;
  const toggle = element("button", {
    className: "todo-toggle", text: `计划 · ${done}/${items.length}`,
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
  const card = element("section", { className: "conversation-dock" });
  const resources = element("ul", { className: "conversation-dock-list" });
  for (const resource of item.resources ?? []) resources.append(element("li", {}, [
    element("span", { className: "conversation-dock-state", text: resource.kind || "资源" }),
    element("span", { text: resource.resource }),
  ]));
  const deny = element("button", { text: "拒绝", attrs: {
    type: "button", "data-dock-focus": `approval/${key}/deny` } });
  const allow = element("button", { text: "允许一次", attrs: {
    type: "button", "data-dock-focus": `approval/${key}/allow` } });
  for (const [button, decision] of [[deny, "deny"], [allow, "allow"]]) {
    button.setAttribute("aria-disabled", String(approvalDecisionStatus(key) !== "idle"));
    button.addEventListener("click", async () => {
      if (approvalDecisionStatus(key) !== "idle") return;
      try {
        if (!await decideApproval(item.id, decision)) return;
        await onChanged();
      } catch (error) {
        toast(approvalDecisionStatus(key) === "submitted"
          ? "决策已提交，但状态刷新未完成" : errorMessage(error), "error");
      }
    });
  }
  const argumentsView = element("details", { className: "approval-arguments",
    attrs: { "data-approval-arguments": key,
      open: argumentsOpen.get(key) ? "" : null } }, [
    element("summary", { text: "查看调用参数", attrs: {
      "data-dock-focus": `approval/${key}/arguments` } }),
    element("pre", { text: item.arguments_json || "{}" }),
  ]);
  argumentsView.addEventListener("toggle", () =>
    argumentsOpen.set(key, argumentsView.open));
  card.append(
    element("h3", { text: `${item.tool || "工具"} 请求权限` }),
    element("p", { text: `${item.risk || "未知风险"} · ${(item.effects ?? []).map((effect) => EFFECT_TEXT[effect] || effect).join("、") || "未声明影响"} · ${Math.ceil(Number(item.expires_in_ms || 0) / 1000)} 秒` }),
    resources,
    argumentsView,
    element("div", { className: "conversation-dock-actions" }, [deny, allow]),
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
    attrs: { type: "text", maxlength: "1024", placeholder: "也可以输入自己的回答",
      "aria-label": "回答问题", "aria-describedby": hint.id } });
  input.value = drafts.get(key) ?? "";
  const submit = element("button", { text: "提交回答",
    attrs: { type: "button" } });
  const buttons = [submit];
  const encoder = new TextEncoder();
  function updateValidity() {
    const answer = input.value.trim();
    const tooLong = encoder.encode(answer).length > 1024;
    const pending = deciding.has(key);
    const submitted = answered.has(key);
    input.setAttribute("aria-invalid", String(tooLong));
    input.readOnly = pending || submitted;
    hint.textContent = pending ? "正在提交回答…" :
      submitted ? "已提交，等待模型继续…" :
        tooLong ? "回答不能超过 1024 字节" : "";
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
      toast(answered.has(key) ? "回答已提交，但状态刷新未完成" :
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
  input.addEventListener("keydown", (event) => {
    if (event.key === "Enter" && !event.isComposing) {
      event.preventDefault();
      if (submit.getAttribute("aria-disabled") === "false") void respond(input.value);
    }
  });
  updateValidity();
  card.append(element("h3", { text: "需要你回答" }),
    element("p", { className: "ask-dock-question", text: item.question }),
    actions, element("div", { className: "ask-dock-free" }, [input, submit]),
    hint);
  return { node: card, sync: updateValidity };
}

export function createConversationDocks({ container, navigation, tasksStore, approvalsStore,
  asksStore, todoStore, runsStore, onOpenTasks, onChanged }) {
  const askDeciding = new Set();
  const askAnswered = new Set();
  const expanded = new Map();
  const argumentsOpen = new Map();
  const drafts = new Map();
  const askNodes = new Map();
  const otherRoot = element("div", { className: "conversation-dock-stack" });
  const askRoot = element("div", { className: "conversation-dock-stack" });
  container.append(otherRoot, askRoot);

  function render() {
    const focusedDock = otherRoot.contains(document.activeElement)
      ? document.activeElement?.dataset.dockFocus : "";
    const focusedAsk = askRoot.contains(document.activeElement);
    for (const details of otherRoot.querySelectorAll("details[data-approval-arguments]"))
      argumentsOpen.set(details.dataset.approvalArguments, details.open);
    const selected = navigation.get();
    const sessionId = selected.view === "workspace" ? selected.sessionId : "";
    const tasks = sessionId ? (tasksStore.get().data?.items ?? []).filter((item) =>
      item.owner_session === sessionId && !item.terminal) : [];
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
      text: `计划读取失败：${errorMessage(todoStore.get().error)}`,
    }));
    if (tasks.length) otherRoot.append(taskCard(tasks, onOpenTasks));
    for (const item of approvals) otherRoot.append(approvalCard(
      item, argumentsOpen, onChanged));
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
    for (const item of asks) {
      const key = `${selected.projectId}/${sessionId}/${item.id}`;
      live.add(key);
      if (!askNodes.has(key)) {
        const card = askCard(item, selected.projectId, sessionId,
          askDeciding, askAnswered, drafts, onChanged, render);
        askNodes.set(key, card);
        askRoot.append(card.node);
      } else askNodes.get(key).sync();
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
  }

  const unsubscribers = [
    navigation.subscribe(render), tasksStore.subscribe(render),
    approvalsStore.subscribe(render), runsStore.subscribe(render),
    approvalDecisionStore.subscribe(render),
    asksStore.subscribe(render),
    todoStore.subscribe(render),
  ];
  return () => unsubscribers.forEach((unsubscribe) => unsubscribe());
}
