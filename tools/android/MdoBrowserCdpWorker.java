package org.xserver.android;

import android.os.Handler;
import android.os.Looper;
import java.io.IOException;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Map;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.LongSupplier;

/** Private bounded bridge: UI admission/results, one persistent protocol worker.
 * One job per lease, eight jobs total (including posted UI results). No caller
 * runs policy, implicit reconnect or replay. Payload bytes live only through
 * the synchronous UI completion callback and are cleared on every exit. */
final class MdoBrowserCdpWorker implements AutoCloseable {
    interface Ui {boolean onThread();boolean post(Runnable task);void remove(Runnable task);}
    interface Connector {MdoBrowserCdpBinding bind(MdoBrowserCdpMarker marker,int timeoutMs)throws IOException;}
    interface Operation {byte[] run(MdoBrowserCdpBinding binding,Deadline deadline)throws IOException;}
    interface Guard {void check(MdoBrowserCdpBinding binding,Deadline deadline)throws IOException;}
    interface Completion {void done(Result result);}
    static final class Result {
        final String code;final boolean effectMayHaveOccurred;final byte[] bytes;
        Result(String code,boolean effect,byte[] bytes){this.code=code;effectMayHaveOccurred=effect;this.bytes=bytes;}
    }
    static final class Deadline {
        private final Job job;Deadline(Job job){this.job=job;}
        int remaining()throws IOException{return job.remaining(10);}
    }
    private static final int LIMIT=8,RESULT_MAX=1024*1024;
    private final Ui ui;private final Connector connector;private final LongSupplier clock;
    private final ThreadPoolExecutor executor;
    private final MdoBrowserCdpNetworks networks;
    private final AtomicBoolean closed=new AtomicBoolean();
    private final java.util.Set<Thread> nativeThreads=ConcurrentHashMap.newKeySet();
    private final AtomicBoolean exitFailed=new AtomicBoolean();
    private final Map<MdoBrowserCdpMarker.Lease,Job> jobs=new ConcurrentHashMap<>();
    private final Map<MdoBrowserCdpMarker.Lease,MdoBrowserCdpBinding> bindings=new ConcurrentHashMap<>();
    MdoBrowserCdpWorker(){this(framework(),(marker,timeout)->MdoBrowserCdpBinding.bind(marker,timeout,(method,session,params)->{}),android.os.SystemClock::elapsedRealtime);}
    MdoBrowserCdpWorker(Ui ui,Connector connector,LongSupplier clock){
        this(ui,connector,clock,MdoBrowserCdpBinding::bindNetwork);
    }
    MdoBrowserCdpWorker(Ui ui,Connector connector,LongSupplier clock,MdoBrowserCdpNetworks.Connector networkConnector){
        this(ui,connector,clock,networkConnector,MdoBrowserCdpPopupBroker::new);
    }
    MdoBrowserCdpWorker(Ui ui,Connector connector,LongSupplier clock,MdoBrowserCdpNetworks.Connector networkConnector,MdoBrowserCdpPopupWorker.Factory popupFactory){
        if(ui==null||connector==null||clock==null||!ui.onThread())throw new IllegalArgumentException("CDP worker requires its UI owner");
        this.ui=ui;this.connector=connector;this.clock=clock;
        executor=new ThreadPoolExecutor(1,1,0,TimeUnit.MILLISECONDS,new ArrayBlockingQueue<Runnable>(LIMIT*2+1),task->{
            // One executor worker slot is reserved before invoking its factory.
            // Any older NEW record therefore belongs to an unsuccessful start,
            // not another concurrently starting owner. Do not accumulate it.
            for(Thread prior:nativeThreads)if(prior.getState()==Thread.State.NEW)nativeThreads.remove(prior);
            Thread thread=new Thread(()->{
                try(MdoBrowserCdpSession.Cooperation ignored=MdoBrowserCdpSession.cooperate(this::cooperate)){task.run();}
                finally{
                    try{threadExited();}catch(RuntimeException|Error failure){exitFailed.set(true);throw failure;}
                    finally{nativeThreads.remove(Thread.currentThread());}
                }
            },"mdo-private-cdp");thread.setDaemon(true);nativeThreads.add(thread);return thread;
        },new ThreadPoolExecutor.AbortPolicy());
        networks=new MdoBrowserCdpNetworks(ui,networkConnector,clock,executor,this::shutdown,popupFactory);
    }
    private void cooperate(){networks.turn();}
    private void threadExited(){if(!closed.get())shutdown();networks.ownerTerminated();}
    void startNetwork(MdoBrowserCdpMarker marker,int timeout,MdoBrowserCdpNetworks.Events events,MdoBrowserCdpNetworks.Completion complete)throws IOException {
        thread();if(closed.get())throw new IOException("cdp_worker_closed");networks.start(marker,timeout,events,complete);
    }
    void cancelNetwork(MdoBrowserCdpMarker.Lease lease){thread();networks.cancel(lease);}
    void retireNetwork(MdoBrowserCdpMarker.Lease lease){thread();networks.retire(lease);}
    MdoBrowserCdpPopupWorker.Request preparePopups(MdoBrowserCdpMarker.Lease lease,int timeout,MdoBrowserCdpPopupWorker.Events events)throws IOException {
        thread();if(closed.get())throw new IOException("cdp_worker_closed");return networks.startPopups(lease,timeout,events);
    }
    MdoBrowserCdpPopupWorker.Request prepareNativePopups(MdoBrowserCdpPopupNative source,int timeout,MdoBrowserCdpPopupWorker.Events events)throws IOException {
        thread();if(closed.get())throw new IOException("cdp_worker_closed");if(source==null)throw new IOException("cdp_popup_native_source_invalid");
        return networks.startPopups(source.lease(),timeout,events,source);
    }
    int popupPending(){thread();return networks.popupPending();}
    MdoBrowserCdpPopupWorker.Request prepareAttachedNativePopups(MdoBrowserCdpPopupNative source,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Source proof,int timeout,MdoBrowserCdpPopupWorker.Events events)throws IOException {
        thread();if(closed.get())throw new IOException("cdp_worker_closed");return networks.startAttachedPopups(source,handoff,proof,timeout,events);
    }
    MdoBrowserCdpNetworks.Navigation navigateNetwork(MdoBrowserCdpMarker.Lease lease,String url,int timeout)throws IOException {
        thread();if(closed.get())throw new IOException("cdp_worker_closed");return networks.navigate(lease,url,timeout);
    }
    MdoBrowserCdpNetworks.Navigation traverseNetwork(MdoBrowserCdpMarker.Lease lease,String action,int timeout)throws IOException {
        thread();if(closed.get())throw new IOException("cdp_worker_closed");return networks.traverse(lease,action,timeout);
    }
    /** UI admission only. The original source pump issues once and retains
     * native ACK/commit state; no ordinary Slot, second worker or UI I/O. */
    MdoBrowserCdpNetworks.Navigation navigatePopup(MdoBrowserCdpMarker marker,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Source source,String url,String action,int timeout)throws IOException {
        thread();MdoBrowserCdpNetworks.Navigation ticket=MdoBrowserCdpNetworks.Navigation.attached(url,action,timeout,clock);
        if(marker==null||handoff==null||source==null||marker.lease()!=source.lease||!source.current()||!handoff.current()||!handoff.owns(source))throw new IOException("cdp_popup_source_owner_invalid");
        MdoBrowserCdpBinding existing=bindings.get(marker.lease());if(existing==null||!existing.matchesSource(source))throw new IOException("cdp_worker_binding_unavailable");
        PopupNavigation admission=new PopupNavigation(ticket);
        submit(marker,Math.min(timeout,5000),false,admission,admission,existing,null,handoff,null,source);return ticket;
    }
    /** This exact private operation only registers stop/receipt data. A queued
     * timeout must not retire the physical source whose wire it never touches. */
    private static final class PopupNavigation implements Operation,Completion {
        private final MdoBrowserCdpNetworks.Navigation ticket;
        PopupNavigation(MdoBrowserCdpNetworks.Navigation ticket){this.ticket=ticket;}
        public byte[] run(MdoBrowserCdpBinding binding,Deadline deadline)throws IOException {
            try{binding.startSourceNavigation(ticket);}catch(MdoBrowserCdpSession.Failure denied){ticket.admissionFailed(denied.getMessage());}return new byte[]{1};
        }
        public void done(Result result){if(result.code!=null)ticket.admissionFailed(result.code.equals("cdp_worker_timeout")?"cdp_navigation_timeout":result.code.equals("cdp_worker_cancelled")?"cdp_navigation_cancelled":result.code);}
    }
    /** Explicit fresh-read authority after natural navigation. It retains only
     * weak source ownership and native atomic document fences, not a UI graph. */
    static final class DocumentRead implements Guard {
        private final WeakReference<MdoBrowserCdpBinding> source;private final MdoBrowserCdpPopupBroker.Handoff handoff;private final MdoBrowserCdpPopupNative.Source original;
        private volatile MdoBrowserCdpBinding.CurrentDocument proof;
        private DocumentRead(MdoBrowserCdpBinding source,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Source original){this.source=new WeakReference<>(source);this.handoff=handoff;this.original=original;}
        private boolean owned(){return handoff.current()&&handoff.owns(original)&&original.current();}
        boolean current(){MdoBrowserCdpBinding.CurrentDocument current=proof;return owned()&&current!=null&&current.current();}
        public void check(MdoBrowserCdpBinding document,Deadline deadline)throws IOException {
            MdoBrowserCdpBinding binding=source.get();if(binding==null||!owned()||!binding.matchesSource(original))throw new IOException("cdp_popup_source_owner_invalid");
            proof=binding.currentDocument(document,deadline,proof);if(!current())throw new IOException("cdp_navigation_document_changed");
        }
    }
    /** A published document can lose its native contexts without a UI epoch.
     * Inspect only concurrent/volatile state; no protocol I/O on the UI thread. */
    boolean documentUnavailable(MdoBrowserCdpMarker marker,MdoBrowserCdpPopupNative.Document document){
        thread();if(closed.get()||marker==null||document==null||marker.lease()!=document.lease||!document.current()||pending(marker))return false;
        MdoBrowserCdpBinding binding=bindings.get(marker.lease());return binding==null||binding.documentUnavailable();
    }
    DocumentRead currentPopupDocument(MdoBrowserCdpMarker marker,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Source source)throws IOException {
        thread();if(closed.get())throw new IOException("cdp_worker_closed");
        if(marker==null||handoff==null||source==null||marker.lease()!=source.lease||!source.current()||!handoff.current()||!handoff.owns(source))throw new IOException("cdp_popup_source_owner_invalid");
        MdoBrowserCdpBinding binding=bindings.get(marker.lease());if(binding==null||!binding.matchesSource(source))throw new IOException("cdp_worker_binding_unavailable");
        return new DocumentRead(binding,handoff,source);
    }

