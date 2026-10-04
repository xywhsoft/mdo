import { element, clear, formatRelativeTime, errorMessage, toast } from "../../utils/dom.js";
import {
  cancelTask, clearSelectedTask, loadTasks, readArtifactPreview,
  refreshSelectedTask, selectTask, selectedTask,
} from "../../state/tasks.js";
import { subscribeLocale, t } from "../../i18n.js";
import { taskOwnerLocation } from "./task-owner.js";
import { createTaskQuestions } from "./task-questions.js";

const ACTIVE_STATES = new Set(["pending", "running"]);
const STATE_LABELS = Object.freeze({
  pending: "等待中", running: "运行中", succeeded: "已完成", failed: "失败",
  cancelled: "已停止", timed_out: "已超时", lost: "已丢失",
});
const KIND_LABELS = Object.freeze({ process: "Shell", agent: "子 Agent", scheduled: "计划任务" });
function stateLabel(value) {
  return t(`task.state.${value}`, {}, STATE_LABELS[value] || value);
}

function stopping(item) {
  return ACTIVE_STATES.has(item.state) && Boolean(item.stop_requested);
}

function taskStateLabel(item) {
  return stopping(item) ? t("task.stopping", {}, "正在停止…") :
    ACTIVE_STATES.has(item.state) && Number(item.pending_questions) > 0
      ? t("task.awaitingAnswer", {}, "等待你回答") : stateLabel(item.state);
}

function kindLabel(value) {
  return t(`task.kind.${value}`, {}, KIND_LABELS[value] || value);
}

function taskName(item) {
  return item.label || t("task.fallback", { id: item.id }, `任务 #${item.id}`);
}

