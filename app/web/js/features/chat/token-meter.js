import { clear, element, isImeKey } from "../../utils/dom.js";
import { currentLocale, subscribeLocale, t } from "../../i18n.js";
import { findModel } from "../../utils/models.js";

export function estimateInputTokens(text) {
  // Match xllmEstimateTextTokens: ceil(ASCII / 4) + Unicode scalar count.
  let ascii = 0, other = 0;
  for (const character of String(text || "")) {
    if (character.codePointAt(0) < 128) ascii += 1;
    else other += 1;
  }
  return Math.ceil(ascii / 4) + other;
}

export function contextCheckpoint(events) {
  const boundary = events.findLastIndex(event => event.kind === "history_truncated");
  const retained = events.slice(boundary + 1).filter(event => !Number(event.agent_depth));
  const latestCall = retained.findLast(event => event.kind === "model_done" &&
    event.success !== false && (event.input_tokens || event.output_tokens));
  const contextEvent = retained.findLast(event => event.context &&
    ["estimated_input_tokens", "input_budget_tokens", "compact_threshold_tokens"].every(key =>
      Number.isSafeInteger(event.context[key]) && event.context[key] >= 0));
  return { latestCall, contextEvent };
}

export function createTokenMeter({ root, trigger, ring, panel, estimate, prompt,
  modelSelect, sessionStore, timelineStore, modelsStore, runsStore,
  attachments = () => [] }) {
  let open = false;
  let closeButton = null;

  function update() {
    const number = (value) => value.toLocaleString(currentLocale());
    const input = estimateInputTokens(prompt.value);
    const imageCount = attachments().length;
    const hasImages = imageCount > 0;
    const draft = input + imageCount * 4096 + (prompt.value || hasImages ? 12 : 0);
    estimate.textContent = hasImages
      ? t("token.textEstimateWithImages", { count: number(input), images: number(imageCount * 4096) },
        `文字 ~${number(input)} tok · 图片 ~${number(imageCount * 4096)} tok`)
      : prompt.value ? t("token.inputEstimate", { count: number(input) },
        `输入 ~${number(input)} tok`) : "";
    estimate.title = hasImages
      ? t("token.imageEstimateTitle", {}, "图片按每张 4,096 token 估算；实际用量由模型返回")
      : t("token.estimateTitle", {}, "按输入文本粗略估算");
    const session = sessionStore.get().data;
    const models = modelsStore.get().data?.models ?? [];
    const model = findModel(models, modelSelect.value || session?.model_id);
    const timeline = timelineStore.get().data;
    const events = session && timeline?.projectId === session.project_id &&
      timeline?.sessionId === session.id ? timeline.events ?? [] : [];
    const calls = events.filter((event) =>
      event.kind === "model_done" && (event.input_tokens || event.output_tokens));
    const { latestCall, contextEvent } = contextCheckpoint(events);
    const reference = contextEvent || latestCall;
    const latestInput = Number(latestCall?.input_tokens || 0);
    const latestOutput = Number(latestCall?.output_tokens || 0);
    const eventModelId = reference?.model_id || "";
    const lastRun = reference && !eventModelId && session &&
      (runsStore?.get().data?.items ?? []).find((run) =>
        run.project_id === session.project_id && run.session_id === session.id &&
        Number(run.agent_run_id) === Number(reference.run_id));
    const historicalModelId = eventModelId || lastRun?.model_id || "";
    const matchingModels = models.filter((item) => reference?.model &&
      (item.id === reference.model || item.wire_model === reference.model));
    const latestModel = historicalModelId
      ? findModel(models, historicalModelId)
      : matchingModels.length === 1 ? matchingModels[0] : null;
    const lastModelName = latestModel?.name || historicalModelId ||
      reference?.model || "—";
    const totalInput = calls.reduce((sum, item) => sum + Number(item.input_tokens || 0), 0);
    const totalOutput = calls.reduce((sum, item) => sum + Number(item.output_tokens || 0), 0);
    const windowTokens = Number(model?.context_window_tokens || 0);
    const lastWindowTokens = Number(reference?.context_window_tokens || 0) ||
      Number(latestModel?.context_window_tokens || 0) || (!reference ? windowTokens : 0);
    const base = contextEvent ? contextEvent.context.estimated_input_tokens : latestInput + latestOutput;
    const nextInput = base + draft;
    const compactThreshold = Number(contextEvent?.context.compact_threshold_tokens || 0);
    const percent = lastWindowTokens
      ? Math.min(100, Math.round(nextInput / lastWindowTokens * 100)) : 0;
    ring.style.setProperty("--meter-percent", `${percent}%`);
    trigger.title = lastWindowTokens
      ? t("token.tooltip", { model: reference ? lastModelName : model?.name || model?.id || "—",
        input: number(nextInput), limit: number(lastWindowTokens) },
        `${lastModelName} 预计下次输入 ~${number(nextInput)} / 上下文上限 ${number(lastWindowTokens)} tokens`)
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
      ...(reference && latestModel?.id !== model?.id ? [
        [t("token.lastModel", {}, "上次调用模型"), lastModelName],
        [t("token.lastContextLimit", {}, "上次调用上下文上限"),
          lastWindowTokens ? number(lastWindowTokens) : "—"],
      ] : []),
      [t("token.lastInput", {}, "上次模型输入"), latestInput ? number(latestInput) : "—"],
      [t("token.lastOutput", {}, "上次模型输出"), latestOutput ? number(latestOutput) : "—"],
      [t("token.nextInput", {}, "预计下次输入"), `~${number(nextInput)}`],
      ...(contextEvent ? [
        [t("token.usageBase", {}, "服务端用量基准"), contextEvent.context.exact
          ? number(contextEvent.context.fill_tokens) : t("token.estimatedBase", {}, "历史内容估算")],
        [t("token.contextGrowth", {}, "基准后新增估算"), `~${number(contextEvent.context.growth_tokens + draft)}`],
        [t("token.compactThreshold", {}, "自动压缩阈值"), number(compactThreshold)],
      ] : []),
      [t(hasImages ? "token.currentTextEstimate" : "token.currentEstimate", {},
        hasImages ? "本次文字估算" : "本次输入估算"), `~${number(input)}`],
      [t("token.visibleInput", {}, "可见调用累计输入"), number(totalInput)],
      [t("token.visibleOutput", {}, "可见调用累计输出"), number(totalOutput)],
    ];
    for (const [label, value] of rows)
      details.append(element("dt", { text: label }), element("dd", { text: value }));
    panel.append(heading, details,
      element("p", { text: t("token.note", {},
        "预计输入与自动压缩共用上下文检查点：上次服务端输入与输出，加上此后新增内容及草稿估算。输出预留与安全余量已计入压缩阈值；累计用量只包含可见调用。") }));
    if (hasImages)
      panel.append(element("p", { text: t("token.imageEstimateNote", {},
        "图片按每张 4,096 token 估算，实际用量取决于模型。") }));
    if (!contextEvent && latestCall)
      panel.append(element("p", { text: t("token.legacyCheckpoint", {},
        "此会话暂无新的上下文检查点，当前使用上次用量估算；继续运行后刷新。") }));
    if (reference && !lastWindowTokens)
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
