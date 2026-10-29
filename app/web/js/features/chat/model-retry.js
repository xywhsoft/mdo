import { element } from "../../utils/dom.js";
import { t } from "../../i18n.js";

export function modelRetryCopy(item, now = Date.now()) {
  const status = Number(item.httpStatus || 0);
  const limited = item.errorKind === "rate_limited" || status === 429;
  const title = limited
    ? t("model.retry.rateLimited", {}, "模型服务暂时限流，正在重试…")
    : item.errorKind === "timeout"
      ? t("model.retry.timeout", {}, "模型请求超时，正在重试…")
      : item.errorKind === "network"
        ? t("model.retry.network", {}, "模型连接暂时中断，正在重试…")
        : t("model.retry.temporary", {}, "模型请求暂时失败，正在重试…");
  const seconds = Math.max(0, Math.ceil((item.retryAt - now) / 1000));
  const attempts = Math.max(0, Number(item.attempts) || 0);
  const detail = item.retryWaiting && seconds > 0
    ? t("model.retry.waiting", { attempts, seconds }, `已尝试 ${attempts} 次，${seconds} 秒后再次尝试`)
    : item.retryWaiting
      ? t("model.retry.scheduled", { attempts }, `已尝试 ${attempts} 次，即将再次尝试…`)
      : t("model.retry.connecting", { attempt: attempts + 1 }, `正在进行第 ${attempts + 1} 次尝试…`);
  return { title: status ? `${title} (${status})` : title, detail };
}

export function modelRetryNode(item) {
  const copy = modelRetryCopy(item);
  return element("li", { className: "timeline-item model-retry-notice", attrs: {
    "data-kind": "retry", "data-state": "running", "data-model-retry": "",
    "data-retry-at": String(item.retryAt), "data-retry-waiting": String(item.retryWaiting),
    "data-retry-attempts": String(item.attempts), role: "status", "aria-live": "polite",
  } }, [element("span", { className: "model-retry-dot", attrs: { "aria-hidden": "true" } }),
    element("div", {}, [element("div", { className: "model-retry-title", text: copy.title }),
      element("div", { className: "model-retry-detail", text: copy.detail,
        attrs: { "aria-live": "off" } })])]);
}

export function updateModelRetryCountdowns(container, now = Date.now()) {
  const notices = container.querySelectorAll("[data-model-retry]");
  for (const node of notices) {
    const copy = modelRetryCopy({ retryAt: Number(node.dataset.retryAt),
      retryWaiting: node.dataset.retryWaiting === "true", attempts: Number(node.dataset.retryAttempts) }, now);
    const detail = node.querySelector(".model-retry-detail");
    if (detail && detail.textContent !== copy.detail) detail.textContent = copy.detail;
  }
  return notices.length;
}
