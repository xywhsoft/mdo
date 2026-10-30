package org.xserver.android;

import java.io.File;
import java.io.IOException;
import java.util.Locale;

/** mdo's APK file policy, shared by initial validation and the install recheck. */
final class MdoPackageInstallPolicy {
    // Must match MDO_UPDATE_LIMIT in app/src/update/manager.c. Full APKs include
    // local tools and already exceed the SDK installer's old 32 MiB ceiling.
    static final long MAX_BYTES = 512L * 1024 * 1024;

    private static final String[][] COPY = {
        {"更新文件路径缺失，请重新下载", "Update file path is missing. Download again.", "Путь обновления отсутствует. Скачайте файл заново."},
        {"更新文件不在应用私有目录中，请重新下载", "Update file is outside private app storage. Download again.", "Файл обновления вне частного хранилища приложения. Скачайте заново."},
        {"更新文件不存在或不是普通文件，请重新下载", "Update file is missing or is not a regular file. Download again.", "Файл обновления отсутствует или не является обычным файлом. Скачайте заново."},
        {"更新文件为空，请重新下载", "Update file is empty. Download again.", "Файл обновления пуст. Скачайте заново."},
        {"更新文件超过 512 MiB 上限，无法安装", "Update file exceeds the 512 MiB limit and cannot be installed.", "Файл обновления превышает лимит 512 MiB и не может быть установлен."},
        {"正在验证更新包…", "Verifying update…", "Проверка обновления…"},
        {"安装应用更新", "Install app update", "Установить обновление"},
        {"更新包已通过校验。接下来由安卓系统确认安装，现有数据会保留。", "Update verified. Android will confirm installation; existing data is retained.", "Обновление проверено. Android запросит подтверждение установки; данные сохранятся."},
        {"取消", "Cancel", "Отмена"}, {"继续", "Continue", "Продолжить"},
        {"无法验证安装包签名", "Cannot verify package signature", "Не удалось проверить подпись пакета"},
        {"更新包 hash 不匹配，请重新下载", "Update hash mismatch. Download again.", "Хеш обновления не совпадает. Скачайте заново."},
        {"更新包不属于当前应用", "Update is for a different app", "Обновление предназначено для другого приложения"},
        {"更新包签名与当前应用不同", "Update signature differs from the installed app", "Подпись обновления отличается от установленного приложения"},
        {"安卓系统不允许安装更低 versionCode 的更新包", "Android does not allow an update with a lower versionCode", "Android не разрешает установку обновления с меньшим versionCode"},
        {"未允许安装更新；原应用仍可使用。", "Update installation was not allowed. The existing app is still usable.", "Установка обновления не разрешена. Прежнее приложение доступно."},
        {"正在提交系统安装器…", "Sending to the system installer…", "Передача системному установщику…"},
        {"更新包在安装前发生变化", "Update package changed before installation", "Пакет обновления изменился перед установкой"},
        {"无法安装更新：", "Cannot install update: ", "Не удалось установить обновление: "},
        {"\n原应用仍可使用。", "\nThe existing app is still usable.", "\nПрежнее приложение по-прежнему доступно."},
        {"无法打开系统安装器", "Cannot open the system installer", "Не удалось открыть системный установщик"},
        {"更新未安装；原应用仍可使用。", "Update not installed. The existing app is still usable.", "Обновление не установлено. Прежнее приложение доступно."},
    };

    static String text(String source) { return text(source, Locale.getDefault()); }
    static String text(String source, Locale locale) {
        String language = locale.getLanguage();
        if ("zh".equals(language)) return source;
        int column = "ru".equals(language) ? 2 : 1;
        for (String[] row : COPY) if (row[0].equals(source)) return row[column];
        return source; // Preserve unexpected OS/installer diagnostics verbatim.
    }

    static File validateFile(File filesDirectory, File candidate) throws IOException {
        if (filesDirectory == null || candidate == null)
            throw new IOException(text("更新文件路径缺失，请重新下载"));
        File root = filesDirectory.getCanonicalFile();
        File file = candidate.getCanonicalFile();
        if (!file.getPath().startsWith(root.getPath() + File.separator))
            throw new IOException(text("更新文件不在应用私有目录中，请重新下载"));
        if (!file.isFile())
            throw new IOException(text("更新文件不存在或不是普通文件，请重新下载"));
        long bytes = file.length();
        if (bytes <= 0)
            throw new IOException(text("更新文件为空，请重新下载"));
        if (bytes > MAX_BYTES)
            throw new IOException(text("更新文件超过 512 MiB 上限，无法安装"));
        return file;
    }

    private MdoPackageInstallPolicy() {}
}
