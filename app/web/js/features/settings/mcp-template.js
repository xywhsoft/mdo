import { t as uiText } from "../../i18n.js";
// Pure recipe helpers, also used by creator-center previews. Never resolve or
// serialize device credentials here; private bindings belong to the native API.
export function newMcpTemplate(id="my-server") {
  return {format:"mdo.mcp-template.v1",id,operator:"",inputs:[{id:"endpoint",type:"text",label:uiText("ui.mcpTemplate.serviceUrl", {}, "服务地址"),description:uiText("ui.mcpTemplate.httpsMcpEndpoint", {}, "HTTPS MCP 端点"),required:true,secret:false}],
    variants:[{id:"default",label:uiText("ui.mcpTemplate.httpService", {}, "HTTP 服务"),platforms:["any"],config:{schema_version:1,name:uiText("ui.singleResourceStore.mcpService", {}, "MCP 服务"),description:uiText("ui.mcpTemplate.mcpServiceConnection", {}, "MCP 服务连接"),protocol_version:"2025-11-25",startup_timeout_ms:10000,request_timeout_ms:60000,
      limits:{message_bytes:1048576,tools:128},tools:{allow:[],deny:[]},security:{default_effects:["external-service"],permission_profile:"balanced",trust_read_only_annotations:false},auto_reconnect:true,
      transport:{type:"streamable-http",endpoint:{input:"endpoint"},headers:[]}}}]};
}
export function mcpVariantInputs(template, variant) {
  const used = new Set();
  function walk(value) {
    if (Array.isArray(value)) value.forEach(walk);
    else if (value && typeof value === "object") {
      if (typeof value.input === "string") used.add(value.input);
      else Object.values(value).forEach(walk);
    } else if (typeof value === "string" && value.startsWith("input:")) used.add(value.slice(6));
  }
  walk(variant?.config);
  return (template.inputs || []).filter(input => used.has(input.id));
}
export function mcpTemplateSummary(template) {
  const variants = template?.variants || [];
  return {
    transports: [...new Set(variants.map(v => v.config?.transport?.type))].filter(Boolean),
    authentication: [...new Set(variants.map(v => v.config?.oauth?.enabled ? "OAuth" : v.config?.transport?.headers?.length || v.config?.transport?.environment?.length ? uiText("ui.mcpTemplate.secretEnvironmentVariables", {}, "密钥 / 环境变量") : uiText("ui.mcpTemplate.noAuthorizationRequired", {}, "无需授权")))],
    platforms: [...new Set(variants.flatMap(v => v.platforms || []))],
  };
}
export function parseMcpTemplate(text) {
  const template = JSON.parse(text);
  if (template?.format !== "mdo.mcp-template.v1" || !Array.isArray(template.inputs) || !Array.isArray(template.variants) || !template.variants.length) throw Error(uiText("ui.mcpTemplate.selectAnMcpServiceTemplateMdoMcpTemplateV1", {}, "请选择 MCP 服务模板（mdo.mcp-template.v1）"));
  if (template.inputs.some(i => i.secret && Object.hasOwn(i, "default"))) throw Error(uiText("ui.mcpTemplate.credentialInputsCannotContainDefaults", {}, "凭据参数不能包含默认值"));
  return template;
}

export function mcpTemplateDiff(before, after) {
  function flatten(value, path = "", out = {}) {
    if (value && typeof value === "object") for (const [key, child] of Object.entries(value)) flatten(child, path ? `${path}.${key}` : key, out);
    else out[path] = value;
    return out;
  }
  const a=flatten(before), b=flatten(after);
  return [...new Set([...Object.keys(a),...Object.keys(b)])].filter(key=>JSON.stringify(a[key])!==JSON.stringify(b[key])).map(path=>({path,before:a[path],after:b[path]}));
}
