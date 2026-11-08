/* Original-world admission. Runtime events, the creation reply and a negative and
 * positive unique-ID probe must agree before any caller script can execute.
 * No provider fallback may turn a unique-world failure into numeric execution. */
static constexpr UINT_PTR xb_world_timer=0xBADD9;
static std::map<HWND,std::weak_ptr<xb_view>> xb_world_views;
using xb_world_result=std::function<void(std::shared_ptr<xb_world_context>)>;
static bool xb_world_source_live(const std::shared_ptr<xb_view>& view,const std::string& session,const std::shared_ptr<xb_frame_source_identity>& owner){
  if(session.empty())return !owner;auto state=view->frames;
  if(!state||state->closed||!owner||!owner->alive)return false;
  auto at=state->sessions.find(session);return at!=state->sessions.end()&&at->second.identity==owner;
}
static void xb_world_protocol(const std::shared_ptr<xb_view>& view,const std::string& session,const std::shared_ptr<xb_frame_source_identity>& owner,
                              const wchar_t* method,const std::string& params,xb_cdp_result complete){
  if(!view||view->closed||!view->core||!xb_world_source_live(view,session,owner)){complete(false,"{}");return;}
  xb_frame_cdp(view,session,method,params,std::move(complete));
}

// BROWSER_WORLD_REGISTRY_BEGIN
static int xb_world_number(const std::string& text){
  if(text.empty()||text.size()>10)return 0;uint64_t value=0;
  for(char digit:text){if(digit<'0'||digit>'9')return 0;value=value*10+digit-'0';if(value>INT32_MAX)return 0;}
  return static_cast<int>(value);
}
static bool xb_world_live(const std::shared_ptr<xb_view>& view,const std::shared_ptr<xb_world_context>& pin){
  if(!view||view->closed||!view->core||!view->worlds||view->worlds->closed||!view->worlds->error.empty()||!pin||!pin->alive||!pin->registered||!pin->admitted||!xb_world_source_live(view,pin->session,pin->sourceOwner))return false;
  auto found=view->worlds->names.find(pin->name);return found!=view->worlds->names.end()&&found->second.lock()==pin;
}
static bool xb_world_cleanup_live(const std::shared_ptr<xb_view>& view,const std::shared_ptr<xb_world_context>& pin){
  // Runtime reset/restore can clear reported contexts while the original JS
  // world survives. Only fixed keyed observer disposal may use that retained
  // unique pin; metadata loss never authorizes a new effect or proves disposal.
  return xb_world_live(view,pin)||view&&!view->closed&&view->core&&view->worlds&&!view->worlds->closed&&view->worlds->error.empty()&&
    pin&&pin->inventoryLost&&!pin->retired&&pin->registered&&pin->admitted&&xb_world_source_live(view,pin->session,pin->sourceOwner);
}
static void xb_world_metadata(const std::shared_ptr<xb_world_state>& state,const std::string& method,const std::string& data,const std::string& session=""){
  if(state->closed||!state->error.empty())return;
  if(method=="Runtime.executionContextCreated"){
    const auto context=xb_get(data,"context"),name=xb_get(context,"name");auto at=state->names.find(name);
    if(at==state->names.end())return;auto pin=at->second.lock();if(!pin||!pin->alive||pin->session!=session)return;
    const auto aux=xb_get(context,"auxData"),unique=xb_get(context,"uniqueId");const auto numeric=xb_world_number(xb_get(context,"id"));
    const auto frame=xb_get(aux,"frameId");
    if(!numeric||unique.empty()||unique.size()>256||frame.empty()||frame.size()>128||xb_get(aux,"isDefault")!="false"||xb_get(aux,"type")!="isolated"){
      pin->alive=false;state->error="world_context_metadata_invalid";return;
    }
    // Accessing a same-origin child WindowProxy may instantiate this named
    // isolated world in that child as well. Its native identity grants no
    // authority over the requested root and must not overwrite that pin.
    if(frame!=pin->frame){if(state->otherFrames<1000000)state->otherFrames++;return;}
    if(pin->registered){
      if(pin->numeric==numeric&&pin->unique==unique){if(state->refreshes<1000000)state->refreshes++;return;}
      pin->alive=false;state->error="world_context_metadata_rebound";return;
    }
    if(state->seen.count(unique)||state->seen.size()>=1024){pin->alive=false;state->error="world_context_metadata_invalid";return;}
    // Reuse of a numeric renderer-local ID is legitimate after process swaps.
    // Retire its previous object; never rewrite or revive an original pin.
    for(auto& item:state->names)if(auto previous=item.second.lock())if(previous!=pin&&previous->registered&&previous->session==session&&previous->sourceOwner==pin->sourceOwner&&previous->numeric==numeric)previous->alive=false;
    state->seen.emplace(unique,true);pin->numeric=numeric;pin->unique=unique;pin->registered=true;return;
  }
  const auto unique=xb_get(data,"executionContextUniqueId");const int numeric=xb_world_number(xb_get(data,"executionContextId"));
  for(auto& item:state->names)if(auto pin=item.second.lock()){
    if(pin->session!=session)continue;
    if(method=="Runtime.executionContextsCleared"){pin->alive=false;pin->inventoryLost=true;}
    else if(method=="Runtime.executionContextDestroyed"&&(!unique.empty()?pin->unique==unique:numeric&&pin->registered&&pin->numeric==numeric)){pin->alive=false;pin->retired=true;}
  }
}
static std::string xb_world_selector(const std::shared_ptr<xb_world_context>& pin){return "\"uniqueContextId\":"+xb_quote(pin->unique);}
static bool xb_world_negative_proof(bool ok,const std::string& result){
  const auto error=xb_get(result,"error"),message=xb_get(error,"message");
  return !ok&&xb_get(error,"code")=="-32602"&&(message=="invalid uniqueContextId"||message=="uniqueContextId not found");
}
static bool xb_world_tree_visit(const std::string& tree,const std::string& raw,const std::string& loader,unsigned depth,unsigned& count,bool& found){
  if(depth>=32||++count>64)return false;const auto frame=xb_get(tree,"frame"),id=xb_get(frame,"id");if(id.empty()||id.size()>128)return false;
  if(id==raw){if(found||xb_get(frame,"loaderId")!=loader)return false;found=true;}
  const auto children=xb_get(tree,"childFrames");if(!children.empty()&&children.front()!='[')return false;
  for(int index=0;index<65;index++){const auto child=webview::detail::json_parse(children,"",index);if(child.empty())break;if(!xb_world_tree_visit(child,raw,loader,depth+1,count,found))return false;}
  return true;
}
static bool xb_world_tree_matches(const std::string& reply,const std::string& raw,const std::string& loader){
  unsigned count=0;bool found=false;return !loader.empty()&&xb_world_tree_visit(xb_get(reply,"frameTree"),raw,loader,0,count,found)&&found;
}
// BROWSER_WORLD_REGISTRY_END

