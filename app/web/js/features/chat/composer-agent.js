import { t, subscribeLocale, setUiText } from "../../i18n.js";
import { updateSessionProfile } from "../../state/sessions.js";
import { errorMessage, toast } from "../../utils/dom.js";
import { subscribeTarget, targetState } from "../../api/target.js";

export const agentLabel = agent => agent?.id === "mdo.default" ? t("agent.defaultName", {}, "墨斗") : agent?.name || agent?.id || "";
export function createComposerAgent({ select, navigation, sessionStore, agentsStore, profile,
  blocked = () => false, onBusy = () => {}, onSelected = () => {}, onSync = () => {}, beforeSelect = async () => {}, update = updateSessionProfile }) {
  const drafts = new Map(); let busy = false, stopped = false, controller = null, last = "";
  const current = () => {
    const route = navigation.get(), session = sessionStore.get().data;
    return session?.id === route.sessionId && session?.project_id === route.projectId ? session : null;
  };
  const selection = () => current()?.agent_id || drafts.get(navigation.get().projectId || "default") || "mdo.default";
  function sync() {
    if (stopped) return;
    const route = navigation.get(), session = current(), id = selection();
    // MAIN is bit zero in the public Agent descriptor; SubAgent-only roles do not appear here.
    const agents = (agentsStore.get().data?.items || []).filter(a => Number(a.flags) & 1);
    select.replaceChildren();
    for (const agent of agents) {
      const option = select.ownerDocument.createElement("option"); option.value = agent.id;
      setUiText(option, agentLabel(agent)); select.append(option);
    }
    if (!agents.some(a => a.id === id)) {
      const option = select.ownerDocument.createElement("option"); option.value = id;
      setUiText(option, id === "mdo.default" ? agentLabel({ id }) : id); option.disabled = true; select.append(option);
    }
    select.value = id; select.title = t("composer.chooseAgent", {}, "选择 Agent"); select.setAttribute("aria-label", select.title);
    select.disabled = busy || blocked() || (Boolean(route.sessionId) && (!session || session.status !== "active"));
    if (select.disabled) select.title = t("composer.agentSwitchIdle", {}, "请等待当前任务和待发送消息结束后切换 Agent");
    const context = { agent: id, project: route.projectId || "default", session: session?.id || "", view: route.view,
      target: JSON.stringify([targetState().selected, targetState().runtimeChanged]) };
    const key = JSON.stringify(context);
    if (key !== last) { last = key; onSelected(route.view === "workspace" ? context : null); }
    onSync();
  }
  async function change() {
    if (busy || blocked()) { sync(); return; }
    const id = select.value, route = { ...navigation.get() }, session = current();
    if (session?.agent_id === id) return;
    if (!session) { if (!route.sessionId) drafts.set(route.projectId || "default", id); sync(); return; }
    busy = true; controller = new AbortController(); onBusy(true); sync();
    try {
      await beforeSelect(id, controller.signal);
      if (controller.signal.aborted) return;
      const updated = await update(session, { ...profile(), agent_id: id }, { signal: controller.signal });
      const now = navigation.get();
      if (now.projectId === route.projectId && now.sessionId === route.sessionId && !controller.signal.aborted)
        sessionStore.setData(updated);
    } catch (error) { if (error.name !== "AbortError") toast(errorMessage(error), "error"); }
    finally { busy = false; controller = null; onBusy(false); sync(); }
  }
  select.addEventListener("change", change);
  const unsubs = [navigation.subscribe(() => { controller?.abort(); sync(); }), sessionStore.subscribe(sync), agentsStore.subscribe(sync),
    subscribeLocale(sync), subscribeTarget(() => { controller?.abort(); drafts.clear(); last = ""; sync(); })];
  sync();
  return { selection, sync, isBusy: () => busy, destroy() { stopped = true; controller?.abort(); unsubs.forEach(fn => fn()); select.removeEventListener("change", change); } };
}
