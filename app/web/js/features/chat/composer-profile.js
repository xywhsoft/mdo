import { updateSessionProfile } from "../../state/sessions.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";

const EFFORT_LABEL = Object.freeze({
  none: "无思考", minimal: "极低", low: "低", medium: "中",
  high: "高", xhigh: "极高", max: "最大",
});

function selectedModel(models, id) {
  return models.find((model) => model.id === id) ?? null;
}

export function fillReasoningOptions(select, model, preferred = "") {
  const efforts = model?.reasoning_efforts?.length
    ? model.reasoning_efforts : (preferred ? [preferred] : []);
  clear(select);
  for (const effort of efforts)
    select.append(element("option", { text: EFFORT_LABEL[effort] || effort,
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
  isRunActive, onBusyChange }) {
  const draft = { model_id: "", reasoning_effort: "", permission_profile: "balanced" };
  let busy = false;
  let runActive = false;

  function models() { return modelsStore.get().data?.models ?? []; }
  function current() { return sessionStore.get().data; }
  function agentPermission(session) {
    const agent = agentsStore.get().data?.items?.find((item) =>
      item.id === (session?.agent_id || "mdo.default"));
    return session?.permission_profile || agent?.permission_profile || "balanced";
  }

  function sync() {
    const session = current();
    const catalog = models();
    const id = session?.model_id || draft.model_id || catalog[0]?.id || "";
    clear(modelSelect);
    for (const model of catalog) {
      const suffix = model.free ? " · 免费" : "";
      modelSelect.append(element("option", {
        text: `${model.name || model.id}${suffix}`,
        attrs: { value: model.id },
      }));
    }
    if (id && !selectedModel(catalog, id))
      modelSelect.append(element("option", { text: id, attrs: { value: id } }));
    modelSelect.value = id;
    const model = selectedModel(catalog, id);
    const effort = session?.reasoning_effort || draft.reasoning_effort ||
      model?.default_reasoning_effort || "";
    fillReasoningOptions(reasoningSelect, model, effort);
    permissionSelect.value = session ? agentPermission(session) : draft.permission_profile;
    if (!session) {
      draft.model_id = modelSelect.value;
      draft.reasoning_effort = reasoningSelect.value;
      draft.permission_profile = permissionSelect.value;
    }
    const disabled = busy || runActive || (session && session.status !== "active");
    modelSelect.disabled = disabled || catalog.length === 0;
    reasoningSelect.disabled = disabled || !reasoningSelect.options.length;
    permissionSelect.disabled = disabled;
  }

  async function changed() {
    const session = current();
    const model = selectedModel(models(), modelSelect.value);
    const effort = fillReasoningOptions(reasoningSelect, model,
      reasoningSelect.value);
    const profile = {
      model_id: modelSelect.value,
      reasoning_effort: effort,
      permission_profile: permissionSelect.value,
    };
    if (!session) { Object.assign(draft, profile); sync(); return; }
    if (busy || isRunActive() || session.status !== "active") {
      sync();
      return;
    }
    busy = true;
    onBusyChange(true);
    sync();
    try {
      const updated = await updateSessionProfile(session, profile);
      const selected = navigation.get();
      if (selected.projectId === updated.project_id &&
          selected.sessionId === updated.id) sessionStore.setData(updated);
      toast("会话配置已更新");
    } catch (error) {
      toast(errorMessage(error), "error");
      if (navigation.get().sessionId === session.id) sync();
    } finally {
      busy = false;
      onBusyChange(false);
      sync();
    }
  }

  modelSelect.addEventListener("change", () => {
    const model = selectedModel(models(), modelSelect.value);
    fillReasoningOptions(reasoningSelect, model,
      model?.default_reasoning_effort || "");
    void changed();
  });
  reasoningSelect.addEventListener("change", () => { void changed(); });
  permissionSelect.addEventListener("change", () => { void changed(); });
  modelsStore.subscribe(sync);
  agentsStore.subscribe(sync);
  sessionStore.subscribe(sync);
  navigation.subscribe(sync);
  return Object.freeze({
    selection: () => ({
      model_id: modelSelect.value,
      reasoning_effort: reasoningSelect.value,
      permission_profile: permissionSelect.value,
    }),
    setRunActive(value) { runActive = Boolean(value); sync(); },
    isBusy: () => busy,
    sync,
  });
}
