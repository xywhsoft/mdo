package org.xserver.android;

import java.util.function.BooleanSupplier;
import java.util.function.LongSupplier;
import java.util.UUID;

/** One physical profile owner plus an optional isolated document world. It does not
 * enable the product's unfinished input/capture/frame/transfer capabilities. */
final class MdoBrowserDocument implements AutoCloseable {
    static final int CHANNEL=1,BACKGROUND=2,MANUAL=4,DIALOG=8,NAVIGATION=16,POPUP=32,CHOOSER=64;
    static final String HELLO="mdo-browser-document-v1";
    static final String BOOTSTRAP="(()=>{if(self!==top||globalThis.__mdoBrowserDocumentReady)return;"+
        "Object.defineProperty(globalThis,'__mdoBrowserDocumentReady',{value:true});"+
        MdoBrowserWorld.LISTENER+".postMessage('"+HELLO+"')})()";
    private MdoBrowserWorld world;private MdoBrowserProfile physicalProfile;private final Object view;
    private final BooleanSupplier owned;
    private final LongSupplier epoch;
    private final Thread owner=Thread.currentThread();
    private MdoBrowserWorld.Peer candidate,active;
    private long activeEpoch,serial;
    private int blockers;
    private boolean loading=true,closed,started,initializing,completionRegistered,realmInstalled;private Runnable initialized;private BooleanSupplier startupAdmission;
    private String fault,nativeDocument;