    int networkPending(){thread();return networks.pending();}
    boolean networkPending(MdoBrowserCdpMarker marker){thread();return marker!=null&&networks.pending(marker.lease());}
    private static Ui framework(){
        Handler handler=new Handler(Looper.getMainLooper());return new Ui(){
            public boolean onThread(){return Looper.myLooper()==Looper.getMainLooper();}
            public boolean post(Runnable task){return handler.post(task);}
            public void remove(Runnable task){handler.removeCallbacks(task);}
        };
    }
    private void thread(){if(!ui.onThread())throw new IllegalStateException("CDP dispatch requires its UI owner");}
    void submit(MdoBrowserCdpMarker marker,int timeoutMs,boolean effect,Operation operation,Completion complete)throws IOException {
        submit(marker,timeoutMs,effect,operation,complete,null,null);
    }
    void submitDocument(MdoBrowserCdpMarker marker,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Document document,int timeout,boolean effect,Operation operation,Completion complete)throws IOException{
        thread();if(marker==null||handoff==null||document==null||marker.lease()!=document.lease||!document.current()||!handoff.current()||!handoff.owns(document))throw new IOException("cdp_popup_document_owner_invalid");
        submit(marker,timeout,effect,operation,complete,null,null,handoff,document);
    }
    void submitDocumentGuarded(MdoBrowserCdpMarker marker,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Document document,int timeout,boolean effect,Guard guard,Operation operation,Completion complete)throws IOException{
        thread();if(guard==null||marker==null||handoff==null||document==null||marker.lease()!=document.lease||!document.current()||!handoff.current()||!handoff.owns(document))throw new IOException("cdp_popup_document_owner_invalid");
        if(effect&&guard instanceof DocumentRead)throw new IOException("cdp_navigation_read_only");
        submit(marker,timeout,effect,operation,complete,null,guard,handoff,document);
    }
    void submitSource(MdoBrowserCdpMarker marker,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Source source,int timeout,Operation operation,Completion complete)throws IOException {
        thread();if(marker==null||handoff==null||source==null||marker.lease()!=source.lease||!source.current()||!handoff.current()||!handoff.owns(source))throw new IOException("cdp_popup_source_owner_invalid");
        submit(marker,timeout,false,operation,complete,null,null,handoff,null,source);
    }
    void submitGuarded(MdoBrowserCdpMarker marker,int timeoutMs,boolean effect,Guard guard,Operation operation,Completion complete)throws IOException {
        if(guard==null)throw new IOException("cdp_worker_arguments_invalid");
        submit(marker,timeoutMs,effect,operation,complete,null,guard);
    }
    /** A native continuation cannot reconnect after losing its original world. */
    void submitExisting(MdoBrowserCdpMarker marker,int timeoutMs,boolean effect,Operation operation,Completion complete)throws IOException {
        thread();if(closed.get())throw new IOException("cdp_worker_closed");MdoBrowserCdpBinding bound=marker==null?null:bindings.get(marker.lease());
        if(bound==null)throw new IOException("cdp_worker_binding_unavailable");submit(marker,timeoutMs,effect,operation,complete,bound,null);
    }
    private void submit(MdoBrowserCdpMarker marker,int timeoutMs,boolean effect,Operation operation,Completion complete,MdoBrowserCdpBinding existing,Guard guard)throws IOException {
        submit(marker,timeoutMs,effect,operation,complete,existing,guard,null,null);
    }
    private void submit(MdoBrowserCdpMarker marker,int timeoutMs,boolean effect,Operation operation,Completion complete,MdoBrowserCdpBinding existing,Guard guard,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Document document)throws IOException {
        submit(marker,timeoutMs,effect,operation,complete,existing,guard,handoff,document,null);
    }
    private void submit(MdoBrowserCdpMarker marker,int timeoutMs,boolean effect,Operation operation,Completion complete,MdoBrowserCdpBinding existing,Guard guard,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Document document,MdoBrowserCdpPopupNative.Source source)throws IOException {
        thread();if(closed.get())throw new IOException("cdp_worker_closed");
        if(marker==null||!marker.lease().current()||operation==null||complete==null||timeoutMs<100||timeoutMs>15000)
            throw new IOException("cdp_worker_arguments_invalid");
        if(marker.lease().physicalNetwork()&&source==null)throw new IOException("cdp_network_scope_invalid");
        if(jobs.containsKey(marker.lease()))throw new IOException("cdp_worker_busy");
        if(jobs.size()>=LIMIT)throw new IOException("cdp_worker_limit");
        Job job=new Job(marker,timeoutMs,effect,operation,complete,existing,guard,handoff,document,source);jobs.put(job.lease,job);
        try{executor.execute(job);}catch(RejectedExecutionException failure){jobs.remove(job.lease,job);job.drop();throw new IOException("cdp_worker_unavailable");}
        catch(RuntimeException|Error failure){jobs.remove(job.lease,job);job.drop();throw failure;}
    }
    /** UI lifecycle hook. Post a cancellation receipt; never synchronously
     * reenter the controller while it is retiring a native view. */
    void cancel(MdoBrowserCdpMarker.Lease lease){
        thread();Job job=jobs.get(lease);if(job==null)return;
        job.cancel();MdoBrowserCdpMarker marker=job.marker.get();if(marker!=null)marker.close();
        // A queued job removed from the executor has no native work to drain.
        // A running (or just dequeued) job must finish through run().finally;
        // posting its receipt now would release the UI reservation too early.
        if(executor.remove(job))job.finish(null,"cdp_worker_cancelled");
    }
    int pending(){thread();return jobs.size();}
    boolean pending(MdoBrowserCdpMarker marker){thread();return marker!=null&&jobs.containsKey(marker.lease());}
    boolean terminated(){
        if(!executor.isTerminated()||exitFailed.get())return false;
        // ThreadPoolExecutor terminates its task loop before the outer native
        // retirement finally runs. That finally, not the pool state alone,
        // proves no original worker I/O can still be running. A never-started
        // factory thread cannot own native work.
        for(Thread thread:nativeThreads)if(thread.getState()!=Thread.State.NEW)return false;
        return true;
    }
    private void prune(){
        for(Map.Entry<MdoBrowserCdpMarker.Lease,MdoBrowserCdpBinding> row:bindings.entrySet())if(!row.getKey().current()&&bindings.remove(row.getKey(),row.getValue()))row.getValue().close();
    }
    private void shutdown(){
        closed.set(true);networks.closeWorker();
        for(Job job:new ArrayList<>(jobs.values())){
            job.cancel();MdoBrowserCdpMarker marker=job.marker.get();if(marker!=null)marker.close();
            job.drop();try{ui.remove(job.delivery);}catch(RuntimeException|Error ignored){}
        }
        jobs.clear();executor.shutdownNow();
        for(MdoBrowserCdpBinding binding:bindings.values())binding.close();bindings.clear();
    }
    @Override public void close(){thread();shutdown();}
    private final class Job implements Runnable {
        final WeakReference<MdoBrowserCdpMarker> marker;final MdoBrowserCdpMarker.Lease lease;
        final int timeout;final long started;final boolean effect,admissionOnly;
        final MdoBrowserCdpBinding existing;
        final MdoBrowserCdpPopupBroker.Handoff handoff;final MdoBrowserCdpPopupNative.Document document;final MdoBrowserCdpPopupNative.Source source;
        final AtomicBoolean cancelled=new AtomicBoolean();final AtomicReference<Completion> callback;
        final Runnable delivery=this::deliver;
        private Operation operation;private Guard guard;private byte[] bytes;private String code;private boolean posted,delivered;
        private volatile boolean entered;
        Job(MdoBrowserCdpMarker marker,int timeout,boolean effect,Operation operation,Completion callback,MdoBrowserCdpBinding existing,Guard guard,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Document document,MdoBrowserCdpPopupNative.Source source){
            this.marker=new WeakReference<>(marker);lease=marker.lease();this.timeout=timeout;this.effect=effect;
            this.operation=operation;this.guard=guard;this.callback=new AtomicReference<>(callback);started=clock.getAsLong();this.existing=existing;
            admissionOnly=existing!=null&&operation instanceof PopupNavigation;
            this.handoff=handoff;this.document=document;this.source=source;
        }
        int remaining(int minimum)throws IOException {
            if(closed.get()||cancelled.get()||!lease.current()||document!=null&&!document.current()||source!=null&&(!source.current()||!handoff.current()))throw new IOException("cdp_worker_cancelled");
            long used=clock.getAsLong()-started;if(used<0||used>timeout-minimum)throw new IOException("cdp_worker_timeout");
            return timeout-(int)used;
        }
        private void retireBinding(){MdoBrowserCdpBinding bound=bindings.remove(lease);if(bound!=null)bound.close();}
        @Override public void run(){
            byte[] value=null;String failure=null;
            try{
                prune();remaining(100);MdoBrowserCdpBinding binding=bindings.get(lease);
                if(existing!=null){if(binding!=existing)throw new IOException("cdp_worker_binding_changed");binding.current();}
                else if(binding==null){
                    MdoBrowserCdpMarker owned=marker.get();if(owned==null)throw new IOException("cdp_binding_owner_changed");
                    if(document==null&&source==null&&lease.attached())throw new IOException("cdp_attached_binding_required");
                    binding=source!=null?networks.bindPopupSource(handoff,source,owned,remaining(100)):document==null?connector.bind(owned,remaining(100)):networks.bindPopupDocument(handoff,document,owned,remaining(100));
                    if(binding==null)throw new IOException("cdp_worker_binding_invalid");
                    bindings.put(lease,binding);remaining(10);
                }
                Operation work;Guard fence;synchronized(this){
                    remaining(10);work=operation;operation=null;fence=guard;guard=null;
                    if(work==null)throw new IOException("cdp_worker_cancelled");
                }
                Deadline deadline=new Deadline(this);if(fence!=null)fence.check(binding,deadline);
                synchronized(this){remaining(10);entered=true;}
                value=work.run(binding,deadline);remaining(10);if(fence!=null)fence.check(binding,deadline);remaining(10);
                if(value==null||value.length>RESULT_MAX)throw new IOException("cdp_worker_result_invalid");
            }catch(IOException error){failure=safe(error.getMessage());}
            catch(RuntimeException|Error error){failure="cdp_worker_failed";}
            finally{
                synchronized(this){operation=null;guard=null;}
                if(failure!=null){if(!admissionOnly)retireBinding();clear(value);value=null;}
                finish(value,failure);
            }
        }
        private String safe(String value){return value!=null&&value.matches("cdp_[a-z_]{1,80}")?value:"cdp_worker_failed";}
        private void finish(byte[] value,String failure){
            boolean accepted;
            synchronized(this){
                if(delivered||posted){clear(value);return;}
                if(cancelled.get()||closed.get()||!lease.current()){clear(value);value=null;failure="cdp_worker_cancelled";}
                bytes=value;code=failure;posted=true;
                try{accepted=ui.post(delivery);}catch(RuntimeException|Error error){accepted=false;}
            }
            if(!accepted)shutdown();
        }
        private void deliver(){
            thread();byte[] value;String failure;
            synchronized(this){if(delivered)return;delivered=true;value=bytes;bytes=null;failure=code;operation=null;}
            jobs.remove(lease,this);Completion complete=callback.getAndSet(null);
            try{
                if(closed.get()||complete==null)return;
                if(cancelled.get()||!lease.current()){clear(value);value=null;failure="cdp_worker_cancelled";}
                complete.done(new Result(failure,entered&&effect,value));
            }catch(RuntimeException|Error callbackFailure){
                MdoBrowserCdpMarker owned=marker.get();if(owned!=null)owned.close();retireBinding();
            }finally{clear(value);}
        }
        private synchronized void cancel(){cancelled.set(true);operation=null;guard=null;}
        private synchronized void drop(){delivered=true;operation=null;guard=null;callback.set(null);clear(bytes);bytes=null;}
    }
    private static void clear(byte[] value){if(value!=null)Arrays.fill(value,(byte)0);}
}
