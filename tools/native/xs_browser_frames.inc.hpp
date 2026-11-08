/* Private CDP routing belongs to this content view. No browser-wide target
 * discovery, security flags or universal-origin execution are enabled. Public
 * aliases never accept a caller-supplied raw frame/session/context identity. */
static constexpr UINT_PTR xb_frames_timer=0xBADD8;
static std::map<HWND,std::weak_ptr<xb_view>> xb_frame_views;
static const char* xb_frame_owner_capture=R"js(function(){
  const frame=this,doc=frame.ownerDocument,view=doc?.defaultView,runtime=globalThis.__mdo_browser_use_local_frame_v1;
  const fail=code=>({ok:false,error:{code}});
  if(!frame.isConnected||!view||!['iframe','frame'].includes(frame.localName))return fail('frame_owner_unavailable');
  if(!runtime||runtime.version<14||runtime.documentNode!==doc)return fail('frame_world_not_ready');
  const owner=runtime.pinCaptureFrame(frame);if(!owner.ok)return owner;
  const reject=code=>({ok:false,owner,error:{code}});
  if(Math.abs((view.visualViewport?.scale||1)-1)>.001)return reject('unsupported_geometry');
  let node=frame,depth=0;
  for(;node&&depth<256;depth++){
    const style=view.getComputedStyle(node);
    if(style.transform!=='none'||style.perspective!=='none'||style.rotate&&style.rotate!=='none'||
       style.scale&&style.scale!=='none'||style.translate&&style.translate!=='none'||style.zoom&&Number(style.zoom)!==1)return reject('unsupported_geometry');
    node=node.assignedSlot||node.parentElement||node.getRootNode()?.host;
  }
  if(node)return reject('frame_ancestry_limit');
  const box=frame.getBoundingClientRect(),style=view.getComputedStyle(frame),number=key=>parseFloat(style[key])||0;
  const left=number('borderLeftWidth')+number('paddingLeft'),top=number('borderTopWidth')+number('paddingTop');
  const content={x:box.x+left,y:box.y+top,width:box.width-left-number('borderRightWidth')-number('paddingRight'),height:box.height-top-number('borderBottomWidth')-number('paddingBottom')};
  if(![box.x,box.y,box.width,box.height,content.x,content.y,content.width,content.height].every(Number.isFinite))return reject('unsupported_geometry');
  const painted=style.display!=='none'&&style.visibility!=='hidden'&&style.visibility!=='collapse'&&Number(style.opacity)!==0&&frame.getClientRects().length>0&&content.width>0&&content.height>0;
  return{ok:true,owner,painted,content,box:{x:box.x,y:box.y,width:box.width,height:box.height},scroll:{x:view.scrollX,y:view.scrollY},viewport:{width:view.innerWidth,height:view.innerHeight}};
})js";
static const char* xb_frame_owner_upload_policy=R"js(function(){
  const fail=code=>({ok:false,error:{code}}),frame=this;
  if(!frame.isConnected||!frame.ownerDocument?.defaultView||!['iframe','frame'].includes(frame.localName))return fail('frame_owner_unavailable');
  let node=frame;
  for(let depth=0;node&&depth<256;depth++){
    if(node.inert)return fail('element_disabled');
    node=node.assignedSlot||node.parentElement||node.getRootNode()?.host;
  }
  return node?fail('frame_ancestry_limit'):{ok:true};
})js";
static const char* xb_frame_owner_geometry=R"js(function(point){
  const fail=code=>({ok:false,error:{code}}),frame=this,doc=frame.ownerDocument,view=doc?.defaultView;
  if(!frame.isConnected||!view||!['iframe','frame'].includes(frame.localName))return fail('frame_owner_unavailable');
  if(Math.abs((view.visualViewport?.scale||1)-1)>.001)return fail('unsupported_geometry');
  let node=frame,depth=0;
  for(;node&&depth<256;depth++){
    const style=view.getComputedStyle(node);
    if(node.inert||node.hidden||node.getAttribute('aria-hidden')==='true'||style.display==='none'||
       style.visibility==='hidden'||style.visibility==='collapse'||Number(style.opacity)===0)return fail('frame_owner_hidden');
    if(style.transform!=='none'||style.perspective!=='none'||style.rotate&&style.rotate!=='none'||
       style.scale&&style.scale!=='none'||style.translate&&style.translate!=='none'||
       style.zoom&&Number(style.zoom)!==1)return fail('unsupported_geometry');
    node=node.assignedSlot||node.parentElement||node.getRootNode()?.host;
  }
  if(node)return fail('frame_ancestry_limit');
  const box=frame.getBoundingClientRect(),style=view.getComputedStyle(frame);
  const left=parseFloat(style.paddingLeft)||0,top=parseFloat(style.paddingTop)||0,
    right=parseFloat(style.paddingRight)||0,bottom=parseFloat(style.paddingBottom)||0;
  const borderLeft=parseFloat(style.borderLeftWidth)||0,borderTop=parseFloat(style.borderTopWidth)||0,
    borderRight=parseFloat(style.borderRightWidth)||0,borderBottom=parseFloat(style.borderBottomWidth)||0;
  const content={x:box.x+borderLeft+left,y:box.y+borderTop+top,
    width:box.width-borderLeft-borderRight-left-right,height:box.height-borderTop-borderBottom-top-bottom};
  if(![content.x,content.y,content.width,content.height].every(Number.isFinite)||content.width<=0||content.height<=0)
    return fail('frame_owner_hidden');
  if(point.x<0||point.y<0||point.x>=content.width||point.y>=content.height)return fail('frame_point_outside_viewport');
  const mapped={x:content.x+point.x,y:content.y+point.y};
  if(mapped.x<0||mapped.y<0||mapped.x>=view.innerWidth||mapped.y>=view.innerHeight)return fail('frame_point_outside_viewport');
  let hit=doc.elementFromPoint(mapped.x,mapped.y);
  for(let depth=0;depth<16&&hit?.shadowRoot;depth++){
    const child=hit.shadowRoot.elementFromPoint?.(mapped.x,mapped.y);if(!child||child===hit)break;hit=child;
  }
  if(hit!==frame)return fail('frame_owner_obscured');
  let active=doc.activeElement;
  for(let depth=0;depth<16&&active?.shadowRoot?.activeElement;depth++)active=active.shadowRoot.activeElement;
  return{ok:true,point:mapped,content,focused:active===frame,viewport:{width:view.innerWidth,height:view.innerHeight},
    geometry:[box.x,box.y,box.width,box.height,content.x,content.y,content.width,content.height]};
})js";
static std::string xb_frame_id(const char* prefix){
  GUID value{};wchar_t text[40]{};if(FAILED(CoCreateGuid(&value))||!StringFromGUID2(value,text,40))return{};
  return std::string(prefix)+webview::detail::narrow_string(std::wstring(text+1,36));
}
static bool xb_frame_live(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,UINT64 navigation){
  return xb_automation_live(host,view,navigation)&&view->frames&&!view->frames->closed;
}
struct xb_frame_cdp_completion{
  xb_cdp_result done;bool settled=false;
  void finish(bool ok,const std::string& value){if(settled)return;settled=true;auto callback=std::move(done);callback(ok,value);}
};
static void xb_frame_cdp(const std::shared_ptr<xb_view>& view,const std::string& session,const wchar_t* method,
                         const std::string& params,xb_cdp_result done){
  auto completion=std::make_shared<xb_frame_cdp_completion>();completion->done=std::move(done);
  xs_wv_com<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler> callback;
  callback.p=xb_cb<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,HRESULT,LPCWSTR>(
    IID_ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,[completion](HRESULT status,LPCWSTR output){
      size_t size=0;if(output)while(size<=1048576&&output[size])size++;
      if(size>1048576){completion->finish(false,"{}");return S_OK;}
      const auto value=webview::detail::narrow_string(output?output:L"{}");
      if(value.size()>2097152){completion->finish(false,"{}");return S_OK;}
      completion->finish(SUCCEEDED(status)&&xb_get(value,"error").empty(),value);return S_OK;
    });
  HRESULT status=E_NOINTERFACE;
  if(session.empty())status=view->core->CallDevToolsProtocolMethod(method,webview::detail::widen_string(params).c_str(),callback.p);
  else if(view->frames&&view->frames->source&&view->frames->sessions.count(session))
    status=view->frames->source->CallDevToolsProtocolMethodForSession(webview::detail::widen_string(session).c_str(),method,webview::detail::widen_string(params).c_str(),callback.p);
  if(FAILED(status))completion->finish(false,"{}");
}
static bool xb_frame_source(const std::shared_ptr<xb_frame_state>& state,const std::string& session){
  if(session.empty())return true;auto found=state->sessions.find(session);return found!=state->sessions.end();
}
static void xb_frame_session_start(const std::shared_ptr<xb_view>& view,const std::string& session){
  auto state=view->frames;std::weak_ptr<xb_view> weak=view;std::weak_ptr<xb_frame_state> held=state;
  xb_frame_cdp(view,session,L"Page.enable","{}",[weak,held,session](bool enabled,const std::string&){
    auto view=weak.lock();auto state=held.lock();if(!view||!state||view->closed||state->closed)return;
    if(!session.empty()&&!state->sessions.count(session))return;
    if(!enabled){if(session.empty()){state->error="frame_page_events_unavailable";state->starting=false;}else state->sessions.at(session).error="frame_page_events_unavailable";return;}
    xb_frame_cdp(view,session,L"Target.setAutoAttach","{\"autoAttach\":true,\"waitForDebuggerOnStart\":false,\"flatten\":true,\"filter\":[{\"type\":\"iframe\"}]}",
      [weak,held,session](bool attached,const std::string&){
        auto view=weak.lock();auto state=held.lock();if(!view||!state||view->closed||state->closed)return;
        if(session.empty()){state->starting=false;state->ready=attached;if(!attached)state->error="frame_sessions_unavailable";}
        else if(state->sessions.count(session)){
          if(!attached){state->sessions.at(session).error="frame_sessions_unavailable";return;}
          // An attached OOPIF must inherit Agent picker suppression before it
          // becomes an input-ready frame. No process-global interception.
          if(view->chooser&&view->chooser->wanted){
            xb_frame_cdp(view,session,L"Page.setInterceptFileChooserDialog","{\"enabled\":true}",[weak,held,session](bool armed,const std::string&){
              auto view=weak.lock();auto state=held.lock();if(!view||!state||view->closed||state->closed||!state->sessions.count(session))return;
              auto& child=state->sessions.at(session);child.ready=armed;child.chooserEnabled=armed;
              if(!armed){child.error="frame_chooser_unavailable";if(view->chooser)view->chooser->policyFailed=true;}
            });
          }else state->sessions.at(session).ready=true;
        }
      });
  });
}
static xb_frame_node* xb_frame_upsert(const std::shared_ptr<xb_frame_state>& state,const std::string& raw,const std::string& parent,
                                     const std::string& loader,const std::string& url,const std::string& name,const std::string& session){
  if(raw.empty()||raw.size()>128||parent.size()>128||loader.size()>128)return nullptr;
  auto at=state->nodes.find(raw);
  if(at==state->nodes.end()){
    if(state->nodes.size()>=64){state->error="frame_limit";return nullptr;}
    xb_frame_node node;node.raw=raw;
    // NavigationStarting can lead to a file response without replacing any DOM.
    // Reuse identity only after the fresh native inventory proves the same
    // nonempty loader, raw frame and parent. Never infer this from a URL.
    auto previous=state->previousNodes.find(raw);
    if(previous!=state->previousNodes.end()&&!loader.empty()&&previous->second.loader==loader&&previous->second.parent==parent){
      node.id=previous->second.id;node.document=previous->second.document;node.loader=loader;
    }else{node.id=raw==state->root?"f0":xb_frame_id("f-");node.document=xb_frame_id("fd-");}
    if(previous!=state->previousNodes.end())state->previousNodes.erase(previous);
    if(node.id.empty()||node.document.empty()){state->error="frame_identity_unavailable";return nullptr;}
    at=state->nodes.emplace(raw,std::move(node)).first;
  }
  auto& node=at->second;
  if(!loader.empty()&&node.loader!=loader){auto document=xb_frame_id("fd-");if(document.empty()){state->error="frame_identity_unavailable";return nullptr;}node.document=document;node.loader=loader;}
  if(!parent.empty()||raw==state->root)node.parent=parent;
  node.session=session;node.url=xb_bounded_text(url,8192);node.name=xb_bounded_text(name,1024);
  node.loading=state->loading.count(raw)&&state->loading.at(raw);return &node;
}
static void xb_frame_detach(const std::shared_ptr<xb_frame_state>& state,const std::string& raw){
  std::vector<std::string> remove{raw};
  for(size_t index=0;index<remove.size()&&index<64;index++)for(const auto& item:state->nodes)if(item.second.parent==remove[index])remove.push_back(item.first);
  for(const auto& key:remove){state->nodes.erase(key);state->previousNodes.erase(key);state->loading.erase(key);}
}
static bool xb_frame_attached(const std::shared_ptr<xb_frame_state>& state,const xb_frame_node& node){
  const xb_frame_node* current=&node;
  for(unsigned depth=0;depth<32;depth++){
    if(current->raw==state->root)return current->session.empty();
    auto parent=state->nodes.find(current->parent);if(parent==state->nodes.end())return false;current=&parent->second;
  }return false;
}
static xb_frame_node* xb_frame_alias(const std::shared_ptr<xb_frame_state>& state,const std::string& id){
  for(auto& item:state->nodes)if(item.second.id==id&&xb_frame_attached(state,item.second))return &item.second;return nullptr;
}
static bool xb_frame_chain(const std::shared_ptr<xb_view>& view,const std::string& raw,xb_input_frame_guard& guard){
  guard.clear();auto state=view->frames;if(!state)return false;auto at=state->nodes.find(raw);
  for(unsigned depth=0;at!=state->nodes.end()&&depth<32;depth++){
    guard.emplace_back(at->second.id,at->second.document);
    if(at->second.id=="f0")return xb_input_frames_live(view,guard);
    at=state->nodes.find(at->second.parent);
  }guard.clear();return false;
}
static void xb_frame_event(const std::shared_ptr<xb_view>& view,const std::string& method,const std::string& source,const std::string& data){
  auto state=view->frames;if(!state||state->closed||!xb_frame_source(state,source))return;
  if(method=="Target.attachedToTarget"){
    const auto target=xb_get(data,"targetInfo"),session=xb_get(data,"sessionId"),raw=xb_get(target,"targetId");
    if(session.empty()||session.size()>128||raw.empty()||raw.size()>128)return;
    if(xb_get(target,"type")!="iframe"){
      xb_frame_cdp(view,source,L"Target.detachFromTarget","{\"sessionId\":"+xb_quote(session)+"}",[](bool,const std::string&){});return;
    }
    if(state->sessions.count(session))return;
    if(state->sessions.size()>=64){
      state->error="frame_session_limit";
      xb_frame_cdp(view,source,L"Target.detachFromTarget","{\"sessionId\":"+xb_quote(session)+"}",[](bool,const std::string&){});return;
    }
    xb_frame_session item;item.target=raw;item.parent=source;state->sessions.emplace(session,item);xb_frame_session_start(view,session);return;
  }
  if(method=="Target.detachedFromTarget"){
    const auto session=xb_get(data,"sessionId");auto found=state->sessions.find(session);if(found==state->sessions.end())return;
    std::vector<std::string> remove{session};
    for(size_t index=0;index<remove.size()&&index<64;index++)for(const auto& item:state->sessions)
      if(item.second.parent==remove[index]&&std::find(remove.begin(),remove.end(),item.first)==remove.end())remove.push_back(item.first);
    for(const auto& key:remove){auto child=state->sessions.find(key);if(child!=state->sessions.end()){xb_world_retire_source(view,key,child->second.identity);xb_frame_detach(state,child->second.target);state->sessions.erase(child);}}return;
  }
  const auto frame=xb_get(data,"frame"),raw=frame.empty()?xb_get(data,"frameId"):xb_get(frame,"id");
  if(raw.empty()||raw.size()>128)return;
  if(method=="Page.frameDetached"){xb_world_retire_frame(view,raw);xb_frame_detach(state,raw);return;}
  if(method=="Page.frameAttached"){
    const auto parent=xb_get(data,"parentFrameId");if(!parent.empty())xb_frame_upsert(state,raw,parent,"","","",source);return;
  }
  auto at=state->nodes.find(raw);
  if(method=="Page.frameStartedLoading"||method=="Page.frameStoppedLoading"){
    const bool loading=method=="Page.frameStartedLoading";
    if(loading){if(state->loading.size()<64)state->loading[raw]=true;else state->error="frame_limit";}else state->loading.erase(raw);
    if(at!=state->nodes.end())at->second.loading=loading;return;
  }
  if(method=="Page.frameNavigated"){
    const auto loader=xb_get(frame,"loaderId");if(!loader.empty())xb_world_retire_frame(view,raw,loader);
    if(source.empty()&&xb_get(frame,"parentId").empty())state->root=raw;
    xb_frame_upsert(state,raw,xb_get(frame,"parentId"),xb_get(frame,"loaderId"),xb_get(frame,"url"),xb_get(frame,"name"),source);
  }
}
static bool xb_frames_install(const std::shared_ptr<xb_view>& view){
  if(view->frames)return true;auto state=std::make_shared<xb_frame_state>();
  if(FAILED(view->core->QueryInterface(IID_ICoreWebView2_11,reinterpret_cast<void**>(&state->source))))return false;
  view->frames=state;
  const wchar_t* events[]={L"Page.frameAttached",L"Page.frameDetached",L"Page.frameNavigated",L"Page.frameStartedLoading",L"Page.frameStoppedLoading",L"Target.attachedToTarget",L"Target.detachedFromTarget"};
  for(auto event:events){
    auto receiver=std::make_shared<xb_frame_receiver>();
    if(FAILED(view->core->GetDevToolsProtocolEventReceiver(event,&receiver->source))||!receiver->source){view->frames.reset();return false;}
    const auto name=webview::detail::narrow_string(event);std::weak_ptr<xb_view> weak=view;std::weak_ptr<xb_frame_state> held=state;
    auto* callback=xb_cb<ICoreWebView2DevToolsProtocolEventReceivedEventHandler,ICoreWebView2*,ICoreWebView2DevToolsProtocolEventReceivedEventArgs*>(
      IID_ICoreWebView2DevToolsProtocolEventReceivedEventHandler,[weak,held,name](ICoreWebView2*,ICoreWebView2DevToolsProtocolEventReceivedEventArgs* args){
        auto view=weak.lock();auto state=held.lock();if(!view||!state||view->closed||state->closed||view->frames!=state)return S_OK;
        xs_wv_com<ICoreWebView2DevToolsProtocolEventReceivedEventArgs2> routed;xs_wv_text session,text;
        if(FAILED(args->QueryInterface(IID_ICoreWebView2DevToolsProtocolEventReceivedEventArgs2,reinterpret_cast<void**>(routed.out())))||
           FAILED(routed->get_SessionId(&session.p))||FAILED(args->get_ParameterObjectAsJson(&text.p))){state->error="frame_event_routing_unavailable";return S_OK;}
        size_t size=0;if(text.p)while(size<=32768&&text.p[size])size++;if(size>32768){state->error="frame_event_limit";return S_OK;}
        xb_frame_event(view,name,webview::detail::narrow_string(session.p?session.p:L""),webview::detail::narrow_string(text.p?text.p:L"{}"));return S_OK;
      });
    receiver->subscribed=SUCCEEDED(receiver->source->add_DevToolsProtocolEventReceived(callback,&receiver->token));callback->Release();
    if(!receiver->subscribed){view->frames.reset();return false;}state->receivers.push_back(receiver);
  }
  state->starting=true;xb_frame_session_start(view,"");return true;
}
struct xb_frame_job:std::enable_shared_from_this<xb_frame_job>{
  std::weak_ptr<xb_host> host;std::weak_ptr<xb_view> view;std::weak_ptr<xb_frame_state> state;
  std::string id,action,alias,expected,script,selected,document,session,parentRaw,parentAlias,parentDocument,parentSession,parentLoader,objectGroup;
  std::shared_ptr<xb_world_context> world;
  double pointX=0,pointY=0;UINT64 navigation=0;uint64_t generation=0;
  ULONGLONG deadline=0;bool done=false,started=false,complete=true,evaluationSent=false;size_t next=0;
  std::vector<std::string> sources;std::map<std::string,bool> seen;
  void finish(const std::string& result,const char* code=nullptr){
    if(done)return;done=true;auto owner=host.lock();auto current=state.lock();
    if(current)current->jobs.erase(id);
    auto active=view.lock();if(active&&current&&active->frames==current&&!objectGroup.empty()&&(!world||xb_world_source_live(active,world->session,world->sourceOwner))){
      const auto group=std::move(objectGroup);xb_frame_cdp(active,parentSession,L"Runtime.releaseObjectGroup","{\"objectGroup\":"+xb_quote(group)+"}",[](bool,const std::string&){});
    }
    if(owner&&owner->alive)owner->reply(id,code?"{\"ok\":false,\"error\":{\"code\":"+xb_quote(code)+",\"evaluation_may_have_occurred\":"+(evaluationSent?"true":"false")+",\"message\":\"Frame state changed or could not be established; inspect before another action\"},\"usage\":{\"frames\":"+
      std::to_string(current?current->nodes.size():0)+",\"sessions\":"+std::to_string(current?current->sessions.size():0)+",\"pending\":"+std::to_string(current?current->jobs.size():0)+"}}":result);
  }
  bool live(){
    auto owner=host.lock();auto active=view.lock();auto current=state.lock();
    if(!owner||!active||!current||active->frames!=current||current->generation!=generation||!xb_frame_live(owner,active,navigation)){finish("","frame_interrupted");return false;}
    if(GetTickCount64()>=deadline){finish("","frame_timed_out");return false;}return true;
  }
  void call(const std::string& source,const wchar_t* method,const std::string& params,std::function<void(bool,const std::string&)> next){
    if(!live())return;std::weak_ptr<xb_frame_job> weak=shared_from_this();auto active=view.lock();
    const auto complete=[weak,next](bool ok,const std::string& result){auto self=weak.lock();if(self&&self->live())next(ok,result);};
    if(world&&source==world->session)xb_world_protocol(active,source,world->sourceOwner,method,params,complete);else xb_frame_cdp(active,source,method,params,complete);
  }
  bool tree(const std::string& value,const std::string& source,const std::string& parent,unsigned depth=0){
    auto current=state.lock();const auto frame=xb_get(value,"frame"),raw=xb_get(frame,"id");
    if(depth>=32||raw.empty()||(!seen.count(raw)&&seen.size()>=64))return false;
    if(parent.empty()&&source.empty())current->root=raw;
    if(!source.empty()&&!current->sessions.count(source))return false;
    if(parent.empty()&&!source.empty()&&current->sessions.at(source).target!=raw)return false;
    auto routed=source;for(const auto& item:current->sessions)if(item.second.target==raw){routed=item.first;break;}
    const auto loader=xb_get(frame,"loaderId");if(!loader.empty())xb_world_retire_frame(view.lock(),raw,loader);
    auto node=xb_frame_upsert(current,raw,parent.empty()?xb_get(frame,"parentId"):parent,loader,xb_get(frame,"url"),xb_get(frame,"name"),routed);
    if(!node)return false;seen[raw]=true;
    const auto children=xb_get(value,"childFrames");
    for(int index=0;index<65;index++){const auto child=webview::detail::json_parse(children,"",index);if(child.empty())break;if(!tree(child,routed,raw,depth+1))return false;}
    return true;
  }
  void collect(){
    if(!live())return;auto current=state.lock();
    if(next==sources.size()){
      for(auto item=current->nodes.begin();item!=current->nodes.end();)if(!seen.count(item->first))item=current->nodes.erase(item);else ++item;
      for(const auto& item:current->sessions)if(!item.second.ready||std::find(sources.begin(),sources.end(),item.first)==sources.end())complete=false;
      if(action=="evaluate"||action=="owner"||action=="upload_policy"||action=="capture_owner"){evaluate();return;}std::string rows;
      for(const auto& item:current->nodes){const auto& node=item.second;if(!xb_frame_attached(current,node))continue;
        bool ready=node.session.empty()||current->sessions.count(node.session)&&current->sessions.at(node.session).ready;
        if(!rows.empty())rows+=",";const auto parent=current->nodes.find(node.parent);
        rows+="{\"id\":"+xb_quote(node.id)+",\"document\":"+xb_quote(node.document)+",\"parent\":"+(parent==current->nodes.end()?"null":xb_quote(parent->second.id))+
          ",\"url\":"+xb_quote(node.url)+",\"name\":"+xb_quote(node.name)+",\"loading\":"+(node.loading?"true":"false")+",\"isolated\":"+(ready?"true":"false")+",\"separate_process\":"+(!node.session.empty()?"true":"false")+"}";
      }
      finish("{\"ok\":true,\"complete\":"+std::string(complete?"true":"false")+",\"frames\":["+rows+"]}");return;
    }
    const auto source=sources[next++];std::weak_ptr<xb_frame_job> weak=shared_from_this();
    call(source,L"Page.getFrameTree","{}",[weak,source](bool ok,const std::string& result){auto self=weak.lock();if(!self)return;
      if(!ok){self->complete=false;self->collect();return;}
      if(!self->tree(xb_get(result,"frameTree"),source,"")){self->finish("","frame_tree_invalid");return;}self->collect();
    });
  }
  bool selected_live(){
    if(!live())return false;auto current=state.lock();auto node=xb_frame_alias(current,alias);
    if(!node||node->raw!=selected||node->document!=document||node->session!=session||node->loading){finish("","stale_frame");return false;}return true;
  }
  bool owner_live(){
    if(!selected_live())return false;auto current=state.lock();auto child=current->nodes.find(selected),parent=current->nodes.find(parentRaw);
    if(child==current->nodes.end()||child->second.parent!=parentRaw||parent==current->nodes.end()||parent->second.document!=parentDocument||
       parent->second.session!=parentSession||parent->second.loading){finish("","stale_frame");return false;}return true;
  }
  void owner_geometry(){
    auto current=state.lock();auto child=current->nodes.find(selected);auto parent=current->nodes.find(child->second.parent);
    if(parent==current->nodes.end()){finish("","root_has_no_frame_owner");return;}
    parentRaw=parent->second.raw;parentAlias=parent->second.id;parentDocument=parent->second.document;parentSession=parent->second.session;parentLoader=parent->second.loader;
    if(!parentSession.empty()&&(!current->sessions.count(parentSession)||!current->sessions.at(parentSession).ready)){finish("","frame_session_unavailable");return;}
    if(!owner_live())return;std::weak_ptr<xb_frame_job> weak=shared_from_this();
    call(parentSession,L"DOM.getFrameOwner","{\"frameId\":"+xb_quote(selected)+"}",[weak](bool ok,const std::string& result){
      auto self=weak.lock();if(!self||!self->owner_live())return;int backend=0;try{backend=std::stoi(xb_get(result,"backendNodeId"));}catch(...){}
      if(!ok||backend<=0){self->finish("","frame_owner_unavailable");return;}
      xb_world_frame(self->host.lock(),self->view.lock(),self->navigation,self->parentSession,self->parentRaw,self->parentLoader,
        [weak](){auto self=weak.lock();return self&&self->owner_live();},[weak,backend](std::shared_ptr<xb_world_context> world){
          auto self=weak.lock();if(!self||!self->owner_live())return;
          if(!xb_world_live(self->view.lock(),world)){self->finish("","frame_world_unavailable");return;}self->world=world;
          self->objectGroup="mdo-frame-owner-"+self->id;
          self->call(self->parentSession,L"DOM.resolveNode","{\"backendNodeId\":"+std::to_string(backend)+",\"executionContextId\":"+std::to_string(world->numeric)+",\"objectGroup\":"+xb_quote(self->objectGroup)+"}",
            [weak](bool ready,const std::string& result){
              auto self=weak.lock();if(!self||!self->owner_live())return;const auto object=xb_get(xb_get(result,"object"),"objectId");
              if(!ready||object.empty()||object.size()>1024||!xb_world_live(self->view.lock(),self->world)){self->finish("","frame_owner_unavailable");return;}
              self->evaluationSent=true;
              const bool policy=self->action=="upload_policy"||self->action=="capture_owner";
              const auto function=self->action=="capture_owner"?xb_frame_owner_capture:self->action=="upload_policy"?xb_frame_owner_upload_policy:xb_frame_owner_geometry;
              const auto args=policy?std::string("[]"):"[{\"value\":{\"x\":"+std::to_string(self->pointX)+",\"y\":"+std::to_string(self->pointY)+"}}]";
              xb_world_function(self->view.lock(),self->world,xb_world_node_params(self->world,object,function,args),[weak](){auto self=weak.lock();return self&&self->owner_live();},
                [weak](bool ok,const std::string& result){
                  auto self=weak.lock();if(!self||!self->owner_live())return;const auto value=xb_cdp_value(xb_get(result,"result"));
                  if(!ok||!xb_get(result,"exceptionDetails").empty()||value.empty()){self->finish("","frame_owner_unavailable");return;}
                  self->finish("{\"ok\":true,\"frame\":"+xb_quote(self->alias)+",\"frame_document\":"+xb_quote(self->document)+",\"parent\":"+xb_quote(self->parentAlias)+
                    ",\"parent_document\":"+xb_quote(self->parentDocument)+",\"value\":"+value+"}");
                });
            });
        });
    });
  }
  void evaluate(){
    auto current=state.lock();auto node=xb_frame_alias(current,alias);
    if(!node||(!expected.empty()&&node->document!=expected)||node->loading){finish("","stale_frame");return;}
    selected=node->raw;document=node->document;session=node->session;
    if(!session.empty()&&(!current->sessions.count(session)||!current->sessions.at(session).ready)){finish("","frame_session_unavailable");return;}
    if(action=="owner"||action=="upload_policy"||action=="capture_owner"){owner_geometry();return;}
    std::weak_ptr<xb_frame_job> weak=shared_from_this();
    xb_world_frame(host.lock(),view.lock(),navigation,session,selected,node->loader,[weak](){auto self=weak.lock();return self&&self->selected_live();},[weak](std::shared_ptr<xb_world_context> world){
      auto self=weak.lock();if(!self||!self->selected_live())return;
      if(!xb_world_live(self->view.lock(),world)){self->finish("","frame_world_unavailable");return;}self->world=world;
      self->evaluationSent=true;
      self->call(self->session,L"Runtime.evaluate","{\"expression\":"+xb_quote(self->script)+","+xb_world_selector(world)+",\"returnByValue\":true,\"awaitPromise\":false,\"timeout\":3000}",
        [weak](bool ready,const std::string& value){auto self=weak.lock();if(!self||!self->selected_live())return;
          const auto result=xb_cdp_value(xb_get(value,"result"));
          if(!ready||!xb_world_live(self->view.lock(),self->world)||!xb_get(value,"exceptionDetails").empty()||result.empty()){self->finish("","frame_evaluation_failed");return;}
          self->finish("{\"ok\":true,\"frame\":"+xb_quote(self->alias)+",\"frame_document\":"+xb_quote(self->document)+",\"value\":"+result+"}");
        });
    });
  }
  void start(){
    if(started||!live())return;auto current=state.lock();if(!current->error.empty()){finish("",current->error.c_str());return;}if(!current->ready)return;
    started=true;sources.push_back("");for(const auto& item:current->sessions)if(item.second.ready)sources.push_back(item.first);collect();
  }
};
static void CALLBACK xb_frames_expire(HWND window,UINT,UINT_PTR,DWORD){
  auto found=xb_frame_views.find(window);if(found==xb_frame_views.end()){KillTimer(window,xb_frames_timer);return;}
  auto view=found->second.lock();if(!view||!view->frames){KillTimer(window,xb_frames_timer);xb_frame_views.erase(found);return;}
  auto state=view->frames;auto jobs=state->jobs;for(auto& item:jobs)if(item.second->live())item.second->start();
  if(view->frames==state&&state->jobs.empty()){KillTimer(window,xb_frames_timer);xb_frame_views.erase(window);}
}
static void xb_frames_request(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,const std::string& id,const std::string& json){
  if(!xb_automation_live(host,view,view->navigation)){host->reply(id,"{\"ok\":false,\"error\":{\"code\":\"frame_interrupted\"}}");return;}
  try{
    if(!xb_frames_install(view)){host->reply(id,"{\"ok\":false,\"error\":{\"code\":\"unsupported_frame_sessions\"}}");return;}
    auto state=view->frames;const auto action=xb_get(json,"action");
    if((action!="list"&&action!="evaluate"&&action!="owner"&&action!="upload_policy"&&action!="capture_owner")||id.empty()||id.size()>128||state->jobs.size()>=16||state->jobs.count(id)){host->reply(id,"{\"ok\":false,\"error\":{\"code\":\"frame_request_invalid\"}}");return;}
    auto job=std::make_shared<xb_frame_job>();job->host=host;job->view=view;job->state=state;job->id=id;job->action=action;
    job->alias=xb_get(json,"frame");job->expected=xb_get(json,"document");job->script=xb_get(json,"script");
    if(action=="evaluate"&&(job->alias.empty()||job->alias.size()>64||job->expected.size()>64||job->script.empty()||job->script.size()>131072)){host->reply(id,"{\"ok\":false,\"error\":{\"code\":\"frame_request_invalid\"}}");return;}
    if((action=="upload_policy"||action=="capture_owner")&&(job->alias.empty()||job->alias.size()>64||job->expected.empty()||job->expected.size()>64)){host->reply(id,"{\"ok\":false,\"error\":{\"code\":\"frame_request_invalid\"}}");return;}
    if(action=="owner"){
      try{const auto point=xb_get(json,"point");job->pointX=std::stod(xb_get(point,"x"));job->pointY=std::stod(xb_get(point,"y"));}catch(...){host->reply(id,"{\"ok\":false,\"error\":{\"code\":\"frame_point_invalid\"}}");return;}
      if(job->alias.empty()||job->alias.size()>64||job->expected.empty()||job->expected.size()>64||!std::isfinite(job->pointX)||!std::isfinite(job->pointY)||
         job->pointX<0||job->pointY<0||job->pointX>32768||job->pointY>32768){host->reply(id,"{\"ok\":false,\"error\":{\"code\":\"frame_point_invalid\"}}");return;}
    }
    job->navigation=view->navigation;job->generation=state->generation;job->deadline=GetTickCount64()+10000;state->jobs.emplace(id,job);xb_frame_views[view->window]=view;
    if(!SetTimer(view->window,xb_frames_timer,20,xb_frames_expire)){job->finish("","frame_timer_unavailable");return;}job->start();
  }catch(...){host->reply(id,"{\"ok\":false,\"error\":{\"code\":\"frame_allocation_failed\"}}");}
}
static void xb_frames_navigation(const std::shared_ptr<xb_view>& view){
  if(!view->frames)return;auto state=view->frames;state->generation++;auto jobs=state->jobs;
  for(auto& item:jobs)item.second->finish("","stale_frame");state->previousNodes=std::move(state->nodes);state->nodes.clear();state->loading.clear();state->root.clear();
  if(state->error=="frame_limit"||state->error=="frame_session_limit"||state->error=="frame_event_limit"||state->error=="frame_identity_unavailable")state->error.clear();
}
static void xb_frames_close(const std::shared_ptr<xb_view>& view){
  xb_worlds_close(view);
  if(!view->frames)return;auto state=view->frames;state->closed=true;auto jobs=state->jobs;
  for(auto& item:state->sessions)item.second.identity->alive=false;
  for(auto& item:jobs)item.second->finish("","frame_interrupted");state->receivers.clear();view->frames.reset();
  KillTimer(view->window,xb_frames_timer);xb_frame_views.erase(view->window);
}
