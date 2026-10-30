import { t as uiText } from "../i18n.js";
import { api, uploadExtensionChunk } from "./client.js";
import { createSha256 } from "../utils/sha256.js";

const CHUNK = 1048576;
const check = signal => { if (signal?.aborted) throw new DOMException("Cancelled", "AbortError"); };
export async function stageExtensionFile(file, { kind = "", purpose = "install", taskId = "", signal, onProgress = () => {}, onIdentity = () => {} } = {}) {
  if (!(file instanceof Blob) || !Number.isSafeInteger(file.size) || !file.size || file.size > 512 * 1024 * 1024)
    throw Error(uiText("ui.extensionArchives.fileSizeMustBeBetween1ByteAnd512", {}, "文件必须介于 1 字节和 512 MiB 之间"));
  const hash = createSha256(); let id = "";
  for (let at = 0; at < file.size; at += CHUNK) {
    check(signal); hash.update(new Uint8Array(await file.slice(at, at + CHUNK).arrayBuffer()));
    onProgress({ phase: "hashing", done: Math.min(at + CHUNK, file.size), total: file.size });
    await new Promise(resolve => setTimeout(resolve, 0));
  }
  check(signal);
  if(taskId&&!/^[A-Za-z0-9_-]{32}$/.test(taskId))throw Error(uiText("ui.extensionArchives.invalidArchiveTaskId", {}, "无效的归档任务编号"));
  const task = taskId ? (await api.get(`/extension-tasks/${taskId}`, {signal})).data : (await api.post("/extension-tasks", { kind, purpose, sha256: hash.hex(), bytes: file.size }, { signal })).data;
  if(task.kind!==kind||task.purpose!==purpose||task.sha256!==hash.hex()||task.bytes!==file.size||task.store||task.active)throw Error(uiText("ui.extensionArchives.theArchiveTaskDoesNotMatchThisFileOr", {}, "归档任务与所选文件不符，或已有操作正在使用此任务"));
  if(task.state==="ready"){onIdentity(task);return task;}
  if(task.state!=="receiving"||!Number.isSafeInteger(task.received)||task.received<0||task.received>file.size)throw Error(uiText("ui.extensionArchives.thisArchiveUploadCannotContinueCancelItAndSelect", {}, "此归档任务不能继续上传，请取消后重新选择文件"));
  id = task.id; onIdentity(task);
  try {
    for (let at = task.received; at < file.size; at += CHUNK) {
      check(signal); const bytes = new Uint8Array(await file.slice(at, at + CHUNK).arrayBuffer());
      const progress = (await uploadExtensionChunk(id, at, bytes, { signal })).data;
      if (progress.received !== at + bytes.length) throw Error(uiText("ui.extensionArchives.uploadProgressDoesNotMatchTheBytesReceived", {}, "文件上传进度与实际接收字节不一致"));
      onProgress({ phase: "uploading", done: progress.received, total: file.size });
    }
    return (await api.post(`/extension-tasks/${id}/seal`, {}, { signal })).data;
  } catch (error) {
    if (signal?.aborted) void api.delete(`/extension-tasks/${id}`).catch(() => {});
    throw error;
  }
}
