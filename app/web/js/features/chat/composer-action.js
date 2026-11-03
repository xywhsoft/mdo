import { uiMessage, setUiAttribute } from "../../i18n.js";

// Keep native form submission separate from cancellation: Enter in an empty
// editor must never activate Stop through the form's default submit action.
export function renderComposerAction(button, { running, hasDraft, sendDisabled,
  stopDisabled, stopBusy, queue, stopLabel = uiMessage("shell.stop") }) {
  const stopping = Boolean(running && !hasDraft);
  button.dataset.action = stopping ? "stop" : "send";
  button.type = stopping ? "button" : "submit";
  button.classList.toggle("send-button", !stopping);
  button.classList.toggle("stop-button", stopping);
  button.disabled = Boolean(stopping ? stopDisabled : sendDisabled);
  if (stopping) button.setAttribute("aria-disabled", String(Boolean(stopBusy)));
  else button.removeAttribute("aria-disabled");
  const label = stopping ? stopLabel : queue
    ? uiMessage("composer.queue", {}, "加入待发送队列")
    : uiMessage("shell.send", {}, "发送任务");
  setUiAttribute(button, "title", label);
  setUiAttribute(button, "aria-label", label);
}
