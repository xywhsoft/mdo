// One quiet-period save at a time. Editing remains available while a request
// is in flight; a successful save is followed by the newest remaining edits.
// Failures wait for an explicit retry or another edit, rather than looping.
export function createSettingsAutosave({ capture, write, onStart, onSettled,
  timers = window, delay = 600 }) {
  let timer = 0;
  let saving = false;
  let composing = false;
  let disposed = false;

  function cancel() {
    timers.clearTimeout(timer);
    timer = 0;
  }

  function schedule() {
    cancel();
    if (!disposed && !composing)
      timer = timers.setTimeout(() => { void flush(); }, delay);
  }

  async function flush() {
    cancel();
    if (disposed || composing || saving) return false;
    const submitted = capture();
    if (!submitted) return false;
    saving = true;
    onStart(submitted);
    let result;
    let failure = null;
    try { result = await write(submitted); }
    catch (cause) { failure = cause; }
    finally {
      saving = false;
      cancel();
      if (!disposed) {
        onSettled(failure, result);
        if (!failure && capture()) schedule();
      }
    }
    return !failure;
  }

  return Object.freeze({ schedule, flush, cancel,
    composing(value) {
      composing = Boolean(value);
      if (composing) cancel();
      else schedule();
    },
    destroy() { disposed = true; cancel(); },
  });
}

// Rebase only fields edited after the submitted snapshot. The server may
// normalize earlier edits; its reply must not replace newer form values.
export function changedSettingsValues(before, current) {
  return Object.fromEntries(Object.entries(current).filter(([name, value]) =>
    Object.hasOwn(before, name) && before[name] !== value));
}
