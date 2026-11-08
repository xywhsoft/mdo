package org.xserver.android;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.ref.WeakReference;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.Callable;

/** Small adapter to the installed WebView's published support-library boundary.
 * No AndroidX/AAR, hidden framework API or page-world evaluation is used.
 * Protocol signatures/revision: host/android/protocol.json. UI-thread confined.
 */
final class MdoBrowserWorld implements AutoCloseable {
    static final String WORLD="mdo.browser-use.v1", LISTENER="__mdoBrowserWorld";
    private static final String PREFIX="org.chromium.support_lib_boundary.";
    interface Scheduler { long now(); void post(Runnable task,long milliseconds); void remove(Runnable task); }
    interface Receiver { void message(Peer peer,String text); void fault(String code); }
    interface Completion { void complete(Result result); }
    static final class Result {
        final boolean ok,evaluationMayHaveOccurred;final String value,code;
        Result(boolean ok,String value,String code,boolean sent){this.ok=ok;this.value=value;this.code=code;this.evaluationMayHaveOccurred=sent;}
    }
    static final class Failure extends Exception {
        final String code;Failure(String code){super(code);this.code=code;}
    }
    static final class Budget {
        private final Thread owner=Thread.currentThread();private final int limit;private int used;
        Budget(int limit){if(limit<1||limit>128)throw new IllegalArgumentException("Invalid world budget");this.limit=limit;}
        private void thread(){if(Thread.currentThread()!=owner)throw new IllegalStateException("World budget requires its owning UI thread");}
        boolean acquire(){thread();if(used>=limit)return false;used++;return true;}
        void release(){thread();if(used<=0)throw new IllegalStateException("World budget underflow");used--;}
        int used(){thread();return used;}int limit(){thread();return limit;}
    }
    static final class Features {
        final boolean isolatedWorld,profiles,messageListener;final int count;
        private final ClassLoader loader;private final InvocationHandler factory;
        Features(ClassLoader loader,InvocationHandler factory,Set<String> names,int count){
            this.loader=loader;this.factory=factory;this.count=count;
            isolatedWorld=names.contains("JS_INJECTION_IN_FRAME_AND_WORLD");profiles=names.contains("MULTI_PROFILE");messageListener=names.contains("WEB_MESSAGE_LISTENER");
        }
    }
    private final Object view;private final ClassLoader loader;private final InvocationHandler provider;
    private final Scheduler scheduler;private final Receiver receiver;private final Thread owner=Thread.currentThread();
    private final Set<Pending> pending=new HashSet<>();private final Set<NativeCall> outstanding=new HashSet<>();
    private final MdoBrowserProfile profile;private final boolean ownsProfile;private final Budget budget;
    private InvocationHandler script;private Peer current;private boolean listening,closed,scriptUncertain;
    private boolean installAttempted;

