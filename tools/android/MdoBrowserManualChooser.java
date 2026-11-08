package org.xserver.android;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebView;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.function.BooleanSupplier;

/** One human document picker per Activity. No Agent URI assignment or private
 * provider access. Cancelled native launches keep their original result slot. */
final class MdoBrowserManualChooser implements AutoCloseable {
    private static final int FIRST=0x6000,LAST=0x7fff;private static int next=FIRST;
    private static final String SAVED="mdo.browser.manual.chooser.next";
    private static final class Pending {
        final int code;final long deadline;final boolean multiple;WebView view;ValueCallback<Uri[]> reply;BooleanSupplier original;Runnable unblock;Runnable timer;boolean handling;
        Pending(int code,WebView view,ValueCallback<Uri[]> reply,BooleanSupplier original,Runnable unblock,boolean multiple){
            this.code=code;this.view=view;this.reply=reply;this.original=original;this.unblock=unblock;this.multiple=multiple;deadline=SystemClock.uptimeMillis()+120000;
        }
    }
    private static final class Timeout implements Runnable {
        final WeakReference<MdoBrowserManualChooser> owner;final int code;
        Timeout(MdoBrowserManualChooser owner,int code){this.owner=new WeakReference<>(owner);this.code=code;}
        public void run(){MdoBrowserManualChooser value=owner.get();if(value!=null)value.expire(code);}
    }
    private final Activity activity;private final Handler handler=new Handler(Looper.getMainLooper());
    private final ArrayList<WeakReference<ValueCallback<Uri[]>>> seen=new ArrayList<>();private ValueCallback<Uri[]> last;
    private Pending pending;private boolean closed,failed,resumed,identityUncertain;
    MdoBrowserManualChooser(Activity activity){this.activity=activity;}
    private void ui(){if(Looper.myLooper()!=Looper.getMainLooper())throw new IllegalStateException("Manual chooser requires UI");}
    void restore(Bundle saved){ui();if(saved!=null){int value=saved.getInt(SAVED,next);next=value<FIRST||value>LAST+1?LAST+1:Math.max(next,value);}}
    void save(Bundle saved){ui();saved.putInt(SAVED,next);}
    void resume(){ui();resumed=true;}
    void pause(){ui();resumed=false;} // Returning from our system picker is allowed before Activity.onResume.
    boolean busy(WebView view){ui();return pending!=null&&pending.view==view&&pending.reply!=null;}
    boolean ready(){ui();return !closed&&!failed&&pending==null;}
    boolean clean(){ui();return !failed;}
    boolean awaitingResult(){ui();return pending!=null&&pending.reply==null;}
    private boolean known(ValueCallback<Uri[]> reply){
        if(last==reply)return true;
        for(int at=seen.size()-1;at>=0;at--){ValueCallback<Uri[]> value=seen.get(at).get();if(value==null)seen.remove(at);else if(value==reply)return true;}
        return false;
    }
    private boolean claim(ValueCallback<Uri[]> reply){
        if(identityUncertain)return false;
        if(seen.size()>=128){failed=true;identityUncertain=true;return false;}
        last=reply;
        try{seen.add(new WeakReference<>(reply));}catch(OutOfMemoryError failure){failed=true;identityUncertain=true;}
        return true;
    }
    private void complete(Pending value,Uri[] uris){
        ValueCallback<Uri[]> reply=value.reply;Runnable unblock=value.unblock;BooleanSupplier original=value.original;
        value.reply=null;value.view=null;value.original=null;value.unblock=null;
        if(value.timer!=null){try{handler.removeCallbacks(value.timer);}catch(RuntimeException|OutOfMemoryError ignored){}value.timer=null;}
        if(unblock!=null)try{unblock.run();}catch(RuntimeException|OutOfMemoryError error){failed=true;uris=null;}
        if(uris!=null)try{if(closed||failed||SystemClock.uptimeMillis()>=value.deadline||original==null||!original.getAsBoolean())uris=null;}
        catch(RuntimeException|OutOfMemoryError lostOwner){uris=null;}
        if(reply!=null)try{reply.onReceiveValue(uris);}catch(RuntimeException|OutOfMemoryError error){failed=true;}
    }
    private boolean valid(Pending value){return !closed&&!failed&&value.reply!=null&&SystemClock.uptimeMillis()<value.deadline&&value.original!=null&&value.original.getAsBoolean();}
    private void expire(int code){ui();Pending value=pending;if(value==null||value.code!=code||value.reply==null)return;
        try{if(SystemClock.uptimeMillis()<value.deadline){if(handler.postAtTime(value.timer,value.deadline))return;failed=true;}}
        catch(RuntimeException|OutOfMemoryError failure){failed=true;}complete(value,null);
    }
    void cancel(WebView view){ui();if(pending!=null&&pending.view==view)complete(pending,null);}
    void invalidate(){ui();if(pending!=null)complete(pending,null);}
    boolean open(WebView view,ValueCallback<Uri[]> reply,WebChromeClient.FileChooserParams params,BooleanSupplier original,Runnable block,Runnable unblock){
        ui();if(reply==null||view==null)return false;
        if(known(reply))return true;if(params==null||!claim(reply))return false;
        if(closed||failed||!resumed||pending!=null||next>LAST){reject(reply);return true;}
        Pending value=null;boolean entered=false;
        try{
            if(!original.getAsBoolean()){reject(reply);return true;}
            int mode=params.getMode();if(mode!=0&&mode!=1){reject(reply);return true;}
            String[] accepts=params.getAcceptTypes();if(accepts!=null&&accepts.length>16){reject(reply);return true;}
            LinkedHashSet<String> types=new LinkedHashSet<>();
            if(accepts!=null)for(String raw:accepts){if(raw!=null&&raw.length()>256)throw new IllegalArgumentException("Chooser metadata too large");if(raw==null||raw.trim().isEmpty())continue;String type=raw.trim().toLowerCase(java.util.Locale.ROOT);
                if(type.length()>128||!type.matches("(?:[a-z0-9!#$&^_.+-]+|\\*)/(?:[a-z0-9!#$&^_.+-]+|\\*)")){types.clear();types.add("*/*");break;}types.add(type);}
            Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                .setType(types.size()==1?types.iterator().next():"*/*").putExtra(Intent.EXTRA_ALLOW_MULTIPLE,mode==1);
            if(types.size()>1)intent.putExtra(Intent.EXTRA_MIME_TYPES,types.toArray(new String[0]));
            value=new Pending(next++,view,reply,original,unblock,mode==1);value.timer=new Timeout(this,value.code);pending=value;
            block.run();if(!valid(value))throw new IllegalStateException("Manual chooser owner changed");
            if(!handler.postAtTime(value.timer,value.deadline))throw new IllegalStateException("Manual chooser scheduler stopped");
            entered=true;activity.startActivityForResult(intent,value.code);return true;
        }catch(ActivityNotFoundException absent){
            if(value==null)reject(reply);else if(value.reply!=null){if(pending==value){pending=null;complete(value,null);}else{value.reply=null;reject(reply);}}return true;
        }
        catch(RuntimeException|OutOfMemoryError failure){
            // A launch may have occurred before a platform failure. Never reuse
            // its result slot or route that late result into a new callback.
            if(value==null)reject(reply);else if(value.reply!=null){if(pending==value){if(!entered)pending=null;complete(value,null);}else{value.reply=null;reject(reply);}}return true;
        }
    }
    // Null means the pinned host may handle a nonstandard mode. A false
    // admission is terminal, so it must never fall through to another picker.
    Boolean openHost(WebView view,ValueCallback<Uri[]> reply,WebChromeClient.FileChooserParams params,BooleanSupplier original){
        ui();if(view==null||reply==null)return Boolean.FALSE;if(known(reply))return Boolean.TRUE;if(params==null)return Boolean.FALSE;
        try{int mode=params.getMode();if(mode!=0&&mode!=1)return null;}catch(RuntimeException|OutOfMemoryError invalidMetadata){}
        return Boolean.valueOf(open(view,reply,params,original,()->{},()->{}));
    }
    private void reject(ValueCallback<Uri[]> reply){try{reply.onReceiveValue(null);}catch(RuntimeException|OutOfMemoryError failure){failed=true;}}
    boolean result(int request,int result,Intent data){
        ui();if(request<FIRST||request>LAST)return false;Pending value=pending;
        if(value==null||value.code!=request||value.handling)return true;value.handling=true;
        Uri[] selected=null;try{if(valid(value)){selected=MdoBrowserPickerUris.read(activity,result,data,value.multiple);if(!valid(value))selected=null;}}
        catch(RuntimeException|OutOfMemoryError invalid){}
        finally{if(pending==value)pending=null;}
        complete(value,selected);return true;
    }
    @Override public void close(){ui();if(closed)return;closed=true;resumed=false;invalidate();}
}
