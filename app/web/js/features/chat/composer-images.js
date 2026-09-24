import { api, attachmentUrl } from "../../api/client.js";
import { clear, element } from "../../utils/dom.js";

const TYPES = new Set(["image/png", "image/jpeg", "image/webp"]);
const MAX_IMAGE_BYTES = 8 * 1024 * 1024;

export function createComposerImages({ composer, prompt, button, input, strip,
  navigation, modelsStore, sessionStore, ensureSession, onChange, onRemove,
  onUploading, onError }) {
  let ids = [];
  let uploading = false;
  let writable = true;

  function owner() {
    const { projectId, sessionId } = navigation.get();
    return projectId && sessionId ? { projectId, sessionId } : null;
  }

  function render() {
    clear(strip);
    const selected = owner();
    strip.hidden = ids.length === 0 && !uploading;
    if (selected) for (const [index, id] of ids.entries()) {
      if (!/^[0-9a-f]{32}$/.test(id)) continue;
      const remove = element("button", {
        text: "×", attrs: { type: "button", "aria-label": `移除图片 ${index + 1}` },
      });
      remove.disabled = uploading || !writable;
      remove.addEventListener("click", () => {
        ids = ids.filter((_, position) => position !== index);
        render();
        onChange([...ids]);
        void Promise.resolve(onRemove?.(selected, id)).catch(onError);
      });
      strip.append(element("div", { className: "composer-image" }, [
        element("img", { attrs: { src: attachmentUrl(selected.projectId,
          selected.sessionId, id), alt: `图片 ${index + 1}` } }),
        remove,
      ]));
    }
    if (uploading) strip.append(element("span", {
      className: "composer-image-uploading", text: "正在保存图片…",
    }));
    button.disabled = !writable || uploading;
  }

  function imageCapable() {
    const modelId = sessionStore.get().data?.model_id ||
      document.querySelector("#composer-model")?.value;
    const model = modelsStore.get().data?.models?.find((item) =>
      item.id === modelId);
    return Boolean(Number(model?.attachments ?? 0) & 1);
  }

  async function addFiles(files) {
    const images = [...files];
    if (!images.length || uploading) return;
    if (!writable) { onError(new Error("当前会话不可添加图片")); return; }
    if (!imageCapable()) {
      onError(new Error("当前模型不支持图片，请先切换到支持图片的模型"));
      return;
    }
    if (images.length + ids.length > 4) {
      onError(new Error("每条消息最多可添加 4 张图片"));
      return;
    }
    for (const file of images) {
      if (!TYPES.has(file.type)) {
        onError(new Error("仅支持 PNG、JPEG 和 WebP 图片"));
        return;
      }
      if (file.size === 0 || file.size > MAX_IMAGE_BYTES) {
        onError(new Error("单张图片不得超过 8 MiB"));
        return;
      }
    }
    uploading = true;
    onUploading(true);
    render();
    try {
      const selected = owner() ?? await ensureSession(prompt.value);
      const key = `${selected.projectId}/${selected.sessionId}`;
      for (const file of images) {
        const stored = await api.uploadImage(selected.projectId,
          selected.sessionId, file);
        const current = owner();
        if (`${current?.projectId}/${current?.sessionId}` !== key) {
          void api.deleteImage(selected.projectId, selected.sessionId,
            stored.id).catch(() => {});
          throw new Error("会话已切换，图片未加入当前草稿");
        }
        ids = [...ids, stored.id];
        onChange([...ids]);
        render();
      }
    } catch (error) { onError(error); }
    finally {
      uploading = false;
      onUploading(false);
      input.value = "";
      render();
    }
  }

  button.addEventListener("click", () => {
    if (!imageCapable()) {
      onError(new Error("当前模型不支持图片，请先切换到支持图片的模型"));
      return;
    }
    input.click();
  });
  input.addEventListener("change", () => { void addFiles(input.files ?? []); });
  prompt.addEventListener("paste", (event) => {
    const images = [...(event.clipboardData?.files ?? [])].filter((file) =>
      file.type.startsWith("image/"));
    if (!images.length) return;
    event.preventDefault();
    void addFiles(images);
  });
  composer.addEventListener("dragover", (event) => {
    if (![...(event.dataTransfer?.types ?? [])].includes("Files")) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = "copy";
  });
  composer.addEventListener("drop", (event) => {
    if (![...(event.dataTransfer?.types ?? [])].includes("Files")) return;
    event.preventDefault();
    void addFiles(event.dataTransfer?.files ?? []);
  });

  render();
  return Object.freeze({
    get: () => [...ids],
    set(value) { ids = Array.isArray(value) ? [...value] : []; render(); },
    clear() { ids = []; render(); },
    isUploading: () => uploading,
    setWritable(value) { writable = Boolean(value); render(); },
  });
}
