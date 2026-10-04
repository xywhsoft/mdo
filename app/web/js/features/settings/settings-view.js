import { applySettings, loadSettings, restoreSettings } from "../../state/settings.js";
import { errorMessage, toast } from "../../utils/dom.js";
import { currentLocale, loadLocale, supportedLocales, t } from "../../i18n.js";
import { createSettingsAutosave, changedSettingsValues } from "./settings-autosave.js";
import { createSettingsPages } from "./settings-pages.js";

function number(form, name) {
  return Number(form.elements[name].value);
}

function settingsPatch(form) {
  const proxySecretRef = form.elements.proxy_secret_ref.value.trim();
  return {
    locale: form.elements.locale.value,
    appearance: {
      theme: form.elements.theme.value,
      font_size: form.elements.font_size.value,
      density: form.elements.density.value,
    },
    composer: { submit_mode: form.elements.submit_mode.value },
    notifications: { sound: form.elements.completion_sound.checked },
    power: { prevent_sleep: form.elements.prevent_sleep.checked },
    agent: {
      permission_profile: form.elements.permission_profile.value,
      reasoning_effort: form.elements.reasoning_effort.value,
      user_instructions: form.elements.user_instructions.value,
      web_search: form.elements.web_search.checked,
      memory: form.elements.memory.checked,
      schedules: form.elements.schedules.checked,
      max_parallel_tools: number(form, "max_parallel_tools"),
      max_parallel_subagents: number(form, "max_parallel_subagents"),
    },
    web: { search: { endpoint: form.elements.endpoint.value.trim() } },
    transport: {
      ca_pem_path: form.elements.ca_pem_path.value.trim(),
      proxy: {
        kind: form.elements.proxy_kind.value,
        host: form.elements.proxy_host.value.trim(),
        port: number(form, "proxy_port"),
        user: form.elements.proxy_user.value.trim(),
        bypass: form.elements.proxy_bypass.value.trim(),
        ...(form.elements.proxy_clear_secret.checked ? { credential: null }
          : proxySecretRef ? { credential: { secret_ref: proxySecretRef } } : {}),
      },
    },
    workspace: {
      open_mode: form.elements.open_mode.value,
      confirm_external_write: form.elements.confirm_external_write.checked,
    },
  };
}

export function applyAppearance(settings) {
  if (!settings?.appearance) return;
  const root = document.documentElement;
  const theme = settings.appearance.theme;
  if (theme === "system") root.removeAttribute("data-theme");
  else root.dataset.theme = theme;
  root.dataset.fontSize = settings.appearance.font_size;
  root.dataset.density = settings.appearance.density;
}

