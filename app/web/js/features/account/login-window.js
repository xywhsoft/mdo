import { openLayer } from "../../components/layers.js";
import { element, errorMessage } from "../../utils/dom.js";
import { t, subscribeLocale } from "../../i18n.js";
import { subscribeTarget } from "../../api/target.js";

let activeWindow = null;

// The displaying installation's connector account and a selected remote
// account use the same form, with explicitly separate request adapters.
export function createLoginWindow({ request, scope, allowBrowser = () => false, onSnapshot = () => {} }) {
  let view = null, disposed = false;
  const copy = key => t(`account.${key}`);
  const current = own => !disposed && view === own && own.scope === scope() && !own.layer.signal.aborted;
  const el = (tag, className, attrs) => element(tag, { className, attrs });
  function render(own) {
    if (!current(own)) return;
    const state = own.snapshot || {};
    own.layer.update({ title: copy("login") });
    for (const [node, key] of own.labels) node.textContent = copy(key);
    own.remember.disabled = own.pending || state.persistence_available === false;
    if (state.persistence_available === false) own.remember.checked = false;
    own.storage.textContent = copy(state.persistence_available === false ? "storageUnavailable" : "storage");
    own.status.textContent = own.error || own.readError || copy(state.state === "signing_in" ? "signingIn" :
      state.state === "authorizing" ? "waitingBrowser" : own.attempted && state.message ? state.message : "directDescription");
    own.status.setAttribute("role", own.error || own.readError ? "alert" : "status");
    own.form.hidden = state.state === "authorizing";
    own.submit.disabled = own.pending || state.state === "signing_in";
    own.browser.hidden = !allowBrowser(); own.browser.disabled = own.pending;
    own.browser.textContent = copy(state.state === "authorizing" ? "restart" : "begin");
    own.link.hidden = !own.authorizationUrl;
    if (own.authorizationUrl) own.link.href = own.authorizationUrl;
    own.cancel.disabled = own.pending;
    if (own.attempted && state.state === "signed_in" && !state.busy && !state.message) {
      own.success = true; void own.layer.close("success");
    }
  }
  function apply(own, snapshot) {
    if (!current(own)) return;
    own.snapshot = snapshot; onSnapshot(snapshot); render(own);
  }
  function schedule(own) {
    clearTimeout(own.timer);
    if (current(own)) own.timer = setTimeout(() => void poll(own), own.error || own.snapshot?.busy || ["signing_in", "authorizing"].includes(own.snapshot?.state) ? 1000 : 10000);
  }
  async function poll(own) {
    if (!current(own) || own.fetching || own.pending) return;
    own.fetching = true; const revision = own.revision;
    try {
      const result = await request.get("/account", { signal: own.layer.signal });
      if (current(own) && revision === own.revision && !own.pending) { own.readError = ""; apply(own, result.data); }
    } catch (error) { if (current(own) && revision === own.revision && error.name !== "AbortError") { own.readError = errorMessage(error); render(own); } }
    finally { own.fetching = false; schedule(own); }
  }
  async function submit(own, browser = false) {
    if (!current(own) || own.pending || (!browser && own.snapshot?.state === "signing_in") || (browser && !allowBrowser())) return;
    own.pending = true; own.attempted = true; own.error = own.readError = ""; own.revision++; clearTimeout(own.timer);
    const body = browser ? { remember: own.remember.checked } : {
      identifier: own.identifier.value.trim(), password: own.password.value, remember: own.remember.checked };
    own.password.value = ""; render(own);
    try {
      const result = await request.post("/account/login", body, { signal: own.layer.signal });
      if (!current(own)) return;
      if (browser) {
        const url = new URL(result.data.authorization_url);
        if (!["https:", "http:"].includes(url.protocol)) throw new Error(copy("invalidService"));
        own.authorizationUrl = url.href;
      } else own.authorizationUrl = "";
      apply(own, result.data);
    } catch (error) { if (current(own) && error.name !== "AbortError") { own.error = errorMessage(error); render(own); } }
    finally {
      if (!browser) body.password = "";
      own.pending = false;
      if (current(own)) { render(own); void poll(own); schedule(own); }
    }
  }
  function open(snapshot = null) {
    if (disposed) return Promise.resolve(false);
    if (view) return view.done;
    // A second caller cannot replace a form containing an unsubmitted password.
    if (activeWindow) return activeWindow.scope === scope() ? activeWindow.done : Promise.resolve(false);
    const own = { scope: scope(), snapshot, labels: [], attempted: false, success: false, pending: false, fetching: false, error: "", readError: "", revision: 0, timer: 0, authorizationUrl: "" };
    let resolve; own.done = new Promise(done => { resolve = done; });
    own.layer = openLayer({ title: copy("login"), icon: "shield", width: "32rem", height: "auto",
      async beforeClose() {
        if (own.success || (!own.attempted && !["authorizing", "signing_in"].includes(own.snapshot?.state)) || !current(own)) return true;
        if (own.pending) return false;
        own.pending = true; own.revision++; render(own);
        try { await request.post("/account/cancel", {}, { signal: own.layer.signal }); return true; }
        catch (error) { if (current(own)) { own.error = errorMessage(error); } return false; }
        finally { own.pending = false; render(own); }
      },
      onClosed() {
        if (own.password) own.password.value = "";
        clearTimeout(own.timer);
        if (view === own) view = null;
        if (activeWindow === own) activeWindow = null;
        resolve(own.success);
      } });
    own.layer.element.id = "account-login-dialog"; own.layer.element.classList.add("account-login-window");
    own.status = el("p", "account-login-progress"); own.status.setAttribute("data-account-progress", "");
    own.link = el("a", "", { target: "_blank", rel: "noopener noreferrer" }); own.labels.push([own.link, "reopen"]);
    own.form = el("form", "account-login-form", { id: "account-password-form" });
    for (const [key, type, max] of [["identifier", "text", 254], ["password", "password", 128]]) {
      const label = el("label", "account-field"), caption = el("span"); own.labels.push([caption, key]);
      own[key] = el("input", "", { name: key, type, autocomplete: key === "password" ? "current-password" : "username", maxlength: max, required: "", autocapitalize: "none", spellcheck: "false" });
      label.append(caption, own[key]); own.form.append(label);
    }
    const remember = el("label", "account-remember"), caption = el("span"); own.labels.push([caption, "remember"]);
    own.remember = el("input", "", { type: "checkbox", name: "remember" }); own.remember.checked = snapshot?.persistence_available !== false;
    remember.append(own.remember, caption); own.form.append(remember);
    own.submit = el("button", "primary-button account-password-submit", { type: "submit" }); own.labels.push([own.submit, "login"]); own.form.append(own.submit);
    own.storage = el("p", "account-note");
    own.browser = el("button", "secondary-button", { type: "button" });
    own.cancel = el("button", "secondary-button", { type: "button" }); own.labels.push([own.cancel, "cancel"]);
    own.layer.body.append(own.status, own.link, own.form, own.storage); own.layer.footer.append(own.browser, own.cancel);
    const options = { signal: own.layer.signal };
    own.form.addEventListener("submit", event => { event.preventDefault(); void submit(own); }, options);
    own.browser.addEventListener("click", () => { void submit(own, true); }, options);
    own.cancel.addEventListener("click", () => { void own.layer.close("cancel"); }, options);
    view = activeWindow = own; render(own); own.identifier.focus({ preventScroll: true }); void poll(own);
    return own.done;
  }
  const locale = subscribeLocale(() => { if (view) render(view); });
  const targets = subscribeTarget(() => { if (view && !current(view)) view.layer.dispose(); });
  return { open, dispose() { disposed = true; view?.layer.dispose(); locale(); targets(); } };
}
