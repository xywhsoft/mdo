package org.xserver.android;

import java.lang.ref.WeakReference;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.BooleanSupplier;
import java.util.function.LongSupplier;

/** One retained native pending window per exact opener, as required by WebView.
 * UI-thread confined. Sending a transport is irreversible; a same-queue barrier
 * acknowledges consumption, not successful navigation or document readiness. */
final class MdoBrowserPopups<T> implements AutoCloseable {
    interface Scheduler {long now();void at(Runnable task,long uptime);void remove(Runnable task);}
    interface Transport<T> {Object token();void send(T child,Runnable consumed);}
    interface Completion {void complete(boolean ok,String code,boolean mayHaveOccurred);}
    interface Listener {void blocked(boolean value);}
    static final class Budget {
        private int used;final int limit;
        Budget(int limit){if(limit<1||limit>8)throw new IllegalArgumentException("Invalid popup budget");this.limit=limit;}
        private boolean take(){if(used>=limit)return false;used++;return true;}
        private void release(){if(used<=0)throw new IllegalStateException("Popup budget underflow");used--;}
        int used(){return used;}
    }
    static final class Snapshot {
        final String id;final long epoch;final boolean userInitiated;
        Snapshot(String id,long epoch,boolean userInitiated){this.id=id;this.epoch=epoch;this.userInitiated=userInitiated;}
    }
    /** Opaque original action generation. Worker-visible fields contain no UI
     * predicate, native transport, view or owning controller reference. */
    static final class Scope {
        private final WeakReference<MdoBrowserPopups<?>> owner;private final long epoch;
        private final AtomicBoolean live=new AtomicBoolean(true);private boolean ready;
        private Scope(MdoBrowserPopups<?> owner,long epoch){this.owner=new WeakReference<>(owner);this.epoch=epoch;}
        boolean current(){return live.get();}
    }
    static final class Pending {
        private final Scope scope;private final AtomicBoolean live=new AtomicBoolean(true);
        final String id;final long epoch;final boolean userInitiated;
        private Pending(Scope scope,Snapshot value){this.scope=scope;id=value.id;epoch=value.epoch;userInitiated=value.userInitiated;}
        boolean current(){return live.get()&&scope.current();}
    }
    /** One original native send/consumption receipt, not a CDP target, fresh
     * child ownership, document readiness or isolated-world authorization. */
    static final class Adoption {
        private final Pending original;private final WeakReference<Object> child;
        private final AtomicInteger state=new AtomicInteger();private volatile String code;private volatile boolean consumed,dispatched,childDestroyed;private boolean childClaimed;
        private Adoption(Pending original,Object child){this.original=original;this.child=new WeakReference<>(child);}
        boolean pending(){return state.get()==0;}
        boolean ok(){return state.get()==1;}
        boolean nativeConsumed(){return consumed;}
        boolean dispatched(){return dispatched;}
        String failure(){return code;}
        boolean mayHaveOccurred(){return dispatched;}
        private void finish(boolean ok,String reason){if(state.get()!=0)return;code=reason;state.compareAndSet(0,ok?1:2);}
    }
    interface ProtocolUi {void consumed(Adoption original);default void drained(){}}
    private final class Protocol {
        final Scope scope;final BooleanSupplier owned;ProtocolUi uiAttachment;boolean retained;
        Protocol(Scope scope,BooleanSupplier owned){this.scope=scope;this.owned=owned;}
    }
    /** Data-only rejection evidence for the protocol worker. Only this native
     * controller can complete it; a queue barrier is not debugger detachment. */
    static final class Stop {
        private final WeakReference<MdoBrowserPopups<?>> owner;private final AtomicInteger state=new AtomicInteger();
        private volatile String code;private final Scope scope;private final Adoption retirement;
        private Stop(MdoBrowserPopups<?> owner,Scope scope){this(owner,scope,null);}
        private Stop(MdoBrowserPopups<?> owner,Scope scope,Adoption retirement){this.owner=new WeakReference<>(owner);this.scope=scope;this.retirement=retirement;}
        boolean pending(){return state.get()==0;}
        boolean confirmed(){return state.get()==1;}
        String failure(){return code==null?"popup_native_rejection_unconfirmed":code;}
        private void confirm(){state.compareAndSet(0,1);}
        private void fail(String value){code=value;state.compareAndSet(0,2);}
        boolean release(){MdoBrowserPopups<?> core=owner.get();return core==null||core.releaseStop(this);}
    }
    private final class Entry {
        final Transport<T> transport;final Snapshot snapshot;final Pending pending;long expires;final Runnable timer,consumed;
        Completion completion;Stop stop;Adoption adoption;boolean sent,adopted,invalidated,timedOut;
        Entry(Transport<T> transport,long epoch,boolean userInitiated,Scope scope){
            this.transport=transport;snapshot=new Snapshot(UUID.randomUUID().toString(),epoch,userInitiated);
            pending=scope==null?null:new Pending(scope,snapshot);
            expires=scheduler.now()+15000;
            WeakReference<MdoBrowserPopups<T>> weak=new WeakReference<>(MdoBrowserPopups.this);
            String id=snapshot.id;
            timer=()->{MdoBrowserPopups<T> core=weak.get();if(core!=null)core.expire(id);};
            consumed=()->{MdoBrowserPopups<T> core=weak.get();if(core!=null)core.consumed(id);};
        }
    }
    private final T opener;private final Scheduler scheduler;private final Budget budget;
    private final BooleanSupplier owned;private final LongSupplier navigation;private final Listener listener;
    private final Thread owner=Thread.currentThread();private Entry entry;private Protocol protocol;private Stop protocolStop;private boolean protocolStopping,closed,faulted,destroyed;
    MdoBrowserPopups(T opener,Scheduler scheduler,Budget budget,BooleanSupplier owned,LongSupplier navigation,Listener listener){
        if(opener==null||scheduler==null||budget==null||owned==null||navigation==null||listener==null)throw new IllegalArgumentException("Missing popup binding");
        this.opener=opener;this.scheduler=scheduler;this.budget=budget;this.owned=owned;this.navigation=navigation;this.listener=listener;
    }
    private void thread(){if(Thread.currentThread()!=owner)throw new IllegalStateException("Popup requires its owning UI thread");}
    private boolean current(Entry value){
        // After admission the caller must still return true to WebView. A host
        // ownership query can fail; do not escape as a fresh framework rejection
        // while retaining a timer that could later consume another native popup.
        try{return !closed&&!destroyed&&owned.getAsBoolean()&&navigation.getAsLong()==value.snapshot.epoch&&
            (value.pending==null||protocolCurrent(value.pending.scope));}
        catch(RuntimeException|OutOfMemoryError error){faulted=true;return false;}
    }
    private void blocked(boolean value){try{listener.blocked(value);}catch(RuntimeException|OutOfMemoryError error){faulted=true;}}
    /** False leaves the unaccepted transport with WebView's own rejection path. */
    boolean open(Transport<T> transport,boolean userInitiated){
        thread();if(transport==null)return false;
        if(entry!=null&&entry.transport.token()==transport.token())return true;
        if(closed||destroyed||faulted||protocolStopping||entry!=null||!owned.getAsBoolean())return false;
        long epoch=navigation.getAsLong();if(epoch<=0)return false;
        Scope scope=null;
        if(protocol!=null){
            boolean actionOwned=protocolOwner(protocol.scope);if(faulted)return false;
            if(actionOwned){scope=protocol.scope;if(!scope.current()||!scope.ready)return false;}
            else protocol.scope.live.set(false);
        }
        Entry next;
        try{next=new Entry(transport,epoch,userInitiated,scope);}catch(RuntimeException|OutOfMemoryError error){return false;}
        if(!budget.take())return false;
        entry=next;blocked(true);
        try{scheduler.at(next.timer,next.expires);}catch(RuntimeException|OutOfMemoryError error){faulted=true;reject(next);return true;}
        if(faulted||!current(next))reject(next);
        return true;
    }
    Snapshot snapshot(){
        thread();if(entry==null)return null;
        if(!entry.sent&&(!current(entry)||scheduler.now()>=entry.expires))reject(entry);
        return entry==null||entry.sent?null:entry.snapshot;
    }
    void adopt(String id,T child,Completion completion){
        thread();Snapshot pending=snapshot();
        if(child==null||child==opener||pending==null||!pending.id.equals(id)||completion==null||entry.pending!=null)
            throw new IllegalStateException("popup_request_unavailable");
        Entry value=entry;value.completion=completion;send(value,child);
    }
    boolean dismiss(String id){
        thread();if(entry==null||!entry.snapshot.id.equals(id)||entry.sent)return false;reject(entry);return true;
    }
    private void reject(Entry value){if(entry==value&&!value.sent){value.invalidated=true;send(value,null);}}
    private void send(Entry value,T child){
        // Claim before entering framework code or application callbacks. Neither
        // exceptions nor reentrancy authorize sending this Message a second time.
        value.sent=true;value.adopted=child!=null;if(value.pending!=null)value.pending.live.set(false);
        value.expires=scheduler.now()+5000;
        try{scheduler.remove(value.timer);scheduler.at(value.timer,value.expires);}
        catch(RuntimeException|OutOfMemoryError error){faulted=true;}
        try{value.transport.send(child,value.consumed);}
        catch(RuntimeException|OutOfMemoryError error){faulted=true;if(value.stop!=null)value.stop.fail("popup_transport_unconfirmed");finish(value,false,"popup_transport_unconfirmed",child!=null);}
    }
    private void finish(Entry value,boolean ok,String code,boolean mayHaveOccurred){
        if(value.adoption!=null)value.adoption.finish(ok,code);
        Completion callback=value.completion;value.completion=null;
        if(callback!=null)try{callback.complete(ok,code,mayHaveOccurred);}catch(RuntimeException|OutOfMemoryError error){faulted=true;}
    }
    private void consumed(String id){
        thread();Entry value=entry;if(value==null||!value.snapshot.id.equals(id)||!value.sent)return;
        Protocol original=protocol;
        if(value.adopted&&value.adoption!=null&&value.adoption.dispatched)value.adoption.consumed=true;
        // A queued timeout may run after the native acknowledgement. Receipt
        // validity depends on its original deadline, not Handler queue order.
        try{if(scheduler.now()>=value.expires){value.timedOut=true;faulted=true;}}
        catch(RuntimeException|OutOfMemoryError error){value.timedOut=true;faulted=true;}
        if(entry!=value)return;
        boolean valid=!value.invalidated&&!value.timedOut&&current(value)&&!faulted;
        entry=null;budget.release();
        try{scheduler.remove(value.timer);}catch(RuntimeException|OutOfMemoryError error){faulted=true;valid=false;}
        blocked(false);valid=valid&&!faulted;
        if(value.stop!=null){
            if(value.timedOut||faulted)value.stop.fail("popup_native_rejection_unconfirmed");
            else if(value.stop.retirement!=null&&value.stop.retirement==value.adoption&&value.adopted)confirmRetirement(value.stop);
            else if(!value.adopted)value.stop.confirm();else value.stop.fail("popup_native_rejection_unconfirmed");
        }
        finish(value,valid,valid?null:value.timedOut?"popup_transport_unconfirmed":"popup_source_changed",true);
        if(value.adoption!=null&&value.adoption.nativeConsumed()&&original!=null&&original.scope==value.adoption.original.scope&&original.uiAttachment!=null)
            try{original.uiAttachment.consumed(value.adoption);}catch(RuntimeException|OutOfMemoryError error){faulted=true;if(value.stop!=null)value.stop.fail("popup_native_rejection_unconfirmed");}
        if(original!=null&&original.uiAttachment!=null)try{original.uiAttachment.drained();}
            catch(RuntimeException|OutOfMemoryError error){faulted=true;if(value.stop!=null)value.stop.fail("popup_native_rejection_unconfirmed");}
    }
    private void expire(String id){
        thread();Entry value=entry;if(value==null||!value.snapshot.id.equals(id))return;
        if(scheduler.now()<value.expires){try{scheduler.at(value.timer,value.expires);}catch(RuntimeException|OutOfMemoryError error){faulted=true;reject(value);}return;}
        if(!value.sent){reject(value);return;}
        // A missing barrier is uncertainty, not permission to return its native
        // resource reservation or replay a popup. Exact teardown can release it.
        value.timedOut=true;faulted=true;if(value.stop!=null)value.stop.fail("popup_transport_unconfirmed");finish(value,false,"popup_transport_unconfirmed",true);
    }
    boolean protocolSource(T exact){thread();return exact==opener&&!closed&&!destroyed&&!faulted&&!protocolStopping;}
    boolean original(T exact){thread();return exact==opener;}
    Scope claimProtocol(T exact,BooleanSupplier actionOwner){
        thread();if(!protocolSource(exact)||protocol!=null||entry!=null||actionOwner==null)throw new IllegalStateException("popup_native_scope_unavailable");
        long epoch=navigation.getAsLong();if(epoch<=0||!owned.getAsBoolean()||!actionOwner.getAsBoolean())throw new IllegalStateException("popup_source_changed");
        Scope next=new Scope(this,epoch);protocol=new Protocol(next,actionOwner);return next;
    }
    void attachProtocolUi(Scope exact,ProtocolUi attachment){
        thread();if(attachment==null||!protocolCurrent(exact)||protocol.uiAttachment!=null)throw new IllegalStateException("popup_native_scope_unavailable");protocol.uiAttachment=attachment;
    }
    void retainProtocolUi(Scope exact){
        thread();if(closed||destroyed||protocol==null||protocol.scope!=exact||!exact.current()||protocol.uiAttachment==null||protocol.retained)throw new IllegalStateException("popup_native_scope_unavailable");protocol.retained=true;
    }
    boolean protocolHasObligations(Scope exact){thread();return entry!=null&&entry.pending!=null&&entry.pending.scope==exact;}
    void retireProtocolUi(Scope exact){thread();if(protocol!=null&&protocol.scope==exact)exact.live.set(false);}
    void releaseProtocolUi(Scope exact){
        thread();if(protocol!=null&&protocol.scope==exact){protocol.retained=false;if(closed&&entry==null){exact.live.set(false);protocol=null;}}
    }
    boolean protocolCurrent(Scope scope){
        thread();if(scope==null||scope.owner.get()!=this||protocol==null||protocol.scope!=scope||!scope.current())return false;
        boolean valid=protocolOwner(scope);if(!valid)scope.live.set(false);return valid;
    }
    private boolean protocolOwner(Scope scope){
        boolean valid;try{valid=!closed&&!destroyed&&!faulted&&owned.getAsBoolean()&&navigation.getAsLong()==scope.epoch&&protocol.owned.getAsBoolean();}
        catch(RuntimeException|OutOfMemoryError error){faulted=true;valid=false;}
        return valid;
    }
    boolean readyProtocol(Scope scope){thread();if(!protocolCurrent(scope))return false;scope.ready=true;return true;}
    boolean abandonProtocol(Scope scope){
        thread();if(scope==null||protocol==null||protocol.scope!=scope||protocolStop!=null||entry!=null&&entry.pending!=null&&entry.pending.scope==scope)return false;
        scope.live.set(false);protocol=null;return true;
    }
    Pending pendingProtocol(Scope scope){
        thread();if(!protocolCurrent(scope)||!scope.ready)return null;
        Snapshot snapshot=snapshot();return snapshot!=null&&entry.pending!=null&&entry.pending.scope==scope?entry.pending:null;
    }
    Adoption adoptProtocol(Scope scope,Pending pending,T child){
        Adoption receipt=prepareProtocolAdoption(scope,pending,child);sendProtocolAdoption(scope,receipt);return receipt;
    }
    Adoption prepareProtocolAdoption(Scope scope,Pending pending,T child){
        thread();Pending original=pendingProtocol(scope);
        if(pending==null||pending!=original||!pending.current()||child==null||child==opener||entry.adoption!=null)throw new IllegalStateException("popup_request_unavailable");
        Adoption receipt=new Adoption(pending,child);entry.adoption=receipt;return receipt;
    }
    @SuppressWarnings("unchecked") // The exact original Pending/Entry joins the generic child before dispatch.
    void sendProtocolAdoption(Scope scope,Adoption original){
        thread();Pending pending=pendingProtocol(scope);T child=original==null?null:(T)original.child.get();
        if(original==null||pending!=original.original||!pending.current()||entry.adoption!=original||original.dispatched||child==null||child==opener)
            throw new IllegalStateException("popup_request_unavailable");
        original.dispatched=true;send(entry,child);
    }
    boolean sentProtocolAdoption(Scope scope,Adoption original){
        thread();return scope!=null&&protocol!=null&&protocol.scope==scope&&original!=null&&original.original.scope==scope&&
            entry!=null&&entry.adoption==original&&entry.sent&&entry.adopted&&original.dispatched;
    }
    void nativeChildDestroyed(Adoption original,T exact){
        thread();if(exact==null||!adoptedTo(original,exact)||!original.dispatched)throw new IllegalStateException("popup_child_owner_lost");original.childDestroyed=true;
        if(protocolStop!=null&&protocolStop.retirement==original)confirmRetirement(protocolStop);
    }
    void failChildRetirement(Adoption original){
        thread();if(protocolStop!=null&&protocolStop.retirement==original)protocolStop.fail("popup_native_child_cleanup_unconfirmed");
    }
    private void confirmRetirement(Stop stop){
        if(faulted||destroyed){stop.fail("popup_native_rejection_unconfirmed");return;}
        if(stop.retirement!=null&&stop.retirement.nativeConsumed()&&stop.retirement.childDestroyed)stop.confirm();
    }
    boolean adoptedTo(Adoption original,T exact){
        thread();return original!=null&&original.original.scope.owner.get()==this&&original.child.get()==exact;
    }
    boolean claimAdopted(Scope scope,Adoption original,T exact){
        thread();if(!protocolCurrent(scope)||original==null||original.original.scope!=scope||!original.ok()||!original.nativeConsumed()||
            !adoptedTo(original,exact)||original.childClaimed)return false;
        original.childClaimed=true;return true;
    }
    Stop stopForProtocol(Scope scope){
        return stopForProtocol(scope,null);
    }
    Stop stopForProtocol(Scope scope,Adoption retirement){
        thread();if(scope==null||protocol==null||protocol.scope!=scope||protocolStop!=null)throw new IllegalStateException("popup_native_scope_unavailable");
        if(retirement!=null&&!sentProtocolAdoption(scope,retirement))throw new IllegalStateException("popup_native_scope_unavailable");
        boolean stillOwned=protocolOwner(scope);scope.live.set(false);
        Entry value=entry;boolean original=value!=null&&value.pending!=null&&value.pending.scope==scope;
        // A retired task must never send a later human callback's Message.
        protocolStopping=stillOwned||original;Stop stop=new Stop(this,scope,retirement);protocolStop=stop;
        if(faulted||destroyed){stop.fail("popup_native_rejection_unconfirmed");return stop;}
        if(!original){stop.confirm();return stop;}
        stopEntry(value,stop);return stop;
    }
    Stop stopForProtocol(){
        thread();if(protocolStopping||protocol!=null)throw new IllegalStateException("popup_native_stop_busy");
        // Keep admission held even if allocating the receipt itself fails.
        protocolStopping=true;Stop stop=new Stop(this,null);protocolStop=stop;Entry value=entry;
        if(faulted||destroyed){stop.fail("popup_native_rejection_unconfirmed");return stop;}
        if(value==null){stop.confirm();return stop;}
        stopEntry(value,stop);return stop;
    }
    private void stopEntry(Entry value,Stop stop){
        value.stop=stop;
        if(value.sent&&value.adopted){if(stop.retirement==null||stop.retirement!=value.adoption)stop.fail("popup_native_already_adopted");else confirmRetirement(stop);return;}
        value.invalidated=true;if(!value.sent)reject(value);
    }
    private boolean releaseStop(Stop stop){
        thread();if(protocolStop!=stop||!stop.confirmed()||entry!=null&&(stop.scope==null||entry.pending!=null&&entry.pending.scope==stop.scope))return false;
        if(stop.scope!=null){
            // Exact native destruction may already have removed this protocol.
            // The original confirmed Stop still has to acknowledge its release.
            // A closed controller, foreign Stop or failed receipt is insufficient.
            if(protocol==null){if(!destroyed)return false;}
            else{if(protocol.scope!=stop.scope)return false;protocol=null;}
        }
        protocolStop=null;protocolStopping=false;return true;
    }
    void cancel(){thread();if(protocol!=null)protocol.scope.live.set(false);if(entry!=null){entry.invalidated=true;if(!entry.sent)reject(entry);}}
    boolean clean(){thread();return !faulted;}
    boolean busy(){thread();return entry!=null;}
    @Override public void close(){thread();if(closed)return;closed=true;if(protocol!=null)protocol.scope.live.set(false);cancel();}
    void viewDestroyed(T exact){
        thread();if(exact!=opener||destroyed)return;destroyed=true;closed=true;
        if(protocol!=null&&protocol.retained&&protocolHasObligations(protocol.scope)){
            protocol.scope.live.set(false);entry.invalidated=true;entry.pending.live.set(false);
            if(protocolStop!=null)protocolStop.fail("popup_source_destroyed");
            finish(entry,false,"popup_source_destroyed",entry.sent);blocked(false);return;
        }
        Entry value=entry;entry=null;if(protocol!=null){protocol.scope.live.set(false);protocol=null;}
        if(protocolStop!=null)protocolStop.fail("popup_source_destroyed");
        if(value!=null){if(value.pending!=null)value.pending.live.set(false);budget.release();try{scheduler.remove(value.timer);}catch(RuntimeException|OutOfMemoryError ignored){}
            finish(value,false,"popup_source_destroyed",value.sent);}
        faulted=false;blocked(false);
    }
}
