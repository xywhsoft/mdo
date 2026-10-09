// One attempt per interrupted turn in this history generation. Editing history
// can reuse a turn number; append and polling must not reset the attempt limit.
// The host supplies only epochs that remain stable on append. Stops stay manual.
export function createAutomaticRecovery() {
  const attempted = new Set();
  return {
    claim(data, ownsView) {
      if (!ownsView || data?.automatic_resume !== true || data.resume_required !== true ||
          !data.project_id || !data.session_id || !data.items?.length ||
          !/^[0-9a-f]{64}$/.test(data.history_epoch || "") ||
          !data.items.every(item => item.tool_available === true &&
            item.automatic_retry_safe === true && Number.isSafeInteger(item.turn) && item.turn > 0)) return false;
      const turns = [...new Set(data.items.map(item => item.turn))].sort((a, b) => a - b);
      const key = JSON.stringify([data.project_id, data.session_id, data.history_epoch, turns]);
      if (attempted.has(key)) return false;
      attempted.add(key);
      return true;
    },
  };
}
