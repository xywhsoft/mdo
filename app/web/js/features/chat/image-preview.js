import { t } from "../../i18n.js";
import { previewImageName } from "./image-names.js";

export function createImagePreview({ dialog, image, caption, closeButton, navigation }) {
  let active = null;

  function showName(name) {
    image.alt = name || t("image.previewTitle", {}, "图片预览");
    if (caption) {
      caption.textContent = image.alt;
      caption.title = image.alt;
    }
  }

  function finishClose(restoreFocus = true) {
    const previous = active;
    active = null;
    image.removeAttribute("src");
    image.alt = "";
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
    if (!thumbnail?.src) return;
    const selected = { trigger, reference: trigger.dataset.imageRef };
    active = selected;
    image.src = thumbnail.currentSrc || thumbnail.src;
    showName(thumbnail.alt);
    dialog.showModal();
    closeButton.focus();
    previewImageName(trigger).then((name) => {
      if (name && active === selected && dialog.open) showName(name);
    });
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
