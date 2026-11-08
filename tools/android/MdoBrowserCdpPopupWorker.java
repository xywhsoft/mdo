package org.xserver.android;

import java.io.IOException;
import java.lang.ref.WeakReference;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;
import java.util.function.LongSupplier;

/** UI admission/results and process popup service on the existing executor.
 * No caller-runs fallback, extra thread, UI I/O or managed-child authority. */
final class MdoBrowserCdpPopupWorker {
    interface Factory {MdoBrowserCdpPopupBroker create(BooleanSupplier current,LongSupplier clock);}
    interface Source {MdoBrowserCdpBinding.Network resolve(MdoBrowserCdpMarker.Lease lease)throws IOException;}
    interface Events {void state(Result result);void candidate(MdoBrowserCdpPopupBroker.Candidate candidate);default void protection(MdoBrowserCdpPopupBroker.Candidate candidate){}
        default void nativeProof(MdoBrowserCdpPopupNative.Child child){}}
    /** Original guarded captures that already existed before native construction.
     * Data-only, bounded and single-use; it cannot name a later target or view. */
    static final class Allocation {
        private final WeakReference<MdoBrowserCdpPopupNative> owner;private final MdoBrowserCdpMarker.Lease lease;
        final MdoBrowserPopups.Pending pending;private final MdoBrowserCdpPopupBroker.Registration registration;
        private final MdoBrowserCdpPopupBroker.Candidate[] targets;private final BooleanSupplier scope;
        private final AtomicBoolean claimed=new AtomicBoolean();
        private Allocation(MdoBrowserCdpPopupNative owner,MdoBrowserCdpMarker.Lease lease,MdoBrowserPopups.Pending pending,
                           MdoBrowserCdpPopupBroker.Registration registration,List<MdoBrowserCdpPopupBroker.Candidate> targets,BooleanSupplier scope){
            this.owner=new WeakReference<>(owner);this.lease=lease;this.pending=pending;this.registration=registration;
            this.targets=targets.toArray(new MdoBrowserCdpPopupBroker.Candidate[0]);this.scope=scope;
        }
        boolean scopeCurrent(){return scope.getAsBoolean()&&registration.current();}
        boolean current(){if(!scopeCurrent())return false;
            for(MdoBrowserCdpPopupBroker.Candidate candidate:targets)if(candidate.current()&&candidate.mayBelongTo(registration))return true;return false;}
        boolean claim(MdoBrowserCdpPopupNative exact){return owner.get()==exact&&exact.lease()==lease&&pending.current()&&current()&&claimed.compareAndSet(false,true);}
        boolean includes(MdoBrowserCdpPopupBroker.Candidate original){for(MdoBrowserCdpPopupBroker.Candidate candidate:targets)if(candidate==original)return true;return false;}
    }
    static final class Result {
        final boolean ready,terminal,processMayHaveBeenEnabled,sourceReleased,processCleanupConfirmed;final String code;
        private Result(boolean ready,boolean terminal,String code,boolean enabled,boolean released,boolean clean){
            this.ready=ready;this.terminal=terminal;this.code=code;processMayHaveBeenEnabled=enabled;sourceReleased=released;processCleanupConfirmed=clean;
        }
    }
    static final class Request implements AutoCloseable {
        private final AtomicBoolean cancelled=new AtomicBoolean();private final AtomicReference<Result> result=new AtomicReference<>();private volatile Runnable wake;
        private final MdoBrowserCdpMarker.Lease lease;private final ArrayDeque<MdoBrowserCdpPopupNative.Child> nativeQueue=new ArrayDeque<>();
        private final Thread uiOwner=Thread.currentThread();private final WeakReference<MdoBrowserCdpPopupNative> nativeSource;
        private MdoBrowserCdpPopupNative.Retirement retirement;
        private final MdoBrowserCdpPopupBroker.Handoff parent;private final MdoBrowserCdpPopupNative.Source parentProof;
        private final List<MdoBrowserCdpPopupBroker.Candidate> captured=new ArrayList<>();private MdoBrowserCdpPopupBroker.Registration captureOwner;private Allocation allocation;
        private Request(Runnable wake,MdoBrowserCdpMarker.Lease lease,MdoBrowserCdpPopupNative source,MdoBrowserCdpPopupBroker.Handoff parent,MdoBrowserCdpPopupNative.Source parentProof){this.wake=wake;this.lease=lease;nativeSource=new WeakReference<>(source);this.parent=parent;this.parentProof=parentProof;}
        private boolean parentCurrent(){return parent==null||parent.current()&&parent.owns(parentProof)&&parentProof.current();}
        synchronized Allocation allocateNative(MdoBrowserCdpPopupNative original)throws IOException {
            if(Thread.currentThread()!=uiOwner)throw new IOException("cdp_requires_ui_thread");
            Result value=result.get();if(original==null||nativeSource.get()!=original||original.lease()!=lease||!parentCurrent()||cancelled.get()||value==null||!value.ready||value.terminal||retirement==null||!retirement.current()||!retirement.belongsTo(original)||!original.current())throw new IOException("cdp_popup_native_allocation_unavailable");
            MdoBrowserPopups.Pending pending=original.pending();if(pending==null||!pending.current())throw new IOException("cdp_popup_native_allocation_unavailable");
            if(allocation!=null&&allocation.pending==pending){if(allocation.claimed.get())throw new IOException("cdp_popup_native_allocation_replayed");if(!allocation.current())throw new IOException("cdp_popup_native_allocation_unavailable");return allocation;}
            List<MdoBrowserCdpPopupBroker.Candidate> frozen=new ArrayList<>();
            for(MdoBrowserCdpPopupBroker.Candidate candidate:captured)if(candidate.current()&&candidate.mayBelongTo(captureOwner)&&candidate.unbound()){
                MdoBrowserCdpSession.ControlResult guard=candidate.protection();if(guard==null||!guard.ok)throw new IOException("cdp_popup_interception_required");frozen.add(candidate);
            }
            if(frozen.isEmpty()||captureOwner==null)throw new IOException("cdp_popup_native_capture_pending");
            allocation=new Allocation(original,lease,pending,captureOwner,frozen,proofScope());return allocation;
        }
        private synchronized void capture(MdoBrowserCdpPopupBroker.Registration owner,MdoBrowserCdpPopupBroker.Candidate original)throws IOException {
            captured.removeIf(candidate->!candidate.current()||!candidate.mayBelongTo(owner));if(captured.size()>=7)throw new IOException("cdp_popup_candidate_limit");
            captureOwner=owner;captured.add(original);
        }
        Result result(){return result.get();}
        synchronized boolean proveNative(MdoBrowserCdpPopupNative.Child child)throws IOException {
            Result value=result.get();if(cancelled.get()||!parentCurrent()||value==null||!value.ready||value.terminal||retirement==null||!retirement.current()||child==null||!child.current()||!child.belongsTo(lease)||!child.belongsTo(retirement)||!child.allocated())throw new IOException("cdp_popup_native_proof_unavailable");
            if(nativeQueue.size()>=7)throw new IOException("cdp_popup_native_proof_limit");if(!child.enqueue())return false;
            nativeQueue.add(child);Runnable notify=wake;if(notify!=null)notify.run();return true;
        }
        private synchronized MdoBrowserCdpPopupNative.Child takeNative(){return nativeQueue.poll();}
        private synchronized void terminal(Result value){result.set(value);wake=null;captured.clear();captureOwner=null;allocation=null;MdoBrowserCdpPopupNative.Child child;
            while((child=nativeQueue.poll())!=null)child.finish(false,"cdp_popup_prepare_cancelled",0,false);}
        private BooleanSupplier proofScope(){AtomicBoolean stop=cancelled;AtomicReference<Result> latest=result;MdoBrowserCdpMarker.Lease original=lease;MdoBrowserCdpPopupNative.Retirement action=retirement;
            MdoBrowserCdpPopupBroker.Handoff owner=parent;MdoBrowserCdpPopupNative.Source proof=parentProof;
            return ()->{Result value=latest.get();return !stop.get()&&original.current()&&(owner==null||owner.current()&&owner.owns(proof)&&proof.current())&&(action==null||action.current())&&value!=null&&value.ready&&!value.terminal;};}
        @Override public void close(){Result value=result.get();if(value!=null&&value.terminal)return;cancelled.set(true);Runnable notify=wake;if(notify!=null)notify.run();}
    }
    private static final int QUEUED=0,STARTING=1,READY=2,CANCELLING=3,NATIVE_CANCELLING=4,TERMINAL=5;
    private final MdoBrowserCdpWorker.Ui ui;private final ThreadPoolExecutor executor;private final Source source;private final Factory factory;private final LongSupplier clock;
    private final Runnable wake,unavailable;private final Object gate=new Object();private final Job[] jobs=new Job[8];
    private final AtomicBoolean closed=new AtomicBoolean();private final AtomicInteger active=new AtomicInteger();
    private volatile MdoBrowserCdpPopupBroker broker;private boolean operating;private String poisoned;
    MdoBrowserCdpPopupWorker(MdoBrowserCdpWorker.Ui ui,ThreadPoolExecutor executor,Source source,Factory factory,LongSupplier clock,Runnable wake,Runnable unavailable){
        if(ui==null||executor==null||source==null||factory==null||clock==null||wake==null||unavailable==null||!ui.onThread())throw new IllegalArgumentException("Popup dispatch requires its UI owner");
        this.ui=ui;this.executor=executor;this.source=source;this.factory=factory;this.clock=clock;this.wake=wake;this.unavailable=unavailable;
    }
    private void thread(){if(!ui.onThread())throw new IllegalStateException("Popup admission requires its UI thread");}
    private static String safe(String code){return code!=null&&code.matches("cdp_[a-z_]{1,80}")?code:"cdp_popup_worker_failed";}
    Request start(MdoBrowserCdpMarker.Lease lease,int timeout,Events events)throws IOException {
        return start(lease,timeout,events,null);
    }
    Request start(MdoBrowserCdpMarker.Lease lease,int timeout,Events events,MdoBrowserCdpPopupNative nativeSource)throws IOException {
        return start(lease,timeout,events,nativeSource,null,null);
    }
    Request startAttached(MdoBrowserCdpPopupNative source,MdoBrowserCdpPopupBroker.Handoff parent,MdoBrowserCdpPopupNative.Source proof,int timeout,Events events)throws IOException {
        thread();if(source==null||parent==null||proof==null||source.lease()!=proof.lease||!parent.current()||!parent.owns(proof)||!proof.current())throw new IOException("cdp_popup_attached_opener_invalid");
        return start(source.lease(),timeout,events,source,parent,proof);
    }
    private Request start(MdoBrowserCdpMarker.Lease lease,int timeout,Events events,MdoBrowserCdpPopupNative nativeSource,MdoBrowserCdpPopupBroker.Handoff parent,MdoBrowserCdpPopupNative.Source proof)throws IOException {
        thread();if(closed.get())throw new IOException("cdp_worker_closed");
        if(lease==null||!lease.current()||!lease.physicalNetwork()||events==null||timeout<100||timeout>15000)throw new IOException("cdp_popup_prepare_arguments_invalid");
        Job job=new Job(lease,timeout,events,nativeSource,parent,proof);
        synchronized(gate){int index=-1;for(int i=0;i<jobs.length;i++){
            if(jobs[i]!=null&&jobs[i].lease==lease)throw new IOException("cdp_popup_prepare_busy");if(jobs[i]==null)index=i;
        }if(index<0)throw new IOException("cdp_popup_prepare_limit");if(nativeSource!=null){nativeSource.claim(lease);try{nativeSource.retainUiOwner();job.retirement=nativeSource.retirement();job.request.retirement=job.retirement;job.retirement.onDelivered(job.retirementDelivered);job.retirement.onQuiescence(job.nativeQuiescence);}catch(IOException|RuntimeException|Error error){nativeSource.abandon();throw error;}}jobs[index]=job;active.incrementAndGet();}
        try{executor.execute(job);}catch(RejectedExecutionException error){remove(job);active.decrementAndGet();if(nativeSource!=null)nativeSource.abandon();throw new IOException("cdp_worker_unavailable");}
        catch(RuntimeException|Error error){remove(job);active.decrementAndGet();if(nativeSource!=null)nativeSource.abandon();throw error;}
        return job.request;
    }
    private void remove(Job job){synchronized(gate){for(int i=0;i<jobs.length;i++)if(jobs[i]==job){jobs[i]=null;return;}}}
    private List<Job> snapshot(){synchronized(gate){List<Job> found=new ArrayList<>();for(Job job:jobs)if(job!=null)found.add(job);return found;}}
    boolean needed(){MdoBrowserCdpPopupBroker current=broker;return !closed.get()&&(active.get()!=0||current!=null&&current.armed());}
    MdoBrowserCdpBinding bindDocument(MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Document document,MdoBrowserCdpMarker marker,int timeout)throws IOException{
        MdoBrowserCdpPopupBroker current=broker;if(closed.get()||current==null||!current.armed())throw new IOException("cdp_popup_broker_closed");return current.bindDocument(handoff,document,marker,timeout);
    }
    MdoBrowserCdpBinding bindSource(MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Source source,MdoBrowserCdpMarker marker,int timeout)throws IOException {
        MdoBrowserCdpPopupBroker current=broker;if(closed.get()||current==null||!current.armed())throw new IOException("cdp_popup_broker_closed");return current.bindSource(handoff,source,marker,timeout);
    }
    int pending(){thread();synchronized(gate){int count=0;for(Job job:jobs)if(job!=null)count++;return count;}}
    private boolean clean(){MdoBrowserCdpPopupBroker current=broker;return current==null||current.cleanupConfirmed();}
    private boolean enabled(){MdoBrowserCdpPopupBroker current=broker;return current!=null&&current.enabledMayHaveOccurred();}
    private void failAll(String code){
        List<Job> owned=snapshot();
        for(Job job:owned)if(job.stage!=QUEUED&&job.stage!=TERMINAL){job.request.cancelled.set(true);if(job.nativeSource!=null&&job.registration!=null)job.nativeCancelRequested=true;}
        MdoBrowserCdpPopupBroker current=broker;if(current!=null)current.close();boolean clean=clean();
        if(!clean)poisoned="cdp_popup_cleanup_unconfirmed";
        for(Job job:owned)if(job.stage!=QUEUED&&job.stage!=TERMINAL){
            job.finish(safe(code),clean||job.registration==null,clean);
        }
        if(clean)broker=null;
    }
    /** The existing network pump calls this, also from cooperative RPC waits. */
    void turn(){
        if(closed.get()||operating||!needed())return;operating=true;
        try{
            MdoBrowserCdpPopupBroker current=broker;
            if(current==null){for(Job job:snapshot())if(job.stage==READY||job.stage==CANCELLING||job.stage==NATIVE_CANCELLING)job.finish("cdp_popup_worker_failed",false,true);return;}
            for(Job job:snapshot())if(job.stage==READY&&(job.request.cancelled.get()||!job.request.parentCurrent()||!job.lease.current()||job.nativeSource!=null&&(!job.nativeSource.live()||job.retirement==null||!job.retirement.current()))){
                if(job.nativeSource!=null){job.beginNativeCancel();continue;}
                for(MdoBrowserCdpPopupBroker.Candidate candidate:current.pending())if(candidate.mayBelongTo(job.registration))throw new IOException("cdp_popup_native_cancel_required");
                job.releaseRegistration();
            }
            for(Job job:snapshot())if(job.stage==NATIVE_CANCELLING){
                if(job.retirement==null||!job.retirement.belongsTo(job.nativeSource))throw new IOException("cdp_popup_native_scope_changed");
                long elapsed=clock.getAsLong()-job.cancelBorn;if(elapsed<0||elapsed>=5000)throw new IOException("cdp_popup_native_cancel_timeout");
                if(job.nativeSource.rejectionPending())continue;
                String error=job.nativeSource.failure();if(error!=null)throw new IOException(error);
                if(!job.nativeSource.confirmed())continue;
                boolean held=false;for(MdoBrowserCdpPopupBroker.Candidate candidate:current.pending())if(candidate.mayBelongTo(job.registration)){held=true;break;}
                if(!held)job.releaseRegistration();
            }
            if(current.armed())current.turn(10,250);
            boolean protectedOne=false;
            for(Job job:snapshot()){
                if(job.stage==CANCELLING){job.finish(job.cancelCode,true,current.cleanupConfirmed());continue;}
                if(job.stage!=READY)continue;
                if(!current.armed())throw new IOException("cdp_popup_broker_closed");
                for(MdoBrowserCdpPopupBroker.Candidate candidate:current.pending())if(candidate.mayBelongTo(job.registration)){
                    if(job.notified.add(candidate.id)){job.request.capture(job.registration,candidate);job.offer(candidate);}
                    if(candidate.protectionRequested()&&candidate.protection()==null&&!job.request.cancelled.get()){
                        if(job.nativeSource==null)current.refuseProtection(candidate,"cdp_popup_native_source_required");
                        else if(!protectedOne&&current.idle()){protectedOne=true;current.protect(candidate,250);}
                    }
                    MdoBrowserCdpSession.ControlResult protection=candidate.protection();
                    if(protection!=null&&job.protectionNotified.add(candidate.id)){
                        job.offerProtection(candidate);
                        if(!protection.ok&&job.nativeSource!=null){job.cancelCode=protection.code;job.request.cancelled.set(true);job.beginNativeCancel();break;}
                    }
                }
                if(!protectedOne&&!job.request.cancelled.get()&&current.idle()){
                    MdoBrowserCdpPopupNative.Child child=job.request.takeNative();if(child!=null){protectedOne=true;
                        if(job.nativeSource==null)child.finish(false,"cdp_popup_native_source_required",0,false);
                        else try{current.proveNative(job.registration,child,1500,job.request.proofScope());}
                        catch(IOException error){child.finish(false,safe(error.getMessage()),0,true);throw error;}
                        catch(RuntimeException|Error error){child.finish(false,"cdp_popup_native_proof_failed",0,true);throw error;}
                        job.offerNative(child);
                    }
                }
            }
            if(!current.armed()&&current.cleanupConfirmed())broker=null;
        }catch(IOException error){failAll(safe(error.getMessage()));}
        catch(RuntimeException|Error error){failAll("cdp_popup_worker_failed");}
        finally{operating=false;}
    }
    void close(){
        if(!closed.compareAndSet(false,true))return;List<Job> owned=snapshot();
        for(Job job:owned){job.request.cancelled.set(true);executor.remove(job);
            if(job.nativeSource!=null&&job.stage!=QUEUED&&job.stage!=TERMINAL)job.nativeCancelRequested=true;
        }
        MdoBrowserCdpPopupBroker current=broker;if(current!=null)current.close();
        for(Job job:owned){MdoBrowserCdpPopupBroker.Registration registration=job.registration;if(registration!=null)registration.close();
            job.finish("cdp_worker_closed",registration==null&&job.stage!=STARTING,clean());
        }
    }
    void ownerTerminated(){close();for(Job job:snapshot()){if(job.stage!=TERMINAL)job.finish("cdp_worker_closed",false,clean());else job.post();}}
    private final class Job implements Runnable {
        final MdoBrowserCdpMarker.Lease lease;final int timeout;final long born;final Request request;
        final MdoBrowserCdpPopupNative nativeSource;volatile boolean nativeCancelRequested;long cancelBorn;private boolean nativeStopDelivered;
        private MdoBrowserCdpPopupNative.Retirement retirement;
        final Runnable nativeQuiescence=this::quiesceNative;
        private void quiesceNative(){request.cancelled.set(true);nativeCancelRequested=true;post();wake.run();}
        final Runnable retirementDelivered=this::retirementDelivered;
        private void retirementDelivered(){
            thread();synchronized(this){if(stage!=TERMINAL||events!=null||delivering||terminalDelivered)return;terminalDelivered=true;}
            try{ui.remove(delivery);}catch(RuntimeException|Error ignored){}remove(this);
        }
        String cancelCode="cdp_popup_prepare_cancelled";
        final Set<String> notified=new HashSet<>();final ArrayDeque<MdoBrowserCdpPopupBroker.Candidate> candidates=new ArrayDeque<>();
        final Set<String> protectionNotified=new HashSet<>();final ArrayDeque<MdoBrowserCdpPopupBroker.Candidate> protections=new ArrayDeque<>();
        final ArrayDeque<MdoBrowserCdpPopupNative.Child> nativeProofs=new ArrayDeque<>();
        final Runnable delivery=this::deliver;volatile int stage=QUEUED;volatile MdoBrowserCdpPopupBroker.Registration registration;
        private Events events;private boolean posted,delivering,readyDelivered,terminalDelivered;
        Job(MdoBrowserCdpMarker.Lease lease,int timeout,Events events,MdoBrowserCdpPopupNative nativeSource,MdoBrowserCdpPopupBroker.Handoff parent,MdoBrowserCdpPopupNative.Source proof){this.lease=lease;this.timeout=timeout;this.events=events;this.nativeSource=nativeSource;born=clock.getAsLong();request=new Request(()->{wake.run();},lease,nativeSource,parent,proof);}
        void beginNativeCancel(){synchronized(this){if(stage==TERMINAL)return;request.cancelled.set(true);cancelBorn=clock.getAsLong();stage=NATIVE_CANCELLING;nativeCancelRequested=true;}post();}
        void releaseRegistration(){synchronized(this){if(stage==TERMINAL)return;registration.close();registration=null;stage=CANCELLING;}}
        private int remaining()throws IOException {
            if(closed.get()||request.cancelled.get()||!request.parentCurrent()||!lease.current()||retirement!=null&&!retirement.current()){request.cancelled.set(true);throw new IOException("cdp_popup_prepare_cancelled");}
            long used=clock.getAsLong()-born;if(used<0||used>timeout-10)throw new IOException("cdp_popup_prepare_timeout");return (int)Math.min(5000,timeout-used);
        }
        @Override public void run(){
            synchronized(this){if(stage==TERMINAL)return;stage=STARTING;}
            if(operating){finish("cdp_popup_worker_failed",true,clean());return;}operating=true;
            try{
                remaining();if(poisoned!=null)throw new IOException(poisoned);
                MdoBrowserCdpBinding.Network original=request.parent==null?source.resolve(lease):null;remaining();
                if(broker==null&&request.parent==null){broker=factory.create(()->!closed.get(),clock);if(broker!=null)broker.guardNativeClose();}
                if(broker==null)throw new IOException("cdp_popup_broker_connection_invalid");
                if(retirement!=null)broker.retainNative(retirement);
                registration=request.parent==null?broker.register(original):broker.registerAttached(request.parent,request.parentProof);if(!broker.armed())broker.arm(remaining());
                remaining();synchronized(this){if(stage==TERMINAL)return;stage=READY;request.result.set(new Result(true,false,null,enabled(),false,false));}post();
            }catch(IOException error){
                if(registration!=null&&broker!=null&&broker.armed()&&(!lease.current()||request.cancelled.get())){
                    if(nativeSource!=null)beginNativeCancel();else releaseRegistration();
                }else if(registration==null)finish(safe(error.getMessage()),true,clean());
                else failAll(safe(error.getMessage()));
            }catch(RuntimeException|Error error){failAll("cdp_popup_worker_failed");}
            finally{operating=false;wake.run();}
        }
        void offer(MdoBrowserCdpPopupBroker.Candidate candidate){
            synchronized(this){if(stage!=READY||request.cancelled.get())return;if(candidates.size()>=7){request.cancelled.set(true);return;}candidates.add(candidate);}post();
        }
        void offerProtection(MdoBrowserCdpPopupBroker.Candidate candidate){
            synchronized(this){if(stage!=READY||request.cancelled.get())return;if(protections.size()>=7){request.cancelled.set(true);return;}protections.add(candidate);}post();
        }
        void offerNative(MdoBrowserCdpPopupNative.Child child){
            synchronized(this){if(stage!=READY||request.cancelled.get())return;if(nativeProofs.size()>=7){request.cancelled.set(true);return;}nativeProofs.add(child);}post();
        }
        void finish(String code,boolean released,boolean clean){
            // Closing the host can wake a protocol wait before close() records
            // its terminal result. Keep that explicit cause without upgrading
            // either the source-release or protocol-cleanup receipt.
            synchronized(this){if(stage==TERMINAL)return;if(closed.get())code="cdp_worker_closed";stage=TERMINAL;candidates.clear();protections.clear();nativeProofs.clear();if(retirement!=null)retirement.request(released);request.terminal(new Result(false,true,code,enabled(),released,clean));active.decrementAndGet();}post();
        }
        private void post(){
            synchronized(this){if(events==null&&(retirement==null||!retirement.pending())){if(stage==TERMINAL)remove(this);return;}if(posted||delivering||terminalDelivered)return;posted=true;}
            boolean accepted;try{accepted=ui.post(delivery);}catch(RuntimeException|Error error){accepted=false;}
            if(!accepted){synchronized(this){posted=false;events=null;}request.cancelled.set(true);if(stage==TERMINAL&&(retirement==null||!retirement.pending()))remove(this);unavailable.run();}
        }
        private void deliver(){
            thread();Events action;Result value;List<MdoBrowserCdpPopupBroker.Candidate> pending,protectedTargets;List<MdoBrowserCdpPopupNative.Child> provenTargets;
            synchronized(this){
                posted=false;if(delivering||terminalDelivered||events==null&&(retirement==null||!retirement.pending()))return;delivering=true;action=events;value=request.result.get();
                if(value!=null&&value.terminal){terminalDelivered=true;pending=new ArrayList<>();protectedTargets=new ArrayList<>();provenTargets=new ArrayList<>();}
                else if(stage==READY&&!request.cancelled.get()&&lease.current()){pending=new ArrayList<>(candidates);candidates.clear();protectedTargets=new ArrayList<>(protections);protections.clear();provenTargets=new ArrayList<>(nativeProofs);nativeProofs.clear();if(readyDelivered)value=null;else readyDelivered=true;}
                else{value=null;pending=new ArrayList<>();protectedTargets=new ArrayList<>();provenTargets=new ArrayList<>();candidates.clear();protections.clear();nativeProofs.clear();}
            }
            try{
                if(nativeSource!=null&&value!=null&&value.ready&&!request.cancelled.get()&&stage==READY&&(retirement==null||!retirement.current()||!retirement.belongsTo(nativeSource)||!nativeSource.ready())){request.cancelled.set(true);value=null;wake.run();}
                if(stage==READY&&(!request.parentCurrent()||nativeSource!=null&&(retirement==null||!retirement.belongsTo(nativeSource)||!nativeSource.current()))){request.cancelled.set(true);wake.run();}
                if(retirement!=null&&(value!=null&&value.terminal||retirement.quiescenceRequested())){retirement.deliver();if(retirement.quiescenceDelivered())nativeStopDelivered=true;}
                else if(nativeCancelRequested&&!nativeStopDelivered&&retirement!=null){nativeStopDelivered=true;retirement.rejectOriginal();wake.run();}
                if(value!=null&&(value.terminal||!request.cancelled.get()&&lease.current()&&stage==READY)){
                    if(action!=null)action.state(value);
                }
                for(MdoBrowserCdpPopupBroker.Candidate candidate:pending)if(!request.cancelled.get()&&lease.current()&&candidate.current())action.candidate(candidate);
                for(MdoBrowserCdpPopupBroker.Candidate candidate:protectedTargets)if(!request.cancelled.get()&&lease.current()&&candidate.current())action.protection(candidate);
                for(MdoBrowserCdpPopupNative.Child child:provenTargets)if(!request.cancelled.get()&&lease.current()&&child.current())action.nativeProof(child);
            }
            catch(IOException|RuntimeException|Error error){request.cancelled.set(true);wake.run();}
            finally{boolean again; synchronized(this){delivering=false;if(terminalDelivered)events=null;Result latest=request.result.get();again=latest!=null&&latest.terminal||!candidates.isEmpty()||!protections.isEmpty()||!nativeProofs.isEmpty()||nativeCancelRequested&&!nativeStopDelivered;}
                if(terminalDelivered)remove(this);else if(again)post();}
        }
    }
}