export function createSettingsView({ form, store, navigation, onApplied }) {
  const revision = document.querySelector("#settings-revision");
  const feedback = document.querySelector("#settings-feedback");
  const actions = document.querySelector("#settings-actions");
  const applyButton = document.querySelector("#apply-settings");
  const discardButton = document.querySelector("#discard-settings");
  const restoreButton = document.querySelector("#restore-settings");
  const restoreConfirm = document.querySelector("#restore-confirm");
  const cancelRestoreButton = document.querySelector("#cancel-restore");
  const confirmRestoreButton = document.querySelector("#confirm-restore");
  const pages = createSettingsPages({
    workspace: document.querySelector("#settings-workspace"), form, navigation,
  });
  const proxyCredential = document.querySelector("#proxy-credential-state");
  const powerStatus = document.querySelector("#settings-power-status");
  const instructionsCount = document.querySelector("#settings-instructions-count");
  const pendingLink = document.querySelector("#settings-pending-link");
  let snapshot = null;
  let baselineFingerprint = "";
  let baselineValues = {};
  let submittedValues = null;
  let saveFailure = "";
  let busy = false;
  let selectedSection = "general";
  let lastEditedSection = "general";
  let localeReady = Promise.resolve();
  let previewActive = false;

  function values() {
    return Object.fromEntries([...form.elements].filter((field) => field.name)
      .map((field) => [field.name,
        field.type === "checkbox" ? field.checked : field.value]));
  }

  function fingerprint() {
    return snapshot ? JSON.stringify(settingsPatch(form)) : "";
  }

  function syncPendingLink() {
    pendingLink.hidden = !snapshot || fingerprint() === baselineFingerprint ||
      pages.usesPreferences(selectedSection);
  }

  function previewAppearance() {
    if (!snapshot) return;
    applyAppearance({ appearance: {
      theme: form.elements.theme.value,
      font_size: form.elements.font_size.value,
      density: form.elements.density.value,
    } });
  }

  function syncShortActions() {
    // Short mobile viewports can release the footer when it has no action.
    const idle = !busy && Boolean(snapshot) &&
      fingerprint() === baselineFingerprint &&
      feedback.dataset.tone !== "error";
    actions.dataset.shortState = !idle ? "active"
      : restoreButton.disabled ? "empty" : "restore";
  }

  function feedbackText(text, tone = "neutral") {
    feedback.textContent = text;
    feedback.dataset.tone = tone;
    syncShortActions();
  }

  function setBusy(value) {
    busy = value;
    const dirty = Boolean(snapshot) && fingerprint() !== baselineFingerprint;
    applyButton.disabled = value || !dirty;
    discardButton.disabled = value || !dirty;
    restoreButton.disabled = value || !snapshot?.user_patches?.settings;
    syncShortActions();
  }

  function validatePower() {
    const runtime = snapshot?.power_runtime;
    return !form.elements.prevent_sleep.checked || !runtime?.checked ||
      runtime.available;
  }

  function renderPowerStatus(settings) {
    const runtime = settings.power_runtime;
    powerStatus.textContent = !runtime?.checked
      ? t("settings.preventSleepChecking")
      : !runtime.available
        ? t("settings.preventSleepUnavailable")
        : t("settings.preventSleepReady");
  }

  function focusAfterAction(preferred = feedback) {
    if (document.activeElement !== document.body &&
        document.activeElement !== document.documentElement) return;
    const target = preferred?.isConnected && !preferred.disabled &&
      preferred.getClientRects().length ? preferred : visibleFeedbackTarget();
    if (target?.isConnected && target.getClientRects().length)
      target.focus({ preventScroll: true });
  }

  function visibleFeedbackTarget() {
    return feedback.getClientRects().length ? feedback :
      pages.navigationTarget();
  }

  function renderStatus(settings) {
    revision.textContent = t("settings.autosaveDescription", {},
      "有效更改会自动保存；保存失败时可点击保存重试。");
    feedbackText(settings.transaction_service.runtime_consistent
      ? t("settings.synced", {}, "配置与本地服务保持同步。")
      : t("settings.runtimeError", { error: settings.transaction_service.last_error },
        `运行时配置需要处理：${settings.transaction_service.last_error}`),
    settings.transaction_service.runtime_consistent ? "neutral" : "error");
  }

  function renderProxyCredential(settings) {
    const configured = Boolean(settings.transport?.proxy?.credential_configured);
    document.querySelector("#proxy-clear-secret-row").hidden = !configured;
    proxyCredential.textContent = configured
      ? t("settings.proxyCredentialConfigured", {},
        "代理密码引用已配置；输入新引用可替换，或勾选清除。")
      : t("settings.proxyCredentialMissing", {}, "未配置代理密码引用。");
  }

  function validateInstructions() {
    const field = form.elements.user_instructions;
    const bytes = new TextEncoder().encode(field.value).length;
    field.setCustomValidity(bytes > 8192
      ? t("settings.instructionsTooLong", {}, "自定义指令不能超过 8192 字节。") : "");
    instructionsCount.textContent = t("settings.instructionsBytes",
      { bytes }, `${bytes} / 8192 字节`);
    instructionsCount.dataset.tone = bytes > 8192 ? "error" : "neutral";
    return bytes <= 8192;
  }

  function validateProxy() {
    const enabled = form.elements.proxy_kind.value !== "none";
    form.elements.proxy_host.required = enabled;
    form.elements.proxy_port.min = enabled ? "1" : "0";
    const user = form.elements.proxy_user;
    const hasCredential = Boolean(snapshot?.transport?.proxy?.credential_configured) ||
      Boolean(form.elements.proxy_secret_ref.value.trim());
    user.setCustomValidity(!form.elements.proxy_clear_secret.checked &&
      hasCredential &&
      !user.value.trim() ? t("settings.proxyUserRequired", {},
        "配置密码引用时须填写代理用户名。") : "");
    return form.elements.proxy_host.validity.valid &&
      form.elements.proxy_port.validity.valid && user.validity.valid;
  }

  function fill(settings, preserve = true) {
    const field = document.activeElement;
    const editing = form.contains(field) ? { field, value: field.value,
      start: field.selectionStart, end: field.selectionEnd,
      direction: field.selectionDirection, top: field.scrollTop, left: field.scrollLeft } : null;
    const retained = preserve && snapshot
      ? changedSettingsValues(submittedValues ?? baselineValues, values()) : {};
    snapshot = settings;
    form.elements.locale.value = supportedLocales.includes(settings.locale)
      ? settings.locale : "zh-CN";
    form.elements.theme.value = settings.appearance.theme;
    form.elements.font_size.value = settings.appearance.font_size;
    form.elements.density.value = settings.appearance.density;
    form.elements.submit_mode.value = settings.composer.submit_mode;
    form.elements.prevent_sleep.checked = settings.power?.prevent_sleep ?? false;
    form.elements.prevent_sleep.disabled = Boolean(settings.power_runtime?.checked &&
      !settings.power_runtime.available && !form.elements.prevent_sleep.checked);
    renderPowerStatus(settings);
    form.elements.completion_sound.checked = Boolean(settings.notifications?.sound);
    form.elements.open_mode.value = settings.workspace.open_mode;
    form.elements.confirm_external_write.checked = settings.workspace.confirm_external_write;
    form.elements.permission_profile.value = settings.agent.permission_profile ?? "balanced";
    form.elements.reasoning_effort.value = settings.agent.reasoning_effort;
    form.elements.user_instructions.value = settings.agent.user_instructions ?? "";
    validateInstructions();
    form.elements.max_parallel_tools.value = settings.agent.max_parallel_tools;
    form.elements.max_parallel_subagents.value = settings.agent.max_parallel_subagents;
    form.elements.web_search.checked = settings.agent.web_search;
    form.elements.memory.checked = settings.agent.memory;
    form.elements.schedules.checked = settings.agent.schedules;
    form.elements.endpoint.value = settings.web.endpoint;
    form.elements.ca_pem_path.value = settings.transport?.ca_pem_path ?? "";
    const proxy = settings.transport?.proxy ?? {};
    form.elements.proxy_kind.value = proxy.kind ?? "none";
    form.elements.proxy_host.value = proxy.host ?? "";
    form.elements.proxy_port.value = proxy.port ?? 0;
    form.elements.proxy_user.value = proxy.user ?? "";
    form.elements.proxy_secret_ref.value = "";
    form.elements.proxy_clear_secret.checked = false;
    form.elements.proxy_bypass.value = proxy.bypass ?? "";
    validateProxy();
    renderProxyCredential(settings);
    baselineFingerprint = fingerprint();
    baselineValues = values();
    for (const [name, value] of Object.entries(retained)) {
      const field = form.elements[name];
      if (field.type === "checkbox") field.checked = value;
      else field.value = value;
    }
    // Assigning the accepted value and then a newer edit moves a text cursor.
    // Keep the active editor's selection and scroll when its text is retained.
    if (editing && document.activeElement === editing.field &&
        editing.field.value === editing.value) {
      if (typeof editing.start === "number")
        editing.field.setSelectionRange(editing.start, editing.end, editing.direction);
      editing.field.scrollTop = editing.top;
      editing.field.scrollLeft = editing.left;
    }
    syncPendingLink();
    restoreConfirm.hidden = true;
    validateInstructions();
    validateProxy();
    if (fingerprint() === baselineFingerprint && !busy && !saveFailure) renderStatus(settings);
    else markDirty();
    if (previewActive) previewAppearance();
    else applyAppearance(settings);
    setBusy(busy);
    const selectedLocale = previewActive ? form.elements.locale.value : settings.locale;
    localeReady = loadLocale(selectedLocale).then((applied) => {
      if (applied && snapshot === settings) {
        renderProxyCredential(settings);
        renderPowerStatus(settings);
        validateInstructions();
        if (fingerprint() === baselineFingerprint && !busy && !saveFailure) renderStatus(settings);
        else markDirty();
      }
    }).catch((error) => toast(errorMessage(error), "error"));
  }

  function markDirty(event) {
    if (event) saveFailure = "";
    const section = event?.target?.closest?.("[data-settings-panel]")?.dataset.settingsPanel;
    if (pages.usesPreferences(section)) lastEditedSection = section;
    if (previewActive) previewAppearance();
    const validInstructions = validateInstructions();
    const validProxy = validateProxy();
    const validPower = validatePower();
    const dirty = Boolean(snapshot) && fingerprint() !== baselineFingerprint;
    syncPendingLink();
    applyButton.disabled = busy || !dirty;
    discardButton.disabled = busy || !dirty;
    feedbackText(!validInstructions
      ? t("settings.instructionsTooLong", {}, "自定义指令不能超过 8192 字节。")
      : !validProxy
        ? t("settings.proxyInvalid", {}, "代理地址、端口或用户名需要检查。")
      : !validPower
        ? t("settings.preventSleepUnavailable")
      : busy
        ? t("settings.saving", {}, "正在保存…")
      : saveFailure
        ? saveFailure
      : dirty
        ? t("settings.pending", {}, "有未保存的更改；有效输入会自动保存。")
        : t("settings.synced", {}, "配置与本地服务保持同步。"),
    validInstructions && validProxy && validPower && !saveFailure ? "neutral" : "error");
    if (event) autosave.schedule();
  }
  form.addEventListener("input", markDirty);
  form.addEventListener("change", markDirty);
  function reportSettingsValidity() {
    const invalid = [...form.elements].find((field) =>
      field.willValidate && !field.validity.valid);
    if (!invalid) return true;
    // Native form validation cannot focus a field in a hidden settings panel.
    // Reveal its section before reporting that field, keeping every edit.
    const section = invalid.closest("[data-settings-panel]")?.dataset.settingsPanel;
    if (section && section !== selectedSection) navigation.openSettings(section);
    feedbackText(t("settings.invalidInput"), "error");
    invalid.focus();
    pages.keepFocusedFieldVisible();
    invalid.reportValidity();
    return false;
  }
  pendingLink.addEventListener("click", () => {
    navigation.openSettings(lastEditedSection);
    pages.focusNavigation();
  });
  form.elements.locale.addEventListener("change", async () => {
    try {
      const applied = await loadLocale(form.elements.locale.value);
      if (!applied) return;
      if (snapshot) renderStatus(snapshot);
      if (snapshot) renderProxyCredential(snapshot);
      if (snapshot) renderPowerStatus(snapshot);
      validateInstructions();
      markDirty();
    }
    catch (error) {
      form.elements.locale.value = currentLocale();
      markDirty();
      toast(errorMessage(error), "error");
    }
  });

  const autosave = createSettingsAutosave({
    capture() {
      if (!snapshot || busy || !restoreConfirm.hidden ||
          fingerprint() === baselineFingerprint || !validateInstructions() ||
          !validateProxy() || !validatePower() ||
          [...form.elements].some((field) => field.willValidate && !field.validity.valid))
        return null;
      return { patch: settingsPatch(form), etag: snapshot.etag, values: values() };
    },
    write: (submitted) => applySettings(submitted.patch, submitted.etag),
    onStart(submitted) {
      saveFailure = "";
      submittedValues = submitted.values;
      setBusy(true);
      feedbackText(t("settings.saving", {}, "正在保存…"));
    },
    onSettled(error) {
      saveFailure = "";
      if (error?.status === 412) saveFailure = t("settings.saveConflict", {},
        "设置已在其他窗口更新。你的修改仍保留，可点击保存重试。");
      else if (error) saveFailure = t("settings.saveFailed", { error: errorMessage(error) },
        `保存失败：${errorMessage(error)}。更改已保留，可点击保存重试。`);
      submittedValues = null;
      setBusy(false);
      syncPendingLink();
      if (error) feedbackText(saveFailure, "error");
      else if (fingerprint() !== baselineFingerprint) markDirty();
      else feedbackText(t("settings.saved", {}, "已保存"), "success");
      if (!error) void Promise.resolve(onApplied?.()).catch(() => {});
      if (error?.status === 412) {
        // A second window changed settings. Read its values once, rebase the
        // unsaved fields, and leave the retry to the user with a fresh ETag.
        setBusy(true);
        void loadSettings().then((loaded) => {
          setBusy(false);
          syncPendingLink();
          if (loaded.status === "ready" && fingerprint() === baselineFingerprint) {
            saveFailure = "";
            feedbackText(t("settings.saved", {}, "已保存"), "success");
          }
          else if (saveFailure) feedbackText(saveFailure, "error");
          else { markDirty(); autosave.schedule(); }
        });
      }
    },
  });
  form.addEventListener("compositionstart", () => autosave.composing(true));
  form.addEventListener("compositionend", () => autosave.composing(false));
  function saveNow() {
    if (!snapshot || busy || !reportSettingsValidity() || !validatePower()) return;
    void autosave.flush().then(() => focusAfterAction());
  }
  applyButton.addEventListener("click", saveNow);
  form.noValidate = true;
  form.addEventListener("submit", (event) => {
    event.preventDefault();
    saveNow();
  });

  discardButton.addEventListener("click", () => {
    autosave.cancel();
    saveFailure = "";
    if (snapshot) fill(snapshot, false);
    // fill() disables the clicked button; move focus before the browser blurs it.
    visibleFeedbackTarget()?.focus({ preventScroll: true });
  });
  function closeRestoreConfirm() {
    restoreConfirm.hidden = true;
    autosave.schedule();
    (restoreButton.disabled ? visibleFeedbackTarget() : restoreButton)
      ?.focus({ preventScroll: true });
  }
  restoreButton.addEventListener("click", () => {
    autosave.cancel();
    restoreConfirm.hidden = false;
    cancelRestoreButton.focus({ preventScroll: true });
  });
  cancelRestoreButton.addEventListener("click", closeRestoreConfirm);
  restoreConfirm.addEventListener("keydown", (event) => {
    if (event.key !== "Escape" || busy) return;
    event.preventDefault();
    closeRestoreConfirm();
  });
  confirmRestoreButton.addEventListener("click", async () => {
    if (!snapshot || busy) return;
    cancelRestoreButton.disabled = true;
    confirmRestoreButton.disabled = true;
    submittedValues = values();
    saveFailure = "";
    setBusy(true);
    let restored = false;
    try {
      await restoreSettings(snapshot.etag);
      restored = true;
      restoreConfirm.hidden = true;
      feedbackText(t("settings.restored", {}, "已恢复默认设置。"), "success");
      toast(t("settings.restoreToast", {}, "已恢复默认设置"));
      onApplied?.();
    } catch (error) {
      feedbackText(errorMessage(error), "error");
    } finally {
      busy = false;
      submittedValues = null;
      setBusy(false);
      cancelRestoreButton.disabled = false;
      confirmRestoreButton.disabled = false;
      focusAfterAction(
        restoreConfirm.hidden ? feedback : confirmRestoreButton);
      if (restored) autosave.schedule();
    }
  });

  const unsubscribe = store.subscribe((state) => {
    if (state.status === "loading" && !state.data) {
      revision.textContent = t("settings.loading", {}, "正在读取当前配置…");
      return;
    }
    if (state.status === "error") {
      feedbackText(errorMessage(state.error), "error");
      return;
    }
    if (state.status === "ready" && state.data) fill(state.data);
  });

  return Object.freeze({
    localeReady() { return localeReady; },
    hasPendingChanges() {
      return busy || (Boolean(snapshot) && fingerprint() !== baselineFingerprint);
    },
    setActive(value) {
      const next = Boolean(value);
      if (previewActive === next) return;
      previewActive = next;
      if (!snapshot) return;
      if (!next) {
        applyAppearance(snapshot);
        localeReady = loadLocale(snapshot.locale)
          .catch((error) => toast(errorMessage(error), "error"));
        return;
      }
      previewAppearance();
      const selectedLocale = form.elements.locale.value;
      localeReady = loadLocale(selectedLocale).then((applied) => {
        if (!applied || !previewActive || form.elements.locale.value !== selectedLocale) return;
        renderProxyCredential(snapshot);
        renderPowerStatus(snapshot);
        validateInstructions();
        if (fingerprint() === baselineFingerprint && !busy && !saveFailure) renderStatus(snapshot);
        else markDirty();
      }).catch((error) => toast(errorMessage(error), "error"));
    },
    selectSection(section) {
      selectedSection = pages.selectSection(section);
      syncPendingLink();
      return selectedSection;
    },
    destroy() {
      autosave.destroy();
      unsubscribe();
      pages.destroy();
    },
  });
}
