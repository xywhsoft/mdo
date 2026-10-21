package org.xserver.android;

import java.io.File;
import java.io.IOException;

/** mdo's APK file policy, shared by initial validation and the install recheck. */
final class MdoPackageInstallPolicy {
    // Must match MDO_UPDATE_LIMIT in app/src/update/manager.c. Full APKs include
    // local tools and already exceed the SDK installer's old 32 MiB ceiling.
    static final long MAX_BYTES = 512L * 1024 * 1024;

    static File validateFile(File filesDirectory, File candidate) throws IOException {
        if (filesDirectory == null || candidate == null)
            throw new IOException("更新文件路径缺失，请重新下载");
        File root = filesDirectory.getCanonicalFile();
        File file = candidate.getCanonicalFile();
        if (!file.getPath().startsWith(root.getPath() + File.separator))
            throw new IOException("更新文件不在应用私有目录中，请重新下载");
        if (!file.isFile())
            throw new IOException("更新文件不存在或不是普通文件，请重新下载");
        long bytes = file.length();
        if (bytes <= 0)
            throw new IOException("更新文件为空，请重新下载");
        if (bytes > MAX_BYTES)
            throw new IOException("更新文件超过 512 MiB 上限，无法安装");
        return file;
    }

    private MdoPackageInstallPolicy() {}
}
