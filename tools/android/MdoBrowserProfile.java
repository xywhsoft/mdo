package org.xserver.android;

import java.lang.reflect.InvocationHandler;

/** One exact native view's acknowledged profile association. No isolated-world,
 * script, listener, network, worker or default-profile access is needed. Callers
 * own fresh native construction and must assign before other WebView use. */
final class MdoBrowserProfile implements AutoCloseable {
    private final Object view;private final ClassLoader loader;private final InvocationHandler provider;
    private final boolean supported;private final Thread owner=Thread.currentThread();
    private boolean attempted,uncertain,sealed,closed;private String assigned;

    MdoBrowserProfile(Object view,ClassLoader loader,InvocationHandler provider,boolean supported){
        if(view==null||loader==null||provider==null)throw new IllegalArgumentException("Missing native profile owner");
        this.view=view;this.loader=loader;this.provider=provider;this.supported=supported;
    }
    static MdoBrowserProfile open(Object view,ClassLoader loader)throws MdoBrowserWorld.Failure {
        if(view==null||loader==null)throw new MdoBrowserWorld.Failure("world_arguments_invalid");
        MdoBrowserWorld.Features features=MdoBrowserWorld.inspect(loader);
        if(!features.profiles)throw new MdoBrowserWorld.Failure("browser_profile_unsupported");
        return new MdoBrowserProfile(view,loader,MdoBrowserWorld.provider(features,view),true);
    }
    /** Inherit only the confirmed original association in this provider namespace.
     * The returned child holds no parent reference and never shares its lifetime.
     * A failure can follow a native setter; the caller still owns child teardown. */
    static MdoBrowserProfile inherit(Object view,ClassLoader loader,MdoBrowserProfile original)throws MdoBrowserWorld.Failure {
        String name=inheritedName(view,loader,original);MdoBrowserProfile child=null;
        try{child=open(view,loader);child.inherit(original,name);return child;}
        catch(MdoBrowserWorld.Failure|RuntimeException|Error failure){if(child!=null)child.close();throw failure;}
    }
    static String inheritedName(Object view,ClassLoader loader,MdoBrowserProfile original)throws MdoBrowserWorld.Failure {
        if(original==null)throw new MdoBrowserWorld.Failure("browser_profile_source_invalid");original.thread();
        String name=original.name();
        if(view==null||view==original.view||loader!=original.loader||name==null)throw new MdoBrowserWorld.Failure("browser_profile_source_invalid");
        return name;
    }
    /** The physical controller retains this child before a native setter may
     * reenter. Failed acknowledgment therefore remains chargeable to its owner. */
    void inherit(MdoBrowserProfile original,String expected)throws MdoBrowserWorld.Failure {
        thread();if(original==null||expected==null||view==original.view||loader!=original.loader)throw new MdoBrowserWorld.Failure("browser_profile_source_invalid");original.thread();
        if(!expected.equals(original.name()))throw new MdoBrowserWorld.Failure("browser_profile_owner_changed");
        assign(expected);
        if(!expected.equals(original.name()))throw new MdoBrowserWorld.Failure("browser_profile_owner_changed");
        seal();
    }
    private void thread(){if(Thread.currentThread()!=owner)throw new IllegalStateException("Profile requires its owning UI thread");}
    boolean supported(){thread();return supported;}
    boolean owns(Object exact){thread();return view==exact&&name()!=null;}
    InvocationHandler provider(Object exact,ClassLoader original)throws MdoBrowserWorld.Failure {
        thread();if(view!=exact||loader!=original||!sealed||name()==null)throw new MdoBrowserWorld.Failure("browser_profile_source_invalid");return provider;
    }
    void assign(String name)throws MdoBrowserWorld.Failure {
        thread();available();
        if(closed||sealed||attempted&&!java.util.Objects.equals(name,assigned))throw new MdoBrowserWorld.Failure("world_profile_order");
        if(!supported)throw new MdoBrowserWorld.Failure("browser_profile_unsupported");
        if(name==null||name.equals("Default")||!name.matches("[A-Za-z0-9._-]{1,80}"))throw new MdoBrowserWorld.Failure("world_profile_invalid");
        if(attempted)return;
        // Reading the previous profile first may lock it. One setter, followed
        // by exact native-view readback; acknowledgment loss is never replayed.
        attempted=true;uncertain=true;
        try{
            MdoBrowserWorld.invoke(loader,provider,"WebViewProviderBoundaryInterface","setProfile",name);
            if(closed)throw new MdoBrowserWorld.Failure("browser_profile_unconfirmed");
            Object profile=MdoBrowserWorld.invoke(loader,provider,"WebViewProviderBoundaryInterface","getProfile");
            if(closed||!(profile instanceof InvocationHandler))throw new MdoBrowserWorld.Failure("browser_profile_unconfirmed");
            Object actual=MdoBrowserWorld.invoke(loader,(InvocationHandler)profile,"ProfileBoundaryInterface","getName");
            if(closed||!name.equals(actual))throw new MdoBrowserWorld.Failure("browser_profile_unconfirmed");
            assigned=name;uncertain=false;
        }catch(MdoBrowserWorld.Failure failure){throw new MdoBrowserWorld.Failure("browser_profile_unconfirmed");}
    }
    void available()throws MdoBrowserWorld.Failure {thread();if(uncertain)throw new MdoBrowserWorld.Failure("browser_profile_unconfirmed");}
    void seal()throws MdoBrowserWorld.Failure {thread();available();if(closed)throw new MdoBrowserWorld.Failure("world_profile_order");sealed=true;}
    String name(){thread();return closed||uncertain?null:assigned;}
    @Override public void close(){thread();closed=true;}
    void viewDestroyed(Object original){thread();if(!closed||view!=original)throw new IllegalStateException("Confirm only the exact closed native view");uncertain=false;}
    boolean cleanupConfirmed(){thread();return closed&&!uncertain;}
}
