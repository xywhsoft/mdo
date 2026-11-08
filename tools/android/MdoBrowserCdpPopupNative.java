package org.xserver.android;

import android.os.Looper;
import android.webkit.WebView;
import java.io.IOException;
import java.lang.ref.WeakReference;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.atomic.AtomicInteger;

/** Exact UI-owned physical view/native callback join. Workers see only the
 * original lease and a data-only native rejection receipt, never a WebView. */
final class MdoBrowserCdpPopupNative {
    interface Disposal {void destroy(String key,WebView exact);}
    interface Destruction {boolean ready();default boolean failed(){return false;}void destroy();}
    interface ConsumptionEvents {void consumed(MdoBrowserPopups.Adoption original);void failed(String code);}
    /** UI-owned subscription. Retained native records keep only its weak link,
     * so an uncertain transport cannot retain an Activity/callback indefinitely. */
    static final class Consumption implements AutoCloseable {
        private final WeakReference<Created> owner;private final MdoBrowserPopups.Adoption original;
        private ConsumptionEvents events;private int state;
        private Consumption(Created owner,MdoBrowserPopups.Adoption original,ConsumptionEvents events){this.owner=new WeakReference<>(owner);this.original=original;this.events=events;}
        boolean current(){thread();Created value=owner.get();MdoBrowserCdpPopupNative source=value==null?null:value.owner.get();
            return state==0&&value!=null&&!value.done&&value.live.get()&&!value.retirementRequested&&value.adoption==original&&source!=null&&source.scope==value.action&&source.current();}
        boolean delivered(){thread();return state==1;}
        private void unbind(){Created value=owner.get();if(value!=null&&value.consumption.get()==this)value.consumption.clear();owner.clear();}
        private void deliver(){
            thread();Created value=owner.get();if(state!=0||value==null||!value.consumptionObserved)return;
            boolean valid=current()&&original.nativeConsumed()&&original.ok();ConsumptionEvents callback=events;events=null;state=valid?1:3;unbind();
            if(callback==null)return;
            if(!valid){try{callback.failed("cdp_popup_native_consumption_unavailable");}catch(RuntimeException|Error ignored){}return;}
            try{callback.consumed(original);}
            catch(RuntimeException|Error error){state=3;try{callback.failed("cdp_popup_native_consumption_callback_failed");}catch(RuntimeException|Error ignored){/* never replay native consumption */}}
        }
        @Override public void close(){thread();if(state==0)state=2;events=null;unbind();}
    }
    private static final AtomicBoolean CREATION_UNCERTAIN=new AtomicBoolean();
    private static final UiRetention[] UI_OWNERS=new UiRetention[8];
    private static final Created[] PROMOTED=new Created[8];
    private static final android.os.Handler PROMOTION_UI=new android.os.Handler(Looper.getMainLooper());
    static boolean creationConfirmed(){return !CREATION_UNCERTAIN.get();}
    static int retainedUiOwners(){thread();int count=0;for(UiRetention owner:UI_OWNERS)if(owner!=null)count++;return count;}
    static int pendingRetirements(){thread();int count=0;for(UiRetention owner:UI_OWNERS)if(owner!=null&&owner.retirement.pending())count++;return count;}
    static int promotedUiOwners(){thread();int count=0;for(Created child:PROMOTED)if(child!=null)count++;return count;}
    private static void reconcilePromoted(Retirement original){for(Created child:PROMOTED)if(child!=null&&child.promotion!=null&&(original==null||child.promotion.parent==original)){
        HumanControl control=child.promotion.humanControl;if(control!=null)control.expire();
        if(!child.promotion.live.get()||child.promotion.parent.quiescenceRequested())try{child.close();}catch(IOException failure){/* exact failed owner remains retained */}
    }}
    static void reconcileRetirements(){thread();for(UiRetention owner:UI_OWNERS)if(owner!=null)owner.retirement.deliver();reconcilePromoted(null);}
    static void viewDestroyed(WebView exact){thread();for(UiRetention owner:UI_OWNERS)if(owner!=null)owner.destroyed(exact);for(Created child:PROMOTED)if(child!=null&&child.view==exact&&child.construction.destroyed())try{child.close();}catch(IOException failure){/* retained cleanup uncertainty */}}
    private static boolean destructionAllowed(WebView exact){
        if(exact==null)return false;
        for(UiRetention owner:UI_OWNERS)if(owner!=null){
            if(owner.source.view.get()==exact&&owner.core.protocolHasObligations(owner.scope))return false;
            for(Created child:owner.book.constructions)if(child!=null&&child.view==exact&&child.sent&&(child.adoption==null||!child.adoption.nativeConsumed()))return false;
        }return true;
    }
    /** Register every host close before closing controllers can call back. */
    static boolean registerDestruction(WebView exact,Destruction destruction){
        thread();if(exact==null||destruction==null)throw new IllegalArgumentException("Native destruction requires its exact UI owner");boolean owned=false;
        for(UiRetention owner:UI_OWNERS)if(owner!=null){
            for(Created child:owner.book.constructions)if(child!=null&&child.view==exact&&!child.done){
                if(child.hostDestruction==null)child.hostDestruction=destruction;child.live.set(false);owned=true;
            }
            if(owner.source.view.get()==exact){if(owner.rootDestruction==null)owner.rootDestruction=destruction;owner.ending=true;owner.core.retireProtocolUi(owner.scope);owned=true;}
        }for(Created child:PROMOTED)if(child!=null&&child.view==exact&&!child.done){if(child.hostDestruction==null)child.hostDestruction=destruction;child.promotion.live.set(false);owned=true;}return owned;
    }
    static void activateDestruction(WebView exact,Destruction destruction)throws IOException {
        thread();for(UiRetention owner:UI_OWNERS)if(owner!=null){
            for(Created child:owner.book.constructions)if(child!=null&&child.view==exact&&child.hostDestruction==destruction){
                // The exact product disposer can register its host callback
                // from inside Created.close. Complete that callback directly
                // once its own native barriers permit it, without reentering
                // Created.close or replaying a currently running destructor.
                if(child.closing){if(!destruction.failed()&&destruction.ready()&&destructionAllowed(exact))destruction.destroy();}
                else child.close();
            }
            if(owner.source.view.get()==exact&&owner.rootDestruction==destruction){if(owner.source.scope==owner.scope)owner.source.reject();owner.sweep();}
        }
        for(Created child:PROMOTED)if(child!=null&&child.view==exact&&child.hostDestruction==destruction){if(child.closing){if(!destruction.failed()&&destruction.ready()&&destructionAllowed(exact))destruction.destroy();}else child.close();}
    }
    /** One original browser wire and at most eight data-only native barriers.
     * All permits are revoked before any UI notification. Retaining the wire
     * cannot retain a session, broker, worker, listener or SDK/UI owner. */
    static final class CloseGate implements MdoBrowserCdpSession.CloseBarrier {
        private final Retirement[] owners=new Retirement[8];
        private WeakReference<MdoBrowserCdpSession.Wire> wire=new WeakReference<>(null);
        private MdoBrowserCdpSession.WireCancellation cancellation;private boolean stopping,attempted,complete,failed;
        @Override public synchronized void bind(MdoBrowserCdpSession.Wire original,MdoBrowserCdpSession.WireCancellation cancel)throws IOException {
            if(stopping||cancellation!=null||original==null||cancel==null)throw new IOException("cdp_popup_close_scope_invalid");
            wire=new WeakReference<>(original);cancellation=cancel;
        }
        synchronized void add(Retirement original)throws IOException {
            if(stopping||original==null)throw new IOException("cdp_popup_close_scope_invalid");int slot=-1;
            for(int i=0;i<owners.length;i++){if(owners[i]==original)return;if(owners[i]==null||owners[i].nativeSettled.get())slot=i;}
            if(slot<0)throw new IOException("cdp_popup_close_owner_limit");
            if(!original.closeGate.compareAndSet(null,this))throw new IOException("cdp_popup_close_scope_invalid");owners[slot]=original;
        }
        @Override public void close(MdoBrowserCdpSession.Wire original){
            synchronized(this){
                if(original==null||wire.get()!=original)throw new IllegalStateException("Native popup transport changed");if(stopping)return;stopping=true;
                for(Retirement owner:owners)if(owner!=null&&!owner.nativeSettled.get())owner.quiesceRequested.set(true);
            }
            for(Retirement owner:owners)if(owner!=null&&!owner.nativeSettled.get())owner.notifyQuiescence();settled();
        }
        void settled(){
            MdoBrowserCdpSession.WireCancellation original;
            synchronized(this){if(!stopping||attempted)return;for(Retirement owner:owners)if(owner!=null&&!owner.nativeSettled.get())return;attempted=true;original=cancellation;}
            try{original.close();synchronized(this){complete=true;cancellation=null;}}
            catch(RuntimeException|Error failure){synchronized(this){failed=true;}CREATION_UNCERTAIN.set(true);}
        }
        synchronized boolean pending(){return stopping&&!attempted;}
        synchronized boolean complete(){return complete;}
        synchronized boolean failed(){return failed;}
        synchronized boolean accepting(){return cancellation!=null&&!stopping&&!failed;}
    }
    /** Worker-visible cancellation intent. It carries only a weak link to its
     * original UI record, never a core, host, view or replacement claim. */
    static final class Retirement {
        private final WeakReference<UiRetention> owner;
        private final MdoBrowserPopups.Scope scope;
        private final AtomicBoolean requested=new AtomicBoolean(),delivered=new AtomicBoolean();
        private final AtomicBoolean quiesceRequested=new AtomicBoolean(),quiesceDelivered=new AtomicBoolean(),nativeSettled=new AtomicBoolean();
        private final AtomicReference<CloseGate> closeGate=new AtomicReference<>();
        private final AtomicBoolean ownerSettled=new AtomicBoolean();private final Promotion[] promotions=new Promotion[7];
        private WeakReference<Runnable> notification=new WeakReference<>(null);
        private WeakReference<Runnable> quiescenceNotification=new WeakReference<>(null);
        private boolean releaseAllowed;
        private Retirement(UiRetention owner){this.owner=new WeakReference<>(owner);scope=owner.scope;}
        synchronized void request(boolean released){if(!requested.get()){releaseAllowed=released;requested.set(true);}}
        boolean pending(){return requested.get()&&!delivered.get()||quiesceRequested.get()&&!quiesceDelivered.get();}
        boolean current(){return creationConfirmed()&&!requested.get()&&!quiesceRequested.get()&&scope.current();}
        boolean quiescenceRequested(){return quiesceRequested.get();}
        boolean quiescenceDelivered(){return quiesceDelivered.get();}
        boolean belongsTo(MdoBrowserCdpPopupNative source){return source!=null&&source.scope==scope;}
        void onDelivered(Runnable callback){thread();if(callback==null||requested.get()||delivered.get()||notification.get()!=null)throw new IllegalStateException("Native retirement notification is already bound");notification=new WeakReference<>(callback);}
        void onQuiescence(Runnable callback){thread();if(callback==null||quiesceRequested.get()||quiescenceNotification.get()!=null)throw new IllegalStateException("Native quiescence notification is already bound");quiescenceNotification=new WeakReference<>(callback);}
        private void notifyQuiescence(){Runnable callback=quiescenceNotification.get();if(callback!=null)try{callback.run();}catch(RuntimeException|Error ignored){/* UI recovery can reconcile the original record */}synchronized(this){for(Promotion child:promotions)if(child!=null)child.notifyClose();}}
        private synchronized void retainPromotion(Promotion original)throws IOException {
            CloseGate gate=closeGate.get();if(!current()||ownerSettled.get()||gate==null||!gate.accepting())throw new IOException("cdp_popup_native_promotion_unavailable");
            for(int i=0;i<promotions.length;i++)if(promotions[i]==null){promotions[i]=original;return;}throw new IOException("cdp_popup_native_promotion_limit");
        }
        private void releasePromotion(Promotion original){synchronized(this){for(int i=0;i<promotions.length;i++)if(promotions[i]==original){promotions[i]=null;break;}}settleDependencies();}
        private void settleDependencies(){synchronized(this){if(!ownerSettled.get())return;for(Promotion child:promotions)if(child!=null)return;if(!nativeSettled.compareAndSet(false,true))return;}CloseGate original=closeGate.get();if(original!=null)original.settled();}
        private void settled(){ownerSettled.set(true);settleDependencies();}
        private void failed(UiRetention original){CREATION_UNCERTAIN.set(true);if(original.source.scope==original.scope&&original.source.failure==null)original.source.failure="cdp_popup_native_retirement_unconfirmed";}
        private void notifyDelivered(){Runnable callback=notification.get();notification.clear();if(callback!=null)try{callback.run();}catch(RuntimeException|Error ignored){/* native retirement is independent of observer bookkeeping */}}
        void rejectOriginal()throws IOException {
            thread();UiRetention original=owner.get();if(original==null||original.released)return;
            original.core.retireProtocolUi(original.scope);
            if(original.source.scope==original.scope)original.source.reject();
            else{
                boolean failed=false;for(Created child:original.book.constructions)if(child!=null)try{child.close();}catch(IOException error){failed=true;}
                if(failed||original.core.protocolHasObligations(original.scope))throw new IOException("cdp_popup_native_scope_changed");
            }
        }
        void deliver(){
            thread();
            reconcilePromoted(this);
            if(quiesceRequested.get()&&quiesceDelivered.compareAndSet(false,true)){
                UiRetention original=owner.get();if(original==null||original.released)settled();
                else try{rejectOriginal();original.sweep();}catch(IOException|RuntimeException|Error failure){failed(original);}
            }
            if(!requested.get()||!delivered.compareAndSet(false,true))return;
            UiRetention original=owner.get();if(original==null||original.released){notifyDelivered();return;}
            try{
                if(!quiesceDelivered.get())rejectOriginal();if(original.source.scope==original.scope)original.source.finish(releaseAllowed);
            }catch(IOException|RuntimeException|Error failure){
                failed(original);
            }finally{try{original.ending();}finally{notifyDelivered();}}
        }
    }
    /** UI-only, single original human-control attempt. Preparing reserves a
     * bounded timer without revoking data; start retires the original handoff
     * before the page coordinator fences document leases and cancels input;
     * commit still requires the same clean native owner. No protocol receipt. */
    static final class HumanControl implements AutoCloseable {
        private final Promotion owner;private final MdoBrowserCdpPopupBroker.Handoff original;
        private final WeakReference<WebView> view;private final long born;
        private final Runnable expiry=this::expire;private int state;private String failure; // 0 prepared, 1 started, 2 committed, 3 cancelled
        private HumanControl(Promotion owner,MdoBrowserCdpPopupBroker.Handoff original,WebView exact){this.owner=owner;this.original=original;view=new WeakReference<>(exact);born=android.os.SystemClock.elapsedRealtime();}
        private boolean timely(){long age=android.os.SystemClock.elapsedRealtime()-born;return age>=0&&age<5000;}
        private void removeTimer(){try{PROMOTION_UI.removeCallbacks(expiry);}catch(RuntimeException|Error ignored){/* bounded stale timer has no live authority */}}
        boolean current(){thread();WebView exact=view.get();return state<2&&timely()&&owner.humanControl==this&&original.owns(owner)&&exact!=null&&owner.nativeOwner.ownsControl(exact)&&
            (state==0?owner.humanTransfer.get()==4&&owner.current()&&original.current():owner.humanTransfer.get()==1&&original.draining());}
        boolean started(){thread();return state==1;}
        boolean committed(){thread();return state==2;}
        String failure(){thread();return failure;}
        void expire(){thread();if(state<2&&!timely())cancel("cdp_popup_human_control_expired");}
        void start()throws IOException{
            thread();expire();if(state!=0)throw new IOException(failure==null?"cdp_popup_human_control_replayed":failure);
            if(!current()){cancel("cdp_popup_human_owner_invalid");throw new IOException(failure);}
            owner.humanTransfer.set(1);state=1;original.retireAuthority();owner.live.set(false);
        }
        boolean ready(){thread();if(state!=1||!current())return false;try{return owner.nativeOwner.view()==view.get();}catch(RuntimeException unavailable){return false;}}
        void commit()throws IOException{
            thread();if(state!=1)throw new IOException(failure!=null?failure:state>=2?"cdp_popup_human_control_replayed":"cdp_popup_human_control_not_started");
            if(!timely()){cancel("cdp_popup_human_control_expired");throw new IOException(failure);}
            if(!current()){cancel("cdp_popup_human_owner_invalid");throw new IOException(failure);}
            if(!ready())throw new IOException("cdp_popup_human_cleanup_pending");
            // Suppress reentrant close until native ownership has its real
            // result. Neither a timer nor an old callback may replay commit.
            state=3;removeTimer();
            try{owner.nativeOwner.takeControl();owner.humanTransfer.set(2);state=2;}
            catch(RuntimeException|Error error){owner.humanTransfer.set(3);failure="cdp_popup_human_takeover_failed";throw new IOException(failure);}
            finally{owner.humanControl=null;owner.live.set(false);view.clear();owner.notifyClose();}
        }
        private void cancel(String code){
            thread();if(state>=2)return;boolean started=state==1;state=3;failure=code;removeTimer();view.clear();
            if(owner.humanControl!=this)return;owner.humanControl=null;
            owner.humanTransfer.set(started?3:0);
            if(started){original.retireAuthority();owner.live.set(false);owner.notifyClose();}
        }
        @Override public void close(){cancel("cdp_popup_human_control_cancelled");}
    }
    /** Native ownership only, not a protocol/session/world promotion. Workers
     * retain weak UI notification and data-only registry/dependency receipts. */
    static final class Promotion implements AutoCloseable {
        private final MdoBrowserLifecycle.Independent<WebView> nativeOwner;private final Retirement parent;private final Object proofIdentity;
        private final WeakReference<Runnable> closer;private final AtomicBoolean live=new AtomicBoolean(true),ended=new AtomicBoolean(),failed=new AtomicBoolean();
        private final AtomicInteger humanTransfer=new AtomicInteger(); // 0 unused, 1 retiring, 2 committed, 3 failed, 4 prepared
        private HumanControl humanControl;
        private Promotion(MdoBrowserLifecycle.Independent<WebView> owner,Retirement parent,Object proof,Runnable closer){nativeOwner=owner;this.parent=parent;proofIdentity=proof;this.closer=new WeakReference<>(closer);}
        boolean current(){CloseGate gate=parent.closeGate.get();return creationConfirmed()&&live.get()&&!ended.get()&&!failed.get()&&!nativeOwner.humanOwned()&&nativeOwner.current()&&gate!=null&&gate.accepting();}
        boolean matches(Child original){return original!=null&&original.identity==proofIdentity;}
        Document document(MdoBrowserCdpMarker marker)throws IOException {
            thread();if(!current()||marker==null||marker.lease().physicalNetwork()||!marker.ownsNative(view()))throw new IOException("cdp_popup_document_owner_invalid");
            Document proof=new Document(this,marker.lease());if(!proof.lease.attach(proof.identity))throw new IOException("cdp_popup_document_owner_invalid");return proof;
        }
        Source source(MdoBrowserCdpMarker marker)throws IOException {
            thread();if(!current()||marker==null||!marker.lease().physicalNetwork()||!marker.ownsNative(view()))throw new IOException("cdp_popup_source_owner_invalid");
            Source proof=new Source(this,marker.lease());if(!proof.lease.attach(proof.identity))throw new IOException("cdp_popup_source_owner_invalid");return proof;
        }
        boolean ended(){return ended.get();}boolean failed(){return failed.get();}boolean nativeDestroyed(){return nativeOwner.destroyed();}
        boolean humanTransferred(){return humanTransfer.get()==2;}
        private boolean humanPending(){return humanTransfer.get()==1;}
        boolean controlEligible(MdoBrowserCdpPopupBroker.Handoff original,WebView exact){thread();return original!=null&&original.owns(this)&&original.current()&&current()&&nativeOwner.ownsControl(exact)&&humanTransfer.get()==0;}
        HumanControl prepareControl(MdoBrowserCdpPopupBroker.Handoff original,WebView exact)throws IOException {
            thread();if(!controlEligible(original,exact))throw new IOException("cdp_popup_human_owner_invalid");
            HumanControl prepared=new HumanControl(this,original,exact);
            final boolean scheduled;try{scheduled=PROMOTION_UI.postDelayed(prepared.expiry,5000);}catch(RuntimeException|Error error){throw new IOException("cdp_popup_human_timer_unavailable");}
            if(!scheduled)throw new IOException("cdp_popup_human_timer_unavailable");
            if(!humanTransfer.compareAndSet(0,4)){prepared.removeTimer();throw new IOException("cdp_popup_human_control_replayed");}
            humanControl=prepared;return prepared;
        }
        /** Exact UI transaction: publish typed retirement before dropping
         * native ownership, but defer old close notifications until the
         * registry has irrevocably assigned this same view to its human. */
        void takeControl(MdoBrowserCdpPopupBroker.Handoff original)throws IOException {
            thread();
            if(original==null||!original.owns(this)||!original.current()||!current())throw new IOException("cdp_popup_human_owner_invalid");
            try{nativeOwner.view();}catch(RuntimeException error){throw new IOException("cdp_popup_human_owner_invalid");}
            HumanControl control=prepareControl(original,nativeOwner.view());
            try{control.start();control.commit();}finally{control.close();}
        }
        WebView view(){thread();if(!current())throw new IllegalStateException("cdp_popup_native_promotion_unavailable");return nativeOwner.view();}
        private void notifyClose(){if(humanPending())return;Runnable action=closer.get();if(action!=null)try{action.run();}catch(RuntimeException|Error ignored){/* retained UI owner is reconciled on recovery */}}
        @Override public void close(){live.set(false);notifyClose();}
    }
    /** UI-certified exact document marker. Its marker/quota stay UI-owned;
     * only data receipts cross to the existing worker. */
    static final class Document {
        private final Promotion owner;final MdoBrowserCdpMarker.Lease lease;private final Object identity=new Object();
        private Document(Promotion owner,MdoBrowserCdpMarker.Lease lease){this.owner=owner;this.lease=lease;}
        boolean current(){return owner.current()&&lease.current()&&lease.belongsToAttachment(identity);}
        boolean matches(Promotion exact){return owner==exact;}
    }
    /** Exact UI-native physical ownership, deliberately independent of a DOM
     * epoch or an isolated execution world. It grants network housekeeping only. */
    static final class Source {
        private final Promotion owner;final MdoBrowserCdpMarker.Lease lease;private final Object identity=new Object();
        private Source(Promotion owner,MdoBrowserCdpMarker.Lease lease){this.owner=owner;this.lease=lease;}
        boolean current(){return owner.current()&&lease.physicalNetwork()&&lease.current()&&lease.belongsToAttachment(identity);}
        boolean matches(Promotion exact){return owner==exact;}
    }
    /** Bounded UI ownership survives a host removing its opener's controller.
     * Workers retain only weak links to this record, never its core/book/host. */
    private static final class UiRetention {
        final MdoBrowserCdpPopupNative source;final MdoBrowserPopups<WebView> core;final MdoBrowserPopups.Scope scope;final UiOwned book;
        boolean ending,released,sweeping,rootFailed;Destruction rootDestruction;final Retirement retirement;
        UiRetention(MdoBrowserCdpPopupNative source,MdoBrowserPopups<WebView> core,MdoBrowserPopups.Scope scope,UiOwned book){this.source=source;this.core=core;this.scope=scope;this.book=book;retirement=new Retirement(this);}
        void ending(){ending=true;sweep();}
        void destroyed(WebView exact){
            if(released)return;
            if(source.view.get()==exact){ending=true;if(source.scope==scope){source.destroyedSource=retirement;source.reject();}else for(Created child:book.constructions)if(child!=null)try{child.close();}catch(IOException error){/* retained uncertainty */}}
            for(Created child:book.constructions)if(child!=null&&child.view==exact&&child.construction!=null&&child.construction.destroyed())try{child.close();}catch(IOException error){/* retained uncertainty */}
            sweep();
        }
        void sweep(){
            if(released||!ending&&!retirement.quiesceDelivered.get()||sweeping)return;sweeping=true;
            try{
                for(Created child:book.constructions)if(child!=null&&child.construction!=null&&child.construction.destroyed())try{child.close();}catch(IOException error){/* no false physical release */}
                if(core.protocolHasObligations(scope))return;
                if(rootDestruction!=null){
                    if(rootDestruction.failed()){rootFailed=true;CREATION_UNCERTAIN.set(true);if(source.failure==null)source.failure="cdp_popup_native_owner_cleanup_unconfirmed";return;}
                    if(rootFailed||!rootDestruction.ready()||!destructionAllowed(source.view.get()))return;
                    try{rootDestruction.destroy();rootDestruction=null;}
                    catch(RuntimeException|Error error){rootFailed=true;CREATION_UNCERTAIN.set(true);if(source.failure==null)source.failure="cdp_popup_native_owner_cleanup_unconfirmed";return;}
                }
                for(Created child:book.constructions)if(child!=null)return;
                retirement.settled();if(!ending)return;
                core.releaseProtocolUi(scope);released=true;for(int i=0;i<UI_OWNERS.length;i++)if(UI_OWNERS[i]==this)UI_OWNERS[i]=null;
                if(book.retention.get()==this)book.retention.clear();if(source.retention.get()==this)source.retention.clear();
            }finally{sweeping=false;}
        }
    }
    /** UI-owned construction result. Only this class invokes the native new;
     * callers cannot certify a supplied/preexisting WebView as post-capture. */
    static final class Created implements AutoCloseable {
        private final WeakReference<MdoBrowserCdpPopupNative> owner;private WebView view;
        private MdoBrowserLifecycle.Fresh<WebView> birth;private MdoBrowserCdpPopupWorker.Allocation allocation;
        private final MdoBrowserLifecycle<WebView> registry;private final String key;private Disposal disposal;
        private final MdoBrowserPopups.Scope action;
        private final WeakReference<UiOwned> book;
        private final AtomicBoolean live=new AtomicBoolean(true);
        private Destruction hostDestruction;
        private MdoBrowserPopups.Adoption adoption;
        private WeakReference<Consumption> consumption=new WeakReference<>(null);
        private boolean consumptionRegistered,consumptionObserved;
        private Promotion promotion;private final AtomicBoolean promotionPosted=new AtomicBoolean();
        private final Runnable promotionClose=()->{promotionPosted.set(false);try{close();}catch(IOException failure){/* sticky diagnostic, no destructor replay */}};
        private final Runnable promotionNotify=()->{if(promotionPosted.compareAndSet(false,true))try{if(!PROMOTION_UI.post(promotionClose))promotionPosted.set(false);}catch(RuntimeException|Error failure){promotionPosted.set(false);}};
        private MdoBrowserLifecycle.Construction<WebView> construction;private boolean sent,done,closing,cleanupFailed,retirementRequested,awaitingConsumption;
        private Created(MdoBrowserCdpPopupNative owner,UiOwned book,MdoBrowserLifecycle<WebView> registry,String key,MdoBrowserCdpPopupWorker.Allocation allocation,Disposal disposal){
            this.owner=new WeakReference<>(owner);this.book=new WeakReference<>(book);this.registry=registry;this.key=key;this.allocation=allocation;this.disposal=disposal;action=owner.scope;
        }
        void attachDisposal(Disposal cleanup){thread();MdoBrowserCdpPopupNative original=owner.get();if(cleanup==null||disposal!=null||sent||done||original==null||!original.current()||!allocation.current())throw new IllegalStateException("cdp_popup_native_child_invalid");disposal=cleanup;}
        WebView view(){thread();if(disposal==null)throw new IllegalStateException("cdp_popup_native_disposal_required");MdoBrowserCdpPopupNative original=owner.get();if(done||!live.get()||view==null||birth==null||!birth.current()||!allocation.current()||original==null||!original.current())throw new IllegalStateException("cdp_popup_native_child_invalid");return view;}
        private void cancelConsumption(){Consumption value=consumption.get();if(value!=null)value.close();}
        private void finished(){cancelConsumption();done=true;if(promotion!=null){promotion.live.set(false);if(promotion.ended.compareAndSet(false,true))promotion.parent.releasePromotion(promotion);for(int i=0;i<PROMOTED.length;i++)if(PROMOTED[i]==this)PROMOTED[i]=null;try{PROMOTION_UI.removeCallbacks(promotionClose);}catch(RuntimeException|Error ignored){}return;}UiOwned original=book.get();if(original!=null){for(int i=0;i<original.constructions.length;i++)if(original.constructions[i]==this){original.constructions[i]=null;break;}original.drained();}}
        private void cancelOwner(){if(promotion!=null)return;MdoBrowserCdpPopupNative original=owner.get();if(original!=null&&original.scope==action&&original.rejecting.get()==0&&original.pending()==allocation.pending)original.reject();}
        private void awaitConsumption(){retirementRequested=true;if(!awaitingConsumption){awaitingConsumption=true;MdoBrowserCdpPopupNative source=owner.get();if(promotion==null&&source!=null)source.retiring.incrementAndGet();}}
        private void consumed(MdoBrowserPopups.Adoption exact)throws IOException {
            if(exact!=adoption)return;consumptionObserved=true;if(retirementRequested){close();return;}
            Consumption callback=consumption.get();if(callback!=null)callback.deliver();
        }
        private Consumption whenConsumed(MdoBrowserPopups.Adoption exact,ConsumptionEvents events)throws IOException {
            thread();if(consumptionRegistered)throw new IOException("cdp_popup_native_consumption_replayed");
            if(events==null||exact==null||exact!=adoption||!sent||done||!live.get()||retirementRequested)throw new IOException("cdp_popup_native_consumption_unavailable");
            Consumption callback=new Consumption(this,exact,events);consumptionRegistered=true;consumption.clear();
            consumption=new WeakReference<>(callback);if(consumptionObserved)callback.deliver();return callback;
        }
        private void clearConsumptionWait(){if(awaitingConsumption){awaitingConsumption=false;MdoBrowserCdpPopupNative source=owner.get();if(promotion==null&&source!=null)source.retiring.decrementAndGet();}}
        private void acknowledgeDestruction(){if(promotion!=null)return;MdoBrowserCdpPopupNative source=owner.get();MdoBrowserPopups<WebView> core=source==null?null:source.owner.get();if(adoption!=null&&adoption.dispatched()&&core!=null)core.nativeChildDestroyed(adoption,view);}
        private Promotion promote(Child child)throws IOException {return promote(child,null);}
        private Promotion promote(Child child,MdoBrowserCdpPopupBroker.Handoff handoff)throws IOException {
            thread();MdoBrowserCdpPopupNative source=owner.get();UiOwned original=book.get();Retirement parent=source==null?null:source.retirement();
            if(done||closing||cleanupFailed||awaitingConsumption||retirementRequested||promotion!=null||disposal==null||source==null||original==null||source.scope!=action||!source.current()||!sent||adoption==null||!adoption.ok()||!adoption.nativeConsumed()||child==null||child.birth!=birth||child.allocation!=allocation||!child.current()||!child.belongsTo(parent)||child.proof()==null||!child.proof().ok)throw new IOException("cdp_popup_native_promotion_unavailable");
            int slot=-1;for(int i=0;i<PROMOTED.length;i++)if(PROMOTED[i]==null){slot=i;break;}if(slot<0)throw new IOException("cdp_popup_native_promotion_limit");
            MdoBrowserLifecycle.Independent<WebView> nativeOwner=registry.prepareIndependent(birth,source.view.get(),view);
            Promotion prepared=new Promotion(nativeOwner,parent,child.identity,promotionNotify);parent.retainPromotion(prepared);
            try{if(handoff!=null)handoff.reserve(child,prepared);registry.commitIndependent(nativeOwner,source.view.get(),view);}catch(IOException|RuntimeException|Error failure){if(handoff!=null)handoff.close();parent.releasePromotion(prepared);throw failure;}
            MdoBrowserCdpPopupWorker.Allocation captured=allocation;
            promotion=prepared;PROMOTED[slot]=this;live.set(false);for(int i=0;i<original.constructions.length;i++)if(original.constructions[i]==this){original.constructions[i]=null;break;}
            allocation=null;owner.clear();book.clear();original.drained();
            if(handoff!=null)handoff.commit(prepared);
            if(!(handoff==null?captured.current():captured.scopeCurrent())||!prepared.current()){if(handoff!=null)handoff.close();prepared.close();throw new IOException("cdp_popup_native_promotion_cancelled");}
            try{child.close();}catch(RuntimeException|Error failure){prepared.failed.set(true);CREATION_UNCERTAIN.set(true);prepared.close();throw failure;}return prepared;
        }
        @Override public void close()throws IOException {
            thread();if(done||closing)return;
            if(promotion!=null&&promotion.humanPending()&&hostDestruction==null)return;
            cancelConsumption();
            live.set(false);
            if(sent&&adoption!=null&&!adoption.dispatched())sent=false;
            if(birth!=null&&birth.humanOwned()&&hostDestruction==null){
                if(sent&&(adoption==null||!adoption.nativeConsumed())){retirementRequested=true;return;}
                clearConsumptionWait();finished();cancelOwner();return;
            }
            if(sent&&(adoption==null||!adoption.nativeConsumed())){awaitConsumption();return;}
            if(hostDestruction!=null&&!hostDestruction.failed()&&(!hostDestruction.ready()||!destructionAllowed(view))){awaitConsumption();return;}
            if(construction!=null&&construction.destroyed()){clearConsumptionWait();acknowledgeDestruction();finished();if(!sent)cancelOwner();return;}
            if(cleanupFailed)throw new IOException(view==null?"cdp_popup_native_constructor_unconfirmed":"cdp_popup_native_child_cleanup_unconfirmed");
            closing=true;MdoBrowserCdpPopupNative source=promotion==null?owner.get():null;if(source!=null)source.disposing.incrementAndGet();
            try{
                clearConsumptionWait();
                if(construction==null){finished();cancelOwner();return;}
                if(!construction.started()){construction.cancelBeforeStart();finished();cancelOwner();return;}
                if(construction.destroyed()){finished();cancelOwner();return;}
                if(view==null)throw new IOException("cdp_popup_native_constructor_unconfirmed");
                if(hostDestruction!=null){if(hostDestruction.failed())throw new IOException("cdp_popup_native_child_cleanup_unconfirmed");hostDestruction.destroy();}
                else if(disposal!=null)disposal.destroy(key,view);
                else{if(registry.owns(key,view))registry.remove(key);view.destroy();registry.viewDestroyed(view);}
                if(!construction.destroyed()&&!registry.nativeDestroyed(view)){
                    if(hostDestruction!=null&&!hostDestruction.failed()&&(!hostDestruction.ready()||!destructionAllowed(view))){awaitConsumption();return;}
                    throw new IOException("cdp_popup_native_child_cleanup_unconfirmed");
                }acknowledgeDestruction();finished();cancelOwner();
            }catch(IOException|RuntimeException|Error error){
                String code=view==null?"cdp_popup_native_constructor_unconfirmed":"cdp_popup_native_child_cleanup_unconfirmed";
                cleanupFailed=true;CREATION_UNCERTAIN.set(true);if(promotion!=null)promotion.failed.set(true);MdoBrowserCdpPopupNative original=promotion==null?owner.get():null;if(original!=null){if(original.failure==null)original.failure=code;MdoBrowserPopups<WebView> core=original.owner.get();if(core!=null&&adoption!=null)core.failChildRetirement(adoption);}cancelOwner();throw new IOException(code);
            }finally{closing=false;if(source!=null)source.disposing.decrementAndGet();}
        }
        void retryCleanup()throws IOException {thread();if(view==null&&construction!=null&&construction.started())throw new IOException("cdp_popup_native_constructor_unconfirmed");cleanupFailed=false;close();}
    }
    /** Original consumed native handoff plus its exact physical child marker.
     * No child/target selector or UI predicate crosses to the shared worker. */
    static final class Child implements AutoCloseable {
        private volatile MdoBrowserCdpPopupBroker.Handoff handoff;
        Object proofIdentity(){return identity;}boolean matchesProof(Object proof){return proof==identity;}
        void prepareHandoff(MdoBrowserCdpPopupBroker.Handoff original){if(handoff!=null)throw new IllegalStateException("cdp_popup_handoff_replayed");handoff=original;}
        MdoBrowserCdpPopupBroker.Handoff handoff(){return handoff;}
        private final Object identity=new Object();
        private final MdoBrowserPopups.Scope source;private volatile MdoBrowserCdpMarker.Lease opener;
        private final WeakReference<MdoBrowserCdpMarker> marker;private final MdoBrowserCdpMarker.Lease lease;
        private final MdoBrowserLifecycle.Fresh<WebView> birth;
        private final AtomicBoolean constructionLive;
        private volatile MdoBrowserCdpPopupWorker.Allocation allocation;
        private final AtomicBoolean live=new AtomicBoolean(true),queued=new AtomicBoolean(),attempted=new AtomicBoolean();
        private final AtomicReference<MdoBrowserCdpSession.ControlResult> proof=new AtomicReference<>();
        private Child(MdoBrowserPopups.Scope source,MdoBrowserCdpMarker.Lease opener,MdoBrowserCdpMarker marker,MdoBrowserLifecycle.Fresh<WebView> birth,MdoBrowserCdpPopupWorker.Allocation allocation,AtomicBoolean constructionLive){this.source=source;this.opener=opener;this.marker=new WeakReference<>(marker);lease=marker.lease();this.birth=birth;this.allocation=allocation;this.constructionLive=constructionLive;}
        boolean current(){MdoBrowserCdpMarker.Lease parent=opener;MdoBrowserCdpPopupWorker.Allocation captured=allocation;return creationConfirmed()&&live.get()&&(constructionLive==null||constructionLive.get())&&birth.current()&&source.current()&&parent!=null&&parent.current()&&lease.current()&&(captured==null||captured.current());}
        boolean allocated(){return allocation!=null;}
        boolean includes(MdoBrowserCdpPopupBroker.Candidate original){MdoBrowserCdpPopupWorker.Allocation captured=allocation;return captured!=null&&captured.includes(original);}
        boolean nativeDestroyed(){return birth.destroyed();}
        boolean belongsTo(MdoBrowserCdpMarker.Lease exact){return exact!=null&&opener==exact;}
        boolean belongsTo(Retirement original){return original!=null&&source==original.scope;}
        boolean ownedBy(MdoBrowserCdpPopupBroker.Registration source){MdoBrowserCdpMarker.Lease parent=opener;return source!=null&&parent!=null&&source.owns(parent);}
        MdoBrowserCdpMarker marker(){return marker.get();}
        boolean enqueue(){return current()&&queued.compareAndSet(false,true);}
        boolean claim(){return current()&&queued.get()&&attempted.compareAndSet(false,true);}
        MdoBrowserCdpSession.ControlResult proof(){return proof.get();}
        void finish(boolean ok,String code,int protocol,boolean effect){proof.compareAndSet(null,new MdoBrowserCdpSession.ControlResult(ok,code,protocol,effect));}
        @Override public void close(){live.set(false);opener=null;allocation=null;finish(false,"cdp_popup_native_child_retired",0,attempted.get());MdoBrowserCdpMarker original=marker.get();if(original!=null)original.close();else lease.invalidate();}
    }
    private final MdoBrowserCdpMarker.Lease lease;private final WeakReference<WebView> view;
    private final WeakReference<MdoBrowserPopups<WebView>> owner;private final WeakReference<MdoBrowserCdpView> adapter;
    private boolean claimed,released;private volatile MdoBrowserPopups.Scope scope;private Retirement destroyedSource;
    private volatile MdoBrowserPopups.Stop stop;private volatile String failure;
    private final AtomicBoolean rejectStarted=new AtomicBoolean();
    private static final class UiOwned implements MdoBrowserPopups.ProtocolUi {
        final Created[] constructions=new Created[7];
        WeakReference<UiRetention> retention=new WeakReference<>(null);
        public void consumed(MdoBrowserPopups.Adoption exact){for(Created child:constructions)if(child!=null)try{child.consumed(exact);}catch(IOException error){/* sticky failure; never replay the native send */}}
        public void drained(){UiRetention owner=retention.get();if(owner!=null)owner.sweep();}
    }
    private WeakReference<UiOwned> uiOwned=new WeakReference<>(null);
    private WeakReference<UiRetention> retention=new WeakReference<>(null);
    private final AtomicInteger disposing=new AtomicInteger();
    private final AtomicInteger rejecting=new AtomicInteger();
    private final AtomicInteger retiring=new AtomicInteger();
    MdoBrowserCdpPopupNative(MdoBrowserCdpView adapter,WebView exact,MdoBrowserPopups<WebView> core)throws IOException {
        thread();if(adapter==null||core==null||!core.protocolSource(exact))throw new IOException("cdp_popup_native_source_invalid");
        if(!adapter.popupCurrent(exact))throw new IOException("cdp_popup_native_source_invalid");
        lease=adapter.physicalLease(exact);view=new WeakReference<>(exact);owner=new WeakReference<>(core);this.adapter=new WeakReference<>(adapter);
    }
    private static void thread(){if(Looper.myLooper()!=Looper.getMainLooper())throw new IllegalStateException("Native popup source requires its UI owner");}
    MdoBrowserCdpMarker.Lease lease(){return lease;}
    Promotion promote(Created original,Child child)throws IOException {thread();if(original==null||original.owner.get()!=this)throw new IOException("cdp_popup_native_promotion_unavailable");return original.promote(child);}
    Promotion handoff(Created original,Child child)throws IOException {thread();if(original==null||original.owner.get()!=this||child==null||child.handoff()==null)throw new IOException("cdp_popup_handoff_unavailable");return original.promote(child,child.handoff());}
    Retirement retirement()throws IOException {thread();UiRetention original=retention.get();if(original==null||original.released||original.scope!=scope)throw new IOException("cdp_popup_native_source_invalid");return original.retirement;}
    void retainUiOwner()throws IOException {
        thread();UiRetention existing=retention.get();if(existing!=null&&!existing.released&&existing.scope==scope)return;
        MdoBrowserPopups<WebView> core=owner.get();UiOwned book=uiOwned.get();if(!current()||core==null||book==null)throw new IOException("cdp_popup_native_source_invalid");
        int slot=-1;for(int i=0;i<UI_OWNERS.length;i++)if(UI_OWNERS[i]==null){slot=i;break;}if(slot<0)throw new IOException("cdp_popup_native_owner_limit");
        UiRetention next=new UiRetention(this,core,scope,book);WeakReference<UiRetention> weak=new WeakReference<>(next);
        core.retainProtocolUi(scope);UI_OWNERS[slot]=next;retention=weak;book.retention=weak;
    }
    void claim(MdoBrowserCdpMarker.Lease original)throws IOException {
        thread();if(!creationConfirmed())throw new IOException("cdp_popup_native_creation_unconfirmed");if(original!=lease||!lease.current())throw new IOException("cdp_popup_native_source_invalid");
        if(claimed&&released)finish(true);
        if(claimed)throw new IOException("cdp_popup_native_source_busy");
        WebView exact=view.get();MdoBrowserPopups<WebView> core=owner.get();MdoBrowserCdpView nativeView=adapter.get();
        if(exact==null||core==null||nativeView==null||!nativeView.popupCurrent(exact))throw new IOException("cdp_popup_native_source_invalid");
        UiOwned ui=new UiOwned();
        try{scope=core.claimProtocol(exact,()->nativeView.popupCurrent(exact));core.attachProtocolUi(scope,ui);uiOwned=new WeakReference<>(ui);}
        catch(RuntimeException|OutOfMemoryError error){if(scope!=null)try{core.abandonProtocol(scope);}catch(RuntimeException|OutOfMemoryError ignored){}scope=null;throw new IOException("cdp_popup_native_source_busy");}
        rejectStarted.set(false);claimed=true;released=false;destroyedSource=null;
    }
    void abandon(){
        thread();MdoBrowserPopups<WebView> core=owner.get();
        if(stop==null&&failure==null&&scope!=null&&core!=null&&core.abandonProtocol(scope)){UiRetention retained=retention.get();claimed=false;scope=null;if(retained!=null)retained.ending();}
    }
    /** Called only on the original UI before exposing preparation/candidates. */
    boolean current(){thread();MdoBrowserPopups<WebView> core=owner.get();return failure==null&&creationConfirmed()&&claimed&&core!=null&&core.protocolCurrent(scope);}
    boolean ready(){thread();MdoBrowserPopups<WebView> core=owner.get();return current()&&core!=null&&core.readyProtocol(scope);}
    WebView opener()throws IOException {thread();WebView exact=view.get();if(exact==null||!current())throw new IOException("cdp_popup_native_source_invalid");return exact;}
    boolean live(){MdoBrowserPopups.Scope value=scope;return failure==null&&creationConfirmed()&&value!=null&&value.current();}
    MdoBrowserPopups.Pending pending(){thread();MdoBrowserPopups<WebView> core=owner.get();return claimed&&core!=null?core.pendingProtocol(scope):null;}
    Created construct(MdoBrowserCdpPopupWorker.Allocation allocation,MdoBrowserLifecycle<WebView> registry,String key)throws IOException {
        return construct(allocation,registry,key,null);
    }
    Created construct(MdoBrowserCdpPopupWorker.Allocation allocation,MdoBrowserLifecycle<WebView> registry,String key,Disposal disposal)throws IOException {
        thread();WebView opener=view.get();
        if(!creationConfirmed()||allocation==null||registry==null||opener==null||!current()||pending()!=allocation.pending)throw new IOException("cdp_popup_native_allocation_unavailable");
        UiOwned ui=uiOwned.get();if(ui==null)throw new IOException("cdp_popup_native_allocation_unavailable");Created[] constructions=ui.constructions;
        int slot=-1;for(int i=0;i<constructions.length;i++){Created old=constructions[i];if(old!=null&&old.construction!=null&&old.construction.destroyed())constructions[i]=null;if(constructions[i]==null)slot=i;}
        if(slot<0)throw new IOException("cdp_popup_native_child_limit");
        final android.content.Context context=opener.getContext();Created created=new Created(this,ui,registry,key,allocation,disposal);
        try{created.construction=registry.reservePopup(key,opener);}catch(RuntimeException error){throw new IOException("cdp_popup_native_child_unavailable");}
        if(!allocation.claim(this)){created.construction.cancelBeforeStart();throw new IOException("cdp_popup_native_allocation_unavailable");}
        constructions[slot]=created;
        try{
            created.construction.start();created.view=new WebView(context);
            created.birth=registry.putPopup(created.construction,created.view);
            if(!current()||!allocation.current()||pending()!=allocation.pending)throw new IllegalStateException("cdp_popup_native_allocation_unavailable");
            return created;
        }catch(RuntimeException|Error error){
            String code="cdp_popup_native_construct_failed";try{created.close();}catch(IOException cleanup){code=cleanup.getMessage();}
            if(scope==created.action)reject();throw new IOException(code);
        }
    }
    MdoBrowserPopups.Adoption adopt(Created original){
        thread();if(original==null||original.owner.get()!=this||original.allocation==null||!original.allocation.current())throw new IllegalStateException("popup_request_unavailable");
        if(original.disposal==null)throw new IllegalStateException("cdp_popup_native_disposal_required");
        if(original.done||!original.live.get()||original.sent||scope!=original.action||!current())throw new IllegalStateException("popup_request_unavailable");
        MdoBrowserPopups<WebView> core=owner.get();if(!claimed||core==null||core.pendingProtocol(scope)!=original.allocation.pending)throw new IllegalStateException("popup_request_unavailable");
        original.adoption=core.prepareProtocolAdoption(scope,original.allocation.pending,original.birth.claim(view.get()));
        original.sent=true;core.sendProtocolAdoption(scope,original.adoption);return original.adoption;
    }
    boolean owns(Created original){thread();return original!=null&&original.owner.get()==this&&original.action==scope&&!original.done&&original.live.get()&&original.promotion==null&&current();}
    boolean adoptable(Created original){thread();return owns(original)&&!original.sent&&original.allocation!=null&&original.allocation.current()&&pending()==original.allocation.pending;}
    Consumption consume(Created original,MdoBrowserPopups.Adoption exact,ConsumptionEvents events)throws IOException {
        thread();if(!owns(original))throw new IOException("cdp_popup_native_consumption_unavailable");return original.whenConsumed(exact,events);
    }
    MdoBrowserPopups.Adoption adopt(MdoBrowserPopups.Pending original,MdoBrowserLifecycle.Fresh<WebView> child){
        thread();MdoBrowserPopups<WebView> core=owner.get();
        if(!current()||core==null||original==null||core.pendingProtocol(scope)!=original||!original.current()||child==null)throw new IllegalStateException("popup_request_unavailable");
        return core.adoptProtocol(scope,original,child.claim(view.get()));
    }
    Child child(MdoBrowserPopups.Adoption original,MdoBrowserLifecycle.Fresh<WebView> birth,MdoBrowserCdpView adapter)throws IOException {
        return child(original,birth,adapter,null,null);
    }
    Child child(MdoBrowserPopups.Adoption original,Created created,MdoBrowserCdpView adapter)throws IOException {
        thread();if(created==null||created.owner.get()!=this||created.done||!created.live.get()||!created.sent||created.adoption!=original||!created.allocation.current())throw new IOException("cdp_popup_native_child_invalid");
        return child(original,created.birth,adapter,created.allocation,created.live);
    }
    private Child child(MdoBrowserPopups.Adoption original,MdoBrowserLifecycle.Fresh<WebView> birth,MdoBrowserCdpView adapter,MdoBrowserCdpPopupWorker.Allocation allocation,AtomicBoolean constructionLive)throws IOException {
        thread();try{MdoBrowserPopups<WebView> core=owner.get();
            WebView exact=birth==null?null:birth.attached(view.get());
            if(!current()||core==null||adapter==null||!adapter.popupCurrent(exact)||adapter.physicalLease(exact).busy())throw new IOException("cdp_popup_native_child_invalid");
            Child child=new Child(scope,lease,adapter.marker(),birth,allocation,constructionLive);
            if(!core.claimAdopted(scope,original,exact))throw new IOException("cdp_popup_native_child_invalid");return child;
        }catch(RuntimeException|OutOfMemoryError error){throw new IOException("cdp_popup_native_child_invalid");}
    }
    void reject(){
        thread();rejecting.incrementAndGet();rejectStarted.set(true);try{UiOwned ui=uiOwned.get();if(ui!=null)for(Created child:ui.constructions)if(child!=null)try{child.close();}catch(IOException error){/* sticky failure + exact retained resource */}
        if(stop!=null)return;
        WebView exact=view.get();MdoBrowserPopups<WebView> core=owner.get();
        if(!claimed||exact==null||core==null||!core.original(exact)){failure="cdp_popup_native_source_lost";return;}
        MdoBrowserPopups.Adoption adopted=null;if(ui!=null)for(Created child:ui.constructions)if(child!=null&&child.sent&&(child.birth==null||!child.birth.humanOwned())&&core.sentProtocolAdoption(scope,child.adoption)){adopted=child.adoption;break;}
        try{stop=core.stopForProtocol(scope,adopted);}catch(RuntimeException|OutOfMemoryError error){failure="cdp_popup_native_rejection_unconfirmed";}
        }finally{rejecting.decrementAndGet();}
    }
    boolean rejectionPending(){MdoBrowserPopups.Stop value=stop;return disposing.get()!=0||rejecting.get()!=0||retiring.get()!=0||!rejectStarted.get()&&(failure!=null||!creationConfirmed())||value!=null&&value.pending();}
    boolean confirmed(){MdoBrowserPopups.Stop value=stop;return failure==null&&!rejectionPending()&&value!=null&&value.confirmed();}
    /** Data-only reconciliation after exact physical retirement and the
     * original worker's release receipt. The UI record may already be gone;
     * no native cleanup or notification is replayed from this query. */
    private void acknowledgeDestroyedSource(){
        Retirement original=destroyedSource;
        if(original==null||original.scope!=scope||!original.requested.get()||!original.delivered.get()||!original.releaseAllowed||!original.nativeSettled.get()||failure!=null)return;
        released=true;claimed=false;scope=null;stop=null;destroyedSource=null;
    }
    /** confirmed() describes the original Stop, not historical physical release. */
    boolean released(){thread();acknowledgeDestroyedSource();return released&&!claimed&&scope==null&&stop==null&&failure==null&&!rejectionPending();}
    String failure(){
        if(failure!=null)return failure;MdoBrowserPopups.Stop value=stop;
        if(value==null||value.pending()||value.confirmed())return null;
        return "popup_native_already_adopted".equals(value.failure())?"cdp_popup_native_already_adopted":"cdp_popup_native_rejection_unconfirmed";
    }
    void finish(boolean sourceReleased){
        thread();UiRetention retained=retention.get();if(retained!=null&&retained.scope==scope)retained.ending();if(!sourceReleased)return;released=true;
        // Native destruction can finish the exact UI record before its Stop
        // acknowledgment is delivered. This is a physical retirement receipt,
        // not a successful Stop: every original native dependency must have
        // settled, and the source's saved failure is never cleared here.
        acknowledgeDestroyedSource();if(!claimed&&scope==null&&stop==null)return;
        MdoBrowserPopups.Stop value=stop;
        if(value==null){abandon();return;}
        if(confirmed()&&value.release()){claimed=false;stop=null;scope=null;destroyedSource=null;}
    }
}
