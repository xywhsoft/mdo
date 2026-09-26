import { api, attachmentUrl } from "../../api/client.js";
import { clear, element, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";

const TYPES = new Set(["image/png", "image/jpeg", "image/webp"]);
const EXTENSION_TYPES = new Map([
  ["png", "image/png"], ["jpg", "image/jpeg"],
  ["jpeg", "image/jpeg"], ["webp", "image/webp"],
]);
const MAX_IMAGE_BYTES = 8 * 1024 * 1024;

export function imageUploadType(file) {
  if (TYPES.has(file?.type)) return file.type;
  if (file?.type && file.type !== "application/octet-stream") return "";
  const extension = /\.([^.]+)$/.exec(file?.name ?? "")?.[1]?.toLowerCase();
  return EXTENSION_TYPES.get(extension) ?? "";
}

function unsupportedModelError() {
  const error = new Error(t("image.unsupportedModel", {},
    "当前模型不支持图片，请先切换到支持图片的模型"));
  error.code = "image_model_unsupported";
  return error;
}

function selectionError(message) {
  const error = new Error(message);
  error.code = "image_selection_invalid";
  return error;
}

export function createComposerImages({ composer, prompt, button, input, strip,
  navigation, modelsStore, sessionStore, ensureSession, onChange, onRemove,
  onUploading, onError }) {
  let ids = [];
  const uploads = new Set();
  const removals = new Set();
  let writable = true;
  let dragDepth = 0;

  function clearDragTarget() {
    dragDepth = 0;
    composer.removeAttribute("data-drag-over");
  }

  function owner() {
    const { projectId, sessionId } = navigation.get();
    return projectId && sessionId ? { projectId, sessionId } : null;
  }

  function scopeKey() {
    const { projectId, sessionId } = navigation.get();
    return `${projectId || "default"}/${sessionId || "@new"}`;
  }

  function uploadingCurrent() {
    return uploads.has(scopeKey());
  }

  function removingCurrent() {
    const selected = owner();
    return Boolean(selected && removals.has(
      `${selected.projectId}/${selected.sessionId}`));
  }

  function render() {
    composer.dataset.dropLabel = t("image.dropHint", {}, "松开以添加图片");
    const focused = strip.contains(document.activeElement)
      ? document.activeElement : null;
    const focusedId = focused?.dataset.imageId;
    const focusedIndex = Number(focused?.dataset.imageIndex ?? 0);
    const focusedKind = focused?.classList.contains("composer-image-remove")
      ? "remove" : focused?.matches("[data-image-preview]") ? "preview" : "";
    clear(strip);
    const selected = owner();
    const uploading = uploadingCurrent();
    const removing = removingCurrent();
    strip.hidden = ids.length === 0 && !uploading && !removing;
    if (selected) for (const [index, id] of ids.entries()) {
      if (!/^[0-9a-f]{32}$/.test(id)) continue;
      const remove = element("button", {
        className: "composer-image-remove", text: "×",
        attrs: { type: "button", "aria-label": t("image.remove", { number: index + 1 },
          `移除图片 ${index + 1}`),
          "data-image-id": id, "data-image-index": String(index) },
      });
      remove.disabled = uploading || removing || !writable;
      remove.addEventListener("click", async () => {
        if (removals.has(`${selected.projectId}/${selected.sessionId}`)) return;
        const previous = [...ids];
        const key = `${selected.projectId}/${selected.sessionId}`;
        const restoreFocus = document.activeElement === remove;
        removals.add(key);
        onUploading(true);
        try {
          ids = ids.filter((_, position) => position !== index);
          onChange([...ids]);
          render();
          if (await onRemove?.(selected, id, previous) === false)
            throw new Error(t("image.removeRollback", {}, "草稿未保存，图片已恢复"));
        } catch (error) {
          const current = owner();
          if (`${current?.projectId}/${current?.sessionId}` === key) {
            ids = previous;
            onChange([...ids]);
            onError(error);
          }
        } finally {
          removals.delete(key);
          onUploading(false);
          render();
          const current = owner();
          if (restoreFocus && `${current?.projectId}/${current?.sessionId}` === key &&
              (document.activeElement === document.body ||
                strip.contains(document.activeElement))) {
            const choices = [...strip.querySelectorAll(".composer-image-remove")];
            (choices[Math.min(index, choices.length - 1)] ?? button)
              .focus({ preventScroll: true });
          }
        }
      });
      strip.append(element("div", { className: "composer-image" }, [
        element("button", { className: "composer-image-preview", attrs: {
          type: "button", "aria-label": t("image.view", { number: index + 1 },
            `查看图片 ${index + 1}`),
          "data-image-preview": "",
          "data-image-id": id, "data-image-index": String(index),
          "data-image-ref": `draft:${selected.projectId}/${selected.sessionId}/${id}/${index}`,
        } }, [element("img", { attrs: { src: attachmentUrl(selected.projectId,
          selected.sessionId, id), alt: t("image.alt", { number: index + 1 },
          `图片 ${index + 1}`) } })]),
        remove,
      ]));
    }
    if (uploading) strip.append(element("span", {
      className: "composer-image-uploading",
      text: t("image.saving", {}, "正在保存图片…"),
    }));
    if (removing) strip.append(element("span", {
      className: "composer-image-uploading",
      text: t("image.removing", {}, "正在移除图片…"),
    }));
    button.disabled = !writable || uploading || removing;
    if (focusedKind && !removing) {
      const candidates = [...strip.querySelectorAll(focusedKind === "remove"
        ? ".composer-image-remove" : "[data-image-preview]")];
      const next = candidates.find((item) =>
        item.dataset.imageId === focusedId) ??
        candidates[Math.min(focusedIndex, candidates.length - 1)];
      (next ?? button).focus({ preventScroll: true });
    }
  }

  function imageCapable() {
    const modelId = sessionStore.get().data?.model_id ||
      document.querySelector("#composer-model")?.value;
    const model = modelsStore.get().data?.models?.find((item) =>
      item.id === modelId);
    return Boolean(Number(model?.attachments ?? 0) & 1);
  }

  async function addFiles(files) {
    const candidates = [...files];
    if (!candidates.length || uploadingCurrent() || removingCurrent()) return;
    if (!writable) { onError(new Error(t("image.readOnly", {},
      "当前会话不可添加图片"))); return; }
    if (!imageCapable()) {
      onError(unsupportedModelError());
      return;
    }
    const images = [];
    let otherFiles = 0;
    let invalidSize = 0;
    for (const file of candidates) {
      const mime = imageUploadType(file);
      if (!mime) otherFiles += 1;
      else if (file.size === 0 || file.size > MAX_IMAGE_BYTES) invalidSize += 1;
      else images.push({ file, mime });
    }
    if (!images.length) {
      onError(selectionError(otherFiles ? t("image.typesOnly", {},
        "仅支持 PNG、JPEG 和 WebP 图片") : t("image.maxSize", {},
        "单张图片不得超过 8 MiB")));
      return;
    }
    if (images.length + ids.length > 4) {
      onError(selectionError(t("image.maxCount", {},
        "每条消息最多可添加 4 张图片")));
      return;
    }
    if (otherFiles) toast(t("image.skippedFiles", { count: otherFiles },
      `已跳过 ${otherFiles} 个非图片文件`));
    if (invalidSize) toast(t("image.skippedSize", { count: invalidSize },
      `已跳过 ${invalidSize} 张空白或超过 8 MiB 的图片`));
    let key = scopeKey();
    uploads.add(key);
    onUploading(true);
    render();
    try {
      const selected = owner() ?? await ensureSession(prompt.value);
      const sessionKey = `${selected.projectId}/${selected.sessionId}`;
      if (key !== sessionKey && scopeKey() !== sessionKey) return;
      if (key !== sessionKey) {
        uploads.delete(key);
        key = sessionKey;
        uploads.add(key);
        onUploading(true);
        render();
      }
      for (const { file, mime } of images) {
        const stored = await api.uploadImage(selected.projectId,
          selected.sessionId, file, mime);
        if (scopeKey() !== key) {
          void api.deleteImage(selected.projectId, selected.sessionId,
            stored.id).catch(() => {});
          break;
        }
        ids = [...ids, stored.id];
        onChange([...ids]);
        render();
      }
    } catch (error) {
      if (scopeKey() === key) onError(error);
      else toast(t("image.previousUnsaved", {}, "原会话图片未保存"), "error");
    }
    finally {
      uploads.delete(key);
      onUploading(false);
      input.value = "";
      render();
    }
  }

  button.addEventListener("click", () => {
    if (!imageCapable()) {
      onError(unsupportedModelError());
      return;
    }
    input.click();
  });
  input.addEventListener("change", () => {
    const files = [...(input.files ?? [])];
    // A rejected file should still trigger change when selected again.
    input.value = "";
    void addFiles(files);
  });
  prompt.addEventListener("paste", (event) => {
    const images = [...(event.clipboardData?.files ?? [])].filter((file) =>
      Boolean(imageUploadType(file)));
    if (!images.length) return;
    event.preventDefault();
    void addFiles(images);
  });
  composer.addEventListener("dragenter", (event) => {
    if (![...(event.dataTransfer?.types ?? [])].includes("Files")) return;
    dragDepth += 1;
    composer.setAttribute("data-drag-over", "");
  });
  composer.addEventListener("dragover", (event) => {
    if (![...(event.dataTransfer?.types ?? [])].includes("Files")) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = "copy";
    composer.setAttribute("data-drag-over", "");
  });
  composer.addEventListener("dragleave", () => {
    if (dragDepth > 0) dragDepth -= 1;
    if (dragDepth === 0) clearDragTarget();
  });
  composer.addEventListener("drop", (event) => {
    if (![...(event.dataTransfer?.types ?? [])].includes("Files")) return;
    event.preventDefault();
    clearDragTarget();
    void addFiles(event.dataTransfer?.files ?? []);
  });
  window.addEventListener("dragend", clearDragTarget);
  window.addEventListener("blur", clearDragTarget);
  subscribeLocale(render);

  render();
  return Object.freeze({
    get: () => [...ids],
    set(value) { ids = Array.isArray(value) ? [...value] : []; render(); },
    clear() { ids = []; render(); },
    isUploading: () => uploadingCurrent() || removingCurrent(),
    supportsCurrentModel: imageCapable,
    setWritable(value) { writable = Boolean(value); render(); },
  });
}
