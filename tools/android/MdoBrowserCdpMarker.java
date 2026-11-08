package org.xserver.android;

import java.io.IOException;
import java.lang.ref.WeakReference;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;

/** A fixed native housekeeping expression, not Agent/page-provided code. One
 * instance belongs to one exact UI view/document generation. Its owner must
 * close it before navigation, teardown, takeover or loss of host admission.
 * A separately constructed physical-network marker survives document changes
 * only under its native view owner's lifetime; it cannot grant frame/world use.
 * Only a thread-safe lease crosses to the protocol worker; callbacks retain
 * neither the native view nor its UI owner. */
final class MdoBrowserCdpMarker implements AutoCloseable {
    interface Ui {boolean onThread();boolean post(Runnable task);void remove(Runnable task);}
    interface View {boolean current();void evaluate(String source,Reply reply);default boolean owns(Object exact){return false;}}
    interface Reply {void done(String json);}
    private static final class RandomHolder {static final SecureRandom VALUE=new SecureRandom();}
    /** UI-owned document quota, shared across all short-lived admission leases.
     * Hiding/reconnecting must not reset lexical declarations in the same DOM. */
    static final class DocumentQuota implements AutoCloseable {
        private final Thread owner=Thread.currentThread();private int declarations;private final AtomicBoolean retired=new AtomicBoolean();
        private final AtomicInteger worldNames=new AtomicInteger();
        private void thread(){if(Thread.currentThread()!=owner)throw new IllegalStateException("Document quota requires its UI owner");}
        boolean acquire(){thread();if(retired.get()||declarations>=16)return false;declarations++;return true;}
        boolean world(){for(;;){if(retired.get())return false;int count=worldNames.get();
            if(count>=256)return false;if(worldNames.compareAndSet(count,count+1))return !retired.get();}}
        @Override public void close(){thread();retired.set(true);}
    }
    static final class Budget {
        private final int limit;private final ArrayList<WeakReference<Cell>> cells=new ArrayList<>();
        Budget(int limit){if(limit<1||limit>64)throw new IllegalArgumentException("Invalid marker budget");this.limit=limit;}
        private void prune(){for(Iterator<WeakReference<Cell>> rows=cells.iterator();rows.hasNext();)if(rows.next().get()==null)rows.remove();}
        synchronized boolean acquire(Cell cell){prune();if(cells.size()>=limit)return false;cells.add(new WeakReference<>(cell));return true;}
        synchronized void release(Cell cell){for(Iterator<WeakReference<Cell>> rows=cells.iterator();rows.hasNext();)if(rows.next().get()==cell){rows.remove();break;}}
        synchronized int used(){prune();return cells.size();}
    }
    static final class Lease {
        private final WeakReference<MdoBrowserCdpMarker> owner;
        private final AtomicBoolean live=new AtomicBoolean(true);
        private final AtomicReference<AutoCloseable> channel=new AtomicReference<>();
        private final AtomicReference<Object> attachment=new AtomicReference<>();
        boolean attached(){return attachment.get()!=null;}
        boolean belongsToAttachment(Object exact){return exact!=null&&attachment.get()==exact;}
        boolean attach(Object exact){return exact!=null&&current()&&!busy()&&attachment.compareAndSet(null,exact);}
        Lease(MdoBrowserCdpMarker marker){owner=new WeakReference<>(marker);}
        boolean physicalNetwork(){MdoBrowserCdpMarker marker=owner.get();return marker!=null&&marker.network;}
        boolean current(){return live.get()&&owner.get()!=null;}
        boolean busy(){return channel.get()!=null;}
        boolean worldName()throws IOException {
            MdoBrowserCdpMarker marker=owner.get();if(!current()||marker==null)throw new IOException("cdp_binding_owner_changed");
            if(marker.network)throw new IOException("cdp_network_scope_invalid");
            boolean admitted=marker.document.world();if(!current())throw new IOException("cdp_binding_owner_changed");return admitted;
        }
        void track(AutoCloseable next) throws IOException {
            if(!current()){close(next);throw new IOException("cdp_binding_owner_changed");}
            if(!channel.compareAndSet(null,next))throw new IOException("cdp_binding_busy");
            if(!current()){untrack(next);close(next);throw new IOException("cdp_binding_owner_changed");}
        }
        void untrack(AutoCloseable original){channel.compareAndSet(original,null);}
        void transfer(AutoCloseable original,AutoCloseable bound) throws IOException {
            if(!current()||!channel.compareAndSet(original,bound)){close(bound);throw new IOException("cdp_binding_owner_changed");}
            if(!current()){untrack(bound);close(bound);throw new IOException("cdp_binding_owner_changed");}
        }
        private static void close(AutoCloseable value){if(value!=null)try{value.close();}catch(Exception ignored){}}
        void invalidate(){live.set(false);close(channel.getAndSet(null));}
    }
    static final class Ticket implements AutoCloseable {
        // 0 queued, 1 native callback outstanding, 2 confirmed, 3 failed/cancelled.
        final AtomicInteger state=new AtomicInteger();final String name,value,source;
        final AtomicBoolean dispatched=new AtomicBoolean();
        private final WeakReference<MdoBrowserCdpMarker> owner;
        private final Cell cell;private final Runnable task;
        private volatile String failure;private volatile BooleanSupplier operation;
        Ticket(MdoBrowserCdpMarker marker,BooleanSupplier operation) {
            owner=new WeakReference<>(marker);name="__mdo_cdp_"+nonce();value=nonce();
            this.operation=operation;
            // The fixed source label reveals no identifier/token to resource
            // profiling. No function/global-property/DOM hook is called.
            source="let "+name+"=\""+value+"\";"+name+"\n//# sourceURL=mdo-native-binding.js";
            cell=new Cell(this,marker.budget);task=new Start(this);
        }
        boolean pending(){return state.get()<2;}
        boolean confirmed(){return state.get()==2;}
        String failure(){return failure==null?"cdp_binding_cancelled":failure;}
        private boolean current(){BooleanSupplier scope=operation;try{return scope==null||scope.getAsBoolean();}catch(RuntimeException|OutOfMemoryError error){return false;}}
        private void fail(String code){failure=code;state.set(3);synchronized(this){notifyAll();}}
        boolean await(int timeoutMs) throws IOException {
            MdoBrowserCdpMarker marker=owner.get();
            if(marker!=null&&marker.ui.onThread())throw new IOException("cdp_requires_background_thread");
            long start=android.os.SystemClock.elapsedRealtime();
            synchronized(this){for(;;) {
                long used=android.os.SystemClock.elapsedRealtime()-start;
                if(used<0||used>=timeoutMs)return false;
                if(!pending())return confirmed();
                if(!current()){fail("cdp_binding_owner_changed");return false;}
                // Object.wait counts uptime. Recheck elapsed time promptly on
                // resume, including a callback delivered after the deadline.
                try{wait(Math.min(20,timeoutMs-used));}catch(InterruptedException interrupted){Thread.currentThread().interrupt();close();throw new IOException("cdp_binding_cancelled");}
            }}
        }
        @Override public void close() {
            int before=state.getAndSet(3);operation=null;MdoBrowserCdpMarker marker=owner.get();
            synchronized(this){notifyAll();}
            if(marker==null)return;
            marker.active.compareAndSet(this,null);
            if(before==0) {
                try {marker.ui.remove(task);marker.budget.release(cell);}catch(RuntimeException|Error ignored){}
            }
        }
    }
    private static final class Cell implements Reply {
        final Ticket ticket;final Budget budget;final AtomicBoolean returned=new AtomicBoolean();
        Cell(Ticket ticket,Budget budget){this.ticket=ticket;this.budget=budget;}
        @Override public void done(String json) {
            if(!returned.compareAndSet(false,true))return;
            try {
                MdoBrowserCdpMarker marker=ticket.owner.get();
                View view=marker==null?null:marker.view;
                if(view==null||!marker.ui.onThread()||!marker.lease.current()||marker.active.get()!=ticket||!ticket.current()||!view.current()) {
                    ticket.fail("cdp_binding_owner_changed");return;
                }
                if(!("\""+ticket.value+"\"").equals(json)){ticket.fail("cdp_binding_marker_failed");return;}
                ticket.state.compareAndSet(1,2);
                synchronized(ticket){ticket.notifyAll();}
            } catch(RuntimeException|Error failure){ticket.fail("cdp_binding_marker_failed");}
            finally {budget.release(this);}
        }
    }
    private static final class Start implements Runnable {
        private final Ticket ticket;Start(Ticket ticket){this.ticket=ticket;}
        @Override public void run() {
            if(!ticket.state.compareAndSet(0,1))return;
            MdoBrowserCdpMarker marker=ticket.owner.get();
            try {
                View view=marker==null?null:marker.view;
                if(view==null||!marker.ui.onThread()||!marker.lease.current()||marker.active.get()!=ticket||!ticket.current()||!view.current()) {
                    ticket.fail("cdp_binding_owner_changed");ticket.cell.budget.release(ticket.cell);return;
                }
                if(ticket.state.get()!=1){ticket.cell.budget.release(ticket.cell);return;}
                if(!marker.document.acquire()){ticket.fail("cdp_binding_document_limit");ticket.cell.budget.release(ticket.cell);return;}
                ticket.dispatched.set(true);
                view.evaluate(ticket.source,ticket.cell);
            } catch(RuntimeException|Error failure) {
                // The provider may already retain the callback before throwing.
                // Keep its weak budget slot until completion or actual GC.
                ticket.fail("cdp_binding_marker_failed");
                if(!ticket.dispatched.get())ticket.cell.budget.release(ticket.cell);
            }
        }
    }
    private final Ui ui;private volatile View view;private final Budget budget;private final DocumentQuota document;
    private final boolean network;private final AtomicBoolean networkRequested=new AtomicBoolean();
    private final Lease lease;private final AtomicReference<Ticket> active=new AtomicReference<>();
    MdoBrowserCdpMarker(Ui ui,View view,Budget budget) {
        this(ui,view,budget,new DocumentQuota());
    }
    MdoBrowserCdpMarker(Ui ui,View view,Budget budget,DocumentQuota document) {
        this(ui,view,budget,document,false);
    }
    private MdoBrowserCdpMarker(Ui ui,View view,Budget budget,DocumentQuota document,boolean network) {
        if(ui==null||view==null||budget==null||document==null||!ui.onThread())throw new IllegalArgumentException("Marker requires its owning UI thread");
        this.ui=ui;this.view=view;this.budget=budget;this.document=document;this.network=network;lease=new Lease(this);
    }
    /** Native physical owner only: close on view replacement/destruction,
     * renderer loss, takeover or host admission loss, not ordinary navigation.
     * One proof attempt; never upgrade/reuse a document marker for networking. */
    static MdoBrowserCdpMarker network(Ui ui,View view,Budget budget,DocumentQuota document){return new MdoBrowserCdpMarker(ui,view,budget,document,true);}
    Lease lease(){return lease;}
    boolean ownsNative(Object exact){return ui.onThread()&&exact!=null&&lease.current()&&view!=null&&view.current()&&view.owns(exact);}
    Ticket request() throws IOException {return request(null);}
    Ticket request(BooleanSupplier operation) throws IOException {
        if(!lease.current())throw new IOException("cdp_binding_owner_changed");
        if(network&&!networkRequested.compareAndSet(false,true))throw new IOException("cdp_network_rebind_forbidden");
        Ticket ticket=new Ticket(this,operation);
        if(!active.compareAndSet(null,ticket))throw new IOException("cdp_binding_busy");
        try {
            if(!lease.current())throw new IOException("cdp_binding_owner_changed");
            if(!budget.acquire(ticket.cell))throw new IOException("cdp_binding_callback_limit");
            if(!ui.post(ticket.task))throw new IOException("cdp_binding_scheduler_failed");
            return ticket;
        } catch(IOException|RuntimeException|Error failure){ticket.close();throw failure;}
    }
    private static String nonce() {
        byte[] bytes=new byte[16];char[] text=new char[32];
        try {RandomHolder.VALUE.nextBytes(bytes);String hex="0123456789abcdef";
            for(int i=0;i<bytes.length;i++){int n=bytes[i]&255;text[2*i]=hex.charAt(n>>>4);text[2*i+1]=hex.charAt(n&15);}return new String(text);
        } finally {java.util.Arrays.fill(bytes,(byte)0);java.util.Arrays.fill(text,'\0');}
    }
    /** Never evaluate cleanup in a possibly replaced default UI document.
     * Only the protocol binder can clear a proven unique context. Uncertain
     * values/empty lexical slots remain bounded until document replacement. */
    void discard(Ticket ticket) {
        if(ticket!=null)ticket.close();
    }
    @Override public void close(){view=null;lease.invalidate();Ticket ticket=active.getAndSet(null);if(ticket!=null)ticket.close();}
}
