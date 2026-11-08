package org.xserver.android;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.IdentityHashMap;
import java.lang.ref.WeakReference;
import java.util.Iterator;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BooleanSupplier;
import java.util.function.LongSupplier;

/** Owns exact native view instances, not reusable tab names. UI-thread confined.
 * Lifecycle input cleanup runs before the caller detaches/destroys its view. */
final class MdoBrowserLifecycle<T> implements AutoCloseable {
    interface Factory<T> {MdoBrowserInput create(T view,LongSupplier epoch,BooleanSupplier admitted);}
    interface Invalidated<T> {void changed(String key,T view,long epoch,String reason);}
    /** A missing initial scale may be sampled after a native visual barrier.
     * This is a geometry candidate, never document/input admission. The native
     * callback retains weak identities and cannot revive a replaced document. */
    static final class InitialScale<T> {
        private final WeakReference<MdoBrowserLifecycle<T>> owner;private final WeakReference<T> view;
        private final String key;final long epoch;private boolean submitted,cancelled;
        private InitialScale(MdoBrowserLifecycle<T> owner,String key,T view,long epoch){this.owner=new WeakReference<>(owner);this.view=new WeakReference<>(view);this.key=key;this.epoch=epoch;}
        private boolean current(){
            MdoBrowserLifecycle<T> core=owner.get();T exact=view.get();if(core==null||exact==null)return false;core.thread();
            MdoBrowserLifecycle<T>.Entry entry=core.current(key,exact);
            return !cancelled&&entry!=null&&entry.epoch==epoch&&entry.initialScale==this&&!Double.isFinite(entry.scale);
        }
        boolean forView(T exact){return view.get()==exact&&current();}
        T currentView(){return current()?view.get():null;}
        boolean submit(T exact){if(submitted)return false;if(!forView(exact)){cancel();return false;}submitted=true;return true;}
        void cancel(){
            MdoBrowserLifecycle<T> core=owner.get();T exact=view.get();if(core==null||exact==null){cancelled=true;return;}core.thread();cancelled=true;
            MdoBrowserLifecycle<T>.Entry entry=core.current(key,exact);
            if(entry!=null&&entry.initialScale==this){if(entry.scaleWantedEpoch==epoch)entry.scaleWantedEpoch=0;if(!submitted)entry.initialScale=null;}
        }
        InitialScale<T> complete(java.util.function.ToDoubleFunction<T> sample){
            MdoBrowserLifecycle<T> core=owner.get();T exact=view.get();if(core==null||exact==null)return null;core.thread();
            MdoBrowserLifecycle<T>.Entry entry=core.current(key,exact);if(entry==null||entry.initialScale!=this)return null;
            double candidate=Double.NaN;boolean sampled=false;
            if(current())try{candidate=sample.applyAsDouble(exact);sampled=true;}catch(RuntimeException failure){cancelled=true;}
            boolean publish=sampled&&current();entry=core.current(key,exact);if(entry==null||entry.initialScale!=this)return null;
            entry.initialScale=null;if(entry.scaleWantedEpoch==epoch)entry.scaleWantedEpoch=0;
            if(publish)core.scaleChanged(key,exact,candidate);
            entry=core.current(key,exact);
            return entry!=null&&entry.scaleWantedEpoch==entry.epoch&&!Double.isFinite(entry.scale)?core.initialScale(key,exact):null;
        }
    }
    /** Minted only by the UI registry at construction, never from a tab ID or
     * a currently blank view. Long-lived consumers retain data/weak refs only. */
    static final class Fresh<T> {
        private final WeakReference<MdoBrowserLifecycle<T>> owner;private final WeakReference<T> view,parent;
        private final String key,parentKey;private final long parentEpoch;
        private final AtomicBoolean live=new AtomicBoolean(true),destroyed=new AtomicBoolean(),humanOwned=new AtomicBoolean();private boolean claimed;
        private volatile Independent<T> independent;
        private Fresh(MdoBrowserLifecycle<T> owner,String key,T view,String parentKey,T parent,long epoch){
            this.owner=new WeakReference<>(owner);this.key=key;this.view=new WeakReference<>(view);this.parentKey=parentKey;this.parent=new WeakReference<>(parent);parentEpoch=epoch;
        }
        boolean current(){return independent==null&&live.get()&&!destroyed.get()&&owner.get()!=null&&view.get()!=null;}
        boolean destroyed(){return destroyed.get();}
        boolean humanOwned(){return humanOwned.get();}
        T claim(T opener){MdoBrowserLifecycle<T> core=owner.get();if(core==null)throw new IllegalStateException("popup_child_owner_lost");return core.popupView(this,opener,true);}
        T attached(T opener){MdoBrowserLifecycle<T> core=owner.get();if(core==null)throw new IllegalStateException("popup_child_owner_lost");return core.popupView(this,opener,false);}
    }
    /** Physical ownership only. It carries no document/world/input authority
     * and no strong view, registry, opener or controller reference. */
    static final class Independent<T> {
        private final Fresh<T> birth;private final long preparedEpoch;private final AtomicBoolean retired=new AtomicBoolean();
        private Independent(Fresh<T> birth,long epoch){this.birth=birth;preparedEpoch=epoch;}
        boolean current(){return birth.independent==this&&!retired.get()&&!birth.destroyed.get()&&birth.owner.get()!=null&&birth.view.get()!=null;}
        boolean destroyed(){return birth.destroyed();}boolean humanOwned(){return birth.humanOwned();}
        T view(){MdoBrowserLifecycle<T> core=birth.owner.get();if(core==null)throw new IllegalStateException("popup_child_owner_lost");return core.independentView(this);}
        boolean ownsControl(T exact){MdoBrowserLifecycle<T> core=birth.owner.get();return core!=null&&core.independentControlCurrent(this,exact);}
        void takeControl(){MdoBrowserLifecycle<T> core=birth.owner.get();if(core==null)throw new IllegalStateException("popup_child_owner_lost");core.takeIndependentControl(this);}
    }
    /** UI-only capacity/key reservation, allocated before native construction.
     * Unknown constructors retain their slot; known failed views stay enumerable. */
    static final class Construction<T> {
        private final WeakReference<MdoBrowserLifecycle<T>> owner;private final String key,parentKey;
        private final WeakReference<T> parent;private final long epoch;
        private boolean started,disposed;private T nativeView;private Fresh<T> birth;
        private Construction(MdoBrowserLifecycle<T> owner,String key,String parentKey,T parent,long epoch){this.owner=new WeakReference<>(owner);this.key=key;this.parentKey=parentKey;this.parent=new WeakReference<>(parent);this.epoch=epoch;}
        boolean started(){MdoBrowserLifecycle<T> core=owner.get();if(core!=null)core.thread();return started;}
        void start(){MdoBrowserLifecycle<T> core=owner.get();if(core==null)throw new IllegalStateException("Native constructor owner lost");core.thread();
            MdoBrowserLifecycle<T>.Entry source=core.current(parentKey,parent.get());
            if(core.closed||started||disposed||core.constructing.get(key)!=this||source==null||source.epoch!=epoch||source.failed||source.hidden||source.manual||core.background)throw new IllegalStateException("Native constructor unavailable");started=true;}
        void cancelBeforeStart(){MdoBrowserLifecycle<T> core=owner.get();if(core==null)return;core.thread();
            if(started)throw new IllegalStateException("Native constructor already started");if(core.constructing.get(key)==this)core.constructing.remove(key);disposed=true;}
        boolean destroyed(){MdoBrowserLifecycle<T> core=owner.get();if(core!=null)core.thread();return disposed||birth!=null&&birth.destroyed();}
    }
    private final class Entry {
        final T view;MdoBrowserInput input;Fresh<T> fresh;InitialScale<T> initialScale;long epoch=1,scaleWantedEpoch;double scale=Double.NaN;boolean hidden,manual,failed;
        Entry(T view){this.view=view;}
    }
    private final class History {
        final WeakReference<T> view;final Fresh<T> fresh;boolean destroyed;
        History(T view,Fresh<T> fresh){this.view=new WeakReference<>(view);this.fresh=fresh;}
    }
    private final Map<String,Entry> entries=new LinkedHashMap<>();private final Factory<T> factory;private final Invalidated<T> invalidated;
    private final Map<T,Fresh<T>> retiring=new IdentityHashMap<>();private final List<History> history=new ArrayList<>();
    private final Map<String,Construction<T>> constructing=new LinkedHashMap<>();
    private final Thread owner=Thread.currentThread();private boolean closed,cleanupFailed,background;
    MdoBrowserLifecycle(Factory<T> factory){this(factory,(key,view,epoch,reason)->{});}
    MdoBrowserLifecycle(Factory<T> factory,Invalidated<T> invalidated){
        if(factory==null||invalidated==null)throw new IllegalArgumentException("Missing native input factory/observer");
        this.factory=factory;this.invalidated=invalidated;
    }
    private void thread(){if(Thread.currentThread()!=owner)throw new IllegalStateException("Browser lifecycle requires its owning UI thread");}
    private Entry current(String key,T view){Entry entry=entries.get(key);return !closed&&entry!=null&&entry.view==view?entry:null;}
    boolean owns(String key,T view){thread();return current(key,view)!=null;}
    int size(){thread();return entries.size()+retiring.size()+constructing.size();}
    T get(String key){thread();Entry entry=entries.get(key);return entry==null?null:entry.view;}
    List<T> values(){thread();List<T> result=new ArrayList<>();for(Entry entry:entries.values())result.add(entry.view);result.addAll(retiring.keySet());
        for(Construction<T> pending:constructing.values())if(pending.nativeView!=null)result.add(pending.nativeView);return result;}
    /** Call before the native constructor, including when an old tab name is
     * still reserved by a removed child whose destruction is not confirmed. */
    void checkNew(String key){
        thread();if(closed)throw new IllegalStateException("Browser lifecycle closed");
        if(key==null||key.isEmpty()||key.length()>100)throw new IllegalArgumentException("Invalid native view binding");
        if(entries.containsKey(key))throw new IllegalStateException("Browser identity is already owned");
        if(constructing.containsKey(key))throw new IllegalStateException("Native browser constructor is pending");
        for(Fresh<T> child:retiring.values())if(child.key.equals(key))throw new IllegalStateException("Native browser identity is retiring");
        if(size()>=8)throw new IllegalStateException("Native browser view limit reached (8)");
    }
    private void remember(T view,Fresh<T> fresh){
        if(view==null)throw new IllegalArgumentException("Invalid native view binding");
        for(Iterator<History> at=history.iterator();at.hasNext();){T original=at.next().view.get();if(original==null)at.remove();else if(original==view)throw new IllegalStateException("Native view was already registered");}
        if(history.size()>=256)throw new IllegalStateException("Native view history limit reached");history.add(new History(view,fresh));
    }
    void put(String key,T view){
        put(key,view,null);
    }
    /** The host invokes this immediately after its private new-view constructor,
     * before configuration/navigation/native transport. It is not an API to
     * certify arbitrary preexisting unregistered framework views as new. */
    Fresh<T> putPopup(String key,T opener,T view){
        thread();String parentKey=null;Entry parent=null;for(Map.Entry<String,Entry> item:entries.entrySet())if(item.getValue().view==opener){parentKey=item.getKey();parent=item.getValue();break;}
        if(parent==null||opener==view)throw new IllegalStateException("popup_child_opener_invalid");
        Fresh<T> child=new Fresh<>(this,key,view,parentKey,opener,parent.epoch);put(key,view,child);return child;
    }
    void popupAdmission(String key,T opener){
        checkNew(key);for(Entry parent:entries.values())if(parent.view==opener){
            if(!background&&!parent.failed&&!parent.hidden&&!parent.manual)return;break;
        }throw new IllegalStateException("popup_child_opener_invalid");
    }
    Construction<T> reservePopup(String key,T opener){
        popupAdmission(key,opener);
        for(Map.Entry<String,Entry> item:entries.entrySet())if(item.getValue().view==opener){
            Construction<T> pending=new Construction<>(this,key,item.getKey(),opener,item.getValue().epoch);constructing.put(key,pending);return pending;
        }throw new IllegalStateException("popup_child_opener_invalid");
    }
    Fresh<T> putPopup(Construction<T> pending,T view){
        thread();if(pending==null||pending.owner.get()!=this||!pending.started||pending.disposed||constructing.get(pending.key)!=pending||pending.nativeView!=null||view==null||view==pending.parent.get())throw new IllegalStateException("Native constructor binding invalid");
        // Source construction is private new, never a borrowed native view.
        for(int i=0;i<history.size();i++)if(history.get(i).view.get()==view)throw new IllegalStateException("Native view was already registered");
        pending.nativeView=view;Entry parent=current(pending.parentKey,pending.parent.get());
        if(parent==null||parent.epoch!=pending.epoch||parent.failed||parent.hidden||parent.manual||background)throw new IllegalStateException("popup_child_opener_invalid");
        Fresh<T> child=new Fresh<>(this,pending.key,view,pending.parentKey,parent.view,pending.epoch);pending.birth=child;
        put(pending.key,view,child,pending);return child;
    }
    private void put(String key,T view,Fresh<T> fresh){
        put(key,view,fresh,null);
    }
    private void put(String key,T view,Fresh<T> fresh,Construction<T> construction){
        if(construction==null)checkNew(key);else if(closed||constructing.get(key)!=construction||construction.nativeView!=view)throw new IllegalStateException("Native constructor binding lost");
        remember(view,fresh);Entry entry=new Entry(view);entry.fresh=fresh;
        // Reserve physical ownership before an input factory can fail/reenter.
        // Failed fresh registration must remain recoverable until native destroy.
        if(fresh!=null)retiring.put(view,fresh);
        if(construction!=null){constructing.remove(key);construction.nativeView=null;}
        try{
            entry.input=factory.create(view,()->entry.epoch,()->current(key,view)==entry&&!background&&!entry.hidden&&!entry.manual&&!entry.failed);
            if(entry.input==null)throw new IllegalStateException("Missing native input binding");
            if(closed||entries.containsKey(key)||fresh!=null&&(!fresh.current()||retiring.get(view)!=fresh))throw new IllegalStateException("Native view binding changed during creation");
            entry.input.observeInvalidation(reason->invalidated.changed(key,view,entry.epoch,reason));entries.put(key,entry);if(fresh!=null)retiring.remove(view);
        }catch(RuntimeException|Error failure){
            if(fresh!=null)fresh.live.set(false);
            if(entry.input!=null)try{entry.input.close();}catch(RuntimeException|Error cleanup){cleanupFailed=true;}
            throw failure;
        }
    }
    private T popupView(Fresh<T> child,T opener,boolean claim){
        thread();T view=child.view.get(),parent=child.parent.get();Entry value=current(child.key,view),source=current(child.parentKey,parent);
        if(child.owner.get()!=this||!child.current()||opener==null||parent!=opener||value==null||value.fresh!=child||source==null||source.epoch!=child.parentEpoch||
            source.failed||source.hidden||source.manual||value.failed||value.hidden||value.manual||background||claim&&(child.claimed||value.epoch!=1)||!claim&&!child.claimed)throw new IllegalStateException("popup_child_owner_lost");
        if(claim)child.claimed=true;return view;
    }
    Independent<T> prepareIndependent(Fresh<T> child,T opener,T exact){
        thread();if(child==null||popupView(child,opener,false)!=exact)throw new IllegalStateException("popup_child_owner_lost");Entry entry=current(child.key,exact);
        if(entry.input.busy()||!entry.input.cleanupConfirmed())throw new IllegalStateException("popup_child_input_busy");return new Independent<>(child,entry.epoch);
    }
    void commitIndependent(Independent<T> original,T opener,T exact){
        thread();if(original==null||popupView(original.birth,opener,false)!=exact)throw new IllegalStateException("popup_child_owner_lost");Entry entry=current(original.birth.key,exact);
        if(entry.epoch!=original.preparedEpoch||entry.input.busy()||!entry.input.cleanupConfirmed())throw new IllegalStateException("popup_child_owner_lost");original.birth.independent=original;
    }
    private T independentView(Independent<T> original){
        thread();Fresh<T> birth=original.birth;T exact=birth.view.get();Entry entry=current(birth.key,exact);
        if(!original.current()||entry==null||entry.fresh!=birth||entry.failed||entry.hidden||entry.manual||background||!entry.input.cleanupConfirmed())throw new IllegalStateException("popup_child_owner_lost");return exact;
    }
    /** Physical identity only, allowing its own unfinished input to be cancelled
     * after typed retirement starts. This never returns a view or grants input. */
    private boolean independentControlCurrent(Independent<T> original,T exact){
        thread();if(original==null||exact==null)return false;Fresh<T> birth=original.birth;Entry entry=current(birth.key,exact);
        return original.current()&&birth.view.get()==exact&&entry!=null&&entry.fresh==birth&&!entry.failed&&!entry.hidden&&!entry.manual&&!background&&
            (entry.input.busy()||entry.input.cleanupConfirmed());
    }
    private void takeIndependentControl(Independent<T> original){
        thread();if(original==null||original.birth.humanOwned())throw new IllegalStateException("popup_child_owner_lost");
        T exact=independentView(original);Fresh<T> birth=original.birth;
        manual(birth.key,exact,true);Entry entry=current(birth.key,exact);
        if(entry==null||entry.fresh!=birth||!entry.manual||!birth.humanOwned()||birth.destroyed()||entry.failed)
            throw new IllegalStateException("popup_child_owner_lost");
    }
    /** A native post-destroy acknowledgement, not registry removal. The caller
     * invokes it only after destroying this exact returned framework instance. */
    void viewDestroyed(T exact){
        thread();for(Entry entry:entries.values())if(entry.view==exact)throw new IllegalStateException("Native view is still registered");
        for(History original:history)if(original.view.get()==exact)original.destroyed=true;
        Fresh<T> child=retiring.remove(exact);if(child!=null){child.live.set(false);child.destroyed.set(true);}
        for(Iterator<Construction<T>> at=constructing.values().iterator();at.hasNext();){Construction<T> pending=at.next();if(pending.nativeView==exact){pending.disposed=true;pending.nativeView=null;if(pending.birth!=null){pending.birth.live.set(false);pending.birth.destroyed.set(true);}at.remove();}}
    }
    boolean nativeDestroyed(T exact){thread();for(History original:history)if(original.view.get()==exact)return original.destroyed||original.fresh!=null&&original.fresh.destroyed();return false;}
    private void retire(Entry entry){if(entry.fresh!=null){entry.fresh.live.set(false);if(entry.fresh.independent!=null)entry.fresh.independent.retired.set(true);retiring.put(entry.view,entry.fresh);}}
    private void settle(Entry entry,Runnable operation){
        try{operation.run();}catch(RuntimeException error){entry.failed=true;cleanupFailed=true;}
        if(!entry.input.cleanupConfirmed()&&!entry.input.busy()){entry.failed=true;cleanupFailed=true;}
    }
    boolean navigation(String key,T view){
        thread();Entry entry=current(key,view);if(entry==null)return false;
        entry.epoch++;if(entry.fresh!=null&&!entry.fresh.claimed)entry.fresh.live.set(false);settle(entry,()->entry.input.invalidate("frame_changed"));return current(key,view)==entry;
    }
    void visibility(String key,T view,boolean visible){
        thread();Entry entry=current(key,view);if(entry==null)return;entry.hidden=!visible;
        if(!visible){if(entry.fresh!=null)entry.fresh.live.set(false);settle(entry,()->entry.input.invalidate("input_view_unavailable"));}
    }
    void manual(String key,T view,boolean value){
        thread();Entry entry=current(key,view);if(entry==null)return;entry.manual=value;
        if(value){if(entry.fresh!=null){entry.fresh.humanOwned.set(true);entry.fresh.live.set(false);}settle(entry,()->entry.input.invalidate("user_taken_control"));}
    }
    void stop(String key,T view){thread();Entry entry=current(key,view);if(entry!=null)settle(entry,entry.input::cancel);}
    void stopAll(){thread();for(Entry entry:new ArrayList<>(entries.values()))settle(entry,entry.input::cancel);}
    void background(boolean value){
        thread();if(closed)return;background=value;
        if(value)for(Entry entry:new ArrayList<>(entries.values())){if(entry.fresh!=null)entry.fresh.live.set(false);settle(entry,()->entry.input.invalidate("input_view_unavailable"));}
    }
    long epoch(String key,T view){thread();Entry entry=current(key,view);return entry==null?0:entry.epoch;}
    boolean inputBusy(String key,T view){thread();Entry entry=current(key,view);return entry!=null&&entry.input.busy();}
    boolean inputClean(String key,T view){thread();Entry entry=current(key,view);return entry!=null&&!entry.failed&&entry.input.cleanupConfirmed();}
    double scale(String key,T view){thread();Entry entry=current(key,view);return entry==null?Double.NaN:entry.scale;}
    InitialScale<T> initialScale(String key,T view){
        thread();Entry entry=current(key,view);if(entry==null||Double.isFinite(entry.scale))return null;
        entry.scaleWantedEpoch=entry.epoch;
        if(entry.initialScale!=null){if(entry.initialScale.submitted||entry.initialScale.current())return null;entry.initialScale.cancel();entry.scaleWantedEpoch=entry.epoch;}
        return entry.initialScale=new InitialScale<>(this,key,view,entry.epoch);
    }
    void scaleChanged(String key,T view,double factor){
        thread();Entry entry=current(key,view);if(entry==null)return;
        if(entry.initialScale!=null)entry.initialScale.cancel();entry.scaleWantedEpoch=0;
        double next=Double.isFinite(factor)&&factor>0&&factor<=32?factor:Double.NaN;
        if(Double.compare(next,entry.scale)==0)return;entry.scale=next;
        settle(entry,()->entry.input.invalidate("input_viewport_changed"));
    }
    MdoBrowserInput input(String key,T view){
        thread();Entry entry=current(key,view);if(entry==null||entry.failed)throw new IllegalStateException("Native input binding is unavailable");return entry.input;
    }
    T remove(String key){
        thread();Entry entry=entries.remove(key);if(entry==null)return null;
        retire(entry);settle(entry,entry.input::close);return entry.view;
    }
    boolean cleanupConfirmed(){
        thread();if(cleanupFailed)return false;for(Entry entry:entries.values())if(!entry.input.cleanupConfirmed())return false;return true;
    }
    @Override public void close(){
        thread();if(closed)return;closed=true;List<Entry> owned=new ArrayList<>(entries.values());entries.clear();
        for(Fresh<T> child:retiring.values())child.live.set(false);
        for(Entry entry:owned){retire(entry);settle(entry,entry.input::close);}
    }
}
