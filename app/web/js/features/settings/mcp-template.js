import { t as uiText } from "../../i18n.js";
// Directory authoring and connection forms never resolve or serialize device
// credentials here; private bindings and template validation belong to the native API.
export function newMcpTemplate(id="my-server") {
  return {format:"mdo.mcp-template.v1",id,operator:"",inputs:[{id:"endpoint",type:"text",label:uiText("ui.mcpTemplate.serviceUrl", {}, "服务地址"),description:uiText("ui.mcpTemplate.httpsMcpEndpoint", {}, "HTTPS MCP 端点"),required:true,secret:false}],
    variants:[{id:"default",default:true,label:uiText("ui.mcpTemplate.httpService", {}, "HTTP 服务"),platforms:["any"],config:{schema_version:1,name:uiText("ui.singleResourceStore.mcpService", {}, "MCP 服务"),description:uiText("ui.mcpTemplate.mcpServiceConnection", {}, "MCP 服务连接"),protocol_version:"2025-11-25",startup_timeout_ms:10000,request_timeout_ms:60000,
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
