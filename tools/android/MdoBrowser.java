package org.xserver.android;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.net.Uri;
import android.webkit.*;
import android.view.View;
import android.widget.FrameLayout;
import android.util.Base64;
import org.json.*;
import java.util.*;
import java.io.ByteArrayOutputStream;

/** Main-document-only message port. No JavascriptInterface is exposed to any
 * content frame, including iframe previews in the trusted host WebView. */
final class MdoBrowser {
    private final Activity activity;
    private final MdoBrowserManualChooser manualChooser;
    private final FrameLayout root;
    private final WebView host;
    private final MdoBrowserLifecycle<WebView> views;
    private final Set<String> manual = new HashSet<>();
    private final Map<String,Boolean> loading = new HashMap<>();
    private final Map<String,String> errors = new HashMap<>();
    private final Map<String,Integer> errorCodes = new HashMap<>();
    private final Set<String> crashed = new HashSet<>();
    private final Map<String,String> locations = new HashMap<>();
    private WebMessagePort port;
    private final MdoBrowserChannel<WebMessagePort> channel=new MdoBrowserChannel<>();
    private boolean stopped;
    MdoBrowser(Activity activity, FrameLayout root, WebView host) {
        this.activity=activity; this.root=root; this.host=host;manualChooser=new MdoBrowserManualChooser(activity);
        this.views=new MdoBrowserLifecycle<>((view,epoch,admitted)->new MdoBrowserInput(
            new MdoBrowserInputTarget(view,epoch,()->!stopped&&admitted.getAsBoolean()&&channel.current()!=null&&
                channel.active(channel.current(),host.getUrl())),new MdoBrowserInput.Scheduler(){
                private final android.os.Handler handler=new android.os.Handler(android.os.Looper.getMainLooper());
                public long now(){return android.os.SystemClock.uptimeMillis();}
                public long elapsed(){return android.os.SystemClock.elapsedRealtime();}
                public void post(Runnable task,long delay){if(!handler.postDelayed(task,delay))throw new IllegalStateException("Native input scheduler stopped");}
                public void remove(Runnable task){handler.removeCallbacks(task);}
            }),this::inputInvalidated);

    }
    void attach(String url) {
        if (stopped || !channel.requiresAttach(url,host.getUrl(),port)) return;
        manualChooser.invalidate();channel.invalidate();cancelPopups();cancelDialogs();
        blockDocuments(MdoBrowserDocument.CHANNEL,true);
        if (port != null) port.close();
        WebMessagePort[] ports=host.createWebMessageChannel(); port=ports[0];
        final MdoBrowserChannel<WebMessagePort>.Lease lease=channel.attach(url,port);
        blockDocuments(MdoBrowserDocument.CHANNEL,false);
        port.setWebMessageCallback(new WebMessagePort.WebMessageCallback() {
            @Override public void onMessage(WebMessagePort source, WebMessage message) {
                if (stopped || port!=ports[0] || !channel.owns(lease,source,host.getUrl())) return;
                String text=message.getData(); if(text==null || text.length()>262144) return;
                try { request(new JSONObject(text)); } catch(Exception error) { /* Invalid input has no effects. */ }
            }
        });
        String script="(()=>{if(window.__mdoBrowserInitListener)window.removeEventListener('message',window.__mdoBrowserInitListener);const f=e=>{if(e.data!=='mdo-browser-init'||e.ports.length!==1||e.source!==null)return;window.removeEventListener('message',f);if(window.__mdoBrowserInitListener===f)delete window.__mdoBrowserInitListener;const p=e.ports[0];"
            + "window.XsBrowser={request:s=>p.postMessage(s)};p.onmessage=e=>window.dispatchEvent(new CustomEvent('mdo-browser-result',{detail:e.data}));"
            + "window.dispatchEvent(new Event('mdo-browser-ready'));};window.__mdoBrowserInitListener=f;window.addEventListener('message',f)})()";
        final Uri origin=Uri.parse(Uri.parse(url).getScheme()+"://"+Uri.parse(url).getAuthority());
        host.evaluateJavascript(script, ignored -> {
            if (!stopped&&port==ports[0]&&channel.active(lease,host.getUrl())) host.postWebMessage(new WebMessage("mdo-browser-init",new WebMessagePort[]{ports[1]}),origin);
            else{ports[0].close();ports[1].close();}
        });
    }
    private void reply(String id,Object result,String error) {
        if(stopped || port==null||!channel.owns(channel.current(),port,host.getUrl()))return;
        try { JSONObject value=new JSONObject().put("channel","mdo-browser").put("id",id);
            if(error!=null)value.put("error",error);else value.put("result",result);
            String json=value.toString();
            if(json.length()>1048576)json=new JSONObject().put("channel","mdo-browser").put("id",id).put("error","Browser result exceeds 1 MiB").toString();
            port.postMessage(new WebMessage(json));
        } catch(Exception ignored) { }
    }
    private void reply(MdoBrowserChannel<WebMessagePort>.Lease lease,String id,Object result,String error){
        if(stopped||port==null||channel.current()!=lease||!channel.active(lease,host.getUrl()))return;reply(id,result,error);
    }
    void detach(){
        if(stopped)return;manualChooser.invalidate();channel.invalidate();WebMessagePort previous=port;port=null;
        cancelPopups();cancelDialogs();
        if(previous!=null)previous.close();views.stopAll();blockDocuments(MdoBrowserDocument.CHANNEL,true);
    }
    private static String url(String value) {
        Uri uri=Uri.parse(value);String scheme=uri.getScheme();
        if(value.length()>8192 || (!"http".equals(scheme)&&!"https".equals(scheme)) || uri.getHost()==null || uri.getUserInfo()!=null)
            throw new IllegalArgumentException("Only HTTP(S) navigation is supported");
        return value;
    }
    private void bounds(WebView web,JSONObject request) {
        JSONObject box=request.optJSONObject("bounds");if(box==null)return;
        double scale=Math.max(.5,Math.min(8,box.optDouble("scale",1)));
        int width=(int)Math.max(1,Math.min(16384,box.optDouble("width",640)*scale));
        int height=(int)Math.max(1,Math.min(16384,box.optDouble("height",480)*scale));
        FrameLayout.LayoutParams params=new FrameLayout.LayoutParams(width,height);
        params.leftMargin=host.getLeft()+(int)Math.max(0,Math.min(32768,box.optDouble("x",0)*scale));
        params.topMargin=host.getTop()+(int)Math.max(0,Math.min(32768,box.optDouble("y",0)*scale));
        boolean visible=request.optBoolean("visible",true);if(!visible){cancelPopup(request.optString("browser"));cancelDialog(request.optString("browser"));}if(!visible||web.getWidth()!=width||web.getHeight()!=height)cancelCapture(request.optString("browser"));views.visibility(request.optString("browser"),web,visible);
        web.setLayoutParams(params);web.setVisibility(visible?View.VISIBLE:View.INVISIBLE);
    }
    // UI-owned physical view/profile/channel associations. Navigation changes
    // document authority, never this original network owner. No public wire IDs.
    private final Map<String,NetworkEntry> networkViews=new HashMap<>();
    private final android.os.Handler networkHandler=new android.os.Handler(android.os.Looper.getMainLooper());
    private boolean networkCleanupFailed;
    private final class NetworkEntry {
        final String key,id,target;final WebView view;final MdoBrowserDocument profile;
        final MdoBrowserChannel<WebMessagePort>.Lease channelLease;
        final MdoBrowserCdpMarker.DocumentQuota quota;
        final java.util.concurrent.atomic.AtomicBoolean live=new java.util.concurrent.atomic.AtomicBoolean(true);
        final long born=android.os.SystemClock.elapsedRealtime();final Runnable watch=()->networkWatch(this);
        MdoBrowserCdpView adapter;MdoBrowserCdpMarker marker;MdoBrowserCdpDownloads downloads;MdoBrowserCdpDownloads.Connection downloadConnection;long warmupEpoch,retiredAt;
        NativeNavigation navigation;
        PopupNetwork popup;
        boolean warming=true,starting,ready,created,retired,draining,rearm,responsesDisabled,protocolDrained=true,destroyed,replySent,cleanupObservationExpired;
        String code;
        NetworkEntry(String key,WebView view,MdoBrowserDocument profile,MdoBrowserChannel<WebMessagePort>.Lease lease,String id,String target){
            this(key,view,profile,lease,id,target,new MdoBrowserCdpMarker.DocumentQuota());
        }
        NetworkEntry(String key,WebView view,MdoBrowserDocument profile,MdoBrowserChannel<WebMessagePort>.Lease lease,String id,String target,MdoBrowserCdpMarker.DocumentQuota quota){
            this.key=key;this.view=view;this.profile=profile;channelLease=lease;this.id=id;this.target=target;this.quota=quota;
        }
    }
    private boolean networkCurrent(NetworkEntry entry){
        return networkViews.get(entry.key)==entry&&!stopped&&!browserPaused&&views.owns(entry.key,entry.view)&&
            documents.get(entry.key)==entry.profile&&entry.profile.profileAssigned()&&(!manual.contains(entry.key)||entry.draining||entry.rearm&&entry.starting)&&!pageClosed.contains(entry.key)&&
            channel.current()==entry.channelLease&&channel.active(entry.channelLease,host.getUrl());
    }
    private int networkRetiring(){int count=0;for(NetworkEntry entry:networkViews.values())if(entry.draining||entry.retired&&!entry.protocolDrained)count++;return count;}
    private int downloadRetiring(){int count=0;for(NetworkEntry entry:networkViews.values())if(entry.downloads!=null)count+=entry.downloads.retiringFiles();return count;}
    private boolean networkClean(){return !networkCleanupFailed&&networkRetiring()==0&&downloadRetiring()==0;}
    private void networkAttach(String key,WebView view,boolean required){
        NetworkEntry entry=networkViews.get(key);
        if(entry!=null&&entry.view==view&&(!entry.ready||entry.retired||!networkCurrent(entry)))throw new IllegalStateException("network_owner_unavailable");
        if(required&&(entry==null||entry.view!=view))throw new IllegalStateException("network_owner_unavailable");
    }
    private void networkCommand(String key,WebView view,String command,boolean agent){
        NetworkEntry entry=networkViews.get(key);if(entry==null||entry.view!=view)return;
        PopupControlRequest control=popupControls.get(key);
        if(control!=null&&control.entry==entry&&!Arrays.asList("state","bounds","close","stop","downloads").contains(command))
            throw new IllegalStateException("browser_control_pending");
        if("downloads".equals(command)&&entry.downloads!=null)return;
        if(entry.navigation!=null&&entry.navigation.ticket!=null&&entry.navigation.ticket.result()==null&&(agent||!manual.contains(key))&&Arrays.asList("navigate","back","forward","reload","automation","input","wheel","drag","upload").contains(command))throw new IllegalStateException("navigation_pending");
        if(entry.retired&&(agent||Arrays.asList("automation","frames","input","wheel","drag","upload").contains(command)))throw new IllegalStateException("network_owner_unavailable");
        if(agent&&entry.downloads!=null&&entry.downloads.pending()&&Arrays.asList("navigate","back","forward","reload","automation","input","wheel","drag","upload").contains(command))
            throw new IllegalStateException(entry.downloads.failure()==null?"download_pending":entry.downloads.failure());
        if(entry.draining){if(!agent)return;throw new IllegalStateException("network_retiring");}
        if(entry.ready||entry.retired)return;
        if(!Arrays.asList("state","bounds","close","takeover","stop").contains(command))throw new IllegalStateException("network_starting");
    }
    private void networkStop(String key,WebView view){PopupControlRequest control=popupControls.get(key);if(control!=null&&control.entry.view==view)popupControlCancel(control,"network_control_cancelled");retireProtocolPopupPreparation(key,view,"cdp_popup_preparation_cancelled");NetworkEntry entry=networkViews.get(key);if(entry!=null&&entry.view==view){nativeNavigationRetire(entry);if(!entry.ready)networkRetire(entry,"network_start_cancelled");}}
    /** One bounded UI request for this original physical owner. Native manual
     * ownership and original protocol retirement are separate terminal facts. */
    private final Map<String,PopupControlRequest> popupControls=new HashMap<>();
    private final class PopupControlRequest {
        final NetworkEntry entry;final PopupNetwork popup;final String id;
        final CaptureEntry capture;final MdoBrowserDialogTarget dialog;final MdoBrowserPopups<WebView> windowsOwner;
        final long epoch,born=android.os.SystemClock.elapsedRealtime();final Runnable watch=()->popupControlPoll(this);
        MdoBrowserCdpPopupNative.HumanControl control;boolean starting=true,done,manualPublished;String cancellation;
        PopupControlRequest(NetworkEntry entry,String id){this.entry=entry;popup=entry.popup;this.id=id;capture=captures.get(entry.key);dialog=dialogs.get(entry.key);windowsOwner=windows.get(entry.key);epoch=views.epoch(entry.key,entry.view);}
        boolean timely(){long age=android.os.SystemClock.elapsedRealtime()-born;return age>=0&&age<5000;}
    }
    private boolean popupControlIdentity(NetworkEntry entry){
        return networkViews.get(entry.key)==entry&&!entry.destroyed&&views.owns(entry.key,entry.view)&&
            documents.get(entry.key)==entry.profile&&entry.profile.profileAssigned()&&!pageClosed.contains(entry.key)&&
            channel.current()==entry.channelLease&&channel.active(entry.channelLease,host.getUrl());
    }
    private boolean popupControlOwner(NetworkEntry entry){return !stopped&&!browserPaused&&popupControlIdentity(entry);}
    private String popupControlCode(String code){return code!=null&&code.length()<=80&&code.matches("(?:cdp|download_frame|network|browser_control)_[a-z_]{1,80}")?code:"network_control_unconfirmed";}
    private boolean networkTakeoverRequest(String key,WebView view,String id)throws java.io.IOException {
        NetworkEntry entry=networkViews.get(key);if(entry==null||entry.view!=view||entry.popup==null)return false;
        if(id==null||id.isEmpty()||id.length()>512||popupControls.containsKey(key)||popupControls.size()>=8)throw new java.io.IOException("browser_control_pending");
        for(PopupControlRequest old:popupControls.values())if(old.id.equals(id))throw new java.io.IOException("browser_control_pending");
        if(!popupControlOwner(entry))throw new java.io.IOException("network_owner_changed");
        if(entry.popup.owner.humanTransferred()&&manual.contains(key)){
            if(!entry.popup.returnable())throw new java.io.IOException("network_retiring");
            try{reply(entry.channelLease,id,new JSONObject().put("manual",true).put("network_cleanup_confirmed",true),null);}
            catch(JSONException error){throw new java.io.IOException("network_control_unconfirmed");}return true;
        }
        PopupControlRequest operation=new PopupControlRequest(entry,id);
        // Scheduler admission precedes all data/input effects, including the
        // typed native receipt's own independently bounded preparation.
        final boolean scheduled;try{scheduled=networkHandler.postDelayed(operation.watch,50);}catch(RuntimeException|Error unavailable){throw new java.io.IOException("network_control_scheduler_unavailable");}
        if(!scheduled)throw new java.io.IOException("network_control_scheduler_unavailable");
        try{
            popupControls.put(key,operation);
            try{operation.control=operation.popup.beginControlAndCancel();}
            catch(java.io.IOException|RuntimeException|Error failure){if(operation.cancellation==null)operation.cancellation=popupControlCode(failure.getMessage());}
            finally{operation.starting=false;}
            popupControlPoll(operation);return true;
        }catch(RuntimeException|Error failure){popupControlDone(operation);if(operation.control!=null)operation.control.close();throw failure;}
    }
    private void popupControlManual(PopupControlRequest operation)throws java.io.IOException {
        if(!popupControlIdentity(operation.entry))throw new java.io.IOException("network_owner_changed");
        if(operation.manualPublished)return;
        operation.entry.profile.block(MdoBrowserDocument.MANUAL,true);
        if(!popupControlIdentity(operation.entry))throw new java.io.IOException("network_owner_changed");
        manual.add(operation.entry.key);operation.manualPublished=true;
    }
    private void popupControlPoll(PopupControlRequest operation){
        if(operation.done||operation.starting||popupControls.get(operation.entry.key)!=operation)return;
        networkHandler.removeCallbacks(operation.watch);
        try{
            if(operation.cancellation!=null){popupControlFinish(operation,operation.cancellation);return;}
            if(!operation.timely()){popupControlFinish(operation,"network_control_timeout");return;}
            if(!popupControlOwner(operation.entry)){popupControlFinish(operation,"network_owner_changed");return;}
            MdoBrowserCdpPopupNative.HumanControl control=operation.control;
            if(control==null){popupControlFinish(operation,"network_control_unconfirmed");return;}
            if(!control.committed()){
                control.expire();if(control.failure()!=null)throw new java.io.IOException(control.failure());
                if(!control.current())throw new java.io.IOException("cdp_popup_human_owner_invalid");
                if(operation.popup.controlReady(control))operation.popup.completeControl(control);
            }
            if(control.committed()){
                popupControlManual(operation);operation.popup.watch();
                if(!popupControlOwner(operation.entry))throw new java.io.IOException("network_owner_changed");
                MdoBrowserCdpSession.ControlResult transfer=operation.popup.handoff.humanDetachResult();
                if(transfer!=null&&!transfer.ok)throw new java.io.IOException(transfer.code==null?"network_control_unconfirmed":transfer.code);
                if(operation.entry.protocolDrained&&operation.popup.handoff.humanDetached()&&!networkCleanupFailed){popupControlFinish(operation,null);return;}
                if(networkCleanupFailed||operation.entry.cleanupObservationExpired)throw new java.io.IOException("network_control_cleanup_unconfirmed");
            }
            if(!networkHandler.postDelayed(operation.watch,50))popupControlFinish(operation,"network_control_scheduler_unavailable");
        }catch(java.io.IOException|RuntimeException|Error failure){popupControlFinish(operation,popupControlCode(failure.getMessage()));}
    }
    private void popupControlDone(PopupControlRequest operation){operation.done=true;popupControls.remove(operation.entry.key,operation);networkHandler.removeCallbacks(operation.watch);}
    private MdoBrowserDocument popupControlDocument(WebView view,MdoBrowserDocument current){
        if(popupControls.isEmpty())return null;
        for(PopupControlRequest operation:popupControls.values())if(operation.entry.view==view&&operation.entry.profile!=current)return operation.entry.profile;
        return null;
    }
    private void popupControlCancel(PopupControlRequest operation,String reason){if(operation.done)return;if(operation.cancellation==null)operation.cancellation=reason;if(!operation.starting)popupControlFinish(operation,operation.cancellation);}
    private void cancelPopupControls(){for(PopupControlRequest operation:new ArrayList<>(popupControls.values()))popupControlCancel(operation,"network_control_cancelled");}
    private void popupControlDispose(PopupControlRequest operation){
        NetworkEntry entry=operation.entry;if(views.nativeDestroyed(entry.view))return;
        if(views.owns(entry.key,entry.view))retireNativeDocument(entry.key,entry.view);
        else{
            // A reused key cannot supply any of the replacement's resources.
            NativeDestruction row=prepareNativeDestruction(entry.key,entry.view,operation.windowsOwner,operation.dialog,operation.capture,entry.profile);
            cdpViews.invalidate(entry.key,entry.view,operation.epoch,"input_closed");
            if(operation.windowsOwner!=null){windows.remove(entry.key,operation.windowsOwner);operation.windowsOwner.close();}
            if(operation.dialog!=null){dialogs.remove(entry.key,operation.dialog);operation.dialog.close();}
            if(operation.capture!=null){captures.remove(entry.key,operation.capture);operation.capture.capture.close();}
            documents.remove(entry.key,entry.profile);entry.profile.close();row.prepared=true;executeNativeDestruction(row);
        }
        if(views.nativeDestroyed(entry.view)){operation.popup.destroyed();if(entry.downloads!=null)entry.downloads.close();}
        if(views.get(entry.key)==null){manual.remove(entry.key);loading.remove(entry.key);errors.remove(entry.key);errorCodes.remove(entry.key);crashed.remove(entry.key);locations.remove(entry.key);}
    }
    private void popupControlFinish(PopupControlRequest operation,String failure){
        if(operation.done)return;operation.done=true;networkHandler.removeCallbacks(operation.watch);
        MdoBrowserCdpPopupNative.HumanControl control=operation.control;boolean committed=control!=null&&control.committed();
        try{
            if(committed){
                if(popupControlIdentity(operation.entry))popupControlManual(operation);else popupControlDispose(operation);
            }else{
                if(control!=null)control.close();
                if(operation.popup.human&&operation.popup.handoff.retiring()&&!popupControlIdentity(operation.entry))popupControlDispose(operation);
            }
        }catch(java.io.IOException|RuntimeException|Error cleanup){failure="network_control_cleanup_unconfirmed";try{popupControlDispose(operation);}catch(RuntimeException|Error unconfirmed){nativeViewCleanupFailed=true;}}
        popupControlDone(operation);NetworkEntry entry=operation.entry;
        if(channel.current()!=entry.channelLease||!channel.active(entry.channelLease,host.getUrl()))return;
        JSONObject result;
        try{
            boolean actualManual=committed&&operation.popup.owner.humanTransferred()&&!operation.popup.owner.nativeDestroyed()&&
                views.owns(entry.key,entry.view)&&documents.get(entry.key)==entry.profile&&manual.contains(entry.key);
            boolean clean=entry.protocolDrained&&!networkCleanupFailed;
            if(failure==null&&!operation.timely())failure="network_control_timeout";
            if(failure==null&&!popupControlOwner(entry))failure="network_owner_changed";
            if(failure==null&&(!actualManual||!clean))failure="network_control_cleanup_unconfirmed";
            result=new JSONObject().put("ok",failure==null).put("manual",actualManual);
            if(operation.popup.human||control!=null)result.put("network_cleanup_confirmed",clean);
            if(failure!=null)result.put("error",new JSONObject().put("code",failure));
        }catch(JSONException|RuntimeException|Error unavailable){return;}
        // Mark terminal before attempting original-channel delivery. A throw
        // after native delivery must never cause a second reply or handoff.
        try{reply(entry.channelLease,operation.id,result,null);}catch(RuntimeException|Error unavailable){/* original outcome is terminal */}
    }
    /** Admission must precede document/input cancellation. A refused control
     * request leaves the original Agent page and outstanding read untouched. */
    private void networkTakeoverAdmission(String key,WebView view){
        NetworkEntry original=networkViews.get(key);if(original!=null&&original.view==view&&original.popup!=null)throw new UnsupportedOperationException("popup_takeover_unqualified");
    }
    private void networkTakeover(String key,WebView view){
        networkTakeoverAdmission(key,view);
        retireProtocolPopupPreparation(key,view,"cdp_popup_preparation_cancelled");
        NetworkEntry entry=networkViews.get(key);if(entry==null||entry.view!=view||entry.retired||entry.draining)return;
        if(!entry.ready||entry.marker==null||entry.downloads==null){networkRetire(entry,"network_user_takeover");return;}
        entry.draining=true;entry.ready=false;entry.retiredAt=android.os.SystemClock.elapsedRealtime();entry.code="network_user_takeover";
        nativeNavigationRetire(entry);
        // Explicit human takeover releases only the original UI reservation;
        // its native receipt remains retained and cannot restore a later page.
        networkHandler.removeCallbacks(entry.watch);
        try{
            entry.downloads.quiesce();cdpWorker.retireNetwork(entry.marker.lease());
            cancelNavigationRequest(key,view);
            if(!networkHandler.postDelayed(entry.watch,100))throw new IllegalStateException("network_retirement_scheduler_unavailable");
        }catch(RuntimeException|Error failure){networkRetire(entry,"network_retirement_failed");}
    }
    /** Human return-control is asynchronous: keep manual admission blocked
     * until fresh native proof/frame bootstrap/Fetch.enable are acknowledged. */
    private boolean networkReleaseControl(String key,WebView view,String id,boolean agent)throws java.io.IOException {
        NetworkEntry entry=networkViews.get(key);if(entry==null||entry.view!=view)return false;
        if(entry.popup!=null){
            if(!entry.popup.human)throw new UnsupportedOperationException("popup_takeover_unqualified");
            if(agent)throw new IllegalStateException("user_takeover");
            entry.popup.watch();if(!entry.popup.returnable())throw new IllegalStateException("network_retiring");
        }
        if(entry.draining)throw new IllegalStateException("network_retiring");
        if(!entry.retired){if(entry.rearm&&entry.starting)throw new IllegalStateException("network_starting");return false;}
        if(agent)throw new IllegalStateException("user_takeover");
        if(!entry.protocolDrained||!networkClean())throw new IllegalStateException("network_retiring");
        if(!manual.contains(key)||stopped||browserPaused||!views.owns(key,view)||documents.get(key)!=entry.profile||!entry.profile.profileAssigned()||
            pageClosed.contains(key)||channel.current()!=entry.channelLease||!channel.active(entry.channelLease,host.getUrl()))throw new IllegalStateException("network_owner_unavailable");
        if(Boolean.TRUE.equals(loading.get(key)))throw new IllegalStateException("network_rearm_page_loading");
        if(!networkReturnUnblocked(entry))throw new IllegalStateException("network_return_blocked");
        if(entry.downloads==null)throw new IllegalStateException("network_owner_unavailable");
        NetworkEntry next=new NetworkEntry(key,view,entry.profile,entry.channelLease,id,null,entry.quota);
        next.downloads=entry.downloads;next.rearm=true;next.warming=false;next.starting=true;next.created=true;next.warmupEpoch=views.epoch(key,view);
        next.navigation=entry.navigation;
        next.downloadConnection=entry.downloads.rearm();
        networkHandler.removeCallbacks(entry.watch);networkViews.put(key,next);
        networkStart(next);return true;
    }
    private boolean networkReturnUnblocked(NetworkEntry entry){
        CaptureEntry capture=captures.get(entry.key);MdoBrowserPopups<WebView> popup=windows.get(entry.key);
        return !nativeViewCleanupFailed&&nativeRetiringViews.isEmpty()&&MdoBrowserCdpPopupNative.creationConfirmed()&&networkClean()&&
            views.inputClean(entry.key,entry.view)&&dialogReady(entry.key,entry.view)&&chooserReady(dialogs.get(entry.key))&&!uploadBusy(entry.key,entry.view)&&
            (capture==null||capture.view==entry.view&&!capture.capture.busy()&&capture.capture.cleanupConfirmed())&&
            (popup==null||popup.clean()&&popup.snapshot()==null);
    }
    private void networkBegin(String key,WebView view,String id,String target){
        MdoBrowserDocument profile=documents.get(key);MdoBrowserChannel<WebMessagePort>.Lease lease=channel.current();
        if(profile==null||!profile.profileAssigned()||lease==null||!channel.active(lease,host.getUrl()))throw new IllegalStateException("network_profile_or_channel_unconfirmed");
        if(networkViews.containsKey(key)||networkViews.size()>=8||!networkClean())throw new IllegalStateException("network_owner_unavailable");
        for(NetworkEntry old:networkViews.values())if(old.view==view)throw new IllegalStateException("network_owner_already_registered");
        NetworkEntry entry=new NetworkEntry(key,view,profile,lease,id,target);networkViews.put(key,entry);
        try{
            if(!networkCurrent(entry))throw new IllegalStateException("network_owner_changed");
            if(!networkHandler.postDelayed(entry.watch,100))throw new IllegalStateException("network_scheduler_unavailable");
            view.loadUrl("about:blank");
        }catch(RuntimeException|Error failure){networkRetire(entry,"network_warmup_failed");}
    }
    private void networkNavigation(String key,WebView view,String address){
        NetworkEntry entry=networkViews.get(key);if(entry==null||entry.view!=view||entry.retired||entry.ready)return;
        if(entry.popup!=null)return; // original Browser frame proof owns bootstrap, never a replacement blank load
        if(entry.rearm){networkRetire(entry,"network_rearm_navigation_changed");return;}
        if(!entry.warming||entry.warmupEpoch!=0||!"about:blank".equals(address)||!networkCurrent(entry)){
            networkRetire(entry,"network_bootstrap_navigation_changed");return;
        }
        entry.warmupEpoch=views.epoch(key,view);
    }
    /** Return true while this completion belongs to the initial native blank
     * document, so it cannot clear the target navigation's loading state. */
    private boolean networkLoaded(String key,WebView view,String address){
        NetworkEntry entry=networkViews.get(key);if(entry==null||entry.view!=view||entry.retired||entry.ready)return false;
        if(entry.popup!=null)return true;
        if(!entry.warming)return true;
        if(!"about:blank".equals(address)||!"about:blank".equals(view.getUrl())||entry.warmupEpoch<=0||views.epoch(key,view)!=entry.warmupEpoch||!networkCurrent(entry)){
            networkRetire(entry,"network_bootstrap_navigation_changed");return true;
        }
        entry.warming=false;entry.starting=true;
        networkStart(entry);return true;
    }
    private void networkStart(NetworkEntry entry){
        try{
            final WebView view=entry.view;
            int budget=networkRemaining(entry,100);
            if(!networkCurrent(entry)||entry.rearm&&(Boolean.TRUE.equals(loading.get(entry.key))||views.epoch(entry.key,view)!=entry.warmupEpoch||!networkReturnUnblocked(entry)))throw new IllegalStateException("network_owner_changed");
            entry.adapter=MdoBrowserCdpView.network(view,owned->owned==view&&networkCurrent(entry)&&entry.live.get(),
                owned->owned==view&&networkCurrent(entry)&&entry.live.get()&&entry.ready&&!entry.retired&&!entry.draining&&!manual.contains(entry.key),entry.quota);
            entry.marker=entry.adapter.marker();
            // The worker callback uses only atomic admission and the original
            // native connection. Never consult UI objects from this thread.
            final java.util.concurrent.atomic.AtomicBoolean live=entry.live;
            if(entry.downloads==null){entry.downloads=newNetworkDownloads();entry.downloadConnection=entry.downloads.connection();}
            final MdoBrowserCdpDownloads.Connection downloads=entry.downloadConnection;
            entry.protocolDrained=false;
            if(!networkHandler.postDelayed(entry.watch,100))throw new IllegalStateException("network_scheduler_unavailable");
            cdpWorker.startNetwork(entry.marker,budget,new MdoBrowserCdpNetworks.Events(){
                public void event(MdoBrowserCdpBinding.Network connection,String method,String child,JSONObject params)throws java.io.IOException {
                    if(!live.get())throw new java.io.IOException("cdp_network_cancelled");downloads.event(connection,method,child,params);
                }
                public void turn(MdoBrowserCdpBinding.Network connection)throws java.io.IOException {
                    if(!live.get())throw new java.io.IOException("cdp_network_cancelled");downloads.turn(connection);
                }
                public boolean navigationAllowed(){return live.get()&&downloads.navigationAllowed();}
            },result->networkResult(entry,result));
        }catch(java.io.IOException|RuntimeException|Error failure){
            entry.protocolDrained=entry.marker==null||!cdpWorker.networkPending(entry.marker);
            if(entry.protocolDrained&&entry.downloadConnection!=null)entry.downloadConnection.disconnect(true);
            String code=failure instanceof java.io.IOException?networkProtocolCode(failure.getMessage(),"network_start_failed"):"network_start_failed";
            networkRetire(entry,code);
        }
    }
    private int networkRemaining(NetworkEntry entry,int minimum){
        long used=android.os.SystemClock.elapsedRealtime()-entry.born;
        if(used<0||used>15000-minimum)throw new IllegalStateException("network_start_timeout");return (int)(15000-used);
    }
    private void networkResult(NetworkEntry entry,MdoBrowserCdpNetworks.Result result){
        if(networkViews.get(entry.key)!=entry)return;
        if(result.ready){
            if(entry.ready&&!entry.retired)return;
            try{
                networkRemaining(entry,10);
                if(entry.retired||!networkCurrent(entry)||views.epoch(entry.key,entry.view)!=entry.warmupEpoch||
                    (entry.rearm?Boolean.TRUE.equals(loading.get(entry.key))||!networkReturnUnblocked(entry):!"about:blank".equals(entry.view.getUrl())))throw new IllegalStateException("network_owner_changed");
                if(entry.rearm){
                    JSONObject outcome=new JSONObject().put("manual",false).put("downloads_intercepted",true);
                    entry.profile.block(MdoBrowserDocument.MANUAL,false);views.manual(entry.key,entry.view,false);manual.remove(entry.key);
                    if(!networkCurrent(entry)||entry.retired||views.epoch(entry.key,entry.view)!=entry.warmupEpoch||Boolean.TRUE.equals(loading.get(entry.key))||!networkReturnUnblocked(entry))throw new IllegalStateException("network_owner_changed");
                    entry.starting=false;entry.ready=true;networkHandler.removeCallbacks(entry.watch);entry.replySent=true;
                    reply(entry.channelLease,entry.id,outcome,null);return;
                }
                entry.starting=false;entry.ready=true;networkHandler.removeCallbacks(entry.watch);
                loading.put(entry.key,false);
                NativeNavigation initial="about:blank".equals(entry.target)?null:beginNativeNavigation(entry,entry.target,networkRemaining(entry,100),true);
                if(!networkCurrent(entry)||entry.retired)throw new IllegalStateException("network_owner_changed");
                JSONObject outcome=new JSONObject().put("created",true).put("downloads_intercepted",true);
                if(initial!=null)outcome.put("navigation_ref",initial.id).put("initial_navigation_pending",true);
                entry.created=true;entry.replySent=true;
                reply(entry.channelLease,entry.id,outcome,null);
            }catch(Exception|Error failure){
                if(entry.rearm&&views.owns(entry.key,entry.view)){manual.add(entry.key);
                    try{entry.profile.block(MdoBrowserDocument.MANUAL,true);views.manual(entry.key,entry.view,true);}catch(RuntimeException|Error ignored){}}
                String code=Arrays.asList("network_start_timeout","network_owner_changed").contains(failure.getMessage())?failure.getMessage():networkProtocolCode(failure.getMessage(),"network_start_failed");
                networkRetire(entry,networkTargetMayHaveOccurred(entry)?"network_navigation_uncertain":code);
            }
            return;
        }
        entry.protocolDrained=result.cleanupConfirmed;
        entry.responsesDisabled=result.responsesDisabled;
        if(!result.cleanupConfirmed)networkCleanupFailed=true;
        if(!entry.retired){entry.retired=true;entry.retiredAt=android.os.SystemClock.elapsedRealtime();
            if(!entry.draining||!"cdp_network_retired".equals(result.code))entry.code=networkProtocolCode(result.code,"network_connection_failed");}
        entry.draining=false;entry.ready=false;
        entry.live.set(false);networkHandler.removeCallbacks(entry.watch);
        if(entry.marker!=null)try{entry.marker.close();}catch(RuntimeException|Error failure){networkCleanupFailed=true;}
        if(entry.downloadConnection!=null)try{entry.downloadConnection.disconnect(result.cleanupConfirmed);}catch(RuntimeException|Error failure){networkCleanupFailed=true;}
        if(!entry.replySent)networkReplyFailure(entry);
        else if(entry.protocolDrained)networkDiscardUnsent(entry);
        networkRelease(entry);
    }
    private void networkRetire(NetworkEntry entry,String reason){
        if(networkViews.get(entry.key)!=entry)return;
        if(entry.popup!=null){entry.popup.retire(reason);return;}
        retireProtocolPopupPreparation(entry.key,entry.view,"cdp_popup_preparation_owner_changed");
        nativeNavigationRetire(entry);
        entry.draining=false;
        if(!entry.retired){entry.retired=true;entry.ready=false;entry.code=reason;entry.retiredAt=android.os.SystemClock.elapsedRealtime();}
        entry.live.set(false);networkHandler.removeCallbacks(entry.watch);
        try{
            if(entry.marker!=null){entry.marker.close();cdpWorker.cancelNetwork(entry.marker.lease());}
            if(entry.downloadConnection!=null)entry.downloadConnection.disconnect();
        }catch(RuntimeException|Error failure){networkCleanupFailed=true;}
        if(entry.protocolDrained){if(!entry.replySent)networkReplyFailure(entry);networkRelease(entry);}
        else if(!networkHandler.postDelayed(entry.watch,100)){
            entry.cleanupObservationExpired=true;if(!entry.replySent)networkReplyFailure(entry);
        }
    }
    private void networkRetire(String key,WebView view,String reason){NetworkEntry entry=networkViews.get(key);if(entry!=null&&entry.view==view)networkRetire(entry,reason);}
    private void networkRetireAll(String reason){for(NetworkEntry entry:new ArrayList<>(networkViews.values()))networkRetire(entry,reason);}
    private void networkWatch(NetworkEntry entry){
        if(networkViews.get(entry.key)!=entry)return;
        if(entry.popup!=null){entry.popup.watch();return;}
        long now=android.os.SystemClock.elapsedRealtime();
        if(entry.draining){
            if(!networkCurrent(entry)||now-entry.retiredAt<0||now-entry.retiredAt>=5500){networkRetire(entry,!networkCurrent(entry)?"network_owner_changed":"network_retirement_timeout");return;}
        }else if(entry.retired){
            if(!entry.protocolDrained&&cdpWorker.terminated()){
                if(entry.marker==null||!cdpWorker.networkPending(entry.marker))entry.protocolDrained=true;else networkCleanupFailed=true;
            }
            if(entry.protocolDrained){if(!entry.replySent)networkReplyFailure(entry);networkRelease(entry);return;}
            if(now-entry.retiredAt<0||now-entry.retiredAt>=5000){entry.cleanupObservationExpired=true;if(!entry.replySent)networkReplyFailure(entry);return;}
        }else if(!networkCurrent(entry)||now-entry.born<0||now-entry.born>=15000){
            networkRetire(entry,!networkCurrent(entry)?"network_owner_changed":"network_start_timeout");return;
        }
        if(!networkHandler.postDelayed(entry.watch,100)){entry.cleanupObservationExpired=true;networkRetire(entry,"network_scheduler_unavailable");}
    }
    private void networkReplyFailure(NetworkEntry entry){
        if(entry.replySent)return;entry.replySent=true;
        if(entry.rearm){
            try{reply(entry.channelLease,entry.id,new JSONObject().put("ok",false).put("manual",true)
                .put("error",new JSONObject().put("code",entry.code==null?"network_start_failed":entry.code))
                .put("network_cleanup_confirmed",entry.protocolDrained&&!networkCleanupFailed),null);}catch(JSONException|RuntimeException|Error ignored){}
            return;
        }
        networkDiscardUnsent(entry);
        if(views.owns(entry.key,entry.view)){pageClosed.add(entry.key);loading.put(entry.key,false);errors.put(entry.key,entry.code==null?"network_start_failed":entry.code);}
        try{reply(entry.channelLease,entry.id,new JSONObject().put("ok",false).put("created",false)
            .put("error",new JSONObject().put("code",entry.code==null?"network_start_failed":entry.code))
            .put("native_create",new JSONObject().put("created_may_have_occurred",true).put("navigation_may_have_occurred",networkTargetMayHaveOccurred(entry))
                .put("network_cleanup_confirmed",entry.protocolDrained&&!networkCleanupFailed).put("view_cleanup_confirmed",entry.destroyed&&!nativeViewCleanupFailed)),null);
        }catch(JSONException|RuntimeException|Error ignored){}
    }
    private void networkDiscardUnsent(NetworkEntry entry){
        if(!entry.rearm&&!entry.created&&!networkTargetMayHaveOccurred(entry)&&entry.protocolDrained&&views.owns(entry.key,entry.view)){
            try{closeAndDestroyDocument(entry.key,entry.view);}catch(RuntimeException|Error failure){nativeViewCleanupFailed=true;}
            if(views.get(entry.key)==null){manual.remove(entry.key);loading.remove(entry.key);errors.remove(entry.key);errorCodes.remove(entry.key);crashed.remove(entry.key);locations.remove(entry.key);}
        }
    }
    private void networkDestroyed(String key,WebView view){
        NetworkEntry entry=networkViews.get(key);if(entry==null||entry.view!=view)return;
        if(entry.popup!=null){entry.popup.destroyed();return;}
        entry.destroyed=true;entry.quota.close();networkRelease(entry);
    }
    private void networkDestroyed(WebView view){for(NetworkEntry entry:new ArrayList<>(networkViews.values()))if(entry.view==view){networkDestroyed(entry.key,view);return;}}
    private void networkRelease(NetworkEntry entry){
        if(entry.popup!=null){entry.popup.watch();return;}
        if(entry.destroyed&&entry.downloads!=null)try{entry.downloads.close();}catch(RuntimeException|Error failure){networkCleanupFailed=true;}
        if(entry.destroyed&&entry.protocolDrained){
            networkHandler.removeCallbacks(entry.watch);
            if(entry.downloads==null||entry.downloads.retiringFiles()==0){networkViews.remove(entry.key,entry);return;}
            long age=android.os.SystemClock.elapsedRealtime()-entry.retiredAt;
            if(age<0||age>=5000){entry.cleanupObservationExpired=true;networkCleanupFailed=true;return;}
            if(!networkHandler.postDelayed(entry.watch,100)){entry.cleanupObservationExpired=true;networkCleanupFailed=true;}
        }
    }
    private String networkProtocolCode(String code,String fallback){return code!=null&&code.matches("(?:cdp|download_frame)_[a-z_]{1,80}")?code:fallback;}
    private MdoBrowserCdpDownloads newNetworkDownloads(){MdoBrowserDownloadRuntime service=MdoBrowserDownloadRuntime.get(activity);return new MdoBrowserCdpDownloads(service.files,service.budget);}
    private JSONObject networkState(String key)throws JSONException {
        NetworkEntry entry=networkViews.get(key);
        return new JSONObject().put("installed",entry!=null).put("ready",entry!=null&&entry.ready&&!entry.retired)
            .put("phase",entry==null?"none":entry.draining?"draining":entry.retired?"retired":entry.ready?"ready":entry.rearm?"rearming":entry.warming?"warming":"starting")
            .put("responses_disabled",entry!=null&&entry.responsesDisabled)
            .put("code",entry==null||entry.code==null?JSONObject.NULL:entry.code)
            .put("cleanup_observation_expired",entry!=null&&entry.cleanupObservationExpired)
            .put("cleanup_failed",networkCleanupFailed).put("retiring",networkRetiring());
    }
    /** Bounded final receipts for the original close, never a second teardown.
     * The UI handler is shared with existing native cleanup observation. */
    private final Map<String,BrowserClose> browserCloses=new HashMap<>();
    private final class BrowserClose {
        final String id;final MdoBrowserChannel<WebMessagePort>.Lease lease;final NetworkEntry network;
        final MdoBrowserDocument document;final MdoBrowserDialogTarget dialog;
        final PopupHandoff popup;
        final long born=android.os.SystemClock.elapsedRealtime();final Runnable watch=()->browserClosePoll(this);
        NativeDestruction nativeClose;boolean prepared;
        BrowserClose(String id,MdoBrowserChannel<WebMessagePort>.Lease lease,NetworkEntry network,MdoBrowserDocument document,MdoBrowserDialogTarget dialog,PopupHandoff popup){this.id=id;this.lease=lease;this.network=network;this.document=document;this.dialog=dialog;this.popup=popup;}
    }
    private void executeBrowserClose(String key,String id){
        if(id==null||id.isEmpty()||id.length()>512||browserCloses.containsKey(id)||browserCloses.size()>=32)throw new IllegalStateException("browser_close_busy");
        MdoBrowserChannel<WebMessagePort>.Lease lease=channel.current();
        if(stopped||lease==null||!channel.active(lease,host.getUrl()))throw new IllegalStateException("browser_close_channel_changed");
        WebView view=views.get(key);NativeDestruction pending=null;
        if(view==null)for(NativeDestruction row:nativeDestructions.values())if(key.equals(row.key)){pending=row;break;}
        NetworkEntry network=networkViews.get(key);WebView exact=view!=null?view:pending==null?null:pending.view;
        if(network!=null&&exact!=null&&network.view!=exact)network=null;
        BrowserClose operation=new BrowserClose(id,lease,network,pending==null?documents.get(key):pending.document,pending==null?dialogs.get(key):pending.dialog,protocolPopupHandoffs.get(key));
        try{
            browserCloses.put(id,operation);
            if(operation.popup!=null)operation.popup.close();
            if(view!=null)operation.nativeClose=retireNativeDocument(key,view);
            else if(pending!=null)operation.nativeClose=pending;
            else closeDocument(key);
            // Logical retirement may call into UI callbacks. Do not clear a
            // replacement's metadata if one was registered by such a callback.
            if(views.get(key)==null){manual.remove(key);loading.remove(key);errors.remove(key);errorCodes.remove(key);crashed.remove(key);locations.remove(key);}
            operation.prepared=true;browserClosePoll(operation);
        }catch(RuntimeException|Error failure){browserCloseDone(operation);reply(operation.lease,operation.id,null,"browser_close_unconfirmed");}
    }
    private void browserClosePoll(BrowserClose operation){
        if(browserCloses.get(operation.id)!=operation||!operation.prepared)return;
        networkHandler.removeCallbacks(operation.watch);
        if(stopped||channel.current()!=operation.lease||!channel.active(operation.lease,host.getUrl())){browserCloseDone(operation);return;}
        try{
            NativeDestruction row=operation.nativeClose;NetworkEntry network=operation.network;
            boolean physical=row==null||row.complete;
            boolean popupDone=operation.popup==null||operation.popup.cleanupConfirmed();
            boolean networkDone=popupDone&&(network==null||network.protocolDrained&&(network.downloads==null||network.downloads.retiringFiles()==0));
            boolean failed=nativeViewCleanupFailed||networkCleanupFailed||row!=null&&row.failed;
            boolean clean=physical&&networkDone&&!failed&&(operation.document==null||operation.document.cleanupConfirmed())&&
                (operation.dialog==null||operation.dialog.dialogs.clean())&&chooserClean(operation.dialog);
            long used=android.os.SystemClock.elapsedRealtime()-operation.born;
            boolean expired=used<0||used>=5000;
            clean=clean&&!expired;
            boolean schedulerFailed=false;
            if(!physical||!networkDone){
                if(!failed&&!expired){if(networkHandler.postDelayed(operation.watch,100))return;schedulerFailed=true;}
            }
            JSONObject result=new JSONObject().put("world_cleanup_confirmed",clean).put("native_view_cleanup_failed",nativeViewCleanupFailed)
                .put("native_view_retiring",nativeRetiringViews.size()).put("network_cleanup_failed",networkCleanupFailed).put("network_retiring",networkRetiring())
                .put("close_observation_expired",!clean&&expired).put("close_observation_failed",schedulerFailed).put("native_view_closed",physical).put("network_cleanup_confirmed",networkDone&&!networkCleanupFailed).put("popup_cleanup_confirmed",popupDone);
            browserCloseDone(operation);reply(operation.lease,operation.id,result,null);
        }catch(JSONException|RuntimeException|Error failure){browserCloseDone(operation);reply(operation.lease,operation.id,null,"browser_close_unconfirmed");}
    }
    private void browserCloseDone(BrowserClose operation){browserCloses.remove(operation.id,operation);networkHandler.removeCallbacks(operation.watch);}
    private void cancelBrowserCloses(){for(BrowserClose operation:new ArrayList<>(browserCloses.values()))browserCloseDone(operation);}
    private boolean nativeViewCleanupFailed;
    private final Set<WebView> nativeRetiringViews=Collections.newSetFromMap(new IdentityHashMap<WebView,Boolean>());
    private final Map<WebView,NativeDestruction> nativeDestructions=new IdentityHashMap<>();
    private boolean bulkNativeDestruction;
    private void nativeCreationAdmission(){
        nativeOwnerAdmission();
        if(protocolPopupCreationBlocked())throw new IllegalStateException("native_popup_protection_busy");
    }
    /** Common cleanup barriers also apply to typed existing-owner operations.
     * Process-wide protection fences unrelated construction, not handoff of an
     * already allocated/proved child. Those callers validate their own receipts. */
    private void nativeOwnerAdmission(){
        if(stopped)throw new IllegalStateException("native_browser_closed");
        MdoBrowserCdpPopupNative.reconcileRetirements();
        if(nativeViewCleanupFailed)throw new IllegalStateException("native_view_cleanup_failed");
        if(!nativeRetiringViews.isEmpty())throw new IllegalStateException("native_view_retirement_busy");
        if(networkCleanupFailed)throw new IllegalStateException("network_cleanup_failed");
        if(networkRetiring()!=0||downloadRetiring()!=0)throw new IllegalStateException("native_view_retirement_busy");
    }
    private final Map<String,MdoBrowserDocument> documents=new HashMap<>();
    private final Map<String,String> documentFailures=new HashMap<>();
    private final MdoBrowserWorld.Budget worldBudget=new MdoBrowserWorld.Budget(32);
    private final MdoBrowserCdpWorker cdpWorker=new MdoBrowserCdpWorker();
    private final MdoBrowserCdpRegistry<WebView> cdpViews=new MdoBrowserCdpRegistry<>((view,document,current)->
        new MdoBrowserCdpView(view,owned->owned==view&&current.getAsBoolean(),document).marker(),cdpWorker::cancel);
    private boolean browserPaused;
    // Exact private ownership for the asynchronous frame command route.
    // Product/profile admission is required before requesting this marker.
    private boolean cdpEnvironmentCurrent(String key,WebView view,long epoch,MdoBrowserChannel<WebMessagePort>.Lease original){
        MdoBrowserDialogTarget target=dialogs.get(key);
        return !stopped&&!browserPaused&&views.owns(key,view)&&views.epoch(key,view)==epoch&&view.isShown()&&!manual.contains(key)&&!pageClosed.contains(key)&&
            !loading.getOrDefault(key,true)&&channel.current()==original&&channel.active(original,host.getUrl())&&
            (target==null||target.view==view&&target.dialogs.clean()&&target.dialogs.snapshot()==null)&&
            (!windows.containsKey(key)||windows.get(key).clean()&&windows.get(key).snapshot()==null);
    }
    /** Continue the original bound document; never discover, restore or create
     * a realm. Protected popups register their handoff as the registry owner,
     * while ordinary documents register the UI channel. These owners are not
     * interchangeable even when the physical view and navigation are equal. */
    private MdoBrowserCdpMarker existingDocumentMarker(String key,WebView view,long epoch,MdoBrowserChannel<WebMessagePort>.Lease original)throws java.io.IOException {
        if(!cdpEnvironmentCurrent(key,view,epoch,original))throw new java.io.IOException("cdp_binding_owner_changed");
        PopupPage popup=protocolPopupDocument(key,view);
        if(popup==null){requireLegacyDocumentRoute(key);return cdpViews.existing(key,view,epoch,original);}
        MdoBrowserCdpMarker marker=cdpViews.existing(key,view,epoch,popup.network.handoff);
        if(marker!=popup.marker||marker.lease()!=popup.proof.lease)throw new java.io.IOException("cdp_popup_document_owner_invalid");
        return marker;
    }
    private MdoBrowserCdpMarker cdpMarker(String key,WebView view){
        final long epoch=views.epoch(key,view);final MdoBrowserChannel<WebMessagePort>.Lease original=channel.current();
        return cdpViews.marker(key,view,epoch,original,()->cdpEnvironmentCurrent(key,view,epoch,original)&&chooserReady(dialogs.get(key)));
    }
    private void bindDocument(String key,WebView view,boolean automation,MdoBrowserChannel<WebMessagePort>.Lease original){
        if(documents.containsKey(key))closeDocument(key);
        documentFailures.remove(key);
        if(android.os.Build.VERSION.SDK_INT<28){documentFailures.put(key,"provider_loader_unsupported");if(automation)throw new UnsupportedOperationException("provider_loader_unsupported");return;}
        MdoBrowserDocument document=null;
        try{
            document=prepareDocument(key,view);
            document.initialize(WebView.getWebViewClassLoader(),new MdoBrowserWorld.Scheduler(){
                private final android.os.Handler handler=new android.os.Handler(android.os.Looper.getMainLooper());
                public long now(){return android.os.SystemClock.elapsedRealtime();}
                public void post(Runnable task,long delay){if(!handler.postDelayed(task,delay))throw new IllegalStateException("World scheduler stopped");}
                public void remove(Runnable task){handler.removeCallbacks(task);}
            },automation?"mdo.browser-use":null,worldBudget,()->channel.current()==original&&channel.active(original,host.getUrl()));
            document.block(MdoBrowserDocument.BACKGROUND,browserPaused);
            document.block(MdoBrowserDocument.CHANNEL,channel.current()==null);
            if(documents.get(key)!=document||!views.owns(key,view))throw new IllegalStateException("native_view_owner_changed");
        }catch(MdoBrowserWorld.Failure failure){
            if(document!=null)document.close();
            if(views.owns(key,view))documentFailures.put(key,failure.code);
            if(!automation&&document!=null){if(!document.cleanupConfirmed())throw new UnsupportedOperationException(failure.code);documents.remove(key,document);}
            // The create route owns the one native retirement attempt. Keep
            // this exact view registered until its failure handler tears down
            // input, document and transfer state together; never destroy here.
            if(automation)throw new UnsupportedOperationException(failure.code);
        }
    }
    private MdoBrowserDocument prepareDocument(String key,WebView view)throws MdoBrowserWorld.Failure {
        MdoBrowserDocument document=MdoBrowserDocument.prepare(view,()->!stopped&&views.owns(key,view),()->views.epoch(key,view));
        documents.put(key,document); // Publish logical ownership before native callbacks.
        document.whenInitialized(()->{NativeDestruction row=nativeDestructions.get(view);if(row!=null&&row.prepared)executeNativeDestruction(row);});
        return document;
    }
    private boolean closeDocument(String key){cancelUpload(key);
        WebView networkView=views.get(key);if(networkView!=null)networkRetire(key,networkView,"network_document_closed");
        WebView owned=views.get(key);if(owned!=null)cdpViews.invalidate(key,owned,views.epoch(key,owned),"input_cancelled");
        MdoBrowserPopups<WebView> popup=windows.get(key);if(popup!=null)popup.close();
        navigationRequests.remove(key);
        MdoBrowserDialogTarget dialog=dialogs.get(key);if(dialog!=null)dialog.close();
        CaptureEntry capture=captures.get(key);if(capture!=null)capture.capture.close();
        MdoBrowserDocument document=documents.remove(key);
        documentFailures.remove(key);
        if(document!=null)document.close();
        return !nativeViewCleanupFailed&&nativeRetiringViews.isEmpty()&&networkClean()&&(document==null||document.cleanupConfirmed())&&(dialog==null||dialog.dialogs.clean())&&chooserClean(dialog);
    }
    /** Both single-view and whole-host close use this physical teardown once.
     * Captured resources cannot later be replaced through a reused tab key. */
    private final class NativeDestruction implements MdoBrowserCdpPopupNative.Destruction {
        final String key;final WebView view;final MdoBrowserPopups<WebView> popup;final MdoBrowserDialogTarget dialog;final CaptureEntry capture;final MdoBrowserDocument document,previousDocument;
        boolean prepared,running,destroyed,complete,failed,clean,controlled;
        NativeDestruction(String key,WebView view,MdoBrowserPopups<WebView> popup,MdoBrowserDialogTarget dialog,CaptureEntry capture,MdoBrowserDocument document){this.key=key;this.view=view;this.popup=popup;this.dialog=dialog;this.capture=capture;this.document=document;previousDocument=popupControlDocument(view,document);}
        public boolean ready(){return prepared&&!running&&!failed&&(document==null||!document.initializing())&&(previousDocument==null||!previousDocument.initializing());}
        public boolean failed(){return failed;}
        public void destroy(){
            if(complete||running)return;if(!ready())throw new IllegalStateException("native_view_retirement_busy");running=true;
            try{
                if(previousDocument!=null)previousDocument.close();
                if(!destroyed){root.removeView(view);view.destroy();destroyed=true;}
                retireUploadData(view);
                if(popup!=null)popup.viewDestroyed(view);
                if(dialog!=null&&dialog.view==view)dialog.viewDestroyed();
                if(capture!=null&&capture.view==view)capture.capture.viewDestroyed();
                if(document!=null)document.viewDestroyed(view);
                if(previousDocument!=null)previousDocument.viewDestroyed(view);
                networkDestroyed(view);
                boolean documentClean=(document==null||document.cleanupConfirmed())&&(previousDocument==null||previousDocument.cleanupConfirmed());if(!documentClean)nativeViewCleanupFailed=true;
                clean=documentClean&&networkClean();complete=true;
            }catch(RuntimeException|Error failure){failed=true;nativeViewCleanupFailed=true;throw failure;}
            finally{
                running=false;nativeRetiringViews.remove(view);
                if(complete){
                    try{if(controlled)MdoBrowserCdpPopupNative.activateDestruction(view,this);}catch(java.io.IOException error){nativeViewCleanupFailed=true;}
                    if(!bulkNativeDestruction)nativeDestructions.remove(view,this);
                }
            }
        }
    }
    private NativeDestruction prepareNativeDestruction(String key,WebView view,MdoBrowserPopups<WebView> popup,MdoBrowserDialogTarget dialog,CaptureEntry capture,MdoBrowserDocument document){
        NativeDestruction old=nativeDestructions.get(view);if(old!=null)return old;
        NativeDestruction row=new NativeDestruction(key,view,popup,dialog,capture,document);nativeDestructions.put(view,row);nativeRetiringViews.add(view);
        // Worker polling can run between these UI statements. Record deliberate
        // protocol retirement before native proof revocation, without callbacks.
        retireProtocolPopupAuthority(view);
        row.controlled=MdoBrowserCdpPopupNative.registerDestruction(view,row);return row;
    }
    private boolean executeNativeDestruction(NativeDestruction row){
        if(row.complete)return row.clean;if(row.failed)return false;if(row.document!=null&&row.document.initializing())return false;
        try{if(row.controlled)MdoBrowserCdpPopupNative.activateDestruction(row.view,row);else row.destroy();}
        catch(java.io.IOException error){row.failed=true;nativeViewCleanupFailed=true;nativeRetiringViews.remove(row.view);throw new IllegalStateException("native_view_cleanup_failed",error);}
        return row.complete&&row.clean;
    }
    private NativeDestruction retireNativeDocument(String key,WebView view){
        NativeDestruction old=nativeDestructions.get(view);if(old!=null)return old;
        WebView current=views.get(key);if(current!=null&&current!=view)throw new IllegalStateException("native_view_binding_mismatch");
        if(views.nativeDestroyed(view))return null;
        NativeDestruction row=null;
        try{
            row=prepareNativeDestruction(key,view,windows.get(key),dialogs.get(key),captures.get(key),documents.get(key));
            long epoch=views.epoch(key,view);networkRetire(key,view,"network_view_closed");cancelUpload(key,view);
            if(views.owns(key,view))views.remove(key);cdpViews.invalidate(key,view,epoch,"input_closed");
            windows.remove(key);if(row.popup!=null)row.popup.close();popupLinks.remove(key);pageClosed.remove(key);navigationRequests.remove(key);
            dialogs.remove(key);if(row.dialog!=null)row.dialog.close();captures.remove(key);if(row.capture!=null)row.capture.capture.close();
            documents.remove(key);documentFailures.remove(key);if(row.document!=null)row.document.close();
            row.prepared=true;executeNativeDestruction(row);return row;
        }catch(RuntimeException|Error failure){nativeViewCleanupFailed=true;if(row!=null){row.failed=true;nativeRetiringViews.remove(view);}throw failure;}
    }
    private boolean closeAndDestroyDocument(String key,WebView view){
        NativeDestruction row=retireNativeDocument(key,view);return row==null?!nativeViewCleanupFailed&&networkClean():row.complete&&row.clean;
    }
    private void documentNavigation(String key,WebView view){
        if(!views.owns(key,view))return;
        retireProtocolPopupPreparation(key,view,"cdp_popup_preparation_owner_changed");
        nativeNavigationPageStarted(key,view);
        navigationRequests.remove(key);
        cancelPopup(key);
        cancelDialog(key);
        cancelCapture(key);
        MdoBrowserDocument document=documents.get(key);if(document!=null)document.navigation();
    }
    private void documentLoaded(String key,WebView view){
        if(!views.owns(key,view))return;
        MdoBrowserDocument document=documents.get(key);if(document!=null)document.loaded();
    }
    private void blockDocuments(int reason,boolean value){
        if(value)cancelDownloadReleases();
        if(value)networkRetireAll("network_host_admission_lost");
        if(value)cdpViews.invalidateAll();
        if(value){cancelUploads();cancelCaptures();}
        for(MdoBrowserDocument document:new ArrayList<>(documents.values()))document.block(reason,value);
    }
    private JSONObject documentState(String key) throws JSONException {
        JSONObject attached=protocolPopupDocumentState(key);
        if(attached!=null)return attached.put("native_view_cleanup_failed",nativeViewCleanupFailed||!MdoBrowserCdpPopupNative.creationConfirmed())
            .put("native_view_retiring",nativeRetiringViews.size()).put("network",networkState(key));
        MdoBrowserDocument document=documents.get(key);
        return new JSONObject().put("installed",document!=null&&document.worldInstalled()).put("ready",document!=null&&document.ready())
            .put("phase",document==null?documentFailures.getOrDefault(key,"world_unavailable"):document.phase())
            .put("pending",document==null?0:document.pending()).put("native_outstanding",document==null?0:document.outstanding())
            .put("native_budget_used",worldBudget.used()).put("native_budget_limit",worldBudget.limit())
            .put("native_view_cleanup_failed",nativeViewCleanupFailed||!MdoBrowserCdpPopupNative.creationConfirmed()).put("native_view_retiring",nativeRetiringViews.size())
            .put("native_document",document==null||document.identity()==null?JSONObject.NULL:document.identity())
            .put("adapter","android-main-document-v1")
            .put("profile_isolation",document!=null&&document.profileAssigned()).put("network",networkState(key));
    }
    private void documentFailure(MdoBrowserChannel<WebMessagePort>.Lease lease,String id,String identity,long epoch,String code,boolean sent){
        try{reply(lease,id,new JSONObject().put("ok",false).put("native_document",identity==null?JSONObject.NULL:identity).put("navigation",epoch)
            .put("error",new JSONObject().put("code",code).put("evaluation_may_have_occurred",sent)),null);}
        catch(JSONException error){reply(lease,id,null,"world_result_invalid");}
    }
    private void executeDocument(String key,String id,JSONObject request) throws Exception {
        requireLegacyDocumentRoute(key);
        MdoBrowserDocument document=documents.get(key);
        if(document==null)throw new UnsupportedOperationException(documentFailures.getOrDefault(key,"world_unavailable"));
        final MdoBrowserChannel<WebMessagePort>.Lease lease=channel.current();
        final String identity=document.identity();final long epoch=views.epoch(key,views.get(key));
        final boolean pinned=request.has("native_document")||request.has("navigation");
        String expected=null;
        if(pinned){
            expected=request.getString("native_document");
            if(!(request.get("navigation") instanceof Number)||request.getDouble("navigation")!=epoch||!expected.equals(identity)){
                documentFailure(lease,id,identity,epoch,"frame_changed",false);return;
            }
        }
        String expression="(()=>{try{return {ok:true,value:("+request.getString("script")+")}}catch(e){return {ok:false,error:String(e.message||e)}}})()";
        try{document.evaluate(expected,expression,Math.min(10000,Math.max(1,request.optLong("timeout_ms",10000))),result->{
            if(!result.ok){if(pinned)documentFailure(lease,id,identity,epoch,result.code,result.evaluationMayHaveOccurred);else reply(lease,id,null,result.code);return;}
            try{JSONObject value=new JSONObject(result.value);
                if(pinned&&!value.optBoolean("ok")){documentFailure(lease,id,identity,epoch,"document_script_failed",result.evaluationMayHaveOccurred);return;}
                value.put("native_document",identity).put("navigation",epoch);reply(lease,id,value,value.optBoolean("ok")?null:value.optString("error","World execution failed"));}
            catch(JSONException error){reply(lease,id,null,"world_result_invalid");}
        });}catch(MdoBrowserWorld.Failure failure){if(pinned)documentFailure(lease,id,identity,epoch,failure.code,false);else throw failure;}
    }
    // The UI keeps this bounded scope; worker callbacks retain it weakly. A new
    // request never replaces an unretired source under a reused browser key.
    private final class PopupPreparation {
        final String key;final WebView view;final long epoch;final NetworkEntry network;final PopupNetwork protectedOwner;
        MdoBrowserCdpPopupPreparation preparation;
        PopupPreparation(String key,WebView view,NetworkEntry network){this.key=key;this.view=view;this.network=network;protectedOwner=network.popup;epoch=views.epoch(key,view);}
        boolean current(){return protocolPopupPreparations.get(key)==this&&epoch>0&&views.owns(key,view)&&views.epoch(key,view)==epoch&&networkViews.get(key)==network&&
            network.ready&&!network.retired&&!network.draining&&networkCurrent(network)&&!manual.contains(key)&&!pageClosed.contains(key)&&
            (protectedOwner==null||network.popup==protectedOwner&&protectedOwner.current()&&protectedOwner.page!=null&&
                (protectedOwner.page.documentReady()||protectedOwner.page.continuingNativePreparation(this))&&protectedOwner.sourceProof!=null&&protectedOwner.sourceProof.current());}
        void lost(String reason){if(protocolPopupPreparations.get(key)!=this||!views.owns(key,view))return;
            views.input(key,view).invalidate("input_cancelled");cdpViews.invalidate(key,view,views.epoch(key,view),"input_cancelled");
            if(protectedOwner!=null)protocolPopupDocumentInvalidated(key,view,"popup_preparation_retired");}
    }
    private final Map<String,PopupPreparation> protocolPopupPreparations=new HashMap<>();
    interface ProtocolPopupReady {void ready(MdoBrowserCdpPopupNative.Promotion owner,MdoBrowserCdpPopupBroker.Handoff handoff)throws java.io.IOException;}
    interface ProtocolPopupNetworkReady {void ready(PopupNetwork network)throws java.io.IOException;}
    interface ProtocolPopupPageReady {void ready(PopupPage page)throws java.io.IOException;}
    private boolean protocolPopupCreatePending(String key){return protocolPopupHandoffs.containsKey(key);}
    /** Retain only immutable layout data. A popup URL must never be replayed. */
    private JSONObject protocolPopupLayout(String key,JSONObject request)throws JSONException {
        if(request.has("visible")&&!(request.opt("visible") instanceof Boolean)||request.has("bounds")&&!(request.opt("bounds") instanceof JSONObject))throw new IllegalArgumentException("popup_layout_invalid");
        JSONObject supplied=request.optJSONObject("bounds"),box=new JSONObject();
        String[] names={"x","y","width","height","scale"};double[] defaults={0,0,640,480,1};
        for(int i=0;i<names.length;i++){
            Object value=supplied==null?null:supplied.opt(names[i]);
            if(value!=null&&(!(value instanceof Number)||!Double.isFinite(((Number)value).doubleValue())))throw new IllegalArgumentException("popup_layout_invalid");
            box.put(names[i],value==null?defaults[i]:((Number)value).doubleValue());
        }
        return new JSONObject().put("browser",key).put("bounds",box).put("visible",request.optBoolean("visible",true));
    }
    /** The normal create command joins the exact pending native transport;
     * global automation admission still precedes this private product route. */
    private void executeProtocolPopupCreate(String key,String id,JSONObject request)throws java.io.IOException,JSONException {
        nativeOwnerAdmission();
        String pending=request.optString("popup"),opener=request.optString("opener");WebView exactOpener=views.get(opener);
        if(key.equals(opener)||pending.isEmpty()||opener.isEmpty()||exactOpener==null||views.get(key)!=null)throw new java.io.IOException("cdp_popup_handoff_arguments_invalid");
        final JSONObject layout=protocolPopupLayout(key,request);final MdoBrowserChannel<WebMessagePort>.Lease lease=channel.current();
        final java.util.concurrent.atomic.AtomicBoolean replied=new java.util.concurrent.atomic.AtomicBoolean();
        handoffPreparedProtocolPopupPage(opener,exactOpener,pending,key,9000,page->{try{
            page.remaining();
            PopupNetwork network=page.network;WebView exact=network.entry.view;
            if(!page.current()||!page.downloadOnly&&!page.documentReady()||!network.entry.key.equals(key)||!views.owns(key,exact)||channel.current()!=lease||!channel.active(lease,host.getUrl())||popupLinks.containsKey(key))throw new java.io.IOException("cdp_popup_publication_owner_changed");
            JSONObject result=new JSONObject().put("ok",true).put("created",true).put("popup_attached",true).put("downloads_intercepted",true).put("initial_navigation_pending",false);
            PopupLink link=new PopupLink(exact,opener,pending);popupLinks.put(key,link);
            crashed.remove(key);errors.remove(key);errorCodes.remove(key);pageClosed.remove(key);
            String address=exact.getUrl();locations.put(key,address==null?"about:blank":address);
            root.addView(exact);
            page.remaining();
            if(!page.current()||!views.owns(key,exact)||popupLinks.get(key)!=link||channel.current()!=lease||!channel.active(lease,host.getUrl()))throw new java.io.IOException("cdp_popup_publication_owner_changed");
            bounds(exact,layout);
            page.remaining();
            if(!page.current()||layout.optBoolean("visible",true)&&!page.downloadOnly&&!page.documentReady()||popupLinks.get(key)!=link||channel.current()!=lease||!channel.active(lease,host.getUrl()))throw new java.io.IOException("cdp_popup_publication_owner_changed");
            link.ready=true;if(replied.compareAndSet(false,true))reply(lease,id,result,null);
        }catch(JSONException error){throw new java.io.IOException("cdp_popup_publication_result_invalid");}},(code,effect)->{
            if(!replied.compareAndSet(false,true))return;
            try{reply(lease,id,new JSONObject().put("ok",false).put("created",false).put("popup_attached",false)
                .put("error",new JSONObject().put("code",code!=null&&code.matches("cdp_[a-z_]{1,80}")?code:"cdp_popup_create_failed"))
                .put("native_create",new JSONObject().put("created_may_have_occurred",effect).put("navigation_may_have_occurred",effect)
                    .put("network_cleanup_confirmed",false).put("view_cleanup_confirmed",!effect&&!nativeViewCleanupFailed&&MdoBrowserCdpPopupNative.creationConfirmed()&&views.get(key)==null)),null);}
            catch(JSONException error){reply(lease,id,null,"popup_result_invalid");}
        });
    }
    /** Fixed worker-only world warmup. No UI owner, caller script or target ID. */
    private static final class PopupPageWarmup implements MdoBrowserCdpWorker.Operation {
        static byte inventoryState(JSONObject inventory)throws java.io.IOException {
            org.json.JSONArray frames=inventory.optJSONArray("frames");JSONObject root=null;
            if(!Boolean.TRUE.equals(inventory.opt("ok"))||frames==null||frames.length()>32)throw new java.io.IOException("cdp_popup_page_inventory_invalid");
            for(int i=0;i<frames.length();i++){
                JSONObject frame=frames.optJSONObject(i);if(frame==null)throw new java.io.IOException("cdp_popup_page_inventory_invalid");
                if("f0".equals(frame.opt("id"))){if(root!=null)throw new java.io.IOException("cdp_popup_page_inventory_invalid");root=frame;}
            }
            if(root==null||!(root.opt("loading") instanceof Boolean)||!(root.opt("isolated") instanceof Boolean))throw new java.io.IOException("cdp_popup_page_inventory_invalid");
            if(root.optBoolean("loading"))return 0;
            if(!root.optBoolean("isolated"))throw new java.io.IOException("cdp_popup_page_world_unavailable");
            return 1;
        }
        public byte[] run(MdoBrowserCdpBinding binding,MdoBrowserCdpWorker.Deadline budget)throws java.io.IOException {
            return new byte[]{inventoryState(binding.frames().inventory(budget))};
        }
    }
    private static final class PopupPageForward implements MdoBrowserCdpWorker.Completion {
        private final java.lang.ref.WeakReference<PopupPage> owner;
        PopupPageForward(PopupPage owner){this.owner=new java.lang.ref.WeakReference<>(owner);}
        public void done(MdoBrowserCdpWorker.Result result){PopupPage value=owner.get();if(value!=null)value.completed(result);}
    }
    /** One UI-owned startup on the original guarded network. Download-only
     * popups do not need a document world. A ready world certifies only its
     * captured native document epoch, never a later navigation. */
    private static final class PopupPageNavigationForward implements MdoBrowserCdpWorker.Completion {
        private final java.lang.ref.WeakReference<PopupPage> owner;private final java.lang.ref.WeakReference<NativeNavigation> navigation;private final java.lang.ref.WeakReference<MdoBrowserCdpMarker> marker;
        PopupPageNavigationForward(PopupPage owner,NativeNavigation navigation,MdoBrowserCdpMarker marker){this.owner=new java.lang.ref.WeakReference<>(owner);this.navigation=new java.lang.ref.WeakReference<>(navigation);this.marker=new java.lang.ref.WeakReference<>(marker);}
        public void done(MdoBrowserCdpWorker.Result result){PopupPage value=owner.get();NativeNavigation original=navigation.get();MdoBrowserCdpMarker document=marker.get();if(value!=null&&original!=null&&document!=null)value.navigationCompleted(original,document,result);}
    }
    private final class PopupPage implements AutoCloseable {
        final PopupNetwork network;final Runnable watch=this::poll;
        ProtocolPopupPageReady ready;MdoBrowserCdpPopupPreparation.Failure failure;
        MdoBrowserCdpMarker marker;MdoBrowserCdpPopupNative.Document proof;
        int phase;long epoch;boolean inFlight,downloadOnly,worldReady,suspended,restoring;NativeNavigation navigating;
        boolean naturalReadRecovery;MdoBrowserCdpWorker.DocumentRead restoringRead;
        // An original onCreateWindow callback revokes document execution, but
        // its exact prepared native transport must still be consumable once.
        // UI-only receipt: it never keeps a DOM marker or input reference alive.
        PopupPreparation pendingPreparation;
        PopupPage(PopupNetwork network,ProtocolPopupPageReady ready,MdoBrowserCdpPopupPreparation.Failure failure){this.network=network;this.ready=ready;this.failure=failure;}
        boolean current(){return phase<3&&network.page==this&&network.current();}
        boolean documentCurrent(){return marker!=null&&marker.lease().current()&&views.owns(network.entry.key,network.entry.view)&&views.epoch(network.entry.key,network.entry.view)==epoch;}
        boolean documentReady(){return phase==2&&!downloadOnly&&worldReady&&!restoring&&current()&&documentCurrent();}
        // The UI bootstrap phase can outlive a native realm. Public readiness
        // also needs this original document's current worker-published fence.
        boolean documentReadable(){return documentReady()&&proof!=null&&network.handoff.owns(proof)&&proof.current()&&!cdpWorker.documentUnavailable(marker,proof);}
        boolean continuingNativePreparation(PopupPreparation original){return original!=null&&pendingPreparation==original&&protocolPopupPreparations.get(network.entry.key)==original&&original.protectedOwner==network&&original.epoch==epoch&&phase==2&&!downloadOnly&&current()&&views.epoch(network.entry.key,network.entry.view)==epoch;}
        void invalidated(String reason){
            if(phase!=2||downloadOnly)return;
            PopupPreparation original=protocolPopupPreparations.get(network.entry.key);
            boolean ownPopup="popup_pending".equals(reason)&&original!=null&&original.protectedOwner==network&&original.epoch==epoch&&
                (worldReady||continuingNativePreparation(original))&&current()&&views.epoch(network.entry.key,network.entry.view)==epoch&&original.preparation.originalNativePending()!=null;
            pendingPreparation=ownPopup?original:null;
            worldReady=false;restoring=false;restoringRead=null;
            // Remember only retry intent when preparation ends under takeover
            // or another admission loss. A later explicit inventory must still
            // prove this original physical Source and its current document.
            if("popup_preparation_retired".equals(reason)&&!current()&&views.owns(network.entry.key,network.entry.view)&&views.epoch(network.entry.key,network.entry.view)==epoch)naturalReadRecovery=true;
            // UI-only loss can rearm this exact epoch. A different document
            // additionally needs explicit inventory and an original-source guard;
            // pinned actions never create replacement authority here.
            suspended=(ownPopup||"input_view_unavailable".equals(reason)||"input_viewport_changed".equals(reason)||"popup_preparation_retired".equals(reason))&&current()&&
                views.owns(network.entry.key,network.entry.view)&&views.epoch(network.entry.key,network.entry.view)==epoch;
            NativeNavigation originalNavigation=navigating;
            if("frame_changed".equals(reason)&&originalNavigation!=null&&nativeNavigationScope(originalNavigation)&&!originalNavigation.revoked&&originalNavigation.pageError==null&&originalNavigation.ticket!=null&&originalNavigation.ticket.pageCurrent()){
                originalNavigation.settled=false;
                if(!networkHandler.postDelayed(originalNavigation.watch,20)){originalNavigation.pageError="cdp_navigation_scheduler_failed";nativeNavigationRevoke(originalNavigation);}
            }
        }
        /** A different UI epoch requires this original source's native commit;
         * document/loader/destination proof runs before world inventory. */
        boolean joinNavigation(NativeNavigation operation){
            if(!current()||operation.owner!=network.entry||!nativeNavigationScope(operation)||operation.revoked||operation.ticket==null||!operation.ticket.pageCurrent()){
                operation.pageError="cdp_navigation_document_changed";return false;
            }
            if(navigating==operation&&documentReady())return true;
            String key=network.entry.key;WebView view=network.entry.view;long observed=views.epoch(key,view);
            if(!cdpEnvironmentCurrent(key,view,observed,network.entry.channelLease)||!chooserReady(dialogs.get(key))||views.inputBusy(key,view)||!views.inputClean(key,view))return false;
            if(inFlight||cdpWorker.pending(marker))return false;
            // Old preparation may still deliver its original invalidation.
            // Preserve this navigation's deadline; never warm a replacement
            // marker while that previous action can retire it.
            PopupPreparation previous=protocolPopupPreparations.get(key);
            if(previous!=null&&!previous.preparation.settled())return false;
            try{
                long used=android.os.SystemClock.elapsedRealtime()-operation.born;if(used<0||used>operation.timeout-100)throw new java.io.IOException("cdp_navigation_document_timeout");
                if(navigating!=operation||!documentCurrent()||!restoring){
                    epoch=observed;marker=protocolDocumentMarker(key,network.owner,network.handoff);proof=network.owner.document(marker);
                    navigating=operation;worldReady=false;restoring=true;suspended=false;pendingPreparation=null;downloadOnly=false;restoringRead=null;naturalReadRecovery=false;
                }
                inFlight=true;
                cdpWorker.submitDocumentGuarded(marker,network.handoff,proof,Math.min(3000,(int)(operation.timeout-used)),false,operation.ticket,new PopupPageWarmup(),new PopupPageNavigationForward(this,operation,marker));
            }catch(java.io.IOException|RuntimeException|Error failure){inFlight=false;operation.pageError=reason(failure);restoreFailed(marker);}
            return false;
        }
        void navigationCompleted(NativeNavigation operation,MdoBrowserCdpMarker original,MdoBrowserCdpWorker.Result result){
            if(navigating!=operation||marker!=original)return;inFlight=false;
            if(!nativeNavigationScope(operation)||operation.revoked||!current())return;
            if(!documentCurrent()){
                worldReady=false;restoring=false;
                if(views.epoch(network.entry.key,network.entry.view)==epoch)operation.pageError="cdp_navigation_document_changed";return;
            }
            if(result.code!=null||operation.ticket==null||!operation.ticket.pageCurrent()||!cdpEnvironmentCurrent(network.entry.key,network.entry.view,epoch,network.entry.channelLease)){
                operation.pageError=result.code==null?"cdp_navigation_document_changed":result.code;restoreFailed(original);return;
            }
            if(result.bytes==null||result.bytes.length!=1||(result.bytes[0]!=0&&result.bytes[0]!=1)){operation.pageError="cdp_popup_page_result_invalid";restoreFailed(original);return;}
            if(result.bytes[0]==1){worldReady=true;restoring=false;pendingPreparation=null;}
        }
        void navigationRejected(NativeNavigation operation){
            if(current()&&operation.owner==network.entry&&network.entry.navigation==operation&&!downloadOnly&&views.epoch(network.entry.key,network.entry.view)==operation.reservation.epoch){
                navigating=null;worldReady=false;restoring=false;suspended=true;pendingPreparation=null;
            }
        }
        void restoreFailed(MdoBrowserCdpMarker original){
            if(marker!=original||!restoring)return;
            worldReady=false;restoring=false;suspended=false;pendingPreparation=null;restoringRead=null;
            cdpViews.invalidate(network.entry.key,network.entry.view,epoch,"popup_restore_failed");
        }
        void restored(MdoBrowserCdpMarker original,JSONObject inventory)throws java.io.IOException {
            if(marker!=original||!restoring||!current()||!documentCurrent())throw new java.io.IOException("cdp_worker_cancelled");
            if(restoringRead!=null&&!restoringRead.current())throw new java.io.IOException("cdp_navigation_document_changed");
            if(PopupPageWarmup.inventoryState(inventory)==1){worldReady=true;restoring=false;pendingPreparation=null;restoringRead=null;naturalReadRecovery=false;}
            // Loading inventory is useful, but does not certify a world. A later
            // explicit read may continue the same marker; never poll or replay.
        }
        int remaining()throws java.io.IOException {
            if(!current())throw new java.io.IOException("cdp_popup_page_owner_changed");
            long left=network.deadline-android.os.SystemClock.elapsedRealtime();if(left<100)throw new java.io.IOException("cdp_popup_page_timeout");return (int)Math.min(3000,left);
        }
        void schedule(){networkHandler.removeCallbacks(watch);if(!networkHandler.postDelayed(watch,50))throw new IllegalStateException("cdp_popup_page_scheduler_failed");}
        void start(){
            try{
                remaining();if(!loading.containsKey(network.entry.key))loading.put(network.entry.key,true);
                activateProtocolPopup(network.owner,network.handoff);schedule();
            }
            catch(java.io.IOException|RuntimeException|Error error){fail(reason(error));}
        }
        private String reason(Throwable error){String code=error.getMessage();return code!=null&&code.matches("(?:cdp|network)_[a-z_]{1,80}")?code:"cdp_popup_page_failed";}
        void poll(){
            if(phase>=2)return;
            try{
                MdoBrowserCdpSession.ControlResult flow=network.handoff.flowFailure();
                if(flow!=null&&!flow.ok)throw new java.io.IOException(flow.code==null?"cdp_popup_page_activation_failed":flow.code);
                int timeout=remaining();
                MdoBrowserCdpSession.ControlResult active=network.handoff.activationResult();
                if(active==null){schedule();return;}
                if(!active.ok)throw new java.io.IOException(active.code==null?"cdp_popup_page_activation_failed":active.code);
                if(phase==0)phase=1;
                // An offer from this exact physical root is useful without DOM.
                // Never replace the offer, consume its bytes or decide for user.
                for(MdoBrowserCdpDownloads.Snapshot item:network.entry.downloads.list())if(item.root&&item.frameCurrent){
                    if(!inFlight&&marker==null){downloadOnly=true;accepted();return;}
                }
                if(inFlight||Boolean.TRUE.equals(loading.get(network.entry.key))){schedule();return;}
                if(marker==null){
                    if(nativeViewCleanupFailed||networkCleanupFailed)nativeOwnerAdmission();
                    // Another original view may still be retiring on this
                    // shared wire. Keep this ready owner's original deadline
                    // instead of failing its document bootstrap on a busy gate.
                    if(!nativeRetiringViews.isEmpty()||networkRetiring()!=0||downloadRetiring()!=0){schedule();return;}
                }
                if(marker==null){epoch=views.epoch(network.entry.key,network.entry.view);marker=protocolDocumentMarker(network.entry.key,network.owner,network.handoff);proof=network.owner.document(marker);}
                if(!documentCurrent())throw new java.io.IOException("cdp_popup_page_document_changed");
                inFlight=true;schedule();
                cdpWorker.submitDocument(marker,network.handoff,proof,Math.min(timeout,remaining()),false,new PopupPageWarmup(),new PopupPageForward(this));
            }catch(java.io.IOException|RuntimeException|Error error){fail(reason(error));}
        }
        void completed(MdoBrowserCdpWorker.Result result){
            if(phase>=2)return;inFlight=false;
            if(result.code!=null){fail(result.code);return;}
            try{
                remaining();if(!documentCurrent())throw new java.io.IOException("cdp_popup_page_document_changed");
                if(result.bytes==null||result.bytes.length!=1||(result.bytes[0]!=0&&result.bytes[0]!=1))throw new java.io.IOException("cdp_popup_page_result_invalid");
                if(result.bytes[0]==0){schedule();return;}accepted();
            }catch(java.io.IOException|RuntimeException|Error error){fail(reason(error));}
        }
        private void accepted()throws java.io.IOException {
            remaining();if(!downloadOnly&&!documentCurrent())throw new java.io.IOException("cdp_popup_page_document_changed");
            phase=2;worldReady=!downloadOnly;networkHandler.removeCallbacks(watch);ProtocolPopupPageReady callback=ready;ready=null;
            if(callback==null){fail("cdp_popup_page_callback_failed");return;}
            try{callback.ready(this);}catch(java.io.IOException|RuntimeException|Error error){fail("cdp_popup_page_callback_failed");return;}
            failure=null;
        }
        void retired(String code){
            if(phase==3)return;phase=3;inFlight=false;worldReady=false;restoring=false;suspended=false;pendingPreparation=null;navigating=null;restoringRead=null;naturalReadRecovery=false;networkHandler.removeCallbacks(watch);ready=null;
            if(marker!=null){
                try{marker.close();}catch(RuntimeException|Error error){networkCleanupFailed=true;code="cdp_popup_page_cleanup_unconfirmed";}
                try{cdpWorker.cancel(marker.lease());}catch(RuntimeException|Error error){networkCleanupFailed=true;code="cdp_popup_page_cleanup_unconfirmed";}
            }proof=null;
            MdoBrowserCdpPopupPreparation.Failure callback=failure;failure=null;
            if(callback!=null)try{callback.failed(code,true);}catch(RuntimeException|Error ignored){/* one startup outcome */}
        }
        void fail(String code){if(phase==3)return;network.retire(code);}
        @Override public void close(){fail("cdp_popup_page_cancelled");}
    }
    private PopupPage prepareProtocolPopupPage(PopupNetwork network,ProtocolPopupPageReady ready,MdoBrowserCdpPopupPreparation.Failure failure)throws java.io.IOException {
        nativeOwnerAdmission();if(network==null||ready==null||failure==null||network.phase!=1||!network.current())throw new java.io.IOException("cdp_popup_page_arguments_invalid");
        if(network.page!=null)throw new java.io.IOException("cdp_popup_page_busy");
        PopupPage page=new PopupPage(network,ready,failure);network.page=page;page.start();return page;
    }
    private void protocolPopupDocumentInvalidated(String key,WebView view,String reason){
        NetworkEntry entry=networkViews.get(key);if(entry!=null&&entry.view==view&&entry.popup!=null&&entry.popup.page!=null)entry.popup.page.invalidated(reason);
    }
    /** Dispatcher family only, never readiness or public automation admission. */
    private boolean protocolPopupFrameRouting(String key){NetworkEntry entry=networkViews.get(key);return entry!=null&&entry.popup!=null||protocolPopupHandoffs.containsKey(key);}
    private void requireLegacyDocumentRoute(String key)throws java.io.IOException {
        if(protocolPopupFrameRouting(key))throw new java.io.IOException("cdp_popup_document_route_required");
    }
    /** UI snapshot only: never prepare, restore, submit a job or create a realm.
     * Native popup frames have no legacy single-world document identity. */
    private JSONObject protocolPopupDocumentState(String key)throws JSONException {
        NetworkEntry entry=networkViews.get(key);
        if(entry==null||entry.popup==null){
            PopupHandoff pending=protocolPopupHandoffs.get(key);if(pending==null)return null;
            return new JSONObject().put("adapter","android-native-frames-v1").put("installed",false).put("ready",false).put("phase","starting")
                .put("pending",0).put("pending_scope","document_worker").put("native_document",JSONObject.NULL)
                .put("profile_isolation",pending.profile!=null&&pending.profile.profileAssigned());
        }
        PopupNetwork owner=entry.popup;PopupPage page=owner.page;
        boolean installed=page!=null&&page.documentReadable();
        boolean pending=page!=null&&cdpWorker.pending(page.marker);
        MdoBrowserPopups<WebView> core=windows.get(key);boolean popupPending=core!=null&&core.busy();
        boolean ready=installed&&!pending&&cdpEnvironmentCurrent(key,entry.view,page.epoch,entry.channelLease)&&chooserReady(dialogs.get(key))&&
            !views.inputBusy(key,entry.view)&&views.inputClean(key,entry.view)&&!nativeViewCleanupFailed&&!networkCleanupFailed&&
            MdoBrowserCdpPopupNative.creationConfirmed()&&nativeRetiringViews.isEmpty()&&networkRetiring()==0&&downloadRetiring()==0;
        String phase=!owner.current()?"owner_unavailable":page==null||page.phase<2?"starting":page.downloadOnly?"download_only":
            views.epoch(key,entry.view)!=page.epoch?"document_changed":popupPending?"popup_pending":page.restoring?"restoring":pending?"busy":ready?"ready":page.suspended?"suspended":"document_unavailable";
        return new JSONObject().put("adapter","android-native-frames-v1").put("installed",installed).put("ready",ready).put("phase",phase)
            .put("pending",pending?1:0).put("pending_scope","document_worker").put("native_document",JSONObject.NULL)
            .put("profile_isolation",entry.profile.profileAssigned());
    }
    private void retireProtocolPopupAuthority(WebView view){
        for(NetworkEntry entry:networkViews.values())if(entry.view==view&&entry.popup!=null)entry.popup.handoff.retireAuthority();
    }
    /** Select only the original prepared document. Fresh explicit inventory may
     * rearm a UI-suspended epoch; pinned operations never create a new realm. */
    private PopupPage protocolPopupDocument(String key,WebView view)throws java.io.IOException {
        return protocolPopupDocument(key,view,false);
    }
    private PopupPage protocolPopupDocument(String key,WebView view,boolean inventory)throws java.io.IOException {
        NetworkEntry entry=networkViews.get(key);if(entry==null||entry.popup==null)return null;
        PopupNetwork network=entry.popup;PopupPage page=network.page;
        if(entry.view!=view||network.entry!=entry||page==null||page.network!=network||page.phase!=2||page.downloadOnly||!page.current())throw new java.io.IOException("cdp_popup_page_document_unavailable");
        long observed=views.epoch(key,view);
        boolean lost=inventory&&page.worldReady&&cdpWorker.documentUnavailable(page.marker,page.proof);
        if(observed!=page.epoch||page.naturalReadRecovery&&!page.restoring||lost){
            if(!inventory||!cdpEnvironmentCurrent(key,view,observed,entry.channelLease)||!chooserReady(dialogs.get(key))||views.inputBusy(key,view)||!views.inputClean(key,view))throw new java.io.IOException("cdp_popup_page_document_unavailable");
            if(page.inFlight||cdpWorker.pending(page.marker))throw new java.io.IOException("cdp_popup_page_restore_busy");
            PopupPreparation pending=protocolPopupPreparations.get(key);
            PopupPreparation continuation=lost&&pending!=null&&pending.current()&&pending.protectedOwner==network&&pending.epoch==observed&&pending.preparation.nativeTurnSettled()?pending:null;
            if(pending!=null&&!pending.preparation.settled()&&continuation==null)throw new java.io.IOException("cdp_popup_page_restore_busy");
            nativeOwnerAdmission();MdoBrowserCdpWorker.DocumentRead guard=cdpWorker.currentPopupDocument(entry.marker,network.handoff,network.sourceProof);
            if(lost)cdpViews.invalidate(key,view,observed,"popup_document_context_lost");
            page.epoch=observed;page.worldReady=false;page.suspended=false;page.restoring=true;page.naturalReadRecovery=true;page.restoringRead=guard;page.navigating=null;page.pendingPreparation=continuation;
            try{page.marker=protocolDocumentMarker(key,network.owner,network.handoff);page.proof=network.owner.document(page.marker);}
            catch(java.io.IOException|RuntimeException|Error error){page.restoreFailed(page.marker);throw error;}
            return page;
        }
        if(page.documentReady()||inventory&&page.restoring){
            if(!page.documentCurrent()||page.proof==null||!network.handoff.owns(page.proof)||!page.proof.current())throw new java.io.IOException("cdp_popup_page_document_unavailable");
            return page;
        }
        if(!inventory||!page.suspended||!cdpEnvironmentCurrent(key,view,page.epoch,entry.channelLease)||!chooserReady(dialogs.get(key))||
            views.inputBusy(key,view)||!views.inputClean(key,view))throw new java.io.IOException("cdp_popup_page_document_unavailable");
        if(cdpWorker.pending(page.marker))throw new java.io.IOException("cdp_popup_page_restore_busy");
        PopupPreparation preparing=protocolPopupPreparations.get(key);
        if(preparing!=null&&!preparing.preparation.settled()&&!(page.continuingNativePreparation(preparing)&&preparing.preparation.nativeTurnSettled()))throw new java.io.IOException("cdp_popup_page_restore_busy");
        nativeOwnerAdmission();page.suspended=false;page.restoring=true;page.worldReady=false;
        try{
            page.marker=protocolDocumentMarker(key,network.owner,network.handoff);page.proof=network.owner.document(page.marker);
        }catch(java.io.IOException|RuntimeException|Error error){page.restoreFailed(page.marker);throw error;}
        return page;
    }
    private static final class PopupNetworkAttach implements MdoBrowserCdpWorker.Operation {
        private final MdoBrowserCdpDownloads.Connection original;
        PopupNetworkAttach(MdoBrowserCdpDownloads.Connection original){this.original=original;}
        public byte[] run(MdoBrowserCdpBinding binding,MdoBrowserCdpWorker.Deadline budget)throws java.io.IOException {binding.attachDownloads(original);return new byte[]{1};}
    }
    private static final class PopupNetworkForward implements MdoBrowserCdpWorker.Completion {
        private final java.lang.ref.WeakReference<PopupNetwork> owner;
        PopupNetworkForward(PopupNetwork owner){this.owner=new java.lang.ref.WeakReference<>(owner);}
        public void done(MdoBrowserCdpWorker.Result result){PopupNetwork value=owner.get();if(value!=null)value.completed(result);}
    }
    /** UI-owned association with the real physical network registry. The worker
     * retains only a weak completion and the original data-only download link.
     * Source bootstrap never reloads/resumes the protected native popup. */
    private final class PopupNetwork implements AutoCloseable {
        final NetworkEntry entry;final MdoBrowserCdpPopupNative.Promotion owner;final MdoBrowserCdpPopupBroker.Handoff handoff;
        MdoBrowserCdpPopupNative.Source sourceProof;
        final long deadline;ProtocolPopupNetworkReady ready;MdoBrowserCdpPopupPreparation.Failure failure;
        int phase;boolean disposing,downloadsClosed,human;PopupPage page;MdoBrowserCdpPopupNative.HumanControl humanControl;
        PopupNetwork(NetworkEntry entry,MdoBrowserCdpPopupNative.Promotion owner,MdoBrowserCdpPopupBroker.Handoff handoff,long deadline,
                ProtocolPopupNetworkReady ready,MdoBrowserCdpPopupPreparation.Failure failure){
            this.entry=entry;this.owner=owner;this.handoff=handoff;this.deadline=deadline;this.ready=ready;this.failure=failure;
        }
        boolean current(){return phase<2&&networkCurrent(entry)&&entry.live.get()&&owner.current()&&handoff.current()&&handoff.owns(owner);}
        int remaining()throws java.io.IOException {
            if(!current())throw new java.io.IOException("cdp_popup_network_owner_changed");
            long left=deadline-android.os.SystemClock.elapsedRealtime();if(left<100)throw new java.io.IOException("cdp_popup_network_timeout");return (int)Math.min(3000,left);
        }
        private String reason(Throwable error){String code=error.getMessage();return code!=null&&code.matches("(?:cdp|network)_[a-z_]{1,80}")?code:"cdp_popup_network_failed";}
        void start(){
            try{
                int timeout=remaining();WebView exact=entry.view;
                entry.adapter=MdoBrowserCdpView.network(exact,v->v==exact&&current(),v->v==exact&&current()&&phase==1&&entry.ready,entry.quota);
                entry.marker=entry.adapter.marker();sourceProof=owner.source(entry.marker);
                entry.downloads=newNetworkDownloads();entry.downloadConnection=entry.downloads.connection();
                final MdoBrowserCdpDownloads.Connection original=entry.downloadConnection;
                schedule();
                cdpWorker.submitSource(entry.marker,handoff,sourceProof,Math.min(timeout,remaining()),new PopupNetworkAttach(original),new PopupNetworkForward(this));
            }catch(java.io.IOException|RuntimeException|Error error){retire(reason(error));}
        }
        void completed(MdoBrowserCdpWorker.Result result){
            if(phase!=0)return;
            if(result.code!=null){retire(result.code);return;}
            try{
                remaining();if(result.bytes==null||result.bytes.length!=1||result.bytes[0]!=1)throw new java.io.IOException("cdp_popup_network_result_invalid");
                phase=1;entry.starting=false;entry.ready=true;
                ProtocolPopupNetworkReady callback=ready;ready=null;
                if(callback==null)throw new java.io.IOException("cdp_popup_network_callback_failed");
                try{callback.ready(this);}catch(java.io.IOException|RuntimeException|Error error){retire("cdp_popup_network_callback_failed");return;}
                failure=null; // an accepted page must not retain its opener transaction
            }catch(java.io.IOException|RuntimeException|Error error){retire(reason(error));}
        }
        void schedule(){networkHandler.removeCallbacks(entry.watch);if(!networkHandler.postDelayed(entry.watch,100))throw new IllegalStateException("network_scheduler_unavailable");}
        /** Begin on this original UI owner before cancelling input/capture.
         * The reserved native receipt supplies its own deadline and cannot
         * grant manual control while any original cleanup remains pending. */
        MdoBrowserCdpPopupNative.HumanControl beginControl()throws java.io.IOException {
            if(phase!=1||page==null||page.phase!=2||!current())throw new java.io.IOException("cdp_popup_human_owner_invalid");
            MdoBrowserCdpPopupNative.HumanControl control=handoff.prepareControl(owner,entry.view);
            // Quiesce before revoking the source, preserving user cancellation
            // and original completed bytes rather than connection-loss text.
            boolean quiesced=false;
            try{
                entry.downloads.quiesce();quiesced=true;control.start();humanControl=control;human=true;
                nativeNavigationRetire(entry);cancelNavigationRequest(entry.key,entry.view);
                retire("network_user_takeover");return control;
            }catch(java.io.IOException|RuntimeException|Error failure){control.close();if(quiesced)retire("network_human_prepare_failed");throw failure;}
        }
        private boolean controlScope(MdoBrowserCdpPopupNative.HumanControl original){
            return original!=null&&humanControl==original&&phase==2&&human&&!entry.destroyed&&networkViews.get(entry.key)==entry&&
                !stopped&&!browserPaused&&views.owns(entry.key,entry.view)&&documents.get(entry.key)==entry.profile&&entry.profile.profileAssigned()&&
                channel.current()==entry.channelLease&&channel.active(entry.channelLease,host.getUrl())&&!pageClosed.contains(entry.key);
        }
        /** Cancel only the resources captured before this original attempt.
         * Each callback may replace a binding, close the view or lose its
         * channel; never continue cancelling resources under a reused key. */
        MdoBrowserCdpPopupNative.HumanControl beginControlAndCancel()throws java.io.IOException {
            CaptureEntry capture=captures.get(entry.key);MdoBrowserDialogTarget dialog=dialogs.get(entry.key);
            MdoBrowserPopups<WebView> popup=windows.get(entry.key);UploadEntry upload=uploadOwners.get(entry.key);
            if(capture!=null&&capture.view!=entry.view||dialog!=null&&dialog.view!=entry.view||upload!=null&&upload.view!=entry.view)
                throw new java.io.IOException("cdp_popup_human_owner_invalid");
            MdoBrowserCdpPopupNative.HumanControl original=beginControl();
            try{
                requireControlResources(original,capture,dialog,popup,upload);
                cdpViews.invalidate(entry.key,entry.view,views.epoch(entry.key,entry.view),"input_cancelled");
                requireControlResources(original,capture,dialog,popup,upload);views.stop(entry.key,entry.view);
                requireControlResources(original,capture,dialog,popup,upload);cancelCapture(entry.key);
                requireControlResources(original,capture,dialog,popup,upload);cancelUpload(entry.key,entry.view);
                requireControlResources(original,capture,dialog,popup,null);cancelChooser(entry.key);
                requireControlResources(original,capture,dialog,popup,null);cancelPopup(entry.key);
                requireControlResources(original,capture,dialog,popup,null);return original;
            }catch(java.io.IOException failure){original.close();throw failure;}
            catch(RuntimeException|Error failure){original.close();throw new java.io.IOException("cdp_popup_human_resource_cancel_failed");}
        }
        private void requireControlResources(MdoBrowserCdpPopupNative.HumanControl original,CaptureEntry capture,MdoBrowserDialogTarget dialog,
                MdoBrowserPopups<WebView> popup,UploadEntry upload)throws java.io.IOException {
            original.expire();if(original.failure()!=null)throw new java.io.IOException(original.failure());
            if(controlScope(original)&&original.current()&&captures.get(entry.key)==capture&&dialogs.get(entry.key)==dialog&&
                windows.get(entry.key)==popup&&uploadOwners.get(entry.key)==upload)return;
            original.close();throw new java.io.IOException("cdp_popup_human_owner_invalid");
        }
        boolean controlReady(MdoBrowserCdpPopupNative.HumanControl original){
            MdoBrowserPopups<WebView> popup=windows.get(entry.key);MdoBrowserDialogTarget dialog=dialogs.get(entry.key);
            return controlScope(original)&&
                (page==null||!cdpWorker.pending(page.marker))&&(entry.marker==null||!cdpWorker.pending(entry.marker))&&
                views.inputClean(entry.key,entry.view)&&captureMutationReady(entry.key,entry.view)&&!uploadBusy(entry.key,entry.view)&&
                (dialog==null||dialog.view==entry.view)&&chooserClean(dialog)&&(popup==null||popup.clean()&&popup.snapshot()==null)&&original.ready();
        }
        void completeControl(MdoBrowserCdpPopupNative.HumanControl original)throws java.io.IOException {
            if(original==null||humanControl!=original)throw new java.io.IOException("cdp_popup_human_owner_invalid");
            original.expire();if(original.failure()!=null)throw new java.io.IOException(original.failure());
            if(!controlScope(original)){original.close();humanControl=null;throw new java.io.IOException("cdp_popup_human_owner_invalid");}
            if(!controlReady(original))throw new java.io.IOException("cdp_popup_human_cleanup_pending");
            commitControl(original);
        }
        private void commitControl(MdoBrowserCdpPopupNative.HumanControl original)throws java.io.IOException {
            if(original==null||humanControl!=original)throw new java.io.IOException("cdp_popup_human_owner_invalid");
            original.commit();humanControl=null;handoff.detachForHuman(owner);watch();
        }
        /** Synchronous internal callers require already clean native input.
         * The public coordinator must use begin/wait/complete instead. */
        void takeControl()throws java.io.IOException {
            try{if(owner.view()!=entry.view)throw new IllegalStateException("popup_child_owner_lost");}
            catch(RuntimeException error){throw new java.io.IOException("cdp_popup_human_owner_invalid");}
            MdoBrowserCdpPopupNative.HumanControl control=beginControl();
            try{commitControl(control);}finally{control.close();}
        }
        /** Historical retirement permits only a fresh same-view network
         * proof. It never revives this popup's document or native authority. */
        boolean returnable(){return human&&phase==3&&entry.retired&&entry.protocolDrained&&!entry.destroyed&&owner.humanTransferred()&&
            owner.ended()&&!owner.failed()&&!owner.nativeDestroyed()&&handoff.humanDetached()&&views.inputClean(entry.key,entry.view);}
        void retire(String code){
            if(phase>=2)return;
            boolean starting=phase==0||page!=null&&page.phase<2;
            MdoBrowserCdpSession.ControlResult original=handoff.flowFailure();if(starting&&original!=null&&!original.ok&&original.code!=null)code=original.code;
            else if(starting&&handoff.detached())code="cdp_session_detached";
            phase=2;entry.retired=true;entry.ready=false;entry.starting=false;entry.draining=false;entry.code=code;entry.retiredAt=android.os.SystemClock.elapsedRealtime();
            ready=null;disposing=true;networkHandler.removeCallbacks(entry.watch);
            try{
                // Revoke the original handoff first: otherwise marker/source
                // invalidation could be mistaken for an unexplained owner loss.
                try{handoff.close();}catch(RuntimeException|Error error){networkCleanupFailed=true;}sourceProof=null;
                try{if(page!=null)page.retired(code);}catch(RuntimeException|Error error){networkCleanupFailed=true;}
                entry.live.set(false);
                try{if(entry.downloads!=null)entry.downloads.quiesce();}catch(RuntimeException|Error error){networkCleanupFailed=true;}
                try{if(entry.marker!=null){entry.marker.close();cdpWorker.cancel(entry.marker.lease());}}catch(RuntimeException|Error error){networkCleanupFailed=true;}
                try{if(entry.downloadConnection!=null)entry.downloadConnection.disconnect();}catch(RuntimeException|Error error){networkCleanupFailed=true;}
            }finally{disposing=false;}
            MdoBrowserCdpPopupPreparation.Failure callback=failure;failure=null;
            if(callback!=null)try{callback.failed(code,true);}catch(RuntimeException|Error ignored){/* one outcome, never replay native creation */}
            watch();
        }
        void destroyed(){entry.destroyed=true;entry.quota.close();if(phase==3&&human){phase=2;entry.retiredAt=android.os.SystemClock.elapsedRealtime();}
            if(phase<2)retire("cdp_popup_network_native_closed");else if(!disposing)watch();}
        void watch(){
            if(phase==3||networkViews.get(entry.key)!=entry||disposing)return;
            if(phase<2){
                try{if(phase==0)remaining();else if(!current())throw new java.io.IOException("cdp_popup_network_owner_changed");schedule();}
                catch(java.io.IOException|RuntimeException|Error error){retire(reason(error));}return;
            }
            boolean nativeClean=entry.destroyed&&owner.nativeDestroyed()&&!owner.failed();
            // A real later physical close releases human-owned cache files
            // independently of an uncertain shared protocol cleanup receipt.
            if(human&&nativeClean)try{closeDownloads();}catch(RuntimeException|Error error){networkCleanupFailed=true;}
            MdoBrowserCdpSession.ControlResult transfer=human?handoff.humanDetachResult():null;if(transfer!=null&&!transfer.ok)entry.code=transfer.code;
            boolean sourceClean=entry.downloads==null||entry.downloads.sourcesClean();
            boolean humanClean=human&&owner.humanTransferred()&&owner.ended()&&!owner.failed()&&!entry.destroyed&&!owner.nativeDestroyed()&&
                views.owns(entry.key,entry.view)&&documents.get(entry.key)==entry.profile&&handoff.humanDetached();
            entry.protocolDrained=handoff.detached()&&(nativeClean||humanClean)&&sourceClean;
            if(entry.protocolDrained){
                try{if(entry.downloadConnection!=null)entry.downloadConnection.disconnect(true);if(nativeClean)closeDownloads();}
                catch(RuntimeException|Error error){networkCleanupFailed=true;}
                if(humanClean){phase=3;networkHandler.removeCallbacks(entry.watch);return;}
                if(entry.downloads==null||entry.downloads.retiringFiles()==0){phase=3;networkHandler.removeCallbacks(entry.watch);networkViews.remove(entry.key,entry);return;}
            }
            long age=android.os.SystemClock.elapsedRealtime()-entry.retiredAt;
            if(age<0||age>=5000){entry.cleanupObservationExpired=true;networkCleanupFailed=true;try{if(!human||entry.destroyed)closeDownloads();}catch(RuntimeException|Error ignored){}return;}
            try{schedule();}catch(RuntimeException|Error error){entry.cleanupObservationExpired=true;/* the retained retiring row fences creation until recovery observes its receipts */}
        }
        private void closeDownloads(){if(!downloadsClosed&&entry.downloads!=null){downloadsClosed=true;entry.downloads.close();}}
        @Override public void close(){retire("cdp_popup_network_cancelled");}
    }
    private PopupNetwork prepareProtocolPopupNetwork(String key,MdoBrowserCdpPopupNative.Promotion owner,MdoBrowserCdpPopupBroker.Handoff handoff,long deadline,
            ProtocolPopupNetworkReady ready,MdoBrowserCdpPopupPreparation.Failure failure)throws java.io.IOException {
        nativeOwnerAdmission();if(owner==null||handoff==null||!handoff.current()||!handoff.owns(owner)||!owner.current()||ready==null||failure==null)throw new java.io.IOException("cdp_popup_network_arguments_invalid");
        long left=deadline-android.os.SystemClock.elapsedRealtime();if(left<100||left>9000)throw new java.io.IOException("cdp_popup_network_timeout");
        WebView view=owner.view();MdoBrowserDocument profile=documents.get(key);MdoBrowserChannel<WebMessagePort>.Lease lease=channel.current();
        if(!views.owns(key,view)||profile==null||!profile.profileAssigned()||lease==null||!channel.active(lease,host.getUrl())||manual.contains(key)||pageClosed.contains(key))throw new java.io.IOException("cdp_popup_network_owner_changed");
        if(networkViews.containsKey(key)||networkViews.size()>=8||!networkClean())throw new java.io.IOException("cdp_popup_network_busy");
        for(NetworkEntry existing:networkViews.values())if(existing.view==view)throw new java.io.IOException("cdp_popup_network_busy");
        NetworkEntry entry=new NetworkEntry(key,view,profile,lease,null,null);entry.warming=false;entry.starting=true;entry.protocolDrained=false;
        PopupNetwork result=new PopupNetwork(entry,owner,handoff,deadline,ready,failure);entry.popup=result;networkViews.put(key,entry);result.start();return result;
    }
    private void resumePopupNetworks(){for(NetworkEntry entry:new ArrayList<>(networkViews.values()))if(entry.popup!=null)entry.popup.watch();}
    private final Map<String,PopupHandoff> protocolPopupHandoffs=new HashMap<>();
    /** One original transaction through native adoption, source/world readiness
     * and optional UI publication. No phase renews its budget or replays a URL. */
    private final class PopupHandoff implements AutoCloseable {
        final String key,opener;final PopupPreparation parent;final MdoBrowserChannel<WebMessagePort>.Lease lease;
        final MdoBrowserPopups.Snapshot pending;
        final long deadline;final ProtocolPopupReady ready;final MdoBrowserCdpPopupPreparation.Failure failure;
        MdoBrowserCdpPopupNative source;MdoBrowserCdpPopupNative.Created created;WebView view;MdoBrowserDocument profile;
        MdoBrowserCdpView adapter;MdoBrowserCdpPopupNative.Child proof;
        MdoBrowserCdpPopupNative.Promotion promoted;MdoBrowserCdpPopupBroker.Handoff handoff;
        ProtocolPopupNetworkReady networkReady;PopupNetwork network;ProtocolPopupPageReady pageReady;PopupPage page;
        int phase;boolean ended,possible;
        PopupHandoff(String key,String opener,PopupPreparation parent,MdoBrowserPopups.Snapshot pending,int timeout,ProtocolPopupReady ready,MdoBrowserCdpPopupPreparation.Failure failure){
            this.key=key;this.opener=opener;this.parent=parent;this.pending=pending;lease=channel.current();deadline=android.os.SystemClock.elapsedRealtime()+timeout;this.ready=ready;this.failure=failure;
        }
        boolean current(){
            if(ended||protocolPopupHandoffs.get(key)!=this||stopped||browserPaused||channel.current()!=lease||!channel.active(lease,host.getUrl()))return false;
            if(view!=null&&(!views.owns(key,view)||profile==null||documents.get(key)!=profile||!profile.profileAssigned()||manual.contains(key)||pageClosed.contains(key)))return false;
            return promoted!=null?promoted.current()&&handoff!=null&&handoff.current():protocolPopupPreparations.get(opener)==parent&&parent.current();
        }
        int remaining()throws java.io.IOException {
            if(!current())throw new java.io.IOException("cdp_popup_handoff_owner_changed");
            long left=deadline-android.os.SystemClock.elapsedRealtime();if(!current())throw new java.io.IOException("cdp_popup_handoff_owner_changed");
            if(left<1)throw new java.io.IOException("cdp_popup_handoff_timed_out");return (int)Math.min(3000,left);
        }
        void start(){
            try{constructPreparedProtocolPopup(opener,parent.view,pending.id,key,remaining(),this::constructing,this::constructed,this::fail);}
            catch(java.io.IOException|RuntimeException|Error error){fail(reason(error),false);}
        }
        void constructing(MdoBrowserCdpPopupNative original,MdoBrowserCdpPopupWorker.Allocation allocation)throws java.io.IOException {
            remaining();if(phase!=0)throw new java.io.IOException("cdp_popup_handoff_replayed");possible=true;
        }
        void constructed(MdoBrowserCdpPopupNative original,MdoBrowserCdpPopupNative.Created child)throws java.io.IOException {
            if(ended)return;possible=true;if(phase!=0)throw new java.io.IOException("cdp_popup_handoff_replayed");
            source=original;created=child;view=child.view();profile=documents.get(key);remaining();phase=1;
            adoptPreparedProtocolPopup(opener,parent.view,child,remaining(),this::consumed,this::fail);
        }
        void consumed(MdoBrowserCdpPopupNative original,MdoBrowserCdpPopupNative.Created child,MdoBrowserPopups.Adoption receipt)throws java.io.IOException {
            if(ended)return;if(phase!=1||source!=original||created!=child)throw new java.io.IOException("cdp_popup_handoff_replayed");remaining();phase=2;
            adapter=MdoBrowserCdpView.network(view,exact->exact==view&&phase==2&&current(),new MdoBrowserCdpMarker.DocumentQuota());
            proof=source.child(receipt,created,adapter);provePreparedProtocolPopup(opener,parent.view,proof,remaining(),this::proved,this::fail);
        }
        void proved(MdoBrowserCdpPopupNative.Child original)throws java.io.IOException {
            if(ended)return;if(phase!=2||proof!=original)throw new java.io.IOException("cdp_popup_handoff_replayed");remaining();
            handoff=proof.handoff();promoted=handoffProtocolPopup(key,source,created,proof);remaining();phase=3;
            if(!disposeProof())throw new java.io.IOException("cdp_popup_handoff_cleanup_unconfirmed");
            if(networkReady!=null||pageReady!=null){
                try{network=prepareProtocolPopupNetwork(key,promoted,handoff,deadline,this::networked,this::fail);}
                catch(java.io.IOException|RuntimeException|Error error){fail(reason(error),true);}return;
            }
            try{ready.ready(promoted,handoff);}
            catch(java.io.IOException|RuntimeException|Error error){fail("cdp_popup_handoff_callback_failed",true);return;}
            if(ended)return;
            ended=true;phase=4;protocolPopupHandoffs.remove(key,this);
        }
        void networked(PopupNetwork original)throws java.io.IOException {
            if(ended)return;remaining();if(phase!=3||network!=null&&network!=original||original.owner!=promoted||original.handoff!=handoff||!original.current())throw new java.io.IOException("cdp_popup_network_owner_changed");
            if(pageReady!=null){network=original;phase=4;page=prepareProtocolPopupPage(original,this::pageReadied,this::fail);return;}
            network=original;ProtocolPopupNetworkReady callback=networkReady;networkReady=null;
            try{callback.ready(original);}catch(java.io.IOException|RuntimeException|Error error){fail("cdp_popup_handoff_callback_failed",true);return;}
            if(ended)return;
            ended=true;phase=4;protocolPopupHandoffs.remove(key,this);
        }
        void pageReadied(PopupPage original)throws java.io.IOException {
            if(ended)return;remaining();if(phase!=4||page!=null&&page!=original||original.network!=network||!original.current())throw new java.io.IOException("cdp_popup_page_owner_changed");
            page=original;ProtocolPopupPageReady callback=pageReady;pageReady=null;
            try{callback.ready(original);}catch(java.io.IOException|RuntimeException|Error error){fail("cdp_popup_handoff_callback_failed",true);return;}
            if(ended)return;ended=true;phase=5;protocolPopupHandoffs.remove(key,this);
        }
        private String reason(Throwable error){String code=error.getMessage();return code!=null&&code.matches("cdp_[a-z_]{1,80}")?code:"cdp_popup_handoff_failed";}
        private boolean disposeProof(){
            boolean clean=true;if(proof!=null){try{proof.close();}catch(RuntimeException|Error error){clean=false;}proof=null;}
            if(adapter!=null){try{adapter.close();}catch(RuntimeException|Error error){clean=false;}adapter=null;}return clean;
        }
        /** A pending create's close must also observe its original source and
         * independent child receipts; removing its map row is not cleanup. */
        boolean cleanupConfirmed(){
            if(!ended||!parent.preparation.settled()||nativeViewCleanupFailed||networkCleanupFailed||!MdoBrowserCdpPopupNative.creationConfirmed())return false;
            if(network!=null)return network.phase==3;
            if(promoted!=null)return promoted.nativeDestroyed()&&!promoted.failed()&&handoff!=null&&handoff.detached();
            return view==null||views.nativeDestroyed(view);
        }
        void fail(String code,boolean effect){
            if(ended)return;ended=true;phase=5;possible|=effect;protocolPopupHandoffs.remove(key,this);boolean clean=true;
            networkReady=null;pageReady=null;if(network!=null)try{network.close();}catch(RuntimeException|Error error){clean=false;}
            if(handoff!=null)try{handoff.close();}catch(RuntimeException|Error error){clean=false;}
            if(!disposeProof())clean=false;
            try{parent.preparation.close(code);}catch(RuntimeException|Error error){clean=false;}
            try{failure.failed(clean?code:"cdp_popup_handoff_cleanup_unconfirmed",possible);}catch(RuntimeException|Error ignored){/* one outcome; never replay creation */}
        }
        @Override public void close(){fail("cdp_popup_handoff_cancelled",possible);}
    }
    private PopupHandoff handoffPreparedProtocolPopup(String opener,WebView exact,String pending,String key,int timeout,
            ProtocolPopupReady ready,MdoBrowserCdpPopupPreparation.Failure failure)throws java.io.IOException {
        return handoffPreparedProtocolPopup(opener,exact,pending,key,timeout,ready,null,null,failure);
    }
    private PopupHandoff handoffPreparedProtocolPopupNetwork(String opener,WebView exact,String pending,String key,int timeout,
            ProtocolPopupNetworkReady ready,MdoBrowserCdpPopupPreparation.Failure failure)throws java.io.IOException {
        return handoffPreparedProtocolPopup(opener,exact,pending,key,timeout,null,ready,null,failure);
    }
    private PopupHandoff handoffPreparedProtocolPopupPage(String opener,WebView exact,String pending,String key,int timeout,
            ProtocolPopupPageReady ready,MdoBrowserCdpPopupPreparation.Failure failure)throws java.io.IOException {
        return handoffPreparedProtocolPopup(opener,exact,pending,key,timeout,null,null,ready,failure);
    }
    private PopupHandoff handoffPreparedProtocolPopup(String opener,WebView exact,String pending,String key,int timeout,
            ProtocolPopupReady ready,ProtocolPopupNetworkReady networkReady,ProtocolPopupPageReady pageReady,MdoBrowserCdpPopupPreparation.Failure failure)throws java.io.IOException {
        nativeOwnerAdmission();
        if((ready==null?0:1)+(networkReady==null?0:1)+(pageReady==null?0:1)!=1||failure==null||timeout<1||timeout>9000||pending==null||pending.isEmpty())throw new java.io.IOException("cdp_popup_handoff_arguments_invalid");
        PopupPreparation parent=protocolPopupPreparations.get(opener);
        if(parent==null||parent.view!=exact||!parent.current()||!parent.preparation.ready())throw new java.io.IOException("cdp_popup_preparation_required");
        MdoBrowserPopups<WebView> core=windows.get(opener);MdoBrowserPopups.Snapshot request=core==null?null:core.snapshot();
        if(request==null||!request.id.equals(pending))throw new java.io.IOException("cdp_popup_native_request_changed");
        if(protocolPopupHandoffs.containsKey(key))throw new java.io.IOException("cdp_popup_handoff_busy");
        for(PopupHandoff original:protocolPopupHandoffs.values())if(original.parent==parent&&original.pending==request)throw new java.io.IOException("cdp_popup_handoff_busy");
        if(protocolPopupHandoffs.size()>=7)throw new java.io.IOException("cdp_popup_handoff_limit");views.checkNew(key);
        PopupHandoff result=new PopupHandoff(key,opener,parent,request,timeout,ready,failure);result.networkReady=networkReady;result.pageReady=pageReady;protocolPopupHandoffs.put(key,result);result.start();return result;
    }
    interface ProtocolPopupCreated {void ready(MdoBrowserCdpPopupNative source,MdoBrowserCdpPopupNative.Created child)throws java.io.IOException;}
    /** Product creation uses only the original pending callback and guarded
     * pre-birth capture. No CDP target ID or supplied WebView enters this API. */
    private void constructPreparedProtocolPopup(String opener,WebView exact,String pendingId,String key,int timeout,
            ProtocolPopupCreated ready,MdoBrowserCdpPopupPreparation.Failure failure)throws java.io.IOException {
        constructPreparedProtocolPopup(opener,exact,pendingId,key,timeout,(source,allocation)->{},ready,failure);
    }
    private void constructPreparedProtocolPopup(String opener,WebView exact,String pendingId,String key,int timeout,
            MdoBrowserCdpPopupPreparation.Allocated constructing,ProtocolPopupCreated ready,MdoBrowserCdpPopupPreparation.Failure failure)throws java.io.IOException {
        nativeOwnerAdmission();PopupPreparation original=protocolPopupPreparations.get(opener);
        if(original==null||original.view!=exact||!original.current()||!original.preparation.ready())throw new java.io.IOException("cdp_popup_preparation_required");
        if(ready==null||constructing==null)throw new java.io.IOException("cdp_popup_delivery_arguments_invalid");
        original.preparation.allocate(pendingId,timeout,(source,allocation)->{
            if(!original.current())throw new java.io.IOException("cdp_popup_preparation_owner_changed");
            constructing.ready(source,allocation);
            if(!original.current())throw new java.io.IOException("cdp_popup_preparation_owner_changed");
            ready.ready(source,constructProtocolPopup(source,allocation,key));
        },failure);
    }
    /** Accept proof only through the original worker's UI delivery, never by
     * polling a completed marker or rebinding a later child/controller. */
    private void provePreparedProtocolPopup(String opener,WebView exact,MdoBrowserCdpPopupNative.Child child,int timeout,
            MdoBrowserCdpPopupPreparation.Proven ready,MdoBrowserCdpPopupPreparation.Failure failure)throws java.io.IOException {
        nativeOwnerAdmission();PopupPreparation original=protocolPopupPreparations.get(opener);
        if(original==null||original.view!=exact||!original.current()||!original.preparation.ready())throw new java.io.IOException("cdp_popup_preparation_required");
        if(ready==null)throw new java.io.IOException("cdp_popup_delivery_arguments_invalid");
        original.preparation.prove(child,timeout,proof->{
            if(!original.current())throw new java.io.IOException("cdp_popup_preparation_owner_changed");ready.ready(proof);
        },failure);
    }
    /** Native adoption is sent once; only its original consumption notification
     * may advance the configured child into proof and independent ownership. */
    private void adoptPreparedProtocolPopup(String opener,WebView exact,MdoBrowserCdpPopupNative.Created child,int timeout,
            MdoBrowserCdpPopupPreparation.Consumed ready,MdoBrowserCdpPopupPreparation.Failure failure)throws java.io.IOException {
        nativeOwnerAdmission();PopupPreparation original=protocolPopupPreparations.get(opener);
        if(original==null||original.view!=exact||!original.current()||!original.preparation.ready())throw new java.io.IOException("cdp_popup_preparation_required");
        if(ready==null)throw new java.io.IOException("cdp_popup_delivery_arguments_invalid");
        original.preparation.adopt(child,timeout,(source,created,receipt)->{
            if(!original.current())throw new java.io.IOException("cdp_popup_preparation_owner_changed");ready.ready(source,created,receipt);
        },failure);
    }
    private JSONObject popupPreparationState(String key)throws JSONException {
        PopupPreparation original=protocolPopupPreparations.get(key);MdoBrowserCdpPopupPreparation value=original==null?null:original.preparation;
        boolean ready=value!=null&&value.ready();return new JSONObject().put("phase",value==null?"none":value.phase()).put("ready",ready)
            .put("cleanup_confirmed",value==null||value.settled()).put("error",value==null||value.failure()==null?JSONObject.NULL:value.failure());
    }
    private boolean protocolPopupCreationBlocked(){
        boolean blocked=false;for(PopupPreparation original:new ArrayList<>(protocolPopupPreparations.values())){
            if(original.preparation.settled())protocolPopupPreparations.remove(original.key,original);else blocked=true;
        }return blocked||MdoBrowserCdpPopupBroker.creationBlocked();
    }
    private void requireProtocolPopupPreparation(String key,WebView view)throws java.io.IOException {
        PopupPreparation original=protocolPopupPreparations.get(key);
        if(original==null)throw new java.io.IOException("cdp_popup_preparation_required");
        if(original.view!=view||!original.current()||!original.preparation.ready()){original.preparation.close("cdp_popup_preparation_owner_changed");throw new java.io.IOException("cdp_popup_preparation_required");}
    }
    private void executePopupPreparation(String key,String id,WebView view,JSONObject request)throws JSONException {
        final MdoBrowserChannel<WebMessagePort>.Lease original=channel.current();final long epoch=views.epoch(key,view);
        if(!request.optBoolean("agent")||!(request.opt("navigation") instanceof Number)||request.getDouble("navigation")!=epoch||epoch<=0)throw new IllegalArgumentException("popup_preparation_scope_invalid");
        final double requested=request.has("timeout_ms")?request.getDouble("timeout_ms"):3000;
        if(request.has("timeout_ms")&&!(request.opt("timeout_ms") instanceof Number)||!Double.isFinite(requested)||requested<1||requested>3000||requested!=Math.floor(requested))throw new IllegalArgumentException("popup_preparation_scope_invalid");
        final java.util.concurrent.atomic.AtomicBoolean replied=new java.util.concurrent.atomic.AtomicBoolean();
        prepareProtocolPopupAction(key,view,(int)requested,()->{
            if(!replied.compareAndSet(false,true))return;
            try{if(channel.current()!=original||!views.owns(key,view)||views.epoch(key,view)!=epoch)throw new IllegalStateException("popup_preparation_scope_changed");
                reply(original,id,new JSONObject().put("ok",true).put("ready",true).put("navigation",epoch),null);}
            catch(JSONException|RuntimeException error){reply(original,id,null,"popup_preparation_scope_changed");}
        },(code,effect)->{
            if(!replied.compareAndSet(false,true))return;
            try{reply(original,id,new JSONObject().put("ok",false).put("ready",false).put("error",new JSONObject().put("code",code).put("effect_may_have_occurred",false)),null);}
            catch(JSONException error){reply(original,id,null,"popup_preparation_failed");}
        });
    }
    private void prepareProtocolPopupAction(String key,WebView view,Runnable effect,MdoBrowserCdpPopupPreparation.Failure failure){
        prepareProtocolPopupAction(key,view,3000,effect,failure);
    }
    private void prepareProtocolPopupAction(String key,WebView view,int timeout,Runnable effect,MdoBrowserCdpPopupPreparation.Failure failure){
        try{
            PopupPreparation old=protocolPopupPreparations.get(key);
            if(old!=null&&!old.current())old.preparation.close("cdp_popup_preparation_owner_changed");
            if(old!=null&&old.preparation.settled()){protocolPopupPreparations.remove(key,old);old=null;}
            if(old==null){
                if(timeout<100)throw new java.io.IOException("cdp_popup_preparation_timeout");
                NetworkEntry network=networkViews.get(key);MdoBrowserPopups<WebView> core=windows.get(key);
                if(network==null||network.view!=view||network.adapter==null||!network.ready||network.retired||network.draining||core==null||!networkCurrent(network)||manual.contains(key)||pageClosed.contains(key))throw new java.io.IOException("cdp_popup_network_owner_unavailable");
                PopupNetwork protectedOwner=network.popup;
                if(protectedOwner!=null){
                    nativeOwnerAdmission();PopupPage page=protocolPopupDocument(key,view);
                    if(page==null||page.network!=protectedOwner||protectedOwner.sourceProof==null||!protectedOwner.handoff.owns(protectedOwner.sourceProof)||!protectedOwner.sourceProof.current()||
                        !cdpEnvironmentCurrent(key,view,views.epoch(key,view),channel.current())||!chooserReady(dialogs.get(key)))throw new java.io.IOException("cdp_popup_attached_opener_unavailable");
                }
                if(protocolPopupPreparations.size()>=8)throw new java.io.IOException("cdp_popup_preparation_limit");
                PopupPreparation added=new PopupPreparation(key,view,network);
                MdoBrowserCdpPopupNative source=new MdoBrowserCdpPopupNative(network.adapter,view,core);
                added.preparation=new MdoBrowserCdpPopupPreparation(source,added::current,added::lost);protocolPopupPreparations.put(key,added);
                try{if(protectedOwner==null)added.preparation.start(cdpWorker,timeout);else added.preparation.startAttached(cdpWorker,timeout,protectedOwner.handoff,protectedOwner.sourceProof);}
                catch(java.io.IOException|RuntimeException|Error error){if(added.preparation.settled())protocolPopupPreparations.remove(key,added);throw error;}old=added;
            }
            if(old.view!=view)throw new java.io.IOException("cdp_popup_preparation_owner_changed");old.preparation.await(effect,failure);
        }catch(java.io.IOException error){failure.failed(error.getMessage(),false);}
        catch(RuntimeException|Error error){failure.failed("cdp_popup_preparation_failed",false);}
    }
    private void retireProtocolPopupPreparation(String key,WebView view,String reason){
        PopupPreparation original=protocolPopupPreparations.get(key);if(original!=null&&original.view==view){original.preparation.close(reason);if(original.preparation.settled())protocolPopupPreparations.remove(key,original);}
    }
    private void nativeInputResult(MdoBrowserChannel<WebMessagePort>.Lease lease,String id,boolean focusing,MdoBrowserInput.Result result){
        try{
            JSONObject value=new JSONObject().put("accepted",result.ok).put("trusted_input",result.ok&&!focusing)
                .put("target_focused",result.ok&&focusing).put("effect_may_have_occurred",result.effectMayHaveOccurred)
                .put("cleanup_confirmed",result.cleanupConfirmed).put("delivered_steps",result.deliveredSteps);
            if(result.code!=null)value.put("error",new JSONObject().put("code",result.code));
            reply(lease,id,value,null);
        }catch(JSONException error){reply(lease,id,null,"input_result_invalid");}
    }
    private interface NativeInputCompletion {void done(boolean focusing,MdoBrowserInput.Result result);}
    private void executeNativeInput(String key,String id,WebView view,JSONObject request)throws Exception {
        final MdoBrowserChannel<WebMessagePort>.Lease original=channel.current();final long epoch=views.epoch(key,view);
        final JSONObject frozen=new JSONObject(request.toString());
        final java.util.concurrent.atomic.AtomicBoolean replied=new java.util.concurrent.atomic.AtomicBoolean();
        final NativeInputCompletion complete=(focusing,result)->{if(replied.compareAndSet(false,true))nativeInputResult(original,id,focusing,result);};
        Runnable run=()->{try{executePreparedNativeInput(key,view,frozen,original,epoch,complete);}
            catch(Exception error){complete.done(false,new MdoBrowserInput.Result("input_guard_failed",true,views.inputClean(key,view),0));}};
        if(frozen.optBoolean("agent"))try{requireProtocolPopupPreparation(key,view);}catch(java.io.IOException error){complete.done(false,new MdoBrowserInput.Result(error.getMessage(),false,views.inputClean(key,view),0));return;}
        run.run();
    }
    private void executePreparedNativeInput(String key,WebView view,JSONObject request,MdoBrowserChannel<WebMessagePort>.Lease lease,long epoch,NativeInputCompletion complete)throws Exception {
        final MdoBrowserInput input=views.input(key,view);
        try{
            if(!views.owns(key,view)||views.epoch(key,view)!=epoch||channel.current()!=lease||!channel.active(lease,host.getUrl()))throw new java.io.IOException("cdp_input_frame_changed");
            if(!dialogReady(key,view))throw new java.io.IOException("cdp_input_dialog_pending_or_unclean");
            CaptureEntry capture=captures.get(key);
            if(capture!=null&&(capture.capture.busy()||!capture.capture.cleanupConfirmed()))throw new java.io.IOException("cdp_input_capture_busy_or_unclean");
            if(!(request.opt("navigation") instanceof Number)||request.getDouble("navigation")!=epoch||epoch==0)
                throw new java.io.IOException("cdp_input_frame_changed");
            final PopupPage popup=protocolPopupDocument(key,view);
            final MdoBrowserCdpMarker marker=popup==null?cdpMarker(key,view):popup.marker;
            MdoBrowserNativeInput bridge=new MdoBrowserNativeInput(view,cdpWorker,marker,
                ()->cdpEnvironmentCurrent(key,view,epoch,lease)&&chooserReady(dialogs.get(key))&&
                    (popup==null||popup.marker==marker&&popup.documentReady()),()->views.scale(key,view),request,
                popup==null?null:popup.network.handoff,popup==null?null:popup.proof);
            final boolean focusing=bridge.focusing();bridge.run(input,result->complete.done(focusing,result));
        }catch(java.io.IOException|MdoBrowserInput.Failure error){String code=error.getMessage();
            complete.done(false,new MdoBrowserInput.Result(code!=null&&code.matches("[a-z_]{1,63}")?code:"input_guard_failed",false,input.cleanupConfirmed(),0));}
    }
    private static final class CaptureEntry {
        final WebView view;final MdoBrowserCaptureTarget target;final MdoBrowserCapture capture;final CaptureIdentity identity;String requestKey;
        CaptureEntry(WebView view,MdoBrowserCaptureTarget target,CaptureIdentity identity){
            this.view=view;this.target=target;this.identity=identity;capture=new MdoBrowserCapture(target,MdoBrowserCaptureRuntime.scheduler(),
                MdoBrowserCaptureRuntime.WORKER,MdoBrowserCaptureRuntime.BUDGET);
        }
    }
    private static final class CaptureIdentity {String document;}
    private final Map<String,CaptureEntry> captures=new HashMap<>();
    private boolean captureMutationReady(String key,WebView view){
        CaptureEntry entry=captures.get(key);
        return entry==null||entry.view==view&&!entry.capture.busy()&&entry.capture.cleanupConfirmed();
    }
    private CaptureEntry captureEntry(String key,WebView view){
        CaptureEntry existing=captures.get(key);
        if(existing!=null){if(existing.view!=view)throw new IllegalStateException("capture_view_changed");return existing;}
        CaptureIdentity identity=new CaptureIdentity();
        MdoBrowserCaptureTarget target=new MdoBrowserCaptureTarget(view,()->identity.document,()->views.epoch(key,view),()->views.scale(key,view),
            ()->!stopped&&!browserPaused&&views.owns(key,view)&&dialogReady(key,view)&&!manual.contains(key)&&!views.inputBusy(key,view)&&views.inputClean(key,view)&&
                channel.current()!=null&&channel.active(channel.current(),host.getUrl()));
        CaptureEntry entry=new CaptureEntry(view,target,identity);captures.put(key,entry);return entry;
    }
    private void cancelCapture(String key){CaptureEntry entry=captures.get(key);if(entry!=null)entry.capture.cancel();}
    private void cancelCaptures(){for(CaptureEntry entry:new ArrayList<>(captures.values()))entry.capture.cancel();}
    private void executeCaptureCancel(String key,String id,JSONObject request)throws JSONException {
        CaptureEntry entry=captures.get(key);Object original=request.opt("capture_key");
        boolean matches=entry!=null&&original instanceof String&&original.equals(entry.requestKey);
        if(matches)entry.capture.cancel();
        reply(id,new JSONObject().put("cancelled",matches).put("pending_native",entry!=null&&entry.capture.busy()),null);
    }
    private MdoBrowserCdpCapture.Session captureDocumentSession(String key,WebView view,MdoBrowserCdpCapture operation,MdoBrowserCdpCapture.Current current)throws java.io.IOException {
        final PopupPage popup=protocolPopupDocument(key,view);
        final long epoch=views.epoch(key,view);final MdoBrowserChannel<WebMessagePort>.Lease lease=channel.current();
        final MdoBrowserCdpMarker marker=popup==null?cdpMarker(key,view):popup.marker;
        MdoBrowserCdpCapture.Current scope=()->{
            if(!cdpEnvironmentCurrent(key,view,epoch,lease)||!chooserReady(dialogs.get(key))||!marker.lease().current()||popup!=null&&(popup.marker!=marker||!popup.documentReady()))return "capture_changed";
            return current.rejection();
        };
        // A protected child already has an attached native document. Its fence
        // must use that exact proof/shared wire, never ordinary target discovery.
        return popup==null?operation.session(cdpWorker,marker,scope):
            operation.attachedSession(cdpWorker,marker,scope,popup.network.handoff,popup.proof);
    }
    private void executeNativeCapture(String key,String id,WebView view,JSONObject request)throws Exception {
        final MdoBrowserChannel<WebMessagePort>.Lease lease=channel.current();final long epoch=views.epoch(key,view);
        MdoBrowserCdpCapture operation=MdoBrowserCdpCapture.parse(request);CaptureEntry entry=captureEntry(key,view);
        if(entry.capture.busy())throw new IllegalStateException("capture_busy");
        if(!entry.capture.cleanupConfirmed())throw new IllegalStateException("capture_cleanup_failed");
        entry.identity.document=operation.nativeDocument;
        MdoBrowserNativeCapture proof=new MdoBrowserNativeCapture(operation,entry.target,
            ()->!stopped&&views.owns(key,view)&&views.epoch(key,view)==epoch&&channel.current()==lease&&channel.active(lease,host.getUrl()),
            current->captureDocumentSession(key,view,operation,current));
        entry.requestKey=proof.captureKey;
        try{entry.capture.capture(proof.plan,proof::check,result->{
            if(proof.captureKey.equals(entry.requestKey))entry.requestKey=null;proof.close();
            try{
                JSONObject value=new JSONObject().put("ok",result.ok).put("native_document",proof.nativeDocument).put("navigation",proof.navigation)
                    .put("document",proof.document).put("capture_key",proof.captureKey);
                if(result.ok)value.put("data",Base64.encodeToString(result.data,Base64.NO_WRAP)).put("scale",proof.reportedScale).put("format","png");
                else value.put("error",new JSONObject().put("code",result.code));
                reply(lease,id,value,null);
            }catch(JSONException|OutOfMemoryError error){reply(lease,id,null,"capture_result_unavailable");}
            finally{if(result.data!=null)java.util.Arrays.fill(result.data,(byte)0);}
        });}catch(MdoBrowserCapture.Failure|RuntimeException|Error failure){entry.requestKey=null;proof.close();throw failure;}
    }
    private JSONObject captureState(String key,JSONObject state)throws JSONException {
        CaptureEntry entry=captures.get(key);boolean busy=entry!=null&&entry.capture.busy();
        boolean clean=entry==null||entry.capture.cleanupConfirmed();
        return dialogState(key,state).put("capture_capability",views.get(key)!=null).put("capture_busy",busy).put("capture_cleanup_confirmed",clean)
            .put("capture_cleanup_failed",!busy&&!clean).put("capture_budget_used",MdoBrowserCaptureRuntime.BUDGET.pending());
    }
    private final Map<String,MdoBrowserDialogTarget> dialogs=new HashMap<>();
    private void bindDialogs(String key,WebView view){
        if(dialogs.containsKey(key))throw new IllegalStateException("Dialog view identity is already bound");
        MdoBrowserDialogTarget target=new MdoBrowserChooserTarget(view,
            ()->!stopped&&!browserPaused&&views.owns(key,view)&&view.isShown()&&channel.current()!=null&&channel.active(channel.current(),host.getUrl()),
            ()->views.epoch(key,view),blocked->{
                MdoBrowserDocument document=documents.get(key);
                if(document!=null)document.block(MdoBrowserDocument.DIALOG,blocked);
                if(blocked){cancelCapture(key);if(views.owns(key,view))views.input(key,view).invalidate("input_dialog_open");}
            },epoch->{if(views.owns(key,view)&&views.epoch(key,view)==epoch)cancelNavigationRequest(key,view);},windowCallbacks(key,view),blocked->chooserBlocked(key,view,blocked),new MdoBrowserChooserTarget.Manual(){
                public boolean open(ValueCallback<Uri[]> callback,WebChromeClient.FileChooserParams params){return openManualChooser(key,view,callback,params);}
                public void cancel(){manualChooser.cancel(view);}
            });
        dialogs.put(key,target);view.setWebChromeClient(target);
    }
    private boolean dialogReady(String key,WebView view){
        MdoBrowserDialogTarget target=dialogs.get(key);
        return target==null||target.view==view&&target.dialogs.clean()&&target.dialogs.snapshot()==null&&chooserReady(target);
    }
    private void cancelDialog(String key){cancelUpload(key);cancelChooser(key);MdoBrowserDialogTarget target=dialogs.get(key);if(target!=null)target.dialogs.cancel();}
    private void cancelDialogs(){cancelUploads();cancelChoosers();for(MdoBrowserDialogTarget target:new ArrayList<>(dialogs.values()))target.dialogs.cancel();}
    private JSONObject dialogState(String key,JSONObject state)throws JSONException {
        MdoBrowserDialogTarget target=dialogs.get(key);MdoBrowserDialogs.Snapshot pending=target==null?null:target.dialogs.snapshot();
        return chooserState(key,state).put("dialog_capability",target!=null).put("dialog_open",pending!=null)
            .put("dialog_cleanup_failed",target!=null&&!target.dialogs.clean());
    }
    private void executeDialog(String key,String id,JSONObject request)throws JSONException {
        MdoBrowserDialogTarget target=dialogs.get(key);if(target==null)throw new IllegalStateException("dialog_unavailable");
        String action=request.optString("action");
        if("list".equals(action)){
            JSONArray rows=new JSONArray();MdoBrowserDialogs.Snapshot pending=target.dialogs.snapshot();
            if(pending!=null)rows.put(new JSONObject().put("id",pending.id).put("kind",pending.kind).put("url",pending.url)
                .put("message",pending.message).put("navigation",pending.navigation).put("frame_scope","owned-webview")
                .put("respondable",pending.respondable));
            reply(id,new JSONObject().put("ok",true).put("dialogs",rows),null);return;
        }
        if(!"respond".equals(action))throw new IllegalArgumentException("dialog_action_invalid");
        if(manual.contains(key)&&request.optBoolean("agent"))throw new IllegalStateException("user_taken_control");
        Object accept=request.opt("accept"),hasText=request.opt("has_text"),text=request.opt("text");
        if(!(accept instanceof Boolean)||hasText!=null&&!(hasText instanceof Boolean)||text!=null&&!(text instanceof String))
            throw new IllegalArgumentException("dialog_response_invalid");
        MdoBrowserDialogs.Result result=target.dialogs.respond(request.optString("dialog"),(Boolean)accept,
            Boolean.TRUE.equals(hasText),text instanceof String?(String)text:null);
        JSONObject value=new JSONObject().put("ok",result.ok).put("responded",result.ok)
            .put("effect_may_have_occurred",result.effectMayHaveOccurred).put("cleanup_confirmed",result.cleanupConfirmed);
        if(!result.ok)value.put("error",new JSONObject().put("code",result.code).put("effect_may_have_occurred",result.effectMayHaveOccurred));
        reply(id,value,null);
    }
    private static final class NavigationRequest {
        final WebView view;final long epoch;final boolean loading;
        NavigationRequest(WebView view,long epoch,boolean loading){this.view=view;this.epoch=epoch;this.loading=loading;}
    }
    private final Map<String,NavigationRequest> navigationRequests=new HashMap<>();
    private void cancelNavigationRequest(String key,WebView view){
        NavigationRequest pending=navigationRequests.get(key);
        if(pending==null||pending.view!=view||!views.owns(key,view)||pending.epoch!=views.epoch(key,view))return;
        NetworkEntry owner=networkViews.get(key);NativeNavigation operation=owner==null?null:owner.navigation;
        if(operation!=null&&operation.reservation==pending&&!operation.revoked&&operation.ticket!=null&&operation.ticket.result()==null)return;
        navigationRequests.remove(key);loading.put(key,pending.loading);
        MdoBrowserDocument document=documents.get(key);if(document!=null)document.block(MdoBrowserDocument.NAVIGATION,false);
    }
    private void performNavigation(String key,WebView view,String command,String location){
        if(!views.owns(key,view))throw new IllegalStateException("frame_changed");
        if(navigationRequests.containsKey(key))throw new IllegalStateException("navigation_pending");
        if("back".equals(command)&&!view.canGoBack()||"forward".equals(command)&&!view.canGoForward())return;
        if(!Arrays.asList("navigate","back","forward","reload").contains(command))throw new IllegalArgumentException("navigation_action_invalid");
        cancelPopup(key);cancelDialog(key);cancelCapture(key);views.stop(key,view);
        if(!dialogReady(key,view))throw new IllegalStateException("dialog_pending_or_unclean");
        navigationRequests.put(key,new NavigationRequest(view,views.epoch(key,view),Boolean.TRUE.equals(loading.get(key))));
        try{
            loading.put(key,true);
            MdoBrowserDocument document=documents.get(key);if(document!=null)document.block(MdoBrowserDocument.NAVIGATION,true);
            if("navigate".equals(command))view.loadUrl(location);
            else if("back".equals(command))view.goBack();
            else if("forward".equals(command))view.goForward();
            else view.reload();
        }catch(RuntimeException error){cancelNavigationRequest(key,view);throw error;}
    }
    private boolean openManualChooser(String key,WebView view,ValueCallback<Uri[]> callback,WebChromeClient.FileChooserParams params){
        final MdoBrowserChannel<WebMessagePort>.Lease original=channel.current();final long epoch=views.epoch(key,view);
        if(stopped||browserPaused||!view.isShown()||epoch<=0||original==null||!channel.active(original,host.getUrl())){callback.onReceiveValue(null);return true;}
        return manualChooser.open(view,callback,params,
            ()->!stopped&&views.owns(key,view)&&views.epoch(key,view)==epoch&&view.isShown()&&channel.active(original,host.getUrl()),
            ()->chooserBlocked(key,view,true),()->chooserBlocked(key,view,false));
    }
    Boolean openHostChooser(WebView source,ValueCallback<Uri[]> callback,WebChromeClient.FileChooserParams params){
        if(source!=host||callback==null)return Boolean.FALSE;
        final MdoBrowserChannel<WebMessagePort>.Lease original=channel.current();
        return manualChooser.openHost(host,callback,params,
            ()->!stopped&&original!=null&&channel.active(original,host.getUrl()));
    }
    void manualChooserRestore(android.os.Bundle saved){manualChooser.restore(saved);}
    void manualChooserSave(android.os.Bundle saved){manualChooser.save(saved);}
    boolean manualChooserResult(int request,int result,android.content.Intent data){return manualChooser.result(request,result,data);}
    private boolean chooserReady(MdoBrowserDialogTarget target){
        if(!(target instanceof MdoBrowserChooserTarget))return true;
        MdoBrowserChoosers binding=((MdoBrowserChooserTarget)target).choosers;
        MdoBrowserPrivatePicker privatePicker=((MdoBrowserChooserTarget)target).privatePicker;
        return binding.ready()&&privatePicker.clean()&&!privatePicker.pending()&&manualChooser.ready();
    }
    private boolean chooserContinuationReady(String key,WebView view){
        long epoch=views.epoch(key,view);MdoBrowserChannel<WebMessagePort>.Lease original=channel.current();
        if(epoch<=0||!cdpEnvironmentCurrent(key,view,epoch,original)||!manualChooser.ready())return false;
        MdoBrowserDialogTarget target=dialogs.get(key);if(!(target instanceof MdoBrowserChooserTarget)||target.view!=view)return false;
        MdoBrowserChooserTarget chooser=(MdoBrowserChooserTarget)target;
        if(!chooser.choosers.intercepting()||!chooser.choosers.clean()||!chooser.privatePicker.clean()||chooser.privatePicker.pending())return false;
        MdoBrowserChoosers.Snapshot pending=chooser.choosers.snapshot();
        return pending!=null&&pending.navigation==epoch&&pending.cancelable&&!pending.capture&&(pending.mode==0||pending.mode==1);
    }
    private void inputInvalidated(String key,WebView view,long epoch,String reason){
        cancelTargetedUpload(key,view,epoch);
        if("file_chooser_open".equals(reason)&&views.epoch(key,view)==epoch&&chooserContinuationReady(key,view)){
            try{MdoBrowserCdpMarker original=existingDocumentMarker(key,view,epoch,channel.current());if(!cdpWorker.pending(original))return;}
            catch(java.io.IOException|IllegalStateException unavailable){/* No original bound realm: retirement remains mandatory. */}
        }
        cdpViews.invalidate(key,view,epoch,reason);
        protocolPopupDocumentInvalidated(key,view,reason);
    }
    private void chooserBlocked(String key,WebView view,boolean blocked){
        // A retired target must not update a replacement document with the same key.
        if(!views.owns(key,view))return;
        MdoBrowserDocument document=documents.get(key);if(document!=null)document.block(MdoBrowserDocument.CHOOSER,blocked);
        if(blocked){cancelCapture(key);if(views.owns(key,view))views.input(key,view).invalidate("file_chooser_open");}
    }
    private void cancelChooser(String key){
        MdoBrowserDialogTarget target=dialogs.get(key);
        if(target!=null)manualChooser.cancel(target.view);
        if(target instanceof MdoBrowserChooserTarget){((MdoBrowserChooserTarget)target).privatePicker.cancel();((MdoBrowserChooserTarget)target).choosers.cancel();}
    }
    private void cancelChoosers(){
        for(MdoBrowserDialogTarget target:new ArrayList<>(dialogs.values()))
            if(target instanceof MdoBrowserChooserTarget){((MdoBrowserChooserTarget)target).privatePicker.cancel();((MdoBrowserChooserTarget)target).choosers.cancel();}
    }
    private boolean chooserClean(MdoBrowserDialogTarget target){
        if(!(target instanceof MdoBrowserChooserTarget))return true;
        MdoBrowserChoosers binding=((MdoBrowserChooserTarget)target).choosers;
        MdoBrowserPrivatePicker privatePicker=((MdoBrowserChooserTarget)target).privatePicker;
        return binding.ready()&&privatePicker.clean()&&!privatePicker.pending()&&manualChooser.ready();
    }
    private JSONObject chooserState(String key,JSONObject state)throws JSONException {
        MdoBrowserDialogTarget target=dialogs.get(key);
        MdoBrowserChoosers binding=target instanceof MdoBrowserChooserTarget?((MdoBrowserChooserTarget)target).choosers:null;
        MdoBrowserChoosers.Snapshot pending=binding==null?null:binding.snapshot();
        MdoBrowserPrivatePicker privatePicker=target instanceof MdoBrowserChooserTarget?((MdoBrowserChooserTarget)target).privatePicker:null;
        return uploadAssignmentState(key,uploadState(state)).put("chooser_capability",binding!=null).put("chooser_upload_capability",false)
            .put("chooser_armed",binding!=null&&binding.intercepting()).put("chooser_open",pending!=null||privatePicker!=null&&privatePicker.pending()||target!=null&&manualChooser.busy(target.view))
            .put("manual_chooser_supported",true).put("manual_chooser_open",target!=null&&manualChooser.busy(target.view))
            .put("manual_chooser_awaiting_result",manualChooser.awaitingResult())
            .put("chooser_cleanup_failed",binding!=null&&!binding.clean()||privatePicker!=null&&!privatePicker.clean()||!manualChooser.clean()).put("chooser_frame_identity_verified",false);
    }
    private void executeChooser(String key,String id,JSONObject request)throws JSONException {
        MdoBrowserDialogTarget original=dialogs.get(key);
        if(!(original instanceof MdoBrowserChooserTarget)||!views.owns(key,original.view))throw new IllegalStateException("chooser_unavailable");
        MdoBrowserChoosers binding=((MdoBrowserChooserTarget)original).choosers;
        String action=request.optString("action");
        if("set_interception".equals(action)){
            Object enabled=request.opt("enabled");if(!(enabled instanceof Boolean))throw new IllegalArgumentException("chooser_interception_invalid");
            if(Boolean.TRUE.equals(enabled)&&!manualChooser.ready())throw new IllegalStateException("manual_chooser_pending");
            binding.interception((Boolean)enabled);
            if(Boolean.TRUE.equals(enabled)&&!binding.intercepting())throw new IllegalStateException("chooser_cleanup_failed");
            reply(id,new JSONObject().put("ok",true).put("enabled",binding.intercepting()).put("assignment_supported",false),null);return;
        }
        if("list".equals(action)){
            JSONArray rows=new JSONArray();MdoBrowserChoosers.Snapshot pending=binding.snapshot();
            if(pending!=null){
                JSONArray accept=new JSONArray();for(String type:pending.accept)accept.put(type);
                rows.put(new JSONObject().put("id",pending.id).put("mode",pending.mode).put("multiple",pending.multiple)
                    .put("directory",pending.directory).put("capture",pending.capture).put("accept",accept).put("navigation",pending.navigation)
                    .put("cancelable",pending.cancelable).put("frame_scope","owned-webview-unidentified-frame").put("assignment_supported",false));
            }
            reply(id,new JSONObject().put("ok",true).put("choosers",rows).put("assignment_supported",false),null);return;
        }
        if(!"cancel".equals(action))throw new UnsupportedOperationException("chooser_assignment_unsupported");
        Object chooser=request.opt("chooser");if(!(chooser instanceof String)||((String)chooser).length()>100)
            throw new IllegalArgumentException("chooser_identity_invalid");
        MdoBrowserChoosers.Result result=binding.cancel((String)chooser);
        JSONObject value=new JSONObject().put("ok",result.ok).put("cancelled",result.ok)
            .put("effect_may_have_occurred",result.effectMayHaveOccurred).put("cleanup_confirmed",result.cleanupConfirmed);
        if(!result.ok)value.put("error",new JSONObject().put("code",result.code).put("effect_may_have_occurred",result.effectMayHaveOccurred));
        reply(id,value,null);
    }

