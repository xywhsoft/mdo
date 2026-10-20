package org.xserver.android;

import java.io.*;
import java.nio.file.*;
import java.security.MessageDigest;

/** Bounded private inbox IO; no Android dependency, also exercised on a real JVM. */
final class MdoShareFiles {
    static final long FILE_LIMIT = 64L * 1024 * 1024;
    static final long BATCH_LIMIT = 128L * 1024 * 1024;
    static final long INBOX_LIMIT = 256L * 1024 * 1024;
    static final int FILE_COUNT = 32, BATCH_COUNT = 8;
    // JSON escaping can expand otherwise valid 64 KiB draft text sixfold.
    static final int MANIFEST_LIMIT = 1200000, PLAN_LIMIT = 1100000;

    static boolean id(String value) { return value != null && value.matches("[0-9a-f]{32}"); }
    static File child(File root, String name) throws IOException {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals("..") ||
            name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || name.indexOf('\0') >= 0)
            throw new IOException("Invalid inbox name");
        File file = new File(root, name);
        if (Files.isSymbolicLink(file.toPath()) || !file.getCanonicalFile().getParentFile().equals(root.getCanonicalFile()))
            throw new IOException("Inbox link is not allowed");
        return file;
    }
    static String filename(String value) {
        String name = value == null ? "shared-file" : value.replace('\\', '/');
        name = name.substring(name.lastIndexOf('/') + 1).replaceAll("[\\p{Cntrl}:<>\"|?*]", "_").trim();
        if (name.isEmpty() || name.equals(".") || name.equals("..")) name = "shared-file";
        int dot = name.lastIndexOf('.');
        String extension = dot > 0 && name.length()-dot <= 16 ? name.substring(dot) : "";
        String stem = extension.isEmpty() ? name : name.substring(0, dot);
        StringBuilder prefix = new StringBuilder();
        int available = 180-extension.getBytes(java.nio.charset.StandardCharsets.UTF_8).length;
        for (int i=0;i<stem.length();) {
            int code = stem.codePointAt(i), bytes = code < 128 ? 1 : code < 2048 ? 2 : code < 65536 ? 3 : 4;
            if (bytes > available) break;
            prefix.appendCodePoint(code); available -= bytes; i += Character.charCount(code);
        }
        return prefix.toString()+extension;
    }
    static String copy(InputStream input, File target, long limit) throws Exception {
        if (limit < 0 || limit > FILE_LIMIT || Files.exists(target.toPath(), LinkOption.NOFOLLOW_LINKS))
            throw new IOException("Invalid share file or limit");
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        long total = 0, deadline = System.nanoTime() + 60_000_000_000L;
        boolean complete = false;
        try (FileOutputStream output = new FileOutputStream(target)) {
            byte[] buffer = new byte[65536]; int size;
            while ((size = input.read(buffer)) != -1) {
                if (total > limit - size || Thread.currentThread().isInterrupted() || System.nanoTime() > deadline)
                    throw new IOException("Share exceeds its size or time limit");
                if (size == 0) continue;
                output.write(buffer, 0, size); digest.update(buffer, 0, size); total += size;
            }
            output.getFD().sync(); complete = true;
        } finally { if (!complete) Files.deleteIfExists(target.toPath()); }
        StringBuilder hash = new StringBuilder();
        for (byte b : digest.digest()) hash.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
        return hash.toString();
    }
    static void atomic(File root, String name, String text) throws IOException {
        File target = child(root, name), pending = File.createTempFile(".manifest-", ".part", root);
        try {
            try (FileOutputStream output = new FileOutputStream(pending)) {
                output.write(text.getBytes(java.nio.charset.StandardCharsets.UTF_8)); output.getFD().sync();
            }
            Files.move(pending.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } finally { Files.deleteIfExists(pending.toPath()); }
    }
    static String text(File file, int maximum) throws IOException {
        if (!Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS) || file.length() > maximum)
            throw new IOException("Invalid inbox metadata");
        byte[] bytes = Files.readAllBytes(file.toPath());
        if (bytes.length > maximum) throw new IOException("Inbox metadata is too large");
        return new String(bytes, java.nio.charset.StandardCharsets.UTF_8);
    }
    static long bytes(File directory) throws IOException {
        long total = 0;
        File[] files = directory.listFiles();
        if (files != null) for (File file : files) {
            if (Files.isSymbolicLink(file.toPath())) throw new IOException("Unexpected inbox link");
            total += file.isDirectory() ? bytes(file) : file.length();
            if (total > INBOX_LIMIT) break;
        }
        return total;
    }
    static void remove(File directory) throws IOException {
        if (Files.isSymbolicLink(directory.toPath())) throw new IOException("Unexpected inbox link");
        File[] files = directory.listFiles();
        if (files != null) for (File file : files) remove(file);
        Files.deleteIfExists(directory.toPath());
    }
}
