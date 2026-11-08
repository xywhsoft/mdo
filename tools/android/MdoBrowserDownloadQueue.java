package org.xserver.android;

import java.io.IOException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.function.LongSupplier;

/** One file worker, 32 retained native tickets and one outstanding chunk per
 * ticket. The protocol worker only hands over native buffers and polls immutable
 * state; it never waits for storage, hashes or a UI delivery. No wire IDs/paths. */
final class MdoBrowserDownloadQueue implements AutoCloseable {
    interface Factory {MdoBrowserDownloadFiles open()throws IOException;}
    static final class Snapshot {
        final String state,code,sha256;final long bytes;final boolean fileVerified,fileCleanupConfirmed;
        final MdoBrowserCdpSession.BodyResult source;
        private Snapshot(String state,String code,long bytes,String sha,boolean verified,boolean clean,MdoBrowserCdpSession.BodyResult source){
            this.state=state;this.code=code;this.bytes=bytes;sha256=sha;fileVerified=verified;fileCleanupConfirmed=clean;this.source=source;
        }
        boolean completed(){return "completed".equals(state)&&fileVerified&&source!=null&&source.ok&&source.eof&&source.streamCloseConfirmed&&source.requestAbortConfirmed;}
    }
    static final class Ticket {
        private final MdoBrowserDownloadQueue owner;private final MdoBrowserCdpSession.Body source;
        private volatile Snapshot snapshot=new Snapshot("opening",null,0,null,false,false,null);
        // All fields below belong to gate, except Writer/size/failure which
        // belong only to the file worker. Never expose a filesystem handle.
        private boolean cancelled,releasing,busy,eofSubmitted,retired;private MdoBrowserCdpSession.BodyChunk chunk;
        private MdoBrowserDownloadFiles.Writer writer;private long bytes;private boolean eof,complete,clean=true;private String failure;
        private Ticket(MdoBrowserDownloadQueue owner,MdoBrowserCdpSession.Body source){this.owner=owner;this.source=source;}
        Snapshot snapshot(){return snapshot;}
    }
    /** A bounded one-shot range receipt. Only the file worker touches the
     * original descriptor; callers poll/take without waiting for filesystem I/O. */
    static final class Read implements AutoCloseable {
        private final MdoBrowserDownloadQueue owner;private final Ticket ticket;
        final long offset,size;final int limit;final String sha256;
        private boolean running,done,closed;private String error;private byte[] bytes;
        private Read(MdoBrowserDownloadQueue owner,Ticket ticket,long offset,int limit){
            this.owner=owner;this.ticket=ticket;this.offset=offset;this.limit=limit;
            size=ticket.snapshot.bytes;sha256=ticket.snapshot.sha256;
        }
        boolean finished(){synchronized(owner.gate){return done||closed;}}
        byte[] take()throws IOException {
            synchronized(owner.gate){
                if(closed)throw new IOException("download_read_cancelled");
                if(!done)throw new IOException("download_read_pending");
                if(error!=null){String code=error;close();throw new IOException(code);}
                byte[] result=bytes;bytes=null;close();return result;
            }
        }
        @Override public void close(){
            synchronized(owner.gate){closed=true;done=true;if(bytes!=null){java.util.Arrays.fill(bytes,(byte)0);bytes=null;}
                if(!running)owner.retireRead(this);owner.gate.notifyAll();}
        }
    }
    private final Object gate=new Object();private final Ticket[] tickets=new Ticket[MdoBrowserDownloadFiles.FILES];
    private final Read[] reads=new Read[4];private int readCursor;private boolean preferRead=true;
    private final Factory factory;private final LongSupplier clock;private final Thread worker;
    private final CountDownLatch stopped=new CountDownLatch(1);
    private MdoBrowserDownloadFiles files;private boolean closing;private int cursor;
    private volatile boolean cleanupFailed;private volatile long reservedBytes;private volatile int reservations;
    MdoBrowserDownloadQueue(Factory factory,LongSupplier clock){
        if(factory==null||clock==null)throw new IllegalArgumentException("Missing native download storage binding");
        this.factory=factory;this.clock=clock;worker=new Thread(this::loop,"mdo-browser-download");worker.setDaemon(true);worker.start();
    }
    Ticket begin(MdoBrowserCdpSession.Body source)throws IOException {
        if(source==null)throw new IOException("download_source_invalid");
        synchronized(gate){
            if(closing)throw new IOException("download_worker_closed");int slot=0;while(slot<tickets.length&&tickets[slot]!=null)slot++;
            if(slot==tickets.length)throw new IOException("download_slot_limit");
            Ticket value=new Ticket(this,source);
            if(!source.claimDownload())throw new IOException("download_source_already_consumed");
            tickets[slot]=value;gate.notifyAll();return value;
        }
    }
    private void owned(Ticket ticket)throws IOException {
        if(ticket==null||ticket.owner!=this)throw new IOException("download_owner_mismatch");
    }
    Read read(Ticket ticket,long offset,int limit)throws IOException {
        owned(ticket);synchronized(gate){
            if(closing)throw new IOException("download_worker_closed");
            if(ticket.retired||ticket.releasing||!ticket.snapshot.completed())throw new IOException("download_not_completed");
            if(offset<0||offset>ticket.snapshot.bytes||limit<1||limit>MdoBrowserDownloadFiles.CHUNK)throw new IOException("download_range_invalid");
            int slot=0;while(slot<reads.length&&reads[slot]!=null)slot++;
            if(slot==reads.length)throw new IOException("download_read_busy");
            Read result=new Read(this,ticket,offset,limit);reads[slot]=result;gate.notifyAll();return result;
        }
    }
    private void retireRead(Read read){for(int i=0;i<reads.length;i++)if(reads[i]==read){reads[i]=null;break;}}
    private void revokeReads(Ticket ticket){for(Read read:reads)if(read!=null&&read.ticket==ticket)read.close();}
    int retainedReads(){synchronized(gate){int count=0;for(Read read:reads)if(read!=null)count++;return count;}}
    boolean writable(Ticket ticket)throws IOException {
        owned(ticket);synchronized(gate){return !closing&&!ticket.retired&&!ticket.cancelled&&!ticket.releasing&&!ticket.busy&&ticket.chunk==null&&
            !ticket.eofSubmitted&&"receiving".equals(ticket.snapshot.state);}
    }
    /** true transfers sole buffer ownership. On false/error the caller retains
     * and must erase the chunk. A transferred chunk cannot be replayed/rebound. */
    boolean offer(Ticket ticket,MdoBrowserCdpSession.BodyChunk chunk)throws IOException {
        owned(ticket);if(chunk==null)throw new IOException("download_chunk_invalid");
        synchronized(gate){
            if(!writable(ticket))return false;
            if(chunk.offset!=ticket.snapshot.bytes||!chunk.claimDownload(ticket.source))throw new IOException("download_chunk_owner_mismatch");
            ticket.chunk=chunk;ticket.eofSubmitted=chunk.eof;gate.notifyAll();return true;
        }
    }
    void cancel(Ticket ticket)throws IOException {
        owned(ticket);synchronized(gate){if(ticket.retired||ticket.complete)return;ticket.cancelled=true;ticket.source.cancel();gate.notifyAll();}
    }
    void release(Ticket ticket)throws IOException {
        owned(ticket);synchronized(gate){if(ticket.retired)return;ticket.releasing=true;revokeReads(ticket);if(!ticket.complete)ticket.source.cancel();gate.notifyAll();}
    }
    private static String code(Throwable failure){
        String value=failure instanceof MdoBrowserDownloadFiles.Failure?((MdoBrowserDownloadFiles.Failure)failure).code:failure instanceof IOException?failure.getMessage():null;
        return value!=null&&value.matches("download_[a-z_]{1,80}")?value:"download_storage_failed";
    }
    private void publish(Ticket ticket,String state,String error,String hash,boolean verified){
        if(files!=null){reservations=files.reservations();reservedBytes=files.reservedBytes();}
        ticket.snapshot=new Snapshot(state,error,ticket.bytes,hash,verified,ticket.clean,ticket.source.result());
    }
    private void discard(Ticket ticket)throws IOException {
        MdoBrowserCdpSession.BodyChunk chunk;synchronized(gate){chunk=ticket.chunk;ticket.chunk=null;}
        if(chunk!=null)chunk.close();
        if(ticket.writer!=null){ticket.writer.close();ticket.writer=null;}
        if(files!=null)files.collectFailed();ticket.clean=true;
    }
    private void retire(Ticket ticket){
        synchronized(gate){ticket.retired=true;revokeReads(ticket);for(int i=0;i<tickets.length;i++)if(tickets[i]==ticket){tickets[i]=null;break;}}
    }
    private Read nextRead(){
        for(int i=0;i<reads.length;i++){int next=readCursor++%reads.length;Read value=reads[next];
            if(value!=null&&!value.closed&&!value.done&&!value.running){value.running=true;readCursor%=reads.length;return value;}}
        readCursor%=reads.length;return null;
    }
    private void stepRead(Read read){
        byte[] bytes=null;String error=null;Ticket ticket=read.ticket;
        try{
            synchronized(gate){if(closing||read.closed||ticket.releasing||ticket.retired)throw new IOException("download_read_cancelled");}
            if(!ticket.complete||ticket.failure!=null||ticket.writer==null)throw new IOException("download_not_completed");
            // Revalidate before a new range sequence and at its end. Every
            // range also checks the pinned file identity/size. The receiver
            // independently hashes the complete saved byte stream.
            if(read.offset==0)ticket.writer.verify();
            bytes=ticket.writer.read(read.offset,read.limit);
            if(read.offset+bytes.length==read.size)ticket.writer.verify();
        }catch(IOException|RuntimeException|LinkageError|OutOfMemoryError failure){
            error=ticket.failure==null?code(failure):ticket.failure;
            if(!error.equals("download_read_cancelled")){
                ticket.failure=error;ticket.complete=false;publish(ticket,"failed",error,null,false);
                synchronized(gate){for(Read other:reads)if(other!=null&&other!=read&&other.ticket==ticket)other.close();}
            }
        }finally{
            synchronized(gate){
                read.running=false;
                if(closing||read.closed||ticket.releasing||ticket.retired){
                    if(bytes!=null)java.util.Arrays.fill(bytes,(byte)0);read.closed=true;read.done=true;retireRead(read);
                }else{read.bytes=bytes;read.error=error;read.done=true;}
                gate.notifyAll();
            }
        }
    }
    private boolean needed(Ticket ticket){
        if(ticket.retired||ticket.busy)return false;
        if(closing)return true;
        if(ticket.releasing)return !ticket.clean||!"releasing".equals(ticket.snapshot.state)||ticket.snapshot.source!=ticket.source.result();
        if(ticket.failure!=null)return !ticket.clean||ticket.snapshot.source!=ticket.source.result();
        if(ticket.complete)return false;
        if(ticket.cancelled)return true;
        MdoBrowserCdpSession.BodyResult source=ticket.source.result();
        return ticket.writer==null||ticket.chunk!=null||source!=null&&(!source.ok||ticket.eof)||ticket.source.expired(clock.getAsLong());
    }
    private void step(Ticket ticket){
        try{
            boolean ending,cancelled,releasing;
            synchronized(gate){ending=closing;cancelled=ticket.cancelled;releasing=ticket.releasing;}
            MdoBrowserCdpSession.BodyResult source=ticket.source.result();
            if(ending||cancelled||releasing||ticket.failure!=null||source!=null&&!source.ok||!ticket.complete&&ticket.source.expired(clock.getAsLong())){
                if(ticket.failure==null&&!ticket.complete)ticket.failure=ending?"download_worker_closed":cancelled||releasing?"download_cancelled":source==null?"download_timeout":"download_source_failed";
                if(!ticket.complete)ticket.source.cancel();discard(ticket);
                boolean sourceDone=ticket.source.result()!=null;
                publish(ticket,ending?"closed":releasing?(sourceDone?"released":"releasing"):cancelled?(sourceDone?"cancelled":"cancelling"):"failed",ticket.failure,null,false);
                if(ending||releasing&&sourceDone)retire(ticket);return;
            }
            if(ticket.writer==null){
                if(files==null)files=factory.open();
                ticket.clean=false;ticket.writer=files.create(ticket.source.maximumBytes());publish(ticket,"receiving",null,null,false);return;
            }
            MdoBrowserCdpSession.BodyChunk chunk;synchronized(gate){chunk=ticket.chunk;ticket.chunk=null;}
            if(chunk!=null)try{
                if(chunk.offset!=ticket.bytes)throw new IOException("download_chunk_order_invalid");
                if(chunk.data.length!=0)ticket.writer.append(ticket.bytes,chunk.data);
                ticket.bytes+=chunk.data.length;ticket.eof=chunk.eof;
                publish(ticket,ticket.eof?"finishing":"receiving",null,null,false);
            }finally{chunk.close();}
            source=ticket.source.result();
            if(ticket.eof&&source!=null){
                if(!source.ok||!source.eof||!source.streamCloseConfirmed||!source.requestAbortConfirmed||source.bytes!=ticket.bytes)throw new IOException("download_source_receipt_invalid");
                ticket.writer.finish(source.bytes);String hash=ticket.writer.sha256();
                synchronized(gate){
                    if(closing||ticket.cancelled||ticket.releasing)return;
                    ticket.complete=true;publish(ticket,"completed",null,hash,true);
                }
            }
        }catch(IOException|RuntimeException|LinkageError|OutOfMemoryError failure){
            if(ticket.failure==null)ticket.failure=code(failure);ticket.source.cancel();
            try{discard(ticket);}catch(IOException|RuntimeException|OutOfMemoryError cleanup){cleanupFailed=true;}
            publish(ticket,"failed",ticket.failure,null,false);
        }finally{
            if(files!=null){reservations=files.reservations();reservedBytes=files.reservedBytes();}
            synchronized(gate){ticket.busy=false;}
        }
    }
    private void loop(){
        try{
            for(;;){
                Ticket selected=null;Read reading=null;
                synchronized(gate){
                    if(!closing&&preferRead)reading=nextRead();
                    if(reading==null)for(int i=0;i<tickets.length;i++){int next=cursor++%tickets.length;Ticket value=tickets[next];if(value!=null&&needed(value)){selected=value;value.busy=true;break;}}cursor%=tickets.length;
                    if(!closing&&reading==null&&selected==null)reading=nextRead();
                    if(selected==null&&reading==null){
                        if(closing)break;boolean polling=false;
                        // Only unfinished original responses need deadline/
                        // receipt polling. Empty or fully retained caches park
                        // until a real begin/offer/cancel/release/close signal.
                        for(Ticket value:tickets)if(value!=null&&!value.retired&&!value.complete&&
                            (value.failure==null||!value.clean||value.snapshot.source==null)){polling=true;break;}
                        try{gate.wait(polling?20:0);}catch(InterruptedException interrupted){closing=true;}continue;
                    }
                }
                if(reading!=null){preferRead=false;stepRead(reading);continue;}preferRead=true;
                step(selected);
                // A failed unlink is retained; avoid an unbounded busy retry.
                if(!selected.clean&&selected.failure!=null){synchronized(gate){if(closing)break;try{gate.wait(100);}catch(InterruptedException interrupted){closing=true;}}}
            }
        }finally{
            synchronized(gate){closing=true;for(Read read:reads)if(read!=null)read.close();}
            for(Ticket ticket:tickets)if(ticket!=null){ticket.source.cancel();try{discard(ticket);}catch(IOException|RuntimeException|OutOfMemoryError failure){cleanupFailed=true;}
                publish(ticket,"closed",ticket.failure==null?"download_worker_closed":ticket.failure,null,false);retire(ticket);}
            if(files!=null)try{files.close();reservations=0;reservedBytes=0;}catch(IOException|RuntimeException|OutOfMemoryError failure){cleanupFailed=true;}
            stopped.countDown();
        }
    }
    int reservations(){return reservations;}
    int retainedTickets(){synchronized(gate){int count=0;for(Ticket ticket:tickets)if(ticket!=null)count++;return count;}}
    long reservedBytes(){return reservedBytes;}
    boolean cleanupFailed(){return cleanupFailed;}
    @Override public void close(){synchronized(gate){closing=true;for(Read read:reads)if(read!=null)read.close();for(Ticket ticket:tickets)if(ticket!=null)ticket.source.cancel();gate.notifyAll();}}
    boolean awaitStopped(long milliseconds)throws InterruptedException{return stopped.await(milliseconds,TimeUnit.MILLISECONDS);}
}
