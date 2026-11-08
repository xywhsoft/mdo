package org.xserver.android;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.function.BooleanSupplier;
import java.util.function.LongSupplier;

/** Retains only the original native chooser callback. URI grants are deliberately
 * absent until a native file-source/target proof is implemented. UI-thread only. */
final class MdoBrowserChoosers implements AutoCloseable {
    interface Scheduler {long now();void at(Runnable task,long uptime);void remove(Runnable task);}
    interface Response {void cancel();}
    interface Listener {void blocked(boolean value);}
    private static boolean typeFits(String value){
        if(value.length()>256)return false;int bytes=0;
        for(int at=0;at<value.length();){
            int code=value.codePointAt(at);if(code<32||code==127||code>=0xd800&&code<=0xdfff)return false;
            bytes+=code<128?1:code<2048?2:code<65536?3:4;if(bytes>256)return false;at+=Character.charCount(code);
        }
        return true;
    }
    static final class Snapshot {
        final String id;final int mode;final boolean multiple,directory,capture,cancelable;
        final long navigation;final List<String> accept;
        Snapshot(Pending value){id=value.id;mode=value.mode;multiple=mode==1;directory=mode==2;capture=value.capture;
            navigation=value.navigation;accept=value.accept;cancelable=!value.sending&&!value.unclean;}
    }
    static final class Result {
        final boolean ok,effectMayHaveOccurred,cleanupConfirmed;final String code;
        Result(boolean ok,String code,boolean effect,boolean clean){this.ok=ok;this.code=code;
            effectMayHaveOccurred=effect;cleanupConfirmed=clean;}
    }
    private static final class Pending {
        final Object token;final Response response;final String id;final int mode;final boolean capture;
        final long navigation,deadline;final List<String> accept;Runnable timer;boolean sending,unclean;
        Pending(Object token,Response response,int mode,String[] types,boolean capture,long epoch,long deadline){
            this.token=token;this.response=response;this.mode=mode;this.capture=capture;navigation=epoch;this.deadline=deadline;
            id="chooser-"+UUID.randomUUID();List<String> values=new ArrayList<>();
            if(types!=null){
                if(types.length>16)throw new IllegalArgumentException("chooser_metadata_invalid");
                for(String type:types){
                    if(type==null)continue;String value=type.trim();
                    if(!typeFits(value))throw new IllegalArgumentException("chooser_metadata_invalid");
                    if(!value.isEmpty()&&!values.contains(value))values.add(value);
                }
            }
            accept=Collections.unmodifiableList(values);
        }
    }
    private static final class Expiry implements Runnable {
        final WeakReference<MdoBrowserChoosers> owner;final String id;
        Expiry(MdoBrowserChoosers owner,String id){this.owner=new WeakReference<>(owner);this.id=id;}
        public void run(){MdoBrowserChoosers value=owner.get();if(value!=null)value.expire(id);}
    }
    private final Object view;private final Scheduler scheduler;private final BooleanSupplier owned;
    private final LongSupplier navigation;private final Listener listener;private final Thread thread=Thread.currentThread();
    private final List<WeakReference<Object>> seen=new ArrayList<>();
    private Object last;private Pending active;private boolean enabled,closed,destroyed,failed,blocked;
    MdoBrowserChoosers(Object view,Scheduler scheduler,BooleanSupplier owned,LongSupplier navigation,Listener listener){
        if(view==null||scheduler==null||owned==null||navigation==null||listener==null)
            throw new IllegalArgumentException("Missing native chooser binding");
        this.view=view;this.scheduler=scheduler;this.owned=owned;this.navigation=navigation;this.listener=listener;
    }
    private void thread(){if(Thread.currentThread()!=thread)throw new IllegalStateException("Choosers require their owning UI thread");}
    boolean known(Object token){
        thread();if(token==null||destroyed)return false;if(last==token||active!=null&&active.token==token)return true;
        for(int index=seen.size()-1;index>=0;index--){Object value=seen.get(index).get();if(value==null)seen.remove(index);else if(value==token)return true;}
        return false;
    }
    private void block(){
        boolean value=!destroyed&&(failed||active!=null);if(blocked==value)return;blocked=value;
        try{listener.blocked(value);}catch(RuntimeException|OutOfMemoryError error){if(!destroyed){failed=true;blocked=true;}}
    }
    private boolean claim(Object token){
        if(known(token))return false;
        // Never forget a live callback merely to admit another one. A provider
        // retaining 128 retired callbacks requires this exact view's teardown.
        if(seen.size()>=128){failed=true;block();return false;}
        last=token; // Once-only identity survives an allocation failure below.
        try{seen.add(new WeakReference<>(token));}catch(OutOfMemoryError error){failed=true;block();}
        return true;
    }
    boolean intercepting(){thread();return enabled&&!closed&&!destroyed&&!failed;}
    void interception(boolean value){thread();enabled=value&&!closed&&!destroyed&&!failed;if(!enabled)cancel();}
    private void reject(Response response){
        try{response.cancel();}catch(RuntimeException|OutOfMemoryError error){if(!destroyed){failed=true;block();}}
    }
    boolean rejectOnce(Object token){
        thread();return token!=null&&!known(token)&&intercepting()&&claim(token);
    }
    boolean claimManual(Object token){thread();return token!=null&&!closed&&!destroyed&&!enabled&&!failed&&!known(token)&&claim(token);}
    // Private native picker rejection/delivery shares this same once-only
    // callback ledger, including late callbacks after close but before teardown.
    boolean claimPrivate(Object token){thread();return token!=null&&!destroyed&&!known(token)&&claim(token);}
    void rejectionFailed(){thread();if(!destroyed){failed=true;block();}}
    boolean open(Object token,Response response,int mode,String[] accept,boolean capture){
        thread();if(known(token))return true;
        if(token==null||response==null||!intercepting()||!claim(token))return false;
        Pending pending=null;boolean published=false;
        try{
            long epoch=navigation.getAsLong(),now=scheduler.now();
            boolean live=owned.getAsBoolean();
            if(destroyed)return true;
            if(failed||closed||!enabled||active!=null||!live||epoch<=0||now<0||now>Long.MAX_VALUE-120000||mode<0||mode>3){reject(response);return true;}
            pending=new Pending(token,response,mode,accept,capture,epoch,now+120000);
            pending.timer=new Expiry(this,pending.id);active=pending;published=true;block();
            if(active!=pending||closed||destroyed)return true;
            if(failed){finish(pending);return true;}
            scheduler.at(pending.timer,pending.deadline);
            if(active==pending&&scheduler.now()>=pending.deadline)finish(pending);
        }catch(RuntimeException|OutOfMemoryError error){
            if(!published){if(!destroyed)reject(response);}else if(active==pending)finish(pending);
        }
        return true;
    }
    private void remove(Pending pending){try{scheduler.remove(pending.timer);}catch(RuntimeException|OutOfMemoryError ignored){/* Exact-ID weak timer becomes inert after retirement. */}}
    private Result refused(String code){return new Result(false,code,false,!failed);}
    private Result finish(Pending pending){
        if(active!=pending||pending.sending||pending.unclean)return refused("chooser_not_pending");
        pending.sending=true;remove(pending);
        try{pending.response.cancel();}
        catch(RuntimeException|OutOfMemoryError error){
            if(destroyed)return new Result(false,"chooser_response_uncertain",true,true);
            pending.sending=false;pending.unclean=true;failed=true;block();
            return new Result(false,"chooser_response_uncertain",true,false);
        }
        if(active==pending)active=null;block();
        return new Result(!failed,failed?"chooser_cleanup_failed":null,true,!failed);
    }
    private void expire(String id){
        thread();Pending pending=active;
        if(pending==null||!pending.id.equals(id)||pending.sending||pending.unclean)return;
        try{if(scheduler.now()<pending.deadline){scheduler.at(pending.timer,pending.deadline);return;}}
        catch(RuntimeException|OutOfMemoryError ignored){}
        finish(pending);
    }
    private void current(){
        Pending pending=active;if(pending==null||pending.sending||pending.unclean)return;
        try{if(intercepting()&&owned.getAsBoolean()&&navigation.getAsLong()==pending.navigation&&scheduler.now()<pending.deadline)return;}
        catch(RuntimeException|OutOfMemoryError ignored){}
        finish(pending);
    }
    Snapshot snapshot(){thread();current();return active==null?null:new Snapshot(active);}
    boolean clean(){thread();return !failed;}
    boolean ready(){thread();current();return !failed&&active==null;}
    Result cancel(String id){
        thread();current();Pending pending=active;
        if(pending==null||id==null||!pending.id.equals(id))return refused("chooser_not_pending");
        return finish(pending);
    }
    void cancel(){thread();if(active!=null)finish(active);}
    @Override public void close(){thread();enabled=false;closed=true;cancel();}
    void viewDestroyed(Object original){
        thread();if(!closed||original!=view)throw new IllegalStateException("Close the exact chooser view before teardown");
        if(destroyed)return;destroyed=true;if(active!=null)remove(active);active=null;last=null;seen.clear();failed=false;block();
    }
}
