import { setUiAttribute as writeUiAttribute, uiMessage, subscribeLocale, t } from "../../i18n.js";

import { element, toast, errorMessage, isImeKey } from "../../utils/dom.js";
import { replaceInputText } from "../../utils/text-edit.js";

import { newPromptContext } from "./prompt-context.js";

export function createComposerAddMenu({ button, input, imageInput, textInput, add, owner = () => "" }) {
  const menu = element("div", { className: "composer-add-menu", attrs: { role: "menu", id: `${input.id}-add-menu` } });
  menu.hidden = true; document.body.append(menu);
  button.setAttribute("aria-haspopup", "menu"); button.setAttribute("aria-controls", menu.id); button.setAttribute("aria-expanded", "false");
  const definitions = [["context.addImages", () => imageInput.click()], ["context.addTextFile", () => textInput.click()],
    ["context.addFile", () => insert("@")], ["context.addSession", () => insert("#")],
    ["context.addCommand", () => insert("/")], ["context.addSkill", () => insert("$")]];
  function close(focus = false) { menu.hidden = true; button.setAttribute("aria-expanded", "false"); if (focus) button.focus(); }
  function insert(prefix) {
    input.focus();
    if (["/", "$"].includes(prefix)) {
      if (input.value.trim()) { toast(t("context.commandFirst")); return; }
      replaceInputText(input, 0, input.value.length, prefix);
    } else {
      const at = input.selectionStart, before = input.value.slice(0, at);
      replaceInputText(input, at, input.selectionEnd, `${before && !/\s$/.test(before) ? " " : ""}${prefix}`);
    }
  }
  function render() {
    menu.replaceChildren(); writeUiAttribute(button, "title", uiMessage("context.add")); writeUiAttribute(button, "aria-label", t("context.add"));
    for (const [key, action] of definitions) {
      if (key === "context.addImages" && !imageInput) continue;
      const entry = element("button", { text: uiMessage(key), attrs: { type: "button", role: "menuitem" } });
      entry.addEventListener("click", () => { close(); action(); }); menu.append(entry);
    }
  }
  button.addEventListener("click", () => {
    if (button.disabled || input.disabled || input.readOnly) return;
    if (!menu.hidden) { close(true); return; }
    const box = button.getBoundingClientRect(); menu.hidden = false;
    menu.style.left = `${Math.max(8, Math.min(box.left, innerWidth - 250))}px`;
    menu.style.bottom = `${Math.max(8, innerHeight - box.top + 8)}px`;
    menu.style.maxHeight = `${Math.max(80, box.top - 16)}px`;
    button.setAttribute("aria-expanded", "true"); menu.querySelector("button")?.focus();
  });
  document.addEventListener("pointerdown", event => { if (!menu.contains(event.target) && !button.contains(event.target)) close(); });
  menu.addEventListener("keydown", event => {
    if (isImeKey(event)) return;
    if (event.key === "Escape") { event.preventDefault(); event.stopPropagation(); close(true); }
    if (["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) {
      event.preventDefault(); const items = [...menu.children], current = items.indexOf(document.activeElement);
      items[event.key === "Home" ? 0 : event.key === "End" ? items.length - 1 :
        (current + (event.key === "ArrowUp" ? -1 : 1) + items.length) % items.length]?.focus();
    }
  });
  let pickerOwner = "";
  textInput.addEventListener("click", () => { pickerOwner = owner(); });
  textInput.addEventListener("change", async () => {
    const origin = pickerOwner;
    const selected = [...(textInput.files ?? [])]; textInput.value = "";
    for (const file of selected) {
      try {
        if (owner() !== origin || input.disabled || input.readOnly) return;
        if (file.size > 49152 || !(/^(text\/|application\/(json|xml|javascript))/.test(file.type) || /\.(txt|md|csv|json|log|ya?ml|xml|[cm]?[jt]s|py|c|h|css|html|toml)$/i.test(file.name)))
          throw new Error(t("context.textFileLimit"));
        const content = new TextDecoder("utf-8", { fatal: true }).decode(await file.arrayBuffer());
        if (owner() !== origin || input.disabled || input.readOnly) return;
        if (content.includes("\0")) throw new Error(t("context.textFileLimit"));
        if (!add(newPromptContext("text", file.name.slice(0, 150), { content }))) break;
      } catch (error) { toast(errorMessage(error), "error"); }
    }
    if (owner() === origin) input.focus();
  });
  window.visualViewport?.addEventListener("resize", () => close());
  window.addEventListener("resize", () => close()); subscribeLocale(render); render();
  return { close };
}
