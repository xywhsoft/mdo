import { mountIcons } from "../../components/icons.js";
import { isImeKey } from "../../utils/dom.js";
import { findModel } from "../../utils/models.js";
import { t } from "../../i18n.js";

const PERMISSION_ICON = Object.freeze({
  "read-only": "shield-check", balanced: "hand", "full-access": "shield-alert",
});

// Presentation only. Native selects remain the profile controller's source of
// truth, so queued runs, persistence and permission enforcement are unchanged.
export function createComposerControls({ root, modelSelect, reasoningSelect,
  permissionSelect, status, modelsStore, navigation }) {
  const doc = root.ownerDocument;
  const win = doc.defaultView;
  const modelTrigger = root.querySelector("#composer-model-trigger");
  const permissionTrigger = root.querySelector("#composer-permission-trigger");
  const modelPanel = root.querySelector("#composer-model-panel");
  const permissionPanel = root.querySelector("#composer-permission-panel");
  const modelLabel = root.querySelector("#composer-model-label");
  const modelEffort = root.querySelector("#composer-model-effort");
  const estimate = root.querySelector("#composer-input-estimate");
  const permissionIcon = root.querySelector("#composer-permission-icon");
  const slider = root.querySelector("#composer-effort-slider");
  const effortLabel = root.querySelector("#composer-effort-label");
  const scale = root.querySelector("#composer-effort-scale");
  const permissionStatus = root.querySelector("#composer-permission-status");
  const choices = [...permissionPanel.querySelectorAll("[data-permission]")];
  let opened = null;
  let anchor = null;
  // Body-owned fixed panels stay above the composer and inside the keyboard
  // viewport, independent of any ancestor's containment or overflow rules.
  doc.body.append(modelPanel, permissionPanel);

  function fitEstimate() {
    // Try the optional text at its natural width. Keep it only if all controls
    // fit without truncating the model; there is no fixed phone breakpoint.
    estimate.hidden = !estimate.textContent.trim();
    if (!estimate.hidden) estimate.hidden = root.scrollWidth > root.clientWidth + 1
      || modelLabel.scrollWidth > modelLabel.clientWidth + 1
      || modelEffort.scrollWidth > modelEffort.clientWidth + 1;
  }

  function position() {
    if (!opened) return;
    const viewport = win.visualViewport;
    const left = viewport?.offsetLeft || 0;
    const top = viewport?.offsetTop || 0;
    const width = viewport?.width || win.innerWidth;
    const height = viewport?.height || win.innerHeight;
    const rect = anchor.getBoundingClientRect();
    const panelWidth = Math.min(320, width - 24);
    opened.style.width = `${panelWidth}px`;
    opened.style.maxHeight = `${Math.max(40, height - 24)}px`;
    opened.style.left = `${Math.max(left + 12, Math.min(rect.left, left + width - panelWidth - 12))}px`;
    const panelHeight = opened.getBoundingClientRect().height;
    opened.style.top = `${Math.max(top + 12, Math.min(rect.top - panelHeight - 8,
      top + height - panelHeight - 12))}px`;
  }

  function close(restoreFocus = false) {
    if (!opened) return;
    opened.hidden = true;
    anchor.setAttribute("aria-expanded", "false");
    if (restoreFocus) anchor.focus({ preventScroll: true });
    opened = anchor = null;
  }
  function toggle(panel, trigger) {
    const wasOpen = opened === panel;
    close();
    if (wasOpen) return;
    opened = panel; anchor = trigger;
    panel.hidden = false; panel.scrollTop = 0;
    trigger.setAttribute("aria-expanded", "true");
    position();
    if (panel === permissionPanel) (choices.find((item) => item.getAttribute("aria-checked") === "true") || choices[0]).focus({ preventScroll: true });
    else panel.focus({ preventScroll: true });
  }

  function sync() {
    const model = findModel(modelsStore.get().data?.models ?? [], modelSelect.value);
    modelLabel.textContent = model?.name || modelSelect.value || t("shell.model", {}, "模型");
    const efforts = [...reasoningSelect.options];
    const current = Math.max(0, efforts.findIndex((item) => item.value === reasoningSelect.value));
    slider.max = String(Math.max(0, efforts.length - 1)); slider.value = String(current);
    slider.disabled = reasoningSelect.disabled || efforts.length < 2;
    slider.setAttribute("aria-valuetext", efforts[current]?.textContent || t("reasoning.none", {}, "无思考"));
    effortLabel.textContent = slider.getAttribute("aria-valuetext");
    modelEffort.textContent = effortLabel.textContent;
    scale.replaceChildren();
    for (const option of efforts) {
      const label = doc.createElement("span"); label.textContent = option.textContent; scale.append(label);
    }
    modelTrigger.disabled = modelSelect.disabled && reasoningSelect.disabled;
    const next = !status.hidden ? status.textContent : "";
    root.dataset.profileState = next ? status.dataset.state || "pending" : "";
    modelTrigger.title = `${modelLabel.textContent} · ${effortLabel.textContent}${next ? ` · ${next}` : ""}`;
    modelTrigger.setAttribute("aria-label", `${t("composer.modelSettings", {}, "模型与思考深度")}：${modelTrigger.title}`);
    permissionTrigger.disabled = permissionSelect.disabled;
    permissionTrigger.dataset.permission = permissionSelect.value;
    permissionIcon.dataset.icon = PERMISSION_ICON[permissionSelect.value] || "shield";
    mountIcons(permissionTrigger);
    const permissionName = permissionSelect.selectedOptions[0]?.textContent || t("shell.permission", {}, "权限");
    permissionTrigger.title = `${permissionName}${next ? ` · ${next}` : ""}`;
    permissionTrigger.setAttribute("aria-label", `${t("shell.permission", {}, "权限")}：${permissionTrigger.title}`);
    for (const choice of choices) {
      choice.setAttribute("aria-checked", String(choice.dataset.permission === permissionSelect.value));
      choice.disabled = permissionSelect.disabled;
    }
    permissionStatus.hidden = status.hidden;
    permissionStatus.textContent = status.textContent;
    permissionStatus.dataset.state = status.dataset.state || "";
    fitEstimate(); position();
  }

  const handlers = [];
  function listen(target, name, handler, options) {
    target.addEventListener(name, handler, options);
    handlers.push(() => target.removeEventListener(name, handler, options));
  }
  listen(modelTrigger, "click", () => toggle(modelPanel, modelTrigger));
  listen(permissionTrigger, "click", () => toggle(permissionPanel, permissionTrigger));
  for (const button of modelPanel.querySelectorAll("[data-profile-close]"))
    listen(button, "click", () => close(true));
  listen(slider, "input", () => {
    const label = reasoningSelect.options[Number(slider.value)]?.textContent || "";
    effortLabel.textContent = label; slider.setAttribute("aria-valuetext", label);
  });
  listen(slider, "change", () => {
    if (slider.disabled) return;
    reasoningSelect.value = reasoningSelect.options[Number(slider.value)]?.value || "";
    reasoningSelect.dispatchEvent(new Event("change", { bubbles: true }));
  });
  for (const choice of choices) listen(choice, "click", () => {
    if (permissionSelect.disabled) return;
    permissionSelect.value = choice.dataset.permission;
    close(true);
    permissionSelect.dispatchEvent(new Event("change", { bubbles: true }));
  });
  listen(doc, "pointerdown", (event) => {
    if (opened && !opened.contains(event.target) && !modelTrigger.contains(event.target)
        && !permissionTrigger.contains(event.target)) close();
  });
  listen(doc, "focusin", (event) => {
    if (opened && !opened.contains(event.target) && !modelTrigger.contains(event.target)
        && !permissionTrigger.contains(event.target)) close();
  });
  listen(doc, "keydown", (event) => {
    if (!opened || event.defaultPrevented || isImeKey(event) || doc.querySelector("dialog[open]")) return;
    if (event.key === "Escape") {
      // Do not let closing a popup also reach Escape-to-stop the active Agent.
      event.preventDefault(); event.stopImmediatePropagation(); close(true);
    } else if (opened === permissionPanel && ["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) {
      event.preventDefault();
      const index = choices.indexOf(doc.activeElement);
      const next = event.key === "Home" ? 0 : event.key === "End" ? choices.length - 1
        : (index + (event.key === "ArrowUp" ? -1 : 1) + choices.length) % choices.length;
      choices[next].focus();
    }
  }, true);
  listen(win, "resize", position);
  if (win.visualViewport) {
    listen(win.visualViewport, "resize", position);
    listen(win.visualViewport, "scroll", position);
  }
  const observer = typeof win.ResizeObserver === "function"
    ? new win.ResizeObserver(() => { fitEstimate(); position(); }) : null;
  observer?.observe(root);
  observer?.observe(modelTrigger);
  const estimateObserver = new win.MutationObserver(fitEstimate);
  estimateObserver.observe(estimate, { childList: true, characterData: true, subtree: true });
  const unsubscribe = navigation.subscribe(() => close());
  return Object.freeze({ sync, close, destroy() {
    close(); observer?.disconnect(); estimateObserver.disconnect(); unsubscribe(); handlers.forEach((remove) => remove());
    modelPanel.remove(); permissionPanel.remove();
  } });
}
