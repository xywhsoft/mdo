import { clear, element, errorMessage, isImeKey, revealListOption, toast } from "../../utils/dom.js";
import { subscribeLocale } from "../../i18n.js";
import { sessionActionItems } from "./session-actions.js";

export function createSessionActionMenu({ control, button, menu, navigation,
  store, onAction }) {
  let renderedKey = null;

  function currentSession() {
    const route = navigation.get();
    const session = store.get().data;
    return route.view === "workspace" && route.projectId === session?.project_id &&
      route.sessionId === session?.id ? session : null;
  }

  function close(restoreFocus = false) {
    if (menu.hidden) return;
    menu.hidden = true;
    button.setAttribute("aria-expanded", "false");
    if (restoreFocus && !button.disabled) button.focus({ preventScroll: true });
  }

  function items() {
    return [...menu.querySelectorAll('[role="menuitem"]')];
  }

  function render() {
    const session = currentSession();
    button.disabled = !session;
    if (!session) close();
    const actions = session ? sessionActionItems(session) : [];
    // Polling can change session metadata while the user reads this menu.
    // Only replace its DOM when the visible action set or session changes.
    const contentKey = session ? JSON.stringify([session.project_id, session.id,
      actions.map(({ name, label, tone }) => [name, label, tone])]) : "";
    if (contentKey === renderedKey) return;
    renderedKey = contentKey;
    const focusedAction = menu.contains(document.activeElement)
      ? document.activeElement.dataset.sessionAction : "";
    clear(menu);
    if (!session) return;
    for (const action of actions) {
      const item = element("button", { text: action.label, attrs: {
        type: "button", role: "menuitem", "data-session-action": action.name,
        "data-tone": action.tone ?? "neutral",
      } });
      item.addEventListener("click", async () => {
        const selected = currentSession();
        if (!selected) return;
        close();
        button.focus({ preventScroll: true });
        try { await onAction(action.name, selected); }
        catch (error) { toast(errorMessage(error), "error"); }
      });
      menu.append(item);
    }
    if (focusedAction) {
      const replacement = items().find((item) => item.dataset.sessionAction === focusedAction);
      (replacement ?? button).focus({ preventScroll: true });
    }
  }

  function open(index = -1) {
    if (!currentSession()) return;
    const wasHidden = menu.hidden;
    render();
    menu.hidden = false;
    button.setAttribute("aria-expanded", "true");
    if (wasHidden) menu.scrollTop = 0;
    if (index >= 0) focusItem(items()[index]);
  }

  function focusItem(item) {
    if (!item) return;
    item.focus({ preventScroll: true });
    revealListOption(menu, item);
  }

  function onButtonClick() {
    if (menu.hidden) open();
    else close();
  }

  function onButtonKeyDown(event) {
    if (isImeKey(event) ||
        !["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key) ||
        (menu.hidden && (event.key === "Home" || event.key === "End"))) return;
    const session = currentSession();
    if (!session) return;
    event.preventDefault();
    const entries = sessionActionItems(session);
    open(event.key === "ArrowDown" || event.key === "Home"
      ? 0 : entries.length - 1);
  }

  function onMenuKeyDown(event) {
    if (isImeKey(event)) return;
    const entries = items();
    const index = entries.indexOf(document.activeElement);
    let next = index;
    if (event.key === "ArrowDown") next = (index + 1) % entries.length;
    else if (event.key === "ArrowUp") next = (index - 1 + entries.length) % entries.length;
    else if (event.key === "Home") next = 0;
    else if (event.key === "End") next = entries.length - 1;
    else return;
    event.preventDefault();
    focusItem(entries[next]);
  }

  function onDocumentKeyDown(event) {
    if (menu.hidden || event.key !== "Escape" || isImeKey(event)) return;
    event.preventDefault();
    event.stopImmediatePropagation();
    close(true);
  }

  function onDocumentPointerDown(event) {
    if (!menu.hidden && !control.contains(event.target)) close();
  }

  function onDocumentFocusIn(event) {
    if (!menu.hidden && !control.contains(event.target)) close();
  }

  button.addEventListener("click", onButtonClick);
  button.addEventListener("keydown", onButtonKeyDown);
  menu.addEventListener("keydown", onMenuKeyDown);
  document.addEventListener("keydown", onDocumentKeyDown, true);
  document.addEventListener("pointerdown", onDocumentPointerDown);
  document.addEventListener("focusin", onDocumentFocusIn);
  const unsubscribeStore = store.subscribe(render);
  const unsubscribeNavigation = navigation.subscribe(render);
  const unsubscribeLocale = subscribeLocale(render);

  return Object.freeze({
    close,
    destroy() {
      unsubscribeStore();
      unsubscribeNavigation();
      unsubscribeLocale();
      document.removeEventListener("keydown", onDocumentKeyDown, true);
      document.removeEventListener("pointerdown", onDocumentPointerDown);
      document.removeEventListener("focusin", onDocumentFocusIn);
      button.removeEventListener("click", onButtonClick);
      button.removeEventListener("keydown", onButtonKeyDown);
      menu.removeEventListener("keydown", onMenuKeyDown);
    },
  });
}
