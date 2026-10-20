package org.xserver.android;

import android.app.Activity;
import android.content.*;
import android.content.res.AssetFileDescriptor;
import android.database.Cursor;
import android.net.Uri;
import android.os.CancellationSignal;
import android.provider.OpenableColumns;
import android.util.Base64;
import android.webkit.*;
import android.widget.Toast;
import org.json.*;
import java.io.*;
import java.lang.ref.WeakReference;
import java.util.*;
import java.util.concurrent.*;

/** Shares are copied while URI grants are live, before frontend/service readiness.
 * The persisted inbox is acknowledged only after the selected draft is durable. */
final class MdoShares {
    private static final Object LOCK = new Object();
    private static final ThreadPoolExecutor WORK = new ThreadPoolExecutor(1, 1, 30, TimeUnit.SECONDS,
        new ArrayBlockingQueue<Runnable>(4), runnable -> { Thread t = new Thread(runnable, "mdo-share"); t.setDaemon(true); return t; });
    private final Context app;
    private final WeakReference<Activity> activity;
    private final WeakReference<WebView> web;
    private final File root;
    private volatile String token = "";

    MdoShares(Activity owner, WebView view) {
        app = owner.getApplicationContext(); activity = new WeakReference<>(owner); web = new WeakReference<>(view);
        root = new File(app.getFilesDir(), "mdo-shares");
    }
    static String id() { return UUID.randomUUID().toString().replace("-", ""); }
    private File directory(String id) throws Exception {
        if (!MdoShareFiles.id(id)) throw new IOException("Invalid share ID");
        return MdoShareFiles.child(root, id);
    }
    private JSONObject load(String id) throws Exception {
        JSONObject value = new JSONObject(MdoShareFiles.text(MdoShareFiles.child(directory(id), "ready.json"), MdoShareFiles.MANIFEST_LIMIT));
        if (!id.equals(value.getString("id"))) throw new IOException("Share identity changed");
        return value;
    }
    private void save(JSONObject value) throws Exception {
        MdoShareFiles.atomic(directory(value.getString("id")), "ready.json", value.toString());
    }
    private boolean allowed(String value) { return value != null && !token.isEmpty() && token.equals(value); }
    void detach() { token = ""; web.clear(); activity.clear(); }
    void ready() {
        WebView view = web.get(); if (view == null) return;
        token = id();
        view.evaluateJavascript("window.__mdoShareToken=" + JSONObject.quote(token) +
            ";window.dispatchEvent(new Event('mdo-shares-ready'));", null);
    }
    private void notifyReady() {
        Activity owner = activity.get();
        if (owner != null) owner.runOnUiThread(() -> {
            WebView view = web.get();
            if (view != null && !token.isEmpty()) view.evaluateJavascript("window.dispatchEvent(new Event('mdo-shares-ready'));", null);
        });
    }
    private void notice() {
        Activity owner = activity.get();
        if (owner != null) owner.runOnUiThread(() -> {
            String language = app.getResources().getConfiguration().getLocales().get(0).getLanguage();
            String message = "zh".equals(language) ? "无法接收分享。请检查文件大小、读取权限、存储空间或待处理分享。" :
                "ru".equals(language) ? "Не удалось принять данные. Проверьте размер, доступ, свободное место и очередь." :
                "Could not receive this share. Check file size, read access, storage and pending shares.";
            Toast.makeText(app, message, Toast.LENGTH_LONG).show();
        });
    }
    void receive(Intent intent, boolean restored) {
        if (restored || intent == null || (intent.getFlags() & Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY) != 0) return;
        String action = intent.getAction();
        if (!Intent.ACTION_SEND.equals(action) && !Intent.ACTION_SEND_MULTIPLE.equals(action)) return;
        try {
            LinkedHashSet<Uri> uris = new LinkedHashSet<>();
            if (Intent.ACTION_SEND_MULTIPLE.equals(action)) {
                ArrayList<?> values = intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
                if (values != null) for (Object item : values) {
                    if (!(item instanceof Uri)) throw new IOException("Invalid shared stream"); uris.add((Uri)item);
                    if (uris.size() > MdoShareFiles.FILE_COUNT) throw new IOException("Too many shared files");
                }
            } else {
                Object item = intent.getParcelableExtra(Intent.EXTRA_STREAM);
                if (item != null) { if (!(item instanceof Uri)) throw new IOException("Invalid shared stream"); uris.add((Uri)item); }
            }
            ClipData clip = intent.getClipData();
            if (clip != null) for (int i = 0; i < clip.getItemCount(); i++) {
                Uri uri = clip.getItemAt(i).getUri(); if (uri != null) uris.add(uri);
                if (uris.size() > MdoShareFiles.FILE_COUNT) throw new IOException("Too many shared files");
            }
            for (Uri uri : uris) if (!"content".equals(uri.getScheme()) || uri.toString().length() > 8192) throw new IOException("Share requires a content URI");
            CharSequence raw = intent.getCharSequenceExtra(Intent.EXTRA_TEXT);
            String text = raw == null ? "" : raw.toString();
            if (raw == null && clip != null) for (int i = 0; i < clip.getItemCount(); i++) {
                ClipData.Item item = clip.getItemAt(i);
                if (item.getUri() == null && item.getText() != null) {
                    if (text.length() + item.getText().length() > 32768) throw new IOException("Shared text is too large");
                    text += (text.isEmpty() ? "" : "\n") + item.getText();
                }
            }
            if (text.getBytes("UTF-8").length > 32768 || (text.isEmpty() && uris.isEmpty())) throw new IOException("Invalid shared text");
            final String sharedText = text;
            WORK.execute(() -> receiveFiles(uris, sharedText));
        } catch (Exception error) { notice(); }
    }
    private void receiveFiles(Set<Uri> uris, String text) {
        JSONObject manifest = new JSONObject(); File target = null;
        try {
            synchronized (LOCK) {
                if ((!root.isDirectory() && !root.mkdirs()) || java.nio.file.Files.isSymbolicLink(root.toPath())) throw new IOException("Invalid share inbox");
                File[] entries = root.listFiles();
                if ((entries != null && entries.length >= MdoShareFiles.BATCH_COUNT) || MdoShareFiles.bytes(root) >= MdoShareFiles.INBOX_LIMIT)
                    throw new IOException("Share inbox is full");
                String key = id(); target = directory(key);
                if (!target.mkdir()) throw new IOException("Cannot create inbox entry");
                manifest.put("id", key).put("created", System.currentTimeMillis()).put("text", text).put("files", new JSONArray());
                MdoShareFiles.atomic(target, "receiving.json", manifest.toString());
            }
            long total = 0;
            for (Uri uri : uris) {
                String name = "shared-file", mime = null, key = id();
                // Display metadata is optional for content providers; inability
                // to query it must not prevent reading an otherwise valid stream.
                try { mime = app.getContentResolver().getType(uri); } catch (RuntimeException ignored) { }
                mime = mime == null ? "application/octet-stream" : mime.split(";",2)[0].trim().toLowerCase(Locale.ROOT);
                if (mime.length() > 127 || !mime.matches("[a-z0-9.+-]+/[a-z0-9.+-]+")) mime = "application/octet-stream";
                try (Cursor cursor = app.getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                    if (cursor != null && cursor.moveToFirst()) name = cursor.getString(0);
                } catch (RuntimeException ignored) { }
                name = MdoShareFiles.filename(name);
                File file = MdoShareFiles.child(target, key + ".bin");
                long room = Math.min(MdoShareFiles.FILE_LIMIT, MdoShareFiles.BATCH_LIMIT - total);
                synchronized (LOCK) { room = Math.min(room, MdoShareFiles.INBOX_LIMIT - MdoShareFiles.bytes(root)); }
                String hash;
                try (AssetFileDescriptor descriptor = app.getContentResolver().openAssetFileDescriptor(uri, "r", new CancellationSignal())) {
                    if (descriptor == null) throw new IOException("Shared file is unavailable");
                    try (InputStream input = descriptor.createInputStream()) { hash = MdoShareFiles.copy(input, file, room); }
                }
                total += file.length();
                manifest.getJSONArray("files").put(new JSONObject().put("id", key).put("name", name)
                    .put("mime", mime == null ? "application/octet-stream" : mime).put("bytes", file.length()).put("sha256", hash));
            }
            synchronized (LOCK) { save(manifest); java.nio.file.Files.deleteIfExists(new File(target, "receiving.json").toPath()); }
            notifyReady();
        } catch (Exception error) {
            try {
                if (target != null) synchronized (LOCK) {
                    MdoShareFiles.remove(target); target.mkdir();
                    manifest.put("files", new JSONArray()).put("error", "incomplete"); save(manifest);
                }
            } catch (Exception ignored) { }
            notice(); notifyReady();
        }
    }
    @JavascriptInterface public String peek(String proof) {
        if (!allowed(proof)) return "null";
        synchronized (LOCK) { try {
            File[] entries = root.listFiles(); if (entries == null) return "null";
            JSONObject oldest = null;
            for (File entry : entries) if (MdoShareFiles.id(entry.getName())) {
                JSONObject value;
                try { value = load(entry.getName()); }
                catch (Exception missing) {
                    // Process death during copying retains an explicit failed share.
                    if (WORK.getActiveCount() != 0) continue;
                    try { value = new JSONObject(MdoShareFiles.text(MdoShareFiles.child(entry, "receiving.json"), MdoShareFiles.MANIFEST_LIMIT)); }
                    catch (Exception invalid) { value = new JSONObject().put("id",entry.getName()).put("created",entry.lastModified()).put("text",""); }
                    value.put("error", "interrupted").put("files", new JSONArray()); save(value);
                }
                if (oldest == null || value.getLong("created") < oldest.getLong("created")) oldest = value;
            }
            return oldest == null ? "null" : oldest.toString();
        } catch (Exception error) { return "null"; } }
    }
    @JavascriptInterface public String bind(String proof, String key, String project, String session, boolean create) {
        if (!allowed(proof)) return "null";
        synchronized (LOCK) { try {
            if (!project.matches("[A-Za-z0-9_-][A-Za-z0-9_.-]{0,127}") || !session.matches("[A-Za-z0-9_-][A-Za-z0-9_.-]{0,127}")) return "null";
            JSONObject value = load(key), destination = value.optJSONObject("destination");
            if (destination != null && (!project.equals(destination.getString("project_id")) || !session.equals(destination.getString("session_id")))) return "null";
            value.put("destination", new JSONObject().put("project_id", project).put("session_id", session).put("create", create)); save(value); return value.toString();
        } catch (Exception error) { return "null"; } }
    }
    @JavascriptInterface public boolean plan(String proof, String key, String document, boolean replace) {
        if (!allowed(proof) || document == null || document.getBytes(java.nio.charset.StandardCharsets.UTF_8).length > MdoShareFiles.PLAN_LIMIT) return false;
        synchronized (LOCK) { try {
            JSONObject value = load(key); if (!value.has("destination")) return false;
            JSONObject plan = new JSONObject(document);
            if (!replace && value.has("delivery") && !value.getJSONObject("delivery").toString().equals(plan.toString())) return false;
            value.put("delivery", plan); save(value); return true;
        } catch (Exception error) { return false; } }
    }
    @JavascriptInterface public String read(String proof, String key, String fileId, int offset) {
        if (!allowed(proof) || !MdoShareFiles.id(fileId) || offset < 0 || offset > 8*1024*1024) return null;
        synchronized (LOCK) { try {
            JSONObject value = load(key); JSONArray files = value.getJSONArray("files"); boolean found = false;
            for (int i=0;i<files.length();i++) if (fileId.equals(files.getJSONObject(i).getString("id")) && files.getJSONObject(i).getLong("bytes") <= 8*1024*1024) found = true;
            if (!found) return null;
            File file = MdoShareFiles.child(directory(key), fileId + ".bin");
            try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
                if (offset > input.length()) return null;
                input.seek(offset); byte[] buffer = new byte[65536]; int count = input.read(buffer);
                return count < 0 ? "" : Base64.encodeToString(buffer, 0, count, Base64.NO_WRAP);
            }
        } catch (Exception error) { return null; } }
    }
    @JavascriptInterface public boolean acknowledge(String proof, String key) {
        if (!allowed(proof)) return false;
        synchronized (LOCK) { try { MdoShareFiles.remove(directory(key)); return true; } catch (Exception error) { return false; } }
    }
}
