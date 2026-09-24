import { api, resourceId } from "../../api/client.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";

const dialog = document.querySelector("#memory-dialog");
const form = dialog.querySelector("#memory-form");
const list = dialog.querySelector("#memory-list");
const status = dialog.querySelector("#memory-status");
const title = dialog.querySelector("#memory-title");
const pathLabel = dialog.querySelector("#memory-path");
const discard = dialog.querySelector("#memory-discard");
const removeButton = dialog.querySelector("#memory-remove");
const deleteConfirm = dialog.querySelector("#memory-delete-confirm");
const encoder = new TextEncoder();

let collectionPath = "";
let collection = null;
let selectedId = "";
let baseline = "";
let busy = false;
let requestSerial = 0;
let returnFocus = null;

function setStatus(message, tone = "neutral") {
  status.textContent = message;
  status.dataset.tone = tone;
}

function fields() {
  return {
    id: form.elements.id.value.trim(),
    title: form.elements.title.value.trim(),
    content: form.elements.content.value,
    tags: form.elements.tags.value.split(",").map((tag) => tag.trim()).filter(Boolean),
    pinned: form.elements.pinned.checked,
  };
}

function fingerprint() { return JSON.stringify(fields()); }

function dirty() { return baseline !== "" && baseline !== fingerprint(); }

function updateDirty() { discard.hidden = !dirty(); }

function setBusy(value) {
  busy = value;
  form.querySelector('button[type="submit"]').disabled = value;
  removeButton.disabled = value;
  dialog.querySelector("#memory-new").disabled = value;
  dialog.querySelector("#memory-refresh").disabled = value;
  updateDirty();
}

function fill(entry = null) {
  selectedId = entry?.id ?? "";
  form.elements.id.value = entry?.id ?? "";
  form.elements.id.readOnly = Boolean(entry);
  form.elements.title.value = entry?.title ?? "";
  form.elements.content.value = entry?.content ?? "";
  form.elements.tags.value = entry?.tags?.join(", ") ?? "";
  form.elements.pinned.checked = Boolean(entry?.pinned);
  removeButton.hidden = !entry;
  deleteConfirm.hidden = true;
  baseline = fingerprint();
  updateDirty();
  for (const button of list.querySelectorAll("button[data-memory-id]"))
    button.setAttribute("aria-current", button.dataset.memoryId === selectedId ? "true" : "false");
}

function renderList() {
  clear(list);
  const items = collection?.data?.items ?? [];
  if (!items.length) {
    list.append(element("p", { className: "empty-state", text: "还没有记忆。" }));
    return;
  }
  for (const item of items) {
    const button = element("button", { className: "memory-list-item",
      attrs: { type: "button", "data-memory-id": item.id,
        "aria-current": item.id === selectedId ? "true" : "false" } });
    button.append(element("strong", { text: `${item.pinned ? "📌 " : ""}${item.title}` }),
      element("small", { text: item.tags?.join(" · ") || item.id }));
    button.addEventListener("click", () => { void select(item.id); });
    list.append(button);
  }
}

async function refresh({ discardDraft = false, selectId = selectedId } = {}) {
  if (busy || (!discardDraft && dirty())) {
    setStatus("有未保存的修改。保存后再刷新，或点击“刷新”放弃当前修改。", "error");
    return;
  }
  const serial = ++requestSerial;
  setBusy(true);
  setStatus("正在读取记忆…");
  try {
    const next = await api.get(collectionPath);
    if (serial !== requestSerial) return;
    collection = next;
    selectedId = "";
    renderList();
    if (selectId && next.data.items.some((item) => item.id === selectId)) {
      const entry = await api.get(`${collectionPath}/${resourceId(selectId, "memory")}`);
      if (serial !== requestSerial) return;
      collection.etag = entry.etag;
      fill(entry.data);
    } else fill();
    setStatus(`${next.data.items.length} 条记忆 · revision ${next.data.revision}`);
  } catch (cause) {
    if (serial === requestSerial) setStatus(errorMessage(cause), "error");
  } finally {
    if (serial === requestSerial) setBusy(false);
  }
}

