import { subscribeLocale, t } from "../../i18n.js";
import { loadCommands, subscribeCommands, parseCommand, runCommand, cancelCommand } from "./extension-commands.js";
import { loadSelectableSkills, parseSkillSelection, expandSkillSelection } from "./skill-selection.js";
import { clear, element, errorMessage, isImeKey, revealListOption, toast } from "../../utils/dom.js";
import { createCompositionTracker } from "../../utils/composition.js";
import { subscribeTarget } from "../../api/target.js";
import { presentCommandResult } from "./command-results.js";

export function createSlashCommands({ composer, input, onExecute, contextKey = () => "", skillProject = () => "default", commandScope = () => ({ projectId: skillProject() }), onPendingChange = () => {} }) {
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
  const pending = new Map();
  const selections = new Set();
  let skills = [], lookup = 0;
  const stopTarget = subscribeTarget(() => { skills = []; ++lookup; hide(); });
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
        element("span", { text: `${command.scope === "builtin" ? t(`slash.${command.id}`, {}, command.description) : command.description}${command.argument_hint ? " · " + command.argument_hint : ""}` }),
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
    if (command.skill) {
      hide(); input.value = input.value.startsWith("/skill:") ? `/skill:${command.id} ` : `$${command.id} `;
      input.dispatchEvent(new Event("input", { bubbles: true })); input.focus(); return;
    }
    if (command.argument_hint && !command.argument_hint.startsWith("[")) {
      hide(); input.value = `${command.name} `;input.dispatchEvent(new Event("input", { bubbles: true }));input.focus();return;
    }
    await executeValue(command.name);
  }

  async function executeValue(value) {
    const selection = parseCommand(value);if (!selection) return false;
    const before = input.value, key = contextKey(), scope = { ...commandScope() };
    const selectionKey=JSON.stringify([key,value]);if(selections.has(selectionKey))return true;
    selections.add(selectionKey);
    hide();let cleared = false;
    try {
      const command = (await loadCommands(scope.projectId, { refresh: true })).find(item => item.id === selection.id);
      if (!command) throw new Error(`命令 /${selection.id} 不存在、已停用或配置无效。`);
      if (key !== contextKey()) return true;
      const result = await runCommand(command, selection.raw, scope, { onPending(id, target, done) {
        if (done) pending.delete(id);else pending.set(id, target);onPendingChange(pending.size);
      } });
      if (key !== contextKey()) { presentCommandResult(result);return true; }
      if (input.value === before) { input.value = "";cleared = true; input.dispatchEvent(new Event("input", { bubbles: true })); }
      await onExecute(result, scope);
      presentCommandResult(result,{input,isCurrent:()=>key===contextKey()});
    } catch (error) {
      if (cleared && key === contextKey() && !input.value) { input.value = before;input.dispatchEvent(new Event("input", { bubbles: true })); }
      if (error?.name !== "AbortError") toast(errorMessage(error), "error");
    } finally { selections.delete(selectionKey); }
    return true;
  }

  function update(fetch = true) {
    if (composition.isComposing(input) || fetch?.isComposing) { hide(); return; }
    const value = input.value;
    if ((!value.startsWith("/") && !value.startsWith("$")) || /[\s]/.test(value)) { ++lookup; hide(); return; }
    if (fetch) {
      skills = [];
      const serial = ++lookup, key = contextKey();
      void loadSelectableSkills(skillProject()).then(items => {
        if (serial !== lookup || key !== contextKey() || input.value !== value) return;
        skills = items; update(false);
      }).catch(() => {});
    }
    if (fetch) void loadCommands(skillProject()).then(items => { custom = items; if (input.value === value && document.activeElement === input) update(false); }).catch(() => {});
    matches = [...custom, ...skills.map(item => ({...item,
      name: value.startsWith("/") ? `/skill:${item.id}` : `$${item.id}`}))]
      .filter((command) => command.name.startsWith(value));
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
      const skill = parseSkillSelection(value);
      if (skill) {
        const key = contextKey();
        const text = await expandSkillSelection(skill, skillProject());
        if (input.value !== value || contextKey() !== key) return true;
        hide(); input.value = text;
        input.dispatchEvent(new Event("input", { bubbles: true })); input.focus(); return true;
      }
      return executeValue(value);
    },
    destroy() { stopTarget(); stopCommands(); composition.dispose(); view?.removeEventListener("blur", hide); hide(); },
    consumeExact(value) {
      if (!parseCommand(value)) return false;
      void executeValue(value);
      return true;
    },
    isExact: value => Boolean(parseCommand(value)),
    pendingCount: () => pending.size,
    async cancel(except = "") {
      const work = [...pending].filter(([id]) => id !== except);
      await Promise.all(work.map(([id, target]) => cancelCommand(id, target)));return work.length;
    },
    hide,
  });
}
