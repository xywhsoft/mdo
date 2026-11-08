"""Add isolated content WebViews without exposing a Java object to iframes."""
from pathlib import Path
import shutil

def manifest(package: str) -> str:
    import re
    if not re.fullmatch(r'[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)+', package):
        raise ValueError('Invalid private upload provider package')
    return (f'<provider android:name="org.xserver.android.MdoBrowserUploadProvider" '
            f'android:authorities="{package}.mdo.browseruse.upload" '
            'android:exported="false" android:grantUriPermissions="false" />')


def apply(directory: Path):
    source=directory/'org/xserver/android/XsActivity.java'
    for name in ('MdoBrowserCdpNative.java','MdoBrowserCdpJson.java','MdoBrowserCdpResponse.java','MdoBrowserCdpSession.java','MdoBrowserCdpMarker.java','MdoBrowserCdpView.java','MdoBrowserCdpBinding.java','MdoBrowserCdpNetworkRequests.java','MdoBrowserCdpNetworkFrames.java','MdoBrowserCdpRegistry.java','MdoBrowserCdpWorker.java','MdoBrowserCdpNetworks.java','MdoBrowserCdpPopupBroker.java','MdoBrowserCdpPopupWorker.java','MdoBrowserCdpPopupNative.java','MdoBrowserCdpPopupSource.java','MdoBrowserCdpPopupPreparation.java','MdoBrowserCdpFrames.java','MdoBrowserCdpWorlds.java','MdoBrowserCdpOwnerScripts.java','MdoBrowserCdpFrameRequest.java','MdoBrowserCdpFrameChain.java','MdoBrowserCdpInput.java','MdoBrowserCdpCapture.java','MdoBrowserCdpFileInput.java','MdoBrowserCdpChoosers.java','MdoBrowserCdpUploadScripts.java','MdoBrowserCdpUpload.java','MdoBrowserUploadCoordinator.java','MdoBrowserPrivatePicker.java','MdoBrowserPrivatePickerReply.java'):
        shutil.copy2(Path(__file__).with_name(name),source.with_name(name))
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
      '@Override public void onPageFinished(WebView view, String url) {':'@Override public void onPageStarted(WebView view, String url, android.graphics.Bitmap icon) { if (view == web && mdoBrowser != null) mdoBrowser.detach(); }\n            @Override public void onPageFinished(WebView view, String url) {',
      '@Override public boolean onRenderProcessGone(WebView view, RenderProcessGoneDetail detail) {':'@Override public boolean onRenderProcessGone(WebView view, RenderProcessGoneDetail detail) { if (view == web && mdoBrowser != null) mdoBrowser.detach();',
      '@Override protected void onPause() {':'@Override protected void onPause() { if (mdoBrowser != null) mdoBrowser.pause();',
      '@Override protected void onResume() {':'@Override protected void onResume() { if (mdoBrowser != null) mdoBrowser.resume();',
      '@Override protected void onDestroy() {':'@Override protected void onDestroy() {\n        if (mdoBrowser != null) mdoBrowser.destroy();',
      'mdoBrowser = new MdoBrowser(this, root, web);':'mdoBrowser = new MdoBrowser(this, root, web);\n        mdoBrowser.manualChooserRestore(saved);',
      'super.onActivityResult(request, result, data);':'super.onActivityResult(request, result, data);\n        if (mdoBrowser != null && mdoBrowser.manualChooserResult(request, result, data)) return;',
      'callback.onReceiveValue(FileChooserResult.parse(result, data, chooserMultiple));':'callback.onReceiveValue(MdoBrowserPickerUris.read(XsActivity.this, result, data, chooserMultiple));',
      '                if (chooser != null) chooser.onReceiveValue(null);\n                chooser = callback;\n                chooserMultiple = params.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE;\n                Intent intent = params.createIntent().addCategory(Intent.CATEGORY_OPENABLE)\n                    .putExtra(Intent.EXTRA_ALLOW_MULTIPLE, chooserMultiple);\n                try { startActivityForResult(intent, PICK_FILE); } catch (ActivityNotFoundException failure) { chooser.onReceiveValue(null); chooser = null; }\n                return true;':'                if (mdoBrowser != null) {\n                    Boolean mdoPicked = mdoBrowser.openHostChooser(view, callback, params);\n                    if (mdoPicked != null) return mdoPicked;\n                }\n                if (chooser != null) chooser.onReceiveValue(null);\n                chooser = callback;\n                chooserMultiple = params.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE;\n                Intent intent = params.createIntent().addCategory(Intent.CATEGORY_OPENABLE)\n                    .putExtra(Intent.EXTRA_ALLOW_MULTIPLE, chooserMultiple);\n                try { startActivityForResult(intent, PICK_FILE); } catch (ActivityNotFoundException failure) { chooser.onReceiveValue(null); chooser = null; }\n                return true;',
      '@Override protected void onSaveInstanceState(Bundle state) {':'@Override protected void onSaveInstanceState(Bundle state) {\n        if (mdoBrowser != null) mdoBrowser.manualChooserSave(state);',
    }
    for old,new in edits.items():
        if text.count(old)!=1:raise ValueError('Pinned Android browser hook changed: '+old)
        text=text.replace(old,new)
    source.write_text(text,encoding='utf-8')
    shutil.copy2(Path(__file__).with_name('MdoBrowser.java'),source.with_name('MdoBrowser.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserWorld.java'),source.with_name('MdoBrowserWorld.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserProfile.java'),source.with_name('MdoBrowserProfile.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserInput.java'),source.with_name('MdoBrowserInput.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserInputTarget.java'),source.with_name('MdoBrowserInputTarget.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserLifecycle.java'),source.with_name('MdoBrowserLifecycle.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserChannel.java'),source.with_name('MdoBrowserChannel.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserDocument.java'),source.with_name('MdoBrowserDocument.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserNativeInput.java'),source.with_name('MdoBrowserNativeInput.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserWheel.java'),source.with_name('MdoBrowserWheel.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserCapture.java'),source.with_name('MdoBrowserCapture.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserCaptureBuffer.java'),source.with_name('MdoBrowserCaptureBuffer.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserCaptureRuntime.java'),source.with_name('MdoBrowserCaptureRuntime.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserCaptureTarget.java'),source.with_name('MdoBrowserCaptureTarget.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserNativeCapture.java'),source.with_name('MdoBrowserNativeCapture.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserDialogs.java'),source.with_name('MdoBrowserDialogs.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserDialogTarget.java'),source.with_name('MdoBrowserDialogTarget.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserPopups.java'),source.with_name('MdoBrowserPopups.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserWindowTarget.java'),source.with_name('MdoBrowserWindowTarget.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserChoosers.java'),source.with_name('MdoBrowserChoosers.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserChooserTarget.java'),source.with_name('MdoBrowserChooserTarget.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserUploadFiles.java'),source.with_name('MdoBrowserUploadFiles.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserDownloadFiles.java'),source.with_name('MdoBrowserDownloadFiles.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserDownloadQueue.java'),source.with_name('MdoBrowserDownloadQueue.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserCdpDownloads.java'),source.with_name('MdoBrowserCdpDownloads.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserDownloadRuntime.java'),source.with_name('MdoBrowserDownloadRuntime.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserUploadStore.java'),source.with_name('MdoBrowserUploadStore.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserUploadQueue.java'),source.with_name('MdoBrowserUploadQueue.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserUploadRuntime.java'),source.with_name('MdoBrowserUploadRuntime.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserUploadUris.java'),source.with_name('MdoBrowserUploadUris.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserUploadProxy.java'),source.with_name('MdoBrowserUploadProxy.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserUploadUriRuntime.java'),source.with_name('MdoBrowserUploadUriRuntime.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserUploadProvider.java'),source.with_name('MdoBrowserUploadProvider.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserManualChooser.java'),source.with_name('MdoBrowserManualChooser.java'))
    shutil.copy2(Path(__file__).with_name('MdoBrowserPickerUris.java'),source.with_name('MdoBrowserPickerUris.java'))
