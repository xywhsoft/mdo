import { apiRequest } from "../../api/client.js";
import { targetState, subscribeTarget, requestTargetSwitch } from "../../api/target.js";
import { element, clear, errorMessage } from "../../utils/dom.js";
import { t } from "../../i18n.js";

const root = "/device-transfers";
const id = () => [...crypto.getRandomValues(new Uint8Array(16))].map(x => x.toString(16).padStart(2, "0")).join("");
const terminal = state => ["completed", "cancelled"].includes(state);
const sourceKey = () => `${targetState().selected?.owner ?? "local"}/${targetState().selected?.id ?? "local"}`;
const text = (key, fallback) => t(`transfer.${key}`, {}, fallback);

export function createDeviceTransferPanel({ beforeStart = async () => true } = {}) {
  const heading = element("h2", { text: text("title", "发送到设备") });
  const description = element("p", { text: text("description", "源数据保留，目标设备创建独立副本。关闭此窗口后，后台继续传输。") });
  const status = element("p", { attrs: { role: "status", "aria-live": "polite" } });
  const error = element("p", { className: "resource-error", attrs: { role: "alert", hidden: true } });
  const devices = element("select", { attrs: { "aria-label": text("device", "目标设备") } });
  const projects = element("select", { attrs: { "aria-label": text("project", "目标项目") } });
  const directory = element("input", { attrs: { type: "text", placeholder: text("directoryDefault", "留空：目标设备默认项目目录"),
    "aria-label": text("directory", "目标设备保存目录") } });
  const prepare = element("button", { text: text("prepare", "选择设备"), attrs: { type: "button" } });
  const confirm = element("button", { className: "primary-button", text: text("start", "开始传输"), attrs: { type: "button", hidden: true } });
  const resume = element("button", { text: text("resume", "重试并继续"), attrs: { type: "button", hidden: true } });
  const cancel = element("button", { text: text("cancel", "取消传输"), attrs: { type: "button", hidden: true } });
  const openTarget = element("button", { text: text("open", "在目标设备打开"), attrs: { type: "button", hidden: true } });
  const refresh = element("button", { text: text("refresh", "刷新设备"), attrs: { type: "button" } });
  const tasks = element("div", { className: "transfer-tasks" });
  const close = element("button", { text: text("close", "关闭"), attrs: { type: "button" } });
  const newTransfer = element("div", { className: "transfer-new" }, [devices, prepare, refresh]);
  const destination = element("div", { className: "transfer-destination", attrs: { hidden: true } }, [projects, directory, confirm]);
  const dialog = element("dialog", { className: "dialog device-transfer-dialog", attrs: { "aria-label": text("title", "发送到设备") } },
    [heading, description, newTransfer, destination, status, error,
      element("div", { className: "dialog-actions" }, [resume, cancel, openTarget, close]), tasks]);
  document.body.append(dialog);
  let selection = null, job = null, requestId = "", timer = 0, controller = null, origin = null;
  let key = "", busy = false, deviceItems = [], epoch = 0;
  let projectOptionsKey = "";
  const writable = () => !targetState().selected || (targetState().selected.mode === "control" && targetState().connected && !targetState().runtimeChanged);
  function renderControls() {
    const canPrepare = Boolean(selection?.project_id);
    newTransfer.hidden = !canPrepare && !(job && !job.incoming && job.state === "paused");
    prepare.hidden = !canPrepare;
    prepare.disabled = busy || !writable() || !devices.value || Boolean(job && !terminal(job.state));
    confirm.disabled = resume.disabled = cancel.disabled = busy || !writable();
  }
  function stopReads() { clearTimeout(timer); controller?.abort(); controller = null; epoch++; }
  async function call(path, options = {}) {
    if (key !== sourceKey()) throw new DOMException("Device changed", "AbortError");
    const generation = epoch;
    const result = await apiRequest(path, { ...options, signal: controller?.signal });
    if (generation !== epoch || key !== sourceKey()) throw new DOMException("Device changed", "AbortError");
    return result.data;
  }
  function failure(cause) {
    if (cause?.name === "AbortError") return;
    error.hidden = false; error.textContent = errorMessage(cause);
  }
  function renderJob(value) {
    job = value;
    const state = value.state;
    status.textContent = t(`transfer.state.${state}`, {}, state);
    if (value.current_file) status.textContent += ` · ${value.current_file}`;
    if (value.current_file_total) status.textContent += ` · ${Math.min(100, Math.floor((value.current_file_bytes ?? 0) * 100 / value.current_file_total))}%`;
    if (value.route) status.textContent += ` · ${value.route === "lan" ? text("lan", "局域网直连") : text("relay", "服务器中转")}`;
    error.hidden = !value.error; error.textContent = value.error || "";
    const review = state === "awaiting_confirmation";
    destination.hidden = !review; confirm.hidden = !review;
    projects.hidden = selection?.kind === "project";
    directory.hidden = selection?.kind !== "project";
    const optionsKey = JSON.stringify(value.target_projects ?? []);
    if (review && projectOptionsKey !== optionsKey) {
      const previous = projects.value; clear(projects); projectOptionsKey = optionsKey;
      projects.append(element("option", { text: text("defaultProject", "默认项目"), attrs: { value: "default" } }));
      for (const project of value.target_projects ?? []) if (project.id !== "default")
        projects.append(element("option", { text: project.name || project.id, attrs: { value: project.id } }));
      if ([...projects.options].some(option => option.value === previous)) projects.value = previous;
    }
    cancel.hidden = terminal(state) || Boolean(value.commit_requested || value.commit_started);
    resume.hidden = state !== "paused" && !(value.queued === false && !terminal(state));
    openTarget.hidden = state !== "completed" || !value.destination_project_id || value.incoming;
    renderControls();
  }
  async function poll() {
    clearTimeout(timer);
    if (!dialog.open || !job) return;
    try {
      renderJob(await call(`${root}/${job.incoming ? "incoming/" : ""}${job.id}`));
      if (!terminal(job.state)) timer = setTimeout(poll, job.state === "awaiting_confirmation" ? 3000 : 1000);
      else await loadTasks();
    } catch (cause) {
      failure(cause);
      if (dialog.open && cause?.name !== "AbortError") timer = setTimeout(poll, 3000);
    }
  }
  async function loadDevices(refreshList = false) {
    if (refreshList) await call(`${root}/devices`, { method: "POST", body: {} });
    const value = await call(`${root}/devices`);
    deviceItems = (value.listing?.devices ?? []).filter(device => device.id !== value.device_id);
    const previous = devices.value || job?.device_id;
    clear(devices);
    for (const device of deviceItems) devices.append(element("option", {
      text: `${device.name || device.id}${device.online ? "" : ` · ${text("offline", "离线")}`}`, attrs: { value: device.id },
    }));
    if (previous) devices.value = deviceItems.some(device => device.id === previous) ? previous : "";
    renderControls();
    if (!deviceItems.length) status.textContent = text("noDevices", "未发现其他设备；请在同账号的目标设备上开启远控和允许接收。");
  }
  async function loadTasks() {
    const value = await call(root); clear(tasks);
    const items = [...(value.outgoing ?? []), ...(value.incoming ?? [])].sort((a, b) => Number(terminal(a.state)) - Number(terminal(b.state)) || b.updated_at - a.updated_at).slice(0, 64);
    for (const item of items) {
      const button = element("button", { text: `${item.incoming ? text("receiving", "接收") : text("sending", "发送")} · ${item.title || item.project_id || item.destination_project_id} · ${t(`transfer.state.${item.state}`, {}, item.state)}`,
        attrs: { type: "button" } });
      button.addEventListener("click", () => {
        if (busy) return;
        stopReads(); controller = new AbortController(); selection = { kind: item.kind }; projectOptionsKey = ""; clear(projects); renderJob(item);
        if (!item.incoming && deviceItems.some(device => device.id === item.device_id)) devices.value = item.device_id;
        void poll();
      });
      tasks.append(button);
    }
  }
  async function action(name) {
    if (busy || !job || !writable()) return;
    busy = true; error.hidden = true; renderControls();
    const generation = epoch;
    try {
      const body = { action: name, ...(name === "confirm" ? { target_project_id: projects.value || "default", target_directory: directory.value.trim() } : {}),
        ...(name === "resume" && !job.incoming && devices.value ? { device_id: devices.value } : {}) };
      renderJob(await call(job.incoming ? `${root}/incoming/${job.id}/${name}` : `${root}/${job.id}`, { method: "POST", body }));
      void poll();
    } catch (cause) { failure(cause); }
    finally { if (generation === epoch) { busy = false; renderControls(); } }
  }
  prepare.addEventListener("click", async () => {
    if (busy || !selection?.project_id || !devices.value || !writable()) return;
    if (job && terminal(job.state)) { job = null; requestId = ""; }
    busy = true; prepare.disabled = true; error.hidden = true;
    const generation = epoch;
    requestId ||= id();
    try {
      if (!await beforeStart(selection)) throw new Error(text("draftPending", "草稿尚未保存，请先保存或复制后再传输。"));
      if (generation !== epoch) throw new DOMException("Transfer selection changed", "AbortError");
      renderJob(await call(root, { method: "POST", body: { id: requestId, ...selection, device_id: devices.value,
        device_name: deviceItems.find(item => item.id === devices.value)?.name || text("device", "目标设备") } }));
      void poll();
    } catch (cause) { failure(cause); }
    finally { if (generation === epoch) { busy = false; renderControls(); } }
  });
  refresh.addEventListener("click", () => { void loadDevices(true).catch(failure); setTimeout(() => { if (dialog.open) void loadDevices().catch(failure); }, 1500); });
  confirm.addEventListener("click", () => { confirm.disabled = true; void action("confirm"); });
  resume.addEventListener("click", () => void action("resume")); cancel.addEventListener("click", () => void action("cancel"));
  close.addEventListener("click", () => dialog.close());
  dialog.addEventListener("close", () => { stopReads(); if (origin?.isConnected) origin.focus(); });
  openTarget.addEventListener("click", () => {
    if (!job?.destination_project_id) return;
    const route = job.kind === "session" ? `#/projects/${encodeURIComponent(job.destination_project_id)}/sessions/${job.destination_session_id}` :
      `#/projects/${encodeURIComponent(job.destination_project_id)}/new`;
    void requestTargetSwitch({ id: job.device_id, owner: String(job.member_id), name: job.device_name || text("device", "目标设备"), mode: "control" }, { route }).catch(failure);
  });
  subscribeTarget(() => { if (dialog.open && (key !== sourceKey() || targetState().runtimeChanged)) dialog.close(); });
  function open(resource = null, button = document.activeElement) {
    stopReads(); key = sourceKey(); controller = new AbortController(); origin = button;
    selection = resource; job = null; requestId = ""; busy = false; projectOptionsKey = ""; clear(projects); directory.value = "";
    destination.hidden = true; error.hidden = true; status.textContent = text("loading", "正在读取设备和传输任务…");
    resume.hidden = cancel.hidden = openTarget.hidden = true; renderControls();
    heading.textContent = resource?.kind === "project" ? text("sendProject", "发送完整项目到设备") : text("title", "发送到设备");
    dialog.showModal(); close.focus();
    void Promise.all([loadDevices(true), loadTasks()]).then(() => {
      setTimeout(() => { if (dialog.open) void loadDevices().catch(failure); }, 1200);
    }).catch(failure);
  }
  return { openSession: session => open({ kind: "session", project_id: session.project_id, session_id: session.id, title: session.title }),
    openProject: (project, button) => open({ kind: "project", project_id: project.id, title: project.name || project.id }, button),
    openTasks: () => open() };
}
