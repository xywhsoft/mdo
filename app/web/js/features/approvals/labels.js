export function formatArguments(source) {
  if (!source) return "{}";
  try { return JSON.stringify(JSON.parse(source), null, 2); }
  catch { return source; }
}
