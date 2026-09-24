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
  runsStore, onOpenTasks, onChanged }) {
  const deciding = new Set();

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
    clear(container);
    if (tasks.length) container.append(taskCard(tasks, onOpenTasks));
    for (const item of approvals) container.append(approvalCard(item, deciding, onChanged));
    container.hidden = !tasks.length && !approvals.length;
  }

  const unsubscribers = [
    navigation.subscribe(render), tasksStore.subscribe(render),
    approvalsStore.subscribe(render), runsStore.subscribe(render),
  ];
  return () => unsubscribers.forEach((unsubscribe) => unsubscribe());
}
