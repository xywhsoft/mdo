// Compose masks only after the real native iframe owner binds them to the
// parent document. Rectangle coincidence never grants permission to unmask.
const issue=(code,message)=>Object.assign(new Error(message),{code});
const documentId=/^d-[a-f0-9-]{36}$/i,elementId=/^e[1-9][0-9]{0,12}$/;
const finite=value=>typeof value==='number'&&Number.isFinite(value)&&Math.abs(value)<=1e8;
const point=value=>value&&finite(value.x)&&finite(value.y);
const rectangle=value=>point(value)&&finite(value.width)&&finite(value.height)&&value.width>=0&&value.height>=0;
const viewport=value=>value&&finite(value.width)&&finite(value.height)&&value.width>0&&value.height>0&&value.width<=32768&&value.height<=32768;
const intersect=(a,b)=>{
  const x=Math.max(a.x,b.x),y=Math.max(a.y,b.y),width=Math.min(a.x+a.width,b.x+b.width)-x,height=Math.min(a.y+a.height,b.y+b.height)-y;
  return width>0&&height>0?{x,y,width,height}:null;
};
const sameRect=(a,b)=>a&&b&&['x','y','width','height'].every(key=>Math.abs(a[key]-b[key])<.05);
const samePoint=(a,b)=>point(a)&&point(b)&&Math.abs(a.x-b.x)<.05&&Math.abs(a.y-b.y)<.05;

