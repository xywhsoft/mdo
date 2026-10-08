package org.xserver.android;

import java.io.File;
import java.io.IOException;

/** Validate runtime directories without following a tool's intentional leaf symlink. */
final class MdoRuntimePaths {
    private MdoRuntimePaths() {}

    static File root(File filesDirectory, int revision) throws IOException {
        if (revision <= 0) throw new IOException("Invalid runtime revision");
        File files = filesDirectory.getCanonicalFile();
        File root = new File(files, "mdo-runtime/" + revision).getCanonicalFile();
        if (!root.getPath().startsWith(files.getPath() + File.separator))
            throw new IOException("Invalid runtime directory");
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Cannot create runtime data");
        return root;
    }

    static File target(File root, String relative) throws IOException {
        if (relative == null || relative.isEmpty() || relative.startsWith("/") || relative.indexOf('\\') >= 0)
            throw new IOException("Invalid runtime path");
        for (int i = 0; i < relative.length(); i++)
            if (relative.charAt(i) < 32 || relative.charAt(i) == 127) throw new IOException("Invalid runtime path");
        for (String component : relative.split("/", -1))
            if (component.isEmpty() || component.equals(".") || component.equals(".."))
                throw new IOException("Invalid runtime path");

        File canonicalRoot = root.getCanonicalFile();
        File target = new File(canonicalRoot, relative);
        File parent = target.getParentFile().getCanonicalFile();
        // The final component may point to PackageManager's native directory,
        // or to its now-removed previous location after an APK update. Only the
        // parent must be confined: rename replaces the leaf without following it.
        if (!parent.equals(canonicalRoot) && !parent.getPath().startsWith(canonicalRoot.getPath() + File.separator))
            throw new IOException("Invalid runtime path");
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create runtime directory");
        return new File(parent, target.getName());
    }

    static File nativeSource(File directory, String name) throws IOException {
        if (name == null || !name.matches("lib[A-Za-z0-9_.-]+\\.so")) throw new IOException("Invalid native runtime name");
        File root = directory.getCanonicalFile();
        File source = new File(root, name).getCanonicalFile();
        if (!source.getPath().startsWith(root.getPath() + File.separator) || !source.isFile())
            throw new IOException("Missing native runtime file");
        return source;
    }
}
