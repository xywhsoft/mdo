import { currentLocale, subscribeLocale, t } from "../../i18n.js";
import { element, formatRelativeTime, isImeKey, revealListOption } from "../../utils/dom.js";
import { createCompositionTracker } from "../../utils/composition.js";
import { matchingSessions } from "./sidebar-tree.js";

const PAGE_SIZE = 40;

// This dialog reads the current device's catalog and owns its query. Opening
// and closing it must not filter the sidebar or change the current task.
export function createSessionSearch({ dialog, button, store, projectsStore,
  getStatus = () => "active", onSelect, onRetry, fallbackFocus }) {
  const input = dialog.querySelector("#session-search");
  const list = dialog.querySelector("#session-search-results");
  const status = dialog.querySelector("#session-search-status");
  const retry = dialog.querySelector("#retry-session-search");
  const more = dialog.querySelector("#more-session-search");
  const dismiss = dialog.querySelector("#close-session-search");
  const composition = createCompositionTracker(input);
  let previousFocus = null;
  let matches = [], active = -1, limit = PAGE_SIZE;
  let renderedKey = null;

  const key = session => `${session.project_id}/${session.id}`;
  const options = () => [...list.querySelectorAll('[role="option"]')];

  function setActive(index, reveal = false) {
    active = index;
    const rows = options();
    for (let i = 0; i < rows.length; i++)
      rows[i].setAttribute("aria-selected", String(i === active));
    if (rows[active]) input.setAttribute("aria-activedescendant", rows[active].id);
    else input.removeAttribute("aria-activedescendant");
    if (reveal) revealListOption(list, rows[active]);
  }

  function render() {
    if (!dialog.open) return;
    const snapshot = store.get();
    const projects = projectsStore.get().data?.items ?? [];
    const projectNames = new Map(projects.map(project => [project.id, project.name]));
    const selectedKey = matches[active] && key(matches[active]);
    matches = matchingSessions({ sessions: snapshot.data?.items, projects,
      status: getStatus(), query: input.value, locale: currentLocale(),
      defaultProjectName: t("nav.defaultProject") });
    const retained = matches.findIndex(session => key(session) === selectedKey);
    active = matches.length ? (retained >= 0 && retained < limit ? retained : 0) : -1;
    const loading = !snapshot.updatedAt && !snapshot.data?.items?.length &&
      ["idle", "loading", "refreshing"].includes(snapshot.status);
    status.textContent = snapshot.status === "error" ? t("sessionList.readFailed")
      : loading ? t("sessionSearch.loading")
      : !matches.length ? t(input.value.trim() ? "sessionSearch.noMatches" : "sessionSearch.empty")
      : input.value.trim() ? t("sessionSearch.results", { count: matches.length })
      : t("sessionSearch.recent");
    retry.hidden = snapshot.status !== "error";
    retry.disabled = Boolean(store.isPending?.());
    retry.textContent = t(retry.disabled ? "sessionLoad.retrying" : "sessionLoad.retry");
    list.setAttribute("aria-busy", String(loading));
    more.hidden = matches.length <= limit;
    const rows = matches.slice(0, limit).map(session => ({ session,
      title: session.title || t("nav.untitled"),
      project: session.project_id === "default" ? t("nav.defaultProject")
        : projectNames.get(session.project_id) || session.project_id }));
    // Ordinary catalog polling must not reset scroll or replace the selected
    // option while the user reads the results or uses the arrow keys.
    const nextKey = JSON.stringify([currentLocale(), rows.map(({ session, title, project }) =>
      [key(session), title, project, session.updated_at])]);
    if (nextKey !== renderedKey) {
      renderedKey = nextKey;
      const hadOptionFocus = list.contains(document.activeElement);
      list.replaceChildren(...rows.map(({ session, title, project }, index) => {
        const row = element("button", { className: "session-search-result", attrs: {
          type: "button", role: "option", tabindex: "-1", id: `session-search-option-${index}`,
        } }, [element("span", { className: "session-search-result-copy" }, [
          element("strong", { text: title }), element("small", { text: project }),
        ]), element("span", { className: "session-search-result-time",
          text: formatRelativeTime(session.updated_at),
          attrs: { "data-relative-time": session.updated_at } })]);
        row.addEventListener("click", () => select(key(session)));
        row.addEventListener("focus", () => setActive(index));
        return row;
      }));
      if (hadOptionFocus) input.focus({ preventScroll: true });
    }
    setActive(active);
  }

  function finishClose(restoreFocus = true) {
    const origin = previousFocus;
    previousFocus = null;
    composition.reset();
    button.setAttribute("aria-expanded", "false");
    input.setAttribute("aria-expanded", "false");
    input.removeAttribute("aria-activedescendant");
    const reachable = node => node?.isConnected && node.getClientRects().length &&
      !node.matches(":disabled") && !node.closest("[hidden], [inert]");
    if (restoreFocus && origin) {
      const target = reachable(origin) ? origin : reachable(button) ? button : fallbackFocus?.();
      target?.focus({ preventScroll: true });
    }
  }

  function close(restoreFocus = true) {
    if (!dialog.open) return;
    dialog.close();
    finishClose(restoreFocus);
  }

  function select(selectedKey) {
    // Resolve against the latest catalog, so a removed or archived search
    // result cannot navigate using a stale row retained by a pointer event.
    render();
    const selected = matches.find(session => key(session) === selectedKey);
    if (!selected) return;
    close(false);
    onSelect(selected);
  }

  function open() {
    if (document.querySelector("dialog[open]") && !dialog.open) return;
    if (!dialog.open) {
      previousFocus = document.activeElement;
      limit = PAGE_SIZE;
      renderedKey = null;
      matches = [];
      dialog.showModal();
      list.scrollTop = 0;
    }
    button.setAttribute("aria-expanded", "true");
    input.setAttribute("aria-expanded", "true");
    render();
    input.focus();
    input.select();
    if (store.get().status === "idle" && !store.isPending?.()) void onRetry?.();
  }

  function onInput() {
    active = -1;
    matches = [];
    limit = PAGE_SIZE;
    list.scrollTop = 0;
    render();
  }

  function onKeyDown(event) {
    if (isImeKey(event, composition.isComposing(input))) {
      // Some WebViews clear isComposing before reporting the IME's Escape.
      if (event.key === "Escape") event.preventDefault();
      return;
    }
    if (event.key === "Escape") {
      event.preventDefault();
      event.stopPropagation();
      close();
      return;
    }
    if (event.ctrlKey || event.metaKey || event.altKey || event.shiftKey ||
        (event.target !== input && !list.contains(event.target))) return;
    const count = Math.min(matches.length, limit);
    if (["ArrowDown", "ArrowUp"].includes(event.key) && count) {
      event.preventDefault();
      input.focus({ preventScroll: true });
      setActive((active + (event.key === "ArrowDown" ? 1 : -1) + count) % count, true);
    } else if (event.key === "Enter") {
      event.preventDefault();
      if (matches[active]) select(key(matches[active]));
    }
  }

  function onCancel(event) {
    event.preventDefault();
    if (!composition.isComposing(input)) close();
  }
  function onClose() { if (!dialog.open) finishClose(); }
  function onDismiss() { close(); }
  function onRetryRead() { if (!store.isPending?.()) void onRetry?.(); }
  function onMore() {
    limit += PAGE_SIZE;
    render();
    if (more.hidden) input.focus({ preventScroll: true });
  }
  function onBackdrop(event) {
    if (event.target !== dialog) return;
    const bounds = dialog.getBoundingClientRect();
    if (event.clientX < bounds.left || event.clientX > bounds.right ||
        event.clientY < bounds.top || event.clientY > bounds.bottom) close();
  }

  button.addEventListener("click", open);
  dismiss.addEventListener("click", onDismiss);
  retry.addEventListener("click", onRetryRead);
  more.addEventListener("click", onMore);
  input.addEventListener("input", onInput);
  dialog.addEventListener("keydown", onKeyDown);
  dialog.addEventListener("cancel", onCancel);
  dialog.addEventListener("close", onClose);
  dialog.addEventListener("click", onBackdrop);
  const unsubscribeStore = store.subscribe(render);
  const unsubscribeProjects = projectsStore.subscribe(render);
  const unsubscribeLocale = subscribeLocale(render);
  return Object.freeze({ open, close, isOpen: () => dialog.open,
    destroy() {
      close(false);
      unsubscribeStore(); unsubscribeProjects(); unsubscribeLocale();
      composition.dispose();
      button.removeEventListener("click", open);
      dismiss.removeEventListener("click", onDismiss);
      retry.removeEventListener("click", onRetryRead);
      more.removeEventListener("click", onMore);
      input.removeEventListener("input", onInput);
      dialog.removeEventListener("keydown", onKeyDown);
      dialog.removeEventListener("cancel", onCancel);
      dialog.removeEventListener("close", onClose);
      dialog.removeEventListener("click", onBackdrop);
    },
  });
}
