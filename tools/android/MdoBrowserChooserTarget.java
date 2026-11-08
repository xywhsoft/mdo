package org.xserver.android;

import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebView;
import java.util.function.BooleanSupplier;
import java.util.function.LongConsumer;
import java.util.function.LongSupplier;

/** Retains/cancels a framework chooser. It does not grant unvalidated URIs,
 * identify a subframe from a callback URL, or enable global automation. */
final class MdoBrowserChooserTarget extends MdoBrowserWindowTarget {
    interface Manual {boolean open(ValueCallback<Uri[]> callback,WebChromeClient.FileChooserParams params);void cancel();}
    final MdoBrowserChoosers choosers;
    final MdoBrowserPrivatePicker privatePicker;
    private final Manual manual;
    MdoBrowserChooserTarget(WebView view,BooleanSupplier owned,LongSupplier navigation,MdoBrowserDialogs.Listener dialogs,
            LongConsumer beforeUnloadCancelled,Windows windows,MdoBrowserChoosers.Listener listener){
        this(view,owned,navigation,dialogs,beforeUnloadCancelled,windows,listener,null);
    }
    MdoBrowserChooserTarget(WebView view,BooleanSupplier owned,LongSupplier navigation,MdoBrowserDialogs.Listener dialogs,
            LongConsumer beforeUnloadCancelled,Windows windows,MdoBrowserChoosers.Listener listener,Manual manual){
        super(view,owned,navigation,dialogs,beforeUnloadCancelled,windows);
        this.manual=manual;
        final Handler handler=new Handler(Looper.getMainLooper());
        choosers=new MdoBrowserChoosers(view,new MdoBrowserChoosers.Scheduler(){
            public long now(){return SystemClock.uptimeMillis();}
            public void at(Runnable task,long uptime){if(!handler.postAtTime(task,uptime))throw new IllegalStateException("Chooser scheduler stopped");}
            public void remove(Runnable task){handler.removeCallbacks(task);}
        },owned,navigation,listener);
        privatePicker=new MdoBrowserPrivatePicker(view,choosers,new MdoBrowserPrivatePicker.Scheduler(){
            public long now(){return SystemClock.elapsedRealtime();}
            public void at(Runnable task,long deadline){
                long delay=Math.max(0,deadline-SystemClock.elapsedRealtime()),uptime=SystemClock.uptimeMillis();
                if(uptime<0||uptime>Long.MAX_VALUE-delay||!handler.postAtTime(task,uptime+delay))throw new IllegalStateException("Private picker scheduler stopped");
            }
            public void remove(Runnable task){handler.removeCallbacks(task);}
        },owned,navigation);
    }
    private boolean rejectPrivate(ValueCallback<Uri[]> callback){
        try{return privatePicker.rejectMetadata(view,callback,new MdoBrowserPrivatePickerReply(view.getContext(),callback));}
        catch(RuntimeException|OutOfMemoryError failure){
            // Allocation/context failure can precede a retained reply adapter.
            // Do not advertise a clean rendezvous that has not observed an ACK.
            choosers.rejectionFailed();
            if(choosers.known(callback)){privatePicker.metadataFailed();return true;}
            if(!choosers.claimPrivate(callback)){privatePicker.metadataFailed();return false;}
            try{callback.onReceiveValue(null);}catch(RuntimeException|OutOfMemoryError ignored){choosers.rejectionFailed();}
            privatePicker.metadataFailed();return true;
        }
    }
    @Override public boolean onShowFileChooser(WebView source,ValueCallback<Uri[]> callback,WebChromeClient.FileChooserParams params){
        if(source!=view||callback==null||Looper.myLooper()!=Looper.getMainLooper())return false;
        if(choosers.known(callback))return true;
        if(params==null)return privatePicker.pending()?rejectPrivate(callback):false;
        MdoBrowserPrivatePickerReply privateReply=null;
        try{
            String[] types=params.getAcceptTypes();
            if(privatePicker.handles(types)){
                privateReply=new MdoBrowserPrivatePickerReply(view.getContext(),callback);
                return privatePicker.open(source,callback,privateReply,params.getMode(),types,params.isCaptureEnabled());
            }
        }catch(RuntimeException|OutOfMemoryError failure){
            if(choosers.known(callback)){privatePicker.metadataFailed();return true;}
            if(privatePicker.pending())return privateReply!=null?privatePicker.rejectMetadata(view,callback,privateReply):rejectPrivate(callback);
        }
        if(!choosers.intercepting()){
            if(manual==null||!choosers.claimManual(callback))return false;
            try{return manual.open(callback,params);}
            catch(RuntimeException|OutOfMemoryError failure){try{callback.onReceiveValue(null);}catch(RuntimeException|OutOfMemoryError uncertain){choosers.rejectionFailed();}return true;}
        }
        try{return choosers.open(callback,()->callback.onReceiveValue(null),params.getMode(),params.getAcceptTypes(),params.isCaptureEnabled());}
        catch(RuntimeException|OutOfMemoryError error){
            if(choosers.known(callback))return true;
            if(!choosers.rejectOnce(callback))return false;
            try{callback.onReceiveValue(null);}catch(RuntimeException|OutOfMemoryError ignored){choosers.rejectionFailed();}
            return true;
        }
    }
    @Override public void close(){privatePicker.close();if(manual!=null)manual.cancel();choosers.close();super.close();}
    @Override void viewDestroyed(){privatePicker.viewDestroyed(view);choosers.viewDestroyed(view);super.viewDestroyed();}
}
