import { element, clear, formatRelativeTime, errorMessage, toast } from "../../utils/dom.js";
import { cancelTask } from "../../state/tasks.js";

const ACTIVE_STATES = new Set(["pending", "running"]);

export function createTaskPanel({ container, summary, store, onChanged }) {
  async function cancel(item, button) {
    button.disabled = true;
    try {
      await cancelTask(item.id);
      toast(`已请求停止任务 #${item.id}`);
      onChanged?.();
    } catch (error) {
      button.disabled = false;
      toast(errorMessage(error), "error");
    }
  }

  function render(state) {
    const items = [...(state.data?.items ?? [])].reverse();
    const active = items.filter((item) => ACTIVE_STATES.has(item.state)).length;
    const failed = items.filter((item) => ["failed", "timed_out", "lost"].includes(item.state)).length;
    clear(summary);
    summary.append(
      element("span", { className: "summary-pill" }, [element("strong", { text: active }), document.createTextNode("活动")]),
      element("span", { className: "summary-pill" }, [element("strong", { text: items.length }), document.createTextNode("最近")]),
      element("span", { className: "summary-pill" }, [element("strong", { text: failed }), document.createTextNode("异常")]),
    );
    clear(container);
    if (state.status === "error") {
      container.append(element("div", { className: "resource-error", text: errorMessage(state.error) }));
      return;
    }
    if (!items.length) {
      container.append(element("div", { className: "empty-state", text: state.status === "loading" ? "正在载入任务…" : "暂无后台任务" }));
      return;
    }
    for (const item of items.slice(0, 30)) {
      const headerChildren = [
        element("span", { className: "status-dot", attrs: { "aria-hidden": "true" } }),
        element("span", { className: "task-name", text: item.label || `任务 #${item.id}` }),
        element("span", { className: "task-kind", text: item.kind }),
      ];
      if (ACTIVE_STATES.has(item.state)) {
        const cancelButton = element("button", { className: "task-cancel", text: "停止", attrs: { type: "button", "aria-label": `停止任务 ${item.label || item.id}` } });
        cancelButton.addEventListener("click", () => cancel(item, cancelButton));
        headerChildren.push(cancelButton);
      }
      container.append(element("article", { className: "task-card", attrs: { "data-state": item.state } }, [
        element("div", { className: "task-card-header" }, headerChildren),
        element("div", { className: "task-meta" }, [
          element("span", { text: item.state }),
          element("time", { className: "task-time", text: formatRelativeTime(item.started_at || item.created_at) }),
        ]),
      ]));
    }
  }

  return store.subscribe(render);
}
