package org.xserver.android;

import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.webkit.WebView;
import java.util.function.BooleanSupplier;
import java.util.function.LongConsumer;
import java.util.function.LongSupplier;

/** Chrome callbacks retain only framework-provided transport and exact view.
 * onCreateWindow exposes no trusted requested URL or originating frame. */
class MdoBrowserWindowTarget extends MdoBrowserDialogTarget {
    interface Windows {
        boolean create(WebView source,boolean userGesture,MdoBrowserPopups.Transport<WebView> transport);
        void close(WebView source);
    }
    private final Windows windows;private final BooleanSupplier owned;
    MdoBrowserWindowTarget(WebView view,BooleanSupplier owned,LongSupplier navigation,MdoBrowserDialogs.Listener listener,
            LongConsumer beforeUnloadCancelled,Windows windows){
        super(view,owned,navigation,listener,beforeUnloadCancelled);this.owned=owned;this.windows=windows;
    }
    static final class Transport implements MdoBrowserPopups.Transport<WebView> {
        private final Message message;private final Handler target;private final WebView.WebViewTransport transport;
        private boolean sent;
        Transport(Message message){
            if(message==null||!(message.obj instanceof WebView.WebViewTransport)||message.getTarget()==null||
                    message.getTarget().getLooper()!=Looper.myLooper()||Looper.myLooper()!=Looper.getMainLooper())
                throw new IllegalArgumentException("popup_transport_invalid");
            this.message=message;target=message.getTarget();transport=(WebView.WebViewTransport)message.obj;
        }
        public Object token(){return message;}
        public void send(WebView child,Runnable consumed){
            if(sent)throw new IllegalStateException("popup_transport_already_sent");sent=true;
            transport.setWebView(child);
            if(!target.sendMessage(message))throw new IllegalStateException("popup_transport_queue_stopped");
            // Same UI queue, after WebView's completeWindowCreation. This proves
            // dispatch consumption only; page load/world readiness stay separate.
            if(!target.post(consumed))throw new IllegalStateException("popup_transport_barrier_stopped");
        }
    }
    @Override public boolean onCreateWindow(WebView source,boolean dialog,boolean userGesture,Message result){
        if(source!=view||windows==null)return false;
        try{if(!owned.getAsBoolean())return false;return windows.create(source,userGesture,new Transport(result));}
        catch(RuntimeException|OutOfMemoryError error){return false;}
    }
    @Override public void onCloseWindow(WebView source){if(source==view&&windows!=null)windows.close(source);}
}
