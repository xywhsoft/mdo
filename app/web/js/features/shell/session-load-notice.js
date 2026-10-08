import { subscribeLocale, t } from "../../i18n.js";

// A direct session URL may finish shell startup before its detail request.
// Keep that destination visible and recoverable while the request is pending.
export function createSessionLoadNotice({ navigation, store, conversation,
  notice, heading, description, retry, sessionTitle, sessionSubtitle,
  mobileTitle, mobileMeta, prompt, timeline = null, onRetryHistory = null, timeoutMs = 8_000 }) {
  let activeKey = "";
  let delayed = false;
  let timer = 0;
  let restoreFocus = false;
  let retrySawLoading = false;

  function clearTimer() {
    if (timer) window.clearTimeout(timer);
    timer = 0;
  }

  function selected() {
    const route = navigation.get();
    return route.view === "workspace" && route.projectId && route.sessionId
      ? `${route.projectId}/${route.sessionId}` : "";
  }

  function render() {
    const key = selected();
    if (key !== activeKey) {
      activeKey = key;
      delayed = false;
      restoreFocus = false;
      retrySawLoading = false;
      clearTimer();
    }
    const state = store.get();
    const route = navigation.get();
    const hasDetail = key && state.data?.project_id === route.projectId &&
      state.data?.id === route.sessionId;
    const history = timeline?.get();
    const matching = history?.data?.projectId === route.projectId && history?.data?.sessionId === route.sessionId;
    const pendingHistory = timeline && (!matching || history.data.initializing || history.data.syncing);
    const historyError = matching && (history.data.syncError ||
      (history.status === "error" ? history.error : null));
    if (!key || (hasDetail && !pendingHistory && !historyError)) {
      clearTimer();
      notice.hidden = true;
      delete conversation.dataset.sessionLoad;
      if (hasDetail && restoreFocus && !prompt.disabled) prompt.focus();
      restoreFocus = false;
      return;
    }

    const failed = state.status === "error" || Boolean(historyError);
    if (!failed && restoreFocus) retrySawLoading = true;
    if (failed) clearTimer();
    else if (!timer && !delayed) timer = window.setTimeout(() => {
      timer = 0;
      delayed = true;
      render();
    }, timeoutMs);
    const phase = failed ? "error" : delayed ? "delayed" : "loading";
    const title = hasDetail ? failed
      ? t("sessionLoad.syncFailed", {}, "同步暂未完成")
      : t("sessionLoad.syncing", {}, "正在同步对话…") : failed
      ? t("sessionLoad.failedTitle", {}, "任务暂时无法载入")
      : t("sessionLoad.loadingTitle", {}, "正在载入任务…");
    const body = hasDetail ? history?.data?.events?.length
      ? t("sessionLoad.cached", {}, "已显示保留的对话，正在检查最新内容。")
      : t("sessionLoad.history", {}, "正在读取最近的对话，较早历史将在向上滚动时加载。") : failed
      ? t("sessionLoad.failedDescription", {}, "请重试读取当前任务；会话记录和草稿不会被删除。")
      : delayed
        ? t("sessionLoad.delayedDescription", {}, "读取时间较长，可以重试当前任务。")
        : t("sessionLoad.loadingDescription", {}, "正在读取会话记录。");
    conversation.dataset.sessionLoad = phase;
    notice.dataset.state = phase;
    notice.setAttribute("role", failed ? "alert" : "status");
    notice.hidden = false;
    notice.dataset.compact = String(Boolean(hasDetail));
    heading.textContent = title;
    description.textContent = body;
    const retrying = restoreFocus && !failed && !delayed;
    retry.textContent = retrying
      ? t("sessionLoad.retrying", {}, "正在重试…")
      : t("sessionLoad.retry", {}, "重试读取");
    retry.hidden = !failed && !delayed && !retrying;
    retry.setAttribute("aria-disabled", retrying ? "true" : "false");
    if (failed && restoreFocus && retrySawLoading) {
      retry.focus();
      restoreFocus = false;
      retrySawLoading = false;
    }
    if (!hasDetail) {
      sessionTitle.textContent = title;
      sessionSubtitle.textContent = body;
      mobileTitle.textContent = title;
      mobileMeta.textContent = "";
    }
  }

  retry.addEventListener("click", () => {
    if (!selected() || (restoreFocus && !delayed)) return;
    restoreFocus = true;
    retrySawLoading = false;
    delayed = false;
    clearTimer();
    const route = navigation.get(), data = store.get().data;
    if (onRetryHistory && data?.project_id === route.projectId && data?.id === route.sessionId)
      void onRetryHistory();
    else navigation.revalidate();
    render();
  });
  navigation.subscribe(render);
  store.subscribe(render);
  timeline?.subscribe(render);
  subscribeLocale(render);
  return { render };
}
