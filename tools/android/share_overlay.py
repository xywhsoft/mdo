"""mdo-owned sharing shell over the pinned xs Android Activity."""
from pathlib import Path
import shutil

MANIFEST = '''<intent-filter>
    <action android:name="android.intent.action.SEND" />
    <action android:name="android.intent.action.SEND_MULTIPLE" />
    <category android:name="android.intent.category.DEFAULT" />
    <data android:mimeType="*/*" />
   </intent-filter>'''


def apply(directory: Path) -> None:
    activity = directory / 'org/xserver/android/XsActivity.java'
    source = activity.read_text(encoding='utf-8')
    if 'private MdoShares mdoShares;' in source:
        raise ValueError('Android sharing overlay is already applied')
    replacements = {
        '    private WebView web;': '    private WebView web;\n    private MdoShares mdoShares;',
        '        receiveAppLink(getIntent());': '''        mdoShares = new MdoShares(this, web);
        web.addJavascriptInterface(mdoShares, "MdoShares");
        mdoShares.receive(getIntent(), saved != null);
        receiveAppLink(getIntent());''',
        '                deliverAppLink();': '                deliverAppLink();\n                mdoShares.ready();',
        'super.onNewIntent(intent); setIntent(intent); receiveAppLink(intent);':
            'super.onNewIntent(intent); setIntent(intent); receiveAppLink(intent); mdoShares.receive(intent, false);',
        '    @Override protected void onDestroy() {':
            '    @Override protected void onDestroy() {\n        if (mdoShares != null) mdoShares.detach();',
    }
    for old, new in replacements.items():
        if source.count(old) != 1:
            raise ValueError('pinned Android share integration point changed: ' + old)
        source = source.replace(old, new)
    activity.write_text(source, encoding='utf-8')
    for name in ('MdoShareFiles.java', 'MdoShares.java'):
        shutil.copy2(Path(__file__).with_name(name), activity.with_name(name))
