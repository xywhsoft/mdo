package org.xserver.android;

import java.io.IOException;
import java.util.Base64;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.util.function.LongSupplier;
import java.util.function.BooleanSupplier;

/** One persistent worker, eight queued tasks, nine unfinished UI deliveries and
 * four native read outcomes. A stalled UI/reader cannot grow an unbounded inbox. */
final class MdoBrowserUploadQueue implements AutoCloseable {
    interface Factory {MdoBrowserUploadFiles open()throws IOException;}
    interface Ui {boolean post(Runnable task);}
    interface Completion {void complete(Outcome result);}
    static final class Request {
        final String action,key,ticket,base64;final MdoBrowserUploadStore.Spec[] specs;final long expires,offset;final int index;
        final MdoBrowserUploadStore.PickerGrant grant;final MdoBrowserUploadStore.RetentionOwner retainer;final MdoBrowserUploadStore.Retention retention;
        final Object assigner;
        private Request(String action,String key,String ticket,String base64,MdoBrowserUploadStore.Spec[] specs,long expires,int index,long offset){
            this(action,key,ticket,base64,specs,expires,index,offset,null,null,null,null);
        }
        private Request(String action,String key,String ticket,String base64,MdoBrowserUploadStore.Spec[] specs,long expires,int index,long offset,
                        MdoBrowserUploadStore.PickerGrant grant,MdoBrowserUploadStore.RetentionOwner retainer,MdoBrowserUploadStore.Retention retention,Object assigner){
            this.action=action;this.key=key;this.ticket=ticket;this.base64=base64;this.specs=specs==null?null:specs.clone();this.expires=expires;this.index=index;this.offset=offset;
            this.grant=grant;this.retainer=retainer;this.retention=retention;
            this.assigner=assigner;
        }
        static Request begin(String key,MdoBrowserUploadStore.Spec[] specs,long expires)throws IOException {
            MdoBrowserUploadStore.manifest(specs);if(!MdoBrowserUploadStore.bounded(key,100))throw new MdoBrowserUploadFiles.Failure("upload_metadata_invalid");
            return new Request("begin",key,null,null,specs,expires,0,0);
        }
        static Request ticket(String action,String ticket)throws IOException {
            if(!java.util.Arrays.asList("seal","status","cancel","release").contains(action)||ticket==null||!ticket.matches("u-[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}"))
                throw new MdoBrowserUploadFiles.Failure("upload_ticket_invalid");
            return new Request(action,null,ticket,null,null,0,0,0);
        }
        static Request chunk(String ticket,int index,long offset,String base64)throws IOException {
            Request valid=ticket("status",ticket);
            if(index<0||index>=16||offset<0||offset>MdoBrowserUploadFiles.FILE_BYTES||base64==null||base64.isEmpty()||base64.length()>87384||base64.length()%4!=0)
                throw new MdoBrowserUploadFiles.Failure("upload_chunk_invalid");
            return new Request("chunk",null,valid.ticket,base64,null,0,index,offset);
        }
        // Native callback route only; the JSON upload action allowlist has no
        // access/read command and cannot serialize this object authority.
        static Request access(String ticket,int index)throws IOException {
            Request valid=ticket("status",ticket);if(index<0||index>=16)throw new MdoBrowserUploadFiles.Failure("upload_range_invalid");
            return new Request("access",null,valid.ticket,null,null,0,index,0);
        }
        static Request retain(MdoBrowserUploadStore.PickerGrant grant,MdoBrowserUploadStore.RetentionOwner owner)throws IOException {
            if(grant==null||owner==null)throw new MdoBrowserUploadFiles.Failure("upload_retention_invalid");
            return new Request("retain",null,null,null,null,0,0,0,grant,owner,null,null);
        }
        static Request retained(MdoBrowserUploadStore.Retention value,boolean release)throws IOException {
            if(value==null)throw new MdoBrowserUploadFiles.Failure("upload_retention_invalid");
            return new Request(release?"retained_release":"retained_status",null,null,null,null,0,0,0,null,null,value,null);
        }
        static Request claim(String ticket,Object assigner,boolean cancel)throws IOException {
            Request valid=ticket("status",ticket);if(assigner==null)throw new MdoBrowserUploadFiles.Failure("upload_assignment_owner_invalid");
            return new Request(cancel?"cancel_claim":"claim",null,valid.ticket,null,null,0,0,0,null,null,null,assigner);
        }
    }
    static final class Outcome {
        final String code,cause;final MdoBrowserUploadStore.Snapshot value;final boolean stagingMayHaveChanged;final MdoBrowserUploadStore.ReadLease reader;
        final MdoBrowserUploadStore.Retention retention;final MdoBrowserUploadStore.RetainedSnapshot retainedValue;
        final boolean retentionHandoff;
        Outcome(String code,String cause,MdoBrowserUploadStore.Snapshot value,boolean changed){this(code,cause,value,changed,null);}
        Outcome(String code,String cause,MdoBrowserUploadStore.Snapshot value,boolean changed,MdoBrowserUploadStore.ReadLease reader){this(code,cause,value,changed,reader,null,null);}
        Outcome(String code,String cause,MdoBrowserUploadStore.Snapshot value,boolean changed,MdoBrowserUploadStore.ReadLease reader,MdoBrowserUploadStore.Retention retention,MdoBrowserUploadStore.RetainedSnapshot retainedValue){
            this(code,cause,value,changed,reader,retention,retainedValue,false);
        }
        Outcome(String code,String cause,MdoBrowserUploadStore.Snapshot value,boolean changed,MdoBrowserUploadStore.ReadLease reader,MdoBrowserUploadStore.Retention retention,MdoBrowserUploadStore.RetainedSnapshot retainedValue,boolean handoff){
            this.code=code;this.cause=cause;this.value=value;stagingMayHaveChanged=changed;this.reader=reader;this.retention=retention;this.retainedValue=retainedValue;retentionHandoff=handoff;
        }
        boolean ok(){return code==null;}
    }
    private static final Outcome FAILED=new Outcome("upload_worker_failed",null,null,true),CLOSED=new Outcome("upload_worker_closed",null,null,false);
    private final Factory factory;private final Ui ui;private final LongSupplier clock;private final BooleanSupplier blockingAllowed;
    private final ArrayBlockingQueue<Work> queue=new ArrayBlockingQueue<>(8);private final Semaphore deliveries=new Semaphore(9);
    private final Semaphore reads=new Semaphore(4);
    private final Object readOwner=new Object();
    private final CountDownLatch stopped=new CountDownLatch(1);private final Thread worker;
    private MdoBrowserUploadStore store;private volatile boolean closing,ready,cleanupFailed;private volatile int activeFiles;
    private volatile long reservedBytes,recoveredBytes;private volatile int recoveredFiles;
    private final class Work {
        final MdoBrowserUploadStore.Scope scope;final ReadCall read;Request request;Completion completion;
        final java.util.concurrent.atomic.AtomicBoolean delivered=new java.util.concurrent.atomic.AtomicBoolean();
        Work(MdoBrowserUploadStore.Scope scope,Request request,Completion completion){this.scope=scope;this.request=request;this.completion=completion;read=null;}
        Work(ReadCall read){this.read=read;scope=read.lease.scope;}
        void deliver(Outcome value){
            Completion callback=completion;completion=null;request=null;
            try{
                if(ui.post(()->{if(!delivered.compareAndSet(false,true))return;boolean accepted=false;
                    try{callback.complete(value);accepted=true;}catch(RuntimeException|OutOfMemoryError ignored){}
                    finally{if(value.retentionHandoff&&value.retention!=null){if(accepted)value.retention.acknowledge();else value.retention.abandonUndelivered();}deliveries.release();}}))return;
            }catch(RuntimeException|OutOfMemoryError ignored){}
            if(delivered.compareAndSet(false,true)){if(value.retentionHandoff&&value.retention!=null)value.retention.abandonUndelivered();deliveries.release();}
        }
    }
    private final class ReadCall {
        final MdoBrowserUploadStore.ReadLease lease;final long offset;final int limit;final CountDownLatch done=new CountDownLatch(1);
        private boolean finished,abandoned,released;private byte[] bytes;private String code;
        ReadCall(MdoBrowserUploadStore.ReadLease lease,long offset,int limit){this.lease=lease;this.offset=offset;this.limit=limit;}
        private void release(){if(finished&&abandoned&&!released){released=true;reads.release();}}
        synchronized void finish(byte[] value,String failure){
            if(finished)return;finished=true;if(!abandoned){bytes=value;code=failure;}release();done.countDown();
        }
        synchronized void abandon(){abandoned=true;bytes=null;release();}
        synchronized boolean abandoned(){return abandoned;}
        byte[] await(long deadline)throws IOException {
            try{
                long remaining=deadline-System.nanoTime();
                if(remaining<=0||!done.await(remaining,TimeUnit.NANOSECONDS))throw new MdoBrowserUploadFiles.Failure("upload_read_timeout");
                synchronized(this){
                    if(code!=null)throw new MdoBrowserUploadFiles.Failure(code);
                    if(closing)throw new MdoBrowserUploadFiles.Failure("upload_worker_closed");
                    if(!lease.live(clock.getAsLong()))throw new MdoBrowserUploadFiles.Failure("upload_read_lease_revoked");
                    if(bytes==null)throw new MdoBrowserUploadFiles.Failure("upload_read_unconfirmed");byte[] value=bytes;bytes=null;return value;
                }
            }catch(InterruptedException cancelled){Thread.currentThread().interrupt();throw new MdoBrowserUploadFiles.Failure("upload_read_cancelled");}
            finally{abandon();}
        }
    }
    MdoBrowserUploadQueue(Factory factory,Ui ui,LongSupplier clock){
        this(factory,ui,clock,()->true);
    }
    MdoBrowserUploadQueue(Factory factory,Ui ui,LongSupplier clock,BooleanSupplier blockingAllowed){
        if(factory==null||ui==null||clock==null||blockingAllowed==null)throw new IllegalArgumentException("Missing upload queue binding");
        this.factory=factory;this.ui=ui;this.clock=clock;this.blockingAllowed=blockingAllowed;
        worker=new Thread(this::loop,"mdo-browser-upload");worker.setDaemon(true);worker.start();
    }
    boolean submit(MdoBrowserUploadStore.Scope scope,Request request,Completion completion){
        if(scope==null||request==null||completion==null)throw new IllegalArgumentException("Missing upload request");
        if(closing||!deliveries.tryAcquire())return false;
        try{Work work=new Work(scope,request,completion);if(!closing&&queue.offer(work))return true;}
        catch(RuntimeException|Error failure){deliveries.release();throw failure;}
        deliveries.release();return false;
    }
    // For native background proxy callbacks, never the UI or this worker.
    // Timed-out/abandoned calls retain their slot until actual worker retirement.
    byte[] read(MdoBrowserUploadStore.ReadLease lease,long offset,int limit,int timeoutMilliseconds)throws IOException {
        long entered=System.nanoTime();
        if(Thread.currentThread()==worker||!blockingAllowed.getAsBoolean())throw new MdoBrowserUploadFiles.Failure("upload_read_thread_invalid");
        if(lease==null)throw new MdoBrowserUploadFiles.Failure("upload_read_lease_invalid");
        if(offset<0||offset>lease.file.size||limit<1||limit>65536||timeoutMilliseconds<1||timeoutMilliseconds>5000)throw new MdoBrowserUploadFiles.Failure("upload_range_invalid");
        if(!lease.live(clock.getAsLong()))throw new MdoBrowserUploadFiles.Failure("upload_read_lease_revoked");
        if(closing)throw new MdoBrowserUploadFiles.Failure("upload_worker_closed");
        if(!reads.tryAcquire())throw new MdoBrowserUploadFiles.Failure("upload_read_busy");boolean queued=false;
        try{
            long deadline=entered+TimeUnit.MILLISECONDS.toNanos(timeoutMilliseconds);
            if(deadline-System.nanoTime()<=0)throw new MdoBrowserUploadFiles.Failure("upload_read_timeout");
            ReadCall call=new ReadCall(lease,offset,limit);if(!closing&&queue.offer(new Work(call))){queued=true;return call.await(deadline);}
            throw new MdoBrowserUploadFiles.Failure(closing?"upload_worker_closed":"upload_read_busy");
        }finally{if(!queued)reads.release();}
    }
    private void executeRead(ReadCall call){
        byte[] bytes=null;String error="upload_read_failed";
        try{
            if(closing)error="upload_worker_closed";
            else if(call.abandoned())error="upload_read_cancelled";
            else if(store==null)error="upload_read_lease_invalid";
            else{bytes=store.read(call.lease,call.offset,call.limit);error=null;}
        }catch(IOException failure){error=failure instanceof MdoBrowserUploadFiles.Failure?((MdoBrowserUploadFiles.Failure)failure).code:"upload_io_failed";}
        catch(RuntimeException|LinkageError|OutOfMemoryError ignored){}
        finally{call.finish(bytes,error);}
    }
    private Outcome execute(Work work)throws IOException {
        Request request=work.request;
        if(closing)return CLOSED;
        if("cancel_claim".equals(request.action)){
            if(store==null)throw new MdoBrowserUploadFiles.Failure("upload_ticket_not_found");
            return new Outcome(null,null,store.cancelClaim(work.scope,request.ticket,request.assigner),true);
        }
        // These native-only object capabilities have no JSON action route.
        // Historical status/release and idempotent retention can outlive the
        // transient input scope; the store still proves exact queue ownership.
        if("retain".equals(request.action)){
            if(work.scope!=request.grant.scope())throw new MdoBrowserUploadFiles.Failure("upload_retention_scope_invalid");
            if(store==null)throw new MdoBrowserUploadFiles.Failure("upload_retention_invalid");
            MdoBrowserUploadStore.Retention kept=store.retain(request.grant,request.retainer);
            try{return new Outcome(null,null,null,true,null,kept,store.retained(kept,false),true);}
            catch(IOException|RuntimeException|Error error){kept.abandonUndelivered();throw error;}
        }
        if(request.retention!=null){
            if(work.scope!=request.retention.scope())throw new MdoBrowserUploadFiles.Failure("upload_retention_scope_invalid");
            if(store==null)throw new MdoBrowserUploadFiles.Failure("upload_retention_invalid");
            return new Outcome(null,null,null,!"retained_status".equals(request.action),null,request.retention,store.retained(request.retention,"retained_release".equals(request.action)));
        }
        if(!work.scope.live())throw new MdoBrowserUploadFiles.Failure("upload_owner_changed");
        if(store==null){
            if(!"begin".equals(request.action))throw new MdoBrowserUploadFiles.Failure("upload_ticket_not_found");
            if(clock.getAsLong()>=request.expires)throw new MdoBrowserUploadFiles.Failure("upload_expired");
            MdoBrowserUploadFiles opened=factory.open();
            try{
                int restoredFiles=opened.recoveredFiles();long restoredBytes=opened.recoveredBytes();
                store=new MdoBrowserUploadStore(opened,clock,readOwner);recoveredFiles=restoredFiles;recoveredBytes=restoredBytes;ready=true;
            }
            catch(RuntimeException|Error failure){try{opened.close();}catch(IOException ignored){}throw failure;}
        }
        MdoBrowserUploadStore.Snapshot value;
        switch(request.action){
            case "begin":value=store.begin(work.scope,request.key,request.specs,request.expires);break;
            case "chunk":
                byte[] bytes;
                try{bytes=Base64.getDecoder().decode(request.base64);}catch(IllegalArgumentException invalid){throw new MdoBrowserUploadFiles.Failure("upload_chunk_invalid");}
                if(bytes.length<1||bytes.length>65536||!Base64.getEncoder().encodeToString(bytes).equals(request.base64))throw new MdoBrowserUploadFiles.Failure("upload_chunk_invalid");
                value=store.chunk(work.scope,request.ticket,request.index,request.offset,bytes);break;
            case "seal":value=store.seal(work.scope,request.ticket);break;
            case "access":return new Outcome(null,null,null,false,store.access(work.scope,request.ticket,request.index));
            case "claim":value=store.claim(work.scope,request.ticket,request.assigner);break;
            case "status":value=store.status(work.scope,request.ticket,clock.getAsLong());break;
            case "cancel":value=store.cancel(work.scope,request.ticket);break;
            case "release":value=store.releaseConfirmed(work.scope,request.ticket);break;
            default:throw new MdoBrowserUploadFiles.Failure("upload_action_unsupported");
        }
        return new Outcome(null,null,value,!"status".equals(request.action));
    }
    private void collect(){
        if(store==null)return;
        try{store.collect(clock.getAsLong());cleanupFailed=false;}catch(IOException|RuntimeException|OutOfMemoryError failure){cleanupFailed=true;}
        activeFiles=store.reservations();reservedBytes=store.reservedBytes();
    }
    private void loop(){
        try{
            while(!closing||!queue.isEmpty()){
                Work work=null;
                try{work=queue.poll(1000,TimeUnit.MILLISECONDS);}catch(InterruptedException interrupted){closing=true;}
                if(work!=null){
                    if(work.read!=null){executeRead(work.read);collect();continue;}
                    Outcome result=FAILED;
                    try{result=execute(work);}
                    catch(IOException failure){
                        try{result=new Outcome(failure instanceof MdoBrowserUploadFiles.Failure?((MdoBrowserUploadFiles.Failure)failure).code:"upload_io_failed",
                            failure.getCause() instanceof MdoBrowserUploadFiles.Failure?((MdoBrowserUploadFiles.Failure)failure.getCause()).code:null,null,true);}
                        catch(OutOfMemoryError ignored){}
                    }catch(RuntimeException|LinkageError|OutOfMemoryError ignored){}
                    collect();work.deliver(result);
                }else collect();
            }
        }finally{
            closing=true;
            Work pending;while((pending=queue.poll())!=null){if(pending.read!=null)pending.read.finish(null,"upload_worker_closed");else pending.deliver(CLOSED);}
            if(store!=null)try{store.close();activeFiles=0;reservedBytes=0;}catch(IOException|RuntimeException|OutOfMemoryError failure){cleanupFailed=true;}
            ready=false;stopped.countDown();
        }
    }
    boolean ready(){return ready&&!closing;}
    boolean cleanupFailed(){return cleanupFailed;}
    int activeFiles(){return activeFiles;}
    long reservedBytes(){return reservedBytes;}
    int recoveredFiles(){return recoveredFiles;}
    long recoveredBytes(){return recoveredBytes;}
    int unfinishedDeliveries(){return 9-deliveries.availablePermits();}
    int unfinishedReads(){return 4-reads.availablePermits();}
    boolean owns(MdoBrowserUploadStore.ReadLease lease){return lease!=null&&lease.belongs(readOwner);}
    @Override public void close(){closing=true;}
    boolean awaitStopped(long milliseconds)throws InterruptedException{return stopped.await(milliseconds,TimeUnit.MILLISECONDS);}
}
