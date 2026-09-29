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
  let deleteTarget = null;
  let historyGeneration = 0;
  let historyItem = null;
  let historyLoading = false;
  let activeRefresh = 0;
  let actionFeedback = { message: "", id: "" };

  function renderEditorHeader() {
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

  function setActionFeedback(message, error = false, id = "") {
    actionFeedback = { message, id };
    if (!message) list.querySelector(".schedule-card-feedback")?.remove();
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
    form.scrollIntoView({ behavior: "smooth", block: "start" });
    fields.label.focus({ preventScroll: true });
  }

  function render(state) {
    if (activeRefresh) { clearTimeout(activeRefresh); activeRefresh = 0; }
    if (state.status === "loading") {
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
    if (state.status !== "error" && visible() && items.some((item) => item.active_runs > 0))
      activeRefresh = setTimeout(() => {
        activeRefresh = 0;
        if (visible()) void loadSchedules();
      }, 1000);
    if (state.status !== "error")
      status.textContent = t("schedule.count", { count: data.total ?? items.length },
        `${data.total ?? items.length} 项计划`) +
        (!data.enabled ? t("schedule.globalDisabled", {}, " · 全局执行已关闭，可在 Agent 设置中开启") : "") +
        (data.truncated ? t("schedule.truncated", {}, " · 仅显示前 100 项") : "") +
        (data.persistence_fault ? t("schedule.storageFault", {}, " · 存储故障，请检查诊断") : "");
    const active = document.activeElement;
    const activeId = active?.dataset?.scheduleId;
    const activeAction = active?.dataset?.scheduleAction;
    clear(list);
    if (!items.length) {
      list.append(element("p", { className: "schedule-empty",
        text: t("schedule.empty", {}, "还没有计划任务。填写下方表单即可创建。") }));
      return;
    }
    for (const item of items) {
      const card = element("article", { className: "schedule-card",
        attrs: { role: "group", "aria-label": item.label, tabindex: "-1" } });
      card.dataset.scheduleId = item.id;
      const heading = element("div", { className: "schedule-card-heading" });
      heading.append(element("strong", { text: item.label }),
        element("span", { className: "schedule-badge", text: item.enabled
          ? t("schedule.enabled", {}, "已启用") : t("schedule.paused", {}, "已暂停") }));
      if (item.active_runs) heading.append(element("span", { className: "schedule-badge",
        text: t("schedule.running", {}, "运行中") }));
      card.append(heading, element("p", { text: `${t(FREQUENCY_KEY[item.frequency], {}, item.frequency)} · ${item.project_id} · ${item.agent_id}` }),
        element("p", { text: t("schedule.nextTrigger", {
          next: clockText(item.next_occurrence_at), count: item.claim_count,
        }, `下次：${clockText(item.next_occurrence_at)} · 已触发 ${item.claim_count} 次`) }));
      const actions = element("div", { className: "schedule-card-actions" });
      for (const [action, text, className] of [
        ["edit", t("schedule.edit", {}, "编辑"), "secondary-button"],
        ["enabled", item.enabled ? t("schedule.pause", {}, "暂停") : t("schedule.enable", {}, "启用"), "secondary-button"],
        ["run", t("schedule.run", {}, "立即运行"), "secondary-button"],
        ["history", t("schedule.history", {}, "历史"), "secondary-button"],
        ["delete", t("schedule.delete", {}, "删除"), "danger-link"],
      ]) {
        const button = element("button", { className, text, attrs: { type: "button" } });
        button.dataset.scheduleId = item.id;
        button.dataset.scheduleAction = action;
        button.disabled = busy || (action === "run" &&
          (!data.enabled || data.persistence_fault ||
            item.active_runs >= item.max_concurrent_runs));
        if (action === "run" && button.disabled && !busy)
          button.title = !data.enabled ? t("schedule.runDisabledGlobal", {}, "请先在 Agent 设置中启用计划任务") :
            data.persistence_fault ? t("schedule.runDisabledStorage", {}, "计划任务存储不可用") :
              t("schedule.runDisabledLimit", {}, "已达到并发运行上限");
        actions.append(button);
      }
      if (actionFeedback.message && item.id === actionFeedback.id)
        card.append(element("p", { className: "schedule-card-feedback",
          text: actionFeedback.message, attrs: { role: "status" } }));
      card.append(actions);
      list.append(card);
    }
    if (activeId && activeAction)
      [...list.querySelectorAll("button")].find((button) =>
        button.dataset.scheduleId === activeId && button.dataset.scheduleAction === activeAction)?.focus();
    else if (activeId && active?.classList.contains("schedule-card"))
      [...list.children].find((card) => card.dataset.scheduleId === activeId)
        ?.focus({ preventScroll: true });
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
    save.disabled = true;
    let savedId = "";
    let succeeded = false;
    try {
      const result = await operation();
      succeeded = true;
      toast(success);
      if (resetEditor) {
        savedId = result?.data?.id || "";
        newSchedule();
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
      save.disabled = false;
      render(schedulesStore.get());
      if (savedId && visible()) {
        if (!focusSchedule(savedId, "", true)) refreshButton.focus();
      } else if (deletion) focusAfterDelete(deletion, succeeded);
      else if (listTarget) focusAfterListAction(listTarget);
    }
  }

  async function openEditor(id) {
    const generation = ++loadGeneration;
    setFeedback(t("schedule.loadingOne", {}, "正在读取计划…"));
    try {
      const result = await readSchedule(id);
      if (generation === loadGeneration) editSchedule(result.data, result.etag);
    } catch (error) {
      if (generation === loadGeneration) setFeedback(errorMessage(error), true);
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
    if (button.dataset.scheduleAction === "edit") void openEditor(item.id);
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
    if (busy || !form.reportValidity()) return;
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
  panel.querySelector("#schedule-new").addEventListener("click", () => {
    ++loadGeneration;
    newSchedule();
    fields.label.focus();
  });
  refreshButton.addEventListener("click", () => {
    setActionFeedback("");
    void loadSchedules();
  });
  document.addEventListener("visibilitychange", () => {
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
