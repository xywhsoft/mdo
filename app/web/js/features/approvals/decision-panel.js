import { approvalDecisionStatus, approvalDecisionStore, decideApproval } from "../../state/approvals.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";
import { effectList, formatArguments, resourceKindLabel, resourceText, riskLabel } from "./labels.js";

function contentKey({ expires_in_ms, ...content }) {
  return JSON.stringify(content);
}

export function createDecisionPanel({ container, summary, store, onChanged }) {
  let state = store.get();
  const argumentsOpen = new Map();
  const cards = new Map();
  const seen = new Set();

  async function decide(item, decision) {
    const key = String(item.id);
    if (approvalDecisionStatus(key) !== "idle") return;
    try {
      if (!await decideApproval(item.id, decision)) return;
      const tool = item.tool || t("decision.tool", {}, "工具");
      toast(decision === "allow_run"
        ? t("decision.allowedRun", {}, "本轮运行的工具请求均已允许")
        : decision === "allow"
          ? t("decision.allowed", { tool }, `已允许 ${tool} 本次执行`)
          : t("decision.denied", { tool }, `已拒绝 ${tool} 本次执行`));
      await onChanged?.();
    } catch (error) {
      toast(approvalDecisionStatus(key) === "submitted"
        ? t("decision.submittedRefreshFailed", {}, "决策已提交，但状态刷新未完成")
        : errorMessage(error), "error");
    }
  }

  function renderCard(item) {
    const key = String(item.id);
    const card = element("article", { className: "approval-card", attrs: {
      "data-risk": item.risk, "data-approval-id": key } });
    const allow = element("button", { className: "primary-button approval-allow",
      text: t("decision.allowOnce", {}, "允许一次"), attrs: {
      type: "button", "data-decision-focus": `${key}/allow` } });
    const allowRun = element("button", { className: "secondary-button approval-allow-run",
      text: t("decision.allowRun", {}, "本轮均允许"), attrs: {
      type: "button", "data-decision-focus": `${key}/allow_run` } });
    const deny = element("button", { className: "secondary-button approval-deny",
      text: t("decision.deny", {}, "拒绝"), attrs: {
      type: "button", "data-decision-focus": `${key}/deny` } });
    const resources = element("ul", { className: "approval-resources" });
    for (const resource of item.resources ?? []) {
      resources.append(element("li", {}, [
        element("span", { className: "approval-resource-kind",
          text: resourceKindLabel(resource.kind) }),
        element("span", { text: resourceText(resource) }),
      ]));
    }
    if (!resources.children.length) resources.append(element("li", {
      text: t("decision.noResources", {}, "未声明具体资源") }));
    const timeout = element("span", { className: "approval-timeout" });
    card.append(
      element("header", { className: "approval-heading" }, [
        element("div", {}, [
          element("h3", { text: item.tool || t("decision.unknownTool", {}, "未知工具"),
            attrs: { tabindex: "-1", "data-decision-focus": `${key}/title` } }),
          element("p", { text: `${riskLabel(item.risk)} · ${effectList(item.effects ?? []) ||
            t("decision.noEffects", {}, "未声明影响")}` }),
        ]),
        timeout,
      ]),
      element("p", { className: "approval-workspace", text: item.workspace_root ||
        t("decision.defaultWorkspace", {}, "默认工作区") }),
      resources,
      element("details", { className: "approval-arguments", attrs: {
        "data-approval-arguments": key, open: argumentsOpen.get(key) ? "" : null } }, [
        element("summary", { text: t("decision.viewArguments", {}, "查看调用参数"), attrs: {
          "data-decision-focus": `${key}/arguments` } }),
        element("pre", { text: formatArguments(item.arguments_json) }),
      ]),
      element("div", { className: "approval-actions" }, [deny, allow, allowRun]),
    );
    const details = card.querySelector("details");
    details.addEventListener("toggle", () => {
      if (details.isConnected) argumentsOpen.set(key, details.open);
    });
    allow.addEventListener("click", () => void decide(item, "allow"));
    allowRun.addEventListener("click", () => void decide(item, "allow_run"));
    deny.addEventListener("click", () => void decide(item, "deny"));
    return { node: card, sync(next) {
      item = next;
      const seconds = Math.max(0, Math.ceil(Number(item.expires_in_ms) / 1000));
      const label = t("decision.seconds", { count: seconds }, `${seconds} 秒`);
      if (timeout.textContent !== label) timeout.textContent = label;
      const busy = String(approvalDecisionStatus(key) !== "idle");
      for (const button of [allow, allowRun, deny])
        button.setAttribute("aria-disabled", busy);
    } };
  }

  function render() {
    const focused = container.contains(document.activeElement)
      ? document.activeElement?.dataset.decisionFocus : "";
    const scrollHost = container.closest('[role="tabpanel"]');
    const previousScroll = scrollHost?.scrollTop ?? 0;
    for (const details of container.querySelectorAll("details[data-approval-arguments]"))
      argumentsOpen.set(details.dataset.approvalArguments, details.open);
    const items = state.data?.items ?? [];
    const total = Number(state.data?.total ?? items.length);
    if (state.status === "ready") {
      const live = new Set(items.map((item) => String(item.id)));
      for (const key of argumentsOpen.keys()) if (!live.has(key)) argumentsOpen.delete(key);
    }
    clear(summary);
    summary.append(
      element("strong", { text: total }),
      document.createTextNode(t("decision.pendingCountSuffix", {}, " 个操作等待决定")),
    );
    if (state.data?.truncated) summary.append(element("span", {
      text: t("decision.truncated", {}, "仅显示最早 4 个") }));
    if (state.status === "error") {
      cards.clear();
      clear(container);
      container.append(element("div", { className: "resource-error", text: errorMessage(state.error) }));
      if (focused) document.querySelector("#decisions-tab")?.focus({ preventScroll: true });
      return;
    }
    if (!items.length) {
      cards.clear();
      clear(container);
      container.append(element("div", { className: "empty-state", text: state.status === "loading"
        ? t("decision.loading", {}, "正在检查待决操作…")
        : t("decision.empty", {}, "当前没有等待决定的操作") }));
      if (focused) document.querySelector("#decisions-tab")?.focus({ preventScroll: true });
      return;
    }
    const nodes = [];
    const liveCards = new Set();
    for (const item of items) {
      const key = String(item.id);
      liveCards.add(key);
      const signature = contentKey(item);
      let cached = cards.get(key);
      if (!cached || cached.signature !== signature) {
        cached = { ...renderCard(item), signature };
        cards.set(key, cached);
      }
      cached.sync(item);
      nodes.push(cached.node);
    }
    for (const key of cards.keys()) if (!liveCards.has(key)) cards.delete(key);
    if (container.children.length !== nodes.length ||
        nodes.some((node, index) => container.children[index] !== node))
      container.replaceChildren(...nodes);
    if (scrollHost) scrollHost.scrollTop = previousScroll;
    if (focused) {
      const replacement = [...container.querySelectorAll("[data-decision-focus]")]
        .find((node) => node.dataset.decisionFocus === focused);
      (replacement ?? document.querySelector("#decisions-tab"))?.focus({ preventScroll: true });
    }
  }

  const unsubscribeStore = store.subscribe((next) => {
    state = next;
    for (const item of next.data?.items ?? []) {
      const id = String(item.id);
      if (!seen.has(id)) {
        seen.add(id);
        if (seen.size > 128) seen.delete(seen.values().next().value);
        const tool = item.tool || t("decision.tool", {}, "工具");
        toast(t("decision.needsApproval", { tool }, `${tool} 需要你的允许`));
      }
    }
    render();
  });
  const unsubscribeDecisions = approvalDecisionStore.subscribe(render);
  const unsubscribeLocale = subscribeLocale(() => { cards.clear(); render(); });
  return () => { unsubscribeStore(); unsubscribeDecisions(); unsubscribeLocale(); };
}
