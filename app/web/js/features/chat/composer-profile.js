import { uiMessage, setUiText as writeUiText, t as uiText, subscribeLocale, t } from "../../i18n.js";

import { updateSessionProfile, updateSessionPermission } from "../../state/sessions.js";
import { projectDraftKey } from "./draft-store.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";

import { findModel as selectedModel, modelLoginBlock } from "../../utils/models.js";

const EFFORT_LABEL = Object.freeze({
  none: ["reasoning.none", uiText("reasoning.none", {}, "无思考")],
  minimal: ["reasoning.minimal", uiText("settings.reasoningMinimal", {}, "极低")],
  low: ["reasoning.low", uiText("settings.reasoningLow", {}, "低")],
  medium: ["reasoning.medium", uiText("settings.reasoningMedium", {}, "中")],
  high: ["reasoning.high", uiText("settings.reasoningHigh", {}, "高")],
  xhigh: ["reasoning.xhigh", uiText("settings.reasoningXhigh", {}, "极高")],
  max: ["reasoning.max", uiText("settings.reasoningMax", {}, "最大")],
});

function reasoningChoice(model, preferred = "") {
  const efforts = Array.isArray(model?.reasoning_efforts) ? model.reasoning_efforts : (preferred ? [preferred] : []);
  return efforts.includes(preferred) ? preferred : efforts.includes(model?.default_reasoning_effort)
    ? model.default_reasoning_effort : efforts[0] || "";
}
export function fillReasoningOptions(select, model, preferred = "") {
  const efforts = Array.isArray(model?.reasoning_efforts)
    ? model.reasoning_efforts : (preferred ? [preferred] : []);
  clear(select);
  for (const effort of efforts)
    select.append(element("option", { text: EFFORT_LABEL[effort]
      ? uiMessage(EFFORT_LABEL[effort][0], {}, EFFORT_LABEL[effort][1]) : effort,
      attrs: { value: effort } }));
  const chosen = reasoningChoice(model, preferred);
  select.value = chosen;
  select.disabled = efforts.length === 0;
  return chosen;
}

