package org.xserver.android;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.function.BooleanSupplier;
import java.util.function.LongSupplier;

/** One native WebView's retained modal result. This does not infer a frame from
 * a callback URL. All calls, including native result delivery, use the UI thread. */
final class MdoBrowserDialogs implements AutoCloseable {
    interface Scheduler {long now();void at(Runnable task,long uptime);void remove(Runnable task);}
    interface Response {void send(boolean accept,String text);}
    interface Listener {void blocked(boolean value);}
    static final class Snapshot {
        final String id,kind,url,message;final long navigation;final boolean respondable;
        Snapshot(Pending pending){id=pending.id;kind=pending.kind;url=pending.url;message=pending.message;
            navigation=pending.navigation;respondable=!pending.sending&&!pending.unclean;}
    }
    static final class Result {
        final boolean ok,effectMayHaveOccurred,cleanupConfirmed;final String code;
        Result(boolean ok,String code,boolean effect,boolean clean){this.ok=ok;this.code=code;
            effectMayHaveOccurred=effect;cleanupConfirmed=clean;}
    }
    private static final class Pending {
        final Object token;final Response response;final String id,kind,url,message,initial;
        final long navigation,deadline;Runnable timer;boolean sending,unclean;
        Pending(Object token,Response response,String kind,String url,String message,String initial,long navigation,long deadline){
            this.token=token;this.response=response;this.kind=kind;this.url=url;this.message=message;this.initial=initial;
            this.navigation=navigation;this.deadline=deadline;id="dialog-"+UUID.randomUUID();
        }
    }
    private static final class Expiry implements Runnable {
        final WeakReference<MdoBrowserDialogs> owner;final String id;
        Expiry(MdoBrowserDialogs owner,String id){this.owner=new WeakReference<>(owner);this.id=id;}
        public void run(){MdoBrowserDialogs current=owner.get();if(current!=null)current.expire(id);}
    }
    private final Object view;private final Scheduler scheduler;private final BooleanSupplier owned;
    private final LongSupplier navigation;private final Listener listener;private final Thread thread=Thread.currentThread();
    private final List<WeakReference<Object>> settled=new ArrayList<>();
    private Pending active;private Object lastToken;private boolean closed,blocked,failed,destroyed;
    MdoBrowserDialogs(Object view,Scheduler scheduler,BooleanSupplier owned,LongSupplier navigation,Listener listener){
        if(view==null||scheduler==null||owned==null||navigation==null||listener==null)throw new IllegalArgumentException("Missing native dialog binding");
        this.view=view;this.scheduler=scheduler;this.owned=owned;this.navigation=navigation;this.listener=listener;
    }
    private void thread(){if(Thread.currentThread()!=thread)throw new IllegalStateException("Dialogs require their owning UI thread");}
    private static String bounded(String value,int maximum){
        if(value==null)return "";int at=0,bytes=0;
        while(at<value.length()){
            int code=value.codePointAt(at),size=code<128?1:code<2048?2:code<65536?3:4;
            if(bytes+size>maximum)break;bytes+=size;at+=Character.charCount(code);
        }
        return value.substring(0,at);
    }
    private static boolean textFits(String value){return value==null||bounded(value,8192).length()==value.length();}
    private boolean seen(Object token){
        if(lastToken==token||active!=null&&active.token==token)return true;
        for(int index=0;index<settled.size();index++)if(settled.get(index).get()==token)return true;return false;
    }
    private void remember(Object token){
        if(destroyed)return;
        // Assign the once-only tombstone before any allocation or native call.
        // Keep at most one strong result, cleared at exact view destruction.
        lastToken=token;
        try{if(settled.size()==8)settled.remove(0);settled.add(new WeakReference<>(token));}
        catch(OutOfMemoryError error){failed=true;}
    }
    private void unblock(){
        if(!blocked)return;blocked=false;
        try{listener.blocked(false);}catch(RuntimeException|OutOfMemoryError error){failed=true;}
    }
    private Result refusal(String code){return new Result(false,code,false,clean());}
    boolean rejectOnce(Object token){
        thread();if(token==null||destroyed||seen(token))return false;
        remember(token);return true;
    }
    void rejectionFailed(){thread();if(!destroyed)failed=true;}
    private void reject(Object token,Response response){
        if(response==null||!rejectOnce(token))return;
        try{response.send(false,null);}catch(RuntimeException|OutOfMemoryError error){rejectionFailed();}
    }
    boolean open(Object token,Response response,String kind,String url,String message,String initial){
        thread();if(token==null||response==null)throw new IllegalArgumentException("Missing native dialog result");
        if(destroyed)return false;
        if(seen(token))return active!=null&&active.token==token;
        boolean live=false;long epoch=0,now=0;
        try{live=!closed&&!failed&&active==null&&owned.getAsBoolean();epoch=navigation.getAsLong();now=scheduler.now();}
        catch(RuntimeException|OutOfMemoryError error){live=false;}
        Pending pending=null;
        try{
            if(!live||epoch<=0||now<0||now>Long.MAX_VALUE-120000||
               !("alert".equals(kind)||"confirm".equals(kind)||"prompt".equals(kind)||"beforeunload".equals(kind))||
               "prompt".equals(kind)&&!textFits(initial)){
                reject(token,response);return false;
            }
            pending=new Pending(token,response,kind,bounded(url,2048),bounded(message,4096),
                "prompt".equals(kind)?(initial==null?"":initial):null,epoch,now+120000);
            pending.timer=new Expiry(this,pending.id);active=pending;
            blocked=true;listener.blocked(true);
            if(active!=pending||closed||failed)return false;
            scheduler.at(pending.timer,pending.deadline);
            if(active!=pending)return false;
            if(scheduler.now()>=pending.deadline){finish(pending,false,null);return false;}
            return true;
        }catch(RuntimeException|OutOfMemoryError error){
            if(pending!=null&&active==pending)finish(pending,false,null);else reject(token,response);return false;
        }
    }
    private void removeTimer(Pending pending){try{scheduler.remove(pending.timer);}catch(RuntimeException|OutOfMemoryError ignored){/* Weak, exact-ID timer is inert after settlement. */}}
    private Result finish(Pending pending,boolean accept,String text){
        if(active!=pending||pending.sending||pending.unclean)return refusal("dialog_not_pending");
        pending.sending=true;removeTimer(pending);
        try{pending.response.send(accept,text);}
        catch(RuntimeException|OutOfMemoryError error){
            // A lost acknowledgement cannot authorize either confirm or cancel
            // to be replayed. Keep admission blocked until exact view teardown.
            if(destroyed)return new Result(false,"dialog_response_uncertain",true,true);
            pending.sending=false;pending.unclean=true;failed=true;
            return new Result(false,"dialog_response_uncertain",true,false);
        }
        remember(pending.token);if(active==pending)active=null;unblock();
        return new Result(!failed,failed?"dialog_cleanup_failed":null,true,!failed);
    }
    private void expire(String id){
        thread();Pending pending=active;
        if(pending==null||!pending.id.equals(id)||pending.sending||pending.unclean)return;
        try{
            if(scheduler.now()<pending.deadline){scheduler.at(pending.timer,pending.deadline);return;}
        }catch(RuntimeException|OutOfMemoryError error){/* A timer that cannot be retained must cancel this result. */}
        finish(pending,false,null);
    }
    private void current(){
        Pending pending=active;if(pending==null||pending.sending||pending.unclean)return;
        try{if(owned.getAsBoolean()&&navigation.getAsLong()==pending.navigation&&scheduler.now()<pending.deadline)return;}
        catch(RuntimeException|OutOfMemoryError ignored){}
        finish(pending,false,null);
    }
    Snapshot snapshot(){thread();current();return active==null?null:new Snapshot(active);}
    boolean clean(){thread();return !failed;}
    Result respond(String id,boolean accept,boolean hasText,String text){
        thread();current();Pending pending=active;
        if(closed||pending==null||!pending.id.equals(id)||pending.sending||pending.unclean)return refusal("dialog_not_pending");
        if(!textFits(text)||hasText&&text==null)return refusal("dialog_text_invalid");
        return finish(pending,accept,"prompt".equals(pending.kind)?(hasText?text:pending.initial):null);
    }
    void cancel(){thread();if(active!=null)finish(active,false,null);}
    @Override public void close(){thread();closed=true;cancel();}
    void viewDestroyed(Object original){
        thread();if(!closed||original!=view)throw new IllegalStateException("Close the exact dialog view before teardown");
        if(destroyed)return;destroyed=true;if(active!=null)removeTimer(active);active=null;lastToken=null;settled.clear();failed=false;unblock();
    }
}