function formatBytes(value) {
  const bytes = Number(value);
  if (!Number.isFinite(bytes) || bytes < 0) return "—";
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(bytes < 10 * 1024 ? 1 : 0)} KiB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MiB`;
}

function detailPair(label, value) {
  return [element("dt", { text: label }), element("dd", { text: value || "—" })];
}

function captureView(container) {
  const panel = container.closest('.inspector-panel');
  return {
    panel, panelScroll: panel?.scrollTop ?? 0,
    focus: container.contains(document.activeElement)
      ? document.activeElement?.dataset?.taskFocus || "" : "",
    outputScrolls: new Map([...container.querySelectorAll("[data-task-scroll]")]
      .map((node) => [node.dataset.taskScroll, node.scrollTop])),
  };
}

function restoreView(container, view, fallback = null) {
  for (const node of container.querySelectorAll("[data-task-scroll]"))
    if (view.outputScrolls.has(node.dataset.taskScroll))
      node.scrollTop = view.outputScrolls.get(node.dataset.taskScroll);
  if (view.focus) {
    const replacement = [...container.querySelectorAll("[data-task-focus]")]
      .find((node) => node.dataset.taskFocus === view.focus);
    (replacement || fallback)?.focus({ preventScroll: true });
  }
  if (view.panel) view.panel.scrollTop = view.panelScroll;
}

export function createTaskPanel({ container, detailContainer, store, detailStore, previewStore, onChanged }) {
  let listState = store.get();
  let detailState = detailStore.get();
  let previewState = previewStore.get();
  const expandedOutputs = new Set(["result"]);
  const pendingCancels = new Set();
  let renderedTaskId = "";
  const detailHeader = element("div");
  const detailBody = element("div");
  const questions = createTaskQuestions({ onChanged: async () => {
    const states = await Promise.all([loadTasks(), refreshSelectedTask()]);
    const failed = states.find((state) => state.status === "error");
    if (failed) throw failed.error;
    onChanged?.();
  } });
  detailContainer.append(detailHeader, questions.node, detailBody);

  function closeDetail() {
    clearSelectedTask();
    renderList();
  }

  function syncCancelButtons() {
    for (const root of [container, detailContainer]) {
      for (const button of root.querySelectorAll("[data-task-cancel]")) {
        // Keep the control focusable while the request is pending; cancel() guards repeat activation.
        if (pendingCancels.has(button.dataset.taskCancel) || button.dataset.taskStopping === "true")
          button.setAttribute("aria-disabled", "true");
        else button.removeAttribute("aria-disabled");
      }
    }
  }

  async function cancel(item) {
    const id = String(item.id);
    if (pendingCancels.has(id) || stopping(item)) return;
    pendingCancels.add(id);
    syncCancelButtons();
    try {
      await cancelTask(item.id);
      toast(t("task.stopRequested", { id: item.id }, `已请求停止任务 #${item.id}`));
      onChanged?.();
    } catch (error) {
      toast(errorMessage(error), "error");
    } finally {
      pendingCancels.delete(id);
      syncCancelButtons();
    }
  }

  function outputSection(name, label, stream) {
    const notes = [];
    if (stream.dropped) notes.push(t("task.output.serverDropped", {},
      "服务端较早输出已被淘汰"));
    if (stream.clientDropped) notes.push(t("task.output.clientDropped", {},
      "界面仅保留最近 256 KiB"));
    const section = element("details", { className: "task-output-section", attrs: { open: expandedOutputs.has(name) ? "" : null } }, [
      element("summary", { attrs: { "data-task-focus": `output/${name}` } }, [
        element("span", { text: label }),
        element("span", { className: "task-output-size", text: formatBytes(stream.bytes.length) }),
      ]),
      notes.length ? element("p", { className: "task-output-note",
        text: notes.join(t("common.listSeparator", {}, "；")) }) : null,
      element("pre", { className: `task-output task-output-${name}`,
        text: stream.text || t("task.output.empty", {}, "暂无输出"),
        attrs: { "data-task-scroll": `output/${name}` } }),
    ]);
    section.addEventListener("toggle", () => {
      if (!section.isConnected) return;
      if (section.open) expandedOutputs.add(name);
      else expandedOutputs.delete(name);
    });
    return section;
  }

  function renderPreview(artifact) {
    if (previewState.status === "loading" || previewState.status === "refreshing") {
      return element("div", { className: "artifact-preview",
        text: t("task.artifact.loading", {}, "正在读取产物…") });
    }
    if (previewState.status === "error") {
      return element("div", { className: "resource-error", text: errorMessage(previewState.error) });
    }
    const preview = previewState.data;
    if (!preview || String(preview.artifact.id) !== String(artifact.id)) return null;
    const body = preview.text
      ? element("pre", { className: "task-output", text: preview.text,
        attrs: { "data-task-scroll": `artifact/${artifact.id}` } })
      : element("p", { className: "task-output-note", text: t("task.artifact.binary",
        { type: preview.mediaType, size: formatBytes(preview.bytes.length) },
        `二进制产物 ${preview.mediaType}，已读取 ${formatBytes(preview.bytes.length)}。`) });
    return element("div", { className: "artifact-preview" }, [
      body,
      element("p", { className: "task-output-note", text: `${preview.eof
        ? t("task.artifact.full", {}, "完整预览")
        : t("task.artifact.partial", {}, "仅预览前 64 KiB")} · SHA-256 ${preview.sha256 || "—"}` }),
    ]);
  }

  function renderDetail() {
    const view = captureView(detailContainer);
    const priorTaskId = renderedTaskId;
    // Polling may rebuild metadata, but must leave the active answer editor
    // mounted so selection, focus and IME composition survive unchanged reads.
    clear(detailHeader);
    clear(detailBody);
    const selected = selectedTask();
    renderedTaskId = selected;
    if (selected && priorTaskId !== selected) {
      view.focus = "";
      view.outputScrolls.clear();
    }
    detailContainer.hidden = !selected;
    const snapshot = detailState.data?.id === selected ? detailState.data : null;
    questions.sync(selected, snapshot && ACTIVE_STATES.has(snapshot.detail.state)
      && !stopping(snapshot.detail) ? snapshot.asks?.items ?? [] : [],
      detailState.status === "ready" || !selected);
    if (!selected) {
      if (view.focus && priorTaskId) container.querySelector(
        `[data-task-focus="open/${priorTaskId}"]`)?.focus({ preventScroll: true });
      if (view.panel) view.panel.scrollTop = view.panelScroll;
      return;
    }
    if (detailState.status === "error") {
      const retry = element("button", { className: "task-detail-close",
        text: t("task.detail.retry", {}, "重试读取"),
        attrs: { type: "button", "data-task-focus": "retry" } });
      retry.addEventListener("click", () => void refreshSelectedTask());
      const close = element("button", { className: "task-detail-close",
        text: t("task.backToList", {}, "返回任务列表"), attrs: {
          type: "button", "data-task-focus": "back" } });
      close.addEventListener("click", closeDetail);
      detailHeader.append(element("div", { className: "task-detail-actions" }, [close, retry]));
      detailBody.append(element("div", { className: "resource-error", text: errorMessage(detailState.error) }));
      restoreView(detailContainer, view, retry);
      return;
    }
    if (!detailState.data) {
      detailBody.append(element("div", { className: "empty-state",
        text: t("task.detail.loading", {}, "正在载入任务详情…") }));
      restoreView(detailContainer, view, container.querySelector(
        `[data-task-focus="open/${selected}"]`));
      return;
    }
    const { detail: task, output, artifacts } = detailState.data;
    const owner = taskOwnerLocation(task.owner_session);
    const close = element("button", { className: "task-detail-close",
      text: t("task.backToList", {}, "返回任务列表"), attrs: {
        type: "button", "data-task-focus": "back" } });
    close.addEventListener("click", closeDetail);
    const headerActions = [close];
    if (ACTIVE_STATES.has(task.state)) {
      const stop = element("button", { className: "task-cancel",
        text: stopping(task) ? t("task.stopping", {}, "正在停止…") : t("task.stop", {}, "停止"), attrs: { type: "button",
          "data-task-focus": "stop", "data-task-cancel": String(task.id),
          "data-task-stopping": String(stopping(task)),
          "aria-label": t("task.stopNamed", { name: task.label || task.id },
            `停止任务 ${task.label || task.id}`) } });
      stop.addEventListener("click", () => cancel(task));
      headerActions.push(stop);
    }
    const metadata = element("dl", { className: "task-detail-meta" });
    metadata.append(
      ...detailPair(t("task.meta.kind", {}, "类型"), kindLabel(task.kind)),
      ...detailPair(t("task.meta.state", {}, "状态"), taskStateLabel(task)),
      ...detailPair(t("task.meta.session", {}, "会话"), owner
        ? `${owner.projectId} / ${owner.sessionId}` : task.owner_session),
      ...detailPair(t("task.meta.started", {}, "开始"),
        formatRelativeTime(task.started_at || task.created_at)),
      ...detailPair(t("task.meta.exitCode", {}, "退出码"),
        task.exit_status_valid ? String(task.exit_code) : "—"),
    );
    const outputBody = element("div", { className: "task-output-list" });
    outputBody.append(
      outputSection("stdout", t("task.output.stdout", {}, "标准输出"), output.streams.stdout),
      outputSection("stderr", t("task.output.stderr", {}, "错误输出"), output.streams.stderr),
      outputSection("result", t("task.output.result", {}, "任务结果"), output.streams.result),
    );
    const artifactList = element("div", { className: "task-artifact-list" });
    for (const artifact of artifacts) {
      const open = element("button", { className: "task-artifact", attrs: {
        type: "button", "data-task-focus": `artifact/${artifact.id}` } }, [
        element("span", { text: artifact.path || t("task.artifact.fallback",
          { id: artifact.id }, `产物 #${artifact.id}`) }),
        element("span", { text: `${artifact.media_type || "binary"} · ${formatBytes(artifact.size_bytes)}` }),
      ]);
      open.addEventListener("click", () => void readArtifactPreview(artifact));
      artifactList.append(open);
      const preview = renderPreview(artifact);
      if (preview) artifactList.append(preview);
    }
    if (!artifacts.length) artifactList.append(element("p", { className: "task-output-note",
      text: t("task.artifact.empty", {}, "该任务没有产物。") }));
    detailHeader.append(
      element("div", { className: "task-detail-actions" }, headerActions),
      element("header", { className: "task-detail-heading" }, [
        element("h3", { text: taskName(task) }),
        element("p", { text: `${kindLabel(task.kind)} · ${taskStateLabel(task)}` }),
      ]),
    );
    detailBody.append(metadata,
      element("section", { className: "task-detail-section" }, [element("h4", {
        text: t("task.output.title", {}, "输出") }), outputBody]),
      element("section", { className: "task-detail-section" }, [element("h4", {
        text: t("task.artifact.title", { count: artifacts.length },
          `产物 ${artifacts.length}`) }), artifactList]),
    );
    syncCancelButtons();
    restoreView(detailContainer, view, close);
  }

  function renderList() {
    const view = captureView(container);
    const items = [...(listState.data?.items ?? [])].reverse();
    clear(container);
    if (listState.status === "error") {
      container.append(element("div", { className: "resource-error", text: errorMessage(listState.error) }));
      restoreView(container, view, document.querySelector("#tasks-title"));
      return;
    }
    if (!items.length) {
      container.append(element("div", { className: "empty-state", text: listState.status === "loading"
        ? t("task.list.loading", {}, "正在载入任务…")
        : t("task.list.empty", {}, "暂无后台任务") }));
      restoreView(container, view, document.querySelector("#tasks-title"));
      return;
    }
    for (const item of items.slice(0, 30)) {
      const open = element("button", { className: "task-open", attrs: {
        type: "button", "aria-expanded": String(selectedTask() === String(item.id)),
        "aria-controls": "task-detail", "data-task-focus": `open/${item.id}` } }, [
        element("span", { className: "status-dot", attrs: { "aria-hidden": "true" } }),
        element("span", { className: "task-name", text: taskName(item) }),
        element("span", { className: "task-kind", text: kindLabel(item.kind) }),
      ]);
      open.addEventListener("click", () => {
        if (selectedTask() === String(item.id)) closeDetail();
        else {
          expandedOutputs.clear();
          expandedOutputs.add("result");
          void selectTask(item.id);
          renderList();
        }
      });
      const headerChildren = [open];
      if (ACTIVE_STATES.has(item.state)) {
        const cancelButton = element("button", { className: "task-cancel",
          text: stopping(item) ? t("task.stopping", {}, "正在停止…") : t("task.stop", {}, "停止"), attrs: { type: "button",
            "data-task-focus": `cancel/${item.id}`, "data-task-cancel": String(item.id),
            "data-task-stopping": String(stopping(item)),
            "aria-label": t("task.stopNamed", { name: item.label || item.id },
              `停止任务 ${item.label || item.id}`) } });
        cancelButton.addEventListener("click", () => cancel(item));
        headerChildren.push(cancelButton);
      }
      container.append(element("article", { className: "task-card", attrs: { "data-state": item.state, "data-selected": String(selectedTask() === String(item.id)) } }, [
        element("div", { className: "task-card-header" }, headerChildren),
        element("div", { className: "task-meta" }, [
          element("span", { text: taskStateLabel(item) }),
          element("time", { className: "task-time", text: formatRelativeTime(item.started_at || item.created_at) }),
        ]),
      ]));
    }
    syncCancelButtons();
    restoreView(container, view, document.querySelector("#tasks-title"));
  }

  const unsubscribers = [
    store.subscribe((state) => { listState = state; renderList(); }),
    detailStore.subscribe((state) => { detailState = state; renderDetail(); renderList(); }),
    previewStore.subscribe((state) => { previewState = state; renderDetail(); }),
    subscribeLocale(() => { renderList(); renderDetail(); }),
  ];
  return () => unsubscribers.forEach((unsubscribe) => unsubscribe());
}