    static Features inspect(ClassLoader loader) throws Failure {
        if(loader==null)throw new Failure("world_provider_unavailable");
        try{
            Class<?> glue=Class.forName("org.chromium.support_lib_glue.SupportLibReflectionUtil",false,loader);
            Object raw=glue.getDeclaredMethod("createWebViewProviderFactory").invoke(null);
            if(!(raw instanceof InvocationHandler))throw new Failure("world_provider_unavailable");
            InvocationHandler factory=(InvocationHandler)raw;
            Object featureList=invoke(loader,factory,"WebViewProviderFactoryBoundaryInterface","getSupportedFeatures");
            if(!(featureList instanceof String[])||((String[])featureList).length>512)throw new Failure("world_features_invalid");
            Set<String> features=new HashSet<>();for(String name:(String[])featureList)if(name!=null&&name.length()<=256)features.add(name);
            return new Features(loader,factory,features,((String[])featureList).length);
        }catch(Failure failure){throw failure;}catch(ReflectiveOperationException|RuntimeException|LinkageError error){throw new Failure("world_provider_unavailable");}
    }
    static MdoBrowserWorld open(Object view,ClassLoader loader,Scheduler scheduler,Receiver receiver) throws Failure {
        return open(view,loader,scheduler,receiver,new Budget(16));
    }
    static MdoBrowserWorld open(Object view,ClassLoader loader,Scheduler scheduler,Receiver receiver,Budget budget) throws Failure {
        if(view==null||loader==null||scheduler==null||receiver==null||budget==null)throw new Failure("world_arguments_invalid");budget.used();
        Features features=inspect(loader);
        if(!features.isolatedWorld)throw new Failure("isolated_world_unsupported");
        InvocationHandler provider=provider(features,view);
        return new MdoBrowserWorld(view,features.loader,provider,scheduler,receiver,new MdoBrowserProfile(view,loader,provider,features.profiles),true,budget);
    }
    static MdoBrowserWorld borrow(Object view,ClassLoader loader,Scheduler scheduler,Receiver receiver,Budget budget,MdoBrowserProfile profile)throws Failure {
        if(view==null||loader==null||scheduler==null||receiver==null||budget==null||profile==null)throw new Failure("world_arguments_invalid");budget.used();
        profile.provider(view,loader);Features features=inspect(loader);if(!features.isolatedWorld)throw new Failure("isolated_world_unsupported");
        return new MdoBrowserWorld(view,loader,profile.provider(view,loader),scheduler,receiver,profile,false,budget);
    }
    static InvocationHandler provider(Features features,Object view)throws Failure {
        try{
            Object result=invoke(features.loader,features.factory,"WebViewProviderFactoryBoundaryInterface","createWebView",view);
            if(!(result instanceof InvocationHandler))throw new Failure("world_provider_unavailable");return(InvocationHandler)result;
        }catch(Failure failure){throw failure;}catch(RuntimeException|LinkageError error){throw new Failure("world_provider_unavailable");}
    }
    private MdoBrowserWorld(Object view,ClassLoader loader,InvocationHandler provider,Scheduler scheduler,Receiver receiver,MdoBrowserProfile profile,boolean ownsProfile,Budget budget){
        this.view=view;this.loader=loader;this.provider=provider;this.scheduler=scheduler;this.receiver=receiver;this.profile=profile;this.ownsProfile=ownsProfile;
        this.budget=budget;
    }
    private void thread(){if(Thread.currentThread()!=owner)throw new IllegalStateException("World operations require their owning UI thread");}
    private static boolean compatible(Class<?> type,Object value){
        if(value==null)return!type.isPrimitive();if(type.isInstance(value))return true;
        return type==int.class&&value instanceof Integer||type==boolean.class&&value instanceof Boolean||type==long.class&&value instanceof Long;
    }
    static Object invoke(ClassLoader loader,InvocationHandler handler,String name,String operation,Object... values) throws Failure {
        try{
            Class<?> boundary=Class.forName(PREFIX+name,false,loader);Method selected=null;
            for(Method method:boundary.getMethods()){
                if(!method.getName().equals(operation)||method.getParameterCount()!=values.length)continue;
                Class<?>[] types=method.getParameterTypes();boolean matches=true;
                for(int i=0;i<types.length;i++)matches&=compatible(types[i],values[i]);
                if(matches){if(selected!=null)throw new Failure("world_method_ambiguous");selected=method;}
            }
            if(selected==null)throw new Failure("world_method_unavailable");
            return handler.invoke(null,selected,values);
        }catch(VirtualMachineError|ThreadDeath fatal){throw fatal;}catch(Failure failure){throw failure;}catch(Throwable error){throw new Failure("world_boundary_failed");}
    }
    boolean supportsProfile(){return profile.supported();}
    void profile(String name) throws Failure {
        thread();profile.available();if(closed||installAttempted)throw new Failure("world_profile_order");profile.assign(name);
    }
    void install(String bootstrap) throws Failure {
        thread();profile.available();
        if(closed||installAttempted)throw new Failure("world_install_order");
        if(!bounded(bootstrap,131072))throw new Failure("world_script_limit");
        profile.seal();installAttempted=true;
        Object world=invoke(loader,provider,"WebViewProviderBoundaryInterface","getJavaScriptWorld",WORLD);
        if(!(world instanceof Integer)||((Integer)world)<=0)throw new Failure("isolated_world_unsupported");
        InvocationHandler listener=(proxy,method,args)->{
            if("getSupportedFeatures".equals(method.getName()))return new String[]{"WEB_MESSAGE_LISTENER"};
            if(!"onPostMessage".equals(method.getName()))return null;
            thread();if(closed)return null;
            if(args==null||args.length!=5||args[0]!=view||!Boolean.TRUE.equals(args[3]))return null;
            if(!(args[1] instanceof InvocationHandler)||!(args[4] instanceof InvocationHandler)){receiver.fault("world_message_invalid");return null;}
            try{
                Object text=invoke(loader,(InvocationHandler)args[1],"WebMessageBoundaryInterface","getData");
                if(!(text instanceof String)||!bounded((String)text,16384)){receiver.fault("world_message_limit");return null;}
                InvocationHandler reply=(InvocationHandler)args[4];
                Object peer=invoke(loader,reply,"JsReplyProxyBoundaryInterface","getOrCreatePeer",(Callable<Object>)()->new Peer(reply));
                if(!(peer instanceof MdoBrowserWorld.Peer)||((Peer)peer).world()!=this){receiver.fault("world_peer_mismatch");return null;}
                Peer active=(Peer)peer;if(active.retired)return null;
                if(current!=null&&current!=active)retire(current,"frame_changed");current=active;
                receiver.message(active,(String)text);
            }catch(Failure failure){receiver.fault(failure.code);}return null;
        };
        try{
            listening=true;
            invoke(loader,provider,"WebViewProviderBoundaryInterface","addWebMessageListener",LISTENER,new String[]{"*"},listener,WORLD);
            scriptUncertain=true;
            Object handle=invoke(loader,provider,"WebViewProviderBoundaryInterface","addJavaScriptOnEvent",bootstrap,new String[]{"*"},0,WORLD);
            if(!(handle instanceof InvocationHandler))throw new Failure("world_script_unavailable");script=(InvocationHandler)handle;scriptUncertain=false;
        }catch(Failure failure){close();throw failure;}
    }
    private static boolean bounded(String value,int maximum){return value!=null&&value.length()<=maximum&&value.getBytes(StandardCharsets.UTF_8).length<=maximum;}
    private void retire(Peer peer,String reason){
        peer.retired=true;for(Pending job:new ArrayList<>(pending))if(job.peer==peer)job.finish(null,reason);
    }
    void invalidate(){thread();if(current!=null){retire(current,"frame_changed");current=null;}}
    void invalidate(Peer expected){
        thread();if(expected==null)return;retire(expected,"frame_changed");if(current==expected)current=null;
    }
    boolean current(Peer peer){thread();return!closed&&(ownsProfile||profile.owns(view))&&peer!=null&&current==peer&&!peer.retired;}
    void cancelPending(String reason){
        thread();for(Pending job:new ArrayList<>(pending))job.finish(null,reason);
    }
    final class Peer {
        private final InvocationHandler reply;private boolean retired;
        Peer(InvocationHandler reply){this.reply=reply;}private MdoBrowserWorld world(){return MdoBrowserWorld.this;}
        void post(String text) throws Failure {
            thread();if(!current(this))throw new Failure("frame_changed");
            if(!bounded(text,16384))throw new Failure("world_message_limit");
            invoke(loader,reply,"JsReplyProxyBoundaryInterface","postMessage",text);
        }
        void evaluate(String expression,long timeout,Completion complete) throws Failure {
            thread();if(!current(this))throw new Failure("frame_changed");
            if(!bounded(expression,131072)||timeout<1||timeout>10000||complete==null)throw new Failure("world_evaluation_invalid");
            if(outstanding.size()>=16)throw new Failure("world_job_limit");
            NativeCall callback=new NativeCall(MdoBrowserWorld.this,budget);
            Pending job=new Pending(this,complete,scheduler.now()+timeout,callback);callback.delivery=job;
            if(!budget.acquire())throw new Failure("world_shared_job_limit");callback.reserved=true;
            try{outstanding.add(callback);pending.add(job);scheduler.post(job.timer,Math.min(timeout,50));}
            catch(RuntimeException|Error error){
                job.done=true;job.complete=null;pending.remove(job);callback.release();
                try{scheduler.remove(job.timer);}catch(RuntimeException ignored){}
                if(error instanceof Error)throw(Error)error;throw new Failure("world_timer_unavailable");
            }
            // Timer registration may resume after sleep or reenter document
            // retirement. No provider owns this callback until native dispatch;
            // release its unused reservation instead of sending expired work.
            String rejected=!current(this)?"frame_changed":scheduler.now()>=job.deadline?"world_timed_out":null;
            if(job.done||rejected!=null){callback.release();if(!job.done)job.finish(null,rejected);return;}
            job.sent=true;
            try{invoke(loader,reply,"JsReplyProxyBoundaryInterface","executeJavaScript",expression,callback);}
            catch(Failure failure){job.finish(null,failure.code);}
        }
    }
    /** A provider can retain this handler after the client's deadline. It must
     * not retain the completed UI/session callback or an old Activity graph. */
    private static final class NativeCall implements InvocationHandler {
        final WeakReference<MdoBrowserWorld> owner;final Budget budget;
        Pending delivery;boolean reserved;
        NativeCall(MdoBrowserWorld owner,Budget budget){this.owner=new WeakReference<>(owner);this.budget=budget;}
        void release(){
            budget.thread();delivery=null;if(!reserved)return;reserved=false;budget.release();
            MdoBrowserWorld world=owner.get();if(world!=null)world.outstanding.remove(this);
        }
        public Object invoke(Object proxy,Method method,Object[] args){
            budget.thread();Pending job=delivery;String name=method.getName();
            boolean terminal="onSuccess".equals(name)||"onFailure".equals(name);
            if(terminal)release();
            if(job==null||job.done)return null;
            if("onSuccess".equals(name)&&args!=null&&args.length==1&&args[0] instanceof String){
                String value=(String)args[0];boolean valid=bounded(value,262144);job.finish(valid?value:null,valid?null:"world_result_limit");
            }else if("onFailure".equals(name)&&args!=null&&args.length==2){
                boolean gone=Integer.valueOf(1).equals(args[0]);if(gone)job.peer.world().retire(job.peer,"frame_changed");
                job.finish(null,gone?"frame_changed":"world_evaluation_failed");
            }else job.finish(null,"world_callback_invalid");return null;
        }
    }
    private final class Pending {
        final Peer peer;Completion complete;final long deadline;final Runnable timer;final NativeCall call;boolean sent,done;
        Pending(Peer peer,Completion complete,long deadline,NativeCall call){this.peer=peer;this.complete=complete;this.deadline=deadline;this.call=call;timer=this::checkDeadline;}
        void checkDeadline(){
            if(done)return;
            long remaining=deadline-scheduler.now();
            if(remaining<=0){finish(null,"world_timed_out");return;}
            // Handler delays use uptime. A short check observes the original
            // elapsed deadline promptly after resume without renewing it.
            try{scheduler.post(timer,Math.min(remaining,50));}
            catch(RuntimeException rejected){finish(null,"world_timer_unavailable");}
        }
        void finish(String value,String code){
            if(done)return;done=true;pending.remove(this);call.delivery=null;
            Completion destination=complete;complete=null;
            try{scheduler.remove(timer);}catch(RuntimeException error){code="world_timer_cleanup_failed";value=null;}
            if(scheduler.now()>=deadline){value=null;code="world_timed_out";}
            if(code==null&&!current(peer)){value=null;code="frame_changed";}
            try{destination.complete(new Result(code==null,value,code,sent));}catch(RuntimeException error){receiver.fault("world_callback_failed");}
        }
    }
    int pending(){thread();return pending.size();}
    int outstanding(){thread();return outstanding.size();}
    int sharedOutstanding(){thread();return budget.used();}
    int sharedLimit(){thread();return budget.limit();}
    @Override public void close(){
        thread();if(ownsProfile)profile.close();if(!closed){closed=true;invalidate();for(Pending job:new ArrayList<>(pending))job.finish(null,"world_closed");}
        if(script!=null)try{invoke(loader,script,"ScriptHandlerBoundaryInterface","remove");script=null;}catch(Failure failure){receiver.fault("world_script_cleanup_failed");}
        if(listening)try{invoke(loader,provider,"WebViewProviderBoundaryInterface","removeWebMessageListener",LISTENER,WORLD);listening=false;}catch(Failure failure){receiver.fault("world_listener_cleanup_failed");}
    }
    void viewDestroyed(Object original){
        thread();if(!closed||original!=view)throw new IllegalStateException("Confirm only the exact closed native view");
        script=null;scriptUncertain=false;listening=false;if(ownsProfile)profile.viewDestroyed(original);
        for(NativeCall call:new ArrayList<>(outstanding))call.release();
    }
    boolean cleanupConfirmed(){thread();return closed&&script==null&&!scriptUncertain&&(!ownsProfile||profile.cleanupConfirmed())&&!listening&&pending.isEmpty()&&outstanding.isEmpty();}
}
