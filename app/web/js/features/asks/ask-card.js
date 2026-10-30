import { setUiText as writeUiText, setUiAttribute as writeUiAttribute, uiMessage, t } from "../../i18n.js";

import { element, errorMessage, isImeKey, toast } from "../../utils/dom.js";

// One answer interaction shared by conversation docks and background tasks.
export function createAskCard({ item, key, deciding, answered, drafts,
  onAnswer, onChanged, onSettled, renderHeader }) {
  const card = element("section", { className: "conversation-dock ask-dock" });
  const actions = element("div", { className: "ask-dock-options" });
  const hint = element("p", { className: "ask-dock-validation",
    attrs: { id: `ask-answer-hint-${key}`, "aria-live": "polite" } });
  const input = element("input", { className: "ask-dock-input",
    attrs: { type: "text", maxlength: "1024", placeholder: uiMessage("dock.ask.placeholder"),
      "aria-label": uiMessage("dock.ask.answerLabel"), "aria-describedby": hint.id } });
  input.value = drafts.get(key) ?? "";
  const submit = element("button", { text: uiMessage("dock.ask.submit"),
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
    writeUiText(hint, pending ? uiMessage("dock.ask.submitting") :
      submitted ? uiMessage("dock.ask.submitted") :
        tooLong ? uiMessage("dock.ask.tooLong") : "");
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
      if (input.isConnected && error?.name !== "AbortError")
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
  const title = element("h3", { text: uiMessage("dock.ask.title"), attrs: { tabindex: "-1" } });
  card.append(renderHeader ? renderHeader(title, card) : title,
    element("p", { className: "ask-dock-question", text: item.question }),
    element("p", { className: "interaction-hint", text: uiMessage((item.options ?? []).length
      ? "interaction.ask.hint" : "interaction.ask.freeHint") }),
    actions, element("div", { className: "ask-dock-free" }, [input, submit]),
    hint);
  return { node: card, sync() {
    writeUiText(title, uiMessage("dock.ask.title"));
    writeUiAttribute(input, "placeholder", uiMessage("dock.ask.placeholder"));
    writeUiAttribute(input, "aria-label", t("dock.ask.answerLabel"));
    writeUiText(submit, uiMessage("dock.ask.submit"));
    updateValidity();
  } };
}
