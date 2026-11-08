// Per-job receipts are independent of the background relay connection worker.
export function connectorJobError(state, action) {
  const job = state?.job || {}, code = job.error ?? state.error;
  const status = job.status ?? state.status ?? 0, phase = job.transport?.phase;
  let message = "无法完成设备操作，请检查登录、设备状态和服务连接";
  if (code === "remote_account_changed") message = "账号已变化，请重新选择设备";
  else if (code === "remote_login_required" || status === 401) message = "登录状态已失效，请重新登录后连接设备";
  else if (status === 403) message = "当前账号没有执行此设备操作的权限";
  else if (status === 429) message = "设备服务请求过多，请稍后重试";
  else if (status >= 500) message = "设备服务暂时异常，请稍后重试";
  else if (!status && phase === "account") message = "登录状态暂时无法就绪，请稍后重试";
  else if (!status && phase === "transport") message = action === "refresh" || action === "list" ?
    "设备服务连接中断，有限重试后仍未恢复；已有设备列表会保留，请稍后刷新" :
    "设备服务连接中断，操作结果尚未确认；请先刷新设备状态，避免重复操作";
  const error = new Error(message);
  error.code = code; error.status = status; error.jobId = job.id; error.transport = job.transport;
  return error;
}
