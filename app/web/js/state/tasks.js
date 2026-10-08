import { api } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { validateAskAnswer } from "./asks.js";
import { t } from "../i18n.js";
import { runTaskStopAction } from "./task-stop-action.js";

const OUTPUT_PAGE_BYTES = 32 * 1024;
const OUTPUT_RETAINED_BYTES = 256 * 1024;
const ARTIFACT_PREVIEW_BYTES = 64 * 1024;
const STREAM_NAMES = Object.freeze(["stdout", "stderr", "result"]);

export const tasksStore = createResourceStore({ total: 0, items: [] });
export const taskDetailStore = createResourceStore();
export const artifactPreviewStore = createResourceStore();

let selectedTaskId = "";

function taskId(value) {
  const id = String(value ?? "");
  if (!/^[1-9][0-9]*$/.test(id)) throw new TypeError("task ID is invalid");
  return id;
}

function decodeBase64(value) {
  const binary = window.atob(value || "");
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index += 1) bytes[index] = binary.charCodeAt(index);
  return bytes;
}

function appendBytes(previous, chunk) {
  const incoming = decodeBase64(chunk.data);
  const contiguous = previous && !chunk.dropped && Number(chunk.start) === previous.next;
  const base = contiguous ? previous.bytes : new Uint8Array();
  let bytes = new Uint8Array(base.length + incoming.length);
  bytes.set(base);
  bytes.set(incoming, base.length);
  let clientDropped = previous?.clientDropped ?? false;
  if (bytes.length > OUTPUT_RETAINED_BYTES) {
    bytes = bytes.slice(bytes.length - OUTPUT_RETAINED_BYTES);
    clientDropped = true;
  }
  return {
    bytes,
    text: new TextDecoder().decode(bytes),
    start: Number(chunk.start),
    next: Number(chunk.next),
    dropped: Boolean(chunk.dropped) || Boolean(previous?.dropped) || Boolean(previous && !contiguous),
    clientDropped,
  };
}

function mergeOutput(previous, page) {
  const streams = {};
  for (const name of STREAM_NAMES) streams[name] = appendBytes(previous?.streams?.[name], page[name]);
  return { complete: Boolean(page.complete), streams };
}

export function loadTasks() {
  return tasksStore.load(async () => (await api.get("/tasks")).data);
}

export function selectedTask() {
  return selectedTaskId;
}

export function clearSelectedTask() {
  selectedTaskId = "";
  taskDetailStore.reset();
  artifactPreviewStore.reset();
}

export async function selectTask(value) {
  const id = taskId(value);
  if (selectedTaskId !== id) {
    selectedTaskId = id;
    taskDetailStore.reset();
    artifactPreviewStore.reset();
  }
  return refreshSelectedTask();
}

export function refreshSelectedTask() {
  if (!selectedTaskId) return Promise.resolve(taskDetailStore.get());
  const id = selectedTaskId;
  const previous = taskDetailStore.get().data?.id === id ? taskDetailStore.get().data : null;
  const stdout = previous?.output?.streams?.stdout?.next ?? 0;
  const stderr = previous?.output?.streams?.stderr?.next ?? 0;
  const result = previous?.output?.streams?.result?.next ?? 0;
  return taskDetailStore.load(async () => {
    const [detail, output, artifacts, asks] = await Promise.all([
      api.get(`/tasks/${id}`),
      api.get(`/tasks/${id}/output?stdout=${stdout}&stderr=${stderr}&result=${result}&limit=${OUTPUT_PAGE_BYTES}`),
      api.get("/artifacts"),
      api.get(`/tasks/${id}/asks`),
    ]);
    if (!Array.isArray(asks.data?.items))
      throw new TypeError(t("ask.invalidResponse", {}, "询问响应无效"));
    return {
      id,
      detail: detail.data,
      asks: asks.data,
      output: mergeOutput(previous?.output, output.data),
      artifacts: (artifacts.data?.items ?? []).filter((item) => String(item.task_id) === id).reverse(),
    };
  });
}

export async function answerTaskAsk(value, askId, answer) {
  const id = taskId(value);
  const validated = validateAskAnswer(askId, answer);
  // Return the acknowledgement before reading again so the card can lock an
  // accepted answer even when the following snapshot cannot be retrieved.
  return (await api.put(`/tasks/${id}/asks/${validated.id}`,
    { answer: validated.answer })).data;
}

export function readArtifactPreview(artifact) {
  const id = taskId(artifact?.id);
  const ownerTask = taskId(artifact?.task_id);
  if (!selectedTaskId || ownerTask !== selectedTaskId) throw new TypeError("artifact does not belong to the selected task");
  artifactPreviewStore.reset();
  return artifactPreviewStore.load(async () => {
    const response = await api.get(`/artifacts/${id}?offset=0&limit=${ARTIFACT_PREVIEW_BYTES}`);
    const chunk = response.data;
    const bytes = decodeBase64(chunk.data);
    const textual = /^(text\/|application\/(json|xml|javascript))/.test(chunk.media_type || "");
    return {
      artifact,
      bytes,
      text: textual ? new TextDecoder().decode(bytes) : "",
      eof: Boolean(chunk.eof),
      totalSize: Number(chunk.total_size),
      mediaType: chunk.media_type || artifact.media_type || "application/octet-stream",
      sha256: chunk.sha256 || artifact.sha256 || "",
    };
  });
}

export async function cancelTask(value) {
  const id = taskId(value);
  const known = tasksStore.get().data?.items?.find(item => String(item.id) === id) ??
    (taskDetailStore.get().data?.id === id ? taskDetailStore.get().data.detail : null);
  const stopped = await runTaskStopAction({ id, task: known });
  // The acknowledged snapshot remains authoritative even if the subsequent
  // read fails. Do not make an accepted stop look available for resubmission.
  const list = tasksStore.get().data;
  if (list?.items) tasksStore.setData({ ...list,
    items: list.items.map((item) => String(item.id) === id ? stopped : item) });
  const detail = taskDetailStore.get().data;
  if (detail?.id === id) taskDetailStore.setData({ ...detail, detail: stopped });
  await loadTasks();
  if (selectedTaskId === id) await refreshSelectedTask();
  return stopped;
}
