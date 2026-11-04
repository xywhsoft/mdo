"""Add isolated content WebViews without exposing a Java object to iframes."""
from pathlib import Path
import shutil

def apply(directory: Path):
    source=directory/'org/xserver/android/XsActivity.java'
    text=source.read_text(encoding='utf-8')
    edits={
      'private WebView web;':'private WebView web;\n    private MdoBrowser mdoBrowser;',
      'web = new WebView(this);':'web = new WebView(this);\n        mdoBrowser = new MdoBrowser(this, root, web);',
      '@JavascriptInterface public String languages() { return languages; }':'''@JavascriptInterface public String languages() { return languages; }
        @JavascriptInterface public void requestStorageAccess() {
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed() || rendererLost || web.getUrl() == null ||
                    !local(Uri.parse(web.getUrl()))) return;
                if (Build.VERSION.SDK_INT >= 30) {
                    Intent request = new Intent("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION",
                        Uri.parse("package:" + getPackageName()));
                    if (request.resolveActivity(getPackageManager()) == null)
                        request = new Intent("android.settings.MANAGE_ALL_FILES_ACCESS_PERMISSION");
                    if (request.resolveActivity(getPackageManager()) != null) startActivity(request);
                } else requestPermissions(new String[]{"android.permission.READ_EXTERNAL_STORAGE",
                    "android.permission.WRITE_EXTERNAL_STORAGE"}, 2092);
            });
        }''',
      'if (!local(Uri.parse(url))) return;':'if (!local(Uri.parse(url))) return;\n                mdoBrowser.attach(url);',
      '@Override protected void onDestroy() {':'@Override protected void onDestroy() {\n        if (mdoBrowser != null) mdoBrowser.destroy();',
    }
    for old,new in edits.items():
        if text.count(old)!=1:raise ValueError('Pinned Android browser hook changed: '+old)
        text=text.replace(old,new)
    source.write_text(text,encoding='utf-8')
    shutil.copy2(Path(__file__).with_name('MdoBrowser.java'),source.with_name('MdoBrowser.java'))
