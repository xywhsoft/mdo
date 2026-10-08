package org.xserver.android;

import android.content.Context;
import android.system.Os;
import org.json.*;
import java.io.*;
import java.security.MessageDigest;

/** Native code is installed by PackageManager. Only data and symlinks live in files/. */
final class MdoRuntime {
    static void initialize(Context context) throws Exception {
        JSONObject metadata;
        try (InputStream input = context.getAssets().open("mdo-runtime.json")) {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192]; int n;
            while ((n = input.read(buffer)) != -1) bytes.write(buffer, 0, n);
            metadata = new JSONObject(bytes.toString("UTF-8"));
        }
        String nativeDir = context.getApplicationInfo().nativeLibraryDir;
        File root = MdoRuntimePaths.root(context.getFilesDir(), metadata.getInt("revision"));
        JSONArray files = metadata.getJSONArray("files");
        for (int i = 0; i < files.length(); i++) {
            JSONObject item = files.getJSONObject(i);
            File target = MdoRuntimePaths.target(root, item.getString("path"));
            File pending = File.createTempFile(".mdo-runtime-", ".pending", target.getParentFile());
            try {
                if (item.has("native")) {
                    File source = MdoRuntimePaths.nativeSource(new File(nativeDir), item.getString("native"));
                    if (!pending.delete()) throw new IOException("Cannot prepare runtime link");
                    Os.symlink(source.getAbsolutePath(), pending.getAbsolutePath());
                } else {
                    MessageDigest digest = MessageDigest.getInstance("SHA-256");
                    try (InputStream input = context.getAssets().open("mdo-runtime/" + item.getString("path"));
                         FileOutputStream output = new FileOutputStream(pending)) {
                        byte[] buffer = new byte[65536]; int n;
                        while ((n = input.read(buffer)) != -1) { digest.update(buffer, 0, n); output.write(buffer, 0, n); }
                        output.getFD().sync();
                    }
                    StringBuilder hash = new StringBuilder();
                    for (byte b : digest.digest()) hash.append(String.format("%02x", b & 255));
                    if (!hash.toString().equals(item.getString("sha256"))) throw new IOException("Runtime data checksum mismatch");
                }
                // POSIX rename atomically replaces both existing and dangling
                // links. Never truncate a native tool by writing through its link.
                Os.rename(pending.getAbsolutePath(), target.getAbsolutePath());
            } finally { pending.delete(); }
        }
        Os.setenv("MDO_EDITION", metadata.getString("edition"), true);
        Os.setenv("MDO_BUILD_ID", metadata.getString("build_id"), true);
        Os.setenv("MDO_NATIVE_DIR", nativeDir, true);
        if (metadata.getString("edition").equals("full")) {
            Os.setenv("MDO_TOOLS_ROOT", root.getAbsolutePath(), true);
            Os.setenv("MDO_PYTHON_HOME", new File(root, "python").getAbsolutePath(), true);
            Os.setenv("PYTHONHOME", new File(root, "python").getAbsolutePath(), true);
            Os.setenv("LD_LIBRARY_PATH", nativeDir, true);
            Os.setenv("SSL_CERT_FILE", new File(root, "curl/cacert.pem").getAbsolutePath(), true);
            Os.setenv("CURL_CA_BUNDLE", new File(root, "curl/cacert.pem").getAbsolutePath(), true);
            Os.setenv("PATH", new File(root, "busybox") + ":" + new File(root, "curl") + ":" +
                new File(root, "jq") + ":" + new File(root, "openssh") + ":" + new File(root, "python/bin") + ":" + System.getenv("PATH"), true);
        }
    }
}
