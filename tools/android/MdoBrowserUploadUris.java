package org.xserver.android;

import java.io.IOException;
import java.util.Arrays;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.LongSupplier;

/** Native object permissions and original FD callback accounting. Tokens never
 * leave the native URI handoff; no path/serialized lease can mint authority. */
final class MdoBrowserUploadUris implements AutoCloseable {
    static final int GRANTS=16,DESCRIPTORS=64,REQUEST_BYTES=1048576;
    private static final int RESERVED=0,OPENING=1,RETURNED=2,UNCERTAIN=3,RELEASED=4;
    static final class FileInfo {
        final String name;final long size;
        private FileInfo(MdoBrowserUploadStore.ReadLease lease){name=lease.file.name;size=lease.file.size;}
    }
    private static final class Entry {
        final String token=UUID.randomUUID().toString();final MdoBrowserUploadStore.ReadLease lease;final FileInfo info;
        volatile boolean revoked;
        Entry(MdoBrowserUploadStore.ReadLease lease){this.lease=lease;info=new FileInfo(lease);}
    }
    static final class Session {
        private final MdoBrowserUploadUris owner;private final Entry entry;private final int slot;
        private final AtomicBoolean reading=new AtomicBoolean();private volatile boolean closed;private volatile int phase=RESERVED;
        private Session(MdoBrowserUploadUris owner,Entry entry,int slot){this.owner=owner;this.entry=entry;this.slot=slot;}
        boolean enter(){return owner.enter(this);}
        boolean returned(){return owner.returned(this);}
        void beforeOpenFailure(){owner.beforeOpenFailure(this);}
        void openFailure(){owner.openFailure(this);}
        void closeFailure(){owner.closeFailure(this);}
        void cancel(){owner.cancel(this);}
        void released(){owner.released(this);}
        long size()throws IOException {if(!live())throw fail("upload_uri_revoked");return entry.info.size;}
        private boolean live(){return !closed&&(phase==OPENING||phase==RETURNED)&&owner.live(entry);}
        int read(long offset,int requested,byte[] output,int timeoutMilliseconds)throws IOException {
            if(offset<0||requested<0||output==null||requested>output.length||requested>REQUEST_BYTES||timeoutMilliseconds<1||timeoutMilliseconds>5000)throw fail("upload_range_invalid");
            if(!reading.compareAndSet(false,true))throw fail("upload_proxy_read_busy");boolean success=false;
            try{
                long deadline=System.nanoTime()+TimeUnit.MILLISECONDS.toNanos(timeoutMilliseconds);
                if(!live())throw fail("upload_uri_revoked");
                int count=offset>=entry.info.size?0:(int)Math.min(requested,entry.info.size-offset),written=0;
                while(written<count){
                    if(!live())throw fail("upload_uri_revoked");long remaining=deadline-System.nanoTime();
                    if(remaining<=0)throw fail("upload_read_timeout");
                    int wait=(int)Math.max(1,Math.min(5000,TimeUnit.NANOSECONDS.toMillis(remaining)));
                    int size=Math.min(65536,count-written);byte[] bytes=owner.queue.read(entry.lease,offset+written,size,wait);
                    if(bytes.length!=size)throw fail("upload_proxy_read_unconfirmed");
                    if(!live())throw fail("upload_uri_revoked");System.arraycopy(bytes,0,output,written,size);written+=size;
                }
                if(!live())throw fail("upload_uri_revoked");if(deadline-System.nanoTime()<=0)throw fail("upload_read_timeout");success=true;return count;
            }finally{
                if(!success)Arrays.fill(output,0,requested,(byte)0);reading.set(false);
            }
        }
    }
    private final MdoBrowserUploadQueue queue;private final LongSupplier clock;
    private final Entry[] entries=new Entry[GRANTS];private final Session[] sessions=new Session[DESCRIPTORS];private volatile boolean closed;
    MdoBrowserUploadUris(MdoBrowserUploadQueue queue,LongSupplier clock){if(queue==null||clock==null)throw new IllegalArgumentException("Missing URI ownership");this.queue=queue;this.clock=clock;}
    private static MdoBrowserUploadFiles.Failure fail(String code){return new MdoBrowserUploadFiles.Failure(code);}
    static boolean token(String value){return value!=null&&value.length()==36&&value.matches("[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}");}
    private boolean live(Entry entry){return !closed&&queue.ready()&&!entry.revoked&&entry.lease.live(clock.getAsLong());}
    private void collect(){for(int at=0;at<entries.length;at++)if(entries[at]!=null&&!live(entries[at])){entries[at].revoked=true;entries[at]=null;}}
    private Entry find(String token)throws IOException {if(closed)throw fail("upload_uri_closed");if(!token(token))throw fail("upload_uri_invalid");collect();for(Entry entry:entries)if(entry!=null&&entry.token.equals(token))return entry;throw fail("upload_uri_not_found");}
    synchronized String publish(MdoBrowserUploadStore.ReadLease lease)throws IOException {
        if(closed)throw fail("upload_uri_closed");if(!queue.ready())throw fail("upload_worker_closed");if(!queue.owns(lease))throw fail("upload_read_lease_invalid");
        if(!lease.live(clock.getAsLong()))throw fail("upload_read_lease_revoked");collect();
        for(Entry entry:entries)if(entry!=null&&entry.lease==lease)return entry.token;
        int slot=0;while(slot<entries.length&&entries[slot]!=null)slot++;if(slot==entries.length)throw fail("upload_uri_limit");
        Entry entry=new Entry(lease);entries[slot]=entry;return entry.token;
    }
    synchronized FileInfo info(String token)throws IOException {return find(token).info;}
    synchronized Session reserve(String token)throws IOException {
        Entry entry=find(token);for(Session value:sessions)if(value!=null&&value.phase==UNCERTAIN)throw fail("upload_proxy_unclean");
        int slot=0;while(slot<sessions.length&&sessions[slot]!=null)slot++;if(slot==sessions.length)throw fail("upload_proxy_limit");
        Session session=new Session(this,entry,slot);sessions[slot]=session;return session;
    }
    private boolean original(Session value){return value.owner==this&&sessions[value.slot]==value;}
    private synchronized boolean enter(Session value){
        if(!original(value)||value.phase!=RESERVED||value.closed||!live(value.entry)){beforeOpenFailure(value);return false;}
        value.phase=OPENING;return true;
    }
    private synchronized boolean returned(Session value){
        if(!original(value)||value.closed||!live(value.entry)){value.closed=true;return false;}
        if(value.phase==RETURNED)return true;if(value.phase!=OPENING){value.closed=true;return false;}
        value.phase=RETURNED;return true;
    }
    private synchronized void beforeOpenFailure(Session value){if(original(value)&&value.phase==RESERVED)released(value);}
    private synchronized void openFailure(Session value){if(original(value)&&value.phase==OPENING){value.closed=true;value.phase=UNCERTAIN;}}
    private synchronized void closeFailure(Session value){if(original(value)&&(value.phase==OPENING||value.phase==RETURNED)){value.closed=true;value.phase=UNCERTAIN;}}
    private synchronized void cancel(Session value){value.closed=true;if(original(value)&&value.phase==RESERVED)released(value);}
    private synchronized void released(Session value){value.closed=true;if(original(value)){value.phase=RELEASED;sessions[value.slot]=null;}}
    synchronized void revoke(String token)throws IOException {Entry entry=find(token);entry.revoked=true;entry.lease.revoke();collect();}
    synchronized int grants(){collect();int count=0;for(Entry entry:entries)if(entry!=null)count++;return count;}
    synchronized int descriptors(){int count=0;for(Session value:sessions)if(value!=null)count++;return count;}
    synchronized int uncertain(){int count=0;for(Session value:sessions)if(value!=null&&value.phase==UNCERTAIN)count++;return count;}
    @Override public synchronized void close(){if(closed)return;closed=true;for(Entry entry:entries)if(entry!=null){entry.revoked=true;entry.lease.revoke();}collect();for(Session value:sessions)if(value!=null){value.closed=true;if(value.phase==RESERVED)released(value);}}
}