    private static final class UploadEntry {
        final WebView view;final MdoBrowserUploadStore.Scope scope;final MdoBrowserUploadStore.RetentionOwner data;
        UploadEntry(WebView view,long epoch,MdoBrowserUploadStore.RetentionOwner data){this.view=view;scope=new MdoBrowserUploadStore.Scope(epoch);this.data=data;}
    }
    // A transient document/channel scope must not own files already handed to
    // the renderer. Keep the physical browser's native data owner independently.
    // Only this UI map holds a WebView; the file worker receives its plain flag.
    private static final class UploadDataEntry {
        final WebView view;final MdoBrowserUploadStore.RetentionOwner owner=new MdoBrowserUploadStore.RetentionOwner();
        UploadDataEntry(WebView view){this.view=view;}
    }
    private final Map<String,UploadEntry> uploadOwners=new HashMap<>();
    private final Map<String,UploadDataEntry> uploadDataOwners=new HashMap<>();
    private MdoBrowserUploadQueue uploads;
    private MdoBrowserUploadStore.RetentionOwner uploadData(String key,WebView view){
        UploadDataEntry original=uploadDataOwners.get(key);
        if(original!=null){if(original.view!=view||!original.owner.live())throw new IllegalStateException("upload_data_owner_changed");return original.owner;}
        if(uploadDataOwners.size()>=8)throw new IllegalStateException("upload_data_owner_limit");
        for(UploadDataEntry entry:uploadDataOwners.values())if(entry.view==view)throw new IllegalStateException("upload_data_owner_changed");
        UploadDataEntry created=new UploadDataEntry(view);uploadDataOwners.put(key,created);return created.owner;
    }
    private void retireUploadData(WebView view){
        // Post-destroy only: removal or retiring a proof is not native teardown.
        views.viewDestroyed(view);
        MdoBrowserCdpPopupNative.viewDestroyed(view);
        retireUploadTargets(view);
        for(Iterator<UploadEntry> at=uploadOwners.values().iterator();at.hasNext();){UploadEntry entry=at.next();if(entry.view==view){entry.scope.cancel();at.remove();}}
        for(Iterator<UploadDataEntry> at=uploadDataOwners.values().iterator();at.hasNext();){UploadDataEntry entry=at.next();if(entry.view==view){entry.owner.close();at.remove();}}
    }
    private void cancelUpload(String key){UploadEntry entry=uploadOwners.remove(key);if(entry!=null){entry.scope.cancel();cancelUploadTargets(entry);}}
    private void cancelUpload(String key,WebView expected){UploadEntry entry=uploadOwners.get(key);if(entry!=null&&entry.view==expected)cancelUpload(key);}
    private void cancelUploads(){List<UploadEntry> owned=new ArrayList<>(uploadOwners.values());uploadOwners.clear();for(UploadEntry entry:owned){entry.scope.cancel();cancelUploadTargets(entry);}}
    private JSONObject uploadState(JSONObject state)throws JSONException {
        return state.put("upload_staging_capability",true).put("upload_staging_ready",uploads!=null&&uploads.ready())
            .put("upload_staging_cleanup_failed",uploads!=null&&uploads.cleanupFailed()).put("upload_staging_files",uploads==null?0:uploads.activeFiles())
            .put("upload_staging_bytes",uploads==null?0:uploads.reservedBytes()).put("upload_capability",false)
            .put("upload_staging_recovered_files",uploads==null?0:uploads.recoveredFiles()).put("upload_staging_recovered_bytes",uploads==null?0:uploads.recoveredBytes())
            .put("file_input_capability",false).put("file_clear_capability",false).put("directory_upload_capability",false);
    }
    private static long uploadNumber(JSONObject value,String key,long minimum,long maximum)throws JSONException {
        Object raw=value.get(key);
        if(!(raw instanceof Number))throw new IllegalArgumentException("upload_number_invalid");
        double number=((Number)raw).doubleValue();long integer=((Number)raw).longValue();
        if(!Double.isFinite(number)||number!=integer||integer<minimum||integer>maximum)throw new IllegalArgumentException("upload_number_invalid");
        return integer;
    }
    private static String uploadText(JSONObject value,String key)throws JSONException {
        Object raw=value.get(key);if(!(raw instanceof String))throw new IllegalArgumentException("upload_text_invalid");return (String)raw;
    }
    private static void uploadFields(JSONObject request,String... allowed){
        Set<String> names=new HashSet<>(Arrays.asList("channel","id","command","browser","agent"));names.addAll(Arrays.asList(allowed));
        for(Iterator<String> at=request.keys();at.hasNext();)if(!names.contains(at.next()))throw new IllegalArgumentException("upload_field_unsupported");
    }
    private void executeUpload(String key,String id,WebView view,JSONObject request)throws Exception {
        String action=uploadText(request,"action");long epoch=views.epoch(key,view);
        if(!views.owns(key,view)||epoch<=0||stopped||channel.current()==null||!channel.active(channel.current(),host.getUrl()))throw new IllegalStateException("upload_owner_changed");
        if(!Arrays.asList("status","cancel","release","clear_status","clear_cancel").contains(action)&&(browserPaused||manual.contains(key)))throw new IllegalStateException("upload_owner_changed");
        if(uploadCommand(key,id,view,request))return;
        if(!Arrays.asList("begin","chunk","seal","status","cancel","release").contains(action))throw new UnsupportedOperationException("upload_assignment_unsupported");
        // This path receives bytes only. A source ref, frame chain, chooser or
        // directory must not be mistaken for a verified Android grant target.
        MdoBrowserUploadQueue.Request queued;
        if("begin".equals(action)){
            uploadFields(request,"action","upload_key","files","navigation","timeout_ms","document","ref","frame_chain");
            if(Boolean.TRUE.equals(loading.get(key))||!dialogReady(key,view))throw new IllegalStateException("upload_document_unavailable");
            if(uploadNumber(request,"navigation",1,9007199254740991L)!=epoch)throw new IllegalStateException("upload_owner_changed");
            long timeout=request.has("timeout_ms")?uploadNumber(request,"timeout_ms",1000,120000):60000;
            long now=android.os.SystemClock.elapsedRealtime();if(now<0||now>Long.MAX_VALUE-timeout)throw new IllegalStateException("upload_clock_invalid");
            JSONArray rows=request.getJSONArray("files");if(rows.length()<1||rows.length()>16)throw new IllegalArgumentException("upload_metadata_invalid");
            MdoBrowserUploadStore.Spec[] specs=new MdoBrowserUploadStore.Spec[rows.length()];
            for(int index=0;index<specs.length;index++){
                JSONObject row=rows.getJSONObject(index);for(Iterator<String> fields=row.keys();fields.hasNext();)if(!Arrays.asList("name","size","sha256").contains(fields.next()))throw new IllegalArgumentException("upload_field_unsupported");
                specs[index]=new MdoBrowserUploadStore.Spec(uploadText(row,"name"),uploadNumber(row,"size",0,33554432L),uploadText(row,"sha256"));
            }
            queued=MdoBrowserUploadQueue.Request.begin(uploadText(request,"upload_key"),specs,now+timeout);
        }else if("chunk".equals(action)){
            uploadFields(request,"action","ticket","navigation","file","offset","base64");
            if(uploadNumber(request,"navigation",1,9007199254740991L)!=epoch)throw new IllegalStateException("upload_owner_changed");
            queued=MdoBrowserUploadQueue.Request.chunk(uploadText(request,"ticket"),(int)uploadNumber(request,"file",0,15),uploadNumber(request,"offset",0,33554432L),uploadText(request,"base64"));
        }else{
            uploadFields(request,"action","ticket","navigation","confirmed");
            if(request.has("navigation")&&uploadNumber(request,"navigation",1,9007199254740991L)!=epoch)throw new IllegalStateException("upload_owner_changed");
            if("release".equals(action)&&!Boolean.TRUE.equals(request.opt("confirmed")))throw new IllegalArgumentException("upload_confirmation_required");
            queued=MdoBrowserUploadQueue.Request.ticket(action,uploadText(request,"ticket"));
        }
        UploadEntry entry=uploadOwners.get(key);
        if(entry==null||entry.view!=view||entry.scope.navigation!=epoch||!entry.scope.live()){
            if(entry!=null)entry.scope.cancel();entry=new UploadEntry(view,epoch,uploadData(key,view));uploadOwners.put(key,entry);
        }
        final UploadEntry original=entry;final MdoBrowserChannel<WebMessagePort>.Lease lease=channel.current();
        final Object reserved=reserveUploadTarget(key,entry,request,queued);
        if(uploads==null)uploads=MdoBrowserUploadRuntime.get(activity);
        if(!uploads.submit(entry.scope,queued,outcome->{
            if(!views.owns(key,view)||views.epoch(key,view)!=epoch||uploadOwners.get(key)!=original||!original.scope.live()){
                reply(lease,id,null,"upload_owner_changed");return;
            }
            try{
                boolean bound=uploadTargetReady(reserved,outcome);
                MdoBrowserUploadStore.Snapshot value=outcome.value;
                if(outcome.ok()&&value!=null&&!Arrays.asList("status","cancel","release").contains(action)&&android.os.SystemClock.elapsedRealtime()>=value.expires){reply(lease,id,null,"upload_expired");return;}
                JSONObject result=new JSONObject().put("ok",outcome.ok()).put("staged_only",true).put("assignment_supported",bound)
                    .put("retained",false).put("effect_may_have_occurred",false).put("staging_may_have_changed",outcome.stagingMayHaveChanged);
                if(!outcome.ok()){
                    JSONObject error=new JSONObject().put("code",outcome.code).put("effect_may_have_occurred",false);
                    if(outcome.cause!=null)error.put("cause_code",outcome.cause);result.put("error",error);
                }else if(value!=null){
                    JSONArray files=new JSONArray();for(int index=0;index<value.files.length;index++){
                        MdoBrowserUploadStore.Spec spec=value.files[index];files.put(new JSONObject().put("name",spec.name).put("size",spec.size).put("sha256",spec.sha256).put("received",value.received[index]));
                    }
                    result.put("ticket",value.ticket).put("upload_key",value.key).put("state",value.state).put("navigation",value.navigation)
                        .put("sealed",value.sealed).put("size",value.size).put("files",files).put("cleanup_confirmed",value.cleanupConfirmed);
                }
                reply(lease,id,result,null);
            }catch(JSONException|RuntimeException|OutOfMemoryError failure){reply(lease,id,null,"upload_result_unconfirmed");}
        }))throw new IllegalStateException("upload_worker_busy");
    }

