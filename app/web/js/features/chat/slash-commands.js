import { clear, element, errorMessage, toast } from "../../utils/dom.js";

const COMMANDS = Object.freeze([
  { name: "/new", description: "新建任务" },
  { name: "/model", description: "选择模型" },
  { name: "/fork", description: "分叉当前会话" },
  { name: "/export", description: "导出当前会话 Markdown" },
  { name: "/clear", description: "清空当前会话历史" },
  { name: "/stop", description: "停止当前任务" },
  { name: "/settings", description: "打开设置" },
  { name: "/theme", description: "打开外观设置" },
  { name: "/help", description: "查看命令" },
]);

export function createSlashCommands({ composer, input, onExecute }) {
  const list = element("div", {
    className: "slash-menu",
    attrs: { id: "slash-menu", role: "listbox", "aria-label": "斜杠命令" },
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
        element("span", { text: command.description }),
      ]);
      option.addEventListener("pointerdown", (event) => {
        event.preventDefault();
        void execute(command);
      });
      list.append(option);
    });
    input.setAttribute("aria-activedescendant", `slash-option-${active}`);
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
