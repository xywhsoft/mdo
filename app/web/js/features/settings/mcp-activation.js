import { uiMessage } from "../../i18n.js";

export function mcpActivationLabel(item) {
  const state=item.enabled===false?"disabled":item.state||"starting";
  const fallback={starting:"正在连接…",ready:"可用",disconnected:"待连接",needs_configuration:"需要配置",needs_authorization:"需要登录",needs_approval:"需要确认",unsupported:"不支持此设备",disabled:"已停用",failed:"连接失败"};
  return uiMessage(`mcpActivation.${state}`,{},fallback[state]||fallback.failed);
}
export function mcpActivationAction(item) {
  if(!item.enabled)return null;
  return ({needs_configuration:"configure",needs_approval:"configure",needs_authorization:"authorize",failed:"retry",disconnected:"retry"})[item.state]||null;
}
