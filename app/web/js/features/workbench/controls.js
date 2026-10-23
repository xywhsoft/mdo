import { mountIcons } from "../../components/icons.js";

export function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}
export function icon(name) {
  const node = element("span", "wb-icon"); node.dataset.icon = name;
  const wrapper = document.createElement("span"); wrapper.append(node); mountIcons(wrapper); return node;
}
export function iconButton(name, label, execute, onError = console.error) {
  const node = element("button", "wb-icon-button"); node.type = "button";
  node.title = label; node.setAttribute("aria-label", label); node.append(icon(name));
  node.onclick = () => Promise.resolve().then(execute).catch(onError); return node;
}
/** One menu implementation for native-content overlays, keyboard and touch. */
export function popupMenu(trigger, items, { signal, onError = console.error } = {}) {
  let menu = null, opener = null;
  trigger.setAttribute("aria-haspopup", "menu"); trigger.setAttribute("aria-expanded", "false");
  const close = (restore = false) => {
    if (!menu) return;
    menu.remove(); menu = null; trigger.setAttribute("aria-expanded", "false");
    document.removeEventListener("pointerdown", outside, true); document.removeEventListener("keydown", keydown, true);
    window.removeEventListener("resize", reposition); window.removeEventListener("scroll", reposition, true);
    if (restore && opener?.isConnected) opener.focus();
  };
  const outside = event => { if (!menu?.contains(event.target) && !trigger.contains(event.target)) close(); };
  const reposition = () => {
    if (!menu) return;
    const box = trigger.getBoundingClientRect();
    menu.style.left = `${Math.max(8, Math.min(innerWidth - menu.offsetWidth - 8, box.right - menu.offsetWidth))}px`;
    menu.style.top = `${Math.max(8, Math.min(innerHeight - menu.offsetHeight - 8, box.bottom + 4))}px`;
  };
  const keydown = event => {
    if (event.key === "Escape") { event.preventDefault(); event.stopPropagation(); close(true); return; }
    if (event.key === "Tab") { close(); return; }
    if (!["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) return;
    event.preventDefault(); const choices = [...menu.querySelectorAll("button:not(:disabled)")];
    if (!choices.length) return;
    const at = choices.indexOf(document.activeElement);
    choices[event.key === "Home" ? 0 : event.key === "End" ? choices.length - 1 : (at + (event.key === "ArrowUp" ? -1 : 1) + choices.length) % choices.length].focus();
  };
  const open = () => {
    if (menu) { close(true); return; }
    if (signal?.aborted) return;
    opener = trigger; menu = element("div", "wb-menu"); menu.setAttribute("role", "menu");
    menu.setAttribute("aria-label", trigger.getAttribute("aria-label") || "更多操作");
    for (const item of typeof items === "function" ? items() : items) {
      const node = element("button", "wb-menu-item", item.label); node.type = "button"; node.disabled = Boolean(item.disabled);
      node.setAttribute("aria-label", item.label);
      node.setAttribute("role", item.checked === undefined ? "menuitem" : "menuitemcheckbox");
      if (item.checked !== undefined) node.setAttribute("aria-checked", String(item.checked));
      if (item.icon) node.prepend(icon(item.icon));
      node.onclick = () => { close(true); Promise.resolve().then(item.execute).catch(onError); };
      menu.append(node);
    }
    document.body.append(menu); trigger.setAttribute("aria-expanded", "true"); reposition();
    document.addEventListener("pointerdown", outside, true); document.addEventListener("keydown", keydown, true);
    window.addEventListener("resize", reposition); window.addEventListener("scroll", reposition, true);
    menu.querySelector("button:not(:disabled)")?.focus();
  };
  trigger.addEventListener("click", open);
  const dispose = () => { close(); trigger.removeEventListener("click", open); signal?.removeEventListener("abort", dispose); };
  signal?.addEventListener("abort", dispose, { once: true }); return { close, dispose };
}
