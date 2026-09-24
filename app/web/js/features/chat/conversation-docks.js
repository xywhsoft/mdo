import { decideApproval } from "../../state/approvals.js";
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
  const open = element("button", { text: "查看任务详情", attrs: { type: "button" } });
  open.addEventListener("click", onOpenTasks);
  return element("section", { className: "conversation-dock" }, [
    element("h3", { text: `后台任务 · ${tasks.length} 项` }), list,
    element("div", { className: "conversation-dock-actions" }, [open]),
  ]);
}

function todoCard(items, expanded, onToggle) {
  const done = items.filter((item) => item.done).length;
  const toggle = element("button", {
    className: "todo-toggle", text: `计划 · ${done}/${items.length}`,
    attrs: { type: "button", "aria-expanded": String(expanded) },
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

function approvalCard(item, deciding, onChanged) {
  const card = element("section", { className: "conversation-dock" });
  const resources = element("ul", { className: "conversation-dock-list" });
  for (const resource of item.resources ?? []) resources.append(element("li", {}, [
    element("span", { className: "conversation-dock-state", text: resource.kind || "资源" }),
    element("span", { text: resource.resource }),
  ]));
  const deny = element("button", { text: "拒绝", attrs: { type: "button" } });
  const allow = element("button", { text: "允许一次", attrs: { type: "button" } });
  for (const [button, decision] of [[deny, "deny"], [allow, "allow"]]) {
    button.disabled = deciding.has(String(item.id));
    button.addEventListener("click", async () => {
      const key = String(item.id);
      if (deciding.has(key)) return;
      deciding.add(key);
      deny.disabled = allow.disabled = true;
      try {
        await decideApproval(item.id, decision);
        await onChanged();
      } catch (error) {
        toast(errorMessage(error), "error");
        deny.disabled = allow.disabled = false;
      } finally { deciding.delete(key); }
    });
  }
  const argumentsView = element("details", { className: "approval-arguments" }, [
    element("summary", { text: "查看调用参数" }),
    element("pre", { text: item.arguments_json || "{}" }),
  ]);
  card.append(
    element("h3", { text: `${item.tool || "工具"} 请求权限` }),
    element("p", { text: `${item.risk || "未知风险"} · ${(item.effects ?? []).map((effect) => EFFECT_TEXT[effect] || effect).join("、") || "未声明影响"} · ${Math.ceil(Number(item.expires_in_ms || 0) / 1000)} 秒` }),
    resources,
    argumentsView,
    element("div", { className: "conversation-dock-actions" }, [deny, allow]),
  );
  return card;
}

export function createConversationDocks({ container, navigation, tasksStore, approvalsStore,
  todoStore, runsStore, onOpenTasks, onChanged }) {
  const deciding = new Set();
  const expanded = new Map();

  function render() {
    const selected = navigation.get();
    const sessionId = selected.view === "workspace" ? selected.sessionId : "";
    const tasks = sessionId ? (tasksStore.get().data?.items ?? []).filter((item) =>
      item.owner_session === sessionId && !item.terminal) : [];
    const runIds = new Set((runsStore.get().data?.items ?? [])
      .filter((run) => run.session_id === sessionId && run.project_id === selected.projectId)
      .map((run) => String(run.agent_run_id)));
    const approvals = sessionId ? (approvalsStore.get().data?.items ?? []).filter((item) =>
      runIds.has(String(item.run_id))) : [];
    const todo = todoStore.get().data;
    const todoError = todoStore.get().status === "error" &&
      todo?.projectId === selected.projectId && todo?.sessionId === sessionId;
    const todoItems = todo?.projectId === selected.projectId &&
      todo?.sessionId === sessionId ? todo.items : [];
    clear(container);
    if (todoItems.length) {
      const key = `${selected.projectId}/${sessionId}`;
      const open = expanded.get(key) !== false;
      container.append(todoCard(todoItems, open, () => {
        expanded.set(key, !open);
        render();
      }));
    }
    if (todoError) container.append(element("p", {
      className: "todo-dock-error",
      text: `计划读取失败：${errorMessage(todoStore.get().error)}`,
    }));
    if (tasks.length) container.append(taskCard(tasks, onOpenTasks));
    for (const item of approvals) container.append(approvalCard(item, deciding, onChanged));
    container.hidden = !todoItems.length && !todoError && !tasks.length &&
      !approvals.length;
  }

  const unsubscribers = [
    navigation.subscribe(render), tasksStore.subscribe(render),
    approvalsStore.subscribe(render), runsStore.subscribe(render),
    todoStore.subscribe(render),
  ];
  return () => unsubscribers.forEach((unsubscribe) => unsubscribe());
}
