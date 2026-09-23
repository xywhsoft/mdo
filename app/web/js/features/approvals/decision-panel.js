import { decideApproval } from "../../state/approvals.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";

const RISK_LABELS = Object.freeze({ low: "低风险", medium: "中风险", high: "高风险" });
const EFFECT_LABELS = Object.freeze({
  read: "读取", workspace_write: "修改工作区", process: "运行进程", network: "访问网络",
  external_service: "外部服务", secrets: "使用凭据", schedule: "计划任务", agent_delegation: "启动子 Agent",
});
const ACCESS_LABELS = Object.freeze({
  read: "读取", write: "写入", execute: "执行", control: "控制", connect: "连接", use: "使用",
});

function formatArguments(source) {
  if (!source) return "{}";
  try { return JSON.stringify(JSON.parse(source), null, 2); } catch { return source; }
}

function resourceText(resource) {
  const access = (resource.access ?? []).map((item) => ACCESS_LABELS[item] ?? item).join("、");
  return `${access || "访问"} · ${resource.resource}`;
}

export function createDecisionPanel({ container, summary, store, onChanged }) {
  let state = store.get();
  const deciding = new Set();
  const seen = new Set();

  async function decide(item, decision, card) {
    if (deciding.has(String(item.id))) return;
    deciding.add(String(item.id));
    for (const button of card.querySelectorAll("button")) button.disabled = true;
    try {
      await decideApproval(item.id, decision);
      toast(decision === "allow" ? `已允许 ${item.tool} 本次执行` : `已拒绝 ${item.tool} 本次执行`);
      onChanged?.();
    } catch (error) {
      toast(errorMessage(error), "error");
    } finally {
      deciding.delete(String(item.id));
      render();
    }
  }

  function renderCard(item) {
    const card = element("article", { className: "approval-card", attrs: { "data-risk": item.risk } });
    const allow = element("button", { className: "primary-button approval-allow", text: "允许一次", attrs: { type: "button" } });
    const deny = element("button", { className: "secondary-button approval-deny", text: "拒绝", attrs: { type: "button" } });
    const resources = element("ul", { className: "approval-resources" });
    for (const resource of item.resources ?? []) {
      resources.append(element("li", {}, [
        element("span", { className: "approval-resource-kind", text: resource.kind || "resource" }),
        element("span", { text: resourceText(resource) }),
      ]));
    }
    if (!resources.children.length) resources.append(element("li", { text: "未声明具体资源" }));
    card.append(
      element("header", { className: "approval-heading" }, [
        element("div", {}, [
          element("h3", { text: item.tool || "未知工具" }),
          element("p", { text: `${RISK_LABELS[item.risk] ?? item.risk} · ${(item.effects ?? []).map((effect) => EFFECT_LABELS[effect] ?? effect).join("、") || "未声明影响"}` }),
        ]),
        element("span", { className: "approval-timeout", text: `${Math.max(0, Math.ceil(Number(item.expires_in_ms) / 1000))} 秒` }),
      ]),
      element("p", { className: "approval-workspace", text: item.workspace_root || "默认工作区" }),
      resources,
      element("details", { className: "approval-arguments" }, [
        element("summary", { text: "查看调用参数" }),
        element("pre", { text: formatArguments(item.arguments_json) }),
      ]),
      element("div", { className: "approval-actions" }, [deny, allow]),
    );
    const busy = deciding.has(String(item.id));
    allow.disabled = busy;
    deny.disabled = busy;
    allow.addEventListener("click", () => void decide(item, "allow", card));
    deny.addEventListener("click", () => void decide(item, "deny", card));
    return card;
  }

  function render() {
    const items = state.data?.items ?? [];
    const total = Number(state.data?.total ?? items.length);
    clear(summary);
    summary.append(
      element("strong", { text: total }),
      document.createTextNode(" 个操作等待决定"),
    );
    if (state.data?.truncated) summary.append(element("span", { text: "仅显示最早 4 个" }));
    clear(container);
    if (state.status === "error") {
      container.append(element("div", { className: "resource-error", text: errorMessage(state.error) }));
      return;
    }
    if (!items.length) {
      container.append(element("div", { className: "empty-state", text: state.status === "loading" ? "正在检查待决操作…" : "当前没有等待决定的操作" }));
      return;
    }
    for (const item of items) container.append(renderCard(item));
  }

  return store.subscribe((next) => {
    state = next;
    for (const item of next.data?.items ?? []) {
      const id = String(item.id);
      if (!seen.has(id)) {
        seen.add(id);
        if (seen.size > 128) seen.delete(seen.values().next().value);
        toast(`${item.tool || "工具"} 需要你的允许`);
      }
    }
    render();
  });
}
