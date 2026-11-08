package org.xserver.android;

import java.io.IOException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.UUID;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Physical-target network ancestry across document navigations. This ledger
 * grants response provenance only, never an execution world or input route. */
final class MdoBrowserCdpNetworkFrames implements MdoBrowserCdpSession.Events,AutoCloseable {
    /** Data-only receipt issued by this exact closed ledger, not an error-text match. */
    static final class Closed extends IOException {
        private final Object original;
        private Closed(Object original){super("download_frame_owner_closed");this.original=original;}
        boolean issuedBy(MdoBrowserCdpNetworkFrames owner){return owner!=null&&original==owner.authority;}
    }
    private static final int LIMIT=32,DEPTH=32,LIFETIME=4096;
    /** Private bounded root commit history, not a public wire-ID lookup. */
    private static final class Start {
        final String loader,type;final long revision;final byte[] destination;final Scope scope=new Scope();
        Start(String loader,String type,long revision,byte[] destination){this.loader=loader;this.type=type;this.revision=revision;this.destination=destination;}
        void revoke(){scope.active=false;if(destination!=null)java.util.Arrays.fill(destination,(byte)0);}
    }
    static final class Traversal {
        final String loader;final long revision;final Origin origin;final boolean sameDocument;
        private Traversal(Commit commit){loader=commit.loader;revision=commit.revision;sameDocument=commit.destination!=null;origin=commit.position.with(commit.started.scope);}
    }
    private static final class Commit {
        final String loader;final long revision;final Origin origin,position;final byte[] destination;final Start started;final boolean restored;
        Commit(String loader,long revision,Origin origin,Origin position,byte[] destination,Start started,boolean restored){
            this.loader=loader;this.revision=revision;this.origin=origin;this.position=position;this.destination=destination;this.started=started;this.restored=restored;
        }
    }
    private Start started;
    private final Commit[] commits=new Commit[LIMIT];private int commitCursor;
    private static final class Scope {volatile boolean active=true;}
    static final class Origin {
        final String frame,document;final boolean root;private final Scope[] ancestry;
        private final java.lang.ref.WeakReference<MdoBrowserCdpMarker.Lease> documentOwner;private final java.util.concurrent.atomic.AtomicBoolean[] documentScopes;
        private Origin(Frame node,Scope[] ancestry,boolean root){frame=node.alias;document=node.document;this.ancestry=ancestry;this.root=root;documentOwner=null;documentScopes=null;}
        private Origin(Origin original,Scope[] ancestry){frame=original.frame;document=original.document;root=original.root;this.ancestry=ancestry;documentOwner=original.documentOwner;documentScopes=original.documentScopes;}
        private Origin(Origin original,Scope blocked){frame=original.frame;document="";root=original.root;ancestry=new Scope[]{blocked};documentOwner=original.documentOwner;documentScopes=original.documentScopes;}
        private Origin(String frame,String document,boolean root,MdoBrowserCdpMarker.Lease owner,java.util.concurrent.atomic.AtomicBoolean[] scopes){this.frame=frame;this.document=document;this.root=root;ancestry=new Scope[0];documentOwner=new java.lang.ref.WeakReference<>(owner);documentScopes=scopes.clone();}
        private Origin with(Scope scope){Scope[] extended=java.util.Arrays.copyOf(ancestry,ancestry.length+1);extended[extended.length-1]=scope;return new Origin(this,extended);}
        boolean current(){if(documentOwner!=null){MdoBrowserCdpMarker.Lease owner=documentOwner.get();if(owner==null||!owner.current())return false;for(java.util.concurrent.atomic.AtomicBoolean scope:documentScopes)if(!scope.get())return false;}for(Scope scope:ancestry)if(!scope.active)return false;return true;}
    }
    /** Only the native attached-frame controller issues these aliases and
     * atomic epoch fences. UI reads retain no frame graph or worker callback. */
    static Origin attached(String frame,String document,boolean root,MdoBrowserCdpMarker.Lease owner,java.util.concurrent.atomic.AtomicBoolean[] scopes)throws IOException {
        if(frame==null||document==null||owner==null||scopes==null||scopes.length<2||scopes.length>34)throw new IOException("download_frame_unproven");
        for(java.util.concurrent.atomic.AtomicBoolean scope:scopes)if(scope==null)throw new IOException("download_frame_unproven");return new Origin(frame,document,root,owner,scopes);
    }
    /** Known physical frame, unproven document. Never an executable/new epoch. */
    static Origin unproven(Origin frame){Scope blocked=new Scope();blocked.active=false;return new Origin(frame,blocked);}
    private static final class Frame {
        final String raw,parent,alias="nf-"+UUID.randomUUID();String loader,document="nd-"+UUID.randomUUID();Scope scope=new Scope(),position=new Scope();boolean swapped;byte[] location;
        Frame(String raw,String parent,String loader){this.raw=raw;this.parent=parent;this.loader=loader;}
        void location(byte[] digest){if(location!=null)java.util.Arrays.fill(location,(byte)0);location=digest;}
        void revoke(){scope.active=false;position.active=false;location(null);}
        void move(){position.active=false;position=new Scope();}
        void navigate(String loader){revoke();scope=new Scope();position=new Scope();document="nd-"+UUID.randomUUID();this.loader=loader;swapped=false;}
    }
    private final Thread worker=Thread.currentThread();private final String root,initialLoader;
    private final Map<String,Frame> frames=new LinkedHashMap<>();private final Scope authority=new Scope();private Scope quiet=new Scope();private boolean ready,rootPending;private volatile boolean closed;private long revision;private int issued;
    MdoBrowserCdpNetworkFrames(String root,String loader)throws IOException {
        if(!identity(root)||!loader(loader))throw fail("download_frame_metadata_invalid");
        this.root=root;initialLoader=loader;frames.put(root,new Frame(root,null,loader));issued=1;
    }
    private static IOException fail(String code){return new IOException(code);}
    static boolean identity(Object value){
        if(!(value instanceof String)||((String)value).isEmpty()||((String)value).length()>128)return false;
        for(int i=0;i<((String)value).length();i++){char c=((String)value).charAt(i);
            if(!(c>='A'&&c<='Z'||c>='a'&&c<='z'||c>='0'&&c<='9'||"_.:-".indexOf(c)>=0))return false;}
        return true;
    }
    private static boolean loader(Object value){return value instanceof String&&(((String)value).isEmpty()||identity(value));}
    private static String id(JSONObject value,String key)throws IOException {Object raw=value.opt(key);if(!identity(raw))throw fail("download_frame_metadata_invalid");return(String)raw;}
    private void current()throws IOException {if(Thread.currentThread()!=worker)throw fail("cdp_wrong_worker");if(closed)throw new Closed(authority);}
    long revision()throws IOException {current();return revision;}
    String rootLoader()throws IOException {origin(root);return frames.get(root).loader;}
    /** A fresh document read needs a settled original root position. The
     * immutable fence also expires when another navigation starts. */
    Origin documentOrigin()throws IOException {
        current();if(rootPending)throw fail("download_frame_unproven");
        return origin(root).with(frames.get(root).position).with(quiet);
    }
    private static byte[] location(JSONObject value){
        if(value.has("unreachableUrl")&&!"".equals(value.opt("unreachableUrl")))return null;
        try{return MdoBrowserCdpSession.navigationFrameDigest(value);}catch(IOException unsupported){return null;}
    }
    /** Positive original-position evidence, not URL equality after the effect. */
    Origin sameLocationOrigin(byte[] destination)throws IOException {
        current();Frame node=frames.get(root);
        if(!ready||rootPending||destination==null||node.location==null||!java.security.MessageDigest.isEqual(node.location,destination))return null;
        return origin(root).with(node.position).with(quiet);
    }
    private void retain(Commit commit){
        Commit old=commits[commitCursor];if(old!=null&&old.destination!=null)java.util.Arrays.fill(old.destination,(byte)0);
        commits[commitCursor]=commit;commitCursor=(commitCursor+1)%LIMIT;
    }
    private void collect(JSONObject tree,String parent,int depth,Map<String,Frame> found)throws IOException,JSONException {
        if(depth>=DEPTH||found.size()>=LIMIT)throw fail("download_frame_limit");
        JSONObject value=tree.getJSONObject("frame");String raw=id(value,"id");Object load=value.opt("loaderId");
        if(!loader(load)||found.containsKey(raw))throw fail("download_frame_metadata_invalid");
        if(value.has("parentId")&&!java.util.Objects.equals(parent,id(value,"parentId")))throw fail("download_frame_parent_unproven");
        if(parent==null&&(!root.equals(raw)||!initialLoader.equals(load)))throw fail("download_frame_root_changed");
        Frame node=new Frame(raw,parent,(String)load);if(parent==null)node.location(location(value));found.put(raw,node);
        JSONArray children=tree.optJSONArray("childFrames");if(tree.has("childFrames")&&children==null)throw fail("download_frame_metadata_invalid");
        if(children!=null){if(children.length()>LIMIT)throw fail("download_frame_limit");for(int i=0;i<children.length();i++)collect(children.getJSONObject(i),raw,depth+1,found);}
    }
    void initialize(JSONObject tree,long before)throws IOException {
        current();if(ready)throw fail("download_frame_bootstrap_replayed");if(before!=revision)throw fail("download_frame_bootstrap_changed");
        Map<String,Frame> found=new LinkedHashMap<>();
        try{collect(tree,null,0,found);}catch(JSONException failure){throw fail("download_frame_metadata_invalid");}
        if(issued> LIFETIME-found.size())throw fail("download_frame_lifetime_limit");
        for(Frame frame:frames.values())frame.revoke();frames.clear();frames.putAll(found);issued+=found.size();ready=true;revision++;
    }
    private void descendants(String raw,boolean include){
        ArrayList<String> keys=new ArrayList<>();keys.add(raw);
        for(int i=0;i<keys.size();i++)for(Frame node:frames.values())if(java.util.Objects.equals(node.parent,keys.get(i))&&!keys.contains(node.raw))keys.add(node.raw);
        for(String key:keys)if(include||!key.equals(raw)){Frame removed=frames.remove(key);if(removed!=null)removed.revoke();}
    }
    private Frame attach(String raw,String parent,String load)throws IOException {
        if(raw.equals(root)||raw.equals(parent))throw fail("download_frame_parent_unproven");
        Frame ancestor=frames.get(parent);if(ancestor==null||ancestor.swapped)throw fail("download_frame_parent_unproven");
        int depth=1;for(Frame at=ancestor;at!=null;at=frames.get(at.parent)){
            if(at.raw.equals(raw)||++depth>DEPTH)throw fail("download_frame_parent_unproven");
        }
        Frame existing=frames.get(raw);
        if(existing!=null){if(!parent.equals(existing.parent))throw fail("download_frame_parent_unproven");return existing;}
        if(frames.size()>=LIMIT)throw fail("download_frame_limit");if(issued>=LIFETIME)throw fail("download_frame_lifetime_limit");
        Frame value=new Frame(raw,parent,load);frames.put(raw,value);issued++;return value;
    }
    @Override public void event(String method,String child,JSONObject params)throws IOException {
        current();if(!method.equals("Page.frameAttached")&&!method.equals("Page.frameNavigated")&&!method.equals("Page.documentOpened")&&
            !method.equals("Page.frameStartedNavigating")&&!method.equals("Page.navigatedWithinDocument")&&!method.equals("Page.frameDetached")&&!method.equals("Page.frameSubtreeWillBeDetached"))return;
        try{
            if(child!=null)throw fail("download_frame_session_unproven");
            if(method.equals("Page.frameStartedNavigating")){
                String raw=id(params,"frameId");if(!raw.equals(root))return;
                Object url=params.opt("url"),load=params.opt("loaderId"),type=params.opt("navigationType");
                if(!(url instanceof String)||((String)url).length()>8192||!loader(load)||!(type instanceof String)||
                   !java.util.Arrays.asList("reload","reloadBypassingCache","restore","restoreWithPost","historySameDocument","historyDifferentDocument","sameDocument","differentDocument").contains(type))throw fail("download_frame_metadata_invalid");
                quiet.active=false;quiet=new Scope();rootPending=true;
                if(started!=null)started.revoke();revision++;
                started=new Start((String)load,(String)type,revision,MdoBrowserCdpSession.navigationUrl((String)url)?MdoBrowserCdpSession.navigationDigest((String)url):null);return;
            }
            if(method.equals("Page.navigatedWithinDocument")){
                String raw=id(params,"frameId");if(!raw.equals(root))return;
                Object url=params.opt("url"),type=params.opt("navigationType");
                if(!(url instanceof String)||((String)url).length()>8192||params.has("navigationType")&&
                   !("fragment".equals(type)||"historyApi".equals(type)||"other".equals(type)))throw fail("download_frame_metadata_invalid");
                Frame node=frames.get(root);node.move();revision++;rootPending=false;node.location(null);
                if(ready&&MdoBrowserCdpSession.navigationUrl((String)url)){
                    Origin position=origin(root).with(node.position);
                    byte[] digest=MdoBrowserCdpSession.navigationDigest((String)url);node.location(java.util.Arrays.copyOf(digest,digest.length));
                    retain(new Commit(node.loader,revision,position,position,digest,started,false));
                }return;
            }
            if(method.equals("Page.frameAttached")){String raw=id(params,"frameId"),parent=id(params,"parentFrameId");attach(raw,parent,"");revision++;return;}
            if(method.equals("Page.frameNavigated")||method.equals("Page.documentOpened")){
                JSONObject value=params.getJSONObject("frame");String raw=id(value,"id");Object load=value.opt("loaderId");
                if(!loader(load))throw fail("download_frame_metadata_invalid");Frame node;
                if(raw.equals(root)){if(value.has("parentId"))throw fail("download_frame_root_changed");node=frames.get(root);}
                else{Frame previous=frames.get(raw);String parent=value.has("parentId")?id(value,"parentId"):previous==null?null:previous.parent;
                    if(parent==null)throw fail("download_frame_parent_unproven");node=attach(raw,parent,(String)load);}
                Object type=params.opt("type");if(method.equals("Page.frameNavigated")&&params.has("type")&&!"Navigation".equals(type)&&!"BackForwardCacheRestore".equals(type))throw fail("download_frame_metadata_invalid");
                descendants(raw,false);node.navigate((String)load);revision++;
                if(raw.equals(root)){rootPending=false;node.location(location(value));}
                if(raw.equals(root)&&method.equals("Page.documentOpened")&&started!=null){started.revoke();started=null;}
                if(ready&&raw.equals(root)&&method.equals("Page.frameNavigated")){
                    Origin document=origin(root);retain(new Commit((String)load,revision,document,document.with(node.position),null,started,"BackForwardCacheRestore".equals(type)));
                }return;
            }
            String raw=id(params,"frameId");if(raw.equals(root))throw fail("download_frame_root_changed");Frame node=frames.get(raw);if(node==null)return;
            String reason=method.equals("Page.frameSubtreeWillBeDetached")?"remove":params.optString("reason","remove");
            if(reason.equals("remove"))descendants(raw,true);
            else if(reason.equals("swap")){descendants(raw,false);node.revoke();node.swapped=true;}
            else throw fail("download_frame_metadata_invalid");revision++;
        }catch(JSONException failure){close();throw new MdoBrowserCdpSession.EventFailure("download_frame_metadata_invalid");}
        catch(IOException failure){close();if(MdoBrowserCdpSession.EventFailure.allowed(failure.getMessage()))throw new MdoBrowserCdpSession.EventFailure(failure.getMessage());throw failure;}
        catch(RuntimeException|Error failure){close();throw failure;}
    }
    Origin origin(String raw)throws IOException {
        current();if(!ready||raw==null)throw fail("download_frame_unproven");Frame selected=frames.get(raw);
        if(selected==null)throw fail("download_frame_unproven");ArrayList<Scope> chain=new ArrayList<>();chain.add(authority);
        for(Frame node=selected;node!=null&&chain.size()<=DEPTH;node=frames.get(node.parent)){
            if(node.swapped||!node.scope.active)throw fail("download_frame_unproven");chain.add(node.scope);
            if(node.raw.equals(root))return new Origin(selected,chain.toArray(new Scope[0]),selected.raw.equals(root));
        }throw fail("download_frame_parent_unproven");
    }
    Origin navigationOrigin(MdoBrowserCdpSession.Navigation navigation,long after)throws IOException {
        current();if(navigation==null||!ready)return null;
        for(int i=0;i<LIMIT;i++){
            Commit commit=commits[(commitCursor+LIMIT-1-i)%LIMIT];
            if(commit!=null&&commit.destination==null&&commit.revision>after&&navigation.matchesCommittedLoader(root,commit.loader))return commit.origin;
        }return null;
    }
    Origin sameDocumentOrigin(MdoBrowserCdpSession.Navigation navigation,long after,String loader,byte[] destination,Origin original)throws IOException {
        current();if(navigation==null||!ready||original==null||!original.current()||!navigation.matchesSameDocument(root))return null;
        for(int i=0;i<LIMIT;i++){
            Commit commit=commits[(commitCursor+LIMIT-1-i)%LIMIT];
            if(commit!=null&&commit.destination!=null&&commit.revision>after&&commit.loader.equals(loader)&&
               java.security.MessageDigest.isEqual(commit.destination,destination))return commit.origin;
        }return null;
    }
    Traversal traversal(MdoBrowserCdpSession.Navigation navigation,long after,String action,String previousLoader,Origin previous,byte[] destination)throws IOException {
        current();MdoBrowserCdpSession.NavigationResult receipt=navigation==null?null:navigation.result();
        if(!ready||navigation==null||!navigation.receiptOnly||receipt==null||!receipt.acknowledged||!receipt.ok)return null;
        for(int i=0;i<LIMIT;i++){
            Commit commit=commits[(commitCursor+LIMIT-1-i)%LIMIT];Start start=commit==null?null:commit.started;
            if(start==null||!start.scope.active||start.destination==null||commit.revision<=after||start.revision<=after||start.revision>=commit.revision||
               !commit.position.current()||!java.security.MessageDigest.isEqual(start.destination,destination))continue;
            boolean same=commit.destination!=null;
            if(action.equals("reload")){
                if(same||commit.restored||!start.type.equals("reload")||!commit.loader.equals(start.loader)||commit.loader.equals(previousLoader))continue;
            }else if(action.equals("back")||action.equals("forward")){
                if(same){
                    if(!start.type.equals("historySameDocument")||previous==null||!previous.current()||!commit.loader.equals(previousLoader)||!java.security.MessageDigest.isEqual(commit.destination,destination))continue;
                }else if(!start.type.equals("historyDifferentDocument")||!commit.restored&&!commit.loader.equals(start.loader))continue;
            }else return null;
            return new Traversal(commit);
        }return null;
    }
    /** Called after the physical native worker is drained, or on that worker.
     * Revoke immutable receipts even when the UI requests target teardown. */
    @Override public void close(){authority.active=false;closed=true;}
}
