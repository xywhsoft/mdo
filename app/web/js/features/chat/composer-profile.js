import { updateSessionProfile } from "../../state/sessions.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";

const EFFORT_LABEL = Object.freeze({
  none: ["reasoning.none", "无思考"],
  minimal: ["reasoning.minimal", "极低"],
  low: ["reasoning.low", "低"],
  medium: ["reasoning.medium", "中"],
  high: ["reasoning.high", "高"],
  xhigh: ["reasoning.xhigh", "极高"],
  max: ["reasoning.max", "最大"],
});

function selectedModel(models, id) {
  return models.find((model) => model.id === id) ?? null;
}

export function fillReasoningOptions(select, model, preferred = "") {
  const efforts = model?.reasoning_efforts?.length
    ? model.reasoning_efforts : (preferred ? [preferred] : []);
  clear(select);
  for (const effort of efforts)
    select.append(element("option", { text: EFFORT_LABEL[effort]
      ? t(EFFORT_LABEL[effort][0], {}, EFFORT_LABEL[effort][1]) : effort,
      attrs: { value: effort } }));
  const chosen = efforts.includes(preferred) ? preferred
    : efforts.includes(model?.default_reasoning_effort)
      ? model.default_reasoning_effort : efforts[0] || "";
  select.value = chosen;
  select.disabled = efforts.length === 0;
  return chosen;
}

export function createComposerProfile({ modelSelect, reasoningSelect,
  permissionSelect, navigation, sessionStore, modelsStore, agentsStore,
  projectsStore, isRunActive, onBusyChange, onSelectionChange }) {
  // Blank tasks have no session metadata. Keep manual choices per project, but
  // continue following its configured default until the user picks a model.
  const drafts = new Map();
  const busy = new Set();
  let runActive = false;

  function models() { return modelsStore.get().data?.models ?? []; }
  function selectedKey() {
    const route = navigation.get();
    return route.sessionId ? `${route.projectId}/${route.sessionId}` : "";
  }
  function current() {
    const route = navigation.get();
    const session = sessionStore.get().data;
    return route.sessionId && session?.id === route.sessionId &&
      session.project_id === route.projectId ? session : null;
  }
  function draft() {
    const projectId = navigation.get().projectId || "default";
    if (!drafts.has(projectId)) drafts.set(projectId, {
      model_id: "", reasoning_effort: "", permission_profile: "balanced",
    });
    return drafts.get(projectId);
  }
  function defaultModelId() {
    const projectId = navigation.get().projectId || "default";
    const project = (projectsStore.get().data?.items ?? []).find((item) =>
      item.id === projectId);
    return project?.default_model_id ||
      modelsStore.get().data?.default_model_id || models()[0]?.id || "";
  }
  function agentPermission(session) {
    const agent = agentsStore.get().data?.items?.find((item) =>
      item.id === (session?.agent_id || "mdo.default"));
    return session?.permission_profile || agent?.permission_profile || "balanced";
  }

  function sync() {
    const session = current();
    const pending = draft();
    const catalog = models();
    const id = session?.model_id || pending.model_id || defaultModelId();
    clear(modelSelect);
    for (const model of catalog) {
      const suffix = model.free ? t("model.freeSuffix", {}, " · 免费") : "";
      modelSelect.append(element("option", {
        text: `${model.name || model.id}${suffix}`,
        attrs: { value: model.id },
      }));
    }
    if (id && !selectedModel(catalog, id))
      modelSelect.append(element("option", { text: id, attrs: { value: id } }));
    modelSelect.value = id;
    const model = selectedModel(catalog, id);
    const effort = session?.reasoning_effort || pending.reasoning_effort ||
      model?.default_reasoning_effort || "";
    fillReasoningOptions(reasoningSelect, model, effort);
    permissionSelect.value = session ? agentPermission(session) : pending.permission_profile;
    const disabled = busy.has(selectedKey()) || runActive ||
      (Boolean(navigation.get().sessionId) && !session) ||
      (session && session.status !== "active");
    modelSelect.disabled = disabled || catalog.length === 0;
    reasoningSelect.disabled = disabled || !reasoningSelect.options.length;
    permissionSelect.disabled = disabled;
    onSelectionChange?.();
  }

  async function changed(field) {
    const session = current();
    if (navigation.get().sessionId && !session) { sync(); return; }
    const model = selectedModel(models(), modelSelect.value);
    const effort = fillReasoningOptions(reasoningSelect, model,
      reasoningSelect.value);
    const profile = {
      model_id: modelSelect.value,
      reasoning_effort: effort,
      permission_profile: permissionSelect.value,
    };
    if (!session) {
      const pending = draft();
      if (field === "model") {
        pending.model_id = profile.model_id;
        pending.reasoning_effort = profile.reasoning_effort;
      } else if (field === "reasoning") {
        pending.reasoning_effort = profile.reasoning_effort;
      } else pending.permission_profile = profile.permission_profile;
      sync();
      return;
    }
    const key = `${session.project_id}/${session.id}`;
    if (busy.has(key) || isRunActive() || session.status !== "active") {
      sync();
      return;
    }
    busy.add(key);
    onBusyChange(true);
    sync();
    try {
      const updated = await updateSessionProfile(session, profile);
      const selected = navigation.get();
      if (selected.projectId === updated.project_id &&
          selected.sessionId === updated.id) sessionStore.setData(updated);
      toast(selected.projectId === updated.project_id &&
        selected.sessionId === updated.id ? t("profile.updated", {}, "会话配置已更新") :
        t("profile.backgroundUpdated", { title: session.title },
          `后台会话“${session.title}”配置已更新`));
    } catch (error) {
      const selected = navigation.get();
      toast(selected.projectId === session.project_id &&
        selected.sessionId === session.id ? errorMessage(error) :
        t("profile.backgroundFailed", { title: session.title, error: errorMessage(error) },
          `后台会话“${session.title}”配置更新失败：${errorMessage(error)}`), "error");
      if (selected.projectId === session.project_id &&
          selected.sessionId === session.id) sync();
    } finally {
      busy.delete(key);
      onBusyChange(false);
      sync();
    }
  }

  modelSelect.addEventListener("change", () => {
    const model = selectedModel(models(), modelSelect.value);
    fillReasoningOptions(reasoningSelect, model,
      model?.default_reasoning_effort || "");
    void changed("model");
  });
  reasoningSelect.addEventListener("change", () => { void changed("reasoning"); });
  permissionSelect.addEventListener("change", () => { void changed("permission"); });
  const unsubscribers = [modelsStore.subscribe(sync), agentsStore.subscribe(sync),
    projectsStore.subscribe(sync), sessionStore.subscribe(sync),
    navigation.subscribe(sync), subscribeLocale(sync)];
  return Object.freeze({
    selection: () => ({
      model_id: modelSelect.value,
      reasoning_effort: reasoningSelect.value,
      permission_profile: permissionSelect.value,
    }),
    setRunActive(value) { runActive = Boolean(value); sync(); },
    isBusy: () => busy.has(selectedKey()),
    sync,
    destroy: () => unsubscribers.forEach((unsubscribe) => unsubscribe()),
  });
}
