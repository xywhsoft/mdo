const applied = new Set();
export async function applyCommandActions(receipt, handlers, isCurrent = () => true) {
  if (receipt?.state !== "succeeded" || !receipt.id || applied.has(receipt.id)) return false;
  const actions = receipt.actions || [];
  if (!Array.isArray(actions) || actions.length > 16 || actions.some(action =>
    !action || typeof action.type !== "string" || typeof handlers[action.type] !== "function"))
    throw new Error("命令返回了无效的界面操作。");
  if (!isCurrent()) return false;
  // Mark before the first effect. A handler can mutate state and then fail;
  // retrying that receipt must never duplicate a fork/export/clear operation.
  applied.add(receipt.id);
  if (applied.size > 256) applied.delete(applied.values().next().value);
  for (const action of actions) {
    if (!isCurrent()) break;
    await handlers[action.type](action.params, receipt);
  }
  return true;
}
