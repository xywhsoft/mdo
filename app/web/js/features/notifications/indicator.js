const SVG = "http://www.w3.org/2000/svg";
const symbols = {
  error: '<circle cx="12" cy="12" r="10" fill="currentColor"/><path d="M12 6.5v7M12 17h.01" stroke="white" stroke-width="2.2"/>',
  warning: '<path d="M10.3 3.6a2 2 0 0 1 3.4 0L22 18a2 2 0 0 1-1.7 3H3.7A2 2 0 0 1 2 18Z" fill="currentColor"/><path d="M12 8v6M12 17.5h.01" stroke="#422006" stroke-width="2.1"/>',
  success: '<circle cx="12" cy="12" r="10" fill="currentColor"/><path d="m7 12 3.2 3.2L17 8.5" stroke="white" stroke-width="2.2" fill="none"/>',
  info: '<circle cx="12" cy="12" r="10" fill="currentColor"/><path d="M12 10v7M12 6.8h.01" stroke="white" stroke-width="2.2"/>',
  coupon: '<path d="M3 6h18v4a2 2 0 0 0 0 4v4H3v-4a2 2 0 0 0 0-4Z" fill="currentColor"/><path d="M15 7v10M7 9l4 6M7 15h.01M11 9h.01" stroke="white" stroke-width="1.5" fill="none"/>',
  more: '<circle cx="5" cy="12" r="1.8"/><circle cx="12" cy="12" r="1.8"/><circle cx="19" cy="12" r="1.8"/>',
};
export function noticeTone(notice) {
  if (notice.rank === 0 || notice.rank === 1 || notice.icon === "update") return "update";
  return ["error", "warning", "success", "coupon", "more"].includes(notice.icon) ? notice.icon : "info";
}
export function bubbleText(notice, copy) {
  return { title: notice.rank === 0 ? copy.newVersion : notice.title, body: notice.body || "" };
}
export function createNoticeIcon(notice) {
  if (/^[a-f0-9]{64}$/.test(notice.icon_sha256 ?? "")) {
    const img = document.createElement("img"); img.src = "https://ai.xywhsoft.com/mdo/blob/" + notice.icon_sha256;
    img.alt = ""; img.width = img.height = 22; return img;
  }
  const icon = document.createElementNS(SVG, "svg"); icon.setAttribute("viewBox", "0 0 24 24");
  icon.setAttribute("aria-hidden", "true"); icon.setAttribute("focusable", "false");
  icon.setAttribute("stroke-linecap", "round"); icon.setAttribute("stroke-linejoin", "round");
  // These SVG fragments are fixed source constants; remote text never becomes markup.
  icon.innerHTML = symbols[noticeTone(notice)] ?? symbols.info; return icon;
}

