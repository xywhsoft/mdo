// One automatic attempt per interrupted turn, even when polling produces a new
// recovery token. A user stop is excluded by the host's explicit declaration.
export function createAutomaticRecovery() {
  const attempted = new Set();
  return {
    claim(data, ownsView) {
      if (!ownsView || data?.automatic_resume !== true || data.resume_required !== true ||
          !data.project_id || !data.session_id || !data.items?.length ||
          !data.items.every(item => item.tool_available === true &&
            item.automatic_retry_safe === true && Number.isSafeInteger(item.turn))) return false;
      const turns = [...new Set(data.items.map(item => item.turn))].sort((a, b) => a - b);
      const key = JSON.stringify([data.project_id, data.session_id, turns]);
      if (attempted.has(key)) return false;
      attempted.add(key);
      return true;
    },
  };
}
