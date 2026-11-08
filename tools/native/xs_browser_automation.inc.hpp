/* Included in the native browser namespace. CDP remains private to the trusted
 * application document. Models receive typed operations, never CDP/script. */
using xb_cdp_result = std::function<void(bool,const std::string&)>;
// json_parse() decodes JSON strings. Replies must retain the raw value token:
// an empty string is a value, and quotes/escapes must survive the outer packet.
static std::string xb_cdp_value(const std::string& remote){
  const char* value=nullptr;size_t size=0;
  webview::detail::json_parse_c(remote.c_str(),remote.size(),"value",5,&value,&size);
  return value?std::string(value,size):std::string{};
}
static bool xb_popup_blocked(const std::shared_ptr<xb_view>& view){
  for(const auto& item:view->popups)if(!item.second->completed)return true;return false;
}
static void xb_cdp(const std::shared_ptr<xb_view>& view,const wchar_t* method,
                   const std::string& params,xb_cdp_result done){
  xs_wv_com<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler> callback;
  callback.p=xb_cb<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,HRESULT,LPCWSTR>(
    IID_ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,
    [done](HRESULT status,LPCWSTR output){
      done(SUCCEEDED(status),webview::detail::narrow_string(output?output:L"{}"));return S_OK;
    });
  HRESULT status=view->core->CallDevToolsProtocolMethod(method,webview::detail::widen_string(params).c_str(),callback.p);
  if(FAILED(status))done(false,"{}");
}
static bool xb_automation_live(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,UINT64 navigation){
  return host->alive&&!view->closed&&!view->requestedClose&&!view->failed&&!view->manual&&!view->dialog&&!xb_popup_blocked(view)&&view->navigation==navigation;
}
static void xb_frame_cdp(const std::shared_ptr<xb_view>& view,const std::string& session,const wchar_t* method,
                         const std::string& params,xb_cdp_result done);
#include "xs_browser_worlds.inc.hpp"
static void xb_automation_reply(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,
                                const std::string& id,const std::string& result,const std::string& error=""){
  if(view->automationRequests.erase(id))host->reply(id,result,error);
}
static void xb_abort_automation(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,int interruption){
  auto pending=std::move(view->automationRequests);view->automationRequests.clear();
  const auto result=interruption==2?"{\"ok\":true,\"value\":{\"ok\":true,\"blocked_by_popup\":true}}":"{\"ok\":true,\"value\":{\"ok\":true,\"blocked_by_dialog\":true}}";
  for(const auto& item:pending)host->reply(item.first,interruption?result:"",interruption?"":"The browser was closed");
}
static void xb_automation(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,
                          const std::string& id,const std::string& script){
  if(script.size()>131072){host->reply(id,"","Automation script exceeds 128 KiB");return;}
  if(view->automationRequests.size()>=32){host->reply(id,"","Native automation request limit reached");return;}
  view->automationRequests[id]=true;
  const auto navigation=view->navigation;
  const auto evaluate=[host,view,id,script,navigation](std::shared_ptr<xb_world_context> context){
    if(!view->automationRequests.count(id))return;
    if(!xb_automation_live(host,view,navigation)||!xb_world_live(view,context)){xb_automation_reply(host,view,id,"","The original isolated browser world is unavailable; observe the current page before another action");return;}
    const auto params="{\"expression\":"+xb_quote(script)+","+xb_world_selector(context)+
      ",\"returnByValue\":true,\"awaitPromise\":false,\"timeout\":3000}";
    xb_world_protocol(view,context->session,context->sourceOwner,L"Runtime.evaluate",params,[host,view,id,navigation,context](bool ok,const std::string& result){
      if(!xb_automation_live(host,view,navigation)||!xb_world_live(view,context)){xb_automation_reply(host,view,id,"","The original browser world changed during evaluation; inspect any delivered effects");return;}
      const auto value=xb_cdp_value(xb_get(result,"result"));
      if(!ok||!xb_get(result,"error").empty()||!xb_get(result,"exceptionDetails").empty()||value.empty())xb_automation_reply(host,view,id,"","Isolated page evaluation failed; inspect any delivered effects");
      else xb_automation_reply(host,view,id,"{\"ok\":true,\"value\":"+value+"}");
    });
  };
  xb_world_root(host,view,navigation,evaluate);
}