export async function prepareFrameCapture({options,describe,locate,evaluate,native,nativeCapture=null}){
  const mode=options.mode||'viewport';
  if(!['viewport','full_page','element'].includes(mode))throw issue('capture_mode_invalid','Choose a supported capture mode');
  const meta=await describe({document:options.document});if(!meta.ok)return meta;
  if(!documentId.test(meta.document)||!Array.isArray(meta.frames)||!meta.frames.length||meta.frames.length>32)
    throw issue('frame_response_invalid','The capture inventory is incomplete');
  const rows=new Map(meta.frames.map(row=>[row.id,{id:row.id,document:row.document,parent:row.parent,loading:row.loading,isolated:row.isolated}])),root=rows.get('f0'),ordered=[root];
  if(!root||root.parent!==null||rows.size!==meta.frames.length)throw issue('frame_response_invalid','Capture has no original frame root');
  for(let index=0;index<ordered.length;index++)ordered.push(...[...rows.values()].filter(row=>row.parent===ordered[index].id).sort((a,b)=>a.id.localeCompare(b.id)));
  if(ordered.length!==rows.size)throw issue('frame_response_invalid','The capture ancestry does not reach its original root');
  let target=null;
  if(mode==='element'){
    const found=await locate({document:meta.document,target:options.target});if(!found.ok)return found;target=found.route;
    if(!rows.has(target.frame)||rows.get(target.frame).document!==target.frame_document)throw issue('stale_frame','The capture target frame changed');
  }
  const records=new Map();let usedNodes=0,remaining=6000;
  const readArea=async(row,localMode,route=null)=>{
    if(!remaining)throw issue('capture_mask_limit','The combined screenshot scan exhausted its node budget');
    const value=await evaluate({frame:row.id,frame_document:row.document},'captureArea',{
      mode:localMode,_max_nodes:remaining,...(route?{document:route.document,target:{ref:route.ref}}:row.id==='f0'?{document:meta.document}:{})});
    if(!value.ok)throw issue(value.error?.code||'capture_preparation_failed','The frame privacy scan did not complete');
    if(!documentId.test(value.document)||!rectangle(value.region)||!value.region.width||!value.region.height||!point(value.scroll)||!viewport(value.viewport)||
       !finite(value.device_pixel_ratio)||value.device_pixel_ratio<=0||value.device_pixel_ratio>16||!Number.isInteger(value.visited)||value.visited<0||value.visited>remaining||
       !Array.isArray(value.redactions)||value.redactions.length>256||value.redactions.some(mask=>!rectangle(mask)||mask.frame_ref!==undefined&&!elementId.test(mask.frame_ref)))
      throw issue('frame_response_invalid','The frame returned invalid privacy masks or traversal usage');
    const refs=value.redactions.filter(mask=>mask.frame_ref).map(mask=>mask.frame_ref);
    if(new Set(refs).size!==refs.length)throw issue('frame_response_invalid','An iframe privacy mask has duplicate identities');
    usedNodes+=value.visited;remaining-=value.visited;return value;
  };
  const base=await readArea(root,target?.frame==='f0'?'element':mode==='full_page'?'full_page':'viewport',target?.frame==='f0'?target:null);
  if(base.document!==meta.document)throw issue('stale_document','The capture root changed while preparing its masks');
  records.set('f0',{row:root,area:base,removed:new Set(),status:'readable',edge:null});
  // Input rectangles are document CSS pixels. Owner content is parent-viewport
  // CSS pixels; every child viewport clips before translation to its parent.
  function project(id,box,clip=base.region){
    if(id==='f0')return intersect(box,clip);
    let record=records.get(id),area=record?.area;if(!area)return null;
    let result={x:box.x-area.scroll.x,y:box.y-area.scroll.y,width:box.width,height:box.height};
    while(record.row.id!=='f0'){
      const edge=record.edge,parent=records.get(record.row.parent);if(!edge||!parent?.area)return null;
      result=intersect(result,{x:0,y:0,width:Math.min(record.area.viewport.width,edge.content.width),height:Math.min(record.area.viewport.height,edge.content.height)});
      if(!result)return null;
      result.x+=edge.content.x;result.y+=edge.content.y;record=parent;
    }
    result.x+=base.scroll.x;result.y+=base.scroll.y;return intersect(result,clip);
  }
  for(const row of ordered.slice(1)){
    const parent=records.get(row.parent),record={row,area:null,removed:new Set(),edge:null,status:'opaque_ancestor'};records.set(row.id,record);
    if(!parent?.area)continue;
    if(row.loading||!row.isolated){record.status='opaque_not_ready';continue;}
    if(!remaining){record.status='opaque_budget';continue;}
    const owner=await native('frames',{action:'capture_owner',agent:true,frame:row.id,document:row.document});
    if(!owner?.ok)throw issue(owner?.error?.code||'frame_unavailable','The exact capture owner could not be resolved');
    if(owner.frame!==row.id||owner.frame_document!==row.document||owner.parent!==row.parent||owner.parent_document!==parent.row.document||
       owner.value?.owner?.document!==parent.area.document||!elementId.test(owner.value.owner.ref))
      throw issue('stale_frame','The capture owner no longer matches its parent document');
    const value=owner.value,mask=parent.area.redactions.find(box=>box.frame_ref===value.owner.ref);
    record.edge={owner:value.owner,content:value.content,box:value.box};
    if(!value.ok){
      if(!mask||!['unsupported_geometry','frame_ancestry_limit'].includes(value.error?.code))throw issue(value.error?.code||'capture_preparation_failed','The frame owner cannot be safely masked');
      record.status='opaque_geometry';continue;
    }
    if(typeof value.painted!=='boolean'||!rectangle(value.content)||!rectangle(value.box)||!point(value.scroll)||!viewport(value.viewport))
      throw issue('frame_response_invalid','The frame owner returned invalid capture geometry');
    if(!samePoint(value.scroll,parent.area.scroll)||Math.abs(value.viewport.width-parent.area.viewport.width)>.05||Math.abs(value.viewport.height-parent.area.viewport.height)>.05)
      throw issue('capture_changed','A parent scrolled or resized during privacy preparation');
    if(!value.painted){record.status='not_painted';continue;}
    const contentDocument={...value.content,x:value.content.x+parent.area.scroll.x,y:value.content.y+parent.area.scroll.y};
    if(!project(row.parent,contentDocument)){record.status='outside_capture';continue;}
    const expected=intersect({...value.box,x:value.box.x+parent.area.scroll.x,y:value.box.y+parent.area.scroll.y},parent.area.region);
    if(!mask||!sameRect(mask,expected))throw issue('capture_changed','The exact iframe mask moved or is missing; retain its opacity and retry');
    try{record.area=await readArea(row,'viewport');}
    catch(error){
      if(error.browserBoundary)throw error;
      if(['frame_evaluation_failed','frame_world_unavailable','frame_unavailable','frame_session_unavailable'].includes(error.code)){record.status='opaque_unreadable';continue;}
      if(error.code==='capture_preparation_failed'){remaining=0;record.status='opaque_budget';continue;}
      throw error;
    }
    if(Math.abs(record.area.device_pixel_ratio-base.device_pixel_ratio)>.001||Math.abs(record.area.viewport.width-value.content.width)>2||Math.abs(record.area.viewport.height-value.content.height)>2){
      record.area=null;record.status='opaque_scale';continue;
    }
    record.status='readable';parent.removed.add(value.owner.ref);
  }
  let region=base.region;
  if(target&&target.frame!=='f0'){
    const record=records.get(target.frame);
    if(!record?.area)throw issue('frame_capture_unsupported','The target frame could not be safely unmasked; use DOM observation or manual control');
    const local=await readArea(record.row,'element',target);
    if(local.document!==record.area.document||!samePoint(local.scroll,record.area.scroll))throw issue('stale_frame','The screenshot target changed during mask preparation');
    const box=project(target.frame,local.region);
    if(!box)throw issue('element_outside_viewport','The screenshot target has no visible region through its iframe ancestors');
    region={x:Math.floor(box.x),y:Math.floor(box.y),width:Math.ceil(box.x+box.width)-Math.floor(box.x),height:Math.ceil(box.y+box.height)-Math.floor(box.y)};
  }
  if(region.width>32768||region.height>32768||region.width*region.height>16777216||region.width*region.height*base.device_pixel_ratio**2>33554432)
    throw issue('capture_area_limit','The combined screenshot region exceeds its raster budget');
  const redactions=[];
  for(const record of records.values())if(record.area)for(const mask of record.area.redactions){
    if(mask.frame_ref&&record.removed.has(mask.frame_ref))continue;
    const projected=project(record.row.id,mask,region);if(!projected)continue;
    if(redactions.length>=256)throw issue('capture_mask_limit','The combined privacy masks exceed their budget; capture a smaller region');
    redactions.push(projected);
  }
  const after=await describe({document:meta.document});if(!after.ok)return after;
  if(!Array.isArray(after.frames)||after.frames.length!==ordered.length||after.frames.some(row=>{
    const original=rows.get(row.id);return!original||row.document!==original.document||row.parent!==original.parent||row.loading!==original.loading||row.isolated!==original.isolated;
  }))throw issue('stale_frame','The frame tree changed during screenshot mask composition');
  const frame_stamp=JSON.stringify(ordered.map(row=>{
    const record=records.get(row.id);return[row.id,row.document,row.parent,row.loading,row.isolated,record.status,
      record.area?.document,record.area?.scroll,record.area?.viewport,record.edge];
  }));
  return{ok:true,document:meta.document,region,redactions,device_pixel_ratio:base.device_pixel_ratio,scroll:base.scroll,viewport:base.viewport,
    ...(nativeCapture?{native_capture:{adapter:'android-native-frames-v1',native_document:root.document,document:meta.document,navigation:nativeCapture.navigation,
      viewport:{width:base.viewport.width,height:base.viewport.height},scroll:{x:base.scroll.x,y:base.scroll.y},device_pixel_ratio:base.device_pixel_ratio,frame_stamp},
      frame_scope:'native-frame-tree',limitations:['android-visible-viewport-only']}:{}),
    redaction:'form-controls-and-opaque-content',frame_stamp,visited:usedNodes,
    frame_masks:{readable:[...records.values()].filter(record=>record.area).length,opaque:[...records.values()].filter(record=>!record.area&&record.status!=='outside_capture'&&record.status!=='not_painted').length}};
}
