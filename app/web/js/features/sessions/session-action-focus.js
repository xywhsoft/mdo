// A metadata update can replace the sidebar button while its dialog is open.
// Native <dialog> cannot return focus to that detached button on close.
export function rememberSessionActionFocus({ container, fallback, navigation,
  document = globalThis.document }) {
  const origin = document.activeElement;
  const route = navigation.get();
  const key = origin?.classList?.contains("session-more")
    ? origin.dataset.sessionKey : null;

  return () => {
    // Navigation or a newer focus choice takes precedence over this dialog.
    if (navigation.get() !== route) return;
    const active = document.activeElement;
    if (active && active !== document.body && active.isConnected) return;
    const target = key
      ? [...container.querySelectorAll(".session-more")]
        .find((button) => button.dataset.sessionKey === key) ?? fallback
      : origin;
    // A closed mobile drawer must not regain keyboard focus.
    if (!target?.isConnected || target.disabled ||
        target.closest("[inert]") || !target.getClientRects().length) return;
    target.focus({ preventScroll: true });
  };
}