// BROWSER_FRAME_INPUT_GUARD_BEGIN
using xb_input_frame_guard=std::vector<std::pair<std::string,std::string>>;
struct xb_input_frame_pin{std::string frame,document,parent;};
using xb_input_frame_snapshot=std::vector<xb_input_frame_pin>;
static bool xb_input_snapshot_live(const std::shared_ptr<xb_view>& view,const xb_input_frame_snapshot& snapshot){
  if(snapshot.empty())return true;
  auto state=view->frames;if(!state||state->closed||!state->error.empty()||snapshot.size()>32||state->nodes.size()!=snapshot.size())return false;
  bool root=false;
  for(size_t index=0;index<snapshot.size();index++){
    const auto& pin=snapshot[index];
    for(size_t previous=0;previous<index;previous++)if(snapshot[previous].frame==pin.frame)return false;
    const xb_frame_node* node=nullptr;for(const auto& item:state->nodes)if(item.second.id==pin.frame){node=&item.second;break;}
    if(!node||node->document!=pin.document||node->loading)return false;
    if(!node->session.empty()){
      auto session=state->sessions.find(node->session);
      if(session==state->sessions.end()||!session->second.ready||!session->second.error.empty())return false;
    }
    if(pin.frame=="f0"){
      if(node->raw!=state->root||!node->parent.empty()||!pin.parent.empty())return false;root=true;
    }else{
      auto parent=state->nodes.find(node->parent);if(parent==state->nodes.end()||parent->second.id!=pin.parent)return false;
    }
    const auto* ancestor=node;unsigned depth=0;
    for(;ancestor->id!="f0"&&depth<32;depth++){
      auto parent=state->nodes.find(ancestor->parent);if(parent==state->nodes.end())return false;ancestor=&parent->second;
    }
    if(ancestor->id!="f0"||depth>=32)return false;
  }
  return root;
}
static bool xb_input_frames_live(const std::shared_ptr<xb_view>& view,const xb_input_frame_guard& guard){
  if(guard.empty())return true;
  auto state=view->frames;if(!state||state->closed||!state->error.empty()||guard.size()>32)return false;
  const xb_frame_node* child=nullptr;
  for(const auto& pin:guard){
    const xb_frame_node* node=nullptr;for(const auto& item:state->nodes)if(item.second.id==pin.first){node=&item.second;break;}
    if(!node||node->document!=pin.second||node->loading||(child&&child->parent!=node->raw))return false;
    if(!node->session.empty()){
      auto session=state->sessions.find(node->session);
      if(session==state->sessions.end()||!session->second.ready||!session->second.error.empty())return false;
    }
    child=node;
  }
  return child&&child->id=="f0"&&child->raw==state->root&&child->parent.empty();
}
struct xb_input_step{const wchar_t* method;std::string params;bool release=false,press=false;};
struct xb_input_sequence:std::enable_shared_from_this<xb_input_sequence>{
  std::shared_ptr<xb_host> host;std::shared_ptr<xb_view> view;std::string id,error;
  UINT64 navigation;std::vector<xb_input_step> steps;xb_input_frame_guard frameGuard;xb_input_frame_snapshot frameSnapshot;size_t index=0;bool replied=false,pressed=false,focusReady=false;
  std::function<void(std::function<void(bool)>)> focusCheck;
  void boundary(const char* kind){
    if(replied)return;replied=true;
    error="A browser interaction checkpoint interrupted input; inspect the page before continuing";
    host->reply(id,std::string("{\"accepted\":true,\"trusted_input\":true,\"")+kind+"_open\":true}");
  }
  void dialogOpened(){boundary("dialog");}
  void popupOpened(){boundary("popup");}
  void run(){
    if(index==steps.size()){if(!replied)host->reply(id,error.empty()?"{\"accepted\":true,\"trusted_input\":true}":"",error);return;}
    const auto step=steps[index];
    if(!xb_automation_live(host,view,navigation)&&error.empty())error="Browser control or document changed; verify the action result";
    if(!xb_input_frames_live(view,frameGuard)&&error.empty())error="A frame document changed; inspect the page before further input";
    if(!xb_input_snapshot_live(view,frameSnapshot)&&error.empty())error="The visual frame snapshot changed; inspect a fresh image before further input";
    if(error.empty()&&!step.release&&focusCheck&&!focusReady){
      auto self=shared_from_this();focusCheck([self](bool focused){
        self->focusReady=true;if(!focused&&self->error.empty())self->error="The original keyboard target lost focus; inspect before further input";self->run();
      });return;
    }
    focusReady=false;index++;
    // Release an admitted press even after navigation/takeover, but never send
    // an unpaired up event for a press that the interruption already skipped.
    if(!error.empty()&&!step.release){run();return;}
    if(step.release&&!pressed){run();return;}
    if(step.release)pressed=false;
    if(step.press)pressed=true; // A lost press acknowledgement still needs cleanup.
    auto self=shared_from_this();
    xb_cdp(view,step.method,step.params,[self](bool ok,const std::string&){
      if(!ok&&self->error.empty())self->error="Native input failed; verify the action result";self->run();
    });
  }
};
// BROWSER_FRAME_INPUT_GUARD_END
// The frames backend is defined after this input helper in the native host.
static void xb_frame_cdp(const std::shared_ptr<xb_view>& view,const std::string& session,const wchar_t* method,
                         const std::string& params,xb_cdp_result done);
