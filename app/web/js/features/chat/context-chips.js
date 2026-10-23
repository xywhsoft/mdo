import { element } from "../../utils/dom.js";
import { t } from "../../i18n.js";

export function previewPromptContext(item, opener = document.activeElement) {
  const dialog = element("dialog", { className: "app-dialog prompt-context-preview" });
  const heading = element("h2", { text: item.label });
  heading.id = `context-${item.id}`;
  dialog.setAttribute("aria-labelledby", heading.id);
  const close = element("button", { text: t("context.close"), attrs: { type: "button", autofocus: "" } });
  close.addEventListener("click", () => dialog.close());
  dialog.append(element("div", { className: "dialog-card" }, [heading,
    element("pre", { text: item.content ?? item.path ?? "" }), close]));
  dialog.addEventListener("close", () => { dialog.remove(); if (opener?.isConnected) opener.focus({ preventScroll: true }); });
  document.body.append(dialog); dialog.showModal();
}

export function promptContextChips(contexts, { onRemove, disabled = false } = {}) {
  const strip = element("div", { className: "prompt-context-chips" });
  for (const item of contexts) {
    const preview = element("button", { className: "prompt-context-open", text: item.label,
      attrs: { type: "button", title: item.content ?? item.path ?? "", "data-context-id": item.id } });
    preview.addEventListener("click", () => previewPromptContext(item, preview));
    const chip = element("span", { className: "prompt-context-chip", attrs: { "data-context-kind": item.kind } }, [preview]);
    if (onRemove) {
      const remove = element("button", { text: "×", attrs: { type: "button",
        "aria-label": t("context.remove", { name: item.label }) } });
      remove.disabled = disabled;
      remove.addEventListener("click", () => onRemove(item.id)); chip.append(remove);
    }
    strip.append(chip);
  }
  return strip;
}