/** Compact indicators and an interactive, anchored preview shared by all headers. */
export function createNoticeIndicators({ hosts, copy, onOpen }) {
  const bubble = document.createElement("button"); bubble.type = "button";
  bubble.className = "notification-bubble"; bubble.id = "notification-preview"; bubble.hidden = true;
  document.body.append(bubble);
  let list = [], preferences = {}, scope = "", anchor = null, activeId = null, hovering = false, focused = false;
  let closeTimer, autoTimer, expiry = 0, destroyed = false;
  const seen = new Set(), fingerprints = new Map();
  const find = id => id === "all-notifications" ? { id, icon: "more", title: copy().title, body: list.map(n => n.title).join("\n") } : list.find(n => n.id === id);
  const visible = e => {
    if (!e || e.hidden || !e.getClientRects().length) return false;
    const rect = e.getBoundingClientRect();
    return rect.right > 0 && rect.bottom > 0 && rect.left < window.innerWidth && rect.top < window.innerHeight;
  };
  function hide() {
    clearTimeout(closeTimer); clearTimeout(autoTimer); bubble.hidden = true;
    anchor?.removeAttribute("aria-describedby"); anchor = null; activeId = null; hovering = focused = false;
  }
  function position() {
    if (!visible(anchor) || document.querySelector("dialog[open]")) return hide();
    const rect = anchor.getBoundingClientRect(), width = bubble.getBoundingClientRect().width;
    const left = Math.max(12, Math.min(rect.left + rect.width / 2 - 26, window.innerWidth - width - 12));
    bubble.style.left = left + "px"; bubble.style.top = rect.bottom + 10 + "px";
    bubble.style.setProperty("--pointer-x", Math.max(16, Math.min(width - 16, rect.left + rect.width / 2 - left)) + "px");
  }
  function show(button, id, automatic = false) {
    const notice = find(id); if (!notice || !visible(button) || document.querySelector("dialog[open]")) return;
    clearTimeout(closeTimer); anchor?.removeAttribute("aria-describedby"); anchor = button; activeId = id;
    const text = bubbleText(notice, copy()), title = document.createElement("strong"), body = document.createElement("span");
    title.textContent = text.title; body.textContent = text.body;
    bubble.replaceChildren(title, body); bubble.hidden = false;
    bubble.setAttribute("aria-label", text.title + (text.body ? " · " + text.body : ""));
    button.setAttribute("aria-describedby", bubble.id); position();
    if (automatic) { expiry = Date.now() + 8000; clearTimeout(autoTimer); autoTimer = setTimeout(() => { if (!hovering && !focused) hide(); }, 8000); }
  }
  function leave() { clearTimeout(closeTimer); closeTimer = setTimeout(() => { if (!hovering && !focused) hide(); }, 180); }
  function open(id) { const notice = find(id); hide(); if (notice) onOpen(id === "all-notifications" ? null : notice); }
  function escape(event) { if (event.key === "Escape" && !bubble.hidden) { event.stopPropagation(); hide(); } }
  bubble.addEventListener("click", () => open(activeId));
  bubble.addEventListener("pointerenter", () => { hovering = true; clearTimeout(closeTimer); });
  bubble.addEventListener("pointerleave", () => { hovering = false; leave(); });
  bubble.addEventListener("focus", () => { focused = true; clearTimeout(closeTimer); });
  bubble.addEventListener("blur", () => { focused = false; leave(); });
  bubble.addEventListener("keydown", escape);
  function makeButton(n) {
    const button = document.createElement("button"); button.type = "button";
    button.className = "notification-indicator"; button.dataset.noticeId = n.id;
    button.dataset.tone = noticeTone(n); button.setAttribute("aria-controls", "notifications-dialog"); button.setAttribute("aria-haspopup", "dialog");
    button.setAttribute("aria-label", n.id === "all-notifications" ? copy().allNotices : n.title);
    if (noticeTone(n) === "update") button.textContent = copy().updateBadge;
    else button.append(createNoticeIcon(n));
    button.addEventListener("click", () => open(n.id));
    button.addEventListener("pointerenter", () => { hovering = true; show(button, n.id); });
    button.addEventListener("pointerleave", () => { hovering = false; leave(); });
    button.addEventListener("focus", () => { focused = true; show(button, n.id); });
    button.addEventListener("blur", () => { focused = false; leave(); });
    button.addEventListener("keydown", escape); return button;
  }
  function render(next, prefs, target) {
    if (destroyed) return;
    const previousId = activeId, wasVisible = !bubble.hidden, priorHost = anchor?.parentElement, previousScope = scope;
    if (target !== scope) { hide(); seen.clear(); }
    list = next; preferences = prefs; scope = target;
    const displayed = list.length > 3 ? [...list.slice(0, 2), find("all-notifications")] : list;
    const signature = JSON.stringify([copy().updateBadge, copy().allNotices, displayed]);
    for (const host of hosts) {
      host.hidden = list.length === 0; host.setAttribute("aria-label", copy().title + " · " + list.length);
      if (fingerprints.get(host) !== signature) { host.replaceChildren(...displayed.map(makeButton)); fingerprints.set(host, signature); }
    }
    if (wasVisible && target === previousScope) {
      const replacement = [...(priorHost?.children ?? [])].find(e => e.dataset.noticeId === previousId);
      if (replacement && find(previousId)) { show(replacement, previousId); if (expiry && Date.now() >= expiry && !hovering && !focused) hide(); }
      else hide();
    }
    const first = list.find(n => preferences[n.id]?.read !== n.revision);
    const stamp = first && scope + ":" + first.id + ":" + first.revision;
    if (stamp && !seen.has(stamp) && !document.querySelector("dialog[open]")) {
      const host = [...hosts].find(visible), button = [...(host?.children ?? [])].find(e => e.dataset.noticeId === first.id);
      if (button) { seen.add(stamp); if (bubble.hidden) show(button, first.id, true); }
    }
  }
  const reposition = () => { if (!bubble.hidden) position(); };
  window.addEventListener("resize", reposition); window.addEventListener("scroll", reposition, true);
  return { render, hide, destroy() { destroyed = true; hide(); bubble.remove(); for (const host of hosts) host.replaceChildren(); window.removeEventListener("resize", reposition); window.removeEventListener("scroll", reposition, true); } };
}