async function select(id) {
  if (busy) return;
  if (dirty()) {
    setStatus("请先保存当前修改，或点击“刷新”放弃修改。", "error");
    return;
  }
  const serial = ++requestSerial;
  setBusy(true);
  try {
    const result = await api.get(`${collectionPath}/${resourceId(id, "memory")}`);
    if (serial !== requestSerial) return;
    collection.etag = result.etag;
    fill(result.data);
    setStatus(`正在编辑“${result.data.title}”`);
    form.elements.title.focus();
  } catch (cause) {
    if (serial === requestSerial) setStatus(errorMessage(cause), "error");
  } finally {
    if (serial === requestSerial) setBusy(false);
  }
}

function validate(input) {
  if (!form.reportValidity()) return false;
  if (encoder.encode(input.content).length > 16 * 1024) {
    setStatus("内容最多 16 KiB（按 UTF-8 字节计算）。", "error");
    return false;
  }
  if (input.tags.length > 16 || new Set(input.tags).size !== input.tags.length ||
      input.tags.some((tag) => encoder.encode(tag).length > 64)) {
    setStatus("标签最多 16 个，每个不超过 64 字节，不能重复。", "error");
    return false;
  }
  return true;
}

function close() {
  if (dirty()) {
    setStatus("有未保存的修改。请保存，或点击“放弃并关闭”。", "error");
    return;
  }
  dialog.close();
}

dialog.querySelector("#memory-close").addEventListener("click", close);
discard.addEventListener("click", () => { baseline = fingerprint(); dialog.close(); });
dialog.addEventListener("cancel", (event) => {
  if (!dirty()) return;
  event.preventDefault();
  setStatus("有未保存的修改。请保存，或点击“放弃并关闭”。", "error");
});
dialog.addEventListener("close", () => {
  ++requestSerial;
  returnFocus?.focus();
  returnFocus = null;
});
form.addEventListener("input", updateDirty);
form.addEventListener("change", updateDirty);
dialog.querySelector("#memory-new").addEventListener("click", () => {
  if (dirty()) { setStatus("请先保存当前修改，或点击“刷新”放弃修改。", "error"); return; }
  fill();
  setStatus("新建记忆；标识保存后不可修改。");
  form.elements.id.focus();
});
dialog.querySelector("#memory-refresh").addEventListener("click", () => {
  void refresh({ discardDraft: true });
});
form.addEventListener("submit", async (event) => {
  event.preventDefault();
  if (busy || !collection?.etag) return;
  const input = fields();
  if (!validate(input)) return;
  setBusy(true);
  try {
    await api.put(collectionPath, input, { ifMatch: collection.etag });
    baseline = fingerprint();
    setBusy(false);
    await refresh({ discardDraft: true, selectId: input.id });
    toast("记忆已保存");
  } catch (cause) {
    setStatus(errorMessage(cause), "error");
  } finally { setBusy(false); }
});
removeButton.addEventListener("click", () => { deleteConfirm.hidden = false; });
dialog.querySelector("#memory-delete-cancel").addEventListener("click", () => {
  deleteConfirm.hidden = true;
  removeButton.focus();
});
dialog.querySelector("#memory-delete-apply").addEventListener("click", async () => {
  if (busy || !selectedId || !collection?.etag) return;
  if (dirty()) { setStatus("先保存或刷新当前修改，再删除。", "error"); return; }
  setBusy(true);
  try {
    await api.delete(`${collectionPath}/${resourceId(selectedId, "memory")}`,
      { ifMatch: collection.etag });
    fill();
    setBusy(false);
    await refresh({ discardDraft: true, selectId: "" });
    toast("记忆已删除");
  } catch (cause) {
    setStatus(errorMessage(cause), "error");
  } finally { setBusy(false); }
});

export function openMemoryPanel(project = null) {
  if (dialog.open) return;
  collectionPath = project ? `/memory/projects/${resourceId(project.id, "project")}`
    : "/memory/global";
  collection = null;
  selectedId = "";
  baseline = "";
  returnFocus = document.activeElement;
  title.textContent = project ? `${project.name || project.id} · 项目记忆` : "全局记忆";
  pathLabel.textContent = project ? `Home/memory/projects/${project.id}.json`
    : "Home/memory/global.json";
  fill();
  clear(list);
  dialog.showModal();
  void refresh({ discardDraft: true, selectId: "" });
}
