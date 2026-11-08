package org.xserver.android;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.function.BooleanSupplier;

/** UI-thread registry for exact view/document identities. Manual browsing does
 * not start CDP. Short admission losses retire leases, retaining the document's
 * lexical quota until an observed navigation or native view destruction. */
final class MdoBrowserCdpRegistry<T> implements AutoCloseable {
    interface Factory<T> {MdoBrowserCdpMarker create(T view,MdoBrowserCdpMarker.DocumentQuota document,BooleanSupplier current);}
    interface Retired {void changed(MdoBrowserCdpMarker.Lease lease);}
    private final class Entry {
        final T view;long epoch;Object channel;MdoBrowserCdpMarker marker;
        MdoBrowserCdpMarker.DocumentQuota document=new MdoBrowserCdpMarker.DocumentQuota();
        Entry(T view,long epoch){this.view=view;this.epoch=epoch;}
        void invalidate(){MdoBrowserCdpMarker old=marker;marker=null;channel=null;if(old!=null){old.close();retired.changed(old.lease());}}
        void retire(){try{invalidate();}finally{document.close();}}
    }
    private final Map<String,Entry> entries=new LinkedHashMap<>();private final Factory<T> factory;private final Retired retired;
    private final Thread owner=Thread.currentThread();private boolean closed;
    MdoBrowserCdpRegistry(Factory<T> factory){this(factory,lease->{});}
    MdoBrowserCdpRegistry(Factory<T> factory,Retired retired){
        if(factory==null||retired==null)throw new IllegalArgumentException("Missing CDP view factory/retirement hook");this.factory=factory;this.retired=retired;
    }
    private void thread(){if(Thread.currentThread()!=owner)throw new IllegalStateException("CDP registry requires its UI owner");}
    MdoBrowserCdpMarker marker(String key,T view,long epoch,Object channel,BooleanSupplier admitted){
        thread();if(closed||key==null||key.isEmpty()||key.length()>100||view==null||epoch<=0||channel==null||admitted==null||!admitted.getAsBoolean())
            throw new IllegalStateException("CDP view admission unavailable");
        Entry entry=entries.get(key);
        if(entry==null){
            if(entries.size()>=8)throw new IllegalStateException("CDP view limit reached");
            for(Entry existing:entries.values())if(existing.view==view)throw new IllegalStateException("CDP view already owned");
            entry=new Entry(view,epoch);entries.put(key,entry);
        }else if(entry.view!=view||entry.epoch!=epoch)throw new IllegalStateException("CDP document owner changed");
        if(entry.marker!=null&&entry.channel==channel&&entry.marker.lease().current())return entry.marker;
        entry.invalidate();final Entry original=entry;final MdoBrowserCdpMarker.DocumentQuota document=entry.document;
        entry.channel=channel;
        try{
            entry.marker=factory.create(view,document,()->!closed&&entries.get(key)==original&&original.epoch==epoch&&
                original.document==document&&original.channel==channel&&admitted.getAsBoolean());
            if(entry.marker==null)throw new IllegalStateException("Missing CDP view adapter");return entry.marker;
        }catch(RuntimeException|Error failure){entry.invalidate();throw failure;}
    }
    /** Native continuation only: never create/rebind or relax ordinary marker
     * admission. The UI must prove its exact chooser/operation scope first. */
    MdoBrowserCdpMarker existing(String key,T view,long epoch,Object channel){
        thread();Entry entry=entries.get(key);
        if(closed||entry==null||view==null||channel==null||epoch<=0||entry.view!=view||entry.epoch!=epoch||entry.channel!=channel||
            entry.marker==null||!entry.marker.lease().current()||!entry.marker.lease().busy())throw new IllegalStateException("Original CDP binding unavailable");
        return entry.marker;
    }
    void invalidate(String key,T view,long epoch,String reason){
        thread();Entry entry=entries.get(key);if(entry==null||entry.view!=view)return;
        entry.invalidate();
        if("input_closed".equals(reason)){entries.remove(key);entry.document.close();}
        else if("frame_changed".equals(reason)&&epoch>entry.epoch){
            entry.document.close();entry.document=new MdoBrowserCdpMarker.DocumentQuota();entry.epoch=epoch;
        }
    }
    void invalidateAll(){thread();for(Entry entry:new ArrayList<>(entries.values()))entry.invalidate();}
    int size(){thread();return entries.size();}
    @Override public void close(){thread();if(closed)return;closed=true;ArrayList<Entry> owned=new ArrayList<>(entries.values());entries.clear();
        for(Entry entry:owned)entry.retire();}
}
