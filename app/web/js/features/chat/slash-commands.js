import { subscribeLocale, t } from "../../i18n.js";
import { loadCommands, subscribeCommands, commandExpansion } from "./extension-commands.js";
import { clear, element, errorMessage, isImeKey, revealListOption, toast } from "../../utils/dom.js";
import { createCompositionTracker } from "../../utils/composition.js";

const COMMANDS = Object.freeze([
  { name: "/new", descriptionKey: "slash.new", fallback: "新建任务" },
  { name: "/model", descriptionKey: "slash.model", fallback: "切换到下一个模型" },
  { name: "/fork", descriptionKey: "slash.fork", fallback: "分叉当前会话" },
  { name: "/export", descriptionKey: "slash.export", fallback: "导出当前会话 Markdown" },
  { name: "/clear", descriptionKey: "slash.clear", fallback: "清空当前会话历史" },
  { name: "/stop", descriptionKey: "slash.stop", fallback: "停止当前任务" },
  { name: "/settings", descriptionKey: "slash.settings", fallback: "打开设置" },
  { name: "/theme", descriptionKey: "slash.theme", fallback: "切换深浅主题" },
  { name: "/help", descriptionKey: "slash.help", fallback: "查看命令" },
]);

function exactCommand(value) {
  return COMMANDS.find((item) => item.name === value);
}

export function createSlashCommands({ composer, input, onExecute, contextKey = () => "" }) {
  const list = element("div", {
    className: "slash-menu",
    attrs: { id: "slash-menu", role: "listbox", "aria-label": t("slash.label", {}, "斜杠命令") },
  });
  list.hidden = true;
  composer.append(list);
  input.setAttribute("aria-controls", list.id);
  input.setAttribute("aria-haspopup", "listbox");
  input.setAttribute("aria-expanded", "false");
  let matches = [];
  let active = 0;
  const composition = createCompositionTracker(input);
  let custom = [];
  let expanding = false;
  const stopCommands = subscribeCommands(items => { custom = items; if (document.activeElement === input && input.value.startsWith("/")) update(false); });

  function hide() {
    composer.removeAttribute("data-slash-menu-open");
    matches = [];
    list.hidden = true;
    clear(list);
    input.setAttribute("aria-expanded", "false");
    input.removeAttribute("aria-activedescendant");
  }

  function render() {
    clear(list);
    list.hidden = matches.length === 0;
    composer.toggleAttribute("data-slash-menu-open", !list.hidden);
    input.setAttribute("aria-expanded", String(matches.length > 0));
    matches.forEach((command, index) => {
      const option = element("div", {
        className: "slash-option",
        attrs: { id: `slash-option-${index}`, role: "option",
          "aria-selected": String(index === active) },
      }, [
        element("code", { text: command.name }),
        element("span", { text: command.custom ? `${command.description}${command.argument_hint ? " · " + command.argument_hint : ""}` : t(command.descriptionKey, {}, command.fallback) }),
      ]);
      option.addEventListener("pointerdown", (event) => {
        event.preventDefault();
      });
      option.addEventListener("click", () => { void execute(command); });
      list.append(option);
    });
    if (matches.length) input.setAttribute("aria-activedescendant", `slash-option-${active}`);
    else input.removeAttribute("aria-activedescendant");
  }

  function moveActive(delta) {
    list.children[active]?.setAttribute("aria-selected", "false");
    active = (active + delta + matches.length) % matches.length;
    const option = list.children[active];
    option?.setAttribute("aria-selected", "true");
    input.setAttribute("aria-activedescendant", option.id);
    revealListOption(list, option);
  }

  async function execute(command) {
    if (command.custom) {
      if (expanding) return;
      const before = input.value, key = contextKey();
      expanding = true; hide();
      try {
        const result = await commandExpansion(command.id, "");
        if (input.value !== before || contextKey() !== key) return;
        input.value = result.takesArguments ? `${command.name} ` : result.text;
        input.dispatchEvent(new Event("input", { bubbles: true }));
        input.focus();
      } catch (error) { toast(errorMessage(error), "error"); }
      finally { expanding = false; }
      return;
    }
    hide();
    input.value = "";
    input.dispatchEvent(new Event("input", { bubbles: true }));
    try { await onExecute(command.name); }
    catch (error) { toast(errorMessage(error), "error"); }
  }

  function update(fetch = true) {
    if (composition.isComposing(input) || fetch?.isComposing) { hide(); return; }
    const value = input.value;
    if (!value.startsWith("/") || /[\s]/.test(value)) { hide(); return; }
    if (fetch) void loadCommands().then(items => { custom = items; if (input.value === value && document.activeElement === input) update(false); }).catch(() => {});
    matches = [...COMMANDS, ...custom].filter((command) => command.name.startsWith(value));
    if (!matches.some((command) => command.custom || command.name !== value)) { hide(); return; }
    active = 0;
    render();
  }

  input.addEventListener("compositionstart", hide);
  input.addEventListener("compositionend", () => {
    if (document.activeElement === input) update();
    else hide();
  });
  input.addEventListener("input", update);
  input.addEventListener("blur", () => {
    window.setTimeout(hide, 0);
  });
  const view = input.ownerDocument?.defaultView;
  view?.addEventListener("blur", hide);
  subscribeLocale(() => {
    list.setAttribute("aria-label", t("slash.label", {}, "斜杠命令"));
    if (!list.hidden) render();
  });
  return Object.freeze({
    onKeyDown(event) {
      if (list.hidden || isImeKey(event, composition.isComposing(input))) return false;
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
        event.preventDefault();
        void execute(matches[active]);
        return true;
      }
      return false;
    },
    async expandIntoComposer(value) {
      const match = /^\/([a-z0-9_.-]+)(?:\s+([\s\S]*))?$/.exec(value);
      if (!match || exactCommand(`/${match[1]}`)) return false;
      const key = contextKey();
      const commands = await loadCommands();
      const command = commands.find(item => item.id === match[1]);
      if (!command) return false;
      const result = await commandExpansion(command.id, match[2] || "");
      if (input.value !== value || contextKey() !== key) return true;
      hide(); input.value = result.text;
      input.dispatchEvent(new Event("input", { bubbles: true })); input.focus();
      return true;
    },
    destroy() { stopCommands(); composition.dispose(); view?.removeEventListener("blur", hide); hide(); },
    consumeExact(value) {
      const command = exactCommand(value);
      if (!command) return false;
      void execute(command);
      return true;
    },
    isExact: (value) => Boolean(exactCommand(value)),
    hide,
  });
}
