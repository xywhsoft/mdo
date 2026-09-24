function playCompletionSound() {
  const Audio = window.AudioContext || window.webkitAudioContext;
  if (!Audio) return;
  try {
    const context = new Audio();
    const note = (frequency, offset, duration, volume) => {
      const oscillator = context.createOscillator();
      const gain = context.createGain();
      const start = context.currentTime + offset;
      oscillator.type = "sine";
      oscillator.frequency.value = frequency;
      gain.gain.setValueAtTime(0.0001, start);
      gain.gain.exponentialRampToValueAtTime(volume, start + 0.015);
      gain.gain.exponentialRampToValueAtTime(0.0001, start + duration);
      oscillator.connect(gain);
      gain.connect(context.destination);
      oscillator.start(start);
      oscillator.stop(start + duration + 0.05);
    };
    note(988, 0, 0.30, 0.11);
    note(784, 0.18, 0.45, 0.09);
    window.setTimeout(() => void context.close().catch(() => {}), 1400);
  } catch { /* Browsers without audio output keep notifications visual. */ }
}

export function createRunNotifications({ runsStore, navigation, settingsStore,
  onUnreadChange }) {
  const baseTitle = document.title;
  const unread = new Set();
  let seen = null;
  let selectedKey = "";

  function display() {
    document.title = unread.size ? `(${Math.min(unread.size, 99)}${unread.size > 99 ? "+" : ""}) ${baseTitle}` : baseTitle;
    onUnreadChange(new Set(unread));
  }

  navigation.subscribe(({ view, projectId, sessionId }) => {
    if (view === "workspace") selectedKey = sessionId ? `${projectId}/${sessionId}` : "";
    if (unread.delete(selectedKey)) display();
  });

  runsStore.subscribe((state) => {
    if (state.status !== "ready") return;
    const next = new Map();
    let changed = false;
    for (const run of state.data?.items ?? []) {
      if (!run?.id) continue;
      next.set(run.id, Boolean(run.terminal));
      if (seen === null || !run.terminal || seen.get(run.id) === true) continue;
      const key = `${run.project_id}/${run.session_id}`;
      if (key === selectedKey) continue;
      if (!unread.has(key)) {
        unread.add(key);
        while (unread.size > 64) unread.delete(unread.keys().next().value);
        changed = true;
      }
    }
    seen = next;
    if (changed) {
      display();
      if (settingsStore.get().data?.notifications?.sound) playCompletionSound();
    }
  });

  display();
}
