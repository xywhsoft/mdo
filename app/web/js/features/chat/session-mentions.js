import { uiMessage, t } from "../../i18n.js";
import { element, isImeKey, toast, errorMessage, revealListOption } from "../../utils/dom.js";

import { subscribeTarget, targetState } from "../../api/target.js";
import { currentPageWriteToken } from "../../api/client.js";
import { loadSessionTranscript } from "../../state/sessions.js";
import { createRequestRecovery } from "../../api/request-recovery.js";
import { eventsToTimeline } from "./timeline.js";
import { newPromptContext, promptContextText } from "./prompt-context.js";

export function sessionContextSnapshot(session, transcript) {
  const lines = eventsToTimeline(transcript.events ?? []).filter(item => ["user", "assistant"].includes(item.kind) && item.text)
    .map(item => `${item.kind === "user" ? "User" : "Assistant"}: ${promptContextText(item.text)}`);
  if (!lines.length) throw new Error(t("context.emptySession"));
  if (transcript.historyLost || transcript.limitReached || transcript.textTruncated)
    lines.unshift(t("context.partialSession"));
  return newPromptContext("session", session.title || session.id, { project: session.project_id,
    session: session.id, content: lines.join("\n\n") });
}

const epoch = () => JSON.stringify([targetState().selected, targetState().runtimeChanged, currentPageWriteToken()]);
export function createSessionMentions({ composer, input, navigation, sessionsStore, add }) {
  const slashMenuId = input.id && input.id !== "prompt" ? `${input.id}-slash-menu` : "slash-menu";
  const menu = element("div", { className: "file-mention-menu session-mention-menu", attrs: {
    role: "listbox", id: `session-mentions-${input.id}`, "aria-label": uiMessage("context.sessions") } });
  menu.hidden = true; composer.append(menu);
  let choices = [], active = 0, generation = 0, recovery = null, loading = false;
  const token = () => input.selectionStart === input.selectionEnd && /(?:^|\s)#([^\n#]*)$/.exec(input.value.slice(0, input.selectionStart));
  function hide() {
    generation++; recovery?.dispose(); recovery = null; loading = false; choices = []; menu.hidden = true; menu.replaceChildren();
    if (input.getAttribute("aria-controls") === menu.id) {
      input.setAttribute("aria-controls", slashMenuId); input.setAttribute("aria-expanded", "false"); input.removeAttribute("aria-activedescendant");
    }
  }
  async function select(session) {
    if (loading || !token() || input.disabled || input.readOnly) return;
    loading = true;
    const serial = ++generation, owner = JSON.stringify(navigation.get()), origin = epoch();
    const before = input.value, caret = input.selectionStart, found = token();
    const start = caret - found[0].length + (found[0][0] === "#" ? 0 : 1);
    recovery = createRequestRecovery();
    menu.replaceChildren(element("p", { text: uiMessage("context.readingSession") }));
    try {
      const transcript = await loadSessionTranscript(session, { createRecovery: () => recovery });
      if (serial !== generation || origin !== epoch() || owner !== JSON.stringify(navigation.get()) || input.value !== before) return;
      const context = sessionContextSnapshot(session, transcript);
      add(context, { start, end: caret }); hide(); input.focus();
    } catch (error) { if (serial === generation && error?.name !== "AbortError") { hide(); toast(errorMessage(error), "error"); } }
    finally { if (serial === generation) { loading = false; recovery?.dispose(); recovery = null; } }
  }
  function update(event) {
    if (loading) { if (event?.type === "input") hide(); return; }
    const found = token();
    if (!found || isImeKey(event ?? {}) || input.disabled || input.readOnly || navigation.get().view !== "workspace") { hide(); return; }
    const needle = found[1].trim().toLocaleLowerCase();
    const route = navigation.get();
    choices = (sessionsStore.get().data?.items ?? []).filter(session => session.status !== "trash" &&
      !(session.id === route.sessionId && session.project_id === route.projectId) &&
      `${session.title} ${session.project_id}`.toLocaleLowerCase().includes(needle)).slice(0, 8);
    menu.replaceChildren(); active = 0;
    menu.hidden = false; input.setAttribute("aria-controls", menu.id); input.setAttribute("aria-expanded", "true");
    if (!choices.length) menu.append(element("p", { text: uiMessage("context.noSessions") }));
    choices.forEach((session, index) => {
      const option = element("div", { className: "file-mention-option", text: `${session.title || session.id} · ${session.project_id}`,
        attrs: { role: "option", id: `${menu.id}-${index}`, "aria-selected": String(index === active) } });
      option.addEventListener("pointerdown", event => event.preventDefault());
      option.addEventListener("click", () => void select(session)); menu.append(option);
    });
    if (choices.length) input.setAttribute("aria-activedescendant", `${menu.id}-0`);
  }
  input.addEventListener("input", update); input.addEventListener("click", update);
  input.addEventListener("compositionstart", hide); input.addEventListener("compositionend", update);
  input.addEventListener("blur", () => { if (!loading) setTimeout(hide, 0); });
  navigation.subscribe(hide); subscribeTarget(hide);
  return { hide, onKeyDown(event) {
    if (menu.hidden || isImeKey(event)) return false;
    if (event.key === "Escape") { event.preventDefault(); event.stopPropagation(); hide(); return true; }
    if (["ArrowUp", "ArrowDown", "Enter", "Tab"].includes(event.key) && !event.shiftKey && !event.ctrlKey && !event.metaKey && !event.altKey) {
      event.preventDefault();
      if (loading || !choices.length) return true;
      if (["Enter", "Tab"].includes(event.key)) void select(choices[active]);
      else {
        active = (active + (event.key === "ArrowUp" ? -1 : 1) + choices.length) % choices.length;
        [...menu.children].forEach((node, index) => node.setAttribute("aria-selected", String(active === index)));
        input.setAttribute("aria-activedescendant", `${menu.id}-${active}`); revealListOption(menu, menu.children[active]);
      }
      return true;
    }
    return false;
  } };
}
