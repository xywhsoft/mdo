import { subscribeLocale, t } from "../../i18n.js";
import { previewImageName } from "./image-names.js";
import { bindTargetImage, releaseTargetImage } from "../../api/target-image.js";

export function createImagePreview({ dialog, image, caption, closeButton,
  status, retryButton, navigation }) {
  let active = null;
  let currentImage = image;

  function showName(name) {
    currentImage.alt = name || t("image.previewTitle", {}, "图片预览");
    if (caption) {
      caption.textContent = currentImage.alt;
      caption.title = currentImage.alt;
    }
  }

  function renderLoadState() {
    const phase = active?.phase;
    dialog.dataset.imageState = phase || "";
    currentImage.hidden = phase !== "ready";
    currentImage.setAttribute("aria-busy", String(phase === "loading"));
    if (status) {
      status.hidden = phase !== "loading" && phase !== "error";
      status.textContent = phase === "loading"
        ? t("image.previewLoading", {}, "正在加载图片…")
        : phase === "error"
          ? t("image.previewFailed", {}, "图片暂时无法显示，请重新载入或关闭后重试。") : "";
    }
    if (retryButton) {
      // Keep the focused retry visible during its request. On success, return
      // focus to the close button before hiding it instead of losing focus.
      const visible = phase === "error" || (phase === "loading" && active.retried);
      if (!visible && dialog.open && document.activeElement === retryButton) closeButton.focus();
      retryButton.hidden = !visible;
      retryButton.setAttribute("aria-disabled", String(phase !== "error"));
      retryButton.textContent = t("image.previewRetry", {}, "重新载入");
    }
  }

  function load(selected) {
    selected.phase = "loading";
    // A DOM image owns one request. Reusing a node would let an old queued
    // load/error event complete a later preview or retry of the same URL.
    const next = currentImage.cloneNode(false);
    next.removeAttribute("src");
    const previous = currentImage;
    currentImage = next;
    previous.replaceWith(next);
    previous.removeAttribute("src");
    const finish = (loaded) => {
      if (active !== selected || currentImage !== next || !dialog.open ||
          selected.phase !== "loading") return;
      selected.phase = loaded && next.naturalWidth > 0 ? "ready" : "error";
      renderLoadState();
    };
    next.addEventListener("load", () => finish(true), { once: true });
    next.addEventListener("error", () => finish(false), { once: true });
    showName(selected.name);
    renderLoadState();
    bindTargetImage(next, selected.source);
  }

  function finishClose(restoreFocus = true) {
    const previous = active;
    active = null;
    releaseTargetImage(currentImage);
    currentImage.removeAttribute("src");
    currentImage.alt = "";
    renderLoadState();
    if (caption) { caption.textContent = ""; caption.removeAttribute("title"); }
    if (!restoreFocus || !previous) return;
    const target = previous.trigger.isConnected ? previous.trigger :
      previous.reference ? [...document.querySelectorAll("button[data-image-ref]")]
        .find((button) => button.dataset.imageRef === previous.reference) : null;
    target?.focus({ preventScroll: true });
  }

  function close(restoreFocus = true) {
    if (dialog.open) dialog.close();
    // Native close events are queued. Finish this preview now so an old event
    // cannot erase a reopened image or steal focus from a navigation target.
    finishClose(restoreFocus);
  }

  // Message rows are replaced as new events arrive, so delegate from the
  // document instead of attaching listeners to short-lived thumbnails.
  document.addEventListener("click", (event) => {
    const trigger = event.target instanceof Element
      ? event.target.closest("button[data-image-preview]") : null;
    if (!trigger || dialog.open) return;
    const thumbnail = trigger.querySelector("img");
    if (!thumbnail || (!thumbnail.src && !thumbnail.dataset.mdoImagePath)) return;
    const selected = { trigger, reference: trigger.dataset.imageRef,
      source: thumbnail.dataset.mdoImagePath || thumbnail.currentSrc || thumbnail.src, name: thumbnail.alt, retried: false };
    active = selected;
    dialog.showModal();
    load(selected);
    closeButton.focus();
    previewImageName(trigger).then((name) => {
      if (name && active === selected && dialog.open) {
        selected.name = name;
        showName(name);
      }
    });
  });

  retryButton?.addEventListener("click", () => {
    if (!dialog.open || active?.phase !== "error") return;
    active.retried = true;
    load(active);
  });
  subscribeLocale(() => {
    if (!active) return;
    showName(active.name);
    renderLoadState();
  });
  closeButton.addEventListener("click", () => close());
  dialog.addEventListener("click", (event) => {
    if (event.target === dialog) close();
  });
  dialog.addEventListener("close", () => {
    // Also support a caller closing the native dialog directly. Ignore an old
    // queued close event if a newer preview has already reopened it.
    if (!dialog.open && active) finishClose();
  });
  dialog.addEventListener("cancel", (event) => {
    event.preventDefault();
    close();
  });
  navigation.subscribe(() => close(false));
}
