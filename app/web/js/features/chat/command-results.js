import { uiMessage } from "../../i18n.js";
import { element, toast, errorMessage } from "../../utils/dom.js";
import { copyText } from "../../utils/clipboard.js";

export function commandResultContent(value) {
  if(value===null||value===undefined)return {message:"",draft:null,detail:""};
  if(typeof value!=="object"||Array.isArray(value))return {message:"",draft:null,detail:JSON.stringify(value,null,2)};
  const {message,draft,...data}=value;
  const validDraft=typeof draft==="string"&&new TextEncoder().encode(draft).length<=65536;
  if(draft!==undefined&&!validDraft)data.draft=draft;
  if(message!==undefined&&typeof message!=="string")data.message=message;
  return {message:typeof message==="string"?message:"",draft:validDraft?draft:null,
    detail:Object.keys(data).length?JSON.stringify(data,null,2):""};
}
// Results never become conversation messages automatically. File metadata is
// plain text; a command result cannot navigate to or fetch arbitrary paths.
export function presentCommandResult(receipt,{input,isCurrent=()=>true}={}) {
  const content=commandResultContent(receipt.result);
  if(content.message)toast(content.message);
  let draft=content.draft;
  if(draft!==null&&input&&!input.value&&isCurrent()){
    input.value=draft;input.dispatchEvent(new Event("input",{bubbles:true}));input.focus();draft=null;
  }
  const text=[draft===null?"":draft,content.detail].filter(Boolean).join("\n\n");
  if(!text){if(!content.message&&!receipt.actions?.length)toast(uiMessage("ui.commandResults.commandCompleted", {}, "命令已完成。"));return;}
  const previous=document.activeElement;
  const dialog=element("dialog",{className:"extension-editor"});
  const preview=element("textarea",{className:"extension-source",attrs:{readonly:"","aria-label":uiMessage("ui.commandResults.commandResult", {}, "命令结果")}});preview.value=text;
  const close=()=>{dialog.remove();if(previous?.isConnected)previous.focus();};
  const copy=element("button",{className:"secondary-button",text:uiMessage("ui.commandResults.copyResult", {}, "复制结果"),attrs:{type:"button"}});
  copy.onclick=()=>{void copyText(text).catch(error=>toast(errorMessage(error),"error"));};
  const done=element("button",{className:"primary-button",text:uiMessage("context.close", {}, "关闭"),attrs:{type:"button"}});done.onclick=close;
  dialog.append(element("h2",{text:uiMessage("ui.commandResults.value1CommandResult", { value1: receipt.command_id }, "/{value1} · 命令结果")}),preview,
    element("div",{className:"extension-toolbar"},[copy,done]));
  dialog.addEventListener("cancel",event=>{event.preventDefault();close();});document.body.append(dialog);dialog.showModal();done.focus();
}
