package org.xserver.android;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.IdentityHashMap;
import java.util.Iterator;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Private protocol envelope codec. The platform JSON reader runs only after
 * bounded strict native validation, including duplicate keys at every depth.
 * Provider error messages/data are never turned into public exception text. */
final class MdoBrowserCdpJson {
    static final int SEND_MAX=262144,MESSAGE_MAX=4194304;
    static final class Message {
        final int id,error;final boolean failed;boolean uniqueContextRejected;
        final String method,session;
        final JSONObject value;
        Message(int id,int error,boolean failed,String method,String session,JSONObject value) {
            this.id=id;this.error=error;this.failed=failed;this.method=method;this.session=session;this.value=value;
        }
        boolean event(){return method!=null;}
    }
    private MdoBrowserCdpJson(){}
    static boolean token(String text) {
        if(text==null||text.length()<1||text.length()>128)return false;
        for(int i=0;i<text.length();i++) {
            char c=text.charAt(i);
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;
        }
        return true;
    }
    static boolean method(String text) {
        if(text==null||text.length()<3||text.length()>128)return false;
        int dot=text.indexOf('.');if(dot<1||dot==text.length()-1||text.indexOf('.',dot+1)>=0)return false;
        for(int i=0;i<text.length();i++) {
            char c=text.charAt(i);if(i==dot)continue;
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(i!=0&&i!=dot+1&&c>='0'&&c<='9')))return false;
        }
        return true;
    }
    private static IOException invalid(){return new IOException("cdp_packet_invalid");}
    private static void fields(JSONObject value,String... allowed) throws IOException {
        for(Iterator<String> keys=value.keys();keys.hasNext();) {
            String key=keys.next();boolean found=false;
            for(String name:allowed)if(name.equals(key)){found=true;break;}
            if(!found)throw invalid();
        }
    }
    private static int integer(Object value,long minimum,long maximum) throws IOException {
        if(!(value instanceof Integer)&&!(value instanceof Long))throw invalid();
        long number=((Number)value).longValue();if(number<minimum||number>maximum)throw invalid();return (int)number;
    }
    static Message decode(byte[] bytes) throws IOException {
        MdoBrowserCdpNative.validate(bytes,MESSAGE_MAX);
        try {
            JSONObject root=new JSONObject(new String(bytes,StandardCharsets.UTF_8));
            String session=null;
            if(root.has("sessionId")) {
                Object value=root.get("sessionId");if(!(value instanceof String)||!token((String)value))throw invalid();
                session=(String)value;
            }
            if(root.has("method")) {
                fields(root,"method","params","sessionId");Object name=root.get("method");
                if(!(name instanceof String)||!method((String)name))throw invalid();
                Object params=root.has("params")?root.get("params"):new JSONObject();
                if(!(params instanceof JSONObject))throw invalid();
                return new Message(0,0,false,(String)name,session,(JSONObject)params);
            }
            fields(root,"id","result","error","sessionId");
            int id=integer(root.get("id"),1,Integer.MAX_VALUE);
            if(root.has("result")==root.has("error"))throw invalid();
            if(root.has("result")) {
                Object result=root.get("result");if(!(result instanceof JSONObject))throw invalid();
                return new Message(id,0,false,null,session,(JSONObject)result);
            }
            Object error=root.get("error");if(!(error instanceof JSONObject))throw invalid();
            JSONObject problem=(JSONObject)error;fields(problem,"code","message","data");
            int code=integer(problem.get("code"),Integer.MIN_VALUE,Integer.MAX_VALUE);Object message=problem.get("message");
            if(!(message instanceof String)||((String)message).length()>4096)throw invalid();
            Message reply=new Message(id,code,true,null,session,null);
            reply.uniqueContextRejected=code==-32602&&("invalid uniqueContextId".equals(message)||"uniqueContextId not found".equals(message));
            return reply;
        } catch(JSONException failure){throw invalid();}
    }
    private static final class Budget {
        int nodes,chars;final IdentityHashMap<Object,Boolean> path=new IdentityHashMap<>();
        void add(int count) throws IOException {
            if(count<0||count>SEND_MAX-chars)throw new IOException("cdp_arguments_invalid");chars+=count;
        }
        void visit(Object value,int depth) throws IOException,JSONException {
            if(depth>64||++nodes>16384)throw new IOException("cdp_arguments_invalid");add(4);
            if(value==null||value==JSONObject.NULL||value instanceof Boolean)return;
            if(value instanceof String){add(((String)value).length());return;}
            if(value instanceof Integer||value instanceof Long||value instanceof Short||value instanceof Byte){add(24);return;}
            if(value instanceof Double||value instanceof Float) {
                double number=((Number)value).doubleValue();
                if(Double.isNaN(number)||Double.isInfinite(number))throw new IOException("cdp_arguments_invalid");add(32);return;
            }
            if(!(value instanceof JSONObject)&&!(value instanceof JSONArray))throw new IOException("cdp_arguments_invalid");
            if(path.put(value,Boolean.TRUE)!=null)throw new IOException("cdp_arguments_invalid");
            try {
                if(value instanceof JSONObject) {
                    JSONObject object=(JSONObject)value;
                    for(Iterator<String> keys=object.keys();keys.hasNext();) {String key=keys.next();add(key.length()+4);visit(object.get(key),depth+1);}
                } else {
                    JSONArray array=(JSONArray)value;for(int i=0;i<array.length();i++)visit(array.get(i),depth+1);
                }
            } finally {path.remove(value);}
        }
    }
    static byte[] encode(int id,String method,JSONObject params,String session) throws IOException {
        if(id<1||!method(method)||(session!=null&&!token(session)))throw new IOException("cdp_arguments_invalid");
        try {
            JSONObject root=new JSONObject().put("id",id).put("method",method).put("params",params==null?new JSONObject():params);
            if(session!=null)root.put("sessionId",session);
            new Budget().visit(root,0);String text=root.toString();
            if(text==null)throw new IOException("cdp_arguments_invalid");
            byte[] bytes=text.getBytes(StandardCharsets.UTF_8);
            if(bytes.length>SEND_MAX){java.util.Arrays.fill(bytes,(byte)0);throw new IOException("cdp_arguments_invalid");}
            try {MdoBrowserCdpNative.validate(bytes,SEND_MAX);return bytes;}
            catch(IOException|RuntimeException|Error failure){java.util.Arrays.fill(bytes,(byte)0);throw failure;}
        } catch(JSONException failure){throw new IOException("cdp_arguments_invalid");}
    }
}
