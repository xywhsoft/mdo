package org.xserver.android;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import org.json.JSONException;
import org.json.JSONObject;

/** Native controller for one physical owner. Public UI operations only inspect
 * immutable records or submit atomic decisions. Events/turns run on the existing
 * protocol worker; disk work stays on the shared bounded download file worker. */
final class MdoBrowserCdpDownloads implements AutoCloseable {
    interface AttachedSource {
        MdoBrowserCdpSession.AttachedResponse next()throws IOException;
        MdoBrowserCdpSession.Offer retain(MdoBrowserCdpSession.AttachedResponse response)throws IOException;
    }
    /** One opaque callback authority per explicitly armed native connection.
     * Never resolve an old callback through the controller's current ticket. */
    static final class Connection implements MdoBrowserCdpNetworks.Events {
        private final MdoBrowserCdpDownloads owner;
        private boolean cleanupConfirmed;
        private Connection(MdoBrowserCdpDownloads owner){this.owner=owner;}
        @Override public void event(MdoBrowserCdpBinding.Network network,String method,String child,JSONObject params)throws IOException {owner.event(this,network,method,child,params);}
        @Override public void turn(MdoBrowserCdpBinding.Network network)throws IOException {owner.turn(this,network);}
        @Override public boolean navigationAllowed(){synchronized(owner.gate){return owner.current==this&&!owner.closed&&!owner.disconnected&&!owner.quiescing&&!owner.pending();}}
        void attach(MdoBrowserCdpPopupBroker.Handoff physical)throws IOException {if(physical==null||!physical.current())throw new IOException("cdp_download_owner_changed");owner.protocol(this,physical);}
        boolean attachedTurn(MdoBrowserCdpPopupBroker.Handoff physical,AttachedSource source,int timeout)throws IOException {return owner.attachedTurn(this,physical,source,timeout);}
        void disconnect(){owner.disconnect(this,false);}
        void disconnect(boolean cleanupConfirmed){owner.disconnect(this,cleanupConfirmed);}
        boolean retirementQuiescent(){synchronized(owner.gate){return owner.current==this&&owner.quiescing&&owner.disconnected&&owner.sourcesClean();}}
    }
    static final class Budget {
        private int used;
        synchronized boolean acquire(){if(used>=32)return false;used++;return true;}
        synchronized void release(){if(used<=0)throw new IllegalStateException("Download receipt budget underflow");used--;}
        synchronized int used(){return used;}
    }
    static final class Snapshot {
        final String id,name,mime,state,error,sha256,failureSource;final long received,expected,maximum,sequence;
        final boolean verified,cacheCleanupConfirmed,sourceCleanupConfirmed,frameCurrent,root;final String sourceFrame,sourceDocument,sourceRequest,sourceEpochKind;
        private Snapshot(Row row,String state,String error,long bytes,String hash,boolean verified,boolean cacheClean,boolean sourceClean,String source){
            id=row.id;name=row.offer.metadata().name;mime=row.offer.metadata().mime;sequence=row.sequence;expected=row.expected;maximum=row.maximum;this.state=state;this.error=error;received=bytes;sha256=hash;
            this.verified=verified;cacheCleanupConfirmed=cacheClean;sourceCleanupConfirmed=sourceClean;failureSource=source;
            frameCurrent=row.origin.current();root=row.origin.root;sourceFrame=row.origin.frame;sourceDocument=row.origin.document;
            sourceRequest=row.offer.sourceRequestReference();
            sourceEpochKind="navigation_candidates".equals(row.offer.sourceEpochKind())&&sourceRequest!=null?"original_navigation":row.offer.sourceEpochKind();
        }
        boolean terminal(){return state.equals("completed")||state.equals("failed")||state.equals("cancelled");}
        JSONObject json()throws JSONException {
            JSONObject out=new JSONObject().put("id",id).put("name",name).put("mime",mime).put("state",state)
                .put("received",received).put("expected_size",expected<0?JSONObject.NULL:expected).put("max_bytes",maximum)
                .put("interrupt_reason",0).put("failure_source",failureSource==null?"":failureSource).put("error",error==null?"":error)
                .put("native",new JSONObject().put("cache_cleanup_confirmed",cacheCleanupConfirmed).put("source_cleanup_confirmed",sourceCleanupConfirmed)
                    .put("source_frame",sourceFrame).put("source_document",sourceDocument.isEmpty()?JSONObject.NULL:sourceDocument).put("source_is_root",root).put("source_current",frameCurrent).put("source_epoch_kind",sourceEpochKind)
                    .put("source_request_ref",sourceRequest==null?JSONObject.NULL:sourceRequest));
            if(verified&&state.equals("completed"))out.put("size",received).put("sha256",sha256);return out;
        }
    }
    static final class Release {
        private final Row row;private final MdoBrowserCdpDownloads owner;
        private Release(MdoBrowserCdpDownloads owner,Row row){this.owner=owner;this.row=row;}
        boolean finished(){owner.refresh(row);return row.releaseDone;}
        boolean released(){return finished()&&row.snapshot.state.equals("released");}
        Snapshot snapshot(){owner.refresh(row);return row.snapshot;}
    }
    private static final class Row {
        final String id="d-"+UUID.randomUUID();final long sequence,expected;final MdoBrowserCdpSession.Offer offer;
        final MdoBrowserCdpNetworkFrames.Origin origin;final Connection connection;
        MdoBrowserCdpSession.AttachedResponse response;
        volatile Snapshot snapshot;volatile MdoBrowserDownloadQueue.Ticket ticket;volatile boolean releaseDone;
        long maximum=33554432;boolean accepted,cancelled,releasing,reading,eof,charged=true,retired;Release release;
        MdoBrowserCdpSession.Body body;String error,failureSource;
        Row(long sequence,MdoBrowserCdpSession.Offer offer,MdoBrowserCdpNetworkFrames.Origin origin,Connection connection){this(sequence,offer,origin,connection,null);}
        Row(long sequence,MdoBrowserCdpSession.Offer offer,MdoBrowserCdpNetworkFrames.Origin origin,Connection connection,MdoBrowserCdpSession.AttachedResponse response){this.sequence=sequence;this.offer=offer;this.origin=origin;this.connection=connection;this.response=response;expected=offer.metadata().declaredLength;}
    }
    private final Object gate=new Object();private final Row[] rows=new Row[32];
    private final MdoBrowserDownloadQueue.Ticket[] retiring=new MdoBrowserDownloadQueue.Ticket[32];
    private final MdoBrowserDownloadQueue files;private final Budget budget;
    private final Thread uiOwner=Thread.currentThread();
    private Connection current=new Connection(this);private int generations=1;
    private Object original;private Thread worker;private long serial;private boolean closed,disconnected,quiescing,sourceCleanupUnconfirmed;
    private volatile String failure;
    MdoBrowserCdpDownloads(MdoBrowserDownloadQueue files,Budget budget){
        if(files==null||budget==null)throw new IllegalArgumentException("Missing native download services");this.files=files;this.budget=budget;
    }
    Connection connection(){synchronized(gate){return current;}}
    /** The physical owner explicitly rearms after native cleanup. Retained
     * rows/files keep their original authority, IDs, ordering and shared quota. */
    Connection rearm()throws IOException {
        synchronized(gate){
            active();if(Thread.currentThread()!=uiOwner)throw new IOException("download_requires_ui_thread");
            if(!disconnected||!current.cleanupConfirmed||original instanceof MdoBrowserCdpBinding.Network&&!((MdoBrowserCdpBinding.Network)original).cleanupConfirmed())throw new IOException("download_connection_retiring");
            for(Row row:rows)if(row!=null){summarize(row);if(!row.snapshot.sourceCleanupConfirmed)throw new IOException("download_source_cleanup_unconfirmed");}
            if(generations>=4096)throw new IOException("download_generation_limit");
            Connection next=new Connection(this);generations++;current=next;original=null;worker=null;
            disconnected=false;quiescing=false;failure=null;return next;
        }
    }
    private void protocol(Connection connection,Object network)throws IOException {
        synchronized(gate){
            if(network==null)throw new IOException("cdp_download_owner_changed");
            if(closed)throw new IOException("cdp_download_owner_closed");
            if(connection!=current)throw new IOException("cdp_download_connection_changed");
            if(disconnected)throw new IOException("cdp_download_connection_closed");
            if(worker==null){worker=Thread.currentThread();original=network;}
            if(worker!=Thread.currentThread()||original!=network)throw new IOException("cdp_download_owner_changed");
        }
    }
    private void event(Connection connection,MdoBrowserCdpBinding.Network network,String method,String child,JSONObject params)throws IOException {
        synchronized(gate){
            protocol(connection,network);if(!"Fetch.requestPaused".equals(method))return;
            if(quiescing){network.continuePausedRequest();return;}
            MdoBrowserCdpResponse response=network.pausedResponse();
            if(!response.kind.equals("candidate")){network.continuePausedRequest();return;}
            // Child download identity requires its separate frame association.
            if(child!=null){failure="download_frame_unproven";network.abortPausedRequest();return;}
            try{network.pausedOrigin();}
            catch(IOException error){if(!"download_frame_unproven".equals(error.getMessage()))throw error;failure="download_frame_unproven";network.abortPausedRequest();return;}
            boolean declined=false;
            int slot=0;while(slot<rows.length&&rows[slot]!=null)slot++;
            if(slot==rows.length||!budget.acquire()){failure="download_receipt_limit";declined=true;}
            else{
                MdoBrowserCdpSession.Offer offer=null;boolean published=false;
                try{
                    if(network.pendingOffers()>=8){failure="download_pending_limit";declined=true;}
                    else{offer=network.retainPausedResponse();Row row=new Row(++serial,offer,offer.sourceEpoch(),connection);
                        if("unproven".equals(offer.sourceEpochKind())){row.error="download_frame_unproven";row.failureSource="native_frame";}
                        if(!row.origin.current()){revokeFrame(row);publish(row,"cancelling",row.error,0,null,false,true,false,row.failureSource);}
                        else publish(row,"pending",null,0,null,false,true,false,null);
                        rows[slot]=row;published=true;}
                }finally{if(!published){budget.release();if(offer!=null){offer.cancel();offer.retireRequestAssociation();}}}
            }
            if(declined)network.abortPausedRequest();
        }
    }
    Snapshot[] list()throws IOException {
        synchronized(gate){active();List<Snapshot> result=new ArrayList<>();for(Row row:rows)if(row!=null){summarize(row);if(!row.retired)result.add(row.snapshot);}
            result.sort(Comparator.comparingLong(value->value.sequence));return result.toArray(new Snapshot[0]);}
    }
    Snapshot get(String id)throws IOException {synchronized(gate){Row row=find(id);summarize(row);return row.snapshot;}}
    MdoBrowserDownloadQueue.Read read(String id,long offset,int limit)throws IOException {
        synchronized(gate){Row row=find(id);summarize(row);
            if(!row.snapshot.state.equals("completed")||!row.snapshot.verified||row.releasing||row.ticket==null)throw new IOException("download_not_completed");
            return files.read(row.ticket,offset,limit);}
    }
    private void active()throws IOException {if(closed)throw new IOException("download_owner_closed");}
    private Row find(String id)throws IOException {
        active();if(id==null||id.length()>100)throw new IOException("download_identity_invalid");
        for(Row row:rows)if(row!=null&&row.id.equals(id))return row;throw new IOException("download_identity_unavailable");
    }
    Snapshot accept(String id,long maximum,int timeoutMs)throws IOException {
        synchronized(gate){Row row=find(id);summarize(row);if(row.snapshot.terminal()||row.accepted||row.cancelled||row.releasing)return row.snapshot;
            if(row.connection!=current)throw new IOException("download_generation_retired");
            if(disconnected)throw new IOException("download_connection_closed");
            if(!row.origin.current()){revokeFrame(row);summarize(row);throw new IOException(row.error==null?"download_frame_changed":row.error);}
            try{row.offer.accept(maximum,timeoutMs);}catch(IOException failure){
                if(!"cdp_offer_byte_limit".equals(failure.getMessage()))throw failure;
                row.maximum=maximum;row.accepted=true;row.error="download_limit";row.failureSource="policy";row.offer.cancel();
                publish(row,"cancelling",row.error,0,null,false,true,false,row.failureSource);return row.snapshot;
            }
            row.maximum=maximum;row.accepted=true;
            publish(row,"receiving",null,row.snapshot.received,null,false,row.snapshot.cacheCleanupConfirmed,false,null);return row.snapshot;}
    }
    Snapshot cancel(String id)throws IOException {
        synchronized(gate){Row row=find(id);summarize(row);if(row.snapshot.terminal()||row.releasing)return row.snapshot;
            row.cancelled=true;row.offer.cancel();if(row.ticket!=null)files.cancel(row.ticket);
            publish(row,"cancelling","cancelled",row.snapshot.received,null,false,row.snapshot.cacheCleanupConfirmed,false,"user");return row.snapshot;}
    }
    Release release(String id)throws IOException {
        synchronized(gate){Row row=find(id);summarize(row);if(row.release!=null)return row.release;
            if(!row.snapshot.terminal())throw new IOException("download_not_terminal");
            if(!row.snapshot.sourceCleanupConfirmed)throw new IOException("download_source_cleanup_unconfirmed");
            Release result=new Release(this,row);row.release=result;row.releasing=true;
            publish(row,"releasing",null,row.snapshot.received,null,false,row.snapshot.cacheCleanupConfirmed,true,null);
            if(row.ticket!=null)files.release(row.ticket);return result;}
    }
    private static String safe(Throwable error){
        String code=error.getMessage();return code!=null&&code.matches("(?:cdp|download)_[a-z_]{1,80}")?code:"download_controller_failed";
    }
    private static boolean sourceClean(Row row){
        MdoBrowserCdpSession.OfferResult offer=row.offer.result();if(offer==null)return false;
        if(offer.requestAbortConfirmed)return true;
        MdoBrowserCdpSession.BodyResult result=offer.body==null?null:offer.body.result();return result!=null&&result.streamCloseConfirmed&&result.requestAbortConfirmed;
    }
    private void publish(Row row,String state,String error,long bytes,String hash,boolean verified,boolean cacheClean,boolean sourceClean,String source){
        Snapshot old=row.snapshot;
        if(old!=null&&old.maximum==row.maximum&&old.state.equals(state)&&Objects.equals(old.error,error)&&old.received==bytes&&Objects.equals(old.sha256,hash)&&
            old.verified==verified&&old.cacheCleanupConfirmed==cacheClean&&old.sourceCleanupConfirmed==sourceClean&&old.frameCurrent==row.origin.current()&&Objects.equals(old.sourceRequest,row.offer.sourceRequestReference())&&Objects.equals(old.failureSource,source))return;
        row.snapshot=new Snapshot(row,state,error,bytes,hash,verified,cacheClean,sourceClean,source);
    }
    private void retire(Row row){
        row.offer.retireRequestAssociation();row.retired=true;if(row.charged){row.charged=false;budget.release();}for(int i=0;i<rows.length;i++)if(rows[i]==row)rows[i]=null;
    }
    private void summarize(Row row){
        if(row.retired)return;
        Snapshot old=row.snapshot;if(old!=null&&!Objects.equals(old.sourceRequest,row.offer.sourceRequestReference()))publish(row,old.state,old.error,old.received,old.sha256,old.verified,old.cacheCleanupConfirmed,old.sourceCleanupConfirmed,old.failureSource);
        if(row.releasing){
            MdoBrowserDownloadQueue.Snapshot file=row.ticket==null?null:row.ticket.snapshot();
            if(file==null||file.state.equals("released")&&file.fileCleanupConfirmed){
                publish(row,"released",null,row.snapshot.received,null,false,true,sourceClean(row),null);row.releaseDone=true;retire(row);
            }else if(file.code!=null&&!file.fileCleanupConfirmed){publish(row,"releasing",file.code,row.snapshot.received,null,false,false,sourceClean(row),"cache");}return;
        }
        MdoBrowserCdpSession.OfferResult offer=row.offer.result();
        if(offer!=null&&(!offer.accepted||offer.body!=null&&offer.body.result()!=null))row.response=null;
        if(offer==null){if(disconnected)publish(row,"failed","download_connection_closed",row.snapshot.received,null,false,row.ticket==null,false,"native_protocol");return;}
        if(!offer.accepted){boolean cancelled=row.cancelled&&offer.requestAbortConfirmed;
            publish(row,cancelled?"cancelled":"failed",cancelled?"cancelled":row.error==null?offer.code:row.error,0,null,false,true,offer.requestAbortConfirmed,
                cancelled?"user":row.failureSource==null?"native_protocol":row.failureSource);return;}
        row.body=offer.body;
        MdoBrowserDownloadQueue.Snapshot file=row.ticket==null?null:row.ticket.snapshot();MdoBrowserCdpSession.BodyResult body=row.body.result();
        boolean clean=sourceClean(row),cacheClean=file==null||file.fileCleanupConfirmed;long received=file==null?0:file.bytes;
        if(file!=null&&file.completed()){publish(row,"completed",null,file.bytes,file.sha256,true,false,true,null);return;}
        String fileError=file==null?null:file.code;
        String error=row.cancelled?"cancelled":row.error!=null?row.error:fileError!=null&&!fileError.equals("download_source_failed")?fileError:body!=null&&!body.ok?body.code:fileError;
        String origin=error==null?null:row.cancelled?"user":row.failureSource!=null?row.failureSource:"download_timeout".equals(fileError)?"timeout":
            fileError!=null&&!fileError.equals("download_source_failed")&&!fileError.equals("download_cancelled")?"cache":"native_protocol";
        boolean stopped=body!=null&&cacheClean;
        if(stopped){publish(row,row.cancelled&&clean?"cancelled":"failed",error==null?"download_source_failed":error,received,null,false,cacheClean,clean,
            origin==null?"native_protocol":origin);return;}
        publish(row,error!=null||row.cancelled?"cancelling":row.eof?"verifying":"receiving",error,received,null,false,cacheClean,clean,
            origin);
    }
    private void advance(Row row,MdoBrowserCdpBinding.Network network)throws IOException {
        synchronized(gate){
            if(closed||row.retired)return;
            if(row.releasing){summarize(row);return;}
            summarize(row);if(!row.snapshot.terminal()&&!row.origin.current()&&!sourceCompleted(row))revokeFrame(row);
            MdoBrowserCdpSession.OfferResult offer=row.offer.result();if(offer==null)return;
            if(!offer.accepted){summarize(row);return;}
            row.body=offer.body;
            if(row.cancelled)row.body.cancel();
            if(row.ticket==null&&!row.cancelled&&row.body.result()!=null&&row.error==null){row.error=row.body.result().code==null?"download_source_already_consumed":row.body.result().code;row.failureSource="native_protocol";}
            if(row.ticket==null&&!row.cancelled&&row.error==null)try{row.ticket=files.begin(row.body);}
                catch(IOException failure){row.error=safe(failure);row.failureSource="cache";row.body.cancel();}
            if(row.ticket!=null&&!row.cancelled&&row.error==null&&!row.eof&&row.body.result()==null){
                if(row.reading&&files.writable(row.ticket)){
                    MdoBrowserCdpSession.BodyChunk chunk=row.response==null?network.takeBodyChunk(row.body):row.response.takeChunk(row.body);
                    if(chunk!=null){row.reading=false;boolean handed=false;try{handed=files.offer(row.ticket,chunk);if(handed)row.eof=chunk.eof;}
                        finally{if(!handed){chunk.close();row.body.cancel();}}
                    }
                }
                if(!row.reading&&!row.eof&&row.body.ready()&&files.writable(row.ticket)){if(row.response==null)network.requestBodyRead(row.body);else row.response.requestRead(row.body);row.reading=true;}
            }
            summarize(row);
        }
    }
    private void revokeFrame(Row row){
        if(row.error==null&&!row.cancelled){row.error="download_frame_changed";row.failureSource="native_frame";}
        row.offer.cancel();if(row.ticket!=null)try{files.cancel(row.ticket);}catch(IOException ignored){}
    }
    private static boolean sourceCompleted(Row row){
        MdoBrowserCdpSession.OfferResult offer=row.offer.result();
        MdoBrowserCdpSession.BodyResult body=offer==null||offer.body==null?null:offer.body.result();
        return body!=null&&body.ok&&body.eof&&body.streamCloseConfirmed&&body.requestAbortConfirmed;
    }
    private void turn(Connection connection,MdoBrowserCdpBinding.Network network)throws IOException {
        synchronized(gate){
            protocol(connection,network);
            for(int i=0;i<rows.length;i++){
                Row row=rows[i];if(row==null)continue;
                if(row.connection!=connection){summarize(row);continue;}
                try{advance(row,network);}catch(IOException failure){
                    row.error=safe(failure);row.failureSource="native_protocol";row.offer.cancel();if(row.ticket!=null)files.cancel(row.ticket);
                }
            }
        }
    }
    /** Same native controller/UI policy/file queue, fed by an exact original
     * popup response. Existing rows use their frozen response after epoch loss;
     * a replacement document cannot select or read their body through its IDs. */
    private boolean attachedTurn(Connection connection,MdoBrowserCdpPopupBroker.Handoff physical,AttachedSource document,int timeout)throws IOException {
        synchronized(gate){
            protocol(connection,physical);if(timeout<10||timeout>5000)throw new IOException("cdp_arguments_invalid");
            for(Row row:rows)if(row!=null&&row.connection==connection)try{advance(row,null);}catch(IOException error){row.error=safe(error);row.failureSource="native_protocol";row.offer.cancel();if(row.ticket!=null)files.cancel(row.ticket);}
        }
        if(document==null)return false;MdoBrowserCdpSession.AttachedResponse response=null;
        try{
            response=document.next();if(response==null)return false;boolean stop;synchronized(gate){stop=quiescing;}
            // ACK waits must never hold the UI snapshot/decision monitor.
            if(stop||!response.metadata().kind.equals("candidate")){if(!stop&&response.metadata().kind.equals("pass"))response.pass(timeout);else response.abort(timeout);return true;}
            if(!retainAttached(connection,physical,document,response))response.abort(timeout);return true;
        }catch(IOException error){
            String code=error.getMessage();boolean retired=java.util.Arrays.asList("cdp_binding_owner_changed","cdp_popup_document_retired","cdp_frame_retired","cdp_frame_root_changed","download_frame_unproven","cdp_attach_response_admission_required","cdp_popup_response_admission_required").contains(code)||"cdp_attach_owner_changed".equals(code)&&physical.current();
            if(!retired)throw error;
            // Normal epoch retirement is not corruption of the shared physical
            // wire. An already selected original event can still be aborted.
            if(response!=null)response.abort(timeout);return response!=null;
        }
    }
    private boolean retainAttached(Connection connection,MdoBrowserCdpPopupBroker.Handoff physical,AttachedSource document,MdoBrowserCdpSession.AttachedResponse response)throws IOException {
        synchronized(gate){
            protocol(connection,physical);
            int slot=0;while(slot<rows.length&&rows[slot]!=null)slot++;
            if(slot==rows.length||!budget.acquire()){failure="download_receipt_limit";return false;}
            MdoBrowserCdpSession.Offer offer=null;boolean published=false;
            try{
                offer=document.retain(response);MdoBrowserCdpNetworkFrames.Origin source=offer.sourceEpoch();Row row=new Row(++serial,offer,source,connection,response);
                if(!source.current()){revokeFrame(row);publish(row,"cancelling",row.error,0,null,false,true,false,row.failureSource);}else publish(row,"pending",null,0,null,false,true,false,null);
                rows[slot]=row;published=true;
            }catch(IOException error){
                if(!"cdp_body_limit".equals(error.getMessage()))throw error;failure="download_pending_limit";
            }finally{if(!published){budget.release();if(offer!=null){offer.cancel();offer.retireRequestAssociation();}}}
            return published;
        }
    }
    String failure(){return failure;}
    /** Observe original stream/abort receipts before discarding UI rows.
     * Target detach or native destruction cannot manufacture an I/O close. */
    boolean sourcesClean(){synchronized(gate){if(sourceCleanupUnconfirmed)return false;for(Row row:rows)if(row!=null&&!sourceClean(row))return false;return true;}}
    /** User takeover cancels unfinished original sources without discarding
     * complete source bytes that are still being independently verified. */
    void quiesce(){
        synchronized(gate){if(closed||disconnected||quiescing)return;quiescing=true;
            for(Row row:rows)if(row!=null){summarize(row);if(row.snapshot.terminal()||sourceCompleted(row))continue;
                boolean user=row.snapshot.error==null||"cancelled".equals(row.snapshot.error);
                if(user)row.cancelled=true;row.offer.cancel();if(row.ticket!=null)try{files.cancel(row.ticket);}catch(IOException ignored){}
                // Quiescing must not replace a known native/cache fault with
                // a newer user decision while its cleanup is still pending.
                if(user)publish(row,"cancelling","cancelled",row.snapshot.received,null,false,row.snapshot.cacheCleanupConfirmed,false,"user");
                else summarize(row);}
        }
    }
    private void refresh(Row row){synchronized(gate){summarize(row);}}
    boolean pending(){synchronized(gate){if(failure!=null)return true;for(Row row:rows)if(row!=null){summarize(row);if(!row.retired&&!row.snapshot.terminal())return true;}return false;}}
    int retiringFiles(){
        synchronized(gate){int count=0;for(int i=0;i<retiring.length;i++)if(retiring[i]!=null){
            MdoBrowserDownloadQueue.Snapshot state=retiring[i].snapshot();
            if(state.fileCleanupConfirmed&&(state.state.equals("released")||state.state.equals("closed")))retiring[i]=null;else count++;
        }return count;}
    }
    private void disconnect(Connection connection,boolean clean){
        synchronized(gate){if(closed||connection!=current)return;connection.cleanupConfirmed|=clean;
            if(disconnected)return;disconnected=true;failure="download_connection_closed";
            for(Row row:rows)if(row!=null&&row.connection==connection){
                summarize(row);
                if(row.error==null&&row.snapshot.error!=null&&("cache".equals(row.snapshot.failureSource)||"timeout".equals(row.snapshot.failureSource)||"native_frame".equals(row.snapshot.failureSource)||"policy".equals(row.snapshot.failureSource))){
                    row.error=row.snapshot.error;row.failureSource=row.snapshot.failureSource;
                }
                MdoBrowserCdpSession.OfferResult offer=row.offer.result();
                MdoBrowserCdpSession.BodyResult body=offer==null||offer.body==null?null:offer.body.result();
                // Original bytes/cleanup can already be complete while the
                // independent file reread is still running. Connection loss
                // does not revoke that successful source or replay the request.
                if(body==null||!body.ok||!body.streamCloseConfirmed||!body.requestAbortConfirmed){
                    if(row.error==null&&!row.cancelled){String cause=body!=null?body.code:offer!=null?offer.code:null;
                        row.error=cause==null?"download_connection_closed":cause;row.failureSource="native_protocol";}
                    row.offer.cancel();if(row.ticket!=null)try{files.cancel(row.ticket);}catch(IOException ignored){}
                }
                summarize(row);
            }
        }
    }
    @Override public void close(){
        synchronized(gate){if(closed)return;closed=true;
            int slot=0;for(Row row:rows)if(row!=null){row.offer.cancel();if(row.ticket!=null){retiring[slot++]=row.ticket;try{files.release(row.ticket);}catch(IOException ignored){}}
                if(!sourceClean(row))sourceCleanupUnconfirmed=true;
                publish(row,"closed","download_owner_closed",row.snapshot.received,null,false,row.ticket==null,sourceClean(row),"owner");row.releaseDone=true;retire(row);}
            original=null;worker=null;
        }
    }
}
