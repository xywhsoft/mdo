package org.xserver.android;

import java.nio.charset.StandardCharsets;

/** UI-thread native input sequencer. No DOM execution, input replay or global
 * input injection. The caller must bind the target to an admitted document and
 * (for text/keys) its verified DOM focus before using this low-level adapter. */
final class MdoBrowserInput implements AutoCloseable {
    interface Scheduler {
        /** Native MotionEvent/KeyEvent timestamps and gesture cadence: uptime. */
        long now();
        /** Operation budgets include device suspend; single-clock test peers may reuse now(). */
        default long elapsed(){return now();}
        void post(Runnable task,long milliseconds);void remove(Runnable task);
    }
    interface Target {
        long document(); int width(); int height(); boolean interactive(); boolean focused();
        default boolean acquireFocus(){return false;}
        default boolean wheel(long time,float x,float y,float dx,float dy){return false;}
        default boolean hover(long time,float x,float y){return false;}
        /** Bounded native touch-feedback interval before a mouse move. No effects. */
        default long hoverDelay(long now){return 0;}
        boolean touch(int action,long down,long time,float x,float y);
        boolean key(boolean down,long started,long time,String name,int modifiers);
        boolean text(String value);
    }
    interface Completion { void complete(Result result); }
    interface Checked {void complete(String rejection);}
    interface Guard {void check(long remainingMilliseconds,Checked complete);default String rejection(){return null;}}
    interface Invalidated {void changed(String reason);}
    static final class Result {
        final boolean ok,effectMayHaveOccurred,cleanupConfirmed;final String code;final int deliveredSteps;
        Result(String code,boolean sent,boolean clean,int delivered){this.ok=code==null;this.code=code;
            this.effectMayHaveOccurred=sent;this.cleanupConfirmed=clean;this.deliveredSteps=delivered;}
    }
    static final class Failure extends Exception {final String code;Failure(String code){super(code);this.code=code;}}
    static final int DOWN=0,UP=1,MOVE=2,CANCEL=3;
    private final Target target;private final Scheduler scheduler;private final Thread owner=Thread.currentThread();
    private Pending active;private boolean closed,poisoned;private Invalidated invalidated;
    MdoBrowserInput(Target target,Scheduler scheduler){
        if(target==null||scheduler==null)throw new IllegalArgumentException("Missing native input target/scheduler");
        this.target=target;this.scheduler=scheduler;
    }
    private void thread(){if(Thread.currentThread()!=owner)throw new IllegalStateException("Input requires its owning UI thread");}
    private static boolean coordinate(float value,int limit){return Float.isFinite(value)&&value>=0&&value<limit;}
    private static boolean keyName(String value){return value!=null&&(value.matches("[A-Z0-9]")||
        value.matches("Enter|Tab|Backspace|Delete|Escape|Space|Arrow(Left|Right|Up|Down)|Home|End|Page(Up|Down)"));}
    private static boolean validText(String value){
        if(value==null||value.length()>8192)return false;
        for(int i=0;i<value.length();i++){
            char c=value.charAt(i);if(c==0)return false;
            if(Character.isHighSurrogate(c)){if(++i==value.length()||!Character.isLowSurrogate(value.charAt(i)))return false;}
            else if(Character.isLowSurrogate(c))return false;
        }
        return value.getBytes(StandardCharsets.UTF_8).length<=16384;
    }
    void tap(float x,float y,Completion complete)throws Failure {tap(x,y,null,complete);}
    void tap(float x,float y,Guard guard,Completion complete)throws Failure {gesture(new float[]{x,y,x,y},48,guard,complete);}
    void gesture(float[] points,int duration,Completion complete)throws Failure {
        gesture(points,duration,null,complete);
    }
    void gesture(float[] points,int duration,Guard guard,Completion complete)throws Failure {
        thread();if(points==null||points.length<4||points.length>128||points.length%2!=0||duration<16||duration>5000)
            throw new Failure("input_gesture_invalid");
        float[] copy=points.clone();Pending job=admit(complete,false,guard);
        for(int i=0;i<copy.length;i+=2)if(!coordinate(copy[i],job.width)||!coordinate(copy[i+1],job.height))
            throw new Failure("input_coordinate_invalid");
        job.points=copy;job.duration=duration;start(job);
    }
    void key(String name,int modifiers,Completion complete)throws Failure {
        key(name,modifiers,null,complete);
    }
    void wheel(float x,float y,float dx,float dy,Guard guard,Completion complete)throws Failure {
        thread();if(!MdoBrowserWheel.delta(dx)||!MdoBrowserWheel.delta(dy))throw new Failure("input_scroll_invalid");
        Pending job=admit(complete,false,guard);
        if(!coordinate(x,job.width)||!coordinate(y,job.height))throw new Failure("input_coordinate_invalid");
        job.wheel=true;job.x=x;job.y=y;job.dx=dx;job.dy=dy;start(job);
    }
    void hover(float x,float y,Guard guard,Completion complete)throws Failure {
        thread();Pending job=admit(complete,false,guard);
        if(!coordinate(x,job.width)||!coordinate(y,job.height))throw new Failure("input_coordinate_invalid");
        job.hover=true;job.x=x;job.y=y;start(job);
    }
    void key(String name,int modifiers,Guard guard,Completion complete)throws Failure {
        thread();if(!keyName(name)||modifiers<0||(modifiers&~7)!=0)throw new Failure("input_key_invalid");
        Pending job=admit(complete,true,guard);job.name=name;job.modifiers=modifiers;job.duration=16;start(job);
    }
    void text(String value,Completion complete)throws Failure {
        text(value,null,complete);
    }
    void text(String value,Guard guard,Completion complete)throws Failure {
        thread();if(!validText(value))throw new Failure("input_text_invalid");
        Pending job=admit(complete,true,guard);job.text=value;start(job);
    }
    void focus(Guard guard,Completion complete)throws Failure {
        thread();if(guard==null)throw new Failure("input_focus_guard_required");
        Pending job=admit(complete,false,guard);job.focus=true;start(job);
    }
    private Pending admit(Completion complete,boolean needsFocus,Guard guard)throws Failure {
        if(closed)throw new Failure("input_closed");if(poisoned)throw new Failure("input_cleanup_failed");
        if(active!=null)throw new Failure("input_busy");if(complete==null)throw new Failure("input_completion_invalid");
        try{
            if(!target.interactive())throw new Failure("input_view_unavailable");
            int width=target.width(),height=target.height();
            if(width<1||height<1||width>16384||height>16384)throw new Failure("input_viewport_invalid");
            if(needsFocus&&!target.focused())throw new Failure("input_focus_required");
            return new Pending(complete,needsFocus,target.document(),width,height,guard);
        }catch(RuntimeException error){throw new Failure("input_target_failed");}
    }
    private void start(Pending job)throws Failure {
        long elapsed;
        try{job.started=scheduler.now();elapsed=scheduler.elapsed();}
        catch(RuntimeException error){throw new Failure("input_timer_unavailable");}
        active=job;long delay=0;
        if(job.hover){
            try{delay=target.hoverDelay(job.started);}
            catch(RuntimeException|OutOfMemoryError error){if(job.done||active!=job)return;active=null;throw new Failure("input_target_failed");}
            if(job.done||active!=job)return;
            if(delay<0||delay>1000){active=null;throw new Failure("input_timing_invalid");}
        }
        // Keep the original 500ms guarded-input budget after the bounded wait.
        // No guard result or coordinate witness is cached across this interval.
        job.deadline=elapsed+job.duration+delay+500;
        try{
            if(job.check!=null)scheduler.post(job.watchdog,Math.min(50,job.duration+delay+501));
            scheduler.post(job.timer,delay);
        }catch(RuntimeException|Error error){active=null;
            try{scheduler.remove(job.timer);}catch(RuntimeException ignored){}
            try{scheduler.remove(job.watchdog);}catch(RuntimeException ignored){}
            if(error instanceof Error)throw(Error)error;
            throw new Failure("input_timer_unavailable");}
    }
    boolean busy(){thread();return active!=null;}
    boolean cleanupConfirmed(){thread();return !poisoned&&active==null;}
    void observeInvalidation(Invalidated observer){thread();if(observer==null||invalidated!=null||closed)
        throw new IllegalStateException("Invalid input observer binding");invalidated=observer;}
    private void retire(String reason){
        try{if(invalidated!=null)invalidated.changed(reason);}
        catch(RuntimeException|Error failure){poisoned=true;throw failure;}
        finally{if(active!=null)active.finish(reason);}
    }
    void cancel(){invalidate("input_cancelled");}
    void invalidate(String reason){thread();
        if(!("frame_changed".equals(reason)||"input_cancelled".equals(reason)||"user_taken_control".equals(reason)||
             "input_view_unavailable".equals(reason)||"input_viewport_changed".equals(reason)||"input_dialog_open".equals(reason)||
             "file_chooser_open".equals(reason)||"popup_pending".equals(reason)))
            throw new IllegalArgumentException("Invalid native input invalidation");
        retire(reason);
    }
    @Override public void close(){thread();if(closed)return;closed=true;try{retire("input_closed");}finally{invalidated=null;}}
    private final class Pending {
        final Completion complete;final boolean needsFocus;final long document;final int width,height;final Guard check;
        final Runnable timer=this::tick,watchdog=this::checkDeadline;float[] points;String name,text;int modifiers,duration,index,delivered,ticket;
        long started,deadline;float x,y,dx,dy;boolean sent,pressed,keyDown,done,finishing,checking,focus,wheel,hover;
        Pending(Completion complete,boolean needsFocus,long document,int width,int height,Guard check){
            this.complete=complete;this.needsFocus=needsFocus;this.document=document;this.width=width;this.height=height;this.check=check;
        }
        void checkDeadline(){
            thread();if(done||finishing||active!=this)return;
            try{
                long remaining=deadline-scheduler.elapsed();
                if(done||finishing||active!=this)return;
                if(remaining<0){finish("input_timed_out");return;}
                // Handler delay is uptime-based. Short slices recheck the
                // original elapsed deadline after resume without extending it.
                scheduler.post(watchdog,Math.min(50,remaining+1));
            }catch(RuntimeException|OutOfMemoryError error){finish("input_timer_unavailable");}
        }
        private String guard(){
            if(closed)return "input_closed";
            if(poisoned)return "input_cleanup_failed";
            if(target.document()!=document)return "frame_changed";
            if(target.width()!=width||target.height()!=height)return "input_viewport_changed";
            if(!target.interactive())return "input_view_unavailable";
            if(needsFocus&&!target.focused())return "input_focus_lost";return null;
        }
        void tick(){
            thread();if(done||finishing||checking||active!=this)return;
            if(check==null){dispatch();return;}
            String failure;
            try{failure=guard();if(failure==null)failure=check.rejection();if(failure==null&&scheduler.elapsed()>deadline)failure="input_timed_out";}
            catch(RuntimeException|OutOfMemoryError error){failure="input_target_failed";}
            // Scope checks may synchronously retire this exact queued input.
            // Native focus is an effect too; never acquire it after retirement.
            if(done||finishing||active!=this)return;
            if(failure!=null){finish(failure);return;}
            try{
                if(focus){sent=true;if(!target.acquireFocus()){finish("input_focus_required");return;}}
                if(done||finishing||active!=this)return;
                checking=true;final int original=++ticket;
                check.check(Math.max(1,deadline-scheduler.elapsed()),code->{
                    thread();if(done||finishing||active!=this||!checking||original!=ticket)return;checking=false;
                    if(code!=null){finish(code);return;}dispatch();
                });
            }catch(RuntimeException|OutOfMemoryError error){if(!done)finish("input_guard_failed");}
        }
        void dispatch(){
            thread();if(done||finishing||active!=this)return;
            String failure=null;boolean accepted=false;long now=0;
            try{
                now=scheduler.now();
                failure=guard();if(failure==null&&scheduler.elapsed()>deadline)failure="input_timed_out";
                if(done||finishing||active!=this)return;
                if(failure==null){
                    // Mark potential effects and held state before calling the
                    // framework: callbacks may synchronously invalidate us.
                    sent=true;
                    if(focus)accepted=target.focused();
                    else if(hover)accepted=target.hover(now,x,y);
                    else if(wheel)accepted=target.wheel(now,x,y,dx,dy);
                    else if(points!=null){int count=points.length/2;x=points[index*2];y=points[index*2+1];
                        int action=index==0?DOWN:index==count-1?UP:MOVE;
                        if(action==DOWN)pressed=true;
                        accepted=target.touch(action,started,now,x,y);
                        if(action==UP&&accepted)pressed=false;
                    }else if(name!=null){if(index==0)keyDown=true;
                        accepted=target.key(index==0,started,now,name,modifiers);
                        if(index!=0&&accepted)keyDown=false;
                    }else accepted=target.text(text);
                }
            }catch(RuntimeException|OutOfMemoryError error){failure="input_dispatch_failed";}
            if(done||finishing||active!=this)return;
            if(failure!=null){finish(failure);return;}
            if(!accepted){finish("input_not_accepted");return;}
            delivered++;index++;
            try{failure=guard();}catch(RuntimeException|OutOfMemoryError error){failure="input_target_failed";}
            if(failure!=null){finish(failure);return;}
            int count=points!=null?points.length/2:name!=null?2:1;
            if(index==count){finish(null);return;}
            long next=started+((long)duration*index/(count-1));
            try{scheduler.post(timer,Math.max(1,next-scheduler.now()));}
            catch(RuntimeException|OutOfMemoryError error){finish("input_timer_unavailable");}
        }
        void finish(String code){
            if(done||finishing)return;finishing=true;boolean clean=true;
            try{scheduler.remove(timer);}catch(RuntimeException|OutOfMemoryError error){clean=false;}
            if(check!=null)try{scheduler.remove(watchdog);}catch(RuntimeException|OutOfMemoryError error){clean=false;}
            if(pressed)try{if(!target.touch(CANCEL,started,scheduler.now(),x,y))clean=false;}
                catch(RuntimeException|OutOfMemoryError error){clean=false;}
            if(keyDown)try{if(!target.key(false,started,scheduler.now(),name,modifiers))clean=false;}
                catch(RuntimeException|OutOfMemoryError error){clean=false;}
            pressed=false;keyDown=false;text=null;points=null;clean&=!poisoned;poisoned|=!clean;
            done=true;finishing=false;checking=false;ticket++;if(active==this)active=null;
            try{complete.complete(new Result(code!=null?code:clean?null:"input_cleanup_failed",sent,clean,delivered));}
            catch(RuntimeException|OutOfMemoryError error){
                // A lost result allocation/delivery cannot authorize replay or
                // further effects, including work admitted by a reentrant
                // completion. Native held state was already settled above.
                poisoned=true;Pending next=active;if(next!=null)next.finish("input_cleanup_failed");
            }
        }
    }
}
