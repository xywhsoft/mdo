import { resourceId } from "../../api/client.js";
import { createSchedule, loadSchedules, readSchedule, removeSchedule,
  readScheduleHistory, replaceSchedule, runSchedule, schedulesStore,
  setScheduleEnabled } from "../../state/schedules.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";
import { currentLocale, subscribeLocale, t } from "../../i18n.js";

const FREQUENCY_KEY = {
  once: "schedule.frequency.once", minutely: "schedule.frequency.minutely",
  hourly: "schedule.frequency.hourly", daily: "schedule.frequency.daily",
  weekly: "schedule.frequency.weekly",
};
const RESULT_KEY = {
  succeeded: "schedule.result.succeeded", failed: "schedule.result.failed",
  cancelled: "schedule.result.cancelled", limit: "schedule.result.limit",
  timed_out: "schedule.result.timedOut",
};
const pad = (value) => String(value).padStart(2, "0");

function localInput(microseconds) {
  const date = new Date(Number(microseconds) / 1000);
  if (!Number.isFinite(date.getTime())) return "";
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}` +
    `T${pad(date.getHours())}:${pad(date.getMinutes())}`;
}

function clockText(microseconds) {
  if (!microseconds) return t("schedule.noNext", {}, "没有下次执行时间");
  const date = new Date(Number(microseconds) / 1000);
  return Number.isFinite(date.getTime())
    ? new Intl.DateTimeFormat(currentLocale(), { dateStyle: "medium", timeStyle: "short" }).format(date)
    : t("schedule.invalidTime", {}, "执行时间不可用");
}

function option(select, id, title) {
  select.append(element("option", { text: title, attrs: { value: id } }));
}

export function createSchedulePanel({ panel, projectsStore, agentsStore, modelsStore }) {
  const form = panel.querySelector("#schedule-form");
  const fields = form.elements;
  const list = panel.querySelector("#schedules-list");
  const status = panel.querySelector("#schedules-status");
  const actionStatus = panel.querySelector("#schedule-action-status");
  const refreshButton = panel.querySelector("#schedules-refresh");
  const newButton = panel.querySelector("#schedule-new");
  const editorDialog = panel.querySelector("#schedule-editor-dialog");
  const editorFields = panel.querySelector("#schedule-editor-fields");
  const editorClose = panel.querySelector("#schedule-editor-close");
  const editorCancel = panel.querySelector("#schedule-editor-cancel");
  const formStatus = panel.querySelector("#schedule-form-status");
  const title = panel.querySelector("#schedule-editor-title");
  const save = panel.querySelector("#schedule-save");
  const weekdayGroup = panel.querySelector("#schedule-weekdays");
  const intervalRow = panel.querySelector("#schedule-interval-row");
  const offsetRow = panel.querySelector("#schedule-offset-row");
  const deleteDialog = panel.querySelector("#schedule-delete-dialog");
  const historyDialog = panel.querySelector("#schedule-history-dialog");
  const historyTitle = panel.querySelector("#schedule-history-title");
  const historyStatus = panel.querySelector("#schedule-history-status");
  const historyRetry = panel.querySelector("#schedule-history-retry");
  const historyList = panel.querySelector("#schedule-history-list");
  let original = null;
  let etag = "";
  let busy = false;
  let loadGeneration = 0;
  let editorLoading = false;
  let editorOrigin = null;
  let deleteTarget = null;
  let historyGeneration = 0;
  let historyItem = null;
  let historyLoading = false;
  let activeRefresh = 0;
  let actionFeedback = { message: "", id: "" };

  function renderEditorHeader() {
    if (editorLoading) return;
    title.textContent = original
      ? t("schedule.editTitle", { label: original.label }, `编辑：${original.label}`)
      : t("schedule.newTitle", {}, "新建计划");
    save.textContent = original
      ? t("schedule.save", {}, "保存更改")
      : t("schedule.create", {}, "创建计划");
  }

  function setFeedback(message, error = false) {
    formStatus.textContent = message;
    formStatus.dataset.tone = error ? "error" : "neutral";
  }

  function syncEditorState() {
    editorFields.hidden = editorLoading;
    editorFields.disabled = busy || editorLoading;
    save.disabled = busy || editorLoading;
    editorClose.disabled = busy;
    editorCancel.disabled = busy;
    newButton.disabled = busy;
  }

  function showEditor(origin) {
    editorOrigin = origin;
    syncEditorState();
    editorDialog.showModal();
    editorDialog.scrollTop = 0;
    if (!editorLoading) fields.label.focus();
  }

  function setActionFeedback(message, error = false, id = "") {
    actionFeedback = { message, id };
    if (!message) {
      for (const feedback of list.querySelectorAll(".schedule-card-feedback"))
        feedback.hidden = true;
    }
    actionStatus.textContent = id ? "" : message;
    actionStatus.dataset.tone = error ? "error" : "neutral";
  }

  function visible() {
    return !document.hidden && panel.getClientRects().length > 0;
  }

  function syncConditionalFields() {
    const weekly = fields.frequency.value === "weekly";
    weekdayGroup.hidden = !weekly;
    intervalRow.hidden = fields.frequency.value === "once";
    offsetRow.hidden = fields.timezone.value !== "fixed_offset";
  }

  function fillCatalogs() {
    const choices = [
      [fields.project_id, projectsStore.get().data?.items ?? [], "tasks", t("schedule.defaultProject", {}, "任务项目")],
      [fields.agent_id, agentsStore.get().data?.items ?? [], "mdo.default", t("schedule.defaultAgent", {}, "默认 Agent")],
      [fields.model_id, modelsStore.get().data?.models ?? [], "", t("schedule.defaultModel", {}, "Agent 默认模型")],
    ];
    for (const [select, items, fallback, fallbackTitle] of choices) {
      const selected = select.options.length ? select.value : (original?.[select.name] ?? fallback);
      clear(select);
      option(select, fallback, fallbackTitle);
      for (const item of items) {
        try {
          const id = resourceId(item.id);
          if (id !== fallback) option(select, id, item.name || item.label || id);
        } catch { /* Ignore invalid catalog entries. */ }
      }
      for (const id of [selected, original?.[select.name]]) {
        if (id && ![...select.options].some((item) => item.value === id))
          option(select, id, id);
      }
      select.value = selected;
    }
  }

  function newSchedule() {
    original = null;
    etag = "";
    form.reset();
    fields.start_at.value = localInput((Date.now() + 60 * 60 * 1000) * 1000);
    fillCatalogs();
    fields.project_id.value = "tasks";
    fields.agent_id.value = "mdo.default";
    fields.model_id.value = "";
    fields.timezone.value = "system_local";
    renderEditorHeader();
    setFeedback("");
    syncConditionalFields();
  }

  function editSchedule(info, tag) {
    original = info;
    etag = tag;
    fields.label.value = info.label;
    fields.input.value = info.input;
    fields.start_at.value = localInput(info.start_at);
    fields.frequency.value = info.frequency;
    fields.interval.value = info.interval;
    fillCatalogs();
    fields.project_id.value = info.project_id;
    fields.agent_id.value = info.agent_id;
    fields.model_id.value = info.model_id;
    fields.reasoning_effort.value = info.reasoning_effort || "";
    fields.timezone.value = info.timezone;
    fields.utc_offset_seconds.value = info.utc_offset_seconds;
    fields.notify.value = info.notify || "";
    fields.misfire_policy.value = info.misfire_policy;
    fields.overlap_policy.value = info.overlap_policy;
    fields.max_catch_up.value = info.max_catch_up;
    fields.max_concurrent_runs.value = info.max_concurrent_runs;
    for (const checkbox of weekdayGroup.querySelectorAll("input"))
      checkbox.checked = Boolean(info.weekday_mask & Number(checkbox.value));
    renderEditorHeader();
    setFeedback("");
    syncConditionalFields();
    fields.label.focus({ preventScroll: true });
  }

  // Keep card nodes stable while an active run is polled. Replacing the list
  // each second discards text selection and keyboard focus inside the cards.
  function syncText(node, value) {
    if (node.textContent !== value) node.textContent = value;
  }

  function scheduleCard(id) {
    const card = element("article", { className: "schedule-card",
      attrs: { role: "group", tabindex: "-1" } });
    card.dataset.scheduleId = id;
    const heading = element("div", { className: "schedule-card-heading" });
    const running = element("span", { className: "schedule-badge" });
    running.hidden = true;
    heading.append(element("strong"), element("span", { className: "schedule-badge" }), running);
    const feedback = element("p", { className: "schedule-card-feedback",
      attrs: { role: "status" } });
    feedback.hidden = true;
    const actions = element("div", { className: "schedule-card-actions" });
    for (const [action, className] of [
      ["edit", "secondary-button"], ["enabled", "secondary-button"],
      ["run", "secondary-button"], ["history", "secondary-button"],
      ["delete", "danger-link"],
    ]) {
      const button = element("button", { className, attrs: { type: "button" } });
      button.dataset.scheduleId = id;
      button.dataset.scheduleAction = action;
      actions.append(button);
    }
    const statistics = element("dl", { className: "schedule-card-stats" });
    for (const name of ["next", "count", "last", "active"]) {
      const stat = element("div");
      stat.dataset.scheduleStat = name;
      stat.append(element("dt"), element("dd"));
      statistics.append(stat);
    }
    card.append(heading, element("p"), statistics, feedback, actions);
    return card;
  }

  function syncScheduleCard(card, item, data) {
    if (card.getAttribute("aria-label") !== item.label)
      card.setAttribute("aria-label", item.label);
    const [heading, frequency, statistics, feedback, actions] = card.children;
    const [label, enabledBadge, runningBadge] = heading.children;
    syncText(label, item.label);
    syncText(enabledBadge, item.enabled
      ? t("schedule.enabled", {}, "已启用") : t("schedule.paused", {}, "已暂停"));
    runningBadge.hidden = !item.active_runs;
    if (item.active_runs) syncText(runningBadge, t("schedule.running", {}, "运行中"));
    const unit = t(FREQUENCY_KEY[item.frequency], {}, item.frequency);
    const repeat = item.frequency === "once" ? unit : t("schedule.repeatEvery",
      { interval: item.interval, unit }, `每 ${item.interval} ${unit}`);
    syncText(frequency, `${repeat} · ${item.project_id} · ${item.agent_id}`);
    const values = [
      [t("schedule.nextRun", {}, "下次运行"), !item.enabled
        ? t("schedule.paused", {}, "已暂停") : !data.enabled
          ? t("schedule.executionOff", {}, "执行已关闭") : clockText(item.next_occurrence_at)],
      [t("schedule.triggerCount", {}, "触发次数"), String(item.claim_count ?? 0)],
      [t("schedule.lastTrigger", {}, "最近触发"), item.last_claimed_at
        ? clockText(item.last_claimed_at) : t("schedule.neverTriggered", {}, "尚未触发")],
      [t("schedule.activeRuns", {}, "正在运行"), String(item.active_runs ?? 0)],
    ];
    [...statistics.children].forEach((stat, index) => {
      syncText(stat.children[0], values[index][0]);
      syncText(stat.children[1], values[index][1]);
    });
    const message = item.id === actionFeedback.id ? actionFeedback.message : "";
    feedback.hidden = !message;
    if (message) syncText(feedback, message);
    const titles = {
      edit: t("schedule.edit", {}, "编辑"),
      enabled: item.enabled ? t("schedule.pause", {}, "暂停") : t("schedule.enable", {}, "启用"),
      run: t("schedule.run", {}, "立即运行"),
      history: t("schedule.history", {}, "历史"),
      delete: t("schedule.delete", {}, "删除"),
    };
    for (const button of actions.children) {
      const action = button.dataset.scheduleAction;
      syncText(button, titles[action]);
      const disabled = busy || (action === "run" &&
        (!data.enabled || data.persistence_fault ||
          item.active_runs >= item.max_concurrent_runs));
      if (button.disabled !== disabled) button.disabled = disabled;
      if (action === "run") button.title = disabled && !busy
        ? (!data.enabled ? t("schedule.runDisabledGlobal", {}, "请先在 Agent 设置中启用计划任务") :
          data.persistence_fault ? t("schedule.runDisabledStorage", {}, "计划任务存储不可用") :
            t("schedule.runDisabledLimit", {}, "已达到并发运行上限")) : "";
    }
  }

  function render(state) {
    if (activeRefresh) { clearTimeout(activeRefresh); activeRefresh = 0; }
    if (state.status === "idle" || state.status === "loading" ||
        (state.status === "refreshing" && typeof state.data?.enabled !== "boolean")) {
      status.textContent = t("schedule.loading", {}, "正在读取计划任务…");
      return;
    }
    const data = state.data;
    if (state.status === "error")
      status.textContent = t("schedule.loadFailed", { error: errorMessage(state.error) },
        `读取失败：${errorMessage(state.error)}`);
    if (!data) return;
    const items = data.items ?? [];
    const feedbackInCard = items.some((item) => item.id === actionFeedback.id);
    actionStatus.textContent = feedbackInCard ? "" : actionFeedback.message;
    if (state.status !== "error" && visible() && items.length)
      activeRefresh = setTimeout(() => {
        activeRefresh = 0;
        if (visible()) void loadSchedules();
      }, items.some((item) => item.active_runs > 0) ? 1000 : 15000);
    if (state.status !== "error")
      status.textContent = t("schedule.count", { count: data.total ?? items.length },
        `${data.total ?? items.length} 项计划`) +
        (!data.enabled ? t("schedule.globalDisabled", {}, " · 全局执行已关闭，可在 Agent 设置中开启") : "") +
        (data.truncated ? t("schedule.truncated", {}, " · 仅显示前 100 项") : "") +
        (data.persistence_fault ? t("schedule.storageFault", {}, " · 存储故障，请检查诊断") : "");
    const active = document.activeElement;
    const activeId = active?.dataset?.scheduleId;
    const activeAction = active?.dataset?.scheduleAction;
    const oldCards = new Map([...list.children]
      .filter((node) => node.dataset.scheduleId)
      .map((node) => [node.dataset.scheduleId, node]));
    const kept = new Set();
    if (!items.length) {
      const empty = list.querySelector(".schedule-empty") ??
        element("p", { className: "schedule-empty" });
      syncText(empty, t("schedule.empty", {}, "还没有计划任务。点击“新建计划”开始。"));
      if (list.firstChild !== empty) list.prepend(empty);
      kept.add(empty);
    }
    items.forEach((item, index) => {
      const card = oldCards.get(item.id) ?? scheduleCard(item.id);
      syncScheduleCard(card, item, data);
      if (list.children[index] !== card)
        list.insertBefore(card, list.children[index] ?? null);
      kept.add(card);
    });
    for (const child of [...list.children]) {
      if (!kept.has(child)) child.remove();
    }
    if (activeId && document.activeElement !== active)
      focusSchedule(activeId, activeAction);
  }

  function focusSchedule(id, action = "", reveal = false) {
    const card = [...list.children].find((item) => item.dataset.scheduleId === id);
    const target = action ? [...(card?.querySelectorAll("button") ?? [])].find((button) =>
      button.dataset.scheduleAction === action) : card;
    if (!target || target.disabled) return false;
    if (reveal) {
      card.scrollIntoView({ block: "start" });
      if (action) target.scrollIntoView({ block: "nearest" });
    }
    target.focus({ preventScroll: true });
    return document.activeElement === target;
  }

  function focusAfterDelete({ id, index }, succeeded) {
    if (!visible()) return;
    if (!succeeded && focusSchedule(id, "delete", true)) return;
    if (succeeded && schedulesStore.get().status === "ready") {
      const cards = [...list.children].filter((card) =>
        card.dataset.scheduleId && card.dataset.scheduleId !== id);
      const neighbor = cards[Math.min(index, cards.length - 1)];
      if (neighbor && focusSchedule(neighbor.dataset.scheduleId, "", true)) return;
    }
    refreshButton.scrollIntoView({ block: "start" });
    refreshButton.focus({ preventScroll: true });
  }

  function focusAfterListAction({ id, action }) {
    if (!visible()) return;
    if (schedulesStore.get().status === "ready" &&
        (focusSchedule(id, action, true) || focusSchedule(id, "", true))) return;
    refreshButton.scrollIntoView({ block: "start" });
    refreshButton.focus({ preventScroll: true });
  }

  function body() {
    const date = new Date(fields.start_at.value);
    const startAt = original && fields.start_at.value === localInput(original.start_at)
      ? original.start_at : date.getTime() * 1000;
    if (!Number.isSafeInteger(startAt) || startAt <= 0)
      throw new Error(t("schedule.invalidStart", {}, "请选择有效的开始时间"));
    const weekdayMask = [...weekdayGroup.querySelectorAll("input:checked")]
      .reduce((mask, box) => mask | Number(box.value), 0);
    if (fields.frequency.value === "weekly" && weekdayMask === 0)
      throw new Error(t("schedule.weekdayRequired", {}, "每周计划请至少选择一天"));
    const label = fields.label.value.trim();
    const input = fields.input.value.trim();
    const notify = fields.notify.value.trim();
    const bytes = new TextEncoder();
    if (!label || !input) throw new Error(t("schedule.required", {}, "请填写计划名称和任务内容"));
    if (bytes.encode(label).length >= 257 || bytes.encode(input).length >= 65537 ||
        bytes.encode(notify).length >= 257)
      throw new Error(t("schedule.tooLong", {}, "名称、内容或完成提示超过字节上限"));
    return {
      label, input,
      start_at: startAt, frequency: fields.frequency.value,
      interval: Number(fields.interval.value), weekday_mask: weekdayMask,
      project_id: fields.project_id.value, agent_id: fields.agent_id.value,
      model_id: fields.model_id.value, reasoning_effort: fields.reasoning_effort.value,
      timezone: fields.timezone.value,
      utc_offset_seconds: fields.timezone.value === "fixed_offset"
        ? Number(fields.utc_offset_seconds.value) : 0,
      notify, misfire_policy: fields.misfire_policy.value,
      overlap_policy: fields.overlap_policy.value,
      max_catch_up: Number(fields.max_catch_up.value),
      max_concurrent_runs: Number(fields.max_concurrent_runs.value),
      ...(original ? {
        protocol: original.protocol, max_output_tokens: original.max_output_tokens,
        workspace_root: original.workspace_root, fold_policy: original.fold_policy,
        misfire_grace_seconds: original.misfire_grace_seconds, enabled: original.enabled,
      } : {}),
    };
  }

  async function mutate(operation, success, resetEditor = false,
                        { listAction = false, deletion = null, listTarget = null } = {}) {
    if (busy) return;
    setActionFeedback("");
    busy = true;
    render(schedulesStore.get());
    if (visible() && (deletion || listTarget))
      focusSchedule((deletion || listTarget).id);
    syncEditorState();
    let savedId = "";
    let succeeded = false;
    try {
      const result = await operation();
      succeeded = true;
      toast(success);
      if (resetEditor) {
        savedId = result?.data?.id || "";
        editorOrigin = null;
        editorDialog.close();
      }
      else if (result?.data?.id === original?.id && result?.etag) {
        original = result.data;
        etag = result.etag;
      } else if (result?.data?.removed && result.data.id === original?.id)
        newSchedule();
      await loadSchedules();
    } catch (error) {
      const message = errorMessage(error);
      if (listAction) setActionFeedback(message, true, (deletion || listTarget)?.id);
      else setFeedback(message, true);
      if (!listAction) toast(message, "error");
      if (error?.status === 412) await loadSchedules();
    } finally {
      busy = false;
      syncEditorState();
      render(schedulesStore.get());
      if (savedId && visible()) {
        if (!focusSchedule(savedId, "", true)) refreshButton.focus();
      } else if (deletion) focusAfterDelete(deletion, succeeded);
      else if (listTarget) focusAfterListAction(listTarget);
    }
  }

  async function openEditor(item, origin) {
    const generation = ++loadGeneration;
    newSchedule();
    editorLoading = true;
    title.textContent = t("schedule.editTitle", { label: item.label }, `编辑：${item.label}`);
    setFeedback(t("schedule.loadingOne", {}, "正在读取计划…"));
    showEditor(origin);
    try {
      const result = await readSchedule(item.id);
      if (generation !== loadGeneration || !editorDialog.open) return;
      editorLoading = false;
      syncEditorState();
      editSchedule(result.data, result.etag);
    } catch (error) {
      if (generation !== loadGeneration || !editorDialog.open) return;
      editorDialog.close();
      setActionFeedback(errorMessage(error), true, item.id);
      render(schedulesStore.get());
    }
  }

  async function openHistory(item) {
    if (historyLoading) return;
    const generation = ++historyGeneration;
    const retrying = historyDialog.open;
    historyItem = item;
    historyLoading = true;
    historyTitle.textContent = t("schedule.historyTitle", { label: item.label },
      `${item.label} · 执行历史`);
    historyStatus.textContent = t("schedule.historyLoading", {}, "正在读取执行历史…");
    historyRetry.hidden = !retrying;
    historyRetry.setAttribute("aria-disabled", "true");
    clear(historyList);
    if (!retrying) historyDialog.showModal();
    try {
      const { data } = await readScheduleHistory(item.id);
      if (generation !== historyGeneration || !historyDialog.open) return;
      historyRetry.hidden = true;
      historyRetry.removeAttribute("aria-disabled");
      historyStatus.textContent = data.items.length
        ? t("schedule.historyCount", { count: data.items.length }, `${data.items.length} 条记录`) +
          (data.has_more ? t("schedule.historyMore", {}, " · 仅显示最近记录") : "")
        : t("schedule.historyEmpty", {}, "尚无已完成的运行。");
      for (const entry of data.items) {
        const card = element("article", { className: "schedule-history-item" });
        const header = element("header");
        const finished = clockText(entry.finished_at);
        header.append(element("strong", { text: t(RESULT_KEY[entry.result], {}, entry.result) }),
          element("time", { text: finished }));
        card.append(header,
          element("p", { text: t("schedule.historyTask", { id: entry.task_id }, `任务 #${entry.task_id}`) +
            (entry.agent_run_id ? t("schedule.historyRun", { id: entry.agent_run_id },
              ` · 运行 #${entry.agent_run_id}`) : "") }));
        if (entry.text) card.append(element("p", { text: entry.text +
          (entry.text_truncated ? "…" : "") }));
        historyList.append(card);
      }
      if (retrying) panel.querySelector("#schedule-history-close").focus({ preventScroll: true });
    } catch (error) {
      if (generation === historyGeneration && historyDialog.open) {
        historyStatus.textContent = t("schedule.loadFailed", { error: errorMessage(error) },
          `读取失败：${errorMessage(error)}`);
        historyRetry.hidden = false;
        historyRetry.removeAttribute("aria-disabled");
        historyRetry.focus({ preventScroll: true });
      }
    } finally {
      if (generation === historyGeneration) historyLoading = false;
    }
  }

  list.addEventListener("click", (event) => {
    const button = event.target.closest("button[data-schedule-action]");
    if (!button || busy) return;
    const item = schedulesStore.get().data?.items?.find((value) => value.id === button.dataset.scheduleId);
    if (!item) return;
    if (button.dataset.scheduleAction === "edit") void openEditor(item, button);
    if (button.dataset.scheduleAction === "history") void openHistory(item);
    if (button.dataset.scheduleAction === "run")
      void mutate(() => runSchedule(item.id, item.revision),
        t("schedule.started", {}, "计划已开始运行"), false,
        { listAction: true, listTarget: { id: item.id, action: "run" } });
    if (button.dataset.scheduleAction === "enabled")
      void mutate(() => setScheduleEnabled(item.id, item.revision, !item.enabled),
        item.enabled ? t("schedule.pausedSuccess", {}, "计划已暂停") :
          t("schedule.enabledSuccess", {}, "计划已启用"), false,
          { listAction: true, listTarget: { id: item.id, action: "enabled" } });
    if (button.dataset.scheduleAction === "delete") {
      deleteTarget = { item, index: schedulesStore.get().data.items.indexOf(item) };
      panel.querySelector("#schedule-delete-label").textContent = t("schedule.deleteLabel",
        { label: item.label }, `“${item.label}”将从本地计划中移除。`);
      deleteDialog.returnValue = "cancel";
      deleteDialog.showModal();
    }
  });
  deleteDialog.addEventListener("close", () => {
    const target = deleteTarget;
    deleteTarget = null;
    if (deleteDialog.returnValue === "delete" && target)
      void mutate(() => removeSchedule(target.item.id, target.item.revision),
        t("schedule.deleted", {}, "计划已删除"), false,
        { listAction: true, deletion: { id: target.item.id, index: target.index } });
  });
  historyDialog.addEventListener("close", () => {
    ++historyGeneration;
    historyItem = null;
    historyLoading = false;
    historyRetry.hidden = true;
  });
  panel.querySelector("#schedule-history-close").addEventListener("click", () => historyDialog.close());
  historyRetry.addEventListener("click", () => {
    if (historyItem && !historyLoading && historyDialog.open) void openHistory(historyItem);
  });
  form.addEventListener("submit", (event) => {
    event.preventDefault();
    if (busy || editorLoading || !editorDialog.open || !form.reportValidity()) return;
    let definition;
    try { definition = body(); }
    catch (error) { setFeedback(errorMessage(error), true); return; }
    const editing = original;
    void mutate(() => editing
      ? replaceSchedule(editing.id, etag, definition) : createSchedule(definition),
    editing ? t("schedule.updated", {}, "计划已更新") :
      t("schedule.created", {}, "计划已创建"), true);
  });
  fields.frequency.addEventListener("change", syncConditionalFields);
  fields.timezone.addEventListener("change", syncConditionalFields);
  newButton.addEventListener("click", () => {
    if (busy) return;
    ++loadGeneration;
    editorLoading = false;
    newSchedule();
    showEditor(newButton);
  });
  for (const button of [editorClose, editorCancel])
    button.addEventListener("click", () => { if (!busy) editorDialog.close(); });
  editorDialog.addEventListener("cancel", (event) => {
    if (busy) event.preventDefault();
  });
  editorDialog.addEventListener("close", () => {
    // Closing during a read invalidates its response; it must not refill a
    // different editor opened before that request finishes.
    ++loadGeneration;
    editorLoading = false;
    newSchedule();
    syncEditorState();
    if (visible() && editorOrigin?.isConnected) editorOrigin.focus({ preventScroll: true });
    editorOrigin = null;
  });
  refreshButton.addEventListener("click", () => {
    setActionFeedback("");
    void loadSchedules();
  });
  document.addEventListener("visibilitychange", () => {
    if (activeRefresh) { clearTimeout(activeRefresh); activeRefresh = 0; }
    if (visible()) void loadSchedules();
  });
  projectsStore.subscribe(fillCatalogs);
  agentsStore.subscribe(fillCatalogs);
  modelsStore.subscribe(fillCatalogs);
  schedulesStore.subscribe(render);
  subscribeLocale(() => {
    fillCatalogs();
    renderEditorHeader();
    setFeedback("");
    render(schedulesStore.get());
  });
  newSchedule();
  return Object.freeze({ refresh: loadSchedules });
}
