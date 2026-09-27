import { api } from "../../api/client.js";
import { subscribeLocale, t } from "../../i18n.js";
import { clear, element, isImeKey, revealListOption } from "../../utils/dom.js";

const WAIT_MS = 180;
const VISIBLE_MAX = 8;

function mentionAtCaret(input) {
  if (input.selectionStart !== input.selectionEnd) return null;
  const caret = input.selectionStart;
  const before = input.value.slice(0, caret);
  const match = /(?:^|\s)@([^\s@]*)$/.exec(before);
  if (!match || !match[1] || match[1].length > 128 || before.startsWith("/")) return null;
  // Completing from the middle of an existing reference must replace its
  // remaining suffix too; otherwise @alpha.c becomes @src/alpha.c ha.c.
  const suffix = /^[^\s@]*/.exec(input.value.slice(caret))?.[0] ?? "";
  return { start: caret - match[0].length + (match[0][0] === "@" ? 0 : 1),
    end: caret + suffix.length, query: match[1] };
}

function fileReference(path) {
  return /\s/.test(path) ? `@"${path.replaceAll("\\", "\\\\").replaceAll('"', '\\"')}"` : `@${path}`;
}

export function createFileMentions({ composer, input, navigation }) {
  const list = element("div", {
    className: "file-mention-menu",
    attrs: { id: "file-mention-menu", role: "listbox", "aria-label": t("mention.label", {}, "工作区文件") },
  });
  list.hidden = true;
  composer.append(list);
  let choices = [];
  let active = 0;
  let current = null;
  let timer = 0;
  let controller = null;
  let serial = 0;
  let composing = false;

  function hide() {
    composer.removeAttribute("data-file-menu-open");
    window.clearTimeout(timer);
    controller?.abort();
    controller = null;
    serial += 1;
    current = null;
    choices = [];
    list.hidden = true;
    clear(list);
    if (input.getAttribute("aria-controls") === list.id) {
      input.setAttribute("aria-controls", "slash-menu");
      input.setAttribute("aria-expanded", "false");
      input.removeAttribute("aria-activedescendant");
    }
  }

  function render() {
    clear(list);
    list.hidden = choices.length === 0;
    composer.toggleAttribute("data-file-menu-open", !list.hidden);
    if (list.hidden) {
      if (input.getAttribute("aria-controls") === list.id) {
        input.setAttribute("aria-controls", "slash-menu");
        input.setAttribute("aria-expanded", "false");
        input.removeAttribute("aria-activedescendant");
      }
      return;
    }
    input.setAttribute("aria-controls", list.id);
    input.setAttribute("aria-expanded", "true");
    choices.forEach((path, index) => {
      const option = element("div", {
        className: "file-mention-option",
        attrs: { id: `file-mention-option-${index}`, role: "option",
          "aria-selected": String(index === active) },
      }, [element("span", { text: path })]);
      option.addEventListener("pointerdown", (event) => {
        event.preventDefault();
      });
      option.addEventListener("click", () => insert(path));
      list.append(option);
    });
    input.setAttribute("aria-activedescendant", `file-mention-option-${active}`);
  }

  function moveActive(delta) {
    list.children[active]?.setAttribute("aria-selected", "false");
    active = (active + delta + choices.length) % choices.length;
    const option = list.children[active];
    option?.setAttribute("aria-selected", "true");
    input.setAttribute("aria-activedescendant", option.id);
    revealListOption(list, option);
  }

  function insert(path) {
    const token = mentionAtCaret(input);
    if (!token || !choices.includes(path)) { hide(); return; }
    const hasSeparator = /\s/.test(input.value[token.end] ?? "");
    const replacement = fileReference(path) + (hasSeparator ? "" : " ");
    input.setRangeText(replacement, token.start, token.end, "end");
    // Reuse an existing separator without leaving the caret inside the mention.
    if (hasSeparator) input.setSelectionRange(input.selectionStart + 1,
      input.selectionStart + 1);
    hide();
    input.dispatchEvent(new Event("input", { bubbles: true }));
    input.focus();
  }

  function update() {
    if (composing) { hide(); return; }
    const token = mentionAtCaret(input);
    const selected = navigation.get();
    if (!token || selected.view !== "workspace" || !selected.projectId) {
      hide(); return;
    }
    const key = `${selected.projectId}/${selected.sessionId}/${token.query}`;
    if (current === key) return;
    hide();
    current = key;
    const generation = serial;
    timer = window.setTimeout(async () => {
      timer = 0;
      controller = new AbortController();
      try {
        const scope = selected.sessionId
          ? `/projects/${selected.projectId}/sessions/${selected.sessionId}`
          : `/projects/${selected.projectId}`;
        const result = await api.get(
          `${scope}/workspace/files?q=${encodeURIComponent(token.query)}`,
          { signal: controller.signal });
        if (serial !== generation || current !== key ||
            mentionAtCaret(input)?.query !== token.query) return;
        choices = (Array.isArray(result.data?.items) ? result.data.items : [])
          .filter((path) => typeof path === "string").slice(0, VISIBLE_MAX);
        active = 0;
        render();
      } catch (error) {
        if (error?.name !== "AbortError" && serial === generation) hide();
      } finally {
        if (serial === generation) controller = null;
      }
    }, WAIT_MS);
  }

  input.addEventListener("compositionstart", () => { composing = true; hide(); });
  input.addEventListener("compositionend", () => {
    composing = false;
    if (document.activeElement === input) update();
    else hide();
  });
  input.addEventListener("input", update);
  input.addEventListener("click", update);
  input.addEventListener("keyup", (event) => {
    if (["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) update();
  });
  input.addEventListener("blur", () => {
    composing = false;
    window.setTimeout(hide, 0);
  });
  navigation.subscribe(() => { hide(); update(); });
  subscribeLocale(() => list.setAttribute("aria-label", t("mention.label", {}, "工作区文件")));

  return Object.freeze({
    hide,
    onKeyDown(event) {
      if (list.hidden || isImeKey(event, composing)) return false;
      if (event.key === "Escape") {
        event.preventDefault();
        event.stopPropagation();
        hide(); return true;
      }
      if (event.key === "ArrowDown" || event.key === "ArrowUp") {
        event.preventDefault();
        moveActive(event.key === "ArrowDown" ? 1 : -1);
        return true;
      }
      if ((event.key === "Enter" || event.key === "Tab") &&
          !event.shiftKey && !event.ctrlKey && !event.metaKey && !event.altKey) {
        event.preventDefault(); insert(choices[active]); return true;
      }
      return false;
    },
  });
}
