export function createImagePreview({ dialog, image, closeButton, navigation }) {
  let origin = null;

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
    image.alt = thumbnail.alt || "图片预览";
    dialog.showModal();
    closeButton.focus();
  });

  closeButton.addEventListener("click", close);
  dialog.addEventListener("click", (event) => {
    if (event.target === dialog) close();
  });
  dialog.addEventListener("close", () => {
    image.removeAttribute("src");
    image.alt = "";
    if (origin?.isConnected) origin.focus();
    origin = null;
  });
  navigation.subscribe(close);
}
