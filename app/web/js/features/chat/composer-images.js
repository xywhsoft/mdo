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

function hasDraggedFiles(transfer) {
  if (!transfer) return false;
  // WebViews do not all expose the same DataTransfer view before drop. The
  // file list can appear only at drop time, even when types lacked "Files".
  return [...(transfer.types ?? [])].includes("Files") ||
    [...(transfer.items ?? [])].some((item) => item.kind === "file") ||
    (transfer.files?.length ?? 0) > 0;
}

function pastedImages(clipboard) {
  // Some WebViews expose pasted images only through DataTransfer.items.
  // Prefer those entries to avoid uploading the same file twice when both
  // collections are populated, then fall back to DataTransfer.files.
  const itemFiles = [...(clipboard?.items ?? [])]
    .map((item) => {
      const file = typeof item.getAsFile === "function" ? item.getAsFile() : null;
      if (!file) return null;
      const mime = imageUploadType(file) ||
        ((!file.type || file.type === "application/octet-stream") &&
          TYPES.has(item.type) ? item.type : "");
      return mime ? { file, mime } : null;
    }).filter(Boolean);
  return itemFiles.length ? itemFiles : [...(clipboard?.files ?? [])]
    .map((file) => ({ file, mime: imageUploadType(file) }))
    .filter((entry) => Boolean(entry.mime));
}

function imageError(key, fallback, code = "") {
  const error = new Error(t(key, {}, fallback));
  error.code = code;
  error.localizedMessageKey = key;
  error.localizedMessageFallback = fallback;
  return error;
}

export function unsupportedModelError() {
  return imageError("image.unsupportedModel",
    "当前模型不支持图片，请先切换到支持图片的模型",
    "image_model_unsupported");
}

function selectionError(key, fallback) {
  return imageError(key, fallback, "image_selection_invalid");
}