static bool xb_worlds_install(const std::shared_ptr<xb_view>& view){
  if(view->worlds)return !view->worlds->closed&&view->worlds->error.empty();
  auto state=std::make_shared<xb_world_state>();view->worlds=state;state->error="world_events_unavailable";
  for(const auto* event:{L"Runtime.executionContextCreated",L"Runtime.executionContextDestroyed",L"Runtime.executionContextsCleared"}){
    auto receiver=std::make_shared<xb_world_receiver>();
    if(FAILED(view->core->GetDevToolsProtocolEventReceiver(event,&receiver->source))||!receiver->source){state->error="world_events_unavailable";return false;}
    const auto method=webview::detail::narrow_string(event);std::weak_ptr<xb_view> weak=view;std::weak_ptr<xb_world_state> held=state;
    auto* handler=xb_cb<ICoreWebView2DevToolsProtocolEventReceivedEventHandler,ICoreWebView2*,ICoreWebView2DevToolsProtocolEventReceivedEventArgs*>(
      IID_ICoreWebView2DevToolsProtocolEventReceivedEventHandler,[weak,held,method](ICoreWebView2*,ICoreWebView2DevToolsProtocolEventReceivedEventArgs* args){
        auto view=weak.lock();auto state=held.lock();if(!view||!state||view->closed||view->worlds!=state||state->closed)return S_OK;
        xs_wv_com<ICoreWebView2DevToolsProtocolEventReceivedEventArgs2> routed;xs_wv_text session,text;
        if(FAILED(args->QueryInterface(IID_ICoreWebView2DevToolsProtocolEventReceivedEventArgs2,reinterpret_cast<void**>(routed.out())))||FAILED(routed->get_SessionId(&session.p))){state->error="world_event_routing_unavailable";return S_OK;}
        if(FAILED(args->get_ParameterObjectAsJson(&text.p))){state->error="world_event_metadata_unavailable";return S_OK;}
        size_t length=0;if(text.p)while(length<=32768&&text.p[length])length++;
        if(length>32768){state->error="world_event_limit";return S_OK;}
        xb_world_metadata(state,method,webview::detail::narrow_string(text.p?text.p:L"{}"),webview::detail::narrow_string(session.p?session.p:L""));return S_OK;
      });
    receiver->subscribed=SUCCEEDED(receiver->source->add_DevToolsProtocolEventReceived(handler,&receiver->token));handler->Release();
    if(!receiver->subscribed){state->error="world_events_unavailable";return false;}state->receivers.push_back(receiver);
  }
  state->error.clear();return true;
}
struct xb_world_request:std::enable_shared_from_this<xb_world_request>{
  std::weak_ptr<xb_host> host;std::weak_ptr<xb_view> view;std::weak_ptr<xb_world_state> state;
  std::shared_ptr<xb_world_context> pin;std::vector<xb_world_result> waiters;
  UINT64 navigation=0;ULONGLONG deadline=0;int replyId=0;bool done=false,proving=false,creating=false;
  bool local=false,cleanup=false;std::string key,session,raw,loader,purpose;
  std::shared_ptr<xb_frame_source_identity> sourceOwner;std::function<bool()> guard;
  void protocol(const wchar_t* method,const std::string& params,xb_cdp_result complete){xb_world_protocol(view.lock(),session,sourceOwner,method,params,std::move(complete));}
  bool live()const{
    auto owner=host.lock();auto active=view.lock();auto current=state.lock();
    return !done&&owner&&active&&current&&active->worlds==current&&!current->closed&&current->error.empty()&&
      (local?current->framePending.count(key)&&current->framePending.at(key).get()==this:current->pending.get()==this)&&
      (cleanup?owner->alive&&!active->closed&&!active->failed&&active->navigation==navigation&&!active->dialog&&!xb_popup_blocked(active):xb_automation_live(owner,active,navigation))&&
      xb_world_source_live(active,session,sourceOwner)&&(!guard||guard())&&GetTickCount64()<deadline&&(!pin||pin->alive);
  }
  void finish(bool admitted,const char* error=""){
    if(done)return;auto active=view.lock();auto current=state.lock();
    if(admitted&&local)try{current->frameWorlds[key]=pin;}catch(...){admitted=false;}
    done=true;
    if(!admitted&&pin)pin->alive=false;
    if(active&&current&&active->worlds==current&&(local?current->framePending.count(key)&&current->framePending.at(key).get()==this:current->pending.get()==this)){
      if(local)current->framePending.erase(key);else{current->pending.reset();current->enabling=false;}
      if(admitted){pin->admitted=true;if(!local){current->original=pin;active->automationWorld=pin;active->automationContext=pin->numeric;}}
      if(*error)current->error=error;
      if(!current->pending&&current->framePending.empty()){KillTimer(active->window,xb_world_timer);xb_world_views.erase(active->window);}
    }
    guard=nullptr;auto callbacks=std::move(waiters);for(auto& callback:callbacks)try{callback(admitted?pin:nullptr);}catch(...){/* A delivery failure cannot replay another caller. */}
  }
  void qualify(){
    if(proving||!replyId||!pin||!pin->registered)return;
    if(!live()||pin->numeric!=replyId){finish(false);return;}proving=true;
    auto self=shared_from_this();
    const auto proof=std::string("{\"expression\":\"void 0\",\"returnByValue\":true,\"silent\":true,\"throwOnSideEffect\":true,\"timeout\":1000,");
    // An empty unique ID must be rejected, not ignored in favor of a default
    // main-world evaluation. Probe contains no caller/page script or side effect.
    protocol(L"Runtime.evaluate",proof+"\"uniqueContextId\":\"\"}",[self,proof](bool ok,const std::string& result){
      if(!self->live()){self->finish(false);return;}
      if(!xb_world_negative_proof(ok,result)){self->finish(false,"world_unique_context_unsupported");return;}
      self->protocol(L"Runtime.evaluate",proof+xb_world_selector(self->pin)+"}",[self](bool ok,const std::string& result){
        if(!self->live()){self->finish(false);return;}
        if(!ok||!xb_get(result,"error").empty()||!xb_get(result,"exceptionDetails").empty()||xb_get(xb_get(result,"result"),"type")!="undefined"){self->finish(false,"world_unique_context_unconfirmed");return;}
        if(!self->local){self->finish(true);return;}
        // The child may have navigated before its event reaches the UI thread.
        // A fresh name/unique context alone could then identify a new document.
        // Correlate the newly created world with the originally captured native
        // loader before admitting caller code. Later changes still address only
        // that original unique world, never the replacement numeric context.
        self->protocol(L"Page.getFrameTree","{}",[self](bool ok,const std::string& tree){
          if(!self->live()||!ok||!xb_world_tree_matches(tree,self->raw,self->loader)){self->finish(false);return;}self->finish(true);
        });
      });
    });
  }
  void createIn(const std::string& frame,const std::string& nativeLoader){
      auto self=shared_from_this();if(!live()||frame.empty()||frame.size()>128||nativeLoader.size()>128){finish(false);return;}
      auto current=state.lock();
      // NavigationStarting can deliver a response/download without replacing
      // the DOM. Reuse only the exact still-live original native world after a
      // fresh nonempty loader/frame match; never infer survival from the URL.
      auto at=current->frameWorlds.find(key);auto original=local?(at==current->frameWorlds.end()?nullptr:at->second):current->original;
      if(original&&xb_world_live(view.lock(),original)&&original->frame==frame&&!nativeLoader.empty()&&original->loader==nativeLoader){
        self->pin=original;self->replyId=original->numeric;self->qualify();return;
      }
      for(auto item=current->names.begin();item!=current->names.end();)if(item->second.expired())item=current->names.erase(item);else ++item;
      unsigned* count=&current->documentWorlds;
      if(local){
        const auto docKey=xb_quote(session)+"/"+frame;
        if(!current->frameDocuments.count(docKey)&&current->frameDocuments.size()>=64){finish(false,"world_identity_limit");return;}
        auto& document=current->frameDocuments[docKey];document.frame=frame;document.session=session;if(!nativeLoader.empty()&&document.loader!=nativeLoader){document.loader=nativeLoader;document.created=0;}count=&document.created;
      }else{const auto proof=frame+"\n"+nativeLoader;if(!nativeLoader.empty()&&current->documentProof!=proof){current->documentProof=proof;current->documentWorlds=0;}}
      if(current->created>=1024||current->names.size()>=128||*count>=32){self->finish(false,"world_identity_limit");return;}
      GUID guid{};wchar_t text[40]{};if(FAILED(CoCreateGuid(&guid))||!StringFromGUID2(guid,text,40)){self->finish(false);return;}
      self->pin=std::make_shared<xb_world_context>();self->pin->frame=frame;self->pin->loader=nativeLoader;self->pin->session=session;self->pin->sourceOwner=sourceOwner;
      self->pin->name="mdo.browser-use."+(local?purpose:std::string("root"))+"."+webview::detail::narrow_string(text);current->created++;(*count)++;
      current->names.emplace(self->pin->name,self->pin);
      protocol(L"Page.createIsolatedWorld","{\"frameId\":"+xb_quote(frame)+",\"worldName\":"+xb_quote(self->pin->name)+",\"grantUniveralAccess\":false}",
        [self](bool ok,const std::string& result){
          if(!self->live()){self->finish(false);return;}self->replyId=xb_world_number(xb_get(result,"executionContextId"));
          if(!ok||!self->replyId){self->finish(false);return;}self->qualify();
        });
  }
  void create(){
    if(creating)return;creating=true;auto self=shared_from_this();
    if(local){createIn(raw,loader);return;}
    protocol(L"Page.getFrameTree","{}",[self](bool ok,const std::string& tree){
      if(!self->live()){self->finish(false);return;}
      const auto node=xb_get(xb_get(tree,"frameTree"),"frame");
      if(!ok){self->finish(false);return;}self->createIn(xb_get(node,"id"),xb_get(node,"loaderId"));
    });
  }
};
static void CALLBACK xb_world_tick(HWND window,UINT,UINT_PTR,DWORD){
  auto at=xb_world_views.find(window);if(at==xb_world_views.end()){KillTimer(window,xb_world_timer);return;}
  auto view=at->second.lock();if(!view||!view->worlds){KillTimer(window,xb_world_timer);xb_world_views.erase(at);return;}
  auto state=view->worlds;std::shared_ptr<xb_world_request> jobs[33];size_t count=0;
  if(state->pending)jobs[count++]=state->pending;
  for(const auto& item:state->framePending)if(count<33)jobs[count++]=item.second;
  if(!count){KillTimer(window,xb_world_timer);xb_world_views.erase(at);return;}
  for(size_t index=0;index<count;index++){auto request=jobs[index];try{if(!request->live())request->finish(false);else request->qualify();}catch(...){request->finish(false);}}
}
static void xb_world_root(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,UINT64 navigation,xb_world_result complete){
  std::shared_ptr<xb_world_request> request;
  try{
  if(!xb_automation_live(host,view,navigation)||!xb_worlds_install(view)){auto callback=std::move(complete);callback(nullptr);return;}
  if(xb_world_live(view,view->automationWorld)){auto callback=std::move(complete);callback(view->automationWorld);return;}
  auto state=view->worlds;
  if(state->pending&&state->pending->navigation!=navigation){auto previous=state->pending;previous->finish(false);}
  if(state->pending){if(state->pending->waiters.size()>=32){auto callback=std::move(complete);callback(nullptr);return;}state->pending->waiters.push_back(complete);return;}
  request=std::make_shared<xb_world_request>();request->host=host;request->view=view;request->state=state;
  request->navigation=navigation;request->deadline=GetTickCount64()+3000;request->waiters.push_back(complete);complete=nullptr;state->pending=request;
  xb_world_views[view->window]=view;
  if(!SetTimer(view->window,xb_world_timer,20,xb_world_tick)){request->finish(false);return;}
  if(state->enabled){request->create();return;}state->enabling=true;
  request->protocol(L"Runtime.enable","{}",[request](bool ok,const std::string& result){
    if(!request->live()){request->finish(false);return;}
    auto state=request->state.lock();state->enabling=false;
    if(!ok||!xb_get(result,"error").empty()){request->finish(false,"world_runtime_unavailable");return;}
    state->enabled=true;request->create();
  });
  }catch(...){if(request&&!request->waiters.empty())request->finish(false);else if(complete){auto callback=std::move(complete);try{callback(nullptr);}catch(...){}}}
}
static std::string xb_world_frame_key(const std::string& session,const std::string& frame,const std::string& purpose){return xb_quote(session)+"/"+xb_quote(frame)+"/"+purpose;}
static void xb_world_frame(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,UINT64 navigation,
                           const std::string& session,const std::string& raw,const std::string& loader,std::function<bool()> guard,
                           xb_world_result complete,const std::string& purpose="frames",bool cleanup=false){
  std::shared_ptr<xb_world_request> request;
  try{
    const bool controlled=host&&view&&(cleanup?host->alive&&!view->closed&&!view->failed&&view->navigation==navigation&&!view->dialog&&!xb_popup_blocked(view):xb_automation_live(host,view,navigation));
    if(!controlled||raw.empty()||raw.size()>128||loader.empty()||loader.size()>128||(purpose!="frames"&&purpose!="chooser")||!guard||!guard()||!xb_worlds_install(view)){
      auto callback=std::move(complete);callback(nullptr);return;
    }
    std::shared_ptr<xb_frame_source_identity> sourceOwner;
    if(!session.empty()){
      if(!view->frames||!view->frames->sessions.count(session)){auto callback=std::move(complete);callback(nullptr);return;}
      sourceOwner=view->frames->sessions.at(session).identity;
    }
    if(!xb_world_source_live(view,session,sourceOwner)){auto callback=std::move(complete);callback(nullptr);return;}
    auto state=view->worlds;const auto key=xb_world_frame_key(session,raw,purpose);
    for(auto item=state->frameWorlds.begin();item!=state->frameWorlds.end();)if(!xb_world_live(view,item->second))item=state->frameWorlds.erase(item);else ++item;
    auto original=state->frameWorlds.find(key);
    if(original!=state->frameWorlds.end()&&original->second->loader==loader){auto callback=std::move(complete);callback(original->second);return;}
    auto pending=state->framePending.find(key);
    if(pending!=state->framePending.end()){
      auto previous=pending->second;
      if(previous->navigation!=navigation||previous->loader!=loader||previous->sourceOwner!=sourceOwner||!previous->live()){previous->finish(false);pending=state->framePending.end();}
      else{if(previous->waiters.size()>=32){auto callback=std::move(complete);callback(nullptr);return;}previous->waiters.push_back(complete);return;}
    }
    if(state->framePending.size()>=32||state->frameWorlds.size()>=128){auto callback=std::move(complete);callback(nullptr);return;}
    request=std::make_shared<xb_world_request>();request->host=host;request->view=view;request->state=state;request->navigation=navigation;
    request->local=true;request->cleanup=cleanup;request->key=key;request->raw=raw;request->loader=loader;request->session=session;request->purpose=purpose;request->sourceOwner=sourceOwner;
    request->guard=std::move(guard);request->deadline=GetTickCount64()+3000;request->waiters.push_back(complete);complete=nullptr;state->framePending.emplace(key,request);
    xb_world_views[view->window]=view;if(!SetTimer(view->window,xb_world_timer,20,xb_world_tick)){request->finish(false);return;}
    if(!request->live()){request->finish(false);return;}
    const bool enabled=session.empty()?state->enabled:state->enabledSources.count(session)&&state->enabledSources.at(session).lock()==sourceOwner;
    if(enabled){request->create();return;}
    request->protocol(L"Runtime.enable","{}",[request](bool ok,const std::string& result){
      if(!request->live()){request->finish(false);return;}
      if(!ok||!xb_get(result,"error").empty()){request->finish(false,"world_runtime_unavailable");return;}
      auto state=request->state.lock();if(request->session.empty())state->enabled=true;else state->enabledSources[request->session]=request->sourceOwner;request->create();
    });
  }catch(...){if(request&&!request->waiters.empty())request->finish(false);else if(complete){auto callback=std::move(complete);try{callback(nullptr);}catch(...){}}}
}
static void xb_world_retire_frame(const std::shared_ptr<xb_view>& view,const std::string& raw,const std::string& loader=""){
  auto state=view->worlds;if(!state)return;
  for(auto& item:state->names)if(auto pin=item.second.lock())if(pin->frame==raw&&(loader.empty()||!pin->loader.empty()&&pin->loader!=loader)){pin->alive=false;pin->retired=true;}
  for(auto item=state->frameWorlds.begin();item!=state->frameWorlds.end();)if(item->second->retired)item=state->frameWorlds.erase(item);else ++item;
  for(auto item=state->frameDocuments.begin();item!=state->frameDocuments.end();){
    if(item->second.frame==raw&&(loader.empty()||!item->second.loader.empty()&&item->second.loader!=loader))item=state->frameDocuments.erase(item);else ++item;
  }
}
static void xb_world_retire_source(const std::shared_ptr<xb_view>& view,const std::string& session,const std::shared_ptr<xb_frame_source_identity>& owner){
  if(owner)owner->alive=false;auto state=view->worlds;if(!state)return;state->enabledSources.erase(session);
  for(auto& item:state->names)if(auto pin=item.second.lock())if(pin->session==session&&pin->sourceOwner==owner){pin->alive=false;pin->retired=true;}
  for(auto item=state->frameWorlds.begin();item!=state->frameWorlds.end();)if(item->second->retired)item=state->frameWorlds.erase(item);else ++item;
  const auto prefix=xb_quote(session)+"/";for(auto item=state->frameDocuments.begin();item!=state->frameDocuments.end();)if(item->first.rfind(prefix,0)==0)item=state->frameDocuments.erase(item);else ++item;
}
static std::string xb_world_node_params(const std::shared_ptr<xb_world_context>& pin,const std::string& object,const std::string& function,
                                        const std::string& arguments="[]",bool awaitPromise=false,const std::string& nodeGuard="node instanceof Element&&node.ownerDocument===document"){
  const auto body="function(node,...args){if(!("+nodeGuard+"))throw Error('Original node world changed');return ("+function+").apply(node,args)}";
  const auto args="[{\"objectId\":"+xb_quote(object)+"}"+(arguments=="[]"?std::string("]"):std::string(",")+arguments.substr(1));
  return "{\"functionDeclaration\":"+xb_quote(body)+","+xb_world_selector(pin)+",\"arguments\":"+args+",\"returnByValue\":true,\"awaitPromise\":"+(awaitPromise?"true":"false")+"}";
}
static void xb_world_function(const std::shared_ptr<xb_view>& view,const std::shared_ptr<xb_world_context>& pin,const std::string& params,
                               std::function<bool()> guard,xb_cdp_result complete){
  const auto live=[view,pin,guard](){return xb_world_live(view,pin)&&guard&&guard();};
  if(!live()){complete(false,"{}");return;}
  const auto execute=[view,pin,params,live,complete](){
    if(!live()){complete(false,"{}");return;}
    xb_world_protocol(view,pin->session,pin->sourceOwner,L"Runtime.callFunctionOn",params,[live,complete](bool ok,const std::string& result){complete(ok&&live(),result);});
  };
  if(pin->functions){execute();return;}
  const auto proof=std::string("{\"functionDeclaration\":\"function(){return 0;}\",\"returnByValue\":true,\"silent\":true,\"throwOnSideEffect\":true,");
  xb_world_protocol(view,pin->session,pin->sourceOwner,L"Runtime.callFunctionOn",proof+"\"uniqueContextId\":\"\"}",[view,pin,proof,live,complete,execute](bool ok,const std::string& result){
    if(!live()||!xb_world_negative_proof(ok,result)){complete(false,"{}");return;}
    xb_world_protocol(view,pin->session,pin->sourceOwner,L"Runtime.callFunctionOn",proof+xb_world_selector(pin)+"}",[pin,live,complete,execute](bool ok,const std::string& result){
      if(!live()||!ok||!xb_get(result,"error").empty()||!xb_get(result,"exceptionDetails").empty()||xb_get(xb_get(result,"result"),"type")!="number"||xb_get(xb_get(result,"result"),"value")!="0"){complete(false,"{}");return;}
      pin->functions=true;execute();
    });
  });
}
static void xb_worlds_close(const std::shared_ptr<xb_view>& view){
  if(!view->worlds)return;auto state=view->worlds;state->closed=true;
  for(auto& item:state->names)if(auto pin=item.second.lock())pin->alive=false;
  if(state->pending){auto pending=state->pending;pending->finish(false);}
  auto jobs=state->framePending;for(auto& item:jobs)item.second->finish(false);
  state->frameWorlds.clear();state->frameDocuments.clear();state->enabledSources.clear();
  state->receivers.clear();state->original.reset();view->automationWorld.reset();view->automationContext=0;
  KillTimer(view->window,xb_world_timer);xb_world_views.erase(view->window);
}
