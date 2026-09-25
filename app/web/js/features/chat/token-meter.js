import { clear, element } from "../../utils/dom.js";
import { currentLocale, subscribeLocale, t } from "../../i18n.js";

export function estimateInputTokens(text) {
  const characters = String(text || "");
  const cjk = (characters.match(/[\u2e80-\u9fff]/gu) ?? []).length;
  return Math.round(cjk + (characters.length - cjk) / 4);
}

export function createTokenMeter({ root, trigger, ring, panel, estimate, prompt,
  modelSelect, sessionStore, timelineStore, modelsStore }) {
  let open = false;

  function update() {
    const number = (value) => value.toLocaleString(currentLocale());
    const input = estimateInputTokens(prompt.value);
    estimate.textContent = t("token.inputEstimate", { count: number(input) },
      `输入 ~${number(input)} tok`);
    const session = sessionStore.get().data;
    const models = modelsStore.get().data?.models ?? [];
    const model = models.find((item) => item.id ===
      (modelSelect.value || session?.model_id)) || null;
    const calls = (timelineStore.get().data?.events ?? []).filter((event) =>
      event.kind === "model_done" && (event.input_tokens || event.output_tokens));
    const latestInput = Number(calls.at(-1)?.input_tokens || 0);
    const totalInput = calls.reduce((sum, item) => sum + Number(item.input_tokens || 0), 0);
    const totalOutput = calls.reduce((sum, item) => sum + Number(item.output_tokens || 0), 0);
    const windowTokens = Number(model?.context_window_tokens || 0);
    const percent = windowTokens ? Math.min(100, Math.round(latestInput / windowTokens * 100)) : 0;
    ring.style.setProperty("--meter-percent", `${percent}%`);
    trigger.title = windowTokens
      ? t("token.tooltip", { input: number(latestInput), limit: number(windowTokens) },
        `上次模型输入 ${number(latestInput)} / 上下文上限 ${number(windowTokens)} tokens`)
      : t("token.view", {}, "查看 token 用量");
    clear(panel);
    const details = element("dl");
    for (const [label, value] of [
      [t("token.model", {}, "当前模型"), model?.name || model?.id || "—"],
      [t("token.contextLimit", {}, "上下文上限"), windowTokens ? number(windowTokens) : "—"],
      [t("token.lastInput", {}, "上次模型输入"), latestInput ? number(latestInput) : "—"],
      [t("token.currentEstimate", {}, "本次输入估算"), `~${number(input)}`],
      [t("token.visibleInput", {}, "可见调用累计输入"), number(totalInput)],
      [t("token.visibleOutput", {}, "可见调用累计输出"), number(totalOutput)],
    ]) details.append(element("dt", { text: label }), element("dd", { text: value }));
    panel.append(element("h3", { text: t("token.title", {}, "Token 用量") }), details,
      element("p", { text: t("token.note", {},
        "模型用量来自服务端事件；输入框估算仅供参考。历史事件被裁剪时，累计值只包含当前可见调用。") }));
  }

  function setOpen(value) {
    open = value;
    panel.hidden = !open;
    trigger.setAttribute("aria-expanded", String(open));
    if (open) update();
  }
  trigger.addEventListener("click", () => setOpen(!open));
  document.addEventListener("click", (event) => {
    if (open && !root.contains(event.target)) setOpen(false);
  });
  document.addEventListener("keydown", (event) => {
    if (open && event.key === "Escape") { setOpen(false); trigger.focus(); }
  });
  prompt.addEventListener("input", update);
  const unsubscribers = [sessionStore.subscribe(update), timelineStore.subscribe(update),
    modelsStore.subscribe(update), subscribeLocale(update)];
  update();
  return Object.freeze({
    refresh: update,
    destroy: () => unsubscribers.forEach((unsubscribe) => unsubscribe()),
  });
}
