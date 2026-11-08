// Browser content cannot access this trusted application UI or its bridge.
export function createBrowserDialogs({ execute, onControl, onChange, onError }) {
  const make = (tag, className, text) => { const node = document.createElement(tag); node.className = className; if (text) node.textContent = text; return node; };
  const root = make("section", "wb-browser-dialog"), title = make("strong", "wb-dialog-title", "网页对话框");
  const source = make("span", "wb-dialog-source"), message = make("p", "wb-dialog-message"), input = make("input", "wb-dialog-input");
  const controls = make("div", "wb-dialog-actions"), accept = make("button", "wb-button"), dismiss = make("button", "wb-button");
  input.autocomplete = "off"; input.spellcheck = false; input.setAttribute("aria-label", "网页对话框回复");
  root.setAttribute("aria-label", "等待处理的网页对话框"); root.setAttribute("role", "group");
  accept.type = dismiss.type = "button"; controls.append(dismiss, accept); root.append(title, source, message, input, controls); root.hidden = true;
  let pending, generation = 0, disposed = false, busy = false;
  function hide(notify = true) { generation++; root.hidden = true; input.value = ""; pending = null; if (notify) onChange?.(); }
  async function respond(answer) {
    if (!pending || busy || disposed) return;
    busy = true; generation++; accept.disabled = dismiss.disabled = input.disabled = true;
    const current = pending, text = input.value;
    try {
      await execute("takeover"); onControl?.();
      await execute("dialog", { action: "respond", dialog: current.id, accept: answer,
        ...(current.kind === "prompt" ? { text, has_text: true } : {}) });
      if (!disposed) hide();
    } catch (error) { if (!disposed) onError?.(error); }
    finally { busy = false; accept.disabled = dismiss.disabled = input.disabled = false; }
  }
  accept.onclick = () => { void respond(true); }; dismiss.onclick = () => { void respond(false); };
  input.onkeydown = event => {
    if (["Enter", "Escape"].includes(event.key) && !event.isComposing) {
      event.preventDefault(); event.stopPropagation(); void respond(event.key === "Enter");
    }
  };
  return Object.freeze({ element: root,
    async refresh(state) {
      if (disposed) return;
      if (!state.dialog_open) { generation++; if (pending) hide(); return; }
      if (busy) return;
      const observed = ++generation;
      const result = await execute("dialog", { action: "list" });
      if (disposed || observed !== generation) return;
      const next = result.dialogs?.[0]; if (!next) { hide(); return; }
      if (pending?.id !== next.id) input.value = "";
      pending = next; root.hidden = false;
      title.textContent = next.kind === "beforeunload" ? "是否离开此页面？" : "网页对话框";
      try { source.textContent = new URL(next.url).origin; } catch { source.textContent = "网页"; }
      message.textContent = String(next.message || "网页正在等待处理。").slice(0, 4096);
      input.hidden = next.kind !== "prompt"; dismiss.hidden = next.kind === "alert";
      accept.textContent = next.kind === "beforeunload" ? "离开页面" : "确定";
      dismiss.textContent = next.kind === "beforeunload" ? "留在页面" : "取消";
      onChange?.();
    },
    dispose() { disposed = true; generation++; hide(false); accept.onclick = dismiss.onclick = input.onkeydown = null; }
  });
}
