import { t } from "../../i18n.js";
import { element, errorMessage, isImeKey, toast } from "../../utils/dom.js";

// One answer interaction shared by conversation docks and background tasks.
export function createAskCard({ item, key, deciding, answered, drafts,
  onAnswer, onChanged, onSettled, renderHeader }) {
  const card = element("section", { className: "conversation-dock ask-dock" });
  const actions = element("div", { className: "ask-dock-options" });
  const hint = element("p", { className: "ask-dock-validation",
    attrs: { id: `ask-answer-hint-${key}`, "aria-live": "polite" } });
  const input = element("input", { className: "ask-dock-input",
    attrs: { type: "text", maxlength: "1024", placeholder: t("dock.ask.placeholder"),
      "aria-label": t("dock.ask.answerLabel"), "aria-describedby": hint.id } });
  input.value = drafts.get(key) ?? "";
  const submit = element("button", { text: t("dock.ask.submit"),
    attrs: { type: "button" } });
  const buttons = [submit];
  const encoder = new TextEncoder();
  let composing = false;
  function updateValidity() {
    const answer = input.value.trim();
    const tooLong = encoder.encode(answer).length > 1024;
    const pending = deciding.has(key);
    const submitted = answered.has(key);
    input.setAttribute("aria-invalid", String(tooLong));
    input.readOnly = pending || submitted;
    hint.textContent = pending ? t("dock.ask.submitting") :
      submitted ? t("dock.ask.submitted") :
        tooLong ? t("dock.ask.tooLong") : "";
    hint.dataset.state = pending || submitted ? "pending" :
      tooLong ? "error" : "";
    submit.setAttribute("aria-disabled", String(!answer || tooLong ||
      pending || submitted));
    for (const button of buttons.slice(1))
      button.setAttribute("aria-disabled", String(pending || submitted));
  }
  input.addEventListener("input", () => {
    drafts.set(key, input.value);
    updateValidity();
  });
  async function respond(value) {
    if (deciding.has(key) || answered.has(key)) return;
    deciding.add(key);
    updateValidity();
    try {
      await onAnswer(value);
      answered.add(key);
      drafts.delete(key);
      await onChanged();
    } catch (error) {
      toast(answered.has(key) ? t("dock.ask.refreshPending") :
        errorMessage(error), "error");
    } finally {
      deciding.delete(key);
      if (input.isConnected) updateValidity();
      onSettled();
    }
  }
  for (const option of item.options ?? []) {
    const button = element("button", { text: option,
      attrs: { type: "button" } });
    button.addEventListener("click", () => void respond(option));
    buttons.push(button);
    actions.append(button);
  }
  submit.addEventListener("click", () => {
    if (submit.getAttribute("aria-disabled") === "false") void respond(input.value);
  });
  input.addEventListener("compositionstart", () => { composing = true; });
  input.addEventListener("compositionend", () => { composing = false; });
  input.addEventListener("blur", () => { composing = false; });
  input.addEventListener("keydown", (event) => {
    if (event.key === "Enter" && !isImeKey(event, composing)) {
      event.preventDefault();
      if (submit.getAttribute("aria-disabled") === "false") void respond(input.value);
    }
  });
  updateValidity();
  const title = element("h3", { text: t("dock.ask.title"), attrs: { tabindex: "-1" } });
  card.append(renderHeader ? renderHeader(title, card) : title,
    element("p", { className: "ask-dock-question", text: item.question }),
    actions, element("div", { className: "ask-dock-free" }, [input, submit]),
    hint);
  return { node: card, sync() {
    title.textContent = t("dock.ask.title");
    input.placeholder = t("dock.ask.placeholder");
    input.setAttribute("aria-label", t("dock.ask.answerLabel"));
    submit.textContent = t("dock.ask.submit");
    updateValidity();
  } };
}