export function createComposerImages({ composer, prompt, button, input, strip,
  modelSelect, navigation, modelsStore, sessionStore, ensureSession, onChange,
  onBeforeRemove, onRemove,
  onUploading, onError, onDiscardedUpload }) {
  let ids = [];
  const uploadJobs = new Map();
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

  function currentUpload() {
    return uploadJobs.get(scopeKey());
  }

  function uploadingCurrent() {
    return Boolean(currentUpload());
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
        let previous = [...ids];
        const key = `${selected.projectId}/${selected.sessionId}`;
        const restoreFocus = document.activeElement === remove;
        let changed = false;
        removals.add(key);
        onUploading(true);
        try {
          // Persist the cleanup intent before changing the draft. If the
          // process exits after the draft save, queue GET can resume cleanup.
          await onBeforeRemove?.(selected, id);
          if (scopeKey() !== key || !ids.includes(id)) return;
          previous = [...ids];
          const position = ids.indexOf(id);
          ids = ids.filter((_, itemIndex) => itemIndex !== position);
          changed = true;
          onChange([...ids]);
          render();
          if (await onRemove?.(selected, id, previous) === false)
            throw new Error(t("image.removeRollback", {}, "草稿未保存，图片已恢复"));
        } catch (error) {
          const current = owner();
          if (`${current?.projectId}/${current?.sessionId}` === key) {
            if (changed) {
              ids = previous;
              onChange([...ids]);
            }
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
    const pending = currentUpload()?.pending ?? 0;
    if (pending) strip.append(element("span", {
      className: "composer-image-uploading",
      text: t("image.pendingCount", { count: pending }, `待添加 ${pending} 张图片`),
    }));
    if (removing) strip.append(element("span", {
      className: "composer-image-uploading",
      text: t("image.removing", {}, "正在移除图片…"),
    }));
    if (ids.length && !imageCapable()) strip.append(element("span", {
      className: "composer-image-unsupported",
      text: t("image.draftUnsupported", {},
        "当前模型不支持图片；切换模型或移除图片后再发送"),
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
    const route = navigation.get();
    const session = sessionStore.get().data;
    // The editor may already target a different model for the next queued
    // message while the current run still owns the session's old model.
    const modelId = modelSelect?.value || (session?.id === route.sessionId &&
      session?.project_id === route.projectId ? session.model_id : "");
    const model = modelsStore.get().data?.models?.find((item) =>
      item.id === modelId);
    return Boolean(Number(model?.attachments ?? 0) & 1);
  }

  async function runUpload(job) {
    let discarded = false;
    try {
      const selected = owner() ?? await ensureSession(prompt.value);
      const sessionKey = `${selected.projectId}/${selected.sessionId}`;
      if (job.key !== sessionKey) {
        if (scopeKey() !== sessionKey) { discarded = true; return; }
        uploadJobs.delete(job.key);
        job.key = sessionKey;
        uploadJobs.set(sessionKey, job);
        onUploading(true);
        render();
      }
      while (job.batches.length && scopeKey() === job.key) {
        const batch = job.batches.shift();
        if (batch.queued) job.pending -= batch.images.length;
        for (const { file, mime } of batch.images) {
          if (scopeKey() !== job.key) { discarded = true; break; }
          let stored;
          try {
            stored = await api.uploadImage(selected.projectId,
              selected.sessionId, file, mime);
          } catch (error) {
            if (scopeKey() === job.key) onError(error);
            else { discarded = true; break; }
          } finally {
            job.reserved -= 1;
          }
          if (!stored) continue;
          if (scopeKey() !== job.key) {
            discarded = true;
            // The response arrived after this editor moved away. Give the
            // unreferenced attachment to the same retrying cleanup path used
            // for removed queue images; a transient DELETE failure must not
            // leave it behind silently.
            onDiscardedUpload(selected, stored.id);
            break;
          }
          ids = [...ids, stored.id];
          onChange([...ids]);
          render();
        }
        render();
      }
    } catch (error) {
      if (scopeKey() === job.key) onError(error);
      else discarded = true;
    } finally {
      uploadJobs.delete(job.key);
      if (discarded || job.pending)
        toast(t("image.pendingCancelled", {},
          "会话已切换或保存失败，待添加图片未保存"), "error");
      onUploading(false);
      input.value = "";
      render();
    }
  }

  function addFiles(files) {
    const candidates = [...files];
    if (!candidates.length) return;
    if (removingCurrent()) {
      onError(selectionError("image.waitForCurrent",
        "请等待当前图片操作完成后再添加"));
      return;
    }
    const job = currentUpload();
    // Creating an image-first task briefly marks the editor non-writable while
    // its session is being created. Already accepted uploads may still queue.
    if (!job && !writable) { onError(imageError("image.readOnly",
      "当前会话不可添加图片")); return; }
    if (!job && !imageCapable()) {
      onError(unsupportedModelError());
      return;
    }
    const images = [];
    let otherFiles = 0;
    let invalidSize = 0;
    for (const candidate of candidates) {
      const file = candidate.file ?? candidate;
      const mime = candidate.mime ?? imageUploadType(file);
      if (!mime) otherFiles += 1;
      else if (file.size === 0 || file.size > MAX_IMAGE_BYTES) invalidSize += 1;
      else images.push({ file, mime });
    }
    if (!images.length) {
      onError(otherFiles
        ? selectionError("image.typesOnly", "仅支持 PNG、JPEG 和 WebP 图片")
        : selectionError("image.maxSize", "请选择非空且不超过 8 MiB 的图片"));
      return;
    }
    if (images.length + ids.length + (job?.reserved ?? 0) > 4) {
      onError(selectionError("image.maxCount",
        "每条消息最多可添加 4 张图片"));
      return;
    }
    if (otherFiles) toast(t("image.skippedFiles", { count: otherFiles },
      `已跳过 ${otherFiles} 个非图片文件`));
    if (invalidSize) toast(t("image.skippedSize", { count: invalidSize },
      `已跳过 ${invalidSize} 张空白或超过 8 MiB 的图片`));
    if (job) {
      job.batches.push({ images, queued: true });
      job.reserved += images.length;
      job.pending += images.length;
      render();
      return;
    }
    const next = { key: scopeKey(), batches: [{ images, queued: false }],
      reserved: images.length, pending: 0 };
    uploadJobs.set(next.key, next);
    onUploading(true);
    render();
    void runUpload(next);
  }

  button.addEventListener("click", () => {
    if (!imageCapable()) {
      onError(unsupportedModelError());
      return;
    }
    input.click();
  });
  input.addEventListener("change", () => {
    const restorePromptFocus = [button, input, document.body,
      document.documentElement].includes(document.activeElement);
    const files = [...(input.files ?? [])];
    // A rejected file should still trigger change when selected again.
    input.value = "";
    void addFiles(files);
    // The native picker can return focus to the hidden input or page root.
    // Keep keyboard entry available while upload continues, without moving
    // focus when another control already owns it.
    if (restorePromptFocus) prompt.focus({ preventScroll: true });
  });
  prompt.addEventListener("paste", (event) => {
    const images = pastedImages(event.clipboardData);
    if (!images.length) return;
    event.preventDefault();
    void addFiles(images);
  });
  composer.addEventListener("dragenter", (event) => {
    if (!hasDraggedFiles(event.dataTransfer)) return;
    dragDepth += 1;
    composer.setAttribute("data-drag-over", "");
  });
  composer.addEventListener("dragover", (event) => {
    if (!hasDraggedFiles(event.dataTransfer)) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = "copy";
    composer.setAttribute("data-drag-over", "");
  });
  composer.addEventListener("dragleave", () => {
    if (dragDepth > 0) dragDepth -= 1;
    if (dragDepth === 0) clearDragTarget();
  });
  composer.addEventListener("drop", (event) => {
    if (!hasDraggedFiles(event.dataTransfer)) return;
    event.preventDefault();
    clearDragTarget();
    void addFiles(event.dataTransfer?.files ?? []);
  });
  // A file released outside the composer would otherwise replace the page in
  // some browsers, taking an unsent draft with it. Leave other drop targets
  // alone when they have already handled the event.
  for (const type of ["dragover", "drop"]) {
    window.addEventListener(type, (event) => {
      if (!hasDraggedFiles(event.dataTransfer) ||
          composer.contains(event.target)) return;
      clearDragTarget();
      if (event.defaultPrevented ||
          (event.target instanceof Element &&
            event.target.closest('input[type="file"]'))) return;
      event.preventDefault();
      if (type === "dragover") event.dataTransfer.dropEffect = "none";
    });
  }
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
    hasUnsupportedDraft: () => ids.length > 0 && !imageCapable(),
    refresh: render,
    setWritable(value) { writable = Boolean(value); render(); },
  });
}
