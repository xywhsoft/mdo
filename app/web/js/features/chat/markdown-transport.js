// Parsing and desktop URL opening belong to the displaying host, even while
// conversation resources address a remote target. Never reuse its write token.
let localToken = "";
export async function callDisplayService(method, params, { signal } = {}) {
  for (let attempt = 0; attempt < 2; attempt++) {
    if (!localToken) {
      const bootstrap = await fetch("/api/v1/bootstrap", { signal, cache: "no-store" });
      localToken = bootstrap.headers.get("X-Mdo-Write-Token") || "";
      if (!bootstrap.ok || !localToken) throw new Error("Display service is unavailable");
    }
    const response = await fetch("/api/v1/application/call", { method: "POST", signal,
      headers: { "Content-Type": "application/json", "X-Mdo-Write-Token": localToken },
      body: JSON.stringify({ method, params, context: {} }) });
    const body = await response.json();
    if (response.ok && body.ok) return body.data;
    if (attempt === 0 && body.error?.code === "write_token_conflict") { localToken = ""; continue; }
    throw Object.assign(new Error(body.error?.message || "Markdown service failed"), { code: body.error?.code });
  }
}
export const parseMarkdown = (source, options) => callDisplayService("markdown.parse", { source }, options);
