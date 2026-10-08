import { element, errorMessage } from "../../utils/dom.js";
import { isTransientReadError } from "../../api/read-recovery.js";
import { t } from "../../i18n.js";

const FALLBACKS = Object.freeze({
  asks: "暂时无法读取对话询问，请检查连接后重试。",
  approvals: "暂时无法读取权限审核，请检查连接后重试。",
  runs: "暂时无法读取运行状态，请检查连接后重试。",
});

export function decisionReadError(error, kind) {
  return isTransientReadError(error) || ["asks_unavailable", "approvals_unavailable", "runs_unavailable"].includes(error?.code)
    ? t(`dock.read.${kind}`, {}, FALLBACKS[kind]) : errorMessage(error);
}

// One persistent surface for exhausted reads, separate from acknowledged
// answers/decisions. Routine retries never create toast errors or move focus.
export function createDecisionReadNotice() {
  let failures = [];
  const message = element("p", { className: "todo-dock-error", attrs: { "aria-live": "polite" } });
  const retry = element("button", { attrs: { type: "button", "data-dock-focus": "reads/retry" } });
  retry.addEventListener("click", () => void Promise.allSettled(failures.map(item => item.retry())));
  const node = element("section", { className: "conversation-dock decision-read-notice" }, [
    message, element("div", { className: "conversation-dock-actions" }, [retry]),
  ]);
  return { node, sync(next) {
    failures = next;
    message.textContent = [...new Set(next.map(item => decisionReadError(item.error, item.kind)))].join("\n");
    retry.textContent = t("task.detail.retry", {}, "重试读取");
  } };
}