    private boolean chooserFramesAllowed(String key,WebView view,JSONObject request){
        if(!"frames".equals(request.opt("command"))||!chooserContinuationReady(key,view)||views.inputBusy(key,view)||!views.inputClean(key,view))return false;
        try{return MdoBrowserCdpFrameRequest.parse(request).chooserContinuation();}
        catch(java.io.IOException|RuntimeException invalid){return false;}
    }
    private void executeFrames(String key,String id,WebView view,JSONObject request)throws JSONException {
        final MdoBrowserChannel<WebMessagePort>.Lease original=channel.current();final long epoch=views.epoch(key,view);
        try{
            if(uploadBusy(key,view))throw new java.io.IOException("cdp_upload_busy");
            final MdoBrowserCdpFrameRequest operation=MdoBrowserCdpFrameRequest.parse(request);
            final MdoBrowserCdpNetworks.Navigation proof=navigationReadProof(key,view,operation.navigationRef);
            // The ordinary frame reader has no UI mutation reservation. Only
            // upload.clear may join the fixed primitive to its original record.
            if(operation.action.equals("file_clear"))throw new java.io.IOException("cdp_file_clear_requires_upload_route");
            if(operation.effect&&request.optBoolean("agent"))requireProtocolPopupPreparation(key,view);
            // Product/profile admission runs in request() before this branch.
            // No request field can enable debugging or bypass that admission.
            boolean blocked=!chooserReady(dialogs.get(key)),continuation=operation.chooserContinuation();
            if(blocked&&(!continuation||!chooserContinuationReady(key,view)||views.inputBusy(key,view)||!views.inputClean(key,view)))throw new java.io.IOException("cdp_chooser_scope_unavailable");
            final PopupPage popup=protocolPopupDocument(key,view,"list".equals(operation.action)&&proof==null);
            final boolean restoring=popup!=null&&popup.restoring;
            final MdoBrowserCdpWorker.DocumentRead restoringRead=restoring?popup.restoringRead:null;
            final MdoBrowserCdpMarker marker;
            if(popup!=null){
                if(continuation){if(!popup.marker.lease().busy())throw new java.io.IOException("cdp_worker_binding_unavailable");}
                else if(!cdpEnvironmentCurrent(key,view,epoch,original))throw new java.io.IOException("cdp_popup_page_document_unavailable");
                marker=popup.marker;
            }else marker=continuation?cdpViews.existing(key,view,epoch,original):cdpMarker(key,view);
            MdoBrowserCdpWorker.Completion complete=result->{
                try{
                    JSONObject response;
                    try{
                        if(!views.owns(key,view)||views.epoch(key,view)!=epoch)throw new java.io.IOException("cdp_worker_cancelled");
                        if(popup!=null&&(popup.marker!=marker||!(restoring&&popup.restoring&&popup.current()&&popup.documentCurrent()||popup.documentReady())||!continuation&&!cdpEnvironmentCurrent(key,view,epoch,original)))throw new java.io.IOException("cdp_worker_cancelled");
                        if(proof!=null&&navigationReadProof(key,view,operation.navigationRef)!=proof)throw new java.io.IOException("cdp_navigation_cancelled");
                        if(restoringRead!=null&&(popup.restoringRead!=restoringRead||result.code==null&&!restoringRead.current()))throw new java.io.IOException("cdp_navigation_document_changed");
                        response=MdoBrowserCdpFrameRequest.response(result);
                        if(restoring){if(response.optBoolean("ok"))popup.restored(marker,response);else popup.restoreFailed(marker);}
                    }catch(java.io.IOException changed){response=MdoBrowserCdpFrameRequest.failure(changed.getMessage(),result.effectMayHaveOccurred);}
                    if(restoring&&!response.optBoolean("ok"))popup.restoreFailed(marker);
                    reply(original,id,response,null);
                }catch(JSONException error){reply(original,id,null,"frame_result_invalid");}
            };
            if(popup!=null){
                if(continuation)cdpWorker.submitExisting(marker,operation.timeout,operation.effect,operation::run,complete);
                else if(proof!=null)cdpWorker.submitDocumentGuarded(marker,popup.network.handoff,popup.proof,operation.timeout,operation.effect,proof,operation::run,complete);
                else if(restoringRead!=null)cdpWorker.submitDocumentGuarded(marker,popup.network.handoff,popup.proof,operation.timeout,false,restoringRead,operation::run,complete);
                else cdpWorker.submitDocument(marker,popup.network.handoff,popup.proof,operation.timeout,operation.effect,operation::run,complete);
            }
            else if(proof!=null)cdpWorker.submitGuarded(marker,operation.timeout,operation.effect,proof,operation::run,complete);
            else if(continuation)cdpWorker.submitExisting(marker,operation.timeout,operation.effect,operation::run,complete);
            else cdpWorker.submit(marker,operation.timeout,operation.effect,operation::run,complete);
        }catch(java.io.IOException error){reply(original,id,MdoBrowserCdpFrameRequest.failure(error.getMessage(),false),null);}
        catch(RuntimeException error){reply(original,id,MdoBrowserCdpFrameRequest.failure("cdp_binding_owner_changed",false),null);}
    }

