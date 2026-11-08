package org.xserver.android;

import java.lang.ref.WeakReference;
import java.util.concurrent.atomic.AtomicInteger;

/** UI-owned capture transaction. Native readiness and worker bitmap ownership
 * outlive a cancelled client until their actual terminal acknowledgement. */
final class MdoBrowserCapture implements AutoCloseable {
    private static void clear(byte[] data){if(data!=null)java.util.Arrays.fill(data,(byte)0);}
    interface Scheduler {long now();void post(Runnable task,long milliseconds);void at(Runnable task,long deadline);void remove(Runnable task);}
    interface Worker {void execute(Runnable task);default void remove(Runnable task){}}
    interface Raster extends AutoCloseable {byte[] encode(String format,int maximumBytes);void close();}
    interface Guard {void check(long remainingMilliseconds,Checked complete);}
    interface Checked {void complete(String rejection);}
    interface Complete {void complete(Result result);}
    interface Target {Snapshot snapshot();void ready(Runnable complete);Raster draw(Plan plan);}
    static final class Failure extends Exception {final String code;Failure(String code){super(code);this.code=code;}}
    static final class Limit extends RuntimeException {Limit(){super("capture_byte_limit");}}
    static final class Cleanup extends RuntimeException {Cleanup(){super("capture_cleanup_failed");}}
    static final class Snapshot {
        final String document;final long navigation;final int width,height,scrollX,scrollY;final double scale;
        Snapshot(String document,long navigation,int width,int height,int scrollX,int scrollY,double scale){
            if(document==null||!document.matches("fd-[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}")||
               navigation<1||width<1||height<1||width>16384||height>16384||!Double.isFinite(scale)||scale<=0||scale>32)
                throw new IllegalArgumentException("Invalid native capture snapshot");
            this.document=document;this.navigation=navigation;this.width=width;this.height=height;
            this.scrollX=scrollX;this.scrollY=scrollY;this.scale=scale;
        }
        boolean same(Snapshot value){return value!=null&&document.equals(value.document)&&navigation==value.navigation&&
            width==value.width&&height==value.height&&scrollX==value.scrollX&&scrollY==value.scrollY&&Double.compare(scale,value.scale)==0;}
    }
    static final class Plan {
        final Snapshot snapshot;final double x,y,width,height;final int outputWidth,outputHeight,maximumBytes;final long timeout;final String format;
        Plan(Snapshot snapshot,double x,double y,double width,double height,double scale,String format,int maximumBytes,long timeout){
            if(snapshot==null||!Double.isFinite(x)||!Double.isFinite(y)||!Double.isFinite(width)||!Double.isFinite(height)||
               width<=0||height<=0||x< -32||y< -32||x+width>snapshot.width+32||y+height>snapshot.height+32||
               !Double.isFinite(scale)||scale<=0||scale>1||!("png".equals(format)||"jpeg".equals(format))||
               maximumBytes<1||maximumBytes>262144||timeout<1||timeout>10000)
                throw new IllegalArgumentException("Invalid bounded native capture plan");
            double ratio=Math.min(scale,Math.min(4096/width,Math.min(4096/height,Math.sqrt(4194304/(width*height)))));
            this.snapshot=snapshot;this.x=x;this.y=y;this.width=width;this.height=height;
            outputWidth=Math.max(1,(int)Math.floor(width*ratio));outputHeight=Math.max(1,(int)Math.floor(height*ratio));
            if((long)outputWidth*outputHeight>4194304)throw new IllegalArgumentException("Native capture raster limit");
            this.format=format;this.maximumBytes=maximumBytes;this.timeout=timeout;
        }
    }
    static final class Result {
        final boolean ok;final String code;final byte[] data;final Plan plan;
        Result(String code,byte[] data,Plan plan){ok=code==null;this.code=code;this.data=data;this.plan=plan;}
    }
    static final class Budget {
        private final int maximum;private int pending;
        Budget(int maximum){if(maximum<1||maximum>8)throw new IllegalArgumentException("Invalid capture budget");this.maximum=maximum;}
        synchronized Token claim(){if(pending==maximum)return null;Token token=new Token(this);pending++;return token;}
        synchronized int pending(){return pending;}
        private synchronized void release(){if(pending<1)throw new IllegalStateException("Capture budget underflow");pending--;}
    }
    private static final class Token {
        private Budget budget;Token(Budget budget){this.budget=budget;}
        synchronized void release(){if(budget!=null){budget.release();budget=null;}}
    }
    private static final class Lease {
        final WeakReference<MdoBrowserCapture> owner;final Scheduler scheduler;final Token token;
        boolean visual,work,validating,abandoned,returned,cleanupFailed;String code;byte[] data;
        Lease(MdoBrowserCapture owner,Scheduler scheduler,Token token){this.owner=new WeakReference<>(owner);this.scheduler=scheduler;this.token=token;}
        synchronized boolean retained(){return !returned;}
        synchronized boolean cleanupFailed(){return cleanupFailed;}
        void checked(int phase,int ticket,String code){MdoBrowserCapture current=owner.get();if(current!=null)current.checked(this,phase,ticket,code);else abandon();}
        synchronized void waitVisual(){visual=true;}
        void ready(){
            MdoBrowserCapture current=owner.get();if(current!=null)current.thread();
            synchronized(this){if(!visual||returned)return;visual=false;
                if(abandoned||current==null){finishIdle();return;}work=true;}
            current.visualReady(this);
        }
        synchronized void abandon(){abandoned=true;validating=false;clear(data);data=null;finishIdle();}
        synchronized void destroyed(){abandoned=true;visual=false;validating=false;clear(data);data=null;finishIdle();}
        private void finishIdle(){
            // A failed bitmap release cannot prove the native allocation is
            // gone. Keep that shared slot reserved until the runtime resets.
            if(!visual&&!work&&!validating&&!returned){returned=true;if(!cleanupFailed)token.release();}
        }
        void encoded(String failure,byte[] bytes){
            synchronized(this){if(!work){clear(bytes);return;}work=false;code=failure;cleanupFailed="capture_cleanup_failed".equals(failure);
                if(abandoned)clear(bytes);data=abandoned?null:bytes;validating=!abandoned&&failure==null&&bytes!=null;finishIdle();}
            try{scheduler.post(this::deliver,0);}catch(RuntimeException|OutOfMemoryError error){
                synchronized(this){clear(data);data=null;validating=false;code="capture_reply_unavailable";finishIdle();}}
        }
        void deliver(){MdoBrowserCapture current=owner.get();if(current!=null)current.encoded(this);else abandon();}
        synchronized Result take(Plan plan){byte[] bytes=data;data=null;
            try{return new Result(code,bytes,plan);}catch(OutOfMemoryError error){clear(bytes);throw error;}}
    }
    private static final class Work implements Runnable {
        private final AtomicInteger state=new AtomicInteger();private final Raster raster;private final Lease lease;private final Plan plan;
        Work(Raster raster,Lease lease,Plan plan){this.raster=raster;this.lease=lease;this.plan=plan;}
        public void run(){
            if(!state.compareAndSet(0,1))return;String code=null;byte[] data=null;
            try{data=raster.encode(plan.format,plan.maximumBytes);if(data==null||data.length<1||data.length>plan.maximumBytes){code="capture_byte_limit";clear(data);data=null;}}
            catch(Limit error){code="capture_byte_limit";}
            catch(OutOfMemoryError error){code="capture_memory_limit";}
            catch(RuntimeException error){code="capture_encode_failed";}
            finally{
                try{raster.close();}catch(RuntimeException|OutOfMemoryError error){code="capture_cleanup_failed";clear(data);data=null;}
                state.set(2);lease.encoded(code,data);
            }
        }
        boolean discard(){
            if(!state.compareAndSet(0,2))return false;String code="capture_cancelled";
            try{raster.close();}catch(RuntimeException|OutOfMemoryError error){code="capture_cleanup_failed";}
            lease.encoded(code,null);return true;
        }
    }
    private final Target target;private final Scheduler scheduler;private final Worker worker;private final Budget budget;
    private final Thread owner=Thread.currentThread();private Job active;private Lease retained;private boolean closed,destroyed,poisoned;
    MdoBrowserCapture(Target target,Scheduler scheduler,Worker worker,Budget budget){
        if(target==null||scheduler==null||worker==null||budget==null)throw new IllegalArgumentException("Missing native capture target/runtime");
        this.target=target;this.scheduler=scheduler;this.worker=worker;this.budget=budget;
    }
    private void thread(){if(Thread.currentThread()!=owner)throw new IllegalStateException("Capture requires its owning UI thread");}
    private String changed(Plan plan){
        try{return plan.snapshot.same(target.snapshot())?null:"capture_changed";}
        catch(OutOfMemoryError error){return "capture_memory_limit";}
        catch(RuntimeException error){return "capture_view_unavailable";}
    }
    void capture(Plan plan,Guard guard,Complete complete)throws Failure {
        thread();if(closed||destroyed)throw new Failure("capture_closed");if(plan==null||guard==null||complete==null)throw new Failure("capture_request_invalid");
        if(busy())throw new Failure("capture_busy");if(poisoned)throw new Failure("capture_cleanup_failed");String code=changed(plan);if(code!=null)throw new Failure(code);
        Token token=budget.claim();if(token==null)throw new Failure("capture_resource_limit");
        Job job;
        try{Lease lease=new Lease(this,scheduler,token);job=new Job(plan,guard,complete,lease);retained=lease;active=job;}
        catch(RuntimeException|OutOfMemoryError error){token.release();throw new Failure("capture_memory_limit");}
        try{job.deadline=scheduler.now()+plan.timeout;scheduler.at(job.watchdog,job.deadline);}
        catch(RuntimeException|OutOfMemoryError error){job.finish("capture_timer_unavailable",null);return;}
        if(active!=job)return;
        code=job.check();if(code!=null){job.finish(code,null);return;}
        requestGuard(job,0);
    }
    private void requestGuard(Job job,int phase){
        if(active!=job||job.done)return;long remaining;
        try{remaining=job.deadline-scheduler.now();}catch(RuntimeException error){job.finish("capture_timer_unavailable",null);return;}
        if(remaining<=0){job.finish("capture_timed_out",null);return;}
        final Lease lease=job.lease;final int ticket=++job.ticket;
        // Retain only the weak lease and phase ticket in a provider callback,
        // never the Job/guard/target. An old acknowledgement cannot admit the
        // post-encode phase or retain a cancelled view and its encoded image.
        try{job.guard.check(remaining,code->lease.checked(phase,ticket,code));}
        catch(RuntimeException|OutOfMemoryError error){if(active==job&&job.phase==phase&&job.ticket==ticket)job.finish("capture_guard_failed",null);}
    }
    private final class Job {
        final Plan plan;final Guard guard;final Complete complete;final Lease lease;final Runnable watchdog=this::checkDeadline;
        long deadline;int phase,ticket;boolean done;Work work;byte[] encoded;
        Job(Plan plan,Guard guard,Complete complete,Lease lease){this.plan=plan;this.guard=guard;this.complete=complete;this.lease=lease;}
        void checkDeadline(){
            thread();if(done||active!=this)return;
            try{
                boolean expired=scheduler.now()>=deadline;
                if(done||active!=this)return;
                if(expired){finish("capture_timed_out",null);return;}
                scheduler.at(watchdog,deadline);
            }catch(RuntimeException|OutOfMemoryError error){if(active==this)finish("capture_timer_unavailable",null);}
        }
        String check(){
            if(closed||destroyed)return "capture_closed";
            try{if(scheduler.now()>=deadline)return "capture_timed_out";}catch(RuntimeException error){return "capture_timer_unavailable";}
            return changed(plan);
        }
        void finish(String code,byte[] data){
            thread();if(done||active!=this)return;done=true;active=null;ticket++;
            try{scheduler.remove(watchdog);}catch(RuntimeException|OutOfMemoryError error){if(code==null)code="capture_timer_unavailable";}
            if(code!=null){clear(encoded);clear(data);data=null;}encoded=null;
            lease.abandon();if(code!=null)discardWork(this);
            try{complete.complete(new Result(code,data,plan));}catch(RuntimeException|OutOfMemoryError ignored){clear(data);}
        }
    }
    private void checked(Lease lease,int phase,int ticket,String code){
        thread();Job job=active;if(job==null||job.lease!=lease||job.phase!=phase||job.ticket!=ticket||phase!=0&&phase!=4)return;
        if(code!=null){job.finish(code.matches("[a-z][a-z0-9_]{0,63}")?code:"capture_guard_failed",null);return;}
        code=job.check();if(code!=null){job.finish(code,null);return;}if(active!=job||job.done||job.phase!=phase||job.ticket!=ticket)return;
        if(phase==4){byte[] data=job.encoded;job.encoded=null;job.finish(null,data);return;}
        job.phase=1;lease.waitVisual();
        try{target.ready(lease::ready);}catch(RuntimeException|OutOfMemoryError error){if(active==job&&job.phase==1)job.finish("capture_ready_unconfirmed",null);}
    }
    private void visualReady(Lease lease){
        thread();Job job=active;
        if(job==null||job.lease!=lease||job.phase!=1){lease.encoded("capture_cancelled",null);return;}
        String code=job.check();if(code!=null){lease.encoded(code,null);job.finish(code,null);return;}
        if(active!=job||job.done||job.phase!=1){lease.encoded("capture_cancelled",null);return;}
        job.phase=2;Raster raster=null;
        try{
            raster=target.draw(job.plan);if(raster==null)throw new IllegalStateException("Missing native capture raster");
            code=job.check();
            if(active!=job||code!=null){Raster stale=raster;raster=null;
                try{stale.close();}catch(RuntimeException|OutOfMemoryError error){code="capture_cleanup_failed";}
                lease.encoded(code==null?"capture_cancelled":code,null);if(active==job)job.finish(code,null);return;}
            job.work=new Work(raster,lease,job.plan);raster=null;job.phase=3;
            try{worker.execute(job.work);}catch(RuntimeException|OutOfMemoryError error){
                discardWork(job);if(active==job)job.finish("capture_worker_unavailable",null);}
        }catch(RuntimeException|OutOfMemoryError error){
            code=error instanceof OutOfMemoryError?"capture_memory_limit":error instanceof Cleanup?"capture_cleanup_failed":"capture_draw_failed";
            if(raster!=null)try{raster.close();}catch(RuntimeException|OutOfMemoryError cleanup){code="capture_cleanup_failed";}
            lease.encoded(code,null);if(active==job)job.finish(code,null);
        }
    }
    private void discardWork(Job job){
        if(job.work!=null&&job.work.discard())try{worker.remove(job.work);}catch(RuntimeException|OutOfMemoryError ignored){}
    }
    private void encoded(Lease lease){
        thread();if(lease.cleanupFailed())poisoned=true;Job job=active;if(job==null||job.lease!=lease||job.phase!=3){lease.abandon();return;}
        Result result;try{result=lease.take(job.plan);}catch(OutOfMemoryError error){job.finish("capture_memory_limit",null);return;}
        String code=job.check();if(active!=job||job.done||job.phase!=3){clear(result.data);return;}
        if(code==null)code=result.code;if(code==null&&(result.data==null||result.data.length<1||result.data.length>job.plan.maximumBytes))code="capture_reply_unavailable";
        if(code!=null){clear(result.data);job.finish(code,null);return;}
        job.encoded=result.data;job.phase=4;requestGuard(job,4);
    }
    boolean busy(){thread();if(retained!=null&&!retained.retained()){if(retained.cleanupFailed())poisoned=true;retained=null;}return active!=null||retained!=null;}
    boolean cleanupConfirmed(){thread();return !busy()&&!poisoned;}
    void cancel(){thread();if(active!=null)active.finish("capture_cancelled",null);}
    void viewDestroyed(){thread();destroyed=true;if(active!=null)active.finish("capture_closed",null);if(retained!=null)retained.destroyed();}
    @Override public void close(){thread();closed=true;if(active!=null)active.finish("capture_closed",null);}
}