    private MdoBrowserDocument(Object view,BooleanSupplier owned,LongSupplier epoch)throws MdoBrowserWorld.Failure {
        if(view==null||owned==null||epoch==null)throw new MdoBrowserWorld.Failure("world_arguments_invalid");this.view=view;this.owned=owned;this.epoch=epoch;
    }
    static MdoBrowserDocument prepare(Object view,BooleanSupplier owned,LongSupplier epoch)throws MdoBrowserWorld.Failure{return new MdoBrowserDocument(view,owned,epoch);}
    MdoBrowserDocument(Object view,ClassLoader loader,MdoBrowserWorld.Scheduler scheduler,BooleanSupplier owned,LongSupplier epoch,String profile)throws MdoBrowserWorld.Failure {
        this(view,loader,scheduler,owned,epoch,profile,new MdoBrowserWorld.Budget(16));
    }
    MdoBrowserDocument(Object view,ClassLoader loader,MdoBrowserWorld.Scheduler scheduler,BooleanSupplier owned,LongSupplier epoch,String profile,MdoBrowserWorld.Budget budget)throws MdoBrowserWorld.Failure {
        this(view,owned,epoch);initialize(loader,scheduler,profile,budget,()->true);
    }
    void whenInitialized(Runnable complete){thread();if(complete==null||completionRegistered)throw new IllegalStateException("Document initialization listener already registered");completionRegistered=true;if((started||closed)&&!initializing)complete.run();else initialized=complete;}
    boolean initializing(){thread();return initializing;}
    private void startup()throws MdoBrowserWorld.Failure {
        if(closed||!owned.getAsBoolean()||startupAdmission!=null&&!startupAdmission.getAsBoolean()||closed)throw new MdoBrowserWorld.Failure(closed?"world_closed":"frame_changed");
    }
    private void begin(BooleanSupplier admission)throws MdoBrowserWorld.Failure {
        if(started)throw new MdoBrowserWorld.Failure("world_install_order");started=true;initializing=true;startupAdmission=admission;
    }
    private void closeAfterFailure(Throwable failure){
        try{close();}catch(VirtualMachineError|ThreadDeath fatal){fatal.addSuppressed(failure);throw fatal;}catch(RuntimeException|Error cleanup){failure.addSuppressed(cleanup);}
    }
    private void end(Throwable failed){
        initializing=false;startupAdmission=null;Runnable complete=initialized;initialized=null;
        try{if(complete!=null)complete.run();if(!closed)adopt();}catch(VirtualMachineError|ThreadDeath fatal){if(failed!=null)fatal.addSuppressed(failed);throw fatal;}catch(RuntimeException|Error cleanup){if(failed!=null)failed.addSuppressed(cleanup);else throw cleanup;}
    }
    /** Confirm configuration before popup transport without creating a script
     * world. The resulting physical owner retains no opener/controller. */
    void initializeInheritedProfile(ClassLoader loader,MdoBrowserDocument original,Object opener,BooleanSupplier admission)throws MdoBrowserWorld.Failure {
        thread();if(loader==null||original==null||opener==null||admission==null)throw new MdoBrowserWorld.Failure("world_arguments_invalid");
        MdoBrowserProfile source=original.profileFor(opener);String name=MdoBrowserProfile.inheritedName(view,loader,source);begin(admission);Throwable failed=null;
        try{startup();physicalProfile=MdoBrowserProfile.open(view,loader);startup();physicalProfile.inherit(source,name);startup();fault="popup_world_unqualified";}
        catch(MdoBrowserWorld.Failure|RuntimeException|Error failure){failed=failure;closeAfterFailure(failure);throw failure;}
        finally{end(failed);}
    }
    private MdoBrowserProfile profileFor(Object exact)throws MdoBrowserWorld.Failure {
        thread();if(exact!=view||!profileAssigned())throw new MdoBrowserWorld.Failure("browser_profile_source_invalid");return physicalProfile;
    }
    void initialize(ClassLoader loader,MdoBrowserWorld.Scheduler scheduler,String profile,MdoBrowserWorld.Budget budget,BooleanSupplier admission)throws MdoBrowserWorld.Failure {
        thread();if(loader==null||scheduler==null||budget==null||admission==null)throw new MdoBrowserWorld.Failure("world_arguments_invalid");budget.used();begin(admission);Throwable failed=null;
        try{
            startup();
            if(profile!=null){physicalProfile=MdoBrowserProfile.open(view,loader);startup();physicalProfile.assign(profile);startup();physicalProfile.seal();}
            MdoBrowserWorld.Receiver receiver=new MdoBrowserWorld.Receiver(){
                public void message(MdoBrowserWorld.Peer peer,String text){receive(peer,text);}
                public void fault(String code){failed(code);}
            };
            try{world=physicalProfile==null?MdoBrowserWorld.open(view,loader,scheduler,receiver,budget):MdoBrowserWorld.borrow(view,loader,scheduler,receiver,budget,physicalProfile);}
            catch(MdoBrowserWorld.Failure failure){
                // Missing optional boundary isolation is not loss of a confirmed
                // physical profile. The original CDP adapter owns native worlds.
                if(physicalProfile!=null&&"isolated_world_unsupported".equals(failure.code)){fault=failure.code;startup();return;}throw failure;
            }
            startup();world.install(BOOTSTRAP);startup();realmInstalled=true;
        }catch(MdoBrowserWorld.Failure|RuntimeException|Error failure){failed=failure;closeAfterFailure(failure);throw failure;}
        finally{end(failed);}
    }
    private void thread(){if(Thread.currentThread()!=owner)throw new IllegalStateException("Document requires its owning UI thread");}
    private void receive(MdoBrowserWorld.Peer peer,String text){
        thread();if(closed||!owned.getAsBoolean())return;
        if(!HELLO.equals(text)){failed("world_message_invalid");return;}
        candidate=peer;if(active==null)adopt();
    }
    private void adopt(){
        if(!closed&&!initializing&&world!=null&&fault==null&&!loading&&blockers==0&&owned.getAsBoolean()&&world.current(candidate)){
            // A replacement peer may precede onPageStarted. A blocker release
            // must not adopt it and let the later start retire the new peer.
            if(active!=null&&active!=candidate)return;
            long next=epoch.getAsLong();
            String identity=active!=candidate||activeEpoch!=next||nativeDocument==null?"fd-"+UUID.randomUUID():nativeDocument;
            active=candidate;activeEpoch=next;nativeDocument=identity;
        }
    }
    private void failed(String code){
        thread();if(closed)return;fault=code;serial++;active=null;nativeDocument=null;
        if(world!=null)world.cancelPending(code);
    }
    void navigation(){
        thread();if(closed)return;loading=true;blockers&=~NAVIGATION;serial++;nativeDocument=null;
        MdoBrowserWorld.Peer previous=active;active=null;
        if(candidate==previous)candidate=null;
        // A new native peer may already have arrived. Retire the previously
        // admitted peer specifically, never an unrelated replacement peer.
        if(world!=null){world.invalidate(previous);world.cancelPending("frame_changed");}
    }
    /** Native commitment releases realm admission, not DOM-ready evidence. */
    void committed(){thread();if(!closed){loading=false;adopt();}}
    void loaded(){committed();}
    void block(int mask,boolean value){
        thread();if(closed)return;
        if(mask!=CHANNEL&&mask!=BACKGROUND&&mask!=MANUAL&&mask!=DIALOG&&mask!=NAVIGATION&&mask!=POPUP&&mask!=CHOOSER)throw new IllegalArgumentException("Invalid document blocker");
        int next=value?blockers|mask:blockers&~mask;
        if(next==blockers)return;blockers=next;
        if(value){serial++;if(world!=null)world.cancelPending(reason());}else adopt();
    }
    private String reason(){
        if(closed)return"world_closed";
        if(fault!=null)return fault;
        if(!owned.getAsBoolean())return"frame_changed";
        if((blockers&CHANNEL)!=0)return"main_document_changed";
        if((blockers&BACKGROUND)!=0)return"world_view_unavailable";
        if((blockers&MANUAL)!=0)return"user_taken_control";
        if((blockers&DIALOG)!=0)return"dialog_open";
        if((blockers&CHOOSER)!=0)return"file_chooser_open";
        if((blockers&POPUP)!=0)return"popup_pending";
        if((blockers&NAVIGATION)!=0)return"navigation_pending";
        if(initializing||world==null||loading||nativeDocument==null||!world.current(active)||activeEpoch!=epoch.getAsLong())return"world_not_ready";
        return null;
    }
    boolean worldInstalled(){thread();return realmInstalled&&!closed;}
    boolean ready(){thread();return reason()==null;}
    String identity(){thread();return reason()==null?nativeDocument:null;}
    String phase(){thread();String code=reason();return code==null?"ready":code;}
    int pending(){thread();return world==null?0:world.pending();}
    int outstanding(){thread();return world==null?0:world.outstanding();}
    int sharedOutstanding(){thread();return world==null?0:world.sharedOutstanding();}
    int sharedLimit(){thread();return world==null?0:world.sharedLimit();}
    boolean profileAssigned(){thread();return !initializing&&!closed&&physicalProfile!=null&&physicalProfile.owns(view);}
    void cancel(){thread();if(!closed){serial++;if(world!=null)world.cancelPending("world_cancelled");}}
    void evaluate(String expression,long timeout,MdoBrowserWorld.Completion complete) throws MdoBrowserWorld.Failure {
        evaluate(null,expression,timeout,complete);
    }
    void evaluate(String expected,String expression,long timeout,MdoBrowserWorld.Completion complete) throws MdoBrowserWorld.Failure {
        thread();if(complete==null)throw new MdoBrowserWorld.Failure("world_arguments_invalid");
        String code=reason();if(code!=null)throw new MdoBrowserWorld.Failure(code);
        if(expected!=null&&!expected.equals(nativeDocument))throw new MdoBrowserWorld.Failure("frame_changed");
        final MdoBrowserWorld.Peer peer=active;final long generation=serial,document=activeEpoch;
        final String identity=nativeDocument;
        peer.evaluate(expression,timeout,result->{
            if(closed){
                complete.complete(new MdoBrowserWorld.Result(false,null,"world_closed",result.evaluationMayHaveOccurred));
            }else if(!result.ok)complete.complete(result);
            else if(generation!=serial||document!=epoch.getAsLong()||!identity.equals(nativeDocument)||!owned.getAsBoolean()||!world.current(peer)){
                complete.complete(new MdoBrowserWorld.Result(false,null,"frame_changed",result.evaluationMayHaveOccurred));
            }else complete.complete(result);
        });
    }
    @Override public void close(){
        thread();if(!closed){closed=true;serial++;active=null;candidate=null;nativeDocument=null;}if(physicalProfile!=null)physicalProfile.close();if(world!=null)world.close();if(!initializing)initialized=null;
    }
    boolean cleanupConfirmed(){thread();return !initializing&&closed&&(world==null||world.cleanupConfirmed())&&(physicalProfile==null||physicalProfile.cleanupConfirmed());}
    void viewDestroyed(Object original){thread();if(!closed)throw new IllegalStateException("Close the document before native teardown");if(original!=view)throw new IllegalStateException("Confirm only the exact native view");if(initializing)throw new IllegalStateException("Native profile initialization still active");if(world!=null)world.viewDestroyed(original);if(physicalProfile!=null)physicalProfile.viewDestroyed(original);}
}
