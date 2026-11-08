package org.xserver.android;

import java.io.IOException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;
import java.util.UUID;
import java.util.function.LongSupplier;

/** One worker owns all ticket state. Scope objects contain no Activity/View or
 * serializable authority; the UI retires them on exact native ownership loss. */
final class MdoBrowserUploadStore implements AutoCloseable {
    static final int TICKETS=64;
    static final class Scope {
        final long navigation;private volatile boolean cancelled;
        Scope(long navigation){if(navigation<=0)throw new IllegalArgumentException("Invalid upload epoch");this.navigation=navigation;}
        void cancel(){cancelled=true;}
        boolean live(){return !cancelled;}
    }
    /** One native browser-generation data owner. No View/Activity/channel or
     * serializable authority; transient input cancellation does not close it. */
    static final class RetentionOwner implements AutoCloseable {
        private final java.util.concurrent.atomic.AtomicBoolean closed=new java.util.concurrent.atomic.AtomicBoolean();
        boolean live(){return !closed.get();}
        @Override public void close(){closed.set(true);}
    }
    /** Created only after the original private picker callback returns. The
     * claim identity is private to that rendezvous, not a supplied nonce/ID. */
    static final class PickerGrant {
        private final ReadLease[] files;private final Object claim;
        private PickerGrant(ReadLease[] files,Object claim){this.files=files;this.claim=claim;}
        Scope scope(){return files[0].scope;}
    }
    static PickerGrant pickerGrant(ReadLease[] files,Object claim,long now)throws IOException {
        if(files==null||files.length<1||files.length>16||claim==null)throw fail("upload_picker_grant_invalid");
        ReadLease[] original=files.clone();Scope scope=null;Object owner=null;
        for(int i=0;i<original.length;i++){
            ReadLease file=original[i];if(file==null||!file.live(now)||file.picker.get()!=claim)throw fail("upload_picker_grant_invalid");
            if(i==0){scope=file.scope;owner=file.owner;}else if(file.scope!=scope||file.owner!=owner)throw fail("upload_picker_grant_invalid");
            for(int j=0;j<i;j++)if(original[j]==file)throw fail("upload_picker_grant_invalid");
        }
        return new PickerGrant(original,claim);
    }
    /** Worker-created object permit. Preparation is still staging-bound. The
     * native coordinator must adopt immediately before possible DOM dispatch;
     * thereafter bytes survive input scope/TTL loss until explicit release or
     * browser-owner teardown. Unknown assignment outcomes must not be replayed. */
    static final class Retention implements AutoCloseable {
        private final Object identity;private final PickerGrant grant;private final RetentionOwner owner;
        private final Scope scope;private final String ticket,key;private final Spec[] files;
        private final long born,expires,size;
        private final LongSupplier clock;
        private final java.util.concurrent.atomic.AtomicInteger state=new java.util.concurrent.atomic.AtomicInteger();
        private final java.util.concurrent.atomic.AtomicBoolean handed=new java.util.concurrent.atomic.AtomicBoolean();
        private volatile boolean cleaned;
        private Retention(Object identity,Job job,PickerGrant grant,RetentionOwner owner,long now,LongSupplier clock){
            this.identity=identity;this.grant=grant;this.owner=owner;scope=job.scope;ticket=job.id;key=job.key;files=job.specs.clone();
            born=now;expires=job.expires;size=job.size;this.clock=clock;
        }
        Scope scope(){return scope;}
        boolean adopted(){return state.get()==1;}
        boolean live(long now){int phase=state.get();return phase!=2&&owner.live()&&now>=born&&(phase==1||scope.live()&&now<expires);}
        boolean adopt(){synchronized(identity){
            try{long now=clock.getAsLong();if(state.get()==1)return live(now);if(!live(now))return false;
                for(ReadLease file:grant.files)if(file.revoked||file.retention!=this)return false;
                long checked=clock.getAsLong();if(checked<now||!live(checked))return false;
                if(!state.compareAndSet(0,1))return state.get()==1&&live(checked);return live(checked);
            }catch(RuntimeException|OutOfMemoryError error){return false;}
        }}
        void abandon(){state.compareAndSet(0,2);}
        void acknowledge(){handed.set(true);}
        void abandonUndelivered(){if(!handed.get())abandon();}
        @Override public void close(){state.set(2);}
    }
    static final class RetainedSnapshot {
        final String ticket,key,state;final long size;final Spec[] files;
        final boolean retained,provisional,cleanupConfirmed;
        RetainedSnapshot(Retention value,long now){
            ticket=value.ticket;key=value.key;size=value.size;files=value.files.clone();cleanupConfirmed=value.cleaned;
            retained=!cleanupConfirmed&&value.adopted()&&value.live(now);provisional=!cleanupConfirmed&&!value.adopted()&&value.live(now);
            state=cleanupConfirmed?"released":retained?"retained":provisional?"prepared":"cleanup_pending";
        }
    }
    static final class Spec {
        final String name,sha256;final long size;
        Spec(String name,long size,String sha256)throws MdoBrowserUploadFiles.Failure {
            if(!safeName(name)||size<0||size>MdoBrowserUploadFiles.FILE_BYTES||sha256==null||!sha256.matches("[a-f0-9]{64}"))throw fail("upload_metadata_invalid");
            this.name=name;this.size=size;this.sha256=sha256;
        }
        boolean same(Spec other){return other!=null&&size==other.size&&name.equals(other.name)&&sha256.equals(other.sha256);}
    }
    static boolean bounded(String text,int maximum){
        if(text==null||text.isEmpty())return false;int bytes=0;
        for(int at=0;at<text.length();){int code=text.codePointAt(at);if(code<32||code==127||code>=0xd800&&code<=0xdfff)return false;
            bytes+=code<128?1:code<2048?2:code<65536?3:4;if(bytes>maximum)return false;at+=Character.charCount(code);}
        return true;
    }
    static boolean safeName(String name){
        if(!bounded(name,240)||name.endsWith(".")||name.endsWith(" "))return false;
        for(int at=0;at<name.length();at++)if("<>:\"/\\|?*".indexOf(name.charAt(at))>=0)return false;
        String stem=name.split("\\.",2)[0].toUpperCase(java.util.Locale.ROOT);
        if(stem.endsWith(" ")||Arrays.asList("CON","PRN","AUX","NUL","CONIN$","CONOUT$","CLOCK$").contains(stem))return false;
        return !(stem.length()==4&&(stem.startsWith("COM")||stem.startsWith("LPT"))&&"0123456789\u00b9\u00b2\u00b3".indexOf(stem.charAt(3))>=0);
    }
    static void manifest(Spec[] specs)throws MdoBrowserUploadFiles.Failure {
        if(specs==null||specs.length<1||specs.length>16)throw fail("upload_metadata_invalid");
        long total=0;for(Spec spec:specs){if(spec==null)throw fail("upload_metadata_invalid");total+=spec.size;}
        if(total>67108864L)throw fail("upload_budget_exceeded");
    }
    static final class Snapshot {
        final String ticket,key,state;final long navigation,expires,size;final Spec[] files;final long[] received;
        final boolean sealed,cleanupConfirmed,retained;
        Snapshot(Job job){
            ticket=job.id;key=job.key;navigation=job.scope.navigation;expires=job.expires;size=job.size;
            files=job.specs.clone();received=job.received.clone();sealed=job.sealed&&!job.failed&&!job.closing&&!job.retired;
            retained=job.retention!=null&&job.retention.adopted()&&!job.retired&&!job.closing;
            state=job.retired?"released":job.closing?"cleanup_pending":job.failed?"failed":retained?"retained":sealed?"sealed":"receiving";cleanupConfirmed=job.retired;
        }
    }
    /** Native-only object authority. Never serialized, refreshed or restored.
     * It owns no View/Activity/thread/file handle and is revoked before cleanup. */
    static final class ReadLease {
        final Scope scope;final Spec file;final long expires;
        private final Object owner;private final String ticket;private final int index;private volatile boolean revoked;private volatile Retention retention;
        // A plain native identity, never a View/Activity/controller. A callback
        // handoff cannot reuse/revoke permissions granted by an older picker.
        private final java.util.concurrent.atomic.AtomicReference<Object> picker=new java.util.concurrent.atomic.AtomicReference<>();
        private ReadLease(Object owner,Job job,int index){this.owner=owner;scope=job.scope;ticket=job.id;this.index=index;file=job.specs[index];expires=job.expires;}
        boolean live(long now){Retention kept=retention;return !revoked&&(kept==null?scope.live()&&now>=0&&now<expires:kept.live(now));}
        boolean belongs(Object identity){return owner==identity;}
        boolean claimPicker(Object identity,long now){return identity!=null&&live(now)&&(picker.get()==identity||picker.compareAndSet(null,identity))&&live(now);}
        void revoke(){revoked=true;Retention kept=retention;if(kept!=null){
            for(ReadLease file:kept.grant.files)if(!file.revoked)return;kept.close();}}
        void revokeStaging(){synchronized(owner){
            // Serialize transient cancellation with the original queue's data
            // adoption. A stale pre-adoption check must not revoke an adopted
            // provider read permission after possible renderer dispatch.
            Retention kept=retention;if(kept!=null&&kept.adopted())return;if(kept!=null)kept.abandon();revoke();
        }}
    }
    private static final class Job {
        final String id="u-"+UUID.randomUUID(),key;final Scope scope;final long expires,size;final Spec[] specs;
        final MdoBrowserUploadFiles.Leaf[] files;final ReadLease[] readers;final long[] received,lastOffset;final byte[][] lastHash;final int[] lastSize;
        boolean sealed,failed,closing,retired,inputCancelled;Retention retention;Object assigner;
        Job(Scope scope,String key,Spec[] specs,long expires){
            this.scope=scope;this.key=key;this.specs=specs.clone();this.expires=expires;
            files=new MdoBrowserUploadFiles.Leaf[specs.length];readers=new ReadLease[specs.length];received=new long[specs.length];lastOffset=new long[specs.length];lastHash=new byte[specs.length][];lastSize=new int[specs.length];
            long total=0;for(Spec spec:specs)total+=spec.size;size=total;
        }
        boolean same(Spec[] values){if(values.length!=specs.length)return false;for(int at=0;at<values.length;at++)if(!specs[at].same(values[at]))return false;return true;}
    }
    private final Thread owner=Thread.currentThread();private final Object identity;private final MdoBrowserUploadFiles files;private final LongSupplier clock;private final Job[] jobs=new Job[TICKETS];
    private boolean closed;
    MdoBrowserUploadStore(MdoBrowserUploadFiles files,LongSupplier clock){this(files,clock,new Object());}
    MdoBrowserUploadStore(MdoBrowserUploadFiles files,LongSupplier clock,Object identity){if(files==null||clock==null||identity==null)throw new IllegalArgumentException("Missing upload files/clock");this.files=files;this.clock=clock;this.identity=identity;}
    private static MdoBrowserUploadFiles.Failure fail(String code){return new MdoBrowserUploadFiles.Failure(code);}
    private void thread(){if(Thread.currentThread()!=owner)throw new IllegalStateException("Upload tickets require their owning worker thread");}
    private void active()throws MdoBrowserUploadFiles.Failure {thread();if(closed)throw fail("upload_store_closed");}
    private void live(Scope scope,long expires)throws MdoBrowserUploadFiles.Failure {
        active();if(scope==null||!scope.live())throw fail("upload_owner_changed");long now=clock.getAsLong();if(now<0||now>=expires)throw fail("upload_expired");
    }
    private Job exact(Scope scope,String id)throws MdoBrowserUploadFiles.Failure {
        active();for(Job job:jobs)if(job!=null&&job.scope==scope&&job.id.equals(id))return job;throw fail("upload_ticket_not_found");
    }
    private void writable(Job job)throws MdoBrowserUploadFiles.Failure {
        live(job.scope,job.expires);if(job.closing||job.retired||job.failed||job.inputCancelled)throw fail("upload_not_receiving");
    }
    private boolean release(Job job){
        job.closing=true;boolean clean=true;
        if(job.retention!=null)job.retention.close();
        for(int at=0;at<job.readers.length;at++)if(job.readers[at]!=null){job.readers[at].revoke();job.readers[at]=null;}
        for(int at=0;at<job.files.length;at++)if(job.files[at]!=null)try{job.files[at].close();job.files[at]=null;}catch(IOException|RuntimeException failure){clean=false;}
        if(clean){job.retired=true;if(job.retention!=null)job.retention.cleaned=true;job.lastHash[0]=null;for(int at=1;at<job.lastHash.length;at++)job.lastHash[at]=null;}
        return clean;
    }
    Snapshot begin(Scope scope,String key,Spec[] specs,long expires)throws IOException {
        live(scope,expires);manifest(specs);long now=clock.getAsLong();
        if(!bounded(key,100)||expires-now>120000L)throw fail("upload_metadata_invalid");
        collect(now);
        for(Job old:jobs)if(old!=null&&old.scope==scope&&old.key.equals(key)){
            if(!old.same(specs))throw fail("upload_identity_conflict");
            if(old.retired||old.closing||old.failed)throw fail("upload_operation_retired");return new Snapshot(old);
        }
        int slot=0;while(slot<jobs.length&&jobs[slot]!=null)slot++;if(slot==jobs.length)throw fail("upload_operation_limit");
        Job job=new Job(scope,key,specs,expires);jobs[slot]=job;
        try{
            for(int at=0;at<job.files.length;at++){
                live(scope,expires);
                try{job.files[at]=files.create(job.specs[at].size,job.specs[at].sha256);}
                catch(IOException|RuntimeException|Error failure){job.files[at]=files.takeFailedCreation();throw failure;}
            }
            live(scope,expires);return new Snapshot(job);
        }catch(IOException|RuntimeException|Error failure){job.failed=true;release(job);throw failure;}
    }
    Snapshot chunk(Scope scope,String id,int index,long offset,byte[] bytes)throws IOException {
        Job job=exact(scope,id);writable(job);if(job.sealed)throw fail("upload_already_sealed");
        if(index<0||index>=job.files.length||bytes==null||bytes.length<1||bytes.length>MdoBrowserUploadFiles.CHUNK)throw fail("upload_chunk_invalid");
        byte[] hash;try{hash=MessageDigest.getInstance("SHA-256").digest(bytes);}catch(NoSuchAlgorithmException impossible){throw fail("upload_hash_unavailable");}
        if(offset==job.lastOffset[index]&&job.lastHash[index]!=null&&bytes.length==job.lastSize[index]&&job.received[index]==offset+bytes.length){
            if(!Arrays.equals(hash,job.lastHash[index]))throw fail("upload_chunk_conflict");return new Snapshot(job);
        }
        if(offset!=job.received[index]||bytes.length>job.specs[index].size-offset)throw fail("upload_chunk_invalid");
        try{
            job.files[index].append(offset,bytes);job.received[index]+=bytes.length;job.lastOffset[index]=offset;job.lastHash[index]=hash;job.lastSize[index]=bytes.length;
            live(scope,job.expires);return new Snapshot(job);
        }catch(IOException|RuntimeException|Error failure){job.failed=true;release(job);throw failure;}
    }
    Snapshot seal(Scope scope,String id)throws IOException {
        Job job=exact(scope,id);writable(job);
        for(int at=0;at<job.files.length;at++)if(job.received[at]!=job.specs[at].size)throw fail("upload_incomplete");
        try{
            for(MdoBrowserUploadFiles.Leaf file:job.files){live(scope,job.expires);file.seal();file.verify();}
            live(scope,job.expires);job.sealed=true;return new Snapshot(job);
        }catch(IOException|RuntimeException|Error failure){job.failed=true;release(job);throw failure;}
    }
    Snapshot status(Scope scope,String id,long now)throws IOException {
        thread();try{collect(now);}catch(IOException ignored){/* Preserve independently inspectable ticket state. */}
        return new Snapshot(exact(scope,id));
    }
    Snapshot claim(Scope scope,String id,Object assigner)throws IOException {
        Job job=exact(scope,id);writable(job);if(!job.sealed)throw fail("upload_not_sealed");
        if(assigner==null)throw fail("upload_assignment_owner_invalid");
        if(job.assigner!=null&&job.assigner!=assigner)throw fail("upload_already_assigning");job.assigner=assigner;return new Snapshot(job);
    }
    Snapshot cancelClaim(Scope scope,String id,Object assigner)throws IOException {
        Job job=exact(scope,id);if(assigner==null||job.assigner!=assigner)throw fail("upload_assignment_owner_changed");return cancel(scope,id);
    }
    ReadLease access(Scope scope,String id,int index)throws IOException {
        Job job=exact(scope,id);writable(job);if(!job.sealed)throw fail("upload_not_sealed");
        if(index<0||index>=job.files.length)throw fail("upload_range_invalid");
        ReadLease old=job.readers[index];
        if(old!=null){if(!old.live(clock.getAsLong()))throw fail("upload_read_lease_revoked");return old;}
        try{
            job.files[index].verify();live(scope,job.expires);ReadLease lease=new ReadLease(identity,job,index);job.readers[index]=lease;return lease;
        }catch(IOException|RuntimeException|Error failure){job.failed=true;release(job);throw failure;}
    }
    Retention retain(PickerGrant grant,RetentionOwner retainer)throws IOException {
        active();if(grant==null||retainer==null||!retainer.live())throw fail("upload_retention_owner_changed");
        ReadLease[] readers=grant.files;ReadLease first=readers[0];
        if(first.owner!=identity)throw fail("upload_read_lease_invalid");Job job=exact(first.scope,first.ticket);
        if(job.retention!=null){Retention old=job.retention;
            if(old.grant!=grant||old.owner!=retainer)throw fail("upload_retention_identity_conflict");
            if(job.closing||job.retired||job.failed||!old.live(clock.getAsLong()))throw fail("upload_retention_retired");return old;}
        if(readers.length!=job.readers.length)throw fail("upload_retention_manifest_invalid");
        for(int i=0;i<readers.length;i++)if(readers[i].owner!=identity||readers[i].scope!=job.scope||!readers[i].ticket.equals(job.id)||
            readers[i].index!=i||job.readers[i]!=readers[i]||readers[i].picker.get()!=grant.claim)throw fail("upload_retention_manifest_invalid");
        writable(job);if(!job.sealed)throw fail("upload_not_sealed");
        for(int i=0;i<readers.length;i++)if(!readers[i].live(clock.getAsLong()))throw fail("upload_read_lease_revoked");
        try{
            for(MdoBrowserUploadFiles.Leaf file:job.files){file.verify();live(job.scope,job.expires);}
            if(!retainer.live())throw fail("upload_retention_owner_changed");
            Retention value=new Retention(identity,job,grant,retainer,clock.getAsLong(),clock);job.retention=value;
            for(ReadLease reader:readers)reader.retention=value;
            if(!value.live(clock.getAsLong()))throw fail("upload_retention_owner_changed");return value;
        }catch(IOException|RuntimeException|Error failure){job.failed=true;release(job);throw failure;}
    }
    private void readable(Job job,ReadLease lease)throws IOException {
        if(job.retention==null)writable(job);
        if(job.closing||job.retired||job.failed||!job.sealed||job.readers[lease.index]!=lease||!lease.live(clock.getAsLong()))throw fail("upload_read_lease_revoked");
    }
    byte[] read(ReadLease lease,long offset,int limit)throws IOException {
        active();if(lease==null||lease.owner!=identity)throw fail("upload_read_lease_invalid");
        Job job=exact(lease.scope,lease.ticket);readable(job,lease);
        if(offset<0||offset>lease.file.size||limit<1||limit>MdoBrowserUploadFiles.CHUNK)throw fail("upload_range_invalid");
        try{
            byte[] bytes=job.files[lease.index].read(offset,limit);readable(job,lease);return bytes;
        }catch(IOException|RuntimeException|Error failure){job.failed=true;release(job);throw failure;}
    }
    Snapshot cancel(Scope scope,String id)throws IOException {Job job=exact(scope,id);
        if(job.retention!=null&&job.retention.adopted()&&job.retention.live(clock.getAsLong())){job.inputCancelled=true;return new Snapshot(job);}
        if(!release(job))throw fail("upload_cleanup_failed");return new Snapshot(job);}
    Snapshot releaseConfirmed(Scope scope,String id)throws IOException {Job job=exact(scope,id);if(!release(job))throw fail("upload_cleanup_failed");return new Snapshot(job);}
    RetainedSnapshot retained(Retention permit,boolean releaseRequested)throws IOException {
        active();if(permit==null||permit.identity!=identity)throw fail("upload_retention_invalid");
        if(!permit.cleaned){Job job=exact(permit.scope,permit.ticket);if(job.retention!=permit)throw fail("upload_retention_invalid");
            if(releaseRequested)permit.close();if(!permit.live(clock.getAsLong())&&!release(job))throw fail("upload_cleanup_failed");}
        return new RetainedSnapshot(permit,clock.getAsLong());
    }
    void collect(long now)throws IOException {
        thread();boolean clean=true;
        for(int at=0;at<jobs.length;at++){
            Job job=jobs[at];if(job==null)continue;
            if(job.closing||(job.retention!=null?!job.retention.live(now):!job.scope.live()||now>=job.expires)){if(!release(job))clean=false;}
            if(job.retired&&!job.scope.live())jobs[at]=null;
        }
        try{files.collectFailed();}catch(IOException failure){clean=false;}
        if(!clean)throw fail("upload_cleanup_failed");
    }
    int reservations(){thread();return files.reservations();}
    long reservedBytes(){thread();return files.reservedBytes();}
    @Override public void close()throws IOException {
        thread();closed=true;boolean clean=true;for(Job job:jobs)if(job!=null&&!release(job))clean=false;
        if(!clean)throw fail("upload_cleanup_failed");files.close();
    }
}
