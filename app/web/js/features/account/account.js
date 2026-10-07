import { api } from "../../api/client.js";
import { currentLocale, subscribeLocale, t } from "../../i18n.js";
import { toast, errorMessage } from "../../utils/dom.js";
import { isRemoteTarget } from "../../api/target.js";
import { allowanceView, searchAllowanceView } from "./allowance.js";

// Tokens stay in the native host. Passwords are submitted once from the form,
// then cleared; account rendering uses only filtered public snapshots.
export function createAccount({ navigation, onModelsChange = () => {} }) {
  const panel = document.querySelector("#account-panel");
  // Benefits belong to the signed-out state, below its status line.
  panel.querySelector("[data-i18n='account.description']")?.remove();
  const sidebar = document.querySelector("#open-account");
  const sidebarQuota = document.createElement("button");
  sidebarQuota.className = "sidebar-model-allowance"; sidebarQuota.type = "button"; sidebarQuota.hidden = true;
  sidebar.closest(".sidebar-identity-row").after(sidebarQuota);
  const identity = document.createElement("span"); identity.className = "account-identity";
  const name = sidebar.querySelector("[data-account-name]"); name.replaceWith(identity); identity.append(name);
  const group = document.createElement("span"); group.className = "account-group"; identity.append(group);
  const vipBadge = document.createElement("span"); vipBadge.className = "account-vip-badge"; vipBadge.textContent = "VIP";
  vipBadge.setAttribute("aria-hidden", "true"); sidebar.querySelector("[data-account-avatar]").append(vipBadge);
  let modelGeneration = null;
  const dialog = document.querySelector("#account-login-dialog");
  const notice = document.querySelector("#account-search-notice");
  const remember = dialog.querySelector("[name=remember]");
  const loginForm = dialog.querySelector("#account-password-form");
  const link = dialog.querySelector("[data-account-link]");
  let snapshot = null, timer = null, fetching = false, acting = false, disposed = false;
  let authorizationUrl = "", returnFocus = null, returnOwner = null, returnAction = "", dialogLogin = false;
  const copy = (key, values = {}) => t(`account.${key}`, values);
  const displayName = () => snapshot?.profile?.nickname || snapshot?.profile?.username || copy("signedIn");
  const signedIn = () => snapshot?.state === "signed_in" ||
    (snapshot?.state === "authorizing" && Boolean(snapshot?.profile));
  function text(tag, value, className) {
    const node = document.createElement(tag); node.textContent = value;
    if (className) node.className = className;
    return node;
  }
  function button(label, action, primary = false) {
    const node = text("button", label, primary ? "primary-button" : "secondary-button");
    node.type = "button"; node.dataset.accountAction = action; node.disabled = acting;
    return node;
  }
  function render() {
    if (!snapshot) return;
    const focused = document.activeElement;
    const focusOwner = panel.contains(focused) ? panel : notice.contains(focused) ? notice : null;
    const focusAction = focused?.dataset?.accountAction, focusId = focused?.dataset?.id;
    sidebar.querySelector("[data-account-name]").textContent = signedIn() ? displayName() : copy("login");
    const avatar = sidebar.querySelector("[data-account-avatar]");
    avatar.replaceChildren(document.createTextNode(signedIn() ? [...displayName()][0]?.toUpperCase() || "◉" : "◉"), vipBadge);
    const allowance = signedIn() ? allowanceView(snapshot.model_allowance) : null;
    const vip = Boolean(allowance?.vip); sidebar.classList.toggle("is-vip", vip); vipBadge.hidden = !vip;
    group.hidden = !signedIn(); group.textContent = !allowance ? copy("groupLoading") : allowance.group === "vip" ? "VIP" :
      allowance.group === "free" ? copy("freeGroup") : allowance.group;
    renderQuotas(sidebarQuota, allowance, true);
    sidebarQuota.hidden = !signedIn();
    sidebar.title = signedIn() ? copy("title") : copy("login");
    const body = panel.querySelector("[data-account-body]");
    const stateText = copy(snapshot.state === "signing_in" ? "signingIn" : snapshot.state === "signed_in" ? "signedIn" :
      snapshot.state === "authorizing" ? "waitingBrowser" : snapshot.state === "refreshing" ? "refreshing" :
      snapshot.state === "expired" ? "expired" : "signedOut");
    const status = text("p", signedIn() ? copy("greeting", { name: displayName() }) : stateText, "account-state");
    status.setAttribute("role", "status");
    const nodes = [status];
    if (!signedIn()) nodes.push(text("p", copy("description"), "account-note account-benefits"));
    if (snapshot.profile) {
      const details = text("dl", "", "account-details");
      for (const [key, value] of [["username", snapshot.profile.username], ["phone", snapshot.profile.phone], ["email", snapshot.profile.email]]) {
        if (!value) continue;
        details.append(text("dt", copy(key)), text("dd", value));
      }
      for (const [key, value] of [["phoneVerified", snapshot.profile.phone_verified], ["emailVerified", snapshot.profile.email_verified]])
        details.append(text("dt", copy(key)), text("dd", copy(value ? "verified" : "unverified")));
      nodes.push(details);
      if (allowance) {
        details.append(text("dt", copy("userGroup")), text("dd", group.textContent));
        const meters = text("section", "", "account-model-allowance"); renderQuotas(meters, allowance, false); nodes.push(meters);
      }
    }
    if (snapshot.message) nodes.push(text("p", copy(snapshot.message), "account-hint"));
    if ([403, 429, 503].includes(snapshot.search_status))
      nodes.push(text("p", copy(`search${snapshot.search_status}`), "account-hint"));
    if (signedIn()) nodes.push(renderSearchQuota());
    if (signedIn() || snapshot.state === "refreshing")
      nodes.push(text("p", copy(snapshot.remembered ? "remembered" : "temporary"), "account-note"));
    const actions = text("div", "", "account-actions");
    actions.append(button(copy(signedIn() ? "switch" : "login"), "login", true));
    if (snapshot.state === "authorizing") actions.append(button(copy("cancel"), "cancel"));
    if (signedIn() || snapshot.state === "expired" || snapshot.state === "refreshing") {
      actions.append(button(copy("manage"), "website"), button(copy("logout"), "logout"));
    }
    nodes.push(actions); body.replaceChildren(...nodes);
    remember.disabled = !snapshot.persistence_available || acting;
    if (!snapshot.persistence_available) remember.checked = false;
    dialog.querySelector("[data-account-storage]").textContent = copy(snapshot.persistence_available ? "storage" : "storageUnavailable");
    dialog.querySelector("[data-account-progress]").textContent = snapshot.state === "signing_in" ? copy("signingIn") :
      snapshot.state === "authorizing" ? copy("waitingBrowser") : dialogLogin && snapshot.message ? copy(snapshot.message) : copy("directDescription");
    loginForm.hidden = snapshot.state === "authorizing";
    loginForm.querySelector("[type=submit]").disabled = acting || snapshot.state === "signing_in";
    link.hidden = !authorizationUrl;
    if (authorizationUrl) link.href = authorizationUrl;
    dialog.querySelector("[data-account-action=begin]").hidden = isRemoteTarget() || snapshot.state === "authorizing";
    dialog.querySelector("[data-account-action=restart]").hidden = isRemoteTarget() || snapshot.state !== "authorizing";
    const waits = snapshot.pending_searches || [];
    notice.hidden = !waits.length;
    if (waits.length) {
      const items = [text("p", copy("searchWaiting")), button(copy("login"), "login", true)];
      for (const wait of waits) {
        const row = text("div", "", "account-search-row"); row.append(text("span", wait.query));
        const skip = button(copy("skip"), "skip"); skip.dataset.id = String(wait.id); row.append(skip); items.push(row);
      }
      notice.replaceChildren(...items);
    }
    if (focusOwner && focusAction) [...focusOwner.querySelectorAll("[data-account-action]")]
      .find((node) => node.dataset.accountAction === focusAction && node.dataset.id === focusId)?.focus({ preventScroll: true });
    if (dialog.open && dialogLogin && snapshot.state !== "authorizing" && !snapshot.busy) {
      if (snapshot.state === "signed_in" && !snapshot.message) { dialogLogin = false; dialog.close(); toast(copy("success"), "success"); }
    }
  }
  function renderQuotas(parent, allowance, compact) {
    parent.replaceChildren(); parent.setAttribute("aria-label", copy("modelAllowance"));
    if (!allowance) { parent.append(text("span", copy(snapshot?.models_status && snapshot.models_status !== 200 ? "allowanceUnavailable" : "allowanceLoading"), "account-quota-note")); return; }
    if (!compact) parent.append(text("h3", copy("modelAllowance")), text("p", copy("tokenReset"), "account-note"));
    for (const q of allowance.quotas) {
      const counts = q.unlimited ? copy("tokensUsed", { count: q.used_tokens.toLocaleString() }) :
        copy("tokensRemaining", { remaining: q.remaining_tokens.toLocaleString(), limit: q.limit_tokens.toLocaleString() });
      const row = quotaRow(q, copy("modelRemaining", { model: q.title }));
      row.title = counts + " · " + copy("tokenReset");
      if (!compact) {
        row.append(text("p", counts, "account-note"));
        if (q.reserved_tokens) row.append(text("p", copy("tokensReserved", { count: q.reserved_tokens.toLocaleString() }), "account-note"));
      }
      parent.append(row);
    }
    if (!allowance.quotas.length) parent.append(text("span", copy("allowanceUnavailable"), "account-quota-note"));
  }
  function quotaRow(quota, label) {
    const row = text("div", "", "account-quota-row"), header = text("div", "", "account-quota-heading");
    header.append(text("span", quota.title), text("strong", quota.stale ? copy("quotaUpdating") : quota.unlimited ? copy("unlimited") : quota.percentText));
    row.append(header); row.classList.toggle("is-low", !quota.unlimited && quota.percentage < 10);
    if (!quota.unlimited) {
      const bar = text("div", "", "account-quota-bar"); bar.setAttribute("role", "progressbar");
      bar.setAttribute("aria-label", label); bar.setAttribute("aria-valuemin", "0"); bar.setAttribute("aria-valuemax", "100");
      bar.setAttribute("aria-valuenow", String(quota.percentage)); bar.setAttribute("aria-valuetext", quota.stale ? copy("quotaUpdating") : quota.percentText);
      const fill = text("span", ""); fill.style.width = `${quota.percentage}%`; bar.append(fill); row.append(bar);
    }
    return row;
  }
  function renderSearchQuota() {
    const meters = text("section", "", "account-search-allowance");
    meters.append(text("h3", copy("searchAllowance")));
    const quota = searchAllowanceView(snapshot.usage);
    if (!quota) { meters.append(text("p", copy(snapshot.search_status && snapshot.search_status !== 200 ? "allowanceUnavailable" : "allowanceLoading"), "account-quota-note")); return meters; }
    quota.title = copy("quotaRemaining"); quota.stale ||= snapshot.search_status !== 200;
    const row = quotaRow(quota, copy("searchAllowance"));
    row.append(text("p", copy("searchesRemaining", { remaining: quota.remaining.toLocaleString(), limit: quota.limit.toLocaleString() }), "account-quota-count"));
    row.append(text("p", copy("searchesUsed", { count: quota.used.toLocaleString() }), "account-note"));
    meters.append(row, text("p", copy("searchReset", { time: new Date(quota.resets_at * 1000).toLocaleString(currentLocale(),
      { month: "numeric", day: "numeric", hour: "2-digit", minute: "2-digit" }) }), "account-note"));
    return meters;
  }
  function schedule() {
    clearTimeout(timer);
    if (!disposed) timer = setTimeout(refresh, document.hidden ? 30000 :
      snapshot?.state === "authorizing" || snapshot?.busy || snapshot?.pending_searches?.length ? 1000 : 10000);
  }
  async function refresh() {
    if (fetching || disposed) return;
    fetching = true;
    try {
      snapshot = (await api.get("/account")).data;
      if (!document.hidden && snapshot.state === "refreshing" && snapshot.refresh_available && !snapshot.busy) {
        await api.post("/account/refresh", {});
        snapshot = (await api.get("/account")).data;
      }
      render();
      if (modelGeneration !== snapshot.model_generation) {
        modelGeneration = snapshot.model_generation; void Promise.resolve(onModelsChange()).catch(() => {});
      }
    }
    catch { /* Local host reconnection uses the existing global connection UI. */ }
    finally { fetching = false; schedule(); }
  }
  function openLogin() {
    if (dialog.open) return;
    returnFocus = document.activeElement;
    returnOwner = panel.contains(returnFocus) ? panel : notice.contains(returnFocus) ? notice : null;
    returnAction = returnFocus?.dataset?.accountAction || "";
    remember.checked = snapshot?.persistence_available !== false;
    dialogLogin = false; loginForm.elements.password.value = "";
    dialog.showModal(); render(); loginForm.elements.identifier.focus();
  }
  loginForm.addEventListener("submit", async event => {
    event.preventDefault();
    if (acting || snapshot?.state === "signing_in") return;
    acting = true; dialogLogin = true;
    const password = loginForm.elements.password.value;
    loginForm.elements.password.value = "";
    try {
      snapshot = (await api.post("/account/login", {identifier: loginForm.elements.identifier.value.trim(), password, remember: remember.checked})).data;
      authorizationUrl = "";
      render();
    } catch (error) { dialogLogin = false; toast(errorMessage(error), "error"); }
    finally { acting = false; await refresh(); }
  });
  async function action(event) {
    const target = event.target.closest("[data-account-action]");
    if (!target || acting) return;
    const kind = target.dataset.accountAction;
    if (kind === "login") { openLogin(); return; }
    acting = true;
    try {
      if (kind === "begin" || kind === "restart") {
        const result = (await api.post("/account/login", { remember: remember.checked })).data;
        const url = new URL(result.authorization_url);
        if (!["https:", "http:"].includes(url.protocol)) throw new Error(copy("invalidService"));
        authorizationUrl = url.href; dialogLogin = true;
      } else if (kind === "skip") await api.post("/account/search/skip", { id: Number(target.dataset.id) });
      else {
        await api.post(`/account/${kind}`, {});
        if (kind === "cancel") { dialogLogin = false; authorizationUrl = ""; dialog.close(); }
        if (kind === "logout") authorizationUrl = "";
      }
    } catch (error) { toast(errorMessage(error), "error"); }
    finally { acting = false; await refresh(); }
  }
  sidebar.addEventListener("click", () => navigation.openSettings("account"));
  sidebarQuota.addEventListener("click", () => navigation.openSettings("account"));
  panel.addEventListener("click", action); dialog.addEventListener("click", action); notice.addEventListener("click", action);
  dialog.addEventListener("cancel", (event) => {
    event.preventDefault(); void action({ target: dialog.querySelector("[data-account-action=cancel]") });
  });
  dialog.addEventListener("close", () => {
    loginForm.elements.password.value = "";
    const replacement = returnOwner && returnAction ? [...returnOwner.querySelectorAll("[data-account-action]")]
      .find((node) => node.dataset.accountAction === returnAction) : null;
    const next = [returnFocus, replacement, document.querySelector("#prompt"), panel.querySelector("button"), sidebar]
      .find((node) => node?.isConnected && node.getClientRects().length && !node.closest("[inert]"));
    next?.focus({ preventScroll: true });
  });
  const receiveLink = async (event) => {
    if (isRemoteTarget()) return;
    const callback = typeof event === "string" ? event : event.detail;
    if (typeof callback !== "string") return;
    try { await api.post("/account/callback", { callback_url: callback }); }
    catch (error) { toast(errorMessage(error), "error"); }
    await refresh();
  };
  window.addEventListener("xs-app-link", receiveLink);
  window.__xsAppLinksReady = !isRemoteTarget();
  try { const pending = !isRemoteTarget() && window.XsAppLinks?.take(); if (pending) void receiveLink(pending); } catch { /* Ordinary browser. */ }
  window.addEventListener("focus", refresh);
  document.addEventListener("visibilitychange", () => { if (!document.hidden) void refresh(); else schedule(); });
  subscribeLocale(render); void refresh();
  return { refresh, dispose() { disposed = true; clearTimeout(timer); window.removeEventListener("xs-app-link", receiveLink); window.removeEventListener("focus", refresh); } };
}
