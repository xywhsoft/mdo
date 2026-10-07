import { abandonRecovery, loadRecovery, resumeRecovery } from "../../state/recovery.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";
import { renderToolPreview, renderToolArguments } from "../approvals/tool-preview.js";
import { createRecoveryDecisions, defaultRecoveryAction } from "../approvals/recovery-decisions.js";
import { isTransientReadError } from "../../api/read-recovery.js";

export function createRecoveryDock({ container, summary, store, onResume, onAbandon }) {
  let state = store.get();
  const decisions = createRecoveryDecisions();
  const argumentsOpen = new Map();

  function choose(data, item, action) {
    if (decisions.choose(data, item.tool_call_id, action)) render();
  }

  async function submitRecovery(data) {
    const operation = decisions.begin(data);
    if (!operation) return;
    let accepted = false;
    render();
    try {
      const run = await resumeRecovery(operation.data, operation.choices);
      accepted = true;
      if (decisions.isCurrent(data)) toast(t("recovery.submitted", {}, "正在继续任务"));
      onResume?.(run, operation.data);
      if (decisions.isCurrent(data)) await loadRecovery();
    } catch (error) {
      if (decisions.isCurrent(data)) {
        toast(errorMessage(error), "error");
        if (error?.code === "recovery_state_conflict") await loadRecovery();
      }
    } finally {
      decisions.finish(operation, accepted);
      render();
    }
  }

  async function submitAbandon(data) {
    const operation = decisions.begin(data);
    if (!operation) return;
    let accepted = false;
    render();
    try {
      await abandonRecovery(operation.data);
      accepted = true;
      if (decisions.isCurrent(data)) {
        toast(t("recovery.abandoned", {}, "已结束本次回复，可以发送新消息"));
        await loadRecovery();
      }
      await onAbandon?.(operation.data);
      const current = store.get().data;
      if (current?.project_id === data.project_id && current?.session_id === data.session_id)
        document.querySelector("#prompt")?.focus({ preventScroll: true });
    } catch (error) {
      if (decisions.isCurrent(data)) {
        toast(errorMessage(error), "error");
        if (error?.code === "recovery_state_conflict") await loadRecovery();
      }
    } finally {
      decisions.finish(operation, accepted);
      render();
    }
  }

  function optionButton(data, item, action, label) {
    const id = String(item.tool_call_id);
    const selected = decisions.choice(id) === action;
    const button = element("button", {
      className: selected ? "recovery-option selected" : "recovery-option",
      text: label,
      attrs: { type: "button", "aria-pressed": String(selected),
        "aria-disabled": String(decisions.isBusy(data) || decisions.isSubmitted(data)),
        "data-recovery-focus": `${id}/${action}` },
    });
    button.disabled = action === "retry" && !item.tool_available;
    button.addEventListener("click", () => choose(data, item, action));
    return button;
  }

  function renderCard(data, item) {
    const id = String(item.tool_call_id);
    const tool = item.tool || t("decision.unknownTool", {}, "工具");
    const card = element("article", { className: "recovery-card" });
    const argumentsView = renderToolArguments(item, {
      "data-recovery-arguments": id, open: argumentsOpen.get(id) ? "" : null,
    }, { "data-recovery-focus": `${id}/arguments` });
    card.append(
      renderToolPreview(item),
      element("p", {
        className: "recovery-warning",
        text: item.tool_available
          ? t(decisions.choice(id) === "retry" ? "recovery.retryWarning" : "recovery.unknownResult")
          : t("recovery.toolUnavailable"),
      }),
      argumentsView,
    );
    if (item.tool_available) card.append(
      element("div", { className: "recovery-options", attrs: { role: "group",
        "aria-label": t("recovery.group", { tool },
          `${tool} 的处理方式`) } }, [
        optionButton(data, item, "record_uncertain", t("recovery.recordUncertain")),
        optionButton(data, item, "retry", t("recovery.retry")),
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
      : document.querySelector("#recovery-title") ?? document.querySelector("#prompt"))?.focus({ preventScroll: true });
  }

  function render() {
    const focused = container.contains(document.activeElement)
      ? document.activeElement?.dataset.recoveryFocus : "";
    for (const details of container.querySelectorAll("details[data-recovery-arguments]"))
      argumentsOpen.set(details.dataset.recoveryArguments, details.open);
    const data = state.data ?? {};
    const items = data.items ?? [];
    decisions.select(data);
    const activeIds = new Set(items.map((item) => String(item.tool_call_id)));
    for (const id of argumentsOpen.keys()) if (!activeIds.has(id)) argumentsOpen.delete(id);
    clear(summary);
    clear(container);
    if (state.status === "error" && isTransientReadError(state.error)) {
      summary.textContent = t("shell.connecting", {}, "正在连接本地服务…");
      restoreFocus(focused);
      return;
    }
    if (state.status === "error") {
      summary.textContent = t("recovery.unavailable", {}, "恢复状态不可用");
      container.append(element("div", { className: "resource-error", text: errorMessage(state.error) }));
      const retry = element("button", { className: "secondary-button",
        text: t("task.detail.retry", {}, "重试读取"), attrs: { type: "button" } });
      retry.addEventListener("click", () => void loadRecovery());
      container.append(retry);
      restoreFocus(focused);
      return;
    }
    if (data.unavailable) {
      summary.textContent = t("recovery.checkPaused", {}, "会话运行期间暂不检查恢复状态");
      restoreFocus(focused);
      return;
    }
    if (!data.resume_required) {
      summary.textContent = state.status === "loading"
        ? t("recovery.checking", {}, "正在检查中断状态…")
        : t("recovery.notRequired", {}, "当前会话不需要恢复");
      restoreFocus(focused);
      return;
    }
    if (decisions.isSubmitted(data)) {
      summary.textContent = t("recovery.syncing");
      restoreFocus(focused);
      return;
    }
    const safeCalls = [], uncertainCalls = [];
    for (const item of items)
      (defaultRecoveryAction(item) === "retry" ? safeCalls : uncertainCalls).push(item);
    summary.textContent = t(uncertainCalls.length ? "recovery.pendingSummary" :
      safeCalls.length ? "recovery.safeSummary" : "recovery.noPendingCalls", { count: items.length });
    for (const item of uncertainCalls) container.append(renderCard(data, item));
    if (safeCalls.length && uncertainCalls.length) container.append(element("p", {
      className: "interaction-hint", text: t("recovery.safeSummary", { count: safeCalls.length }),
    }));
    const ready = decisions.ready(data);
    const submitButton = element("button", {
      className: "primary-button recovery-submit",
      text: decisions.isBusy(data) ? t("recovery.continuing") : t("recovery.resume"),
      attrs: { type: "button", "data-recovery-focus": "submit",
        "aria-disabled": String(decisions.isBusy(data)) },
    });
    submitButton.disabled = !ready;
    submitButton.addEventListener("click", () => void submitRecovery(data));
    {
      const abandonButton = element("button", {
        className: "recovery-abandon",
        text: t("recovery.endTurn", {}, "结束本次回复"),
        attrs: { type: "button", "data-recovery-focus": "abandon",
          "aria-disabled": String(decisions.isBusy(data)) },
      });
      abandonButton.addEventListener("click", () => void submitAbandon(data));
      container.append(element("div", { className: "recovery-actions" }, [
        submitButton, abandonButton,
      ]));
    }
    restoreFocus(focused);
  }

  const unsubscribeStore = store.subscribe((next) => {
    state = next;
    render();
  });
  const unsubscribeLocale = subscribeLocale(render);
  return () => { unsubscribeStore(); unsubscribeLocale(); };
}
