package org.xserver.android;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.webkit.WebView;
import java.util.function.BooleanSupplier;
import java.util.function.DoubleSupplier;
import java.util.function.LongSupplier;
import java.util.function.Supplier;
import java.util.concurrent.atomic.AtomicBoolean;

/** Draw only the owned visible content view, never its parent/window surface. */
final class MdoBrowserCaptureTarget implements MdoBrowserCapture.Target {
    private final WebView view;private final Supplier<String> document;private final LongSupplier navigation;
    private final DoubleSupplier scale;private final BooleanSupplier admitted;private long nextVisual=1;
    private final Handler handler=new Handler(Looper.getMainLooper());
    MdoBrowserCaptureTarget(WebView view,Supplier<String> document,LongSupplier navigation,DoubleSupplier scale,BooleanSupplier admitted){
        if(view==null||document==null||navigation==null||scale==null||admitted==null)throw new IllegalArgumentException("Missing capture view/owner");
        this.view=view;this.document=document;this.navigation=navigation;this.scale=scale;this.admitted=admitted;
    }
    public MdoBrowserCapture.Snapshot snapshot(){
        if(!admitted.getAsBoolean()||!view.isAttachedToWindow()||!view.isShown()||view.getWindowVisibility()!=View.VISIBLE||
           view.getPaddingLeft()!=0||view.getPaddingTop()!=0||view.getPaddingRight()!=0||view.getPaddingBottom()!=0)return null;
        String identity=document.get();double factor=scale.getAsDouble();if(identity==null||!Double.isFinite(factor))return null;
        return new MdoBrowserCapture.Snapshot(identity,navigation.getAsLong(),view.getWidth(),view.getHeight(),view.getScrollX(),view.getScrollY(),factor);
    }
    private static final class Ready extends WebView.VisualStateCallback {
        final long original;final Runnable complete;final Handler handler;final AtomicBoolean sent=new AtomicBoolean();
        Ready(long original,Runnable complete,Handler handler){this.original=original;this.complete=complete;this.handler=handler;}
        @Override public void onComplete(long request){
            if(request!=original||!sent.compareAndSet(false,true))return;
            // The draw guarantee applies AFTER this callback returns. Queue a UI
            // turn and retain only the transaction's weak callback, not a View.
            try{handler.post(complete);}catch(RuntimeException|OutOfMemoryError ignored){}
        }
    }
    public void ready(Runnable complete){long id=nextVisual++;view.postVisualStateCallback(id,new Ready(id,complete,handler));}
    public MdoBrowserCapture.Raster draw(MdoBrowserCapture.Plan plan){
        Bitmap bitmap=Bitmap.createBitmap(plan.outputWidth,plan.outputHeight,Bitmap.Config.ARGB_8888);
        try{
            bitmap.setDensity(Bitmap.DENSITY_NONE);bitmap.eraseColor(Color.WHITE);
            Canvas canvas=new Canvas(bitmap);canvas.scale((float)(plan.outputWidth/plan.width),(float)(plan.outputHeight/plan.height));
            canvas.translate((float)-plan.x,(float)-plan.y);canvas.clipRect(0,0,view.getWidth(),view.getHeight());view.draw(canvas);
            return new Pixels(bitmap);
        }catch(RuntimeException|OutOfMemoryError error){
            try{bitmap.recycle();}catch(RuntimeException|OutOfMemoryError cleanup){throw new MdoBrowserCapture.Cleanup();}
            throw error;
        }
    }
    private static final class Pixels implements MdoBrowserCapture.Raster {
        private Bitmap bitmap;Pixels(Bitmap bitmap){this.bitmap=bitmap;}
        public byte[] encode(String format,int maximumBytes){
            if(Looper.myLooper()==Looper.getMainLooper())throw new IllegalStateException("Bitmap encoding requires the worker");
            Bitmap current=bitmap;if(current==null)throw new IllegalStateException("Capture bitmap was already released");
            MdoBrowserCaptureBuffer output=new MdoBrowserCaptureBuffer(maximumBytes);
            if(!current.compress("png".equals(format)?Bitmap.CompressFormat.PNG:Bitmap.CompressFormat.JPEG,80,output))
                throw new IllegalStateException("Native bitmap encoding failed");
            return output.toByteArray();
        }
        public void close(){Bitmap original=bitmap;bitmap=null;if(original!=null)original.recycle();}
    }
}
