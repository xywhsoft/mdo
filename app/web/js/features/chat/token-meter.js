import { clear, element, isImeKey } from "../../utils/dom.js";
import { currentLocale, subscribeLocale, t } from "../../i18n.js";
import { findModel } from "../../utils/models.js";

export function estimateInputTokens(text) {
  const characters = String(text || "");
  const cjk = (characters.match(/[\u2e80-\u9fff]/gu) ?? []).length;
  return Math.round(cjk + (characters.length - cjk) / 4);
}

export function createTokenMeter({ root, trigger, ring, panel, estimate, prompt,
  modelSelect, sessionStore, timelineStore, modelsStore, runsStore,
  attachments = () => [] }) {
  let open = false;
  let closeButton = null;

  function update() {
    const number = (value) => value.toLocaleString(currentLocale());
    const input = estimateInputTokens(prompt.value);
    const hasImages = attachments().length > 0;
    estimate.textContent = hasImages
      ? t("token.textEstimateWithImages", { count: number(input) },
        `文字 ~${number(input)} tok · 图片另计`)
      : prompt.value ? t("token.inputEstimate", { count: number(input) },
        `输入 ~${number(input)} tok`) : "";
    estimate.title = hasImages
      ? t("token.imageEstimateTitle", {}, "仅估算输入文字；图片 token 未计入")
      : t("token.estimateTitle", {}, "按输入文本粗略估算");
    const session = sessionStore.get().data;
    const models = modelsStore.get().data?.models ?? [];
    const model = findModel(models, modelSelect.value || session?.model_id);
    const timeline = timelineStore.get().data;
    const calls = (session && timeline?.projectId === session.project_id &&
      timeline?.sessionId === session.id ? timeline.events ?? [] : []).filter((event) =>
      event.kind === "model_done" && (event.input_tokens || event.output_tokens));
    const latestCall = calls.at(-1);
    const latestInput = Number(latestCall?.input_tokens || 0);
    const eventModelId = latestCall?.model_id || "";
    const lastRun = latestCall && !eventModelId &&
      !Number(latestCall.agent_depth) && session &&
      (runsStore?.get().data?.items ?? []).find((run) =>
        run.project_id === session.project_id && run.session_id === session.id &&
        Number(run.agent_run_id) === Number(latestCall.run_id));
    const historicalModelId = eventModelId || lastRun?.model_id || "";
    const matchingModels = models.filter((item) => latestCall?.model &&
      (item.id === latestCall.model || item.wire_model === latestCall.model));
    const latestModel = historicalModelId
      ? findModel(models, historicalModelId)
      : matchingModels.length === 1 ? matchingModels[0] : null;
    const lastModelName = latestModel?.name || historicalModelId ||
      latestCall?.model || "—";
    const totalInput = calls.reduce((sum, item) => sum + Number(item.input_tokens || 0), 0);
    const totalOutput = calls.reduce((sum, item) => sum + Number(item.output_tokens || 0), 0);
    const windowTokens = Number(model?.context_window_tokens || 0);
    const lastWindowTokens = Number(latestCall?.context_window_tokens || 0) ||
      Number(latestModel?.context_window_tokens || 0);
    const percent = lastWindowTokens
      ? Math.min(100, Math.round(latestInput / lastWindowTokens * 100)) : 0;
    ring.style.setProperty("--meter-percent", `${percent}%`);
    trigger.title = latestCall && lastWindowTokens
      ? t("token.tooltip", { model: lastModelName,
        input: number(latestInput), limit: number(lastWindowTokens) },
        `上次 ${lastModelName} 输入 ${number(latestInput)} / 上下文上限 ${number(lastWindowTokens)} tokens`)
      : t("token.view", {}, "查看 token 用量");
    const scrollTop = panel.scrollTop;
    const closeFocused = document.activeElement === closeButton;
    clear(panel);
    closeButton = element("button", { className: "context-meter-close", text: "×",
      attrs: { type: "button", "aria-label": t("token.close", {}, "关闭 token 用量") } });
    closeButton.addEventListener("click", () => setOpen(false));
    const heading = element("div", { className: "context-meter-heading" }, [
      element("h3", { text: t("token.title", {}, "Token 用量"),
        attrs: { id: "context-meter-title" } }), closeButton,
    ]);
    const details = element("dl");
    const rows = [
      [t("token.model", {}, "输入区模型"), model?.name || model?.id || "—"],
      [t("token.contextLimit", {}, "输入区上下文上限"),
        windowTokens ? number(windowTokens) : "—"],
      ...(latestCall && latestModel?.id !== model?.id ? [
        [t("token.lastModel", {}, "上次调用模型"), lastModelName],
        [t("token.lastContextLimit", {}, "上次调用上下文上限"),
          lastWindowTokens ? number(lastWindowTokens) : "—"],
      ] : []),
      [t("token.lastInput", {}, "上次模型输入"), latestInput ? number(latestInput) : "—"],
      [t(hasImages ? "token.currentTextEstimate" : "token.currentEstimate", {},
        hasImages ? "本次文字估算" : "本次输入估算"), `~${number(input)}`],
      [t("token.visibleInput", {}, "可见调用累计输入"), number(totalInput)],
      [t("token.visibleOutput", {}, "可见调用累计输出"), number(totalOutput)],
    ];
    for (const [label, value] of rows)
      details.append(element("dt", { text: label }), element("dd", { text: value }));
    panel.append(heading, details,
      element("p", { text: t("token.note", {},
        "模型用量来自服务端事件；输入框估算仅供参考。历史事件被裁剪时，累计值只包含当前可见调用。") }));
    if (hasImages)
      panel.append(element("p", { text: t("token.imageEstimateNote", {},
        "图片 token 未计入估算，实际用量取决于模型。") }));
    if (latestCall && !lastWindowTokens)
      panel.append(element("p", { text: t("token.unknownContext", {},
        "上次调用的模型配置无法确定，暂不显示上下文占比。") }));
    panel.scrollTop = scrollTop;
    if (closeFocused) closeButton.focus({ preventScroll: true });
  }

  function setOpen(value) {
    const focusWasInside = panel.contains(document.activeElement);
    open = value;
    panel.hidden = !open;
    trigger.setAttribute("aria-expanded", String(open));
    if (open) { update(); panel.scrollTop = 0; panel.focus({ preventScroll: true }); }
    else if (focusWasInside) trigger.focus({ preventScroll: true });
  }
  function onTriggerClick() { setOpen(!open); }
  function onOutsideClick(event) {
    if (open && !root.contains(event.target)) setOpen(false);
  }
  function onEscape(event) {
    if (!open || event.key !== "Escape" || event.defaultPrevented || isImeKey(event) ||
        document.querySelector("dialog[open]")) return;
    // The same key must not reach the workspace's Escape-to-stop shortcut.
    event.preventDefault();
    event.stopImmediatePropagation();
    setOpen(false);
  }
  trigger.addEventListener("click", onTriggerClick);
  document.addEventListener("click", onOutsideClick);
  document.addEventListener("keydown", onEscape);
  prompt.addEventListener("input", update);
  const unsubscribers = [sessionStore.subscribe(update), timelineStore.subscribe(update),
    modelsStore.subscribe(update), runsStore?.subscribe(update), subscribeLocale(update)]
    .filter(Boolean);
  update();
  return Object.freeze({
    refresh: update,
    destroy() {
      trigger.removeEventListener("click", onTriggerClick);
      document.removeEventListener("click", onOutsideClick);
      document.removeEventListener("keydown", onEscape);
      prompt.removeEventListener("input", update);
      unsubscribers.forEach((unsubscribe) => unsubscribe());
    },
  });
}
