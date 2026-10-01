// Disabling an editor moves browser focus to the document body. Keep that
// focus only across the owned new-task migration; newer user choices win.
export function createNewTaskComposerFocus({ prompt, navigation }) {
  let pending = null;

  const project = (route) => route.projectId || navigation.preferredProject?.() || "default";
  function matches(route) {
    return route.view === "workspace" && project(route) === pending?.projectId &&
      (route.sessionId || "") === pending?.sessionId;
  }
  function cancel() { pending = null; }
  function onFocus(event) {
    if (pending && event.target !== prompt && event.target !== document.body) cancel();
  }
  function onPointer(event) {
    // A click on non-focusable content is also an intentional focus change.
    if (pending && event.target !== prompt) cancel();
  }
  function onVisibility() { if (document.hidden) cancel(); }

  const unsubscribe = navigation.subscribe((route) => {
    if (pending && !matches(route)) cancel();
  });
  document.addEventListener("focusin", onFocus);
  document.addEventListener("pointerdown", onPointer, true);
  document.addEventListener("visibilitychange", onVisibility);
  document.defaultView?.addEventListener("blur", cancel);

  return Object.freeze({
    capture() {
      const route = navigation.get();
      if (pending || route.view !== "workspace" || route.sessionId ||
          prompt.disabled || document.activeElement !== prompt) return;
      pending = { projectId: project(route), sessionId: "" };
    },
    migrate(projectId, sessionId) {
      if (!pending || !matches(navigation.get()) || pending.projectId !== projectId) return;
      pending.sessionId = sessionId;
    },
    restore() {
      if (!pending?.sessionId || !matches(navigation.get()) ||
          prompt.disabled || !prompt.isConnected) return false;
      const active = document.activeElement;
      const owned = active === prompt || active === document.body || !active?.isConnected;
      cancel();
      if (!owned) return false;
      prompt.focus({ preventScroll: true });
      return true;
    },
    cancel,
    destroy() {
      cancel();
      unsubscribe();
      document.removeEventListener("focusin", onFocus);
      document.removeEventListener("pointerdown", onPointer, true);
      document.removeEventListener("visibilitychange", onVisibility);
      document.defaultView?.removeEventListener("blur", cancel);
    },
  });
}
