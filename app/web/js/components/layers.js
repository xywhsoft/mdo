import { t as uiErrorText, setUiAttribute as writeUiAttribute, setUiText as writeUiText, uiMessage } from "../i18n.js";

import { element, isImeKey } from "../utils/dom.js";
import { mountIcons } from "./icons.js";

const managers = new WeakMap();
let sequence = 0;
const text = (key, fallback) => uiMessage(`layers.${key}`, {}, fallback);
const node = (tag, className) => element(tag, { className });

// Numbers are CSS logical pixels, never physical display pixels. Percentages
// refer to the visual viewport, including when the soft keyboard is visible.
export function layerSize(value, axis) {
  if (axis !== "width" && axis !== "height") throw new TypeError(uiErrorText("error.detail.invalidLayerSizeAxis", {}, "Invalid layer size axis"));
  if (value === "auto") return value;
  if (typeof value === "number" && Number.isFinite(value) && value > 0) return `${value}px`;
  if (typeof value === "string") {
    const match = value.trim().match(/^(\d+(?:\.\d+)?)(px|rem|%)$/);
    if (match && Number.isFinite(Number(match[1])) && Number(match[1]) > 0) return match[2] === "%"
      ? `calc(var(--layer-viewport-${axis}) * ${Number(match[1]) / 100})` : value.trim();
  }
  throw new TypeError(uiErrorText("error.detail.layerSizesMustBePositiveCssPixelsRemPercentagesOr", {}, "Layer sizes must be positive CSS pixels, rem, percentages, or auto"));
}

function managerFor(doc) {
  if (!managers.has(doc)) managers.set(doc, { stack: [], locks: new Map() });
  return managers.get(doc);
}

function lockScroll(manager, targets) {
  const nodes = [...new Set(targets.filter(Boolean))];
  for (const target of nodes) {
    let entry = manager.locks.get(target);
    if (!entry) {
      entry = { count: 0, value: target.style.getPropertyValue("overflow"), priority: target.style.getPropertyPriority("overflow"), top: target.scrollTop, left: target.scrollLeft };
      manager.locks.set(target, entry); target.style.setProperty("overflow", "hidden");
    }
    entry.count++;
  }
  return () => {
    for (const target of nodes) {
      const entry = manager.locks.get(target);
      if (--entry.count) continue;
      if (entry.value) target.style.setProperty("overflow", entry.value, entry.priority);
      else target.style.removeProperty("overflow");
      target.scrollTop = entry.top; target.scrollLeft = entry.left;
      manager.locks.delete(target);
    }
  };
}

