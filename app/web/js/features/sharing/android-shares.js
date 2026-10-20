import { api, currentPageWriteToken } from "../../api/client.js";
import { isRemoteTarget, subscribeTarget, targetState } from "../../api/target.js";
import { createSession } from "../../state/sessions.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";
import { importShare } from "./import-share.js";

const text = (key, values = {}) => t(`share.${key}`, values);

export function createAndroidShares({ draftStore, navigation, projectsStore, sessionsStore,
  modelsStore, canWrite, onImported, mount = document.body }) {
  const native = globalThis.MdoShares;
  if (!native) return { hasPending: () => false };
  const bridge = Object.fromEntries(["peek", "bind", "read", "plan", "acknowledge"].map(name => [name, (...args) => {
    const result = native[name](globalThis.__mdoShareToken || "", ...args.map(value =>
      value && typeof value === "object" ? JSON.stringify(value) : value));
    return ["peek", "bind"].includes(name) ? JSON.parse(result || "null") : result;
  }]));
  const open = element("button", { className: "android-share-open", attrs: { type: "button", hidden: true } });
  const heading = element("h2"), summary = element("p"), files = element("ul"), limits = element("p", { className: "android-share-limits" });
  const projects = element("select"), sessions = element("select");
  const error = element("p", { className: "dialog-error", attrs: { role: "alert", hidden: true } });
  const progress = element("p", { attrs: { role: "status" } });
  const receive = element("button", { className: "primary-button", attrs: { type: "button" } });
  const later = element("button", { className: "secondary-button", attrs: { type: "button" } });
  const discard = element("button", { className: "secondary-button", attrs: { type: "button" } });
  const projectLabel = element("span"), sessionLabel = element("span");
  const dialog = element("dialog", { className: "app-dialog android-share-dialog" }, [heading, summary, files, limits,
    element("label", {}, [projectLabel, projects]), element("label", {}, [sessionLabel, sessions]), progress, error,
    element("div", { className: "dialog-actions" }, [discard, later, receive])]);
  document.body.append(dialog); mount.append(open);
  let share = null, busy = false, deferred = "", origin = null;
  function local() { return !isRemoteTarget() && !targetState().runtimeChanged && canWrite(); }
  function labels() {
    heading.textContent = text("title"); dialog.setAttribute("aria-label", text("title"));
    open.textContent = text("open"); open.title = local() ? text("title") : text("localOnly");
    projectLabel.textContent = text("project"); sessionLabel.textContent = text("conversation");
    receive.textContent = text("receive"); later.textContent = text("later"); discard.textContent = text("discard");
    limits.textContent = text("limits");
  }
  function choices() {
    if (!share || busy) return;
    const wanted = share.destination?.project_id || projects.value || navigation.preferredProject();
    clear(projects);
    const items = [{ id: "default", name: text("defaultProject") }, ...(projectsStore.get().data?.items || [])];
    const seen = new Set();
    for (const item of items) if (!seen.has(item.id)) {
      seen.add(item.id); projects.append(element("option", { text: item.name || item.id, attrs: { value: item.id } }));
    }
    if (share.destination && !seen.has(wanted)) projects.append(element("option", { text: wanted, attrs: { value: wanted } }));
    projects.value = seen.has(wanted) || share.destination ? wanted : "default";
    const selected = share.destination?.session_id || sessions.value ||
      (navigation.get().projectId === projects.value ? navigation.get().sessionId : "");
    clear(sessions); sessions.append(element("option", { text: text("newConversation"), attrs: { value: "" } }));
    for (const item of sessionsStore.get().data?.items || []) if (item.project_id === projects.value && item.status === "active")
      sessions.append(element("option", { text: item.title, attrs: { value: item.id } }));
    if (share.destination && ![...sessions.options].some(option => option.value === selected))
      sessions.append(element("option", { text: selected, attrs: { value: selected } }));
    sessions.value = [...sessions.options].some(option => option.value === selected) ? selected : "";
    controls();
  }
  function controls() {
    receive.disabled = busy || !local() || Boolean(share?.error);
    discard.disabled = later.disabled = busy;
    projects.disabled = sessions.disabled = busy || Boolean(share?.destination);
  }
  function show() {
    if (!share || busy || dialog.open || document.querySelector("dialog[open]")) return;
    origin = document.activeElement;
    summary.textContent = share.text || text("fileCount", { count: share.files.length });
    clear(files); for (const file of share.files) files.append(element("li", { text: file.name }));
    progress.textContent = ""; error.hidden = !share.error;
    error.textContent = share.error ? text("incomplete") : "";
    choices(); labels(); controls(); dialog.showModal(); later.focus();
  }
  function poll() {
    if (busy || !globalThis.__mdoShareToken) return;
    try {
      const next = bridge.peek();
      if (next && (!Array.isArray(next.files) || next.files.length > 32 || !/^[0-9a-f]{32}$/.test(next.id))) return;
      if (share?.id !== next?.id && dialog.open) dialog.close();
      share = next; open.hidden = !share; labels(); controls();
      if (share && share.id !== deferred && local()) show();
    } catch { /* A detached/recreated Android shell will supply another token. */ }
  }
  receive.addEventListener("click", async () => {
    if (!share || busy || !local()) return;
    busy = true; controls(); error.hidden = true;
    const epoch = currentPageWriteToken();
    const check = () => { if (!local() || currentPageWriteToken() !== epoch) throw new Error(text("localOnly")); };
    try {
      // Drain pending edits before choosing the destination's exact draft.
      if (!await draftStore.flushAll()) throw new Error(text("draftFailed"));
      const result = await importShare({ share, bridge, draftStore, api, createSession, check,
        supportsImages: session => {
          const modelId = draftStore.composerProfile?.(`${session.project_id}/${session.id}`)?.model_id || session.model_id;
          return Boolean(Number(modelsStore.get().data?.models?.find(model => model.id === modelId)?.attachments) & 1);
        },
        destination: share.destination || { project_id: projects.value, session_id: sessions.value || share.id, create: !sessions.value },
        onProgress: (index, count) => { progress.textContent = text("progress", { index, count }); },
        onConflict: async () => window.confirm(text("confirmAgain")) });
      dialog.close(); share = null; open.hidden = true;
      onImported?.(result); toast(text("done"));
    } catch (failure) {
      try { share = bridge.peek() || share; } catch { }
      error.textContent = errorMessage(failure); error.hidden = false;
    } finally { busy = false; controls(); choices(); poll(); }
  });
  later.addEventListener("click", () => dialog.close());
  dialog.addEventListener("cancel", event => { if (busy) event.preventDefault(); });
  dialog.addEventListener("close", () => { deferred = share?.id || ""; origin?.isConnected && origin.focus(); });
  discard.addEventListener("click", () => {
    if (!share || busy || !window.confirm(text("confirmDiscard"))) return;
    try {
      if (!bridge.acknowledge(share.id)) throw new Error(text("discardFailed"));
      dialog.close(); share = null; poll();
    } catch { error.textContent = text("discardFailed"); error.hidden = false; }
  });
  open.addEventListener("click", show); projects.addEventListener("change", () => { sessions.value = ""; choices(); });
  const unsubscribers = [projectsStore.subscribe(choices), sessionsStore.subscribe(choices), subscribeLocale(labels), subscribeTarget(poll)];
  globalThis.addEventListener("mdo-shares-ready", poll);
  const timer = setInterval(poll, 2000); poll();
  return { hasPending: () => Boolean(share), destroy() { clearInterval(timer); unsubscribers.forEach(fn => fn());
    globalThis.removeEventListener("mdo-shares-ready", poll); dialog.remove(); open.remove(); } };
}
