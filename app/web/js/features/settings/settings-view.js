import { previewSettings, applySettings, restoreSettings } from "../../state/settings.js";
import { errorMessage, toast } from "../../utils/dom.js";
import { currentLocale, loadLocale, supportedLocales, t } from "../../i18n.js";

function number(form, name) {
  return Number(form.elements[name].value);
}

function settingsPatch(form, snapshot) {
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
      interaction_mode: form.elements.interaction_mode.value,
      reasoning_effort: form.elements.reasoning_effort.value,
      user_instructions: form.elements.user_instructions.value,
      web_search: form.elements.web_search.checked,
      memory: form.elements.memory.checked,
      schedules: form.elements.schedules.checked,
      max_parallel_tools: number(form, "max_parallel_tools"),
      max_parallel_subagents: number(form, "max_parallel_subagents"),
    },
    web: {
      enabled: form.elements.web_enabled.checked,
      allow_http: form.elements.allow_http.checked,
      allow_private_networks: form.elements.allow_private_networks.checked,
      timeout_ms: number(form, "timeout_ms"),
      idle_timeout_ms: number(form, "idle_timeout_ms"),
      max_response_bytes: number(form, "max_response_bytes"),
      max_text_bytes: number(form, "max_text_bytes"),
      max_documents: number(form, "max_documents"),
      search: {
        provider: snapshot.web.provider,
        endpoint: form.elements.endpoint.value.trim(),
        max_results: number(form, "max_results"),
      },
    },
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
  const previewButton = document.querySelector("#preview-settings");
  const applyButton = document.querySelector("#apply-settings");
  const discardButton = document.querySelector("#discard-settings");
  const restoreButton = document.querySelector("#restore-settings");
  const restoreConfirm = document.querySelector("#restore-confirm");
  const cancelRestoreButton = document.querySelector("#cancel-restore");
  const confirmRestoreButton = document.querySelector("#confirm-restore");
  const sectionNavigation = document.querySelector(".settings-navigation");
  const sectionLayout = document.querySelector(".settings-layout");
  const sectionContent = document.querySelector(".settings-content");
  const credential = document.querySelector("#search-credential-state");
  const proxyCredential = document.querySelector("#proxy-credential-state");
  const powerStatus = document.querySelector("#settings-power-status");
  const instructionsCount = document.querySelector("#settings-instructions-count");
  const pendingLink = document.querySelector("#settings-pending-link");
  let snapshot = null;
  let baselineFingerprint = "";
  let previewFingerprint = "";
  let busy = false;
  let selectedSection = "general";
  let lastEditedSection = "general";
  let localeReady = Promise.resolve();
  let previewActive = false;

  function fingerprint() {
    return snapshot ? JSON.stringify(settingsPatch(form, snapshot)) : "";
  }

  function syncPendingLink() {
    pendingLink.hidden = !snapshot || fingerprint() === baselineFingerprint ||
      ["general", "agent", "web"].includes(selectedSection);
  }

  function previewAppearance() {
    if (!snapshot) return;
    applyAppearance({ appearance: {
      theme: form.elements.theme.value,
      font_size: form.elements.font_size.value,
      density: form.elements.density.value,
    } });
  }

  function feedbackText(text, tone = "neutral") {
    feedback.textContent = text;
    feedback.dataset.tone = tone;
  }

  function setBusy(value) {
    busy = value;
    const dirty = Boolean(snapshot) && fingerprint() !== baselineFingerprint;
    previewButton.disabled = value || !dirty ||
      !form.elements.user_instructions.validity.valid || !validateProxy() ||
      !validatePower();
    applyButton.disabled = value || !snapshot || previewFingerprint !== fingerprint();
    discardButton.disabled = value || !dirty;
    restoreButton.disabled = value || !snapshot?.user_patches?.settings;
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
      preferred.getClientRects().length ? preferred : feedback;
    if (target?.isConnected && target.getClientRects().length)
      target.focus({ preventScroll: true });
  }

  function revealSectionButton(button) {
    if (!button || !sectionNavigation.getClientRects().length) return;
    const area = sectionNavigation.getBoundingClientRect();
    const target = button.getBoundingClientRect();
    const inset = 8;
    if (sectionNavigation.scrollWidth > sectionNavigation.clientWidth) {
      if (target.left < area.left + inset)
        sectionNavigation.scrollLeft += target.left - area.left - inset;
      else if (target.right > area.right - inset)
        sectionNavigation.scrollLeft += target.right - area.right + inset;
    }
    if (sectionNavigation.scrollHeight > sectionNavigation.clientHeight) {
      if (target.top < area.top + inset)
        sectionNavigation.scrollTop += target.top - area.top - inset;
      else if (target.bottom > area.bottom - inset)
        sectionNavigation.scrollTop += target.bottom - area.bottom + inset;
    }
  }

  function revealCurrentSection() {
    revealSectionButton(sectionNavigation.querySelector('[aria-current="page"]'));
  }
  function keepFocusedSettingVisible() {
    const field = document.activeElement;
    if (!sectionContent.contains(field) ||
        !["INPUT", "TEXTAREA", "SELECT"].includes(field.tagName) ||
        document.querySelector("dialog[open]")?.contains(field) ||
        getComputedStyle(sectionLayout).display !== "block") return;
    const area = sectionLayout.getBoundingClientRect();
    const nav = sectionNavigation.getBoundingClientRect();
    const top = nav.top < area.bottom && nav.bottom > area.top
      ? Math.max(area.top, nav.bottom) : area.top;
    const target = field.getBoundingClientRect();
    const inset = 8;
    if (target.bottom > area.bottom - inset)
      sectionLayout.scrollTop += target.bottom - area.bottom + inset;
    else if (target.top < top + inset)
      sectionLayout.scrollTop += target.top - top - inset;
  }

  function syncResponsiveSettings() {
    revealCurrentSection();
    keepFocusedSettingVisible();
  }
  window.addEventListener("resize", syncResponsiveSettings);
  const sectionResizeObserver = typeof ResizeObserver === "function"
    ? new ResizeObserver(syncResponsiveSettings) : null;
  sectionResizeObserver?.observe(sectionNavigation);
  sectionResizeObserver?.observe(sectionLayout);

  function renderStatus(settings) {
    revision.textContent = t("settings.revision", { revision: settings.revision },
      `配置 revision ${settings.revision}`) +
      (settings.runtime_override ? t("settings.runtimeOverride", {}, " · 含运行时覆盖") : "");
    feedbackText(settings.transaction_service.runtime_consistent
      ? t("settings.synced", {}, "配置与本地服务保持同步。")
      : t("settings.runtimeError", { error: settings.transaction_service.last_error },
        `运行时配置需要处理：${settings.transaction_service.last_error}`),
    settings.transaction_service.runtime_consistent ? "neutral" : "error");
  }

  function renderCredential(settings) {
    credential.textContent = settings.web.credential_configured
      ? t("settings.credentialConfigured", {},
        "已配置搜索凭据引用。实际值仅在服务端调用时读取，此处不确认其是否可用。")
      : t("settings.credentialMissing", {},
        "尚未配置搜索凭据引用。请通过 mdo Home 文件或环境变量配置。");
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

  function fill(settings) {
    snapshot = settings;
    form.elements.locale.value = supportedLocales.includes(settings.locale)
      ? settings.locale : "zh-CN";
    const selectedLocale = form.elements.locale.value;
    localeReady = loadLocale(selectedLocale).then((applied) => {
      if (applied && snapshot === settings && form.elements.locale.value === selectedLocale) {
        renderCredential(settings);
        renderProxyCredential(settings);
        renderPowerStatus(settings);
        validateInstructions();
        if (fingerprint() === baselineFingerprint) renderStatus(settings);
        else markDirty();
      }
    }).catch((error) => toast(errorMessage(error), "error"));
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
    form.elements.interaction_mode.value = settings.agent.interaction_mode;
    form.elements.reasoning_effort.value = settings.agent.reasoning_effort;
    form.elements.user_instructions.value = settings.agent.user_instructions ?? "";
    validateInstructions();
    form.elements.max_parallel_tools.value = settings.agent.max_parallel_tools;
    form.elements.max_parallel_subagents.value = settings.agent.max_parallel_subagents;
    form.elements.web_search.checked = settings.agent.web_search;
    form.elements.memory.checked = settings.agent.memory;
    form.elements.schedules.checked = settings.agent.schedules;
    form.elements.web_enabled.checked = settings.web.enabled;
    form.elements.allow_http.checked = settings.web.allow_http;
    form.elements.allow_private_networks.checked = settings.web.allow_private_networks;
    form.elements.timeout_ms.value = settings.web.timeout_ms;
    form.elements.idle_timeout_ms.value = settings.web.idle_timeout_ms;
    form.elements.max_response_bytes.value = settings.web.max_response_bytes;
    form.elements.max_text_bytes.value = settings.web.max_text_bytes;
    form.elements.max_documents.value = settings.web.max_documents;
    form.elements.max_results.value = settings.web.max_results;
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
    renderCredential(settings);
    previewFingerprint = "";
    syncPendingLink();
    restoreConfirm.hidden = true;
    renderStatus(settings);
    applyAppearance(settings);
    setBusy(false);
  }

  function markDirty(event) {
    const section = event?.target?.closest?.("[data-settings-panel]")?.dataset.settingsPanel;
    if (["general", "agent", "web"].includes(section)) lastEditedSection = section;
    previewAppearance();
    const validInstructions = validateInstructions();
    const validProxy = validateProxy();
    const validPower = validatePower();
    previewFingerprint = "";
    const dirty = Boolean(snapshot) && fingerprint() !== baselineFingerprint;
    syncPendingLink();
    previewButton.disabled = busy || !dirty || !validInstructions ||
      !validProxy || !validPower;
    applyButton.disabled = true;
    discardButton.disabled = busy || !dirty;
    feedbackText(!validInstructions
      ? t("settings.instructionsTooLong", {}, "自定义指令不能超过 8192 字节。")
      : !validProxy
        ? t("settings.proxyInvalid", {}, "代理地址、端口或用户名需要检查。")
      : !validPower
        ? t("settings.preventSleepUnavailable")
      : dirty
        ? t("settings.pending", {}, "有尚未预览的更改。先预览，确认后再应用。")
        : t("settings.synced", {}, "配置与本地服务保持同步。"),
    validInstructions && validProxy && validPower ? "neutral" : "error");
  }
  form.addEventListener("input", markDirty);
  form.addEventListener("change", markDirty);
  pendingLink.addEventListener("click", () => {
    navigation.openSettings(lastEditedSection);
    sectionNavigation.querySelector(
      `[data-settings-section="${lastEditedSection}"]`)?.focus({ preventScroll: true });
  });
  form.elements.locale.addEventListener("change", async () => {
    try {
      const applied = await loadLocale(form.elements.locale.value);
      if (!applied) return;
      if (snapshot) renderStatus(snapshot);
      if (snapshot) renderCredential(snapshot);
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

  previewButton.addEventListener("click", async () => {
    if (!snapshot || !form.reportValidity() || !validatePower()) return;
    let nextFocus = previewButton;
    setBusy(true);
    try {
      const patch = settingsPatch(form, snapshot);
      const preview = await previewSettings(patch);
      previewFingerprint = JSON.stringify(patch);
      feedbackText(preview.changes
        ? t("settings.previewBytes", { bytes: preview.patch_bytes },
          `预览通过，将合并 ${preview.patch_bytes} 字节配置。`)
        : t("settings.previewNoChange", {}, "预览通过，当前输入不会改变有效配置。"),
      preview.changes ? "success" : "neutral");
      applyButton.disabled = !preview.changes;
      if (preview.changes) nextFocus = applyButton;
    } catch (error) {
      previewFingerprint = "";
      feedbackText(errorMessage(error), "error");
    } finally {
      busy = false;
      setBusy(false);
      focusAfterAction(nextFocus);
    }
  });

  applyButton.addEventListener("click", async () => {
    if (!snapshot || !form.reportValidity() || !validatePower() ||
        previewFingerprint !== fingerprint()) return;
    setBusy(true);
    try {
      const result = await applySettings(settingsPatch(form, snapshot), snapshot.etag);
      feedbackText(result.changed
        ? t("settings.appliedRevision", { revision: result.revision },
          `配置 revision ${result.revision} 已生效。`)
        : t("settings.noChanges", {}, "配置没有变化。"), "success");
      toast(t("settings.appliedToast", {}, "设置已应用"));
      onApplied?.();
    } catch (error) {
      previewFingerprint = "";
      feedbackText(errorMessage(error), "error");
    } finally {
      busy = false;
      focusAfterAction();
    }
  });

  discardButton.addEventListener("click", () => {
    if (snapshot) fill(snapshot);
    // fill() disables the clicked button; move focus before the browser blurs it.
    feedback.focus({ preventScroll: true });
  });
  function closeRestoreConfirm() {
    restoreConfirm.hidden = true;
    (restoreButton.disabled ? feedback : restoreButton).focus({ preventScroll: true });
  }
  restoreButton.addEventListener("click", () => {
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
    setBusy(true);
    try {
      const result = await restoreSettings(snapshot.etag);
      restoreConfirm.hidden = true;
      feedbackText(t("settings.restoreAppliedRevision", { revision: result.revision },
        `已恢复内置默认值，当前 revision ${result.revision}。`), "success");
      toast(t("settings.restoreToast", {}, "已恢复默认设置"));
      onApplied?.();
    } catch (error) {
      feedbackText(errorMessage(error), "error");
    } finally {
      busy = false;
      cancelRestoreButton.disabled = false;
      confirmRestoreButton.disabled = false;
      focusAfterAction(
        restoreConfirm.hidden ? feedback : confirmRestoreButton);
    }
  });

  for (const button of document.querySelectorAll("[data-settings-section]")) {
    button.addEventListener("click", () => navigation.openSettings(button.dataset.settingsSection));
  }

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
        renderCredential(snapshot);
        renderProxyCredential(snapshot);
        renderPowerStatus(snapshot);
        validateInstructions();
        if (fingerprint() === baselineFingerprint) renderStatus(snapshot);
        else markDirty();
      }).catch((error) => toast(errorMessage(error), "error"));
    },
    selectSection(section) {
      const available = [...document.querySelectorAll("[data-settings-panel]")];
      const selected = available.some((panel) => panel.dataset.settingsPanel === section) ? section : "general";
      selectedSection = selected;
      const previous = sectionNavigation.querySelector('[aria-current="page"]')?.dataset.settingsSection;
      for (const panel of available) panel.hidden = panel.dataset.settingsPanel !== selected;
      let activeButton = null;
      for (const button of document.querySelectorAll("[data-settings-section]")) {
        if (button.dataset.settingsSection === selected) {
          button.setAttribute("aria-current", "page");
          activeButton = button;
        }
        else button.removeAttribute("aria-current");
      }
      if (previous !== selected) {
        sectionLayout.scrollTop = 0;
        sectionContent.scrollTop = 0;
      }
      revealSectionButton(activeButton);
      syncPendingLink();
    },
    destroy() {
      unsubscribe();
      window.removeEventListener("resize", syncResponsiveSettings);
      sectionResizeObserver?.disconnect();
    },
  });
}
