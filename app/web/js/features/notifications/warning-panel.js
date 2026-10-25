import { openLayer } from "../../components/layers.js";
import { currentLocale } from "../../i18n.js";
import { element } from "../../utils/dom.js";
import { warningCopy } from "./warnings.js";

export function createWarningPanel({scope,refresh,onLater,onSettings}) {
  let view=null;
  const close=()=>{const own=view;view=null;own?.layer.dispose();};
  const current=own=>view===own&&own.scope===scope();
  const button=(text,run,action)=>{const b=element("button",{text,className:"secondary-button",attrs:{type:"button",...(action?{"data-warning-action":action}:{})}});b.addEventListener("click",run);return b;};
  function render(own) {
    const c=warningCopy(currentLocale());own.locale=currentLocale();own.layer.update({title:own.notice.title});own.layer.body.replaceChildren();
    own.status=element("p",{className:"notice-description",attrs:{role:"status"}});
    for(const n of own.notice.items){
      const card=element("section",{className:"warning-entry"},[element("h3",{text:n.title}),element("p",{text:n.body,className:"notice-description"})]);
      if(n.path)card.append(element("p",{text:`${c.path}：${n.path}`,className:"warning-path"}));
      if(n.remedy)card.append(element("p",{text:`${c.suggestion}：${n.remedy}`,className:"notice-description"}));
      const actions=element("div",{className:"warning-entry-actions"});
      if(n.online)actions.append(button(c.later,()=>{if(current(own)&&own.notice.items.some(r=>r.id===n.id&&r.revision===n.revision))onLater(n.original);},"later"));
      else if(n.section)actions.append(button(c.settings,()=>{if(current(own)){close();onSettings(n.section);}},"settings"));
      if(actions.children.length)card.append(actions);own.layer.body.append(card);
    }
    own.layer.body.append(own.status);own.check.textContent=c.check;own.exit.textContent=c.close;
  }
  return {close,open(notice){
    close();const own={notice,scope:scope()};view=own;
    own.layer=openLayer({title:notice.title,icon:"shield-alert",width:"52rem",height:"80%",onClosed(){if(view===own)view=null;}});
    own.layer.element.id="warning-details-dialog";own.layer.element.classList.add("warning-details-window");
    own.check=button("",async()=>{if(!current(own)||own.check.disabled)return;own.check.disabled=true;try{const ok=await refresh();if(current(own))own.status.textContent=ok?"":warningCopy(currentLocale()).stale;}finally{if(current(own))own.check.disabled=false;}});
    own.exit=button("",()=>{if(current(own))void own.layer.close();});own.layer.footer.append(own.check,own.exit);render(own);
  },sync(notice){if(!view)return;if(!notice||view.scope!==scope())return close();const changed=view.notice.revision!==notice.revision||view.locale!==currentLocale();view.notice=notice;if(changed){const scroll=view.layer.body.scrollTop;render(view);view.layer.body.scrollTop=scroll;}},destroy:close};
}
