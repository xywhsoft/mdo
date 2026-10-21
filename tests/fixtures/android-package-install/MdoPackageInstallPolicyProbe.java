package org.xserver.android;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.file.Files;
import java.nio.file.Path;

public final class MdoPackageInstallPolicyProbe {
    interface Operation { void run() throws Exception; }
    static int cases;
    static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
        cases++;
    }
    static void reject(String reason, Operation operation) throws Exception {
        try { operation.run(); } catch (IOException expected) {
            check(expected.getMessage().contains(reason), "Wrong error: " + expected.getMessage());
            return;
        }
        throw new AssertionError("Invalid update was accepted: " + reason);
    }
    static void resize(Path path, long bytes) throws Exception {
        try (RandomAccessFile file = new RandomAccessFile(path.toFile(), "rw")) { file.setLength(bytes); }
    }
    public static void main(String[] args) throws Exception {
        Path base = new File(args[0]).toPath();
        Path files = Files.createDirectory(base.resolve("files"));
        Path updates = Files.createDirectories(files.resolve("mdo-home/data/update"));
        Path apk = updates.resolve("new.apk");
        // Match the published full APK's exact size. Sparse fixtures do not
        // download models or consume half a GiB of real disk blocks.
        resize(apk, 60977998L);
        check(apk.toFile().length() > 32L * 1024 * 1024,
              "Fixture must trigger the previously shipped rejection");
        check(MdoPackageInstallPolicy.validateFile(files.toFile(), apk.toFile()).equals(apk.toFile()),
              "Current full APK must be accepted under the real nested Home");
        for (long bytes : new long[]{1L, 4220141L, 32L * 1024 * 1024,
                                    32L * 1024 * 1024 + 1L, MdoPackageInstallPolicy.MAX_BYTES}) {
            resize(apk, bytes);
            check(MdoPackageInstallPolicy.validateFile(files.toFile(), apk.toFile()).length() == bytes,
                  "Valid download size rejected: " + bytes);
        }
        resize(apk, MdoPackageInstallPolicy.MAX_BYTES + 1L);
        reject("512 MiB", () -> MdoPackageInstallPolicy.validateFile(files.toFile(), apk.toFile()));
        resize(apk, 0L);
        reject("为空", () -> MdoPackageInstallPolicy.validateFile(files.toFile(), apk.toFile()));
        Files.delete(apk);
        reject("不存在", () -> MdoPackageInstallPolicy.validateFile(files.toFile(), apk.toFile()));
        reject("普通文件", () -> MdoPackageInstallPolicy.validateFile(files.toFile(), updates.toFile()));
        reject("路径缺失", () -> MdoPackageInstallPolicy.validateFile(files.toFile(), null));
        reject("路径缺失", () -> MdoPackageInstallPolicy.validateFile(null, apk.toFile()));

        Path outside = Files.write(base.resolve("outside.apk"), new byte[]{1});
        reject("私有目录", () -> MdoPackageInstallPolicy.validateFile(files.toFile(), outside.toFile()));
        Path sibling = Files.createDirectory(base.resolve("files-extra"));
        Path prefix = Files.write(sibling.resolve("new.apk"), new byte[]{2});
        reject("私有目录", () -> MdoPackageInstallPolicy.validateFile(files.toFile(), prefix.toFile()));
        if (File.separatorChar == '\\') {
            System.out.println("PASS " + cases + " Android installer file-policy cases; symlink cases require Linux/WSL");
            return;
        }
        Files.createSymbolicLink(apk, outside);
        reject("私有目录", () -> MdoPackageInstallPolicy.validateFile(files.toFile(), apk.toFile()));
        Files.delete(apk);
        Files.createSymbolicLink(updates.resolve("escaped"), sibling);
        reject("私有目录", () -> MdoPackageInstallPolicy.validateFile(files.toFile(), updates.resolve("escaped/new.apk").toFile()));
        Path good = Files.write(updates.resolve("valid.apk"), new byte[]{3});
        Files.createSymbolicLink(apk, good);
        check(MdoPackageInstallPolicy.validateFile(files.toFile(), apk.toFile()).equals(good.toFile()),
              "An internal canonical path must remain valid");
        check(Files.readAllBytes(outside)[0] == 1, "Validator must not modify an external file");
        System.out.println("PASS " + cases + " Android installer file-policy cases");
    }
}
