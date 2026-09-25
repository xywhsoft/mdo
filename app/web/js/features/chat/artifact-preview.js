import { api } from "../../api/client.js";
import { element, errorMessage } from "../../utils/dom.js";
import { t } from "../../i18n.js";

const PREVIEW_BYTES = 64 * 1024;

export function artifactPreviewNode(id, actionRef) {
  const artifactId = String(id);
  if (!/^[1-9][0-9]*$/.test(artifactId)) return null;

  const summary = element("summary", { text: t("timeline.previewArtifact", {}, "预览产物"),
    attrs: { "data-timeline-action": actionRef } });
  const status = element("p", { className: "timeline-artifact-note",
    attrs: { role: "status" } });
  const content = element("div", { className: "timeline-artifact-content" });
  const details = element("details", { className: "timeline-artifact-preview" },
    [summary, status, content]);
  let loaded = false;
  let loading = false;

  details.addEventListener("toggle", async () => {
    if (!details.open || loaded || loading) return;
    loading = true;
    status.textContent = t("task.artifact.loading", {}, "正在读取产物…");
    try {
      const { data } = await api.get(`/artifacts/${artifactId}?offset=0&limit=${PREVIEW_BYTES}`);
      const binary = window.atob(data.data || "");
      const bytes = Uint8Array.from(binary, (character) => character.charCodeAt(0));
      const mediaType = data.media_type || "application/octet-stream";
      content.replaceChildren();
      status.textContent = "";
      if (/^(text\/|application\/(json|xml|javascript))/.test(mediaType)) {
        content.append(element("pre", { text: new TextDecoder().decode(bytes) }));
      } else {
        content.append(element("p", { text: t("task.artifact.binary",
          { type: mediaType, size: `${bytes.length} B` },
          `二进制产物 ${mediaType}，已读取 ${bytes.length} B。`) }));
      }
      content.append(element("p", { className: "timeline-artifact-note",
        text: `${data.eof
          ? t("task.artifact.full", {}, "完整预览")
          : t("task.artifact.partial", {}, "仅预览前 64 KiB")} · SHA-256 ${data.sha256 || "—"}` }));
      loaded = true;
    } catch (error) {
      status.textContent = errorMessage(error);
    } finally {
      loading = false;
    }
  });
  return details;
}
