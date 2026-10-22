import { targetState } from "../../api/target.js";
import { api } from "../../api/client.js";

export async function mcpContextRead(server, method, params = {}, { signal, isCurrent = () => true, onProgress } = {}) {
  const owner = targetState().selected?.id || "local";
  const { data } = await api.post(`/mcp/${encodeURIComponent(server)}/context`, { method, params }, { signal });
  let operation = data, progressAt = 0;
  try {
    while (!operation.terminal) {
      if (!isCurrent() || signal?.aborted) throw new DOMException("Context changed", "AbortError");
      await new Promise(resolve => setTimeout(resolve, 150));
      operation = (await api.get(`/operations/${encodeURIComponent(data.id)}`, { signal })).data;
      if (onProgress && !operation.terminal && Date.now() - progressAt > 750) {
        progressAt = Date.now();
        try {
          const { data: info } = await api.get(`/mcp/${encodeURIComponent(server)}/diagnostics`, { signal });
          if (isCurrent() && !signal?.aborted) onProgress(info.operation_progress || info);
        } catch (error) { if (signal?.aborted) throw error; }
      }
    }
    if (operation.state !== "succeeded") throw new Error(`${operation.message} (${operation.error_code}${operation.http_status ? ` · HTTP ${operation.http_status}` : ""})`);
    return operation.result.content;
  } catch (error) {
    // A context read has no external write side effects; cancel its native wait.
    if ((signal?.aborted || !isCurrent()) && owner === (targetState().selected?.id || "local")) void api.delete(`/operations/${encodeURIComponent(data.id)}`).catch(() => {});
    throw error;
  }
}

export function mcpPromptText(result) {
  const messages = result?.messages ?? [];
  const text = messages.map(message => {
    if (!["user", "assistant"].includes(message.role)) throw new Error("MCP 提示词包含不支持的消息角色。");
    if (message.content?.type !== "text") throw new Error("此提示词含有非文本内容，请先在 MCP 资源页查看完整结果。");
    return `${messages.length > 1 ? `[${message.role}]\n` : ""}${String(message.content.text || "")}`;
  }).join("\n\n");
  if (!text || text.length > 65536) throw new Error("MCP 提示词为空或超过 64 KiB，请缩小参数范围。");
  return text;
}

export async function expandMcpCommand(value, project, options) {
  const match = /^\/mcp\s+([A-Za-z0-9_.-]{1,128})\s+([^\s]{1,256})(?:\s+([\s\S]*))?$/.exec(value);
  if (!match) throw new Error('用法：/mcp 服务器 提示词名称 {"参数":"值"}');
  const args = match[3]?.trim() ? JSON.parse(match[3]) : {};
  return expandMcpArguments([match[1], match[2], args], project, options);
}

export async function expandMcpArguments(values, project, options) {
  if (!Array.isArray(values) || values.length < 2 || values.length > 3 ||
      typeof values[0] !== "string" || !/^[A-Za-z0-9_.-]{1,128}$/.test(values[0]) ||
      typeof values[1] !== "string" || !values[1] || values[1].length > 256)
    throw new Error('用法：/mcp ["服务器","提示词",{"参数":"值"}]');
  const args = values.length === 3 ? values[2] : {};
  if (!args || Array.isArray(args) || typeof args !== "object" || Object.values(args).some(v => typeof v !== "string")) throw new Error("MCP 提示词参数必须是字符串值的 JSON 对象。");
  let server = values[0];
  const query = `?project=${encodeURIComponent(project || "default")}`;
  const items = (await api.get(`/mcp/project${query}`, { signal: options?.signal })).data.items || [];
  const scoped = items.find(item => item.id === server);
  if (scoped) {
    if (!scoped.enabled || !scoped.valid) throw new Error("同名项目 MCP 已停用或配置无效。");
    await api.post(`/mcp/project${query}`, {}, { signal: options?.signal }); server = scoped.runtime_id;
  }
  return mcpPromptText(await mcpContextRead(server, "prompts/get", { name: values[1], arguments: args }, options));
}
