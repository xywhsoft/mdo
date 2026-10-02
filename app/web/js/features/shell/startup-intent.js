// Restoration is a fallback. Typing, composing, keyboard navigation or a
// click in the workspace claims the current view before slow startup reads.
export function createStartupIntent({ root, navigation }) {
  const initial = navigation.get();
  let interacted = false;
  const events = ["pointerdown", "input", "compositionstart", "keydown"];
  const options = { capture: true };
  function claim(event) {
    if (event.type === "keydown" && ["Shift", "Control", "Alt", "Meta"].includes(event.key)) return;
    interacted = true;
  }
  for (const name of events) root.addEventListener(name, claim, options);
  return Object.freeze({
    allowsRestore: () => !interacted && navigation.get() === initial,
    destroy() {
      for (const name of events) root.removeEventListener(name, claim, options);
    },
  });
}
