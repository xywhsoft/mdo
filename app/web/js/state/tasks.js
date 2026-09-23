import { api } from "../api/client.js";
import { createResourceStore } from "./store.js";

const OUTPUT_PAGE_BYTES = 32 * 1024;
const OUTPUT_RETAINED_BYTES = 256 * 1024;
const EVENT_RETAINED_ITEMS = 128;
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

function mergeEvents(previous, page) {
  const reset = !previous || page.history_lost;
  const items = [...(reset ? [] : previous.items), ...(page.items ?? [])];
  return {
    items: items.slice(-EVENT_RETAINED_ITEMS),
    nextRevision: Number(page.next_revision ?? 0),
    historyLost: Boolean(page.history_lost) || Boolean(!reset && previous.historyLost),
  };
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
  const after = previous?.events?.nextRevision ?? 0;
  return taskDetailStore.load(async () => {
    const [detail, output, events, artifacts] = await Promise.all([
      api.get(`/tasks/${id}`),
      api.get(`/tasks/${id}/output?stdout=${stdout}&stderr=${stderr}&result=${result}&limit=${OUTPUT_PAGE_BYTES}`),
      api.get(`/tasks/${id}/events?after=${after}&limit=64`),
      api.get("/artifacts"),
    ]);
    return {
      id,
      detail: detail.data,
      output: mergeOutput(previous?.output, output.data),
      events: mergeEvents(previous?.events, events.data),
      artifacts: (artifacts.data?.items ?? []).filter((item) => String(item.task_id) === id).reverse(),
    };
  });
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
  const response = await api.delete(`/tasks/${id}`);
  await loadTasks();
  if (selectedTaskId === id) await refreshSelectedTask();
  return response.data;
}
