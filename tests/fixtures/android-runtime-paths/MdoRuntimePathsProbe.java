package org.xserver.android;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

public final class MdoRuntimePathsProbe {
    interface Operation { void run() throws Exception; }
    static void check(boolean value, String message) { if (!value) throw new AssertionError(message); }
    static void reject(Operation operation) throws Exception {
        try { operation.run(); } catch (IOException expected) { return; }
        throw new AssertionError("Unsafe path was accepted");
    }
    public static void main(String[] args) throws Exception {
        Path base = new File(args[0]).toPath();
        Path files = Files.createDirectory(base.resolve("files"));
        Path oldNative = Files.createDirectory(base.resolve("old-native"));
        Path newNative = Files.createDirectory(base.resolve("new-native"));
        Path oldTool = Files.write(oldNative.resolve("libmdo_busybox.so"), new byte[]{1, 2});
        Path newTool = Files.write(newNative.resolve("libmdo_busybox.so"), new byte[]{3, 4});
        File root = MdoRuntimePaths.root(files.toFile(), 2);
        File target = MdoRuntimePaths.target(root, "busybox/busybox");
        Files.createSymbolicLink(target.toPath(), oldTool);
        check(!target.getCanonicalPath().startsWith(root.getCanonicalPath() + "/"), "Fixture must reproduce the original false rejection");
        check(MdoRuntimePaths.target(MdoRuntimePaths.root(files.toFile(), 2), "busybox/busybox").equals(target), "Second initialization must accept an installed native link");
        check(MdoRuntimePaths.nativeSource(oldNative.toFile(), "libmdo_busybox.so").toPath().equals(oldTool), "Native source must resolve inside PackageManager's directory");

        Files.delete(oldTool); // APK upgrade removes its previous native directory.
        check(MdoRuntimePaths.target(root, "busybox/busybox").equals(target), "Dangling link must remain replaceable");
        Path pending = target.toPath().resolveSibling("replacement.pending");
        Files.createSymbolicLink(pending, newTool);
        Files.move(pending, target.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        check(target.getCanonicalFile().toPath().equals(newTool), "Replacement must use the new install location");
        check(MdoRuntimePaths.target(root, "busybox/busybox").equals(target), "Restart after upgrade must work");

        for (String relative : new String[]{"", "/tmp/tool", "../tool", "x/../tool", "x//tool", "x/./tool", "x/", "x\\tool", "x\u0000tool", "x\ntool"})
            reject(() -> MdoRuntimePaths.target(root, relative));
        reject(() -> MdoRuntimePaths.root(files.toFile(), 0));
        reject(() -> MdoRuntimePaths.nativeSource(newNative.toFile(), "../libmdo_busybox.so"));
        reject(() -> MdoRuntimePaths.nativeSource(newNative.toFile(), "libmissing.so"));

        Path outside = Files.createDirectory(base.resolve("outside"));
        Files.createSymbolicLink(root.toPath().resolve("escaped"), outside);
        reject(() -> MdoRuntimePaths.target(root, "escaped/tool"));
        reject(() -> MdoRuntimePaths.target(root, "busybox/busybox/child"));
        Path sibling = Files.createDirectory(root.toPath().resolveSibling("2-extra"));
        Files.createSymbolicLink(root.toPath().resolve("prefix-escape"), sibling);
        reject(() -> MdoRuntimePaths.target(root, "prefix-escape/tool"));
        Path outsideFile = Files.write(outside.resolve("liboutside.so"), new byte[]{9});
        Files.createSymbolicLink(newNative.resolve("libescape.so"), outsideFile);
        reject(() -> MdoRuntimePaths.nativeSource(newNative.toFile(), "libescape.so"));
        Path otherFiles = Files.createDirectory(base.resolve("other-files"));
        Files.createSymbolicLink(otherFiles.resolve("mdo-runtime"), outside);
        reject(() -> MdoRuntimePaths.root(otherFiles.toFile(), 2));
        check(Files.readAllBytes(outsideFile)[0] == 9, "Validation must not touch external files");
        System.out.println("PASS first launch, native links on restart, dangling upgrade links and path confinement");
    }
}
