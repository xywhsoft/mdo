import { subscribeLocale, t } from "../../i18n.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";

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

export function createSlashCommands({ composer, input, onExecute }) {
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

  function hide() {
    matches = [];
    list.hidden = true;
    clear(list);
    input.setAttribute("aria-expanded", "false");
    input.removeAttribute("aria-activedescendant");
  }

  function render() {
    clear(list);
    list.hidden = matches.length === 0;
    input.setAttribute("aria-expanded", String(matches.length > 0));
    matches.forEach((command, index) => {
      const option = element("div", {
        className: "slash-option",
        attrs: { id: `slash-option-${index}`, role: "option",
          "aria-selected": String(index === active) },
      }, [
        element("code", { text: command.name }),
        element("span", { text: t(command.descriptionKey, {}, command.fallback) }),
      ]);
      option.addEventListener("pointerdown", (event) => {
        event.preventDefault();
        void execute(command);
      });
      list.append(option);
    });
    if (matches.length) input.setAttribute("aria-activedescendant", `slash-option-${active}`);
    else input.removeAttribute("aria-activedescendant");
  }

  async function execute(command) {
    hide();
    input.value = "";
    input.dispatchEvent(new Event("input", { bubbles: true }));
    try { await onExecute(command.name); }
    catch (error) { toast(errorMessage(error), "error"); }
  }

  function update() {
    const value = input.value;
    if (!value.startsWith("/") || /[\s]/.test(value)) { hide(); return; }
    matches = COMMANDS.filter((command) => command.name.startsWith(value));
    active = 0;
    render();
  }

  input.addEventListener("input", update);
  input.addEventListener("blur", () => window.setTimeout(hide, 0));
  subscribeLocale(() => {
    list.setAttribute("aria-label", t("slash.label", {}, "斜杠命令"));
    if (!list.hidden) render();
  });
  return Object.freeze({
    onKeyDown(event) {
      if (list.hidden || event.isComposing) return false;
      if (event.key === "Escape") {
        event.preventDefault(); hide(); return true;
      }
      if (event.key === "ArrowDown" || event.key === "ArrowUp") {
        event.preventDefault();
        active = (active + (event.key === "ArrowDown" ? 1 : -1) +
          matches.length) % matches.length;
        render();
        return true;
      }
      if (event.key === "Enter" && !event.shiftKey && !event.ctrlKey &&
          !event.metaKey) {
        event.preventDefault();
        void execute(matches[active]);
        return true;
      }
      return false;
    },
    async consumeExact(value) {
      const command = COMMANDS.find((item) => item.name === value);
      if (!command) return false;
      await execute(command);
      return true;
    },
    hide,
  });
}
