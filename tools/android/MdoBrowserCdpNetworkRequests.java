package org.xserver.android;

import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicReference;
import org.json.JSONObject;

/** Private root-Document identity ledger. No URL/header/body or wire selector
 * crosses this boundary. Correlation failure disables correlation, not existing
 * transfer cleanup. Captured evidence grants no stream or permission. */
final class MdoBrowserCdpNetworkRequests implements MdoBrowserCdpSession.Events,AutoCloseable {
    /** Only this ledger's closed admission check can issue its original receipt. */
    static final class Closed extends IOException {
        private final Object original;
        private Closed(Object original){super("cdp_request_owner_closed");this.original=original;}
        boolean issuedBy(MdoBrowserCdpNetworkRequests owner){return owner!=null&&original==owner.owner;}
    }
    private static final int LIMIT=4096,REDIRECTS=32,COMMANDS=32;
    static final class Receipt {
        private final Object owner;final String loader;final long born;private volatile boolean valid=true;private int redirects;
        Receipt(Object owner,String loader,long born){this.owner=owner;this.loader=loader;this.born=born;}
    }
    private static final class Command {
        final long after;final MdoBrowserCdpSession.Navigation navigation;final String reference;final MdoBrowserCdpNetworkFrames.Origin epoch;
        Command(long after,MdoBrowserCdpSession.Navigation navigation,String reference,MdoBrowserCdpNetworkFrames.Origin epoch){this.after=after;this.navigation=navigation;this.reference=reference;this.epoch=epoch;}
        boolean eligible(){MdoBrowserCdpSession.NavigationResult result=navigation.result();return result==null||result.acknowledged&&result.ok&&result.downloadKnown&&result.download&&result.loaderPresent;}
    }
    /** Only immutable native evidence and the original typed ACK survive.
     * No binding, WebView, UI callback or current-navigation lookup is retained. */
    static final class Link {
        private volatile Receipt request;private volatile boolean retired;private final Command[] candidates;private final String frame,network;private final Object owner;
        private final AtomicReference<String> proved=new AtomicReference<>();
        private Link(Object owner,String network,Receipt request,Command[] candidates,String frame){this.owner=owner;this.network=network;this.request=request;this.candidates=candidates;this.frame=frame;}
        private void resolve(Receipt original){if(!retired&&original.owner==owner&&original.valid)request=original;}
        private boolean awaiting(){if(retired||request!=null)return false;for(Command command:candidates)if(command.eligible())return true;return false;}
        /** May run on a receipt/UI thread. Only the owning worker prunes slots. */
        void retire(){retired=true;}
        String reference(){
            String original=proved.get();if(original!=null)return original;
            Receipt source=request;if(source==null||!source.valid)return null;String matched=null;
            for(Command command:candidates){
                if(source.born<=command.after)continue;
                if(command.navigation.result()==null)return null;
                if(command.navigation.matchesDownloadLoader(frame,source.loader)){if(matched!=null)return null;matched=command.reference;}
            }
            if(matched!=null&&source.valid)proved.compareAndSet(null,matched);
            return proved.get();
        }
        /** Freeze only a common original document among still possible commands.
         * Pending ACKs cannot be resolved by borrowing the current page. */
        MdoBrowserCdpNetworkFrames.Origin epoch()throws IOException {
            Receipt source=request;MdoBrowserCdpNetworkFrames.Origin epoch=null;
            for(Command command:candidates){
                if(!command.eligible())continue;
                if(source!=null&&source.valid){
                    if(source.born<=command.after)continue;
                    if(command.navigation.result()!=null&&!command.navigation.matchesDownloadLoader(frame,source.loader))continue;
                }
                if(epoch!=null&&(!epoch.frame.equals(command.epoch.frame)||!epoch.document.equals(command.epoch.document)))throw new IOException("download_frame_unproven");
                epoch=command.epoch;
            }return epoch;
        }
    }
    private final Thread worker=Thread.currentThread();private final Object owner=new Object();private final String root;
    private final Map<String,Receipt> requests=new LinkedHashMap<>(),loaders=new LinkedHashMap<>();
    private final Link[] pending=new Link[32];
    private final Command[] commands=new Command[COMMANDS];private int commandCount;
    private long serial;private volatile boolean closed;private volatile String fault;
    MdoBrowserCdpNetworkRequests(String root){if(!MdoBrowserCdpNetworkFrames.identity(root))throw new IllegalArgumentException("Invalid original root");this.root=root;}
    private void current()throws IOException{if(Thread.currentThread()!=worker)throw new IOException("cdp_wrong_worker");if(closed)throw new Closed(owner);}
    long revision()throws IOException{current();return serial;}
    String failure(){return fault;}
    private void disable(String code){fault=code;for(Receipt receipt:requests.values())receipt.valid=false;requests.clear();loaders.clear();java.util.Arrays.fill(pending,null);java.util.Arrays.fill(commands,null);commandCount=0;}
    @Override public void event(String method,String child,JSONObject params)throws IOException{
        current();if(fault!=null||!method.equals("Network.requestWillBeSent")||child!=null||!"Document".equals(params.opt("type"))||!root.equals(params.opt("frameId")))return;
        Object id=params.opt("requestId"),loader=params.opt("loaderId");
        if(!MdoBrowserCdpNetworkFrames.identity(id)||!MdoBrowserCdpNetworkFrames.identity(loader)){disable("cdp_request_metadata_invalid");return;}
        Receipt previous=requests.get((String)id);boolean redirected=params.has("redirectResponse");
        if(redirected){
            Object raw=params.opt("redirectResponse");Object status=raw instanceof JSONObject?((JSONObject)raw).opt("status"):null;
            if(!(status instanceof Number)||!java.util.Arrays.asList(301d,302d,303d,307d,308d).contains(((Number)status).doubleValue())){disable("cdp_request_redirect_invalid");return;}
        }
        if(previous!=null){
            if(!redirected||!previous.loader.equals(loader)||++previous.redirects>REDIRECTS){disable("cdp_request_identity_rebound");return;}
            serial++;return;
        }
        if(loaders.containsKey((String)loader)){disable("cdp_request_loader_rebound");return;}
        if(requests.size()>=LIMIT){disable("cdp_request_identity_limit");return;}
        Receipt receipt=new Receipt(owner,(String)loader,++serial);requests.put((String)id,receipt);loaders.put((String)loader,receipt);
        for(int i=0;i<pending.length;i++)if(pending[i]!=null){
            if(!pending[i].awaiting())pending[i]=null;
            else if(pending[i].network.equals(id)){pending[i].resolve(receipt);pending[i]=null;}
        }
    }
    void admit(long after,MdoBrowserCdpSession.Navigation navigation,String reference,MdoBrowserCdpNetworkFrames.Origin epoch)throws IOException{
        current();if(fault!=null)return;
        if(after<0||after>serial||navigation==null||navigation.receiptOnly||!MdoBrowserCdpNetworkFrames.identity(reference)||epoch==null||!epoch.root||epoch.document.isEmpty())throw new IOException("cdp_request_command_invalid");
        for(int i=0;i<commandCount;i++)if(commands[i].navigation==navigation)throw new IOException("cdp_request_command_replayed");
        Command admitted=new Command(after,navigation,reference,epoch);
        int kept=0;for(int i=0;i<commandCount;i++){
            Command command=commands[i];
            if(command.eligible())commands[kept++]=command;
        }
        java.util.Arrays.fill(commands,kept,commandCount,null);commandCount=kept;
        if(commandCount==COMMANDS){System.arraycopy(commands,1,commands,0,COMMANDS-1);commandCount--;}
        commands[commandCount]=admitted;commandCount++;
    }
    Link link(String networkId,String frame)throws IOException{
        current();Receipt request=networkId==null?null:requests.get(networkId);
        if(fault!=null||!MdoBrowserCdpNetworkFrames.identity(networkId)||!root.equals(frame)||commandCount==0)return null;
        if(request!=null&&(request.owner!=owner||!request.valid))return null;
        Command[] candidates=java.util.Arrays.copyOf(commands,commandCount);
        if(request!=null){boolean eligible=false;for(Command command:candidates)if(command.eligible()&&request.born>command.after)eligible=true;if(!eligible)return null;return new Link(owner,networkId,request,candidates,root);}
        int slot=-1;for(int i=0;i<pending.length;i++){if(pending[i]!=null&&!pending[i].awaiting())pending[i]=null;if(pending[i]==null)slot=i;}
        if(slot<0){disable("cdp_request_pending_limit");return null;}
        Link link=new Link(owner,networkId,null,candidates,root);pending[slot]=link;return link;
    }
    /** Retained original facts remain inspectable after native teardown. */
    @Override public void close(){closed=true;if(Thread.currentThread()==worker||!worker.isAlive()){requests.clear();loaders.clear();java.util.Arrays.fill(pending,null);java.util.Arrays.fill(commands,null);commandCount=0;}}
}
