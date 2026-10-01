import { t } from "../../i18n.js";

export function createImagePreview({ dialog, image, caption, closeButton, navigation }) {
  let origin = null;
  let nameObserver = null;

  function showName(thumbnail) {
    image.alt = thumbnail.alt || t("image.previewTitle", {}, "图片预览");
    if (caption) {
      caption.textContent = image.alt;
      caption.title = image.alt;
    }
  }

  function close() {
    if (dialog.open) dialog.close();
  }

  // Message rows are replaced as new events arrive, so delegate from the
  // document instead of attaching listeners to short-lived thumbnails.
  document.addEventListener("click", (event) => {
    const trigger = event.target instanceof Element
      ? event.target.closest("button[data-image-preview]") : null;
    if (!trigger || dialog.open) return;
    const thumbnail = trigger.querySelector("img");
    if (!thumbnail?.src) return;
    origin = trigger;
    image.src = thumbnail.currentSrc || thumbnail.src;
    showName(thumbnail);
    // Metadata can arrive after the user opens a thumbnail. Observe only the
    // active source image, then release it on close/navigation.
    if (caption) {
      nameObserver = new MutationObserver(() => showName(thumbnail));
      nameObserver.observe(thumbnail, { attributes: true, attributeFilter: ["alt"] });
    }
    dialog.showModal();
    closeButton.focus();
  });

  closeButton.addEventListener("click", close);
  dialog.addEventListener("click", (event) => {
    if (event.target === dialog) close();
  });
  dialog.addEventListener("close", () => {
    nameObserver?.disconnect();
    nameObserver = null;
    image.removeAttribute("src");
    image.alt = "";
    if (caption) { caption.textContent = ""; caption.removeAttribute("title"); }
    const reference = origin?.dataset.imageRef;
    const target = origin?.isConnected ? origin :
      reference ? [...document.querySelectorAll("button[data-image-ref]")]
        .find((button) => button.dataset.imageRef === reference) : null;
    target?.focus({ preventScroll: true });
    origin = null;
  });
  navigation.subscribe(close);
}
