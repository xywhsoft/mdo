package org.xserver.android;

import android.os.Handler;
import android.os.Looper;
import android.webkit.WebView;
import java.lang.ref.WeakReference;
import java.io.IOException;

/** Public-framework adapter. The UI controller keeps one instance for an exact
 * managed view/document generation, with an ownership predicate capturing that
 * generation and original host channel. Close before its admission changes.
 * Neither protocol workers nor retained provider callbacks read a UI object. */
final class MdoBrowserCdpView implements AutoCloseable {
    interface Owner {boolean current(WebView view);}
    private static final MdoBrowserCdpMarker.Budget CALLBACKS=new MdoBrowserCdpMarker.Budget(16);
    private final MdoBrowserCdpMarker marker;
    private final WeakReference<WebView> original;private final boolean physical;private final Owner actionOwner;
    MdoBrowserCdpView(WebView view,Owner owner) {
        this(view,owner,new MdoBrowserCdpMarker.DocumentQuota());
    }
    MdoBrowserCdpView(WebView view,Owner owner,MdoBrowserCdpMarker.DocumentQuota document) {
        this(view,owner,document,false);
    }
    private MdoBrowserCdpView(WebView view,Owner owner,MdoBrowserCdpMarker.DocumentQuota document,boolean network) {
        this(view,owner,owner,document,network);
    }
    private MdoBrowserCdpView(WebView view,Owner owner,Owner popupOwner,MdoBrowserCdpMarker.DocumentQuota document,boolean network) {
        if(view==null||owner==null||popupOwner==null||Looper.myLooper()!=Looper.getMainLooper())throw new IllegalArgumentException("View marker requires its UI owner");
        WeakReference<WebView> weak=new WeakReference<>(view);original=weak;physical=network;actionOwner=popupOwner;Handler handler=new Handler(Looper.getMainLooper());
        MdoBrowserCdpMarker.Ui ui=new MdoBrowserCdpMarker.Ui(){
            public boolean onThread(){return Looper.myLooper()==Looper.getMainLooper();}
            public boolean post(Runnable task){return handler.post(task);}
            public void remove(Runnable task){handler.removeCallbacks(task);}
        };MdoBrowserCdpMarker.View target=new MdoBrowserCdpMarker.View(){
            public boolean owns(Object exact){return exact!=null&&weak.get()==exact&&current();}
            public boolean current(){WebView current=weak.get();return current!=null&&owner.current(current)&&current.getSettings().getJavaScriptEnabled();}
            public void evaluate(String source,MdoBrowserCdpMarker.Reply reply){
                WebView current=weak.get();if(current==null||!owner.current(current))throw new IllegalStateException("Native marker owner changed");
                current.evaluateJavascript(source,reply==null?null:reply::done);
            }
        };
        marker=network?MdoBrowserCdpMarker.network(ui,target,CALLBACKS,document):new MdoBrowserCdpMarker(ui,target,CALLBACKS,document);
    }
    static MdoBrowserCdpView network(WebView view,Owner owner,MdoBrowserCdpMarker.DocumentQuota document){return new MdoBrowserCdpView(view,owner,document,true);}
    static MdoBrowserCdpView network(WebView view,Owner transportOwner,Owner popupOwner,MdoBrowserCdpMarker.DocumentQuota document){return new MdoBrowserCdpView(view,transportOwner,popupOwner,document,true);}
    MdoBrowserCdpMarker marker(){return marker;}
    MdoBrowserCdpMarker.Lease physicalLease(WebView exact)throws IOException {
        if(Looper.myLooper()!=Looper.getMainLooper())throw new IOException("cdp_requires_ui_thread");
        if(exact==null||original.get()!=exact||!physical||!marker.lease().current())throw new IOException("cdp_popup_native_source_invalid");
        return marker.lease();
    }
    /** Action ownership is deliberately stronger than a physical lease that
     * remains alive during native/protocol cleanup after human takeover. */
    boolean popupCurrent(WebView exact){
        if(Looper.myLooper()!=Looper.getMainLooper())throw new IllegalStateException("Popup ownership requires its UI thread");
        return exact!=null&&original.get()==exact&&physical&&marker.lease().current()&&actionOwner.current(exact)&&exact.getSettings().getJavaScriptEnabled();
    }
    @Override public void close(){marker.close();}
}
