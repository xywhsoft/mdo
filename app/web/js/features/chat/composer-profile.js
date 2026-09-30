import { updateSessionProfile } from "../../state/sessions.js";
import { projectDraftKey } from "./draft-store.js";
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

export function fillAgentOptions(select, agents) {
  const previous = select.value;
  clear(select);
  for (const agent of agents) select.append(element("option", {
    text: agent.name || agent.id, attrs: { value: agent.id },
  }));
  const available = [...select.options].map((option) => option.value);
  select.value = available.includes(previous) ? previous :
    available.includes("mdo.default") ? "mdo.default" : available[0] || "";
}

export function agentProfileDefaults(agent, fallback) {
  return {
    model_id: agent?.model || fallback.model_id,
    reasoning_effort: agent?.reasoning_effort || fallback.reasoning_effort,
    permission_profile: agent?.permission_profile || fallback.permission_profile,
  };
}

export function projectProfileDefaults(projectId, projects, agents, catalog) {
  const project = projects.find((item) => item.id === projectId);
  const defaultAgent = agents.find((item) => item.id === "mdo.default");
  const modelId = project?.default_model_id || defaultAgent?.model ||
    catalog.default_model_id || catalog.models?.[0]?.id || "";
  const model = selectedModel(catalog.models ?? [], modelId);
  return {
    model_id: modelId,
    reasoning_effort: defaultAgent?.reasoning_effort ||
      model?.default_reasoning_effort || "",
    permission_profile: defaultAgent?.permission_profile || "balanced",
  };
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

export function applyAgentProfileDefaults({ agent, fallback, models,
  modelSelect, reasoningSelect, permissionSelect }) {
  const profile = agentProfileDefaults(agent, fallback);
  if (profile.model_id && ![...modelSelect.options].some((option) =>
    option.value === profile.model_id)) {
    modelSelect.append(element("option", { text: profile.model_id,
      attrs: { value: profile.model_id } }));
  }
  modelSelect.value = profile.model_id;
  fillReasoningOptions(reasoningSelect,
    selectedModel(models, profile.model_id), profile.reasoning_effort);
  permissionSelect.value = profile.permission_profile;
}

export function createComposerProfile({ modelSelect, reasoningSelect,
  permissionSelect, navigation, sessionStore, modelsStore, agentsStore,
  projectsStore, draftStore, status, resetButton, isRunActive,
  hasPendingSubmission = () => false, onBusyChange,
  onSelectionChange }) {
  // Blank tasks have no session metadata. Keep manual choices per project, but
  // continue following its configured default until the user picks a model.
  const drafts = new Map();
  const busy = new Set();
  const changing = new Map();
  const resetting = new Set();
  const draftVersions = new Map();
  let runActive = false;
  let locked = false;

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
  function draft(projectId = navigation.get().projectId || "default") {
    if (!drafts.has(projectId)) drafts.set(projectId, {
      model_id: "", reasoning_effort: "", permission_profile: "",
    });
    return drafts.get(projectId);
  }
  function blankOverrides(projectId) {
    const pending = draft(projectId);
    const saved = draftStore?.composerProfile(projectDraftKey(projectId));
    return { model_id: pending.model_id || saved?.model_id || "",
      reasoning_effort: pending.reasoning_effort || saved?.reasoning_effort || "",
      permission_profile: pending.permission_profile ||
        saved?.permission_profile || "" };
  }
  function hasOverride(profile) {
    return Boolean(profile.model_id || profile.reasoning_effort ||
      profile.permission_profile);
  }
  function bumpDraftVersion(projectId) {
    const version = (draftVersions.get(projectId) ?? 0) + 1;
    draftVersions.set(projectId, version);
    return version;
  }
  function defaultAgent() {
    return agentsStore.get().data?.items?.find((item) =>
      item.id === "mdo.default");
  }
  function defaultModelId() {
    const projectId = navigation.get().projectId || "default";
    const project = (projectsStore.get().data?.items ?? []).find((item) =>
      item.id === projectId);
    return project?.default_model_id ||
      defaultAgent()?.model ||
      modelsStore.get().data?.default_model_id || models()[0]?.id || "";
  }
  function agentPermission(session) {
    const agent = agentsStore.get().data?.items?.find((item) =>
      item.id === (session?.agent_id || "mdo.default"));
    return session?.permission_profile || agent?.permission_profile || "balanced";
  }

  function sessionProfile(session) {
    return { model_id: session.model_id,
      reasoning_effort: session.reasoning_effort,
      permission_profile: agentPermission(session) };
  }
  function sameProfile(a, b) {
    return a?.model_id === b?.model_id &&
      a?.reasoning_effort === b?.reasoning_effort &&
      a?.permission_profile === b?.permission_profile;
  }

  function sync() {
    const session = current();
    const projectId = navigation.get().projectId || "default";
    const pending = session ? draft(projectId) : blankOverrides(projectId);
    const key = selectedKey();
    const deferred = session && draftStore?.composerProfile(key);
    const selected = changing.get(key) || deferred;
    const catalog = models();
    const id = selected?.model_id || session?.model_id || pending.model_id ||
      defaultModelId();
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
    const effort = selected?.reasoning_effort || session?.reasoning_effort ||
      pending.reasoning_effort ||
      (!session && defaultAgent()?.reasoning_effort) ||
      model?.default_reasoning_effort || "";
    fillReasoningOptions(reasoningSelect, model, effort);
    permissionSelect.value = selected?.permission_profile ||
      (session ? agentPermission(session) :
        pending.permission_profile || agentPermission(null));
    const disabled = busy.has(key) || locked ||
      (!session && resetting.has(projectId)) ||
      (Boolean(navigation.get().sessionId) && !session) ||
      (session && (session.status !== "active" ||
        (draftStore && !draftStore.isLoaded(key))));
    modelSelect.disabled = disabled || catalog.length === 0;
    reasoningSelect.disabled = disabled || !reasoningSelect.options.length;
    permissionSelect.disabled = disabled;
    if (resetButton) {
      resetButton.hidden = Boolean(navigation.get().sessionId) ||
        !hasOverride(pending);
      resetButton.disabled = disabled || Boolean(draftStore &&
        !draftStore.isLoaded(projectDraftKey(projectId)));
    }
    if (status) {
      const nextRun = session && deferred &&
        !sameProfile(deferred, sessionProfile(session));
      const saveState = draftStore?.profileSaveState(session ? key :
        projectDraftKey(projectId)) ?? "saved";
      status.hidden = !nextRun && saveState === "saved";
      status.dataset.state = saveState;
      status.textContent = saveState === "error"
        ? session ? t("profile.notSaved", {}, "后续配置未保存")
          : t("profile.draftNotSaved", {}, "新任务配置未保存")
        : saveState === "saving"
          ? session ? t("profile.savingNext", {}, "正在保存后续配置…")
            : t("profile.savingDraft", {}, "正在保存新任务配置…")
          : nextRun ? t("profile.nextRun", {}, "下次任务生效") : "";
    }
    onSelectionChange?.();
  }

  async function changed(field) {
    const session = current();
    const projectId = navigation.get().projectId || "default";
    if (navigation.get().sessionId && !session) { sync(); return; }
    if (!session && (locked || resetting.has(projectId))) { sync(); return; }
    const model = selectedModel(models(), modelSelect.value);
    const effort = fillReasoningOptions(reasoningSelect, model,
      reasoningSelect.value);
    const profile = {
      model_id: modelSelect.value,
      reasoning_effort: effort,
      permission_profile: permissionSelect.value,
    };
    if (!session) {
      const version = bumpDraftVersion(projectId);
      const pending = draft(projectId);
      if (field === "model") {
        pending.model_id = profile.model_id;
        pending.reasoning_effort = profile.reasoning_effort;
      } else if (field === "reasoning") {
        pending.reasoning_effort = profile.reasoning_effort;
      } else pending.permission_profile = profile.permission_profile;
      sync();
      if (!draftStore) return;
      const projectKey = projectDraftKey(projectId);
      try {
        if (!await draftStore.ensureLoaded(projectKey))
          throw new Error(t("profile.deferredSaveFailed", {},
            "新任务配置未能保存，请检查草稿状态"));
        if (draftVersions.get(projectId) !== version ||
            resetting.has(projectId)) return;
        draftStore.setComposerProfile(projectKey, blankOverrides(projectId));
        sync();
        if (!await draftStore.flush(projectKey))
          throw new Error(t("profile.deferredSaveFailed", {},
            "新任务配置未能保存，请检查草稿状态"));
      } catch (error) { toast(errorMessage(error), "error"); }
      finally { sync(); }
      return;
    }
    const key = `${session.project_id}/${session.id}`;
    if (busy.has(key) || locked || session.status !== "active" ||
        (draftStore && !draftStore.isLoaded(key))) {
      sync();
      return;
    }
    busy.add(key);
    onBusyChange(true);
    try {
      if (runActive || isRunActive() || hasPendingSubmission(session)) {
        // A queued submission already froze its profile. Do not change the
        // session while that run may start; save the next choice separately.
        if (!draftStore) { sync(); return; }
        draftStore.setComposerProfile(key,
          sameProfile(profile, sessionProfile(session)) ? null : profile);
        sync();
        if (!await draftStore.flush(key))
          throw new Error(t("profile.deferredSaveFailed", {},
            "后续消息配置未能保存，请检查草稿状态"));
        return;
      }
      if (sameProfile(profile, sessionProfile(session))) {
        if (draftStore?.composerProfile(key)) {
          draftStore.setComposerProfile(key, null);
          if (!await draftStore.flush(key))
            throw new Error(t("profile.deferredSaveFailed", {},
              "后续消息配置未能保存，请检查草稿状态"));
        }
        return;
      }
      changing.set(key, profile);
      sync();
      const updated = await updateSessionProfile(session, profile);
      const selected = navigation.get();
      if (selected.projectId === updated.project_id &&
          selected.sessionId === updated.id) sessionStore.setData(updated);
      if (draftStore?.composerProfile(key)) {
        draftStore.setComposerProfile(key, null);
        if (!await draftStore.flush(key))
          throw new Error(t("profile.deferredClearFailed", {},
            "会话配置已更新，但旧的后续消息配置未能清除，请检查草稿状态"));
      }
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
      changing.delete(key);
      busy.delete(key);
      onBusyChange(false);
      sync();
    }
  }

  async function resetBlankProfile() {
    const route = navigation.get();
    const projectId = route.projectId || "default";
    if (route.sessionId || locked || resetting.has(projectId) ||
        !hasOverride(blankOverrides(projectId))) return;
    const projectKey = projectDraftKey(projectId);
    // Keep the old choice visible until the clear is durable. A failed write
    // then leaves a usable retry action, even when the old choice came from disk.
    drafts.set(projectId, blankOverrides(projectId));
    bumpDraftVersion(projectId);
    resetting.add(projectId);
    sync();
    let cleared = false;
    try {
      if (draftStore) {
        if (!await draftStore.ensureLoaded(projectKey))
          throw new Error(t("profile.deferredSaveFailed", {},
            "新任务配置未能保存，请检查草稿状态"));
        draftStore.setComposerProfile(projectKey, null);
        sync();
        if (!await draftStore.flush(projectKey))
          throw new Error(t("profile.deferredSaveFailed", {},
            "新任务配置未能保存，请检查草稿状态"));
      }
      drafts.set(projectId, { model_id: "", reasoning_effort: "",
        permission_profile: "" });
      cleared = true;
    } catch (error) { toast(errorMessage(error), "error"); }
    finally {
      resetting.delete(projectId);
      sync();
      const selected = navigation.get();
      if (cleared && !selected.sessionId &&
          (selected.projectId || "default") === projectId)
        modelSelect.focus({ preventScroll: true });
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
  resetButton?.addEventListener("click", () => { void resetBlankProfile(); });
  const unsubscribers = [modelsStore.subscribe(sync), agentsStore.subscribe(sync),
    projectsStore.subscribe(sync), sessionStore.subscribe(sync),
    navigation.subscribe(sync), subscribeLocale(sync)];
  return Object.freeze({
    selection: () => ({
      model_id: modelSelect.value,
      reasoning_effort: reasoningSelect.value,
      permission_profile: permissionSelect.value,
    }),
    setRunActive(value, blockChanges = false) {
      runActive = Boolean(value);
      locked = Boolean(blockChanges);
      sync();
    },
    isBusy: () => busy.has(selectedKey()),
    hasInFlight: () => busy.size !== 0,
    sync,
    destroy: () => unsubscribers.forEach((unsubscribe) => unsubscribe()),
  });
}