static void xb_input_focus(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,UINT64 navigation,
                           const xb_input_frame_guard& guard,const xb_input_frame_snapshot& snapshot,
                           const std::string& document,const std::string& ref,std::function<void(bool)> complete,bool acquire=false){
  const auto live=[host,view,navigation,guard,snapshot](){return xb_automation_live(host,view,navigation)&&xb_input_frames_live(view,guard)&&xb_input_snapshot_live(view,snapshot);};
  if(guard.empty()||!live()){complete(false);return;}
  std::string raw,session,loader;
  for(const auto& entry:view->frames->nodes)if(entry.second.id==guard.front().first){raw=entry.second.raw;session=entry.second.session;loader=entry.second.loader;break;}
  if(raw.empty()){complete(false);return;}
  xb_world_frame(host,view,navigation,session,raw,loader,live,[view,live,session,document,ref,complete,acquire](std::shared_ptr<xb_world_context> world){
      if(!xb_world_live(view,world)||!live()){complete(false);return;}
      const auto expression="(()=>{const r=globalThis.__mdo_browser_use_local_frame_v1;if(!r||r.documentNode!==document)return 0;const v=r."+std::string(acquire?"focus":"verify")+"({document:"+xb_quote(document)+",op:'key',target:{ref:"+xb_quote(ref)+"}});return v.ok&&v.focused&&document.hasFocus()?1:0})()";
      xb_world_protocol(view,world->session,world->sourceOwner,L"Runtime.evaluate","{\"expression\":"+xb_quote(expression)+","+xb_world_selector(world)+",\"returnByValue\":true,\"timeout\":1000}",
        [view,world,live,complete](bool ok,const std::string& result){const auto value=xb_get(result,"result");complete(ok&&live()&&xb_world_live(view,world)&&xb_get(result,"exceptionDetails").empty()&&xb_get(value,"type")=="number"&&xb_get(value,"value")=="1");});
    });
}
static bool xb_input_read_frames(const std::string& json,xb_input_frame_guard& guard,const std::string& key="frame_chain"){
  const char* raw=nullptr;size_t size=0;
  guard.clear();webview::detail::json_parse_c(json.c_str(),json.size(),key.c_str(),key.size(),&raw,&size);
  if(!raw)return true;
  if(size<2||size>8192||raw[0]!='['||raw[size-1]!=']')return false;
  const std::string chain(raw,size);
  for(size_t index=0;index<33;index++){
    const char* row=nullptr;size_t length=0;
    webview::detail::json_parse_c(chain.c_str(),chain.size(),nullptr,index,&row,&length);
    if(!row)break;if(index==32||!length||row[0]!='{')return false;
    const std::string item(row,length);const auto frame=xb_get(item,"frame"),document=xb_get(item,"document");
    if(frame.empty()||frame.size()>64||document.empty()||document.size()>64)return false;
    guard.emplace_back(frame,document);
  }
  return !guard.empty();
}
static std::string xb_input_frame_key(const xb_input_frame_guard& guard){
  if(guard.empty())return{};std::string key="[";
  for(const auto& pin:guard){if(key.size()>1)key+=",";key+="["+xb_quote(pin.first)+","+xb_quote(pin.second)+"]";}return key+"]";
}
static bool xb_input_pin_frames(const std::shared_ptr<xb_view>& view,const std::string& json,xb_input_frame_guard& guard){
  return xb_input_read_frames(json,guard)&&xb_input_frames_live(view,guard);
}
static bool xb_input_read_snapshot(const std::string& json,xb_input_frame_snapshot& snapshot){
  const char* raw=nullptr;size_t size=0;snapshot.clear();
  webview::detail::json_parse_c(json.c_str(),json.size(),"frame_snapshot",14,&raw,&size);
  if(!raw)return true;if(size<2||size>12288||raw[0]!='['||raw[size-1]!=']')return false;
  const std::string array(raw,size);
  for(size_t index=0;index<33;index++){
    const char* row=nullptr;size_t length=0;webview::detail::json_parse_c(array.c_str(),array.size(),nullptr,index,&row,&length);
    if(!row)break;if(index==32||!length||row[0]!='{')return false;
    const std::string item(row,length);const auto frame=xb_get(item,"frame"),document=xb_get(item,"document"),parent=xb_get(item,"parent");
    if(frame.empty()||frame.size()>64||document.empty()||document.size()>64||parent.size()>64)return false;
    snapshot.push_back({frame,document,parent});
  }
  return !snapshot.empty();
}
static void xb_input(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,
                     const std::string& id,const std::string& json){
  if(xb_get(json,"navigation")!=std::to_string(view->navigation)||view->manual||view->closed){host->reply(id,"","Stale document or manual control");return;}
  if(view->dialog){host->reply(id,"","Respond to the pending JavaScript dialog before input");return;}
  if(xb_popup_blocked(view)){host->reply(id,"","A new-window request is still being adopted");return;}
  if(!view->input.expired()||view->drag||view->uploadBusy||xb_chooser_blocked(view)){host->reply(id,"","A native input or file picker is still completing; inspect the page before another action");return;}
  auto sequence=std::make_shared<xb_input_sequence>();sequence->host=host;sequence->view=view;sequence->id=id;sequence->navigation=view->navigation;
  if(!xb_input_pin_frames(view,json,sequence->frameGuard)){host->reply(id,"","Invalid or stale frame document chain; observe the page before input");return;}
  if(!xb_input_read_snapshot(json,sequence->frameSnapshot)||!xb_input_snapshot_live(view,sequence->frameSnapshot)){host->reply(id,"","Invalid or stale visual frame snapshot; capture a fresh image before input");return;}
  const auto kind=xb_get(json,"kind");
  const auto focusDocument=xb_get(json,"focus_document"),focusRef=xb_get(json,"focus_ref");
  const bool focusRequested=!focusDocument.empty()||!focusRef.empty();
  if(focusRequested||!sequence->frameSnapshot.empty()&&(kind=="key"||kind=="text")){
    if((kind!="key"&&kind!="text"&&kind!="focus")||focusDocument.size()!=38||focusDocument.substr(0,2)!="d-"||focusRef.size()<2||focusRef.size()>14||focusRef[0]!='e'||
       focusRef.find_first_not_of("0123456789",1)!=std::string::npos||sequence->frameGuard.empty()){
      host->reply(id,"","Keyboard input requires its original isolated target and frame document chain");return;
    }
    std::weak_ptr<xb_input_sequence> weak=sequence;
    sequence->focusCheck=[weak,focusDocument,focusRef](std::function<void(bool)> complete){
      auto held=weak.lock();if(!held){complete(false);return;}
      xb_input_focus(held->host,held->view,held->navigation,held->frameGuard,held->frameSnapshot,focusDocument,focusRef,std::move(complete));
    };
  }
  if(kind=="focus"&&focusRequested){
    // Focus the admitted native view/control without activating it by a click.
    // Focus itself can run page handlers, so retain the normal input slot and
    // interaction checkpoints until its exact document/focus is acknowledged.
    view->input=sequence;
    if(!view->controller||FAILED(view->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC))){host->reply(id,"","Cannot focus the original browser view");return;}
    xb_input_focus(host,view,sequence->navigation,sequence->frameGuard,sequence->frameSnapshot,focusDocument,focusRef,
      [sequence](bool focused){if(sequence->replied)return;sequence->replied=true;
        sequence->host->reply(sequence->id,focused?"{\"accepted\":true,\"target_focused\":true,\"focus_method\":\"isolated_focus\"}":"",focused?"":"The original control did not retain focus");},true);
    return;
  }
  if(kind=="click"||kind=="move"||kind=="wheel"){
    double x=-1,y=-1;try{x=std::stod(xb_get(json,"x"));y=std::stod(xb_get(json,"y"));}catch(...){}
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>16384||y>16384){host->reply(id,"","Invalid CSS input coordinates");return;}
    const auto point=",\"x\":"+std::to_string(x)+",\"y\":"+std::to_string(y);
    if(kind=="wheel"){
      double dx=0,dy=0;try{dx=std::stod(xb_get(json,"dx"));dy=std::stod(xb_get(json,"dy"));}catch(...){host->reply(id,"","Invalid wheel delta");return;}
      if(!std::isfinite(dx)||!std::isfinite(dy)||std::abs(dx)>10000||std::abs(dy)>10000){host->reply(id,"","Wheel delta exceeds limit");return;}
      sequence->steps.push_back({L"Input.dispatchMouseEvent","{\"type\":\"mouseWheel\""+point+",\"deltaX\":"+std::to_string(dx)+",\"deltaY\":"+std::to_string(dy)+"}"});
    }else{
      sequence->steps.push_back({L"Input.dispatchMouseEvent","{\"type\":\"mouseMoved\""+point+"}"});
      if(kind=="click"){
        auto button=xb_get(json,"button"),count=xb_get(json,"click_count");
        if(button.empty())button="left";if(count.empty())count="1";
        if((button!="left"&&button!="right"&&button!="middle")||(count!="1"&&count!="2")){host->reply(id,"","Unsupported mouse button or click count");return;}
        const int buttons=button=="left"?1:button=="right"?2:4,clicks=count=="2"?2:1;
        for(int n=1;n<=clicks;n++){
          const auto fields=",\"button\":"+xb_quote(button)+",\"clickCount\":"+std::to_string(n)+point;
          sequence->steps.push_back({L"Input.dispatchMouseEvent","{\"type\":\"mousePressed\",\"buttons\":"+std::to_string(buttons)+fields+"}",false,true});
          sequence->steps.push_back({L"Input.dispatchMouseEvent","{\"type\":\"mouseReleased\",\"buttons\":0"+fields+"}",true});
        }
      }
    }
  }else if(kind=="text"){
    const auto text=xb_get(json,"text");if(text.size()>32768){host->reply(id,"","Text exceeds 32 KiB");return;}
    sequence->steps.push_back({L"Input.insertText","{\"text\":"+xb_quote(text)+"}"});
  }else if(kind=="key"){
    const auto key=xb_get(json,"key");std::string code=key,actual=key;int vk=0,modifiers=0;
    if(key=="Control+A"){actual="a";code="KeyA";vk=65;modifiers=2;}
    else if(key=="Control+Home"){actual="Home";code="Home";vk=36;modifiers=2;}
    else if(key=="Control+End"){actual="End";code="End";vk=35;modifiers=2;}
    else if(key=="Control+ArrowUp"){actual="ArrowUp";code="ArrowUp";vk=38;modifiers=2;}
    else if(key=="Control+ArrowDown"){actual="ArrowDown";code="ArrowDown";vk=40;modifiers=2;}
    else if(key=="Control+Space"){actual=" ";code="Space";vk=32;modifiers=2;}
    else if(key=="Space"){actual=" ";code="Space";vk=32;}
    else if(key=="Shift+Tab"){actual="Tab";code="Tab";vk=9;modifiers=8;}
    else if(key=="Enter")vk=13;else if(key=="Tab")vk=9;else if(key=="Escape")vk=27;
    else if(key=="Backspace")vk=8;else if(key=="Delete")vk=46;else if(key=="ArrowLeft")vk=37;
    else if(key=="ArrowUp")vk=38;else if(key=="ArrowRight")vk=39;else if(key=="ArrowDown")vk=40;
    else if(key=="Home")vk=36;else if(key=="End")vk=35;else if(key=="PageUp")vk=33;else if(key=="PageDown")vk=34;
    if(!vk){host->reply(id,"","Unsupported key; use text for character input");return;}
    const auto fields=",\"key\":"+xb_quote(actual)+",\"code\":"+xb_quote(code)+",\"windowsVirtualKeyCode\":"+std::to_string(vk)+",\"modifiers\":"+std::to_string(modifiers);
    sequence->steps.push_back({L"Input.dispatchKeyEvent","{\"type\":\"keyDown\""+fields+(key=="Enter"?",\"text\":\"\\r\"":"")+"}",false,true});
    sequence->steps.push_back({L"Input.dispatchKeyEvent","{\"type\":\"keyUp\""+fields+"}",true});
  }else{host->reply(id,"","Unsupported native input kind");return;}
  view->input=sequence;sequence->run();
}

