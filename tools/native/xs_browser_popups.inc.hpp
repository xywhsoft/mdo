// Pending new-window deferrals are owned by their actual opener, never a URL.
// The trusted main UI first admits a scoped tab, then adopts the un-navigated
// WebView through the opener's exact environment. No host binding is exposed.
static constexpr UINT_PTR xb_popup_timer=0xBADD2;
static std::map<HWND,std::weak_ptr<xb_view>> xb_popup_views;
static void xb_popup_cleanup(HWND window){KillTimer(window,xb_popup_timer);xb_popup_views.erase(window);}
static void xb_cancel_popups(const std::shared_ptr<xb_view>& view){
  xb_popup_cleanup(view->window);auto pending=std::move(view->popups);view->popups.clear();
  for(auto& item:pending)item.second->complete();
  if(view->opening){view->opening->complete();view->opening.reset();}
}
static void CALLBACK xb_popup_expire(HWND window,UINT,UINT_PTR,DWORD){
  auto item=xb_popup_views.find(window);if(item==xb_popup_views.end()){KillTimer(window,xb_popup_timer);return;}
  auto view=item->second.lock();if(!view){xb_popup_cleanup(window);return;}
  std::vector<std::shared_ptr<xb_popup>> expired;
  for(auto it=view->popups.begin();it!=view->popups.end();){
    if(it->second->completed||GetTickCount64()>=it->second->expires){
      if(!it->second->completed)view->popupError="A new-window request expired before it was adopted";
      expired.push_back(it->second);it=view->popups.erase(it);
    }else ++it;
  }
  if(view->popups.empty())xb_popup_cleanup(window);
  for(auto& popup:expired)popup->complete();
}
static void xb_install_popups(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,const std::string& browser){
  std::weak_ptr<xb_view> weak=view;std::weak_ptr<xb_host> parent=host;
  auto* callback=xb_cb<ICoreWebView2NewWindowRequestedEventHandler,ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs*>(
    IID_ICoreWebView2NewWindowRequestedEventHandler,[weak,parent,browser](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args){
      args->put_Handled(TRUE);auto active=weak.lock();auto owner=parent.lock();
      if(!active||!owner||active->closed||!owner->trusted())return S_OK;
      xs_wv_text uri;if(FAILED(args->get_Uri(&uri.p)))return S_OK;
      auto url=webview::detail::narrow_string(uri.p?uri.p:L"");
      if(!xb_url(url)){active->popupError="A new window using an unsupported URL was blocked";return S_OK;}
      xb_popup_expire(active->window,0,0,0);
      size_t total=0;for(const auto& entry:owner->views)total+=entry.second->popups.size();
      if(active->popups.size()>=4||total>=16||owner->views.size()>=8){active->popupError="New-window resource limit reached";return S_OK;}
      auto popup=std::make_shared<xb_popup>();GUID guid{};wchar_t identifier[40]{};
      if(FAILED(CoCreateGuid(&guid))||!StringFromGUID2(guid,identifier,40)||FAILED(args->GetDeferral(&popup->deferral))){active->popupError="Cannot retain a new-window request";return S_OK;}
      args->AddRef();popup->args=args;popup->id="popup-"+webview::detail::narrow_string(identifier);popup->url=url;popup->expires=GetTickCount64()+20000;
      BOOL user=FALSE;args->get_IsUserInitiated(&user);popup->userInitiated=user!=FALSE;
      active->popups[popup->id]=popup;xb_popup_views[active->window]=active;
      if(!SetTimer(active->window,xb_popup_timer,250,xb_popup_expire)){active->popupError="Cannot retain a new-window request";active->popups.erase(popup->id);if(active->popups.empty())xb_popup_cleanup(active->window);return S_OK;}
      if(auto input=active->input.lock())input->popupOpened();
      xb_upload_boundary(active,true);
      xb_chooser_boundary(active,true);
      xb_abort_automation(owner,active,2);
      const auto event="{\"channel\":\"mdo-browser-popup\",\"browser\":"+xb_quote(browser)+"}";
      owner->main->PostWebMessageAsJson(webview::detail::widen_string(event).c_str());return S_OK;
    });
  EventRegistrationToken token{};view->popupsEnabled=SUCCEEDED(view->core->add_NewWindowRequested(callback,&token));callback->Release();
  auto* closed=xb_cb<ICoreWebView2WindowCloseRequestedEventHandler,ICoreWebView2*,IUnknown*>(
    IID_ICoreWebView2WindowCloseRequestedEventHandler,[weak](ICoreWebView2*,IUnknown*){if(auto active=weak.lock()){if(!active->requestedClose)active->closeRequestedAt=GetTickCount64();active->requestedClose=true;}return S_OK;});
  EventRegistrationToken closedToken{};view->core->add_WindowCloseRequested(closed,&closedToken);closed->Release();
}
static void xb_popup_request(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,const std::string& id,const std::string& json){
  // A query can run after the lease boundary but before the queued WM_TIMER.
  // Do not hide an expired ticket while still retaining its native deferral.
  xb_popup_expire(view->window,0,0,0);
  const auto action=xb_get(json,"action");
  if(action=="list"){
    std::string items;for(const auto& item:view->popups){const auto& popup=item.second;
      if(popup->completed||popup->claimed||GetTickCount64()>=popup->expires)continue;
      if(!items.empty())items+=",";items+="{\"id\":"+xb_quote(popup->id)+",\"url\":"+xb_quote(popup->url)+",\"user_initiated\":"+(popup->userInitiated?"true":"false")+"}";
    }
    host->reply(id,"{\"ok\":true,\"popups\":["+items+"],\"error\":"+xb_quote(view->popupError)+"}");return;
  }
  auto item=view->popups.find(xb_get(json,"popup"));
  if(action!="dismiss"||item==view->popups.end()){host->reply(id,"","Popup request is no longer available");return;}
  auto pending=item->second;view->popups.erase(item);if(view->popups.empty())xb_popup_cleanup(view->window);pending->complete();
  host->reply(id,"{\"ok\":true,\"dismissed\":true}");
}
