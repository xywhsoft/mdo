package org.xserver.android;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import org.json.JSONException;
import org.json.JSONObject;

/** Private per-frame isolated execution. Metadata IDs never grant authority on
 * their own: a native frame route, matching creation reply/unique context and
 * negative unique-ID capability proof are all required before page code. */
final class MdoBrowserCdpWorlds {
    private static final int ACTIVE_LIMIT=256,HISTORY_LIMIT=1024,WORLD_LIMIT=32,RESULT_LIMIT=1048576,SCRIPT_LIMIT=196608;
    private static final class Context {
        final int id;final String session,unique,frame;final Name name;String admittedDocument;
        Context(int id,String session,String unique,String frame,Name name){this.id=id;this.session=session;this.unique=unique;this.frame=frame;this.name=name;}
    }
    private static final class Name {
        final String value,raw,session,loader;
        Name(String value,MdoBrowserCdpFrames.Route route){this.value=value;raw=route.raw;session=route.session;loader=route.loader;}
    }
    private static final class World {
        final MdoBrowserCdpFrames.Route route;final Context context;
        World(MdoBrowserCdpFrames.Route route,Context context){this.route=route;this.context=context;}
    }
    /** Native object pin, never serialized. Short upload steps must not create
     * or adopt a replacement realm even when its frame/loader looks identical. */
    static final class Pin {
        private final MdoBrowserCdpWorlds owner;private final World world;
        private Pin(MdoBrowserCdpWorlds owner,World world){this.owner=owner;this.world=world;}
    }
    private final MdoBrowserCdpBinding binding;private final MdoBrowserCdpFrames frames;
    private final Map<String,Context> contexts=new HashMap<>();private final Set<String> seen=new HashSet<>(),qualified=new HashSet<>();
    private final Map<String,World> worlds=new HashMap<>();
    private final Map<String,Name> names=new HashMap<>(),namesByProof=new HashMap<>();
    MdoBrowserCdpWorlds(MdoBrowserCdpBinding binding,MdoBrowserCdpFrames frames)throws IOException {
        this.binding=binding;this.frames=frames;
        // Runtime was enabled while proving the root, before this listener
        // existed. Retain minimal active identities, no origin/URL/page name.
        for(Map.Entry<Integer,String> row:binding.contextIdentities().entrySet())add(new Context(row.getKey(),null,row.getValue(),null,null));
    }
    private static IOException problem(String code){return new IOException(code);}
    private static String key(String source,int id){return(source==null?"":source)+":"+id;}
    private static String scope(String source){return source==null?"":source;}
    private static int number(Object value)throws IOException {
        if(!(value instanceof Integer)||((Integer)value)<=0)throw problem("cdp_world_metadata_invalid");return(Integer)value;
    }
    private static boolean identity(Object value){
        if(!(value instanceof String)||((String)value).isEmpty()||((String)value).length()>128)return false;
        String text=(String)value;for(int i=0;i<text.length();i++){char c=text.charAt(i);
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||"_.:-".indexOf(c)>=0))return false;}return true;
    }
    private void add(Context context)throws IOException {
        String key=key(context.session,context.id);
        if(seen.contains(key))throw problem("cdp_world_context_reused");
        if(contexts.size()>=ACTIVE_LIMIT||seen.size()>=HISTORY_LIMIT)throw problem("cdp_world_context_limit");
        if(context.unique!=null)for(Context other:contexts.values())if(context.unique.equals(other.unique))throw problem("cdp_world_context_ambiguous");
        seen.add(key);contexts.put(key,context);
    }
    void event(String method,String source,JSONObject params)throws IOException {
        try{
            if("Runtime.executionContextCreated".equals(method)){
                JSONObject value=params.getJSONObject("context"),aux=value.optJSONObject("auxData");int id=number(value.opt("id"));
                Name name=names.get(value.opt("name"));String unique=identity(value.opt("uniqueId"))?(String)value.opt("uniqueId"):null;
                String raw=aux!=null&&aux.opt("frameId") instanceof String&&MdoBrowserCdpJson.token((String)aux.opt("frameId"))?(String)aux.opt("frameId"):null;
                if(name!=null&&(unique==null||raw==null||!Boolean.FALSE.equals(aux.opt("isDefault"))||!"isolated".equals(aux.opt("type"))||
                    !name.raw.equals(raw)||!Objects.equals(name.session,source)))throw problem("cdp_world_context_unsupported");
                add(new Context(id,source,unique,raw,name));return;
            }
            if("Runtime.executionContextDestroyed".equals(method)){
                Context gone=contexts.remove(key(source,number(params.opt("executionContextId"))));
                if(gone!=null&&gone.name!=null&&gone.admittedDocument!=null)frames.worldLost(gone.frame,source,gone.name.loader,gone.admittedDocument);prune();return;
            }
            if("Runtime.executionContextsCleared".equals(method)){
                for(Context context:new ArrayList<>(contexts.values()))if(Objects.equals(source,context.session)){
                    contexts.remove(key(context.session,context.id));if(context.name!=null&&context.admittedDocument!=null)frames.worldLost(context.frame,source,context.name.loader,context.admittedDocument);
                }qualified.remove(scope(source));prune();
            }
        }catch(JSONException error){throw problem("cdp_world_metadata_invalid");}
    }
    void retire(String source){
        contexts.entrySet().removeIf(row->Objects.equals(source,row.getValue().session));qualified.remove(scope(source));prune();
    }
    private boolean active(World world){
        if(world==null||contexts.get(key(world.context.session,world.context.id))!=world.context)return false;
        try{frames.validate(world.route);return true;}catch(IOException failure){return false;}
    }
    private void prune(){worlds.entrySet().removeIf(row->!active(row.getValue()));}
    boolean ready(String frame,String document){World world=worlds.get(frame);return world!=null&&world.route.document.equals(document)&&active(world);}
    private void guard(MdoBrowserCdpFrames.Route route,Context context)throws IOException {
        frames.validate(route);if(context!=null&&contexts.get(key(context.session,context.id))!=context)throw problem("cdp_world_context_changed");
    }
    private JSONObject call(MdoBrowserCdpFrames.Route route,Context context,String method,JSONObject params,boolean effect,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        guard(route,context);JSONObject reply=binding.call(method,params,route.session,Math.min(5000,deadline.remaining()),effect);guard(route,context);return reply;
    }
    private JSONObject evaluation(String expression,String unique,boolean readOnly,MdoBrowserCdpWorker.Deadline deadline)throws JSONException,IOException {
        return new JSONObject().put("expression",expression).put("uniqueContextId",unique).put("returnByValue",true).put("silent",true)
            .put("awaitPromise",false).put("throwOnSideEffect",readOnly).put("timeout",Math.min(5000,deadline.remaining()));
    }
    void prepare(MdoBrowserCdpFrames.Route route,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        guard(route,null);prune();World existing=worlds.get(route.frame);if(active(existing))return;
        if(worlds.size()>=WORLD_LIMIT)throw problem("cdp_world_limit");
        try{
            String nativeProof=scope(route.session)+"\u0000"+route.raw+"\u0000"+route.loader;Name name=namesByProof.get(nativeProof);
            if(name==null){if(!binding.reserveWorldName())throw problem("cdp_world_document_limit");
                name=new Name(binding.worldName+"."+java.util.UUID.randomUUID(),route);names.put(name.value,name);namesByProof.put(nativeProof,name);}
            JSONObject reply=call(route,null,"Page.createIsolatedWorld",new JSONObject().put("frameId",route.raw).put("worldName",name.value)
                .put("grantUniveralAccess",false),false,deadline);int id=number(reply.opt("executionContextId"));
            Context context=contexts.get(key(route.session,id));long started=android.os.SystemClock.elapsedRealtime();
            while(context==null&&android.os.SystemClock.elapsedRealtime()-started<500){
                guard(route,null);binding.poll(Math.min(20,deadline.remaining()));context=contexts.get(key(route.session,id));
            }
            if(context==null||context.name!=name||!route.raw.equals(context.frame)||!Objects.equals(context.session,route.session))throw problem("cdp_world_context_unsupported");
            guard(route,context);
            if(!qualified.contains(scope(route.session))){
                boolean rejected=false;
                try{call(route,context,"Runtime.evaluate",evaluation("void 0","",true,deadline),false,deadline);}
                catch(MdoBrowserCdpSession.Failure failure){if(failure.uniqueContextRejected)rejected=true;else throw failure;}
                if(!rejected)throw problem("cdp_world_unique_context_unsupported");qualified.add(scope(route.session));
            }
            JSONObject proof=call(route,context,"Runtime.evaluate",evaluation("void 0",context.unique,true,deadline),false,deadline);
            JSONObject value=proof.optJSONObject("result");
            if(proof.has("exceptionDetails")||value==null||!"undefined".equals(value.opt("type")))throw problem("cdp_world_context_unsupported");
            guard(route,context);context.admittedDocument=route.document;worlds.put(route.frame,new World(route,context));
        }catch(JSONException error){throw problem("cdp_world_metadata_invalid");}
    }
    JSONObject evaluate(MdoBrowserCdpFrames.Route route,String expression,boolean effect,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        validateScript(expression);
        prepare(route,deadline);return evaluateWorld(worlds.get(route.frame),expression,effect,false,deadline);
    }
    Pin pin(MdoBrowserCdpFrames.Route route)throws IOException {
        World world=worlds.get(route.frame);if(!active(world)||!world.route.document.equals(route.document))throw problem("cdp_world_context_changed");
        return new Pin(this,world);
    }
    void validate(Pin pin)throws IOException {
        if(pin==null||pin.owner!=this||worlds.get(pin.world.route.frame)!=pin.world||!active(pin.world))throw problem("cdp_world_context_changed");
        guard(pin.world.route,pin.world.context);
    }
    JSONObject evaluatePinned(Pin pin,String expression,boolean effect,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        validateScript(expression);
        validate(pin);JSONObject result=evaluateWorld(pin.world,expression,effect,false,deadline);validate(pin);return result;
    }
    JSONObject openPicker(Pin pin,String key,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        if(key==null||!key.matches("up-"+MdoBrowserCdpFrameChain.UUID))throw problem("cdp_upload_request_invalid");
        validate(pin);
        String expression="(p=>{const b=globalThis.__mdo_browser_use_upload_picker_v1;return b&&b.documentNode===document?b.open(p):{ok:false,error:{code:'upload_picker_runtime_unavailable'}};})({key:"+JSONObject.quote(key)+"})";
        JSONObject result=evaluateWorld(pin.world,expression,true,true,deadline);validate(pin);return result;
    }
    private static void validateScript(String expression)throws IOException {
        if(expression==null||expression.isEmpty()||expression.length()>SCRIPT_LIMIT||expression.getBytes(StandardCharsets.UTF_8).length>SCRIPT_LIMIT)
            throw problem("cdp_world_script_invalid");
    }
    private JSONObject evaluateWorld(World world,String expression,boolean effect,boolean pickerGesture,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        if(!active(world))throw problem("cdp_world_context_changed");MdoBrowserCdpFrames.Route route=world.route;
        try{
            // DOM readers necessarily invoke DOM accessors; throwOnSideEffect
            // is used for the fixed capability probes only, never as a policy.
            JSONObject params=evaluation(expression,world.context.unique,false,deadline);
            if(pickerGesture)params.put("userGesture",true);
            JSONObject reply=call(route,world.context,"Runtime.evaluate",params,effect,deadline);
            Object value=byValue(reply);
            JSONObject result=new JSONObject().put("ok",true).put("frame",route.frame).put("frame_document",route.document).put("value",value);
            if(result.toString().getBytes(StandardCharsets.UTF_8).length>RESULT_LIMIT)throw problem("cdp_world_result_limit");guard(route,world.context);return result;
        }catch(JSONException error){throw problem("cdp_world_result_invalid");}
    }
    private static Object byValue(JSONObject reply)throws IOException,JSONException {
        JSONObject remote=reply.optJSONObject("result");
        if(reply.has("exceptionDetails")||remote==null||!remote.has("value")||remote.has("unserializableValue"))throw problem("cdp_world_evaluation_failed");
        Object value=remote.get("value"),type=remote.opt("type");boolean valid=
            "object".equals(type)&&(value==JSONObject.NULL||value instanceof JSONObject||value instanceof org.json.JSONArray)||
            "string".equals(type)&&value instanceof String||"boolean".equals(type)&&value instanceof Boolean||
            "number".equals(type)&&value instanceof Number&&Double.isFinite(((Number)value).doubleValue());
        if(!valid)throw problem("cdp_world_result_invalid");return value;
    }
    private JSONObject ownerCall(MdoBrowserCdpFrames.Route child,MdoBrowserCdpFrames.Route parent,Context context,String method,JSONObject params,
                                 MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        frames.validate(child);JSONObject reply=call(parent,context,method,params,false,deadline);frames.validate(child);return reply;
    }
    private JSONObject function(String source,String unique,org.json.JSONArray args,String group,boolean proof)throws JSONException {
        // Never select a realm using an object/numeric ID. The native engine
        // resolves the unique context and checks argument IDs in that world.
        return new JSONObject().put("functionDeclaration",source).put("uniqueContextId",unique).put("arguments",args)
            .put("returnByValue",true).put("awaitPromise",false).put("silent",true).put("throwOnSideEffect",proof).put("objectGroup",group);
    }
    JSONObject fileInput(MdoBrowserCdpFrames.Route route,String document,String ref,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        if(ref==null||!ref.matches("e[1-9][0-9]{0,12}"))throw problem("cdp_file_input_request_invalid");
        return fileNode(route,document,ref,0,null,deadline);
    }
    JSONObject clearFileInput(MdoBrowserCdpFrames.Route route,String document,String ref,MdoBrowserCdpFileInput.BeforeClear original,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        if(ref==null||!ref.matches("e[1-9][0-9]{0,12}")||original==null)throw problem("cdp_file_input_request_invalid");
        return fileNode(route,document,ref,0,original,deadline);
    }
    JSONObject chooserInput(MdoBrowserCdpFrames.Route route,int backend,String document,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        if(backend<=0)throw problem("cdp_chooser_metadata_invalid");return fileNode(route,document,null,backend,null,deadline);
    }
    private JSONObject fileNode(MdoBrowserCdpFrames.Route route,String document,String ref,int backend,MdoBrowserCdpFileInput.BeforeClear original,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        if(document==null||!document.matches("d-"+MdoBrowserCdpFrameChain.UUID))
            throw problem("cdp_file_input_request_invalid");
        prepare(route,deadline);World world=worlds.get(route.frame);if(!active(world))throw problem("cdp_world_context_changed");
        Context context=world.context;String group="mdo-file-input-"+java.util.UUID.randomUUID();boolean declared=false;IOException failure=null;JSONObject result=null;
        try{
            JSONObject pin=new JSONObject().put("document",document);if(ref!=null)pin.put("ref",ref);declared=true;
            JSONObject reply,node;
            if(backend>0){reply=call(route,context,"DOM.resolveNode",new JSONObject().put("backendNodeId",backend).put("executionContextId",context.id).put("objectGroup",group),false,deadline);node=reply.optJSONObject("object");}
            else{JSONObject query=evaluation(MdoBrowserCdpFileInput.NODE+"("+pin.toString()+")",context.unique,false,deadline).put("returnByValue",false).put("objectGroup",group);
                reply=call(route,context,"Runtime.evaluate",query,false,deadline);node=reply.optJSONObject("result");}
            if(reply.has("exceptionDetails")||node==null||!"object".equals(node.opt("type"))||!"node".equals(node.opt("subtype"))||!(node.opt("objectId") instanceof String))
                throw problem("cdp_file_input_node_invalid");
            String object=node.getString("objectId");if(object.isEmpty()||object.length()>2048)throw problem("cdp_file_input_node_invalid");
            org.json.JSONArray args=new org.json.JSONArray().put(new JSONObject().put("objectId",object));boolean rejected=false;
            try{call(route,context,"Runtime.callFunctionOn",function("function(element){return 0;}","",args,group,true),false,deadline);}
            catch(MdoBrowserCdpSession.Failure error){if(error.uniqueContextRejected)rejected=true;else throw error;}
            if(!rejected)throw problem("cdp_file_input_unique_context_unsupported");
            Object proof=byValue(call(route,context,"Runtime.callFunctionOn",function("function(element){return 0;}",context.unique,args,group,true),false,deadline));
            if(!(proof instanceof Number)||((Number)proof).doubleValue()!=0)throw problem("cdp_file_input_world_proof_failed");
            JSONObject description=call(route,context,"DOM.describeNode",new JSONObject().put("objectId",object).put("depth",0).put("pierce",false),false,deadline).optJSONObject("node");
            if(description==null||!Integer.valueOf(1).equals(description.opt("nodeType"))||!"INPUT".equalsIgnoreCase(description.optString("nodeName"))||!"input".equals(description.opt("localName")))
                throw problem("cdp_file_input_node_invalid");
            int described=number(description.opt("backendNodeId"));if(backend>0&&backend!=described)throw problem("cdp_chooser_target_changed");
            args.put(new JSONObject().put("value",pin));Object inspected=byValue(call(route,context,"Runtime.callFunctionOn",function(ref==null?MdoBrowserCdpFileInput.PIN:MdoBrowserCdpFileInput.INFO,context.unique,args,group,false),false,deadline));
            if(!(inspected instanceof JSONObject))throw problem("cdp_file_input_result_invalid");JSONObject value=(JSONObject)inspected;
            if(!Boolean.TRUE.equals(value.opt("ok"))){String code=value.optString("code","");throw problem(code.matches("[a-z_]{1,40}")?"cdp_file_input_"+code:"cdp_file_input_target_changed");}
            if(!document.equals(value.opt("document"))||!(value.opt("ref") instanceof String)||!((String)value.opt("ref")).matches("e[1-9][0-9]{0,12}")||ref!=null&&!ref.equals(value.opt("ref"))||!(value.opt("multiple") instanceof Boolean)||!(value.opt("directory") instanceof Boolean)||!(value.opt("hidden") instanceof Boolean))
                throw problem("cdp_file_input_result_invalid");
            JSONObject element=new JSONObject().put("ref",value.getString("ref")).put("multiple",value.getBoolean("multiple")).put("directory",value.getBoolean("directory")).put("hidden",value.getBoolean("hidden"));
            result=new JSONObject().put("ok",true).put("frame",route.frame).put("frame_document",route.document)
                .put("value",new JSONObject().put("ok",true).put("document",document).put("element",element));
            if(original!=null){
                // Only this fixed native operation supplies an empty file list.
                // Keep the original opaque node; neither paths nor caller CDP
                // parameters can enter this route, and no failed step is replayed.
                original.check();guard(route,context);deadline.remaining();
                JSONObject cleared=call(route,context,"DOM.setFileInputFiles",new JSONObject().put("objectId",object).put("files",new org.json.JSONArray()),true,deadline);
                if(cleared.length()!=0)throw problem("cdp_file_clear_unconfirmed");
                original.check();guard(route,context);
                Object checked=byValue(call(route,context,"Runtime.callFunctionOn",function(MdoBrowserCdpFileInput.CLEAR_STATUS,context.unique,args,group,false),false,deadline));
                if(!(checked instanceof JSONObject))throw problem("cdp_file_clear_unconfirmed");JSONObject status=(JSONObject)checked;
                if(!Boolean.TRUE.equals(status.opt("ok"))||!document.equals(status.opt("document"))||!ref.equals(status.opt("ref"))||!Boolean.TRUE.equals(status.opt("empty")))
                    throw problem("cdp_file_clear_unconfirmed");
                result.getJSONObject("value").put("cleared",true).put("verified",true).put("dispatched",true).put("input_method","native_file_clear");
            }
        }catch(JSONException error){failure=problem("cdp_file_input_result_invalid");}
        catch(IOException error){failure=error;}
        catch(RuntimeException|Error error){failure=problem("cdp_file_input_failed");}
        finally{
            if(declared)try{binding.call("Runtime.releaseObjectGroup",new JSONObject().put("objectGroup",group),route.session,Math.min(5000,deadline.remaining()),false);}
            catch(IOException|JSONException|RuntimeException|Error error){binding.close();String code=error.getMessage();
                // The cleanup fence must survive a primary inspection/clear
                // failure. Keep that original failure as the private cause.
                failure=new IOException("cdp_worker_timeout".equals(code)||"cdp_worker_cancelled".equals(code)?code:"cdp_file_input_cleanup_failed",failure);}
        }
        if(failure!=null)throw failure;guard(route,context);return result;
    }
    JSONObject owner(MdoBrowserCdpFrames.Route child,MdoBrowserCdpFrames.Route parent,String action,JSONObject point,
                     MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        frames.validate(child);prepare(parent,deadline);World world=worlds.get(parent.frame);if(!active(world))throw problem("cdp_world_context_changed");
        Context context=world.context;String group="mdo-frame-owner-"+java.util.UUID.randomUUID();boolean groupDeclared=false;IOException failure=null;JSONObject result=null;
        try{
            JSONObject owner=ownerCall(child,parent,context,"DOM.getFrameOwner",new JSONObject().put("frameId",child.raw),deadline);
            int backend=number(owner.opt("backendNodeId"));groupDeclared=true;
            JSONObject resolved=ownerCall(child,parent,context,"DOM.resolveNode",new JSONObject().put("backendNodeId",backend)
                .put("executionContextId",context.id).put("objectGroup",group),deadline).optJSONObject("object");
            if(resolved==null||!"object".equals(resolved.opt("type"))||!"node".equals(resolved.opt("subtype"))||!(resolved.opt("objectId") instanceof String))
                throw problem("cdp_owner_node_invalid");
            String object=resolved.getString("objectId");if(object.isEmpty()||object.length()>2048)throw problem("cdp_owner_node_invalid");
            org.json.JSONArray args=new org.json.JSONArray().put(new JSONObject().put("objectId",object));
            boolean uniqueRejected=false;
            try{ownerCall(child,parent,context,"Runtime.callFunctionOn",function("function(element){return 0;}","",args,group,true),deadline);}
            catch(MdoBrowserCdpSession.Failure error){if(error.uniqueContextRejected)uniqueRejected=true;else throw error;}
            if(!uniqueRejected)throw problem("cdp_owner_unique_context_unsupported");
            Object proof=byValue(ownerCall(child,parent,context,"Runtime.callFunctionOn",function("function(element){return 0;}",context.unique,args,group,true),deadline));
            if(!(proof instanceof Number)||((Number)proof).doubleValue()!=0)throw problem("cdp_owner_world_proof_failed");
            String body="owner".equals(action)?MdoBrowserCdpOwnerScripts.GEOMETRY:"upload_policy".equals(action)?MdoBrowserCdpOwnerScripts.UPLOAD_POLICY:MdoBrowserCdpOwnerScripts.CAPTURE;
            if("owner".equals(action))args.put(new JSONObject().put("value",point));
            Object value=byValue(ownerCall(child,parent,context,"Runtime.callFunctionOn",function("function(element,point){return ("+body+").call(element,point);}",context.unique,args,group,false),deadline));
            if(!(value instanceof JSONObject)||!(((JSONObject)value).opt("ok") instanceof Boolean))throw problem("cdp_owner_result_invalid");
            result=new JSONObject().put("ok",true).put("frame",child.frame).put("frame_document",child.document).put("parent",parent.frame)
                .put("parent_document",parent.document).put("value",value);
            if(result.toString().getBytes(StandardCharsets.UTF_8).length>RESULT_LIMIT)throw problem("cdp_world_result_limit");
        }catch(JSONException error){failure=problem("cdp_owner_result_invalid");}
        catch(IOException error){failure=error;}
        catch(RuntimeException|Error error){failure=problem("cdp_owner_failed");}
        finally{
            if(groupDeclared)try{
                // Release this exact random group on its original source even
                // if the document pin changed. No cleanup eval or retargeting.
                binding.call("Runtime.releaseObjectGroup",new JSONObject().put("objectGroup",group),parent.session,Math.min(5000,deadline.remaining()),false);
            }catch(IOException|JSONException|RuntimeException|Error error){binding.close();if(failure==null){
                String code=error.getMessage();failure=problem("cdp_worker_timeout".equals(code)||"cdp_worker_cancelled".equals(code)?code:"cdp_owner_cleanup_failed");}}
        }
        if(failure!=null)throw failure;frames.validate(child);guard(parent,context);return result;
    }
}
