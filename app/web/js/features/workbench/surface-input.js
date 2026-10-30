import { setUiText as writeUiText, uiMessage, t as uiText } from "../../i18n.js";

/* Native drivers remain plugins. This channel only maps UI input into bounded,
 * sequenced events. Receivers must enforce epoch/sequence and lease expiry. */
export function createSurfaceInput({ canvas, toolbar, sdk, stream, signal, onError }) {
  if (!stream) return { stop() {}, dispose() {} };
  const epoch = crypto.randomUUID(), queue = []; let sequence = 0, active = false, sending = false, heartbeat;
  const toggle = document.createElement("button"); toggle.type = "button"; toggle.className = "wb-button"; writeUiText(toggle, uiMessage("ui.surfaceInput.takeControl", {}, "开始控制")); toolbar.append(toggle);
  canvas.tabIndex = 0;
  const packet = (kind, fields = {}) => ({ epoch, sequence: ++sequence, kind, lease_ms: 1500, ...fields });
  const write = event => sdk.call("stream.write", { handle: stream, text: JSON.stringify(event) + "\n", timeout_ms: 250 });
  const release = () => {
    if (!active) return; active = false; clearInterval(heartbeat); queue.length = 0; writeUiText(toggle, uiMessage("ui.surfaceInput.takeControl", {}, "开始控制"));
    void write(packet("release_all")).catch(() => { /* Driver lease expiry releases held input. */ });
  };
  const pump = async () => {
    if (sending) return; sending = true;
    try { while (active && queue.length) await write(queue.shift()); }
    catch (error) { release(); onError(new Error(uiText("ui.surfaceInput.controlChannelUnconfirmedControlEndedValue1", { value1: error.message }, "控制通道未确认，已退出控制：{value1}"))); }
    finally { sending = false; }
  };
  const send = (kind, fields) => {
    if (!active || signal.aborted) return;
    const event = packet(kind, fields), previous = queue.at(-1);
    if (kind === "pointermove" && previous?.kind === kind && previous.pointer === event.pointer) queue[queue.length - 1] = event;
    else if (queue.length < 32) queue.push(event);
    else { release(); onError(new Error(uiText("ui.surfaceInput.controlChannelCongestedControlEnded", {}, "控制通道拥塞，已退出控制"))); return; }
    void pump();
  };
  toggle.onclick = () => {
    if (active) { release(); return; }
    active = true; writeUiText(toggle, uiMessage("ui.surfaceInput.releaseControl", {}, "退出控制")); canvas.focus(); send("acquire"); heartbeat = setInterval(() => send("heartbeat"), 500);
  };
  const pointer = event => {
    if (!active) return; event.preventDefault();
    if (event.type === "pointerdown") { canvas.focus(); canvas.setPointerCapture(event.pointerId); }
    const bounds = canvas.getBoundingClientRect();
    send(event.type, { pointer: event.pointerId, x: Math.max(0, Math.min(canvas.width - 1, Math.round((event.clientX - bounds.left) / bounds.width * canvas.width))),
      y: Math.max(0, Math.min(canvas.height - 1, Math.round((event.clientY - bounds.top) / bounds.height * canvas.height))), button: event.button, buttons: event.buttons, width: canvas.width, height: canvas.height });
    if (["pointerup", "pointercancel"].includes(event.type) && canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId);
  };
  const key = event => { if (!active || event.isComposing) return; event.preventDefault(); event.stopPropagation(); send(event.type, { key: event.key, code: event.code, ctrl: event.ctrlKey, alt: event.altKey, shift: event.shiftKey, meta: event.metaKey }); };
  const wheel = event => { if (!active) return; event.preventDefault(); send("wheel", { x: event.deltaX, y: event.deltaY, mode: event.deltaMode }); };
  const events = ["pointerdown", "pointermove", "pointerup", "pointercancel"];
  for (const name of events) canvas.addEventListener(name, pointer);
  canvas.addEventListener("keydown", key); canvas.addEventListener("keyup", key); canvas.addEventListener("wheel", wheel, { passive: false });
  const blur = () => release(); window.addEventListener("blur", blur); signal.addEventListener("abort", release, { once: true });
  return { stop: release, dispose() { release(); for (const name of events) canvas.removeEventListener(name, pointer); canvas.removeEventListener("keydown", key); canvas.removeEventListener("keyup", key); canvas.removeEventListener("wheel", wheel); window.removeEventListener("blur", blur); signal.removeEventListener("abort", release); toggle.remove(); } };
}