/** Open an owned, themed window in the native dialog top layer. See docs/frontend-layers.md. */
export function openLayer(options = {}) {
  const doc = document, manager = managerFor(doc), opener = options.opener || doc.activeElement;
  let config = { title: "", width: "52rem", height: "auto", mobile: "fullscreen", closeOnEscape: true,
    closeOnBackdrop: false, maximizable: true, draggable: true, ...options };
  const validate = next => {
    layerSize(next.width, "width"); layerSize(next.height, "height");
    if (!["fullscreen", "fit"].includes(next.mobile)) throw new TypeError(uiErrorText("error.detail.invalidMobileLayerLayout", {}, "Invalid mobile layer layout"));
  };
  validate(config);
  const dialog = node("dialog", "mdo-layer"), header = node("header", "mdo-layer-header");
  const heading = node("h2", "mdo-layer-title"), icon = node("span", "icon mdo-layer-icon");
  const tools = node("div", "mdo-layer-controls"), body = node("div", "mdo-layer-body"), footer = node("footer", "mdo-layer-footer");
  const maximize = node("button", "mdo-layer-control"), closeButton = node("button", "mdo-layer-control");
  const id = `mdo-layer-${++sequence}`; heading.id = `${id}-title`; dialog.id = id;
  dialog.setAttribute("aria-labelledby", heading.id); dialog.setAttribute("aria-modal", "true");
  icon.setAttribute("aria-hidden", "true");
  for (const [button, name] of [[maximize, "expand"], [closeButton, "close"]]) {
    button.type = "button";
    button.append(element("span", { className: "icon", attrs: { "data-icon": name, "aria-hidden": "true" } }));
  }
  writeUiAttribute(closeButton, "aria-label", text("close", "关闭窗口")); writeUiAttribute(closeButton, "title", text("close", "关闭窗口"));
  tools.append(maximize, closeButton); header.append(icon, heading, tools); dialog.append(header, body, footer);
  const lifetime = new AbortController(), listeners = new AbortController();
  let ended = false, pendingClose = null, contentDispose, maximized = false, drag = null, shift = { x: 0, y: 0 };
  let resolveClosed;
  const closed = new Promise(resolve => { resolveClosed = resolve; });
  const viewport = () => {
    const view = globalThis.visualViewport;
    return { width: view?.width || globalThis.innerWidth || doc.documentElement?.clientWidth || 1024,
      height: view?.height || globalThis.innerHeight || doc.documentElement?.clientHeight || 768,
      left: view?.offsetLeft || 0, top: view?.offsetTop || 0 };
  };
  const layout = () => {
    const view = viewport(), mobile = view.width <= 600;
    dialog.style.setProperty("--layer-viewport-width", `${view.width}px`);
    dialog.style.setProperty("--layer-viewport-height", `${view.height}px`);
    // Re-clamp after orientation changes, keyboard changes and content resizing.
    if (shift.x || shift.y) {
      const rect = dialog.getBoundingClientRect();
      shift.x = Math.max(-Math.max(0, (view.width - rect.width) / 2 - 12), Math.min(shift.x, Math.max(0, (view.width - rect.width) / 2 - 12)));
      shift.y = Math.max(-Math.max(0, (view.height - rect.height) / 2 - 12), Math.min(shift.y, Math.max(0, (view.height - rect.height) / 2 - 12)));
    }
    if (mobile || maximized) shift = { x: 0, y: 0 };
    dialog.style.left = `${view.left + view.width / 2 + shift.x}px`;
    dialog.style.top = `${view.top + view.height / 2 + shift.y}px`;
    dialog.classList.toggle("is-mobile", mobile);
    dialog.classList.toggle("is-fullscreen", (mobile && config.mobile === "fullscreen") || maximized);
  };
  const paint = () => {
    writeUiText(heading, config.title);
    icon.hidden = !config.icon;
    if (config.icon) { icon.dataset.icon = config.icon; mountIcons(header); }
    dialog.style.width = layerSize(config.width, "width"); dialog.style.height = layerSize(config.height, "height");
    dialog.classList.toggle("is-draggable", Boolean(config.draggable));
    maximize.hidden = !config.maximizable;
    const maximizeLabel = maximized ? text("restore", "还原窗口") : text("maximize", "最大化窗口");
    writeUiAttribute(maximize, "aria-label", maximizeLabel);
    writeUiAttribute(maximize, "title", maximizeLabel); maximize.setAttribute("aria-pressed", String(maximized));
    layout();
  };
  let unlock = () => {};
  const report = error => {
    try { if (config.onError) config.onError(error); else console.error("Layer lifecycle failed", error); }
    catch (handlerError) { console.error("Layer error handler failed", handlerError); }
  };
  const safely = callback => { try { callback?.(); } catch (error) { report(error); } };
  const finish = (reason, value, restoreFocus = true) => {
    if (ended) return;
    ended = true; listeners.abort(); lifetime.abort();
    const index = manager.stack.indexOf(layer); if (index >= 0) manager.stack.splice(index, 1);
    if (dialog.open) dialog.close();
    body.hidden = true; dialog.remove(); unlock();
    safely(contentDispose); contentDispose = undefined;
    if (restoreFocus && opener?.isConnected) opener.focus({ preventScroll: true });
    safely(() => config.onClosed?.({ reason, value })); resolveClosed({ reason, value });
  };
  const requestClose = (reason = "api", value) => {
    if (ended) return Promise.resolve(true);
    if (pendingClose) return pendingClose;
    if (!config.beforeClose) { finish(reason, value); return Promise.resolve(true); }
    closeButton.disabled = true;
    pendingClose = Promise.resolve().then(() => config.beforeClose({ reason, value, layer })).then(allowed => {
      if (allowed === false || ended) return ended;
      finish(reason, value); return true;
    }).catch(error => { report(error); return false; }).finally(() => { pendingClose = null; closeButton.disabled = false; });
    return pendingClose;
  };
  const layer = {
    element: dialog, body, footer, closeButton, signal: lifetime.signal, closed,
    update(patch) { if (ended) return; const next = { ...config, ...patch }; validate(next); config = next; paint(); },
    close: requestClose,
    dispose({ restoreFocus = false } = {}) { finish("dispose", undefined, restoreFocus); },
  };
  const on = (target, name, handler) => target?.addEventListener(name, handler, { signal: listeners.signal });
  on(closeButton, "click", () => { void requestClose("button"); });
  on(maximize, "click", () => { maximized = !maximized; shift = { x: 0, y: 0 }; paint(); });
  on(dialog, "cancel", event => {
    event.preventDefault();
    if (config.closeOnEscape && !isImeKey(event) && manager.stack.at(-1) === layer) void requestClose("escape");
  });
  // Native form method=dialog / direct .close() have already closed the window.
  on(dialog, "close", () => finish("native", dialog.returnValue));
  let backdropDown = false;
  const outside = event => {
    const r = dialog.getBoundingClientRect();
    return event.target === dialog && (event.clientX < r.left || event.clientX > r.right || event.clientY < r.top || event.clientY > r.bottom);
  };
  on(dialog, "pointerdown", event => { backdropDown = outside(event); });
  on(dialog, "pointerup", event => { if (backdropDown && outside(event) && config.closeOnBackdrop) void requestClose("backdrop"); backdropDown = false; });
  on(dialog, "pointercancel", () => { backdropDown = false; });
  on(header, "pointerdown", event => {
    if (!config.draggable || dialog.classList.contains("is-mobile") || maximized || event.button !== 0 || event.target.closest("button")) return;
    drag = { id: event.pointerId, x: event.clientX, y: event.clientY, startX: shift.x, startY: shift.y };
    // Keep text selectable in the body, suppress only titlebar dragging.
    header.setPointerCapture(event.pointerId); event.preventDefault();
  });
  on(header, "pointermove", event => {
    if (!drag || event.pointerId !== drag.id) return;
    shift = { x: drag.startX + event.clientX - drag.x, y: drag.startY + event.clientY - drag.y }; layout();
  });
  const endDrag = () => { drag = null; };
  on(header, "pointerup", endDrag); on(header, "pointercancel", endDrag); on(header, "lostpointercapture", endDrag);
  on(globalThis.window, "resize", layout); on(globalThis.visualViewport, "resize", layout); on(globalThis.visualViewport, "scroll", layout);
  on(globalThis.window, "pagehide", () => layer.dispose());
  let resizeObserver;
  if (globalThis.ResizeObserver) { resizeObserver = new ResizeObserver(layout); resizeObserver.observe(dialog); }
  listeners.signal.addEventListener("abort", () => resizeObserver?.disconnect(), { once: true });
  mountIcons(tools);
  try {
    paint(); (config.host || doc.body).append(dialog);
    unlock = lockScroll(manager, [doc.documentElement, doc.body, opener?.closest?.(".settings-content"), config.owner?.closest?.(".settings-content")]);
    manager.stack.push(layer); dialog.showModal(); layout();
    closeButton.focus({ preventScroll: true });
  } catch (error) { layer.dispose(); throw error; }
  // Pages may briefly detach and reattach their browser while refreshing.
  // Observe the final DOM state, not each intermediate mutation. A hidden or
  // removed owner must not leave an orphan modal blocking another page.
  if (config.owner && globalThis.MutationObserver) {
    const observer = new MutationObserver(() => {
      if (!config.owner.isConnected || config.owner.closest("[hidden]")) layer.dispose();
    });
    observer.observe(doc.body, { childList: true, subtree: true, attributes: true, attributeFilter: ["hidden"] });
    listeners.signal.addEventListener("abort", () => observer.disconnect(), { once: true });
  }
  layer.ready = Promise.resolve().then(() => {
    if (!ended) return config.render?.(body, { layer, signal: lifetime.signal });
  }).then(dispose => {
    if (typeof dispose === "function") { if (ended) safely(dispose); else contentDispose = dispose; }
  }).catch(error => { if (!ended) { layer.dispose(); throw error; } });
  // The caller can await ready for errors; also prevent an unattended window
  // from producing an unhandled rejection if its renderer fails.
  layer.ready.catch(report);
  return layer;
}
