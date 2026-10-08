import { errorMessage } from "../../utils/dom.js";
import { t } from "../../i18n.js";
import { isTransientReadError } from "../../api/read-recovery.js";

// The questions API uses the same code for reading and answering. This read
// surface must not imply that an answer was submitted or lost.
export function taskReadErrorMessage(error) {
  if (error?.code === "asks_unavailable")
    return t("task.questionsUnavailable", {}, "暂时无法读取任务询问，请稍后重试。");
  if (isTransientReadError(error) && !["task_unavailable", "tasks_unavailable",
    "task_output_unavailable", "runtime_unavailable"].includes(error?.code))
    return t("task.readUnavailable", {}, "暂时无法读取任务信息，请检查连接后重试。");
  return errorMessage(error);
}