static constexpr UINT_PTR xb_dialog_timer=0xBADD1;
static std::map<HWND,std::weak_ptr<xb_view>> xb_dialog_views;
static void xb_dialog_cleanup(HWND window){KillTimer(window,xb_dialog_timer);xb_dialog_views.erase(window);}
static void CALLBACK xb_dialog_expire(HWND window,UINT,UINT_PTR,DWORD){
  auto item=xb_dialog_views.find(window);if(item==xb_dialog_views.end()){KillTimer(window,xb_dialog_timer);return;}
  auto view=item->second.lock();
  if(!view||!view->dialog||GetTickCount64()>=view->dialog->expires){
    auto expired=view?std::move(view->dialog):std::shared_ptr<xb_dialog>{};
    xb_dialog_cleanup(window);expired.reset();
  }
}
static std::string xb_bounded_text(std::string text,size_t maximum){
  if(text.size()>maximum){size_t end=maximum;while(end&&(static_cast<unsigned char>(text[end])&0xC0)==0x80)end--;text.resize(end);}return text;
}
static const char* xb_dialog_kind(COREWEBVIEW2_SCRIPT_DIALOG_KIND kind){
  switch(kind){case COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT:return "alert";
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_CONFIRM:return "confirm";
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_PROMPT:return "prompt";
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD:return "beforeunload";default:return "unknown";}
}
static void xb_install_dialogs(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view){
  std::weak_ptr<xb_view> weak=view;std::weak_ptr<xb_host> parent=host;
  auto* callback=xb_cb<ICoreWebView2ScriptDialogOpeningEventHandler,ICoreWebView2*,ICoreWebView2ScriptDialogOpeningEventArgs*>(
    IID_ICoreWebView2ScriptDialogOpeningEventHandler,[weak,parent](ICoreWebView2*,ICoreWebView2ScriptDialogOpeningEventArgs* args){
      auto active=weak.lock();if(!active||active->closed||active->dialog)return S_OK;
      auto dialog=std::make_shared<xb_dialog>();GUID guid{};wchar_t identifier[40]{};
      if(FAILED(CoCreateGuid(&guid))||!StringFromGUID2(guid,identifier,40)||FAILED(args->GetDeferral(&dialog->deferral))){active->error="Cannot retain a JavaScript dialog; it was dismissed";return S_OK;}
      args->AddRef();dialog->args=args;dialog->id="dialog-"+webview::detail::narrow_string(identifier);
      dialog->expires=GetTickCount64()+120000;active->dialog=dialog;
      xb_dialog_views[active->window]=active;
      if(!SetTimer(active->window,xb_dialog_timer,1000,xb_dialog_expire)){active->error="Cannot retain a JavaScript dialog; it was dismissed";active->dialog.reset();xb_dialog_cleanup(active->window);return S_OK;}
      if(auto input=active->input.lock())input->dialogOpened();
      xb_upload_boundary(active,false);
      xb_chooser_boundary(active,false);
      if(auto owner=parent.lock())xb_abort_automation(owner,active,true);return S_OK;
    });
  EventRegistrationToken token{};HRESULT status=view->core->add_ScriptDialogOpening(callback,&token);callback->Release();
  if(SUCCEEDED(status)){
    xs_wv_com<ICoreWebView2Settings> settings;
    if(SUCCEEDED(view->core->get_Settings(settings.out()))&&SUCCEEDED(settings->put_AreDefaultScriptDialogsEnabled(FALSE)))view->dialogsEnabled=true;
    else view->core->remove_ScriptDialogOpening(token);
  }
}
static void xb_dialog_request(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,
                              const std::string& id,const std::string& json){
  const auto action=xb_get(json,"action");auto dialog=view->dialog;
  if(action=="list"){
    std::string item;
    if(dialog){xs_wv_text message,url;COREWEBVIEW2_SCRIPT_DIALOG_KIND kind;
      dialog->args->get_Kind(&kind);dialog->args->get_Message(&message.p);dialog->args->get_Uri(&url.p);
      item="{\"id\":"+xb_quote(dialog->id)+",\"kind\":"+xb_quote(xb_dialog_kind(kind))+
        ",\"message\":"+xb_quote(xb_bounded_text(webview::detail::narrow_string(message.p?message.p:L""),4096))+
        ",\"url\":"+xb_quote(xb_bounded_text(webview::detail::narrow_string(url.p?url.p:L""),2048))+"}";
    }
    host->reply(id,"{\"ok\":true,\"dialogs\":["+item+"]}");return;
  }
  if(action!="respond"||!dialog||dialog->id!=xb_get(json,"dialog")){host->reply(id,"","The requested JavaScript dialog is no longer pending");return;}
  if(view->manual&&xb_get(json,"agent")=="true"){host->reply(id,"","The user currently controls this browser");return;}
  const auto accept=xb_get(json,"accept"),text=xb_get(json,"text");
  if((accept!="true"&&accept!="false")||text.size()>8192){host->reply(id,"","A boolean response and at most 8 KiB of text are required");return;}
  COREWEBVIEW2_SCRIPT_DIALOG_KIND kind;dialog->args->get_Kind(&kind);HRESULT status=S_OK;
  if(accept=="true"){
    if(kind==COREWEBVIEW2_SCRIPT_DIALOG_KIND_PROMPT){
      if(xb_get(json,"has_text")=="true"||!text.empty())status=dialog->args->put_ResultText(webview::detail::widen_string(text).c_str());
      else{xs_wv_text initial;status=dialog->args->get_DefaultText(&initial.p);if(SUCCEEDED(status))status=dialog->args->put_ResultText(initial.p?initial.p:L"");}
    }
    if(SUCCEEDED(status))status=dialog->args->Accept();
  }
  if(FAILED(status)){host->reply(id,"","Dialog response was not confirmed; inspect the current page");return;}
  view->dialog.reset();xb_dialog_cleanup(view->window);dialog.reset();
  host->reply(id,"{\"ok\":true,\"responded\":true}");
}
