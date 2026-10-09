// QA only. Observe native editor events and labelled product assignments.
// Do not replace textarea accessors or dispatch input/composition events:
// changing those can change a browser driver's fill strategy and hide a bug.
(() => {
  const records = [], started = performance.now();
  let node, lastValue, flushTimer;
  function fingerprint(text) {
    let hash = 2166136261;
    for (let i = 0; i < text.length; i++) hash = Math.imul(hash ^ text.charCodeAt(i), 16777619);
    return { length: text.length, fingerprint: (hash >>> 0).toString(16) };
  }
  function publish() {
    if (!node?.isConnected) {
      if (!document.body) return;
      node = document.createElement("script"); node.type = "application/json";
      node.id = "qa-input-events"; document.body.append(node);
    }
    node.textContent = JSON.stringify(records);
  }
  function record(type, fields = {}) {
    const input = document.querySelector("#prompt");
    records.push({ milliseconds: Math.round(performance.now() - started), type,
      value: fingerprint(input?.value ?? ""), selectionStart: input?.selectionStart,
      selectionEnd: input?.selectionEnd, focused: document.hasFocus(),
      active: document.activeElement?.id || document.activeElement?.tagName,
      visibility: document.visibilityState, ...fields });
    if (records.length > 2048) records.shift();
    clearTimeout(flushTimer); flushTimer = setTimeout(publish, 0);
  }
  for (const type of ["beforeinput", "input", "compositionstart", "compositionupdate",
      "compositionend", "keydown", "keyup", "focus", "blur", "paste"])
    document.addEventListener(type, event => {
      if (event.target?.id !== "prompt") return;
      record(type, { trusted: event.isTrusted, inputType: event.inputType,
        composing: event.isComposing, dataLength: event.data?.length,
        key: event.key?.length === 1 ? "character" : event.key,
        keyCode: event.keyCode, ctrl: event.ctrlKey, alt: event.altKey,
        shift: event.shiftKey, meta: event.metaKey });
    }, true);
  document.addEventListener("qa-composer-assignment", event => {
    record("product-assignment", { nextValue: fingerprint(String(event.detail.value)),
      stack: event.detail.stack.split("\n").slice(1, 5) });
  });
  for (const type of ["focus", "blur", "pageshow", "pagehide"])
    window.addEventListener(type, event => record("window-" + type,
      { trusted: event.isTrusted, persisted: event.persisted }));
  document.addEventListener("visibilitychange", () => record("visibility"));
  const timer = setInterval(() => {
    const input = document.querySelector("#prompt");
    if (!input || input.value === lastValue) return;
    lastValue = input.value; record("observed-value");
  }, 50);
  window.addEventListener("pagehide", () => { clearInterval(timer); publish(); });
  document.addEventListener("DOMContentLoaded", publish, { once: true });
  record("observer-start");
})();
