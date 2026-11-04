import { openLayer } from "../../components/layers.js";
import { mountIcons } from "../../components/icons.js";
import { createLocalApplicationClient } from "../../api/application.js";
import { uiMessage, setUiText } from "../../i18n.js";
import { element, errorMessage, toast } from "../../utils/dom.js";

let activeWindow = null;
const copy = key => uiMessage(`about.${key}`);

export function openAboutWindow({ owner } = {}) {
  if (activeWindow) return activeWindow;
  const layer = openLayer({ title: copy("title"), icon: "info", width: "34rem",
    mobile: "fit", maximizable: false, owner,
    onClosed() { if (activeWindow === layer) activeWindow = null; } });
  activeWindow = layer;
  layer.element.id = "about-dialog";
  layer.element.classList.add("about-window");
  layer.body.append(element("div", { className: "about-brand" }, [
    element("img", { attrs: { src: "/assets/mdo-app-icon.png", alt: "", width: 56, height: 56 } }),
    element("h3", { text: uiMessage("app.name") }),
  ]), element("p", { className: "about-description", text: copy("description") }));
  const details = element("dl", { className: "about-details" });
  function row(key, value) {
    const content = element("dd", {}, [value]);
    details.append(element("div", {}, [element("dt", { text: copy(key) }), content]));
    return content;
  }
  const version = row("version", element("span", { text: copy("loadingVersion") }));
  row("author", "xLeaves (xywhsoft)");
  for (const [key, host] of [["website", "mdo.xywhsoft.com"], ["studio", "xywhsoft.com"]]) {
    const link = element("a", { attrs: { href: `https://${host}`, target: "_blank", rel: "noopener noreferrer" } }, [
      host, element("span", { className: "icon", attrs: { "data-icon": "arrow-up-right", "aria-hidden": "true" } }),
    ]);
    link.addEventListener("click", event => {
      if (!globalThis.chrome?.webview && !globalThis.external?.invoke &&
          !globalThis.webkit?.messageHandlers?.external && !globalThis.XsBrowser) return;
      event.preventDefault();
      const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 8000);
      createLocalApplicationClient().call("system.open", { target: link.href }, { signal: controller.signal })
        .catch(error => toast(errorMessage(error), "error")).finally(() => clearTimeout(timer));
    }, { signal: layer.signal });
    row(key, link);
  }
  layer.body.append(details);
  mountIcons(details);
  const close = element("button", { className: "secondary-button", text: uiMessage("layers.close"), attrs: { type: "button" } });
  close.addEventListener("click", () => { void layer.close("button"); }, { signal: layer.signal });
  layer.footer.append(close);
  // About describes the displaying installation, even when a remote device is selected.
  const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 8000);
  const abort = () => controller.abort();
  layer.signal.addEventListener("abort", abort, { once: true });
  void fetch("/api/v1/bootstrap", { signal: controller.signal, cache: "no-store" })
    .then(async response => {
      if (!response.ok) throw new Error("bootstrap unavailable");
      const result = await response.json();
      if (!result.ok || typeof result.data?.version !== "string" || !result.data.version.trim())
        throw new Error("version unavailable");
      if (!layer.signal.aborted) setUiText(version, result.data.version);
    }).catch(() => {
      if (!layer.signal.aborted) setUiText(version, copy("versionUnavailable"));
    }).finally(() => { clearTimeout(timer); layer.signal.removeEventListener("abort", abort); });
  return layer;
}
