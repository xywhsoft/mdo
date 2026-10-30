import { t as uiText } from "../../i18n.js";
import { api } from "../../api/client.js";

export const directoryPath = (kind,id) => `/extensions/${kind}/${encodeURIComponent(id)}`;
export function directoryId(value,max=96) {
  return typeof value==="string" && value.length<=max && /^[A-Za-z0-9_-][A-Za-z0-9._-]*$/.test(value) && !value.endsWith(".") && !value.includes("..") && !/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(value);
}
export const directoryEntries = {agents:"agent.md",subagents:"agent.md",tools:"tool.c",commands:"command.c",skills:"SKILL.md",mcp:"mcp.json",knowledge:"knowledge.json","external-tools":"tool.json",plugins:"plugin.json"};
export function directoryManifest(kind,id,fields={}) {
  return {kind,name:id,alias:id,description:"",entry:directoryEntries[kind],platforms:["any"],...fields};
}
export async function readDirectoryEntry(kind,id,options={}) {
  const item=(await api.get(directoryPath(kind,id),options)).data;
  if(!item.manifest?.entry)throw Error(uiText("ui.directoryDocuments.theExtensionManifestHasNoValidEntryRepairExtension", {}, "扩展根清单无有效入口，请先修复 extension.json"));
  const file=(await api.get(`${directoryPath(kind,id)}/file?path=${encodeURIComponent(item.manifest.entry)}`,options)).data;
  if(file.binary)throw Error(uiText("ui.directoryDocuments.thisEntryCannotBeEditedAsTextUseThe", {}, "此入口不支持文本编辑，请使用目录文件窗口或导入整个 ZIP"));
  return {...item,content:file.text};
}
export async function saveDirectory(kind,id,manifest,edits,expected="",options={}) {
  if(!directoryId(id)||manifest?.kind!==kind)throw Error(uiText("ui.directoryDocuments.selectTheCurrentTypeAndAValidDirectoryName", {}, "请选择当前类型及有效目录名称"));
  return (await api.put(directoryPath(kind,id),{manifest,edits,expected_revision:expected,trust_code:true},options)).data;
}
export async function changeDirectory(kind,item,action,extra={},options={}) {
  // The current whole-directory fingerprint is always part of a destructive
  // operation. A stale list receipt must never authorize replacing newer files.
  const fresh=(await api.get(directoryPath(kind,item.id),options)).data;
  return (await api.post(`${directoryPath(kind,item.id)}/actions`,{action,expected_revision:fresh.revision,...extra},options)).data;
}
