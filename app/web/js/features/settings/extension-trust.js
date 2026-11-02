import { setUiText, uiMessage } from "../../i18n.js";

// Keep the action clickable so an unchecked consent has a visible, accessible
// explanation. This check never grants consent or sends a request itself.
export function extensionTrustCheck(checkbox, message) {
  let previous = null;
  const clear = () => {
    if (!previous) return;
    setUiText(message, previous.text);
    if (previous.role === null) message.removeAttribute("role");
    else message.setAttribute("role", previous.role);
    if (!previous.error) message.classList.remove("extension-error");
    checkbox.removeAttribute("aria-invalid");
    previous = null;
  };
  checkbox.addEventListener("change", () => { if (checkbox.checked) clear(); });
  return () => {
    if (checkbox.checked) { clear(); return true; }
    previous ??= { text: message.textContent, role: message.getAttribute("role") ?? null,
      error: message.classList.contains("extension-error") };
    setUiText(message, uiMessage("extensionTrust.required", {},
      "请先勾选信任确认，确认已核对操作、作者及扩展内容后再继续。"));
    message.classList.add("extension-error");
    message.setAttribute("role", "alert");
    checkbox.setAttribute("aria-invalid", "true");
    checkbox.focus({ preventScroll: true });
    checkbox.scrollIntoView?.({ block: "nearest" });
    return false;
  };
}
