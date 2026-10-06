export function toolActions(distribution, id, target = {}) {
  const pack = distribution.toolpacks?.find(p => p.id === id && p.status !== "withdrawn"), installed = distribution.installed ?? {};
  const writable = !target.selected || (target.connected && !target.runtimeChanged && target.selected.mode !== "view");
  const compatible = pack && (!pack.min_build || distribution.build_id >= pack.min_build) && (!pack.max_build || distribution.build_id < pack.max_build)
    && (pack.dependencies ?? []).every(d => installed[d.id] && installed[d.id + "_revision"] >= d.revision);
  const enabled = writable && !distribution.busy && distribution.edition === "desktop";
  return { install: !!(enabled && compatible && (!installed[id] || pack.revision > installed[id + "_revision"])),
    repair: !!(enabled && compatible && installed[id]), uninstall: !!(enabled && installed[id]),
    rollback: !!(enabled && installed.retired?.some(p => p.id === id)), cleanup: !!(enabled && distribution.cleanup_available),
    check: writable && !distribution.busy };
}
export function createToolPanel({copy,target,command,openFull}) {
  const signatures = new WeakMap();
  const node = (tag,text,cls) => { const e=document.createElement(tag);if(text!=null)e.textContent=text;if(cls)e.className=cls;return e; };
  const button = (text,action,disabled=false) => { const e=node("button",text,"secondary-button");e.type="button";e.disabled=disabled;e.onclick=action;return e; };
  function action(name,id) { const c=copy();if(["uninstall","cleanup"].includes(name)&&!window.confirm(name==="uninstall"?c.confirmUninstall:c.confirmCleanup))return;void command(name,id); }
  return { render(body,d,updatesOnly=false) {
    if(!body)return;
    const context=target(),c=copy(),signature=JSON.stringify([d,context.selected?.id,context.selected?.mode,context.connected,context.runtimeChanged,c,updatesOnly]);
    if(signatures.get(body)===signature)return;signatures.set(body,signature);body.replaceChildren();
    body.append(node("h3",updatesOnly?c.toolsUpdate:c.tools));
    if(d.edition!=="desktop") {
      body.append(node("p",c.android));
      if(!updatesOnly)body.append(button(c.full,openFull,!toolActions(d,"",context).check));
    } else {
      let shown=0;
      for(const id of ["core","python"]) {
        const pack=d.toolpacks?.find(p=>p.id===id),installed=d.installed?.[id],actions=toolActions(d,id,context);
        if(updatesOnly&&(!installed||!pack||pack.status==="withdrawn"||pack.revision<=(d.installed?.[id+"_revision"]??0)))continue;
        shown++;
        const row=node("section",null,"toolpack-section"),controls=node("div",null,"toolpack-row");
        row.append(node("strong",pack?.name||c[id]),node("p",`${installed?c.installed:c.missing}${installed?" · r"+d.installed[id+"_revision"]:""}${pack?" → r"+pack.revision:""}`));
        if(pack?.notes)row.append(node("p",pack.notes,"notice-description"));
        controls.append(button(installed?c.update:c.install,()=>action("install",id),!actions.install));
        if(!updatesOnly&&installed)for(const a of ["repair","rollback","uninstall"])controls.append(button(c[a],()=>action(a,id),!actions[a]));
        row.append(controls);
        if(!updatesOnly&&pack) {
          row.append(node("small",`${c.compatibility}: ${pack.min_build||"…"} ≤ build < ${pack.max_build||"…"}`));
          if(pack.dependencies?.length)row.append(node("p",c.dependencies+": "+pack.dependencies.map(p=>p.id+" ≥ r"+p.revision).join(", ")));
          for(const tool of pack.tools??[]) {
            row.append(node("p",`${tool.name||tool.id} · ${tool.version} · ${(tool.size/1048576).toFixed(1)} MiB · ${tool.license}`));
            if(/^https:\/\//.test(tool.source??"")){const a=node("a",c.source);a.href=tool.source;a.target="_blank";a.rel="noopener noreferrer";row.append(a);}
          }
        }body.append(row);
      }
      if(updatesOnly&&!shown)body.append(node("p",c.noToolUpdates));
      if(!updatesOnly){body.append(button(c.cleanup,()=>action("cleanup",""),!toolActions(d,"",context).cleanup));if(d.installed?.retired?.length)body.append(node("p",c.cleanupHelp));}
    }
    if(!updatesOnly)for(const tool of d.tools??[])body.append(node("p",tool.id+" · "+tool.version+" · "+tool.verified),node("code",tool.path));
    if(d.busy) {
      const stage=c.stages?.[d.stage]||c.busy;const progress=node("progress");progress.max=d.total||1;progress.value=Math.min(d.done||0,progress.max);
      body.append(node("p",stage+(d.total?" · "+Math.floor((d.done||0)*100/d.total)+"%":""),"toolpack-progress"),progress,button(c.cancel,()=>action("cancel",""),!toolActions({...d,busy:false},"",context).check));
    }
    if(d.message)body.append(node("p",d.message,"notice-description"));
    if(!updatesOnly)body.append(button(c.check,()=>action("check",""),!toolActions(d,"",context).check));
  } };
}
