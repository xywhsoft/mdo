import { abandonRecovery, loadRecovery, resumeRecovery } from "../../state/recovery.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";

const EFFECT_LABELS = Object.freeze({
  read: "读取", workspace_write: "修改工作区", process: "运行进程", network: "访问网络",
  external_service: "外部服务", secrets: "使用凭据", schedule: "计划任务", agent_delegation: "启动子 Agent",
});

function formatArguments(source) {
  if (!source) return "{}";
  try { return JSON.stringify(JSON.parse(source), null, 2); } catch { return source; }
}

export function createRecoveryPanel({ container, summary, store, onResume, onAbandon }) {
  let state = store.get();
  let submitting = false;
  const choices = new Map();
  const argumentsOpen = new Map();

  function choose(item, action) {
    if (submitting) return;
    choices.set(String(item.tool_call_id), action);
    render();
  }

  async function submitRecovery(data) {
    if (submitting) return;
    submitting = true;
    render();
    try {
      const run = await resumeRecovery(data, choices);
      choices.clear();
      toast("恢复决定已提交，正在继续会话");
      onResume?.(run);
      await loadRecovery();
    } catch (error) {
      toast(errorMessage(error), "error");
      if (error?.code === "recovery_state_conflict") await loadRecovery();
    } finally {
      submitting = false;
      render();
    }
  }

  async function submitAbandon(data) {
    if (submitting) return;
    submitting = true;
    render();
    try {
      await abandonRecovery(data);
      choices.clear();
      toast("已结束中断的轮次");
      await loadRecovery();
      await onAbandon?.();
    } catch (error) {
      toast(errorMessage(error), "error");
      if (error?.code === "recovery_state_conflict") await loadRecovery();
    } finally {
      submitting = false;
      render();
    }
  }

  function optionButton(item, action, label) {
    const id = String(item.tool_call_id);
    const selected = choices.get(id) === action;
    const button = element("button", {
      className: selected ? "recovery-option selected" : "recovery-option",
      text: label,
      attrs: { type: "button", "aria-pressed": String(selected),
        "aria-disabled": String(submitting),
        "data-recovery-focus": `${id}/${action}` },
    });
    button.disabled = action === "retry" && !item.tool_available;
    button.addEventListener("click", () => choose(item, action));
    return button;
  }

  function renderCard(item) {
    const id = String(item.tool_call_id);
    const effects = (item.effects ?? []).map((effect) => EFFECT_LABELS[effect] ?? effect).join("、") || "未声明影响";
    const card = element("article", { className: "approval-card recovery-card" });
    card.append(
      element("header", { className: "approval-heading" }, [
        element("div", {}, [
          element("h3", { text: item.tool || "未知工具" }),
          element("p", { text: `${effects} · 第 ${item.turn} 轮` }),
        ]),
        element("span", { className: "approval-timeout", text: item.automatic_retry_safe ? "可安全重试" : "需明确决定" }),
      ]),
      element("p", {
        className: "recovery-warning",
        text: item.tool_available
          ? "上次进程可能已执行此调用。再次执行采用至少一次语义。"
          : "当前工具不可用，只能记录为不确定并让模型继续处理。",
      }),
      element("details", { className: "approval-arguments", attrs: {
        "data-recovery-arguments": id, open: argumentsOpen.get(id) ? "" : null } }, [
        element("summary", { text: "查看原调用参数", attrs: {
          "data-recovery-focus": `${id}/arguments` } }),
        element("pre", { text: formatArguments(item.arguments_json) }),
      ]),
      element("div", { className: "recovery-options", attrs: { role: "group", "aria-label": `${item.tool} 的恢复决定` } }, [
        optionButton(item, "record_uncertain", "记录为不确定"),
        optionButton(item, "retry", "重新执行"),
      ]),
    );
    const details = card.querySelector("details");
    details.addEventListener("toggle", () => argumentsOpen.set(id, details.open));
    return card;
  }

  function restoreFocus(focused) {
    if (!focused) return;
    const replacement = [...container.querySelectorAll("[data-recovery-focus]")]
      .find((node) => node.dataset.recoveryFocus === focused);
    (replacement && !replacement.disabled ? replacement
      : document.querySelector("#decisions-tab"))?.focus({ preventScroll: true });
  }

  function render() {
    const focused = container.contains(document.activeElement)
      ? document.activeElement?.dataset.recoveryFocus : "";
    for (const details of container.querySelectorAll("details[data-recovery-arguments]"))
      argumentsOpen.set(details.dataset.recoveryArguments, details.open);
    const data = state.data ?? {};
    const items = data.items ?? [];
    const activeIds = new Set(items.map((item) => String(item.tool_call_id)));
    for (const id of choices.keys()) if (!activeIds.has(id)) choices.delete(id);
    for (const id of argumentsOpen.keys()) if (!activeIds.has(id)) argumentsOpen.delete(id);
    clear(summary);
    clear(container);
    if (state.status === "error") {
      summary.textContent = "恢复状态不可用";
      container.append(element("div", { className: "resource-error", text: errorMessage(state.error) }));
      restoreFocus(focused);
      return;
    }
    if (data.unavailable) {
      summary.textContent = "会话运行期间暂不检查恢复状态";
      restoreFocus(focused);
      return;
    }
    if (!data.resume_required) {
      summary.textContent = state.status === "loading" ? "正在检查中断状态…" : "当前会话不需要恢复";
      restoreFocus(focused);
      return;
    }
    summary.append(
      element("strong", { text: items.length }),
      document.createTextNode(items.length ? " 个持久化调用需要处理" : " 个工具调用；需要重新请求模型"),
    );
    for (const item of items) container.append(renderCard(item));
    const ready = items.every((item) => choices.has(String(item.tool_call_id)));
    const submitButton = element("button", {
      className: "primary-button recovery-submit",
      text: items.length ? "按以上决定恢复会话" : "继续恢复会话",
      attrs: { type: "button", "data-recovery-focus": "submit",
        "aria-disabled": String(submitting) },
    });
    submitButton.disabled = !ready;
    submitButton.addEventListener("click", () => void submitRecovery(data));
    if (items.length) container.append(submitButton);
    else {
      container.append(element("p", { className: "recovery-warning",
        text: "上一轮没有待决工具调用。可以继续请求模型，也可以结束该轮并发送后续消息。",
      }));
      const abandonButton = element("button", {
        className: "recovery-abandon",
        text: "结束中断轮次",
        attrs: { type: "button", "data-recovery-focus": "abandon",
          "aria-disabled": String(submitting) },
      });
      abandonButton.addEventListener("click", () => void submitAbandon(data));
      container.append(element("div", { className: "recovery-actions" }, [
        submitButton, abandonButton,
      ]));
    }
    restoreFocus(focused);
  }

  return store.subscribe((next) => {
    state = next;
    render();
  });
}
