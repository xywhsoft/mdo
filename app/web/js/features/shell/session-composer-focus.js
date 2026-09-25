// A pointer selection can replace its sidebar button while the session loads.
// Focus the composer only if the user has not moved to another control.
export function focusSessionComposerAfterNavigation({ navigation, sessionDetailStore,
  prompt, projectId, sessionId, origin }) {
  const key = `${projectId}/${sessionId}`;
  let finished = false;
  let unsubscribeRoute = () => {};
  let unsubscribeDetail = () => {};

  function finish() {
    if (finished) return;
    finished = true;
    unsubscribeRoute();
    unsubscribeDetail();
  }

  unsubscribeRoute = navigation.subscribe((route) => {
    if (route.view !== "workspace" || `${route.projectId}/${route.sessionId}` !== key)
      finish();
  });
  if (finished) { unsubscribeRoute(); return finish; }

  unsubscribeDetail = sessionDetailStore.subscribe((state) => {
    if (state.status === "error") { finish(); return; }
    if (state.status !== "ready" || state.data?.project_id !== projectId ||
        state.data?.id !== sessionId) return;
    const active = document.activeElement;
    const sameSidebarItem = active?.classList?.contains("session-item") &&
      active.dataset.sessionKey === key;
    const focusAllowed = active === origin || active === document.body ||
      !active?.isConnected || sameSidebarItem;
    if (!prompt.disabled && focusAllowed) prompt.focus();
    finish();
  });
  if (finished) unsubscribeDetail();
  return finish;
}
