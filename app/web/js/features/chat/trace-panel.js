import { element, errorMessage, formatClock } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";

const MAX_VISIBLE_EVENTS = 200;
const MAX_PREVIEW_LENGTH = 160;

function preview(event) {
  const text = String(event.text || "").replace(/\s+/g, " ").trim();
  const fallback = event.tool_name || event.model || event.task_id || "";
  const value = text || String(fallback);
  return value.length > MAX_PREVIEW_LENGTH
    ? `${value.slice(0, MAX_PREVIEW_LENGTH - 1)}…` : value;
}

function eventRow(event) {
  const id = String(event.event_id);
  const time = element("time", { className: "trace-time", text: formatClock(event.time) });
  const label = element("code", { className: "trace-kind", text: event.kind || "event" });
  const description = element("span", { className: "trace-preview", text: preview(event) });
  const details = element("details", { className: "trace-event", attrs: { "data-event-id": id } });
  details.append(element("summary", {}, [
    time, label, element("span", { className: "trace-id", text: `#${id}` }),
    description,
  ]));
  details.addEventListener("toggle", () => {
    if (details.open && !details.querySelector("pre"))
      details.append(element("pre", { text: JSON.stringify(event, null, 2) }));
  });
  return { details, time };
}

export function createTracePanel({ panel, list, summary, store, navigation }) {
  let sessionKey = "";
  const rows = new Map();

  function render() {
    if (panel.hidden) return;
    const route = navigation.get();
    const key = route.view === "workspace" && route.sessionId
      ? `${route.projectId}/${route.sessionId}` : "";
    if (key !== sessionKey) {
      sessionKey = key;
      rows.clear();
      list.replaceChildren();
      panel.scrollTop = 0;
    }
    if (!key) {
      summary.textContent = t("trace.noSession", {}, "选择会话后查看事件轨迹。");
      return;
    }

    const state = store.get();
    const data = state.data;
    if (data?.projectId !== route.projectId || data?.sessionId !== route.sessionId) {
      summary.textContent = t("trace.loading", {}, "正在读取会话事件…");
      return;
    }
    const events = data.events.slice(-MAX_VISIBLE_EVENTS).reverse();
    const prefix = events.length
      ? t("trace.count", { count: events.length }, `最近 ${events.length} 条事件，倒序`)
      : t("trace.empty", {}, "暂无事件");
    const gap = data.historyLost ? ` · ${t("trace.historyLost", {}, "更早的事件已不在当前记录中")}` : "";
    const failure = state.status === "error"
      ? ` · ${t("trace.loadFailed", { error: errorMessage(state.error) },
        `刷新失败：${errorMessage(state.error)}`)}` : "";
    summary.textContent = `${prefix}${gap}${failure}`;

    const oldHeight = panel.scrollHeight;
    const oldTop = panel.scrollTop;
    const live = new Set();
    for (const [index, event] of events.entries()) {
      const id = String(event.event_id);
      live.add(id);
      let row = rows.get(id);
      if (!row) {
        row = eventRow(event);
        rows.set(id, row);
      } else row.time.textContent = formatClock(event.time);
      if (list.children[index] !== row.details)
        list.insertBefore(row.details, list.children[index] ?? null);
    }
    for (const [id, row] of rows) {
      if (live.has(id)) continue;
      row.details.remove();
      rows.delete(id);
    }
    if (oldTop > 8) panel.scrollTop = oldTop + panel.scrollHeight - oldHeight;
  }

  store.subscribe(render);
  navigation.subscribe(render);
  subscribeLocale(render);
  return { render };
}