export function createComposerProfile({ modelSelect, reasoningSelect,
  permissionSelect, navigation, sessionStore, modelsStore, agentsStore,
  projectsStore, settingsStore, draftStore, status, resetButton, isRunActive,
  hasPendingSubmission = () => false, onBusyChange,
  onSelectionChange, onProfileUpdated, updateProfile = updateSessionProfile,
  updatePermission = updateSessionPermission }) {
  // Blank tasks have no session metadata. Keep manual choices per project, but
  // continue following its configured default until the user picks a model.
  const drafts = new Map();
  const busy = new Set();
  const changing = new Map();
  const saving = new Map();
  const resetting = new Set();
  const draftVersions = new Map();
  let runActive = false;
  let locked = false;
  let underlyingProfile = null;

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
    // Existing sessions keep their recorded permission. Blank tasks inherit
    // the global default unless the user has saved a chat-level override.
    return session?.permission_profile || (!session &&
      settingsStore?.get().data?.agent?.permission_profile) || agent?.permission_profile || "balanced";
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
    const saved = session && draftStore?.composerProfile(key);
    const deferred = saved && { ...saved, permission_profile: agentPermission(session) };
    const selected = changing.get(key) || deferred;
    const catalog = models();
    const rootRequested = selected?.model_id || session?.model_id || pending.model_id ||
      defaultModelId();
    const rootModel = selectedModel(catalog, rootRequested);
    const rootEffort = selected?.reasoning_effort || session?.reasoning_effort || pending.reasoning_effort ||
      (!session && defaultAgent()?.reasoning_effort) || rootModel?.default_reasoning_effort || "";
    const execution = session?.execution, member = execution?.available !== false && execution?.target;
    underlyingProfile = { model_id: rootModel?.id || rootRequested, reasoning_effort: reasoningChoice(rootModel, rootEffort) };
    const requested = member ? execution.model_id : rootRequested;
    const id = selectedModel(catalog, requested)?.id || requested;
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
    const effort = member ? execution.reasoning_effort || model?.default_reasoning_effort || "" : rootEffort;
    fillReasoningOptions(reasoningSelect, model, effort);
    permissionSelect.value = selected?.permission_profile ||
      (session ? agentPermission(session) :
        pending.permission_profile || agentPermission(null));
    const disabled = busy.has(key) || locked ||
      (!session && resetting.has(projectId)) ||
      (Boolean(navigation.get().sessionId) && !session) ||
      (session && (session.status !== "active" ||
        (draftStore && !draftStore.isLoaded(key))));
    modelSelect.disabled = disabled || Boolean(member) || execution?.available === false || catalog.length === 0;
    reasoningSelect.disabled = disabled || Boolean(member) || execution?.available === false || !reasoningSelect.options.length;
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
      status.hidden = !member && execution?.available !== false && !nextRun && saveState === "saved";
      status.dataset.state = saveState;
      writeUiText(status, execution?.available === false ? execution.message || uiMessage("profile.targetUnavailable", {}, "当前主讲不可用")
        : member ? uiMessage("profile.memberBinding", { member: member.id }, `主讲 ${member.id} 的模型绑定固定；返回协调者后可修改协调者模型。`)
        : saveState === "error"
        ? session ? uiMessage("profile.notSaved", {}, "后续配置未保存")
          : uiMessage("profile.draftNotSaved", {}, "新任务配置未保存")
        : saveState === "saving"
          ? session ? uiMessage("profile.savingNext", {}, "正在保存后续配置…")
            : uiMessage("profile.savingDraft", {}, "正在保存新任务配置…")
          : nextRun ? uiMessage("profile.nextRun", {}, "下次任务生效") : "");
    }
    onSelectionChange?.();
  }

  async function changed(field) {
    const session = current();
    const execution = session?.execution;
    if (field !== "permission" && (execution?.target || execution?.available === false)) { sync(); return; }
    const projectId = navigation.get().projectId || "default";
    if (navigation.get().sessionId && !session) { sync(); return; }
    if (!session && (locked || resetting.has(projectId))) { sync(); return; }
    const model = selectedModel(models(), modelSelect.value);
    const effort = fillReasoningOptions(reasoningSelect, model,
      reasoningSelect.value);
    const profile = {
      model_id: execution?.target ? underlyingProfile.model_id : modelSelect.value,
      reasoning_effort: execution?.target ? underlyingProfile.reasoning_effort : effort,
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
    let stopSaving = () => {};
    try {
      if (field !== "permission" && (runActive || isRunActive() || hasPendingSubmission(session))) {
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
      if (field === "permission" ? profile.permission_profile === session.permission_profile :
          sameProfile(profile, sessionProfile(session))) {
        if (field !== "permission" && draftStore?.composerProfile(key)) {
          draftStore.setComposerProfile(key, null);
          if (!await draftStore.flush(key))
            throw new Error(t("profile.deferredSaveFailed", {},
              "后续消息配置未能保存，请检查草稿状态"));
        }
        return;
      }
      changing.set(key, profile);
      sync();
      const controller = new AbortController();
      const originalRoute = JSON.stringify(navigation.get());
      saving.set(key, controller);
      const unsubscribe = navigation.subscribe?.(() => {
        if (JSON.stringify(navigation.get()) !== originalRoute) controller.abort();
      });
      stopSaving = () => {
        unsubscribe?.();
        if (saving.get(key) === controller) saving.delete(key);
      };
      const updated = await (field === "permission" ? updatePermission : updateProfile)(
        session, profile, { signal: controller.signal });
      if (controller.signal.aborted) return;
      const selected = navigation.get();
      const latest = current();
      // A newer metadata event can win while the confirmed result is returning.
      // Never roll its model/permission back or announce that older choice.
      if (latest?.revision > updated.revision) return;
      if (selected.projectId === updated.project_id &&
          selected.sessionId === updated.id) {
        sessionStore.setData(updated);
        if (field !== "permission") onProfileUpdated?.(updated);
      }
      if (draftStore?.composerProfile(key)) {
        const deferred = draftStore.composerProfile(key);
        const next = field === "permission" ? { ...deferred,
          permission_profile: updated.permission_profile } : null;
        draftStore.setComposerProfile(key, next && !sameProfile(next, sessionProfile(updated)) ? next : null);
        if (!await draftStore.flush(key))
          throw new Error(t("profile.deferredClearFailed", {},
            "会话配置已更新，但旧的后续消息配置未能清除，请检查草稿状态"));
      }
      toast(selected.projectId === updated.project_id &&
        selected.sessionId === updated.id ? t("profile.updated", {}, "会话配置已更新") :
        t("profile.backgroundUpdated", { title: session.title },
          `后台会话“${session.title}”配置已更新`));
    } catch (error) {
      if (error?.name === "AbortError") return;
      const selected = navigation.get();
      if (error.currentSession?.project_id === selected.projectId &&
          error.currentSession.id === selected.sessionId &&
          error.currentSession.revision >= (current()?.revision ?? 0))
        sessionStore.setData(error.currentSession);
      toast(selected.projectId === session.project_id &&
        selected.sessionId === session.id ? errorMessage(error) :
        t("profile.backgroundFailed", { title: session.title, error: errorMessage(error) },
          `后台会话“${session.title}”配置更新失败：${errorMessage(error)}`), "error");
      if (selected.projectId === session.project_id &&
          selected.sessionId === session.id) sync();
    } finally {
      stopSaving();
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
  if (settingsStore) unsubscribers.push(settingsStore.subscribe(sync));
  return Object.freeze({
    selection: () => ({
      // Queue requests still carry the coordinator's saved profile. Displaying
      // a member must never overwrite the coordinator's persistent selection.
      model_id: current()?.execution?.target ? underlyingProfile.model_id : modelSelect.value,
      reasoning_effort: current()?.execution?.target ? underlyingProfile.reasoning_effort : reasoningSelect.value,
      permission_profile: permissionSelect.value,
    }),
    setRunActive(value, blockChanges = false) {
      runActive = Boolean(value);
      locked = Boolean(blockChanges);
      sync();
    },
    isBusy: () => busy.has(selectedKey()),
    loginBlock: () => modelLoginBlock(modelsStore.get().data, modelSelect.value),
    hasInFlight: () => busy.size !== 0,
    sync,
    destroy() {
      for (const controller of saving.values()) controller.abort();
      unsubscribers.forEach((unsubscribe) => unsubscribe());
    },
  });
}