    /** Native original-target/operation records. These are never reconstructed
     * from a caller ticket, URI, callback timing or a replacement CDP world. */
    private static final class UploadAction {
        final UploadEntry source;final MdoBrowserCdpFileInput target;final MdoBrowserCdpMarker marker;
        final Object channel;final String key;String ticket;MdoBrowserUploadCoordinator coordinator;boolean released;
        UploadAction(UploadEntry source,MdoBrowserCdpFileInput target,MdoBrowserCdpMarker marker,Object channel,String key){
            this.source=source;this.target=target;this.marker=marker;this.channel=channel;this.key=key;
        }
    }
    private static final class UploadBook {
        final WebView view;final MdoBrowserUploadStore.RetentionOwner owner;
        final Map<String,UploadAction> keys=new HashMap<>(),tickets=new HashMap<>();UploadAction active;
        final Map<String,ClearAction> clearKeys=new HashMap<>();ClearAction clearing;
        UploadBook(WebView view,MdoBrowserUploadStore.RetentionOwner owner){this.view=view;this.owner=owner;}
    }
    private static final class ClearAction {
        final UploadEntry source;final MdoBrowserCdpFileInput target;final MdoBrowserCdpMarker marker;
        final Object channel;final String key,ticket="u-"+java.util.UUID.randomUUID();
        boolean done,cancelled,ok,dispatched,clean=true,released,registered;String error;
        ClearAction(UploadEntry source,MdoBrowserCdpFileInput target,MdoBrowserCdpMarker marker,Object channel,String key){
            this.source=source;this.target=target;this.marker=marker;this.channel=channel;this.key=key;
        }
    }
    private final Map<MdoBrowserUploadStore.RetentionOwner,UploadBook> uploadBooks=new HashMap<>();
    private UploadBook uploadBook(String key,WebView view){
        UploadDataEntry data=uploadDataOwners.get(key);if(data==null||data.view!=view||!data.owner.live())return null;
        UploadBook book=uploadBooks.get(data.owner);return book!=null&&book.view==view?book:null;
    }
    private boolean uploadBusy(String key,WebView view){UploadBook book=uploadBook(key,view);return book!=null&&(book.clearing!=null&&!book.clearing.done||book.active!=null&&book.active.coordinator!=null&&!book.active.coordinator.done());}
    private void cancelClear(ClearAction clear){if(clear!=null&&!clear.done){clear.cancelled=true;cdpWorker.cancel(clear.marker.lease());}}
    private void cancelTargetedUpload(String key,WebView view,long epoch){
        UploadBook book=uploadBook(key,view);UploadAction active=book==null?null:book.active;
        if(active!=null&&active.source.scope.navigation==epoch&&active.coordinator!=null)active.coordinator.close();
        if(book!=null&&book.clearing!=null&&book.clearing.source.scope.navigation==epoch)cancelClear(book.clearing);
    }
    private void cancelUploadTargets(UploadEntry source){
        UploadBook book=uploadBooks.get(source.data);if(book==null)return;
        UploadAction active=book.active;if(active!=null&&active.source==source&&active.coordinator!=null)active.coordinator.close();
        if(book.clearing!=null&&book.clearing.source==source)cancelClear(book.clearing);
        for(Iterator<UploadAction> at=book.keys.values().iterator();at.hasNext();)if(at.next().source==source)at.remove();
        for(Iterator<UploadAction> at=book.tickets.values().iterator();at.hasNext();){UploadAction row=at.next();if(row.source==source&&row.coordinator==null)at.remove();}
    }
    private void retireUploadTargets(WebView view){
        // Native callback cancellation can synchronously retire another view.
        // Never retain a live map iterator across coordinator.close().
        for(UploadBook book:new ArrayList<>(uploadBooks.values())){if(book.view!=view||!uploadBooks.remove(book.owner,book))continue;
            UploadAction active=book.active;if(active!=null&&active.coordinator!=null)active.coordinator.close();
            cancelClear(book.clearing);
        }
    }
    private static MdoBrowserCdpFileInput uploadTarget(JSONObject request)throws Exception {
        JSONArray chain=request.getJSONArray("frame_chain");if(chain.length()<1)throw new IllegalArgumentException("upload_target_invalid");
        JSONObject leaf=chain.getJSONObject(0);
        return MdoBrowserCdpFileInput.parse(new JSONObject().put("frame",leaf.get("frame")).put("document",leaf.get("document"))
            .put("local_document",uploadText(request,"document")).put("ref",uploadText(request,"ref")).put("frame_chain",chain));
    }
    private static boolean sameUploadTarget(MdoBrowserCdpFileInput left,MdoBrowserCdpFileInput right){
        if(!left.document.equals(right.document)||!left.ref.equals(right.ref)||left.pins.chain.size()!=right.pins.chain.size())return false;
        for(int i=0;i<left.pins.chain.size();i++){MdoBrowserCdpFrameChain.Pin a=left.pins.chain.get(i),b=right.pins.chain.get(i);if(!a.frame.equals(b.frame)||!a.document.equals(b.document))return false;}return true;
    }
    private Object reserveUploadTarget(String key,UploadEntry source,JSONObject request,MdoBrowserUploadQueue.Request queued)throws Exception {
        if(!"begin".equals(queued.action))return null;
        if(uploadBusy(key,source.view))throw new IllegalStateException("upload_operation_busy");
        boolean pinned=request.has("document")||request.has("ref")||request.has("frame_chain");
        UploadBook book=uploadBook(key,source.view);UploadAction old=book==null?null:book.keys.get(queued.key);
        if(!pinned){
            if(old!=null){if(old.target!=null||old.source!=source||old.channel!=channel.current())throw new IllegalStateException("upload_target_changed");return old;}
            if(book==null){book=new UploadBook(source.view,source.data);uploadBooks.put(source.data,book);}
            if(book.keys.size()>=64||book.tickets.size()>=64)throw new IllegalStateException("upload_operation_limit");
            UploadAction created=new UploadAction(source,null,null,channel.current(),queued.key);book.keys.put(queued.key,created);return created;
        }
        if(!browserUseSupport().optBoolean("available"))throw new UnsupportedOperationException("upload_assignment_unsupported");
        MdoBrowserCdpFileInput target=uploadTarget(request);
        MdoBrowserChannel<WebMessagePort>.Lease original=channel.current();
        if(!cdpEnvironmentCurrent(key,source.view,source.scope.navigation,original)||!chooserReady(dialogs.get(key))||views.inputBusy(key,source.view)||!views.inputClean(key,source.view))
            throw new IllegalStateException("upload_document_unavailable");
        MdoBrowserCdpMarker marker=existingDocumentMarker(key,source.view,source.scope.navigation,original);
        if(cdpWorker.pending(marker)||uploadBusy(key,source.view))throw new IllegalStateException("upload_operation_busy");
        if(old!=null){if(old.target==null||old.source!=source||old.channel!=original||old.marker!=marker||!sameUploadTarget(old.target,target))throw new IllegalStateException("upload_target_changed");return old;}
        if(book==null){book=new UploadBook(source.view,source.data);uploadBooks.put(source.data,book);}
        if(book.keys.size()>=64||book.tickets.size()>=64)throw new IllegalStateException("upload_operation_limit");
        UploadAction created=new UploadAction(source,target,marker,original,queued.key);book.keys.put(queued.key,created);return created;
    }
    private boolean uploadTargetReady(Object reserved,MdoBrowserUploadQueue.Outcome outcome){
        if(reserved==null||!outcome.ok()||outcome.value==null)return false;UploadAction action=(UploadAction)reserved;
        UploadBook book=uploadBooks.get(action.source.data);MdoBrowserUploadStore.Snapshot value=outcome.value;
        if(book==null||book.keys.get(action.key)!=action||!action.key.equals(value.key)||value.navigation!=action.source.scope.navigation||!action.source.scope.live())return false;
        if(action.ticket!=null&&!action.ticket.equals(value.ticket))throw new IllegalStateException("upload_ticket_changed");
        UploadAction old=book.tickets.get(value.ticket);if(old!=null&&old!=action)throw new IllegalStateException("upload_ticket_changed");
        action.ticket=value.ticket;book.tickets.put(value.ticket,action);return action.target!=null;
    }
    private boolean uploadMarkerCurrent(String key,WebView view,long epoch,MdoBrowserChannel<WebMessagePort>.Lease original,MdoBrowserCdpMarker marker){
        try{return existingDocumentMarker(key,view,epoch,original)==marker;}
        catch(java.io.IOException|RuntimeException unavailable){return false;}
    }
    private boolean uploadCurrent(String key,UploadBook book,UploadAction action,MdoBrowserChannel<WebMessagePort>.Lease original){
        UploadDataEntry data=uploadDataOwners.get(key);MdoBrowserDialogTarget dialog=dialogs.get(key);
        return data!=null&&data.view==book.view&&data.owner==book.owner&&book.owner.live()&&book.active==action&&
            uploadOwners.get(key)==action.source&&action.source.scope.live()&&channel.current()==original&&action.channel==original&&
            uploadMarkerCurrent(key,book.view,action.source.scope.navigation,original,action.marker)&&manualChooser.ready()&&
            dialog instanceof MdoBrowserChooserTarget&&dialog.view==book.view&&((MdoBrowserChooserTarget)dialog).choosers.clean()&&
            ((MdoBrowserChooserTarget)dialog).choosers.snapshot()==null&&((MdoBrowserChooserTarget)dialog).privatePicker.clean()&&
            views.inputClean(key,book.view)&&!views.inputBusy(key,book.view);
    }
    private JSONObject uploadReceipt(UploadAction action)throws JSONException {
        MdoBrowserUploadCoordinator.Result value=action.coordinator.result();boolean pending=value==null;
        MdoBrowserUploadStore.Retention kept=action.coordinator.retained();boolean retained=!pending&&value.retained;
        if(pending&&kept!=null)retained=kept.adopted()&&kept.live(android.os.SystemClock.elapsedRealtime());
        boolean released=action.released||!pending&&value.stagingCleanupConfirmed;
        JSONObject result=new JSONObject().put("ok",pending||value.ok).put("ticket",action.ticket).put("upload_key",action.key)
            .put("assignment_ok",!pending&&value.ok)
            .put("navigation",action.source.scope.navigation).put("receipt_only",true).put("assignment_supported",true)
            .put("pending",pending).put("accepted",!pending&&value.accepted).put("completed",!pending&&value.completed)
            .put("verified",!pending&&value.verified).put("dispatched",pending||value.dispatched)
            .put("effect_may_have_occurred",pending||value.dispatched).put("retained",retained).put("released",released)
            .put("state",pending?"assigning":released?"released":retained?"retained":"unretained")
            .put("cleanup_confirmed",!pending&&value.cleanupConfirmed).put("staging_cleanup_confirmed",!pending&&value.stagingCleanupConfirmed)
            .put("events_trusted",false).put("input_method","native_picker_dom_filelist");
        if(!pending&&value.outcome!=null)result.put("outcome",value.outcome);
        if(!pending&&value.code!=null)result.put("error",new JSONObject().put("code",value.code).put("effect_may_have_occurred",value.dispatched));
        return result;
    }
    private JSONObject uploadControlReceipt(UploadAction action)throws JSONException {
        JSONObject value=uploadReceipt(action);if(value.has("error"))value.put("assignment_error",value.remove("error"));return value.put("ok",true);
    }
    private boolean uploadCommand(String key,String id,WebView view,JSONObject request)throws Exception {
        String verb=uploadText(request,"action");
        if(clearCommand(key,id,view,request))return true;
        if("begin".equals(verb)&&uploadBusy(key,view))throw new IllegalStateException("upload_operation_busy");
        if(!Arrays.asList("assign","status","cancel","release").contains(verb))return false;
        if("assign".equals(verb)&&!browserUseSupport().optBoolean("available"))throw new UnsupportedOperationException("upload_assignment_unsupported");
        UploadBook book=uploadBook(key,view);String ticket=uploadText(request,"ticket");UploadAction action=book==null?null:book.tickets.get(ticket);
        MdoBrowserUploadQueue.Request.ticket("status",ticket);
        if(!"assign".equals(verb)){
            if(action==null||action.coordinator==null)return false;
            uploadFields(request,"action","ticket","navigation","confirmed");
            if(request.has("navigation")&&uploadNumber(request,"navigation",1,9007199254740991L)!=action.source.scope.navigation)throw new IllegalStateException("upload_owner_changed");
            final MdoBrowserChannel<WebMessagePort>.Lease replyChannel=channel.current();
            if("cancel".equals(verb)){action.coordinator.close();reply(replyChannel,id,uploadControlReceipt(action).put("cancel_requested",true),null);return true;}
            if("release".equals(verb)&&!Boolean.TRUE.equals(request.opt("confirmed")))throw new IllegalArgumentException("upload_confirmation_required");
            if("release".equals(verb)&&!action.coordinator.done())throw new IllegalStateException("upload_assignment_pending");
            MdoBrowserUploadStore.Retention kept=action.coordinator.retained();
            if(kept==null){reply(replyChannel,id,uploadControlReceipt(action),null);return true;}
            final UploadAction original=action;final boolean release="release".equals(verb);
            if(!uploads.submit(action.source.scope,MdoBrowserUploadQueue.Request.retained(kept,release),outcome->{
                try{
                    if(!views.owns(key,view)||uploadBook(key,view)!=book||book.tickets.get(ticket)!=original){reply(replyChannel,id,null,"upload_owner_changed");return;}
                    if(outcome.ok()&&outcome.retainedValue.cleanupConfirmed)original.released=true;
                    JSONObject result=uploadControlReceipt(original);
                    if(!outcome.ok())result.put("ok",false).put("error",new JSONObject().put("code",outcome.code));
                    else result.put("retained",outcome.retainedValue.retained).put("state",outcome.retainedValue.state).put("released",outcome.retainedValue.cleanupConfirmed);
                    reply(replyChannel,id,result,null);
                }catch(JSONException|RuntimeException|OutOfMemoryError error){reply(replyChannel,id,null,"upload_result_unconfirmed");}
            }))throw new IllegalStateException("upload_worker_busy");return true;
        }
        uploadFields(request,"action","ticket","navigation","document","ref","frame_chain","timeout_ms");
        if(!browserUseSupport().optBoolean("available"))throw new UnsupportedOperationException("upload_assignment_unsupported");
        long epoch=uploadNumber(request,"navigation",1,9007199254740991L);
        if(action==null||action.target==null||epoch!=views.epoch(key,view)||epoch!=action.source.scope.navigation||!sameUploadTarget(action.target,uploadTarget(request)))throw new IllegalStateException("upload_target_changed");
        if(action.coordinator!=null){reply(channel.current(),id,uploadReceipt(action),null);return true;}
        if(uploadBusy(key,view))throw new IllegalStateException("upload_operation_busy");
        final MdoBrowserChannel<WebMessagePort>.Lease original=channel.current();final UploadAction operation=action;final UploadBook originalBook=book;
        if(uploadOwners.get(key)!=action.source||!action.source.scope.live()||action.channel!=original||
            !cdpEnvironmentCurrent(key,view,epoch,original)||!chooserReady(dialogs.get(key))||views.inputBusy(key,view)||!views.inputClean(key,view))throw new IllegalStateException("upload_owner_changed");
        if(existingDocumentMarker(key,view,epoch,original)!=action.marker||cdpWorker.pending(action.marker))throw new IllegalStateException("upload_target_changed");
        MdoBrowserDialogTarget dialog=dialogs.get(key);if(!(dialog instanceof MdoBrowserChooserTarget)||dialog.view!=view)throw new IllegalStateException("upload_picker_unavailable");
        int timeout=(int)(request.has("timeout_ms")?uploadNumber(request,"timeout_ms",100,120000):60000);
        android.os.Handler handler=new android.os.Handler(android.os.Looper.getMainLooper());
        MdoBrowserUploadCoordinator coordinator=new MdoBrowserUploadCoordinator(uploads,action.source.scope,ticket,book.owner,cdpWorker,action.marker,((MdoBrowserChooserTarget)dialog).privatePicker,action.target,
            ()->uploadCurrent(key,originalBook,operation,original),new MdoBrowserUploadCoordinator.Scheduler(){
                public void at(Runnable task,long due){long delay=Math.max(0,due-android.os.SystemClock.elapsedRealtime());if(!handler.postDelayed(task,delay))throw new IllegalStateException("Upload scheduler stopped");}
                public void remove(Runnable task){handler.removeCallbacks(task);}
            },android.os.SystemClock::elapsedRealtime,timeout,value->{
                if(originalBook.active==operation)originalBook.active=null;
                try{
                    JSONObject result=uploadReceipt(operation);
                    if(!views.owns(key,view)||views.epoch(key,view)!=epoch)result.put("ok",false).put("accepted",false).put("completed",false).put("verified",false)
                        .put("error",new JSONObject().put("code","upload_owner_changed").put("effect_may_have_occurred",value.dispatched));
                    reply(original,id,result,null);
                }catch(JSONException|RuntimeException|OutOfMemoryError error){reply(original,id,null,"upload_result_unconfirmed");}
            });
        operation.coordinator=coordinator;book.active=operation;coordinator.start();return true;
    }
    private JSONObject uploadAssignmentState(String key,JSONObject state)throws JSONException {
        UploadBook book=uploadBook(key,views.get(key));boolean failed=false;
        if(book!=null)for(UploadAction action:book.tickets.values())if(action.coordinator!=null&&action.coordinator.done()){
            MdoBrowserUploadCoordinator.Result value=action.coordinator.result();if(value!=null&&!value.cleanupConfirmed){failed=true;break;}}
        boolean supported=browserUseSupport().optBoolean("available");
        if(book!=null)for(ClearAction clear:book.clearKeys.values())if(clear.done&&!clear.clean){failed=true;break;}
        return state.put("upload_busy",uploadBusy(key,views.get(key))).put("upload_cleanup_failed",failed)
            .put("upload_capability",supported).put("file_input_capability",supported).put("file_clear_capability",supported);
    }
    private JSONObject clearReceipt(ClearAction clear,boolean control)throws JSONException {
        JSONObject result=new JSONObject().put("ok",control||!clear.done||clear.ok).put("accepted",clear.done&&clear.ok)
            .put("pending",!clear.done).put("completed",clear.done&&clear.ok).put("cleared",clear.done&&clear.ok).put("verified",clear.done&&clear.ok)
            .put("upload_key",clear.key).put("navigation",clear.source.scope.navigation).put("clear",true)
            .put("receipt_only",true).put("assignment_ok",clear.done&&clear.ok).put("assignment_supported",true)
            .put("dispatched",!clear.done||clear.dispatched).put("effect_may_have_occurred",!clear.done||clear.dispatched)
            .put("retained",false).put("released",clear.released).put("cleanup_confirmed",clear.done&&clear.clean)
            .put("native_job_drained",clear.done).put("state",!clear.done?"clearing":clear.released?"released":clear.ok?"cleared":"failed")
            .put("input_method","native_file_clear");
        if(clear.registered)result.put("ticket",clear.ticket);
        if(clear.error!=null){String message=!clear.clean?"Native file-clear cleanup was not confirmed; end this browser before further input. Its original receipt remains available":
                clear.dispatched?"The original file-clear operation was not confirmed; inspect its receipt and delivered effects before an explicit new action":
                "The native file-clear request was rejected before dispatch; inspect its original scope before another action";
            JSONObject error=new JSONObject().put("code",clear.error).put("message",message).put("effect_may_have_occurred",clear.dispatched);
            result.put(control?"assignment_error":"error",error);}
        return result;
    }
    private boolean clearCurrent(String key,UploadBook book,ClearAction clear,MdoBrowserChannel<WebMessagePort>.Lease channelLease){
        return uploadBook(key,book.view)==book&&book.clearing==clear&&book.clearKeys.get(clear.key)==clear&&!clear.cancelled&&clear.source.scope.live()&&
            uploadOwners.get(key)==clear.source&&channel.current()==channelLease&&clear.channel==channelLease&&
            uploadMarkerCurrent(key,book.view,clear.source.scope.navigation,channelLease,clear.marker)&&chooserReady(dialogs.get(key))&&
            !views.inputBusy(key,book.view)&&views.inputClean(key,book.view)&&captureMutationReady(key,book.view);
    }
    private boolean clearCommand(String key,String id,WebView view,JSONObject request)throws Exception {
        String verb=uploadText(request,"action");UploadBook book=uploadBook(key,view);ClearAction clear=null;
        final MdoBrowserChannel<WebMessagePort>.Lease replyChannel=channel.current();
        if(Arrays.asList("status","cancel","release").contains(verb)){
            String ticket=uploadText(request,"ticket");
            if(book!=null)for(ClearAction previous:book.clearKeys.values())if(previous.ticket.equals(ticket)){clear=previous;break;}
            if(clear==null)return false;
            uploadFields(request,"action","ticket","navigation","confirmed");
            if(request.has("navigation")&&uploadNumber(request,"navigation",1,9007199254740991L)!=clear.source.scope.navigation)throw new IllegalStateException("upload_owner_changed");
            if("release".equals(verb)){
                if(!Boolean.TRUE.equals(request.opt("confirmed")))throw new IllegalArgumentException("upload_confirmation_required");
                if(!clear.done)throw new IllegalStateException("upload_clear_pending");clear.released=true;
            }else if("cancel".equals(verb))cancelClear(clear);
            reply(replyChannel,id,clearReceipt(clear,true),null);return true;
        }
        if(!Arrays.asList("clear","clear_status","clear_cancel").contains(verb))return false;
        String clearKey=uploadText(request,"upload_key");if(!clearKey.matches("[a-zA-Z0-9._:-]{1,100}"))throw new IllegalArgumentException("upload_key_invalid");
        clear=book==null?null:book.clearKeys.get(clearKey);
        if(!"clear".equals(verb)){
            uploadFields(request,"action","upload_key");
            if(clear==null){if(verb.equals("clear_cancel")){reply(replyChannel,id,new JSONObject().put("ok",true).put("cancelled",false),null);return true;}throw new IllegalStateException("upload_clear_missing");}
            if(verb.equals("clear_cancel"))cancelClear(clear);
            reply(replyChannel,id,clearReceipt(clear,true).put("cancel_requested",clear.cancelled),null);return true;
        }
        uploadFields(request,"action","upload_key","navigation","document","ref","frame_chain","timeout_ms");
        long epoch=uploadNumber(request,"navigation",1,9007199254740991L);MdoBrowserCdpFileInput target=uploadTarget(request);
        int timeout=(int)(request.has("timeout_ms")?uploadNumber(request,"timeout_ms",100,120000):10000);timeout=Math.min(timeout,15000);
        if(clear!=null){
            if(clear.source.scope.navigation!=epoch||!sameUploadTarget(clear.target,target))throw new IllegalStateException("upload_clear_conflict");
            reply(replyChannel,id,clearReceipt(clear,false),null);return true;
        }
        if(!browserUseSupport().optBoolean("available"))throw new UnsupportedOperationException("upload_clear_unsupported");
        if(uploadBusy(key,view)||views.inputBusy(key,view))throw new IllegalStateException("upload_operation_busy");
        if(book!=null){
            for(ClearAction old:book.clearKeys.values())if(old.done&&!old.clean)throw new IllegalStateException("upload_cleanup_failed");
            for(UploadAction old:book.tickets.values())if(old.coordinator!=null&&old.coordinator.done()&&old.coordinator.result()!=null&&!old.coordinator.result().cleanupConfirmed)throw new IllegalStateException("upload_cleanup_failed");
        }
        if(!views.inputClean(key,view)||!captureMutationReady(key,view))throw new IllegalStateException("upload_capture_busy_or_unclean");
        if(!cdpEnvironmentCurrent(key,view,epoch,replyChannel)||!chooserReady(dialogs.get(key)))throw new IllegalStateException("upload_owner_changed");
        MdoBrowserCdpMarker marker=existingDocumentMarker(key,view,epoch,replyChannel);if(cdpWorker.pending(marker))throw new IllegalStateException("upload_operation_busy");
        UploadEntry source=uploadOwners.get(key);
        if(source==null||source.view!=view||source.scope.navigation!=epoch||!source.scope.live()){
            if(source!=null){source.scope.cancel();cancelUploadTargets(source);}source=new UploadEntry(view,epoch,uploadData(key,view));uploadOwners.put(key,source);
        }
        if(book==null){book=new UploadBook(view,source.data);uploadBooks.put(source.data,book);}
        if(book.clearKeys.size()>=64)throw new IllegalStateException("upload_clear_limit");
        final UploadBook originalBook=book;final ClearAction operation=new ClearAction(source,target,marker,replyChannel,clearKey);
        try{
            book.clearing=operation;book.clearKeys.put(clearKey,operation);operation.registered=true;
            cdpWorker.submitExisting(marker,timeout,true,(binding,deadline)->{
                JSONObject result=target.clear(binding.frames(),deadline);return result.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8);
            },outcome->{
                operation.dispatched=outcome.effectMayHaveOccurred;operation.error=outcome.code;
                operation.clean=!Arrays.asList("cdp_file_input_cleanup_failed","cdp_worker_failed","cdp_worker_result_invalid").contains(outcome.code);
                try{
                    if(outcome.code==null){
                        JSONObject response=new JSONObject(new String(outcome.bytes,java.nio.charset.StandardCharsets.UTF_8)),value=response.optJSONObject("value");
                        MdoBrowserCdpFrameChain.Pin leaf=target.pins.chain.get(0);JSONObject element=value==null?null:value.optJSONObject("element");
                        operation.ok=Boolean.TRUE.equals(response.opt("ok"))&&leaf.frame.equals(response.opt("frame"))&&leaf.document.equals(response.opt("frame_document"))&&value!=null&&Boolean.TRUE.equals(value.opt("ok"))&&
                            target.document.equals(value.opt("document"))&&element!=null&&target.ref.equals(element.opt("ref"))&&Boolean.TRUE.equals(value.opt("cleared"))&&Boolean.TRUE.equals(value.opt("verified"))&&Boolean.TRUE.equals(value.opt("dispatched"));
                        if(!operation.ok)operation.error="cdp_file_clear_unconfirmed";
                    }
                    if(operation.cancelled){operation.ok=false;operation.error="upload_clear_cancelled";}
                    else if(!clearCurrent(key,originalBook,operation,replyChannel)){operation.ok=false;operation.error="upload_owner_changed";}
                }catch(JSONException|RuntimeException|OutOfMemoryError error){operation.ok=false;operation.error="upload_result_unconfirmed";}
                finally{operation.done=true;if(originalBook.clearing==operation)originalBook.clearing=null;}
                try{reply(replyChannel,id,clearReceipt(operation,false),null);}catch(JSONException|RuntimeException|OutOfMemoryError error){reply(replyChannel,id,null,"upload_result_unconfirmed");}
            });
        }catch(java.io.IOException|RuntimeException|OutOfMemoryError error){
            operation.done=true;operation.error=error.getMessage()!=null&&error.getMessage().matches("(?:cdp_|upload_)[a-z_]{1,80}")?error.getMessage():"upload_clear_unavailable";
            operation.registered=book.clearKeys.get(clearKey)==operation;
            if(book.clearing==operation)book.clearing=null;reply(replyChannel,id,clearReceipt(operation,false),null);
        }
        return true;
    }

    // UI routing only. Native controller decisions and snapshot reads do no
    // socket/disk work; release waits observe the original opaque receipt.
    private final Map<String,DownloadRelease> downloadReleases=new HashMap<>();
    private final Map<String,DownloadRead> downloadReads=new HashMap<>();
    private final class DownloadRead {
        final String id,identity;final NetworkEntry entry;MdoBrowserDownloadQueue.Read range;
        final MdoBrowserChannel<WebMessagePort>.Lease lease;final long born=android.os.SystemClock.elapsedRealtime();
        final Runnable watch=()->downloadReadPoll(this);
        DownloadRead(String id,String identity,NetworkEntry entry,MdoBrowserChannel<WebMessagePort>.Lease lease){
            this.id=id;this.identity=identity;this.entry=entry;this.lease=lease;
        }
    }
    private final class DownloadRelease {
        final String id;final NetworkEntry entry;MdoBrowserCdpDownloads.Release release;
        final MdoBrowserChannel<WebMessagePort>.Lease lease;final long born=android.os.SystemClock.elapsedRealtime();
        final Runnable watch=()->downloadReleasePoll(this);
        DownloadRelease(String id,NetworkEntry entry,MdoBrowserChannel<WebMessagePort>.Lease lease){
            this.id=id;this.entry=entry;this.lease=lease;
        }
    }
    private JSONArray downloadList(String key)throws JSONException {
        JSONArray result=new JSONArray();NetworkEntry entry=networkViews.get(key);
        if(entry==null||entry.downloads==null)return result;
        try{for(MdoBrowserCdpDownloads.Snapshot row:entry.downloads.list())result.put(row.json());}catch(java.io.IOException ignored){}
        return result;
    }
    private boolean downloadControlReady(String key){
        NetworkEntry entry=networkViews.get(key);return entry!=null&&entry.downloads!=null&&entry.ready&&!entry.retired&&networkCurrent(entry);
    }
    /** Page close is a request, not permission to discard original transfer
     * records or their UI replies. This observation performs no native I/O. */
    private boolean downloadPageCloseReady(String key){
        NetworkEntry entry=networkViews.get(key);if(entry==null)return true;
        if(networkCleanupFailed||!entry.retired||!entry.protocolDrained)return false;
        if(entry.downloads==null)return true;
        try{if(entry.downloads.list().length!=0||entry.downloads.retiringFiles()!=0)return false;}
        catch(java.io.IOException failure){return false;}
        for(DownloadRead operation:downloadReads.values())if(operation.entry.downloads==entry.downloads)return false;
        for(DownloadRelease operation:downloadReleases.values())if(operation.entry.downloads==entry.downloads)return false;
        return true;
    }
    private String downloadFailure(String key){
        NetworkEntry entry=networkViews.get(key);return entry==null||entry.downloads==null||entry.downloads.failure()==null?"":entry.downloads.failure();
    }
    private static long downloadInteger(JSONObject request,String key,long fallback,long minimum,long maximum)throws JSONException {
        if(!request.has(key))return fallback;Object value=request.opt(key);
        if(!(value instanceof Integer)&&!(value instanceof Long))throw new IllegalArgumentException("download_arguments_invalid");
        long number=((Number)value).longValue();if(number<minimum||number>maximum)throw new IllegalArgumentException("download_arguments_invalid");return number;
    }
    private void executeDownloads(String key,String id,WebView view,JSONObject request)throws Exception {
        NetworkEntry entry=networkViews.get(key);
        if(entry==null||entry.view!=view||entry.downloads==null||!views.owns(key,view))throw new IllegalStateException("download_owner_unavailable");
        String action=request.optString("action");
        if(action.equals("list")){reply(id,new JSONObject().put("downloads",downloadList(key)).put("error",downloadFailure(key)),null);return;}
        String identity=request.optString("download");
        if(action.equals("get")){reply(id,entry.downloads.get(identity).json(),null);return;}
        if(action.equals("accept")){
            if(!entry.ready||entry.retired||!networkCurrent(entry))throw new IllegalStateException("download_connection_unavailable");
            long maximum=downloadInteger(request,"max_bytes",33554432,1,67108864),timeout=downloadInteger(request,"timeout_ms",60000,1000,600000);
            reply(id,entry.downloads.accept(identity,maximum,(int)timeout).json(),null);return;
        }
        if(action.equals("cancel")){reply(id,entry.downloads.cancel(identity).json(),null);return;}
        if(action.equals("read")){
            long offset=downloadInteger(request,"offset",0,0,67108864),limit=downloadInteger(request,"limit",65536,1,65536);
            if(id==null||id.isEmpty()||id.length()>512||downloadReads.containsKey(id)||downloadReleases.containsKey(id)||downloadReads.size()>=4)throw new IllegalStateException("download_read_busy");
            MdoBrowserChannel<WebMessagePort>.Lease lease=channel.current();
            if(lease==null||!channel.active(lease,host.getUrl()))throw new IllegalStateException("download_owner_channel_changed");
            DownloadRead operation=new DownloadRead(id,identity,entry,lease);downloadReads.put(id,operation);
            try{operation.range=entry.downloads.read(identity,offset,(int)limit);}
            catch(Exception|Error failure){downloadReads.remove(id);throw failure;}
            downloadReadPoll(operation);return;
        }
        if(action.equals("release")){
            if(id==null||id.isEmpty()||id.length()>512||downloadReleases.containsKey(id)||downloadReads.containsKey(id)||downloadReleases.size()>=32)throw new IllegalStateException("download_release_busy");
            MdoBrowserChannel<WebMessagePort>.Lease lease=channel.current();
            if(lease==null||!channel.active(lease,host.getUrl()))throw new IllegalStateException("download_owner_channel_changed");
            // Prepare the watcher before recording an effect, so allocation
            // failure cannot lose the original release association.
            DownloadRelease operation=new DownloadRelease(id,entry,lease);downloadReleases.put(id,operation);
            try{operation.release=entry.downloads.release(identity);}
            catch(Exception|Error failure){downloadReleases.remove(id);throw failure;}
            downloadReleasePoll(operation);return;
        }
        throw new UnsupportedOperationException("download_action_unsupported");
    }
    private void downloadReadPoll(DownloadRead operation){
        if(downloadReads.get(operation.id)!=operation)return;
        networkHandler.removeCallbacks(operation.watch);
        if(stopped||channel.current()!=operation.lease||!channel.active(operation.lease,host.getUrl())){downloadReadDone(operation);return;}
        if(!downloadOwnerCurrent(operation.entry)){
            downloadReadDone(operation);reply(operation.lease,operation.id,null,"download_owner_changed");return;
        }
        byte[] bytes=null;
        try{
            if(operation.range.finished()){
                bytes=operation.range.take();long next=operation.range.offset+bytes.length;
                JSONObject result=new JSONObject().put("id",operation.identity).put("offset",operation.range.offset).put("next",next)
                    .put("bytes",bytes.length).put("size",operation.range.size).put("eof",next==operation.range.size)
                    .put("sha256",operation.range.sha256).put("base64",java.util.Base64.getEncoder().encodeToString(bytes));
                downloadReadDone(operation);reply(operation.lease,operation.id,result,null);return;
            }
            long used=android.os.SystemClock.elapsedRealtime()-operation.born;
            if(used<0||used>=5000||!networkHandler.postDelayed(operation.watch,20)){
                downloadReadDone(operation);reply(operation.lease,operation.id,null,"download_read_timeout");
            }
        }catch(java.io.IOException failure){
            String code=failure.getMessage();downloadReadDone(operation);
            reply(operation.lease,operation.id,null,code!=null&&code.matches("download_[a-z_]{1,80}")?code:"download_read_failed");
        }catch(JSONException|RuntimeException|Error failure){downloadReadDone(operation);reply(operation.lease,operation.id,null,"download_read_failed");}
        finally{if(bytes!=null)java.util.Arrays.fill(bytes,(byte)0);}
    }
    private void downloadReadDone(DownloadRead operation){
        networkHandler.removeCallbacks(operation.watch);downloadReads.remove(operation.id);if(operation.range!=null)operation.range.close();
    }
    private void downloadReleasePoll(DownloadRelease operation){
        if(downloadReleases.get(operation.id)!=operation)return;
        networkHandler.removeCallbacks(operation.watch);
        if(stopped||channel.current()!=operation.lease||!channel.active(operation.lease,host.getUrl())){downloadReleases.remove(operation.id);return;}
        if(!downloadOwnerCurrent(operation.entry)){
            downloadReleases.remove(operation.id);reply(operation.lease,operation.id,null,"download_owner_changed");return;
        }
        try{
            if(operation.release.finished()){
                downloadReleases.remove(operation.id);
                if(operation.release.released())reply(operation.lease,operation.id,new JSONObject().put("ok",true).put("released",true),null);
                else reply(operation.lease,operation.id,null,"download_release_unconfirmed");return;
            }
            long used=android.os.SystemClock.elapsedRealtime()-operation.born;
            if(used<0||used>=5000){downloadReleases.remove(operation.id);reply(operation.lease,operation.id,null,"download_release_unconfirmed");return;}
            if(!networkHandler.postDelayed(operation.watch,20)){downloadReleases.remove(operation.id);reply(operation.lease,operation.id,null,"download_release_unconfirmed");}
        }catch(JSONException|RuntimeException|Error failure){downloadReleases.remove(operation.id);reply(operation.lease,operation.id,null,"download_release_unconfirmed");}
    }
    private void cancelDownloadReleases(){
        for(DownloadRelease operation:new ArrayList<>(downloadReleases.values()))networkHandler.removeCallbacks(operation.watch);downloadReleases.clear();
        for(DownloadRead operation:new ArrayList<>(downloadReads.values()))downloadReadDone(operation);
    }
    private boolean downloadOwnerCurrent(NetworkEntry original){
        NetworkEntry current=networkViews.get(original.key);
        return current!=null&&current.view==original.view&&current.downloads==original.downloads&&views.owns(original.key,original.view);
    }

    // One retained navigation record per physical owner. The UI only submits
    // decisions/reads immutable results; the existing worker performs all I/O.
    private final class NativeNavigation {
        final String id="nv-"+java.util.UUID.randomUUID();final NetworkEntry owner;
        final NavigationRequest reservation;final int timeout;final boolean initial,control;final String action;final long born=android.os.SystemClock.elapsedRealtime();
        final Runnable watch=()->nativeNavigationPoll(this);
        MdoBrowserCdpNetworks.Navigation ticket;boolean revoked,settled,pageJoined;long revokedAt;String pageError;
        NativeNavigation(NetworkEntry owner,NavigationRequest reservation,int timeout,boolean initial,String action){this.owner=owner;this.reservation=reservation;this.timeout=timeout;this.initial=initial;this.action=action;control=!action.equals("goto");}
    }
    private boolean networkNavigate(String key,WebView view,String replyId,String target,boolean agent,long timeout)throws Exception {
        NetworkEntry owner=networkViews.get(key);if(owner==null||owner.view!=view||manual.contains(key)&&!agent)return false;
        NativeNavigation operation=beginNativeNavigation(owner,target,timeout,false);
        try{reply(owner.channelLease,replyId,new JSONObject().put("accepted",true).put("navigation_ref",operation.id),null);return true;}
        catch(Exception|Error failure){nativeNavigationRevoke(operation);throw failure;}
    }
    private NativeNavigation beginNativeNavigation(NetworkEntry owner,String target,long timeout,boolean initial)throws Exception {
        return beginNativeNavigation(owner,target,timeout,initial,"goto");
    }
    private boolean networkTraverse(String key,WebView view,String replyId,String action,boolean agent,long timeout)throws Exception {
        NetworkEntry owner=networkViews.get(key);if(owner==null||owner.view!=view||manual.contains(key)&&!agent)return false;
        NativeNavigation operation=beginNativeNavigation(owner,null,timeout,false,action);
        try{reply(owner.channelLease,replyId,new JSONObject().put("accepted",true).put("navigation_ref",operation.id),null);return true;}
        catch(Exception|Error failure){nativeNavigationRevoke(operation);throw failure;}
    }
    private NativeNavigation beginNativeNavigation(NetworkEntry owner,String target,long timeout,boolean initial,String action)throws Exception {
        String key=owner.key;WebView view=owner.view;long epoch=views.epoch(key,view);
        if(!Arrays.asList("goto","back","forward","reload").contains(action)||initial&&!action.equals("goto"))throw new IllegalArgumentException("navigation_action_invalid");
        if(!owner.ready||owner.retired||!networkCurrent(owner)||owner.marker==null)throw new IllegalStateException("network_owner_unavailable");
        if(timeout<100||timeout>600000)throw new IllegalArgumentException("cdp_navigation_arguments_invalid");
        if(navigationRequests.containsKey(key)||owner.navigation!=null&&owner.navigation.ticket!=null&&owner.navigation.ticket.result()==null)throw new IllegalStateException("navigation_pending");
        // Same-document commits do not enter documentNavigation(). Retire the
        // old action's popup preparation before invalidating its input/realm.
        retireProtocolPopupPreparation(key,view,"cdp_popup_preparation_owner_changed");
        cancelPopup(key);cancelDialog(key);cancelCapture(key);views.stop(key,view);
        if(!networkCurrent(owner)||owner.retired||manual.contains(key)||views.epoch(key,view)!=epoch)throw new IllegalStateException("network_owner_changed");
        if(!dialogReady(key,view))throw new IllegalStateException("dialog_pending_or_unclean");
        NavigationRequest reservation=new NavigationRequest(view,views.epoch(key,view),Boolean.TRUE.equals(loading.get(key)));
        NativeNavigation operation=new NativeNavigation(owner,reservation,(int)timeout,initial,action);
        navigationRequests.put(key,reservation);owner.navigation=operation;
        try{
            loading.put(key,true);owner.profile.block(MdoBrowserDocument.NAVIGATION,true);
            if(!networkCurrent(owner)||owner.retired||manual.contains(key)||views.epoch(key,view)!=epoch)throw new IllegalStateException("network_owner_changed");
            operation.ticket=owner.popup!=null?cdpWorker.navigatePopup(owner.marker,owner.popup.handoff,owner.popup.sourceProof,target,operation.control?action:null,(int)timeout):
                operation.control?cdpWorker.traverseNetwork(owner.marker.lease(),action,(int)timeout):cdpWorker.navigateNetwork(owner.marker.lease(),target,(int)timeout);
            if(!networkHandler.postDelayed(operation.watch,20)){
                operation.ticket.cancel();networkRetire(owner,"cdp_navigation_scheduler_failed");throw new IllegalStateException("cdp_navigation_scheduler_failed");
            }
            return operation;
        }catch(Exception|Error failure){
            if(operation.ticket==null){
                if(views.owns(key,view)&&views.epoch(key,view)==reservation.epoch&&documents.get(key)==owner.profile)cancelNavigationRequest(key,view);
                else navigationRequests.remove(key,reservation);
                if(owner.navigation==operation)owner.navigation=null;
            }
            else{operation.ticket.cancel();nativeNavigationRevoke(operation);}
            throw failure;
        }
    }
    private boolean networkTargetMayHaveOccurred(NetworkEntry owner){
        NativeNavigation operation=owner.navigation;return operation!=null&&operation.initial&&operation.ticket!=null&&operation.ticket.dispatched();
    }
    private boolean networkInitialNavigationPending(String key){
        NetworkEntry owner=networkViews.get(key);NativeNavigation operation=owner==null?null:owner.navigation;
        if(operation==null||operation.owner!=owner||!operation.initial||operation.revoked||operation.pageError!=null||operation.ticket==null)return false;
        MdoBrowserCdpSession.NavigationResult result=operation.ticket.result();
        if(result!=null&&result.acknowledged&&result.ok&&operation.ticket.pageCommitted()&&operation.ticket.pageCurrent()&&navigationRequests.get(key)!=operation.reservation)operation.pageJoined=true;
        if(operation.pageJoined)return false;
        return result==null||result.effectMayHaveOccurred&&!result.acknowledged||result.ok&&
            (!(result.downloadKnown&&result.download)&&(!operation.ticket.pageCommitted()||!operation.ticket.pageCurrent()||navigationRequests.get(key)==operation.reservation));
    }
    private boolean nativeNavigationScope(NativeNavigation operation){
        NetworkEntry owner=operation.owner;
        return networkViews.get(owner.key)==owner&&owner.navigation==operation&&networkCurrent(owner)&&!manual.contains(owner.key)&&!owner.retired&&!owner.draining;
    }
    // Resolve on the UI thread; only the native ticket crosses to the worker.
    private MdoBrowserCdpNetworks.Navigation navigationReadProof(String key,WebView view,String reference)throws java.io.IOException {
        if(reference==null)return null;
        NetworkEntry owner=networkViews.get(key);NativeNavigation operation=owner==null?null:owner.navigation;
        if(owner==null||owner.view!=view||operation==null||!operation.id.equals(reference)||!nativeNavigationScope(operation)||operation.revoked||operation.pageError!=null)
            throw new java.io.IOException("cdp_navigation_cancelled");
        if(operation.ticket!=null&&operation.ticket.sameDocument()&&views.epoch(key,view)!=operation.reservation.epoch)
            throw new java.io.IOException("cdp_navigation_document_changed");
        if(operation.ticket==null||!operation.ticket.pageCurrent()||navigationRequests.containsKey(key))throw new java.io.IOException("cdp_navigation_document_unproven");
        return operation.ticket;
    }
    private void nativeNavigationRevoke(NativeNavigation operation){
        if(!operation.revoked){operation.revoked=true;operation.revokedAt=android.os.SystemClock.elapsedRealtime();}
        if(operation.ticket!=null)operation.ticket.cancel();
    }
    private void nativeNavigationRetire(NetworkEntry owner){if(owner.navigation!=null&&!owner.navigation.settled)nativeNavigationRevoke(owner.navigation);}
    private void nativeNavigationPageStarted(String key,WebView view){
        NetworkEntry owner=networkViews.get(key);NativeNavigation operation=owner==null||owner.view!=view?null:owner.navigation;
        if(operation!=null&&!operation.settled&&(operation.ticket==null||!operation.ticket.dispatched()))nativeNavigationRevoke(operation);
    }
    private void nativeNavigationRestore(NativeNavigation operation,MdoBrowserCdpSession.NavigationResult result){
        NetworkEntry owner=operation.owner;NavigationRequest original=operation.reservation;
        if(navigationRequests.get(owner.key)!=original||!views.owns(owner.key,owner.view)||views.epoch(owner.key,owner.view)!=original.epoch||documents.get(owner.key)!=owner.profile)return;
        // Exact terminal proof may explain the unchanged original document.
        // Unknown dispatched effects never release its input admission fence.
        // A target opened from the native warmup is cross-document. Even a
        // non-download ACK without loader metadata cannot prove that transition.
        boolean committedControl=(operation.control||owner.popup!=null)&&!operation.revoked&&result.acknowledged&&result.ok&&operation.ticket.pageCommitted()&&operation.ticket.pageCurrent();
        if(committedControl){
            // A native commit can precede WebView callbacks, including BFCache.
            // Rotate document/input references before releasing realm admission;
            // shared observation still proves DOM readiness separately.
            if(!operation.ticket.sameDocument()){
                cdpViews.invalidate(owner.key,owner.view,original.epoch,"input_cancelled");
                documentNavigation(owner.key,owner.view);
                if(!nativeNavigationScope(operation)||operation.revoked||!operation.ticket.pageCurrent()||views.epoch(owner.key,owner.view)!=original.epoch||documents.get(owner.key)!=owner.profile){nativeNavigationRevoke(operation);return;}
                if(!views.navigation(owner.key,owner.view)){nativeNavigationRevoke(operation);return;}
                if(!nativeNavigationScope(operation)||!operation.ticket.pageCurrent()||documents.get(owner.key)!=owner.profile){nativeNavigationRevoke(operation);return;}
                long committedEpoch=views.epoch(owner.key,owner.view);owner.profile.committed();
                if(!nativeNavigationScope(operation)||!operation.ticket.pageCurrent()||views.epoch(owner.key,owner.view)!=committedEpoch||documents.get(owner.key)!=owner.profile){nativeNavigationRevoke(operation);return;}
                loading.put(owner.key,false);errors.remove(owner.key);errorCodes.remove(owner.key);return;
            }
            cancelNavigationRequest(owner.key,owner.view);errors.remove(owner.key);errorCodes.remove(owner.key);return;
        }
        boolean sameDocument=!operation.initial&&!operation.control&&result.acknowledged&&result.ok&&result.downloadKnown&&!result.download&&!result.loaderPresent;
        if(!result.effectMayHaveOccurred||result.acknowledged&&(!result.ok||result.downloadKnown&&result.download)||
           sameDocument&&operation.ticket.pageCommitted()&&operation.ticket.pageCurrent()){
            cancelNavigationRequest(owner.key,owner.view);
            if(owner.popup!=null&&owner.popup.page!=null)owner.popup.page.navigationRejected(operation);
            if(!operation.revoked&&result.acknowledged&&!result.ok)errors.put(owner.key,result.nativeError==null?result.code:result.nativeError);
        }
    }
    private void nativeNavigationPoll(NativeNavigation operation){
        networkHandler.removeCallbacks(operation.watch);if(operation.settled)return;
        if(!nativeNavigationScope(operation))nativeNavigationRevoke(operation);
        MdoBrowserCdpSession.NavigationResult result=operation.ticket==null?null:operation.ticket.result();
        long now=android.os.SystemClock.elapsedRealtime(),age=now-operation.born;
        if(result!=null){
            if(operation.control&&result.acknowledged&&result.ok&&(result.downloadKnown||result.download||result.loaderPresent||operation.action.equals("reload")&&operation.ticket.sameDocument())){
                operation.pageError="cdp_navigation_control_reply_invalid";nativeNavigationRevoke(operation);operation.settled=true;return;
            }
            try{nativeNavigationRestore(operation,result);}
            catch(RuntimeException|Error failure){operation.pageError="cdp_navigation_ui_join_failed";nativeNavigationRevoke(operation);networkRetire(operation.owner,operation.pageError);operation.settled=true;return;}
            boolean sameDocument=operation.control?operation.ticket.sameDocument():!operation.initial&&result.downloadKnown&&!result.download&&!result.loaderPresent;
            boolean needsCommit=result.acknowledged&&result.ok&&!(result.downloadKnown&&result.download)&&(operation.control||result.loaderPresent||operation.initial||sameDocument);
            if(needsCommit&&!operation.revoked){
                if(sameDocument&&views.epoch(operation.owner.key,operation.owner.view)!=operation.reservation.epoch)operation.pageError="cdp_navigation_commit_revoked";
                else if(!operation.control&&!result.loaderPresent&&!sameDocument)operation.pageError="cdp_navigation_commit_unproven";
                else if(!operation.pageJoined&&operation.ticket.pageCommitted()&&!operation.ticket.pageCurrent())operation.pageError="cdp_navigation_commit_revoked";
                else if(!operation.pageJoined&&(!operation.ticket.pageCommitted()||navigationRequests.get(operation.owner.key)==operation.reservation)){
                    if(age<0||age>=operation.timeout){operation.pageError="cdp_navigation_commit_timeout";networkRetire(operation.owner,operation.pageError);}
                    else{if(!networkHandler.postDelayed(operation.watch,20)){operation.pageError="cdp_navigation_commit_timeout";nativeNavigationRevoke(operation);networkRetire(operation.owner,"cdp_navigation_scheduler_failed");operation.settled=true;}return;}
                }else operation.pageJoined=true;
            }
            if(needsCommit&&!operation.revoked&&operation.pageError==null&&operation.owner.popup!=null){
                PopupPage page=operation.owner.popup.page;
                if(page==null){operation.pageError="cdp_popup_page_document_unavailable";}
                else if(!page.joinNavigation(operation)){
                    if(operation.pageError!=null){operation.settled=true;return;}
                    if(age<0||age>=operation.timeout){operation.pageError="cdp_navigation_document_timeout";page.restoreFailed(page.marker);operation.settled=true;return;}
                    if(!networkHandler.postDelayed(operation.watch,20)){operation.pageError="cdp_navigation_scheduler_failed";nativeNavigationRevoke(operation);operation.settled=true;}return;
                }
            }
            operation.settled=true;return;
        }
        if(!operation.revoked&&(age<0||age>=operation.timeout)){nativeNavigationRevoke(operation);networkRetire(operation.owner,"cdp_navigation_timeout");}
        if(operation.revoked&&(now-operation.revokedAt<0||now-operation.revokedAt>=5500)){operation.settled=true;return;}
        if(!networkHandler.postDelayed(operation.watch,20)){nativeNavigationRevoke(operation);networkRetire(operation.owner,"cdp_navigation_scheduler_failed");operation.settled=true;}
    }
    private JSONObject networkNavigationState(String key)throws JSONException {
        NetworkEntry owner=networkViews.get(key);NativeNavigation operation=owner==null?null:owner.navigation;
        if(operation==null||operation.ticket==null)return null;
        MdoBrowserCdpSession.NavigationResult result=operation.ticket.result();
        JSONObject value=new JSONObject().put("id",operation.id).put("phase",result==null?(operation.ticket.dispatched()?"sent":"queued"):result.ok?"acknowledged":"failed")
            .put("action",operation.action).put("receipt_kind",operation.control?"control":"navigate").put("control_supported",true)
            .put("request_association_supported",true).put("request_ref",operation.ticket.requestReference()==null?JSONObject.NULL:operation.ticket.requestReference())
            .put("request_association_error",operation.ticket.requestAssociationError()==null?JSONObject.NULL:operation.ticket.requestAssociationError())
            .put("scope_revoked",operation.revoked||operation.owner!=owner).put("dispatched",operation.ticket.dispatched()).put("initial",operation.initial)
            .put("page_commit_supported",true).put("same_document_supported",true).put("same_document",operation.ticket.sameDocument()).put("page_committed",operation.ticket.pageCommitted()).put("page_current",operation.ticket.pageCurrent())
            .put("page_ready",operation.settled&&!operation.revoked&&operation.pageError==null&&operation.ticket.pageCurrent()&&
                (owner.popup==null||owner.popup.page!=null&&owner.popup.page.navigating==operation&&owner.popup.page.documentReadable()))
            .put("page_document",operation.ticket.pageDocument()==null?JSONObject.NULL:operation.ticket.pageDocument()).put("page_error",operation.pageError==null?JSONObject.NULL:operation.pageError);
        if(result!=null)value.put("acknowledged",result.acknowledged).put("ok",result.ok).put("download_known",result.downloadKnown).put("download",result.download)
            .put("loader_present",result.loaderPresent).put("code",result.code==null?JSONObject.NULL:result.code).put("native_error",result.nativeError==null?JSONObject.NULL:result.nativeError);
        return value;
    }
    private void executeNavigationCancel(String key,String id,WebView view,JSONObject request)throws JSONException {
        NetworkEntry owner=networkViews.get(key);NativeNavigation operation=owner==null?null:owner.navigation;
        boolean matches=operation!=null&&operation.ticket!=null&&owner.view==view&&views.owns(key,view)&&operation.id.equals(request.optString("navigation_ref"))&&
            channel.current()==operation.owner.channelLease&&channel.active(operation.owner.channelLease,host.getUrl());
        if(matches)nativeNavigationRevoke(operation);
        reply(id,new JSONObject().put("cancelled",matches).put("dispatched",matches&&operation.ticket.dispatched()),null);
    }

    /** Private coordinator seam. A configured child is not yet published,
     * navigated, granted a document world or sent to its native transport. */
    private MdoBrowserCdpPopupNative.Created constructProtocolPopup(MdoBrowserCdpPopupNative source,
            MdoBrowserCdpPopupWorker.Allocation allocation,String key)throws java.io.IOException {
        nativeOwnerAdmission();
        if(source==null)throw new java.io.IOException("cdp_popup_native_source_invalid");
        final WebView opener=source.opener();String found=null;
        for(Map.Entry<String,MdoBrowserDocument> item:documents.entrySet())if(views.owns(item.getKey(),opener)&&item.getValue().profileAssigned()){found=item.getKey();break;}
        if(found==null)throw new java.io.IOException("browser_profile_source_invalid");
        final String parentKey=found;final MdoBrowserDocument parent=documents.get(parentKey);final MdoBrowserChannel<WebMessagePort>.Lease originalChannel=channel.current();
        MdoBrowserCdpPopupNative.Created created=source.construct(allocation,views,key,(exactKey,exactView)->{
            WebView current=views.get(exactKey);
            if(current!=null&&current!=exactView)throw new IllegalStateException("popup_binding_mismatch");
            NativeDestruction row=retireNativeDocument(exactKey,exactView);
            if(nativeViewCleanupFailed||row!=null&&row.failed)throw new IllegalStateException("native_view_cleanup_failed");
        });
        try{
            WebView view=created.view();
            MdoBrowserDocument document=prepareDocument(key,view);
            java.util.function.BooleanSupplier admitted=()->!stopped&&documents.get(parentKey)==parent&&parent.profileAssigned()&&views.owns(parentKey,opener)&&source.current()&&channel.current()==originalChannel&&channel.active(originalChannel,host.getUrl());
            document.initializeInheritedProfile(WebView.getWebViewClassLoader(),parent,opener,admitted);
            if(!admitted.getAsBoolean()||documents.get(key)!=document||created.view()!=view)throw new IllegalStateException("popup_binding_mismatch");
            documentFailures.put(key,"popup_world_unqualified");
            configureBrowserView(key,view);
            if(!admitted.getAsBoolean()||documents.get(key)!=document||created.view()!=view)throw new IllegalStateException("popup_binding_mismatch");
            return created;
        }catch(MdoBrowserWorld.Failure failure){
            try{created.close();}catch(java.io.IOException cleanup){java.io.IOException combined=new java.io.IOException("native_view_cleanup_failed",failure);combined.addSuppressed(cleanup);throw combined;}
            throw new java.io.IOException(failure.code);
        }catch(RuntimeException|Error failure){
            try{created.close();}
            catch(java.io.IOException cleanup){
                IllegalStateException combined=new IllegalStateException("native_view_cleanup_failed",failure);
                combined.addSuppressed(cleanup);throw combined;
            }
            throw failure;
        }
    }
    /** Native ownership handoff only. The complete coordinator must separately
     * transfer the protected original protocol session and bootstrap a world. */
    private MdoBrowserCdpPopupNative.Promotion promoteProtocolPopup(String key,MdoBrowserCdpPopupNative source,
            MdoBrowserCdpPopupNative.Created original,MdoBrowserCdpPopupNative.Child proof)throws java.io.IOException {
        return transferProtocolPopup(key,source,original,proof,false);
    }
    /** Retain the same protected paused Browser attachment independently of
     * the opener. No debugger resume, request continuation or world bootstrap. */
    private MdoBrowserCdpPopupNative.Promotion handoffProtocolPopup(String key,MdoBrowserCdpPopupNative source,
            MdoBrowserCdpPopupNative.Created original,MdoBrowserCdpPopupNative.Child proof)throws java.io.IOException {
        return transferProtocolPopup(key,source,original,proof,true);
    }
    /** Queue fixed debugger/request flow only for the committed native owner.
     * This neither publishes a tab nor grants document/world/body access. */
    private void activateProtocolPopup(MdoBrowserCdpPopupNative.Promotion owner,MdoBrowserCdpPopupBroker.Handoff handoff)throws java.io.IOException {
        nativeOwnerAdmission();if(handoff==null||!handoff.owns(owner)||!owner.current())throw new java.io.IOException("cdp_popup_activation_unavailable");handoff.activate();
    }
    private MdoBrowserCdpMarker protocolDocumentMarker(String key,MdoBrowserCdpPopupNative.Promotion owner,MdoBrowserCdpPopupBroker.Handoff handoff){
        nativeOwnerAdmission();if(owner==null||handoff==null||!handoff.owns(owner)||!handoff.current())throw new IllegalStateException("cdp_popup_document_owner_invalid");
        final WebView exact=owner.view();final long epoch=views.epoch(key,exact);if(!views.owns(key,exact)||epoch<=0)throw new IllegalStateException("cdp_popup_document_owner_invalid");
        return cdpViews.marker(key,exact,epoch,handoff,()->!stopped&&!browserPaused&&!pageClosed.contains(key)&&views.owns(key,exact)&&views.epoch(key,exact)==epoch&&handoff.current());
    }
    private MdoBrowserCdpView protocolSourceAdapter(String key,MdoBrowserCdpPopupNative.Promotion owner,MdoBrowserCdpPopupBroker.Handoff handoff){
        nativeOwnerAdmission();if(owner==null||handoff==null||!handoff.owns(owner)||!handoff.current())throw new IllegalStateException("cdp_popup_source_owner_invalid");
        final WebView exact=owner.view();if(!views.owns(key,exact))throw new IllegalStateException("cdp_popup_source_owner_invalid");
        return MdoBrowserCdpView.network(exact,v->v==exact&&!stopped&&!browserPaused&&!pageClosed.contains(key)&&views.owns(key,exact)&&handoff.current(),new MdoBrowserCdpMarker.DocumentQuota());
    }
    private MdoBrowserCdpPopupNative.Promotion transferProtocolPopup(String key,MdoBrowserCdpPopupNative source,
            MdoBrowserCdpPopupNative.Created original,MdoBrowserCdpPopupNative.Child proof,boolean protectedAttachment)throws java.io.IOException {
        nativeOwnerAdmission();
        if(source==null||original==null)throw new java.io.IOException("cdp_popup_native_promotion_unavailable");
        WebView exact;try{exact=original.view();}catch(IllegalStateException stale){throw new java.io.IOException("cdp_popup_native_promotion_unavailable");}
        if(!views.owns(key,exact)||popupLinks.containsKey(key))throw new java.io.IOException("cdp_popup_native_promotion_unavailable");
        MdoBrowserCdpPopupNative.Promotion promoted=protectedAttachment?source.handoff(original,proof):source.promote(original,proof);
        try{if(promoted.view()!=exact||!views.owns(key,exact))throw new IllegalStateException("popup_binding_mismatch");return promoted;}
        catch(RuntimeException|Error failure){promoted.close();throw failure;}
    }
    /** Common native policy/listeners, with no navigation or UI publication. */
    private static final class InitialScaleCallback extends WebView.VisualStateCallback {
        private final MdoBrowserLifecycle.InitialScale<WebView> original;
        InitialScaleCallback(MdoBrowserLifecycle.InitialScale<WebView> original){this.original=original;}
        @Override public void onComplete(long id){
            if(id!=original.epoch)original.cancel();
            MdoBrowserLifecycle.InitialScale<WebView> next=original.complete(WebView::getScale);
            if(next!=null)postInitialScale(next.currentView(),next);
        }
    }
    static void postInitialScale(WebView view,MdoBrowserLifecycle.InitialScale<WebView> original){
        if(original==null||!original.submit(view))return;
        try{view.postVisualStateCallback(original.epoch,new InitialScaleCallback(original));}
        catch(RuntimeException failure){original.cancel();}
    }
    private void configureBrowserView(String key,WebView web){
        web.getSettings().setJavaScriptEnabled(true);web.getSettings().setDomStorageEnabled(true);
        web.getSettings().setAllowFileAccess(false);web.getSettings().setAllowContentAccess(false);
        web.getSettings().setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        web.getSettings().setSupportMultipleWindows(true);
        web.getSettings().setJavaScriptCanOpenWindowsAutomatically(true);
        bindDialogs(key,web);
        web.setWebViewClient(new WebViewClient(){
            @Override public void onScaleChanged(WebView view,float oldScale,float newScale){if(views.owns(key,view))cancelCapture(key);views.scaleChanged(key,view,newScale);}
            @Override public void onPageStarted(WebView view,String address,Bitmap icon){if(!views.owns(key,view))return;documentNavigation(key,view);if(!views.navigation(key,view))return;loading.put(key,true);errors.remove(key);errorCodes.remove(key);locations.put(key,address);networkNavigation(key,view,address);}
            @Override public void onPageFinished(WebView view,String address){if(views.owns(key,view)){documentLoaded(key,view);if(!networkLoaded(key,view,address))loading.put(key,false);postInitialScale(view,views.initialScale(key,view));}}
            @Override public void onReceivedError(WebView view,WebResourceRequest navigation,WebResourceError error){
                if(views.owns(key,view)&&navigation.isForMainFrame()){cancelNavigationRequest(key,view);loading.put(key,false);errors.put(key,error.getDescription().toString());errorCodes.put(key,error.getErrorCode());}
            }
            @Override public void onReceivedSslError(WebView view,SslErrorHandler handler,android.net.http.SslError error){handler.cancel();if(!views.owns(key,view))return;cancelNavigationRequest(key,view);loading.put(key,false);errors.put(key,"TLS certificate validation failed");errorCodes.put(key,error.getPrimaryError());}
            @Override public void onReceivedHttpError(WebView view,WebResourceRequest navigation,WebResourceResponse response){
                if(views.owns(key,view)&&navigation.isForMainFrame())errorCodes.put(key,response.getStatusCode());
            }
            @Override public boolean shouldOverrideUrlLoading(WebView view,WebResourceRequest navigation){
                String address=navigation.getUrl().toString();if("about:blank".equals(address))return false;
                try{url(address);return false;}catch(IllegalArgumentException rejected){return true;}
            }
            @Override public boolean onRenderProcessGone(WebView view,RenderProcessGoneDetail detail){
                if(!views.owns(key,view))return true;manual.remove(key);loading.put(key,false);errors.put(key,"Browser process stopped; reload to restore the page");crashed.add(key);closeAndDestroyDocument(key,view);return true;
            }
        });
    }

    private JSONObject browserUseSupport;
    private JSONObject browserUseSupport() throws JSONException {
        if(browserUseSupport!=null)return browserUseSupport;
        boolean isolated=false,profiles=false;String reason="provider_loader_unsupported";
        if(android.os.Build.VERSION.SDK_INT>=28)try{
            MdoBrowserWorld.Features features=MdoBrowserWorld.inspect(WebView.getWebViewClassLoader());
            isolated=features.isolatedWorld;profiles=features.profiles;
            reason=isolated?"android_adapter_incomplete":"isolated_world_unsupported";
        }catch(MdoBrowserWorld.Failure failure){reason="world_provider_unavailable";}
        // Feature support is not product readiness. Do not enable automation
        // until native input/capture/navigation/transfer contracts are integrated.
        browserUseSupport=new JSONObject().put("available",false).put("reason",reason)
            .put("isolated_world_feature",isolated).put("profile_feature",profiles);
        return browserUseSupport;
    }
    JSONObject capabilities() throws JSONException {
        return new JSONObject().put("native",true).put("snapshot",true).put("screenshot",true)
            .put("network_cleanup_failed",networkCleanupFailed).put("network_retiring",networkRetiring())
            .put("download_retiring",downloadRetiring())
            .put("native_view_cleanup_failed",nativeViewCleanupFailed||!MdoBrowserCdpPopupNative.creationConfirmed()).put("native_view_retiring",nativeRetiringViews.size())
            .put("native_popup_owners",MdoBrowserCdpPopupNative.retainedUiOwners())
            .put("native_popup_retirements",MdoBrowserCdpPopupNative.pendingRetirements())
            .put("native_promoted_owners",MdoBrowserCdpPopupNative.promotedUiOwners())
            .put("input",true).put("profile_isolation",false).put("browser_use",browserUseSupport());
    }
    void automationAdmission(JSONObject request) throws JSONException {
        String command=request.optString("command");
        boolean agent=request.optBoolean("agent")&&!Arrays.asList("capabilities","state","bounds","stop","close","takeover","release","capture_cancel").contains(command);
        if(agent||"create".equals(command)&&(request.optBoolean("automation")||request.optBoolean("intercept_downloads")))
            throw new UnsupportedOperationException(browserUseSupport().getString("reason"));
    }

    private static final class PopupLink {
        final WebView view;final String opener,id;boolean ready;
        PopupLink(WebView view,String opener,String id){this.view=view;this.opener=opener;this.id=id;}
    }
    private final Map<String,MdoBrowserPopups<WebView>> windows=new HashMap<>();
    private final Map<String,PopupLink> popupLinks=new HashMap<>();
    private final Set<String> pageClosed=new HashSet<>();
    private final MdoBrowserPopups.Budget popupBudget=new MdoBrowserPopups.Budget(8);
    private MdoBrowserWindowTarget.Windows windowCallbacks(String key,WebView view){
        if(windows.containsKey(key))throw new IllegalStateException("Popup view identity is already bound");
        final android.os.Handler handler=new android.os.Handler(android.os.Looper.getMainLooper());
        final MdoBrowserPopups<WebView> core=new MdoBrowserPopups<>(view,new MdoBrowserPopups.Scheduler(){
            public long now(){return android.os.SystemClock.uptimeMillis();}
            public void at(Runnable task,long uptime){if(!handler.postAtTime(task,uptime))throw new IllegalStateException("Popup scheduler stopped");}
            public void remove(Runnable task){handler.removeCallbacks(task);}
        },popupBudget,()->!stopped&&!browserPaused&&!pageClosed.contains(key)&&views.owns(key,view)&&view.isShown()&&
            channel.current()!=null&&channel.active(channel.current(),host.getUrl()),()->views.epoch(key,view),blocked->{
                if(!views.owns(key,view))return;
                MdoBrowserPopups<WebView> binding=windows.get(key);
                MdoBrowserDocument document=documents.get(key);if(document!=null)document.block(MdoBrowserDocument.POPUP,blocked||binding!=null&&!binding.clean());
                if(blocked){cancelCapture(key);if(views.owns(key,view))views.input(key,view).invalidate("popup_pending");}
            });
        windows.put(key,core);
        return new MdoBrowserWindowTarget.Windows(){
            public boolean create(WebView source,boolean userGesture,MdoBrowserPopups.Transport<WebView> transport){
                if(source!=view||!views.owns(key,view))return false;
                boolean accepted=core.open(transport,userGesture);
                if(accepted)notifyPopup(key);
                return accepted;
            }
            public void close(WebView source){
                if(source!=view||!views.owns(key,view))return;
                pageClosed.add(key);
                try{networkRetire(key,view,"network_page_closed");}catch(RuntimeException|Error failure){networkCleanupFailed=true;}
                core.close();cancelDialog(key);cancelCapture(key);views.stop(key,view);
                MdoBrowserDocument document=documents.get(key);if(document!=null)document.block(MdoBrowserDocument.POPUP,true);
                view.stopLoading();views.visibility(key,view,false);view.setVisibility(View.INVISIBLE);notifyPopup(key);
            }
        };
    }
    private void notifyPopup(String key){
        if(stopped||port==null||!channel.owns(channel.current(),port,host.getUrl()))return;
        try{port.postMessage(new WebMessage(new JSONObject().put("channel","mdo-browser-popup").put("browser",key).toString()));}
        catch(RuntimeException|JSONException|OutOfMemoryError ignored){/* The trusted manager also scans retained native state. */}
    }
    private void cancelPopup(String key){MdoBrowserPopups<WebView> core=windows.get(key);if(core!=null)core.cancel();}
    private void cancelPopups(){for(MdoBrowserPopups<WebView> core:new ArrayList<>(windows.values()))core.cancel();}
    private JSONObject popupState(String key,JSONObject state)throws JSONException {
        MdoBrowserPopups<WebView> core=windows.get(key);PopupLink link=popupLinks.get(key);boolean requested=pageClosed.contains(key);
        state.put("popup_capability",core!=null&&!pageClosed.contains(key)).put("popup_pending",core!=null&&core.busy())
            .put("popup_preparation_required",true).put("popup_preparation_capability",true)
            .put("popup_preparation",popupPreparationState(key))
            .put("popup_cleanup_failed",core!=null&&!core.clean()).put("popup_budget_used",popupBudget.used())
            .put("popup_budget_limit",popupBudget.limit).put("close_requested",requested)
            .put("closed_by_page",requested&&downloadPageCloseReady(key));
        if(link!=null)state.put("opener",link.opener).put("popup",link.id).put("popup_attached",link.ready);
        return state;
    }
    private void executePopups(String key,String id,JSONObject request)throws JSONException {
        MdoBrowserPopups<WebView> core=windows.get(key);if(core==null)throw new IllegalStateException("popup_unavailable");
        if("list".equals(request.optString("action"))){
            JSONArray rows=new JSONArray();MdoBrowserPopups.Snapshot pending=core.snapshot();
            // WebChromeClient supplies no trustworthy URL/frame. The placeholder
            // is admission metadata only; create must transfer original contents.
            if(pending!=null)rows.put(new JSONObject().put("id",pending.id).put("url","about:blank").put("url_known",false)
                .put("user_initiated",pending.userInitiated).put("navigation",pending.epoch));
            reply(id,new JSONObject().put("popups",rows),null);return;
        }
        if(!"dismiss".equals(request.optString("action")))throw new IllegalArgumentException("popup_action_invalid");
        reply(id,new JSONObject().put("dismissed",core.dismiss(request.optString("popup"))),null);
    }
    private void request(JSONObject request) throws Exception {
        String id=request.optString("id"),command=request.optString("command"),key=request.optString("browser");
        if(!"mdo-browser".equals(request.optString("channel"))||id.length()!=36)return;
        try {
            if("capabilities".equals(command)) {reply(id,capabilities(),null);return;}
            if(key.isEmpty()||key.length()>100)throw new IllegalArgumentException("Invalid browser identity");
            automationAdmission(request);
            WebView web=views.get(key);
            if("capture_cancel".equals(command)){executeCaptureCancel(key,id,request);return;}
            if("create".equals(command)) {
                final MdoBrowserChannel<WebMessagePort>.Lease creatingChannel=channel.current();
                final boolean networkRequired=request.optBoolean("automation")||request.optBoolean("intercept_downloads");
                if(web==null&&networkRequired&&!request.optString("popup").isEmpty()){
                    executeProtocolPopupCreate(key,id,request);return;
                }
                if(web==null)nativeCreationAdmission();else nativeOwnerAdmission();
                if(web!=null){
                    if(!views.owns(key,web))throw new IllegalStateException("native_view_owner_changed");
                    if(protocolPopupCreatePending(key))throw new IllegalStateException("popup_transport_unconfirmed");
                    networkAttach(key,web,networkRequired);
                    PopupLink link=popupLinks.get(key);
                    if(request.has("popup")&&(link==null||link.view!=web||!link.id.equals(request.optString("popup"))||!link.opener.equals(request.optString("opener"))))
                        throw new IllegalStateException("popup_binding_mismatch");
                    if(link!=null&&!link.ready)throw new IllegalStateException("popup_transport_unconfirmed");
                    bounds(web,request);reply(id,new JSONObject().put("attached",true),null);return;
                }
                if(!MdoBrowserCdpPopupNative.creationConfirmed())throw new IllegalStateException("native_popup_creation_unconfirmed");
                final String popupId=request.optString("popup"),opener=request.optString("opener");
                final MdoBrowserPopups<WebView> popupSource=popupId.isEmpty()?null:windows.get(opener);
                if(!popupId.isEmpty()){
                    MdoBrowserPopups.Snapshot pending=popupSource==null?null:popupSource.snapshot();
                    if(opener.equals(key)||views.get(opener)==null||pending==null||!pending.id.equals(popupId))throw new IllegalStateException("popup_request_unavailable");
                }else if(!opener.isEmpty())throw new IllegalArgumentException("popup_binding_mismatch");
                views.checkNew(key);
                String target=popupSource==null?request.optString("url","about:blank"):"about:blank";if(!"about:blank".equals(target))target=url(target);
                web=new WebView(activity);final WebView createdView=web;
                try{
                    final WebView exactOpener=popupSource==null?null:views.get(opener);
                    final MdoBrowserLifecycle.Fresh<WebView> fresh=popupSource==null?null:views.putPopup(key,exactOpener,web);
                    if(fresh==null)views.put(key,web);
                    if(popupSource==null)bindDocument(key,web,networkRequired,creatingChannel);
                    else documentFailures.put(key,"popup_world_unqualified");
                    if(!views.owns(key,web)||channel.current()!=creatingChannel)throw new IllegalStateException("native_view_owner_changed");
                    configureBrowserView(key,web);
                    if(!views.owns(key,web)||channel.current()!=creatingChannel)throw new IllegalStateException("native_view_owner_changed");
                    crashed.remove(key);pageClosed.remove(key);locations.put(key,target);root.addView(web);bounds(web,request);
                    if(!views.owns(key,web)||channel.current()!=creatingChannel)throw new IllegalStateException("native_view_owner_changed");loading.put(key,true);
                    if(popupSource!=null){
                        final WebView freshChild=fresh.claim(exactOpener);
                        final PopupLink link=new PopupLink(web,opener,popupId);popupLinks.put(key,link);
                        final MdoBrowserChannel<WebMessagePort>.Lease lease=channel.current();
                        popupSource.adopt(popupId,freshChild,(ok,code,mayHaveOccurred)->{
                            if(!views.owns(key,createdView)||popupLinks.get(key)!=link){reply(lease,id,null,"popup_binding_lost");return;}
                            if(!ok){pageClosed.add(key);createdView.setVisibility(View.INVISIBLE);notifyPopup(key);reply(lease,id,null,code);return;}
                            link.ready=true;
                            try{reply(lease,id,new JSONObject().put("created",true).put("popup_attached",true),null);}
                            catch(JSONException error){reply(lease,id,null,"popup_result_invalid");}
                        });
                    }else if(networkRequired){networkBegin(key,web,id,target);}
                    else{web.loadUrl(target);reply(id,new JSONObject().put("created",true),null);}
                }catch(RuntimeException|JSONException|OutOfMemoryError failure){
                    // Once a transport may have been sent, preserve its exact
                    // child until consumption/explicit close. Never reload a URL.
                    if(!popupLinks.containsKey(key)){
                        try{if(!closeAndDestroyDocument(key,createdView))throw new IllegalStateException("native_view_cleanup_failed");}
                        catch(RuntimeException cleanup){
                            IllegalStateException combined=new IllegalStateException("native_view_cleanup_failed",failure);
                            combined.addSuppressed(cleanup);throw combined;
                        }
                    }
                    throw failure;
                }
                return;
            }
            if("close".equals(command)) {executeBrowserClose(key,id);return;}
            if("state".equals(command)&&crashed.contains(key)){reply(id,new JSONObject().put("url",locations.getOrDefault(key,"about:blank")).put("loading",false).put("platform","android-webview").put("process_failed",true).put("error",errors.get(key)),null);return;}
            if(web==null)throw new IllegalStateException("Browser is not ready");
            if("navigation_cancel".equals(command)){executeNavigationCancel(key,id,web,request);return;}
            networkCommand(key,web,command,request.optBoolean("agent"));
            if("bounds".equals(command)){bounds(web,request);reply(id,new JSONObject(),null);return;}
            if("state".equals(command)){reply(id,popupState(key,captureState(key,new JSONObject().put("url",web.getUrl()).put("title",web.getTitle()).put("loading",Boolean.TRUE.equals(loading.get(key))).put("can_go_back",web.canGoBack()).put("can_go_forward",web.canGoForward()).put("manual",manual.contains(key)).put("visible",web.getVisibility()==View.VISIBLE).put("platform","android-webview").put("automation",false).put("browser_use",browserUseSupport()).put("isolated_world",documentState(key)).put("frame_automation",protocolPopupFrameRouting(key)).put("navigation",views.epoch(key,web)).put("input_busy",views.inputBusy(key,web)).put("input_cleanup_confirmed",views.inputClean(key,web)).put("native_scale_ready",Double.isFinite(views.scale(key,web))).put("frame_target_keyboard",documents.containsKey(key)).put("wheel_input",documents.containsKey(key)).put("hover_input",documents.containsKey(key)).put("download_capability",false).put("download_control_capability",downloadControlReady(key)).put("downloads",downloadList(key)).put("download_cleanup_confirmed",downloadRetiring()==0).put("download_retiring",downloadRetiring()).put("download_error",downloadFailure(key)).put("native_navigation",networkNavigationState(key)).put("native_navigation_controls_supported",true).put("initial_navigation_pending",networkInitialNavigationPending(key)).put("error_code",errorCodes.getOrDefault(key,0)).put("error",errors.getOrDefault(key,"")))),null);return;}
            if("popups".equals(command)){executePopups(key,id,request);return;}
            if("downloads".equals(command)){executeDownloads(key,id,web,request);return;}
            if("upload".equals(command)){executeUpload(key,id,web,request);return;}
            if("chooser".equals(command)){executeChooser(key,id,request);return;}
            if("dialog".equals(command)){executeDialog(key,id,request);return;}
            if("takeover".equals(command)||"release".equals(command)){
                if("release".equals(command)&&manual.contains(key)&&request.optBoolean("agent"))throw new IllegalStateException("Only the user can return manual control");
                if("takeover".equals(command)){if(networkTakeoverRequest(key,web,id))return;networkTakeoverAdmission(key,web);cdpViews.invalidate(key,web,views.epoch(key,web),"input_cancelled");networkTakeover(key,web);}else if(networkReleaseControl(key,web,id,request.optBoolean("agent")))return;manualChooser.cancel(web);MdoBrowserDocument document=documents.get(key);if(document!=null)document.block(MdoBrowserDocument.MANUAL,"takeover".equals(command));
                if("takeover".equals(command)){cancelUpload(key);cancelPopup(key);cancelCapture(key);}views.manual(key,web,"takeover".equals(command));
                if("takeover".equals(command))manual.add(key);else manual.remove(key);reply(id,new JSONObject().put("manual",manual.contains(key)),null);return;
            }
            if(uploadBusy(key,web)&&!Arrays.asList("navigate","back","forward","reload","stop").contains(command))throw new IllegalStateException("upload_operation_busy");
            if(!dialogReady(key,web)&&!Arrays.asList("navigate","back","forward","reload","stop").contains(command)&&!chooserFramesAllowed(key,web,request))throw new IllegalStateException("dialog_pending_or_unclean");
            if(manual.contains(key)&&request.optBoolean("agent")&&!"snapshot".equals(command)&&!"screenshot".equals(command))throw new IllegalStateException("The user has taken control");
            if("navigate".equals(command)){String target=url(request.getString("url"));if(networkNavigate(key,web,id,target,request.optBoolean("agent"),request.optLong("timeout_ms",30000)))return;performNavigation(key,web,"navigate",target);}
            else if("back".equals(command)){if(networkTraverse(key,web,id,"back",request.optBoolean("agent"),request.optLong("timeout_ms",30000)))return;performNavigation(key,web,"back",null);}
            else if("forward".equals(command)){if(networkTraverse(key,web,id,"forward",request.optBoolean("agent"),request.optLong("timeout_ms",30000)))return;performNavigation(key,web,"forward",null);}
            else if("reload".equals(command)){if(networkTraverse(key,web,id,"reload",request.optBoolean("agent"),request.optLong("timeout_ms",30000)))return;performNavigation(key,web,"reload",null);}
            else if("stop".equals(command)){networkStop(key,web);cancelPopup(key);cancelDialog(key);cancelCapture(key);views.stop(key,web);MdoBrowserDocument document=documents.get(key);if(document!=null)document.cancel();web.stopLoading();cancelNavigationRequest(key,web);}
            else if("popup_prepare".equals(command)){executePopupPreparation(key,id,web,request);return;}
            else if("frames".equals(command)){executeFrames(key,id,web,request);return;}
            else if("automation".equals(command)){executeDocument(key,id,request);return;}
            else if("input".equals(command)){executeNativeInput(key,id,web,request);return;}
            else if("capture".equals(command)){executeNativeCapture(key,id,web,request);return;}
            else if("screenshot".equals(command)){
                int width=web.getWidth(),height=web.getHeight();if(width<=0||height<=0||((long)width)*height>16777216)throw new IllegalStateException("Browser viewport is too large");
                Bitmap bitmap=Bitmap.createBitmap(width,height,Bitmap.Config.ARGB_8888);web.draw(new Canvas(bitmap));
                ByteArrayOutputStream bytes=new ByteArrayOutputStream();bitmap.compress(Bitmap.CompressFormat.JPEG,65,bytes);bitmap.recycle();
                reply(id,new JSONObject().put("data",Base64.encodeToString(bytes.toByteArray(),Base64.NO_WRAP)),null);return;
            }else{
                String script,selector=JSONObject.quote(request.optString("selector"));
                if("snapshot".equals(command))script="({url:location.href,title:document.title,text:(document.body?.innerText||'').slice(0,200000),links:[...document.querySelectorAll('a[href]')].slice(0,100).map(a=>({text:a.innerText,url:a.href}))})";
                else if("click".equals(command))script="(()=>{let e="+(request.optString("selector").isEmpty()?"document.elementFromPoint("+request.optInt("x")+","+request.optInt("y")+")":"document.querySelector("+selector+")")+";if(!e)throw Error('Element not found');e.click();return true})()";
                else if("type".equals(command))script="(()=>{let e=document.querySelector("+selector+");if(!e)throw Error('Element not found');e.focus();e.value="+JSONObject.quote(request.optString("text"))+";e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new Event('change',{bubbles:true}));return true})()";
                else if("scroll".equals(command))script="(()=>{scrollBy("+request.optInt("x")+","+request.optInt("y")+");return true})()";
                else if("evaluate".equals(command))script=request.getString("script");
                else throw new IllegalArgumentException("Unsupported browser command");
                final WebView executionView=web;final long executionEpoch=views.epoch(key,web);
                final MdoBrowserChannel<WebMessagePort>.Lease executionLease=channel.current();
                web.evaluateJavascript("(()=>{try{return {ok:true,value:("+script+")}}catch(e){return {ok:false,error:String(e.message||e)}}})()", output -> {
                    if(!views.owns(key,executionView)||views.epoch(key,executionView)!=executionEpoch){reply(executionLease,id,null,"Browser document changed; execution result is unconfirmed");return;}
                    try{JSONObject result=new JSONObject(output);reply(executionLease,id,result,result.optBoolean("ok")?null:result.optString("error","Browser execution failed"));}
                    catch(Exception error){reply(executionLease,id,null,"Browser returned an invalid result");}
                });return;
            }
            reply(id,new JSONObject().put("accepted",true),null);
        }catch(Exception error){reply(id,null,error.getMessage());}
    }
    void pause(){manualChooser.pause();browserPaused=true;cancelPopupControls();cancelPopups();cancelDialogs();blockDocuments(MdoBrowserDocument.BACKGROUND,true);views.background(true);}
    void resume(){manualChooser.resume();browserPaused=false;MdoBrowserCdpPopupNative.reconcileRetirements();views.background(false);blockDocuments(MdoBrowserDocument.BACKGROUND,false);resumePopupNetworks();}
    void destroy(){
        stopped=true;cancelBrowserCloses();
        // Capture and register every exact physical owner before any logical
        // close can synchronously acknowledge a native child transport.
        Map<WebView,MdoBrowserPopups<WebView>> popupRetiring=new IdentityHashMap<>();
        for(Map.Entry<String,MdoBrowserPopups<WebView>> entry:windows.entrySet()){
            WebView view=views.get(entry.getKey());if(view!=null)popupRetiring.put(view,entry.getValue());}
        List<MdoBrowserDialogTarget> dialogRetiring=new ArrayList<>(dialogs.values());
        List<CaptureEntry> captureRetiring=new ArrayList<>(captures.values());
        List<WebView> owned=views.values();Map<WebView,MdoBrowserDocument> retiring=new IdentityHashMap<>();
        for(Map.Entry<String,MdoBrowserDocument> entry:documents.entrySet()){WebView view=views.get(entry.getKey());if(view!=null)retiring.put(view,entry.getValue());}
        List<NativeDestruction> closing=new ArrayList<>();boolean outerBulk=bulkNativeDestruction;bulkNativeDestruction=true;
        try{
            for(WebView web:owned){
                MdoBrowserDialogTarget dialog=null;for(MdoBrowserDialogTarget target:dialogRetiring)if(target.view==web){dialog=target;break;}
                CaptureEntry capture=null;for(CaptureEntry entry:captureRetiring)if(entry.view==web){capture=entry;break;}
                closing.add(prepareNativeDestruction(null,web,popupRetiring.get(web),dialog,capture,retiring.get(web)));
            }
            cancelPopupControls();manualChooser.close();cancelUploads();networkRetireAll("network_host_closed");cdpWorker.close();cdpViews.close();channel.close();if(port!=null)port.close();
            for(MdoBrowserPopups<WebView> popup:new ArrayList<>(windows.values()))popup.close();
            for(MdoBrowserDialogTarget target:dialogRetiring)target.close();
            for(CaptureEntry entry:captureRetiring)entry.capture.close();
            views.close();
            for(MdoBrowserDocument document:new ArrayList<>(documents.values()))document.close();documents.clear();documentFailures.clear();
            captures.clear();dialogs.clear();navigationRequests.clear();windows.clear();popupLinks.clear();pageClosed.clear();
            manual.clear();loading.clear();errors.clear();errorCodes.clear();crashed.clear();locations.clear();
            // A callback may close a sibling. Every row must be prepared before
            // activating even the first physical destructor.
            for(NativeDestruction row:closing)row.prepared=true;
            for(NativeDestruction row:closing)executeNativeDestruction(row);
        }catch(RuntimeException|Error failure){nativeViewCleanupFailed=true;throw failure;}
        finally{
            bulkNativeDestruction=outerBulk;
            if(!bulkNativeDestruction)for(NativeDestruction row:closing)if(row.complete)nativeDestructions.remove(row.view,row);
        }
    }

}
