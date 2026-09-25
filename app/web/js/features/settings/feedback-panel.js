import { api } from "../../api/client.js";
import { clear, element, errorMessage } from "../../utils/dom.js";
import { currentLocale, subscribeLocale, t } from "../../i18n.js";

function feedbackCard(item, navigation) {
  const title = item.title || item.session_id;
  const time = Number(item.occurred_at) > 0
    ? new Date(Number(item.occurred_at) / 1000).toLocaleString(currentLocale())
    : t("feedback.invalidTime", {}, "反馈时间不可用");
  const open = element("button", {
    className: "secondary-button", text: t("feedback.open", {}, "打开会话"),
    attrs: { type: "button" },
  });
  open.addEventListener("click", () =>
    navigation.select(item.project_id, item.session_id));
  return element("article", { className: "resource-card feedback-card" }, [
    element("div", { className: "feedback-card-heading" }, [
      element("h3", { text: title }),
      element("span", { className: `feedback-vote ${item.value}`,
        text: item.value === "good" ? t("feedback.good", {}, "赞") :
          t("feedback.bad", {}, "踩") }),
    ]),
    element("p", { text: t("feedback.cardDetails", {
      time, project: item.project_id, id: item.event_id,
    }, `${time} · ${item.project_id} · 消息 #${item.event_id}`) }),
    open,
  ]);
}

export function createFeedbackPanel({ panel, navigation }) {
  const status = panel.querySelector("#feedback-list-status");
  const list = panel.querySelector("#feedback-list");
  const refreshButton = panel.querySelector("#feedback-refresh");
  const moreButton = panel.querySelector("#feedback-more");
  let items = [];
  let cursor = "";
  let scanned = 0;
  let diagnostics = 0;
  let loaded = false;
  let busy = false;
  let failure = "";
  let failureKey = "";

  function render() {
    clear(list);
    for (const item of items) list.append(feedbackCard(item, navigation));
    if (!items.length && loaded)
      list.append(element("p", { className: "empty-state",
        text: cursor ? t("feedback.pageEmpty", {}, "本页未找到反馈，可以继续加载。") :
          t("feedback.empty", {}, "还没有点赞或点踩记录。") }));
    const diagnosticText = diagnostics ? t("feedback.diagnostics", { count: diagnostics },
      `；${diagnostics} 项会话目录诊断需在“诊断与存储”中核对`) : "";
    status.textContent = failureKey ? t(failureKey, {}, failure) : failure || (loaded
      ? t("feedback.summary", { count: items.length, scanned, diagnostics: diagnosticText },
        `已载入 ${items.length} 条反馈，已检查 ${scanned} 个会话${diagnosticText}。`)
      : t("feedback.loading", {}, "正在读取反馈…"));
    refreshButton.disabled = busy;
    moreButton.hidden = !cursor;
    moreButton.disabled = busy;
  }

  async function loadPage(reset) {
    if (busy) return;
    busy = true;
    failure = "";
    failureKey = "";
    if (reset) {
      items = [];
      cursor = "";
      scanned = 0;
      diagnostics = 0;
      loaded = false;
    }
    render();
    try {
      const path = cursor ? `/feedback?cursor=${encodeURIComponent(cursor)}`
        : "/feedback";
      const response = await api.get(path);
      const data = response.data;
      items.push(...(data.items ?? []));
      items.sort((left, right) =>
        Number(right.occurred_at || 0) - Number(left.occurred_at || 0));
      cursor = data.next_cursor || "";
      scanned += Number(data.scanned_sessions || 0);
      diagnostics = Number(data.catalog_diagnostics || 0);
      loaded = true;
    } catch (error) {
      failureKey = error?.code === "feedback_cursor_stale" ? "feedback.cursorStale" : "";
      failure = failureKey ? "会话列表已变化，请刷新反馈记录。" : errorMessage(error);
    } finally {
      busy = false;
      render();
    }
  }

  refreshButton.addEventListener("click", () => void loadPage(true));
  moreButton.addEventListener("click", () => void loadPage(false));
  subscribeLocale(render);
  return Object.freeze({ refresh: () => loadPage(true) });
}
