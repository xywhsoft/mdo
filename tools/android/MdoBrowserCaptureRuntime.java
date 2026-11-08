package org.xserver.android;

import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/** One bounded encoder for the process; no Activity/View references in tasks. */
final class MdoBrowserCaptureRuntime {
    static final MdoBrowserCapture.Budget BUDGET=new MdoBrowserCapture.Budget(2);
    private static final ThreadPoolExecutor executor=new ThreadPoolExecutor(1,1,15,TimeUnit.SECONDS,
        new ArrayBlockingQueue<Runnable>(1),task->{Thread thread=new Thread(task,"mdo-browser-capture");thread.setDaemon(true);return thread;},
        new ThreadPoolExecutor.AbortPolicy());
    static{executor.allowCoreThreadTimeOut(true);}
    static final MdoBrowserCapture.Worker WORKER=new MdoBrowserCapture.Worker(){
        public void execute(Runnable task){executor.execute(task);}
        public void remove(Runnable task){executor.remove(task);}
    };
    static MdoBrowserCapture.Scheduler scheduler(){
        final Handler handler=new Handler(Looper.getMainLooper());
        return new MdoBrowserCapture.Scheduler(){
            public long now(){return SystemClock.elapsedRealtime();}
            public void post(Runnable task,long delay){if(!handler.postDelayed(task,delay))throw new IllegalStateException("Capture UI scheduler stopped");}
            public void at(Runnable task,long deadline){
                // Handler deadlines exclude deep sleep; the capture budget does
                // not. Wake in short uptime slices against the same deadline.
                long delay=Math.max(0,Math.min(50,deadline-SystemClock.elapsedRealtime())),uptime=SystemClock.uptimeMillis();
                if(uptime<0||uptime>Long.MAX_VALUE-delay||!handler.postAtTime(task,uptime+delay))throw new IllegalStateException("Capture UI scheduler stopped");
            }
            public void remove(Runnable task){handler.removeCallbacks(task);}
        };
    }
}
