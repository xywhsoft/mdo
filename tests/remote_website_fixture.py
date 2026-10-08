"""Shared disposable website for the unified mdo device-relay tests.

The maintained website and xadmin database are never modified. The delivery
fixture owns its copy, activates the unified plugin offline, disables outbound
mail, and supplies a disposable administrator. No public request is needed.
"""
from pathlib import Path
import json
import os
import sys

ROOT = Path(__file__).resolve().parents[1]
HOME = ROOT.parent / 'home'
WEBSITE_HOST = Path(os.environ.get('XS_TEST_EXE',str(HOME/'xs.exe')))
sys.path.insert(0,str(HOME/'tests'))
from test_mdo_delivery import fixture


def remote_website_fixture():
    _, website, port, user, password = fixture('mdo-remote-native-' + os.urandom(4).hex())
    # Account-isolation checks need two local members. Registration throttling
    # is covered by xadmin's own tests, not this disposable relay fixture.
    path = website/'options/global.json'
    options = json.loads(path.read_text(encoding='utf-8-sig'))
    found = False
    for group in options.get('classList',[]):
        for option in group.get('options',[]):
            if option.get('name') == 'registerIntervalSecond':
                option['value'] = '0'; found = True
    if not found:
        options.setdefault('classList',[]).append({'title':'Remote fixture','options':[
            {'name':'registerIntervalSecond','value':'0','title':'Local fixture registration','type':'text'}]})
    path.write_text(json.dumps(options,ensure_ascii=False),encoding='utf-8')
    (website/'db/identity.json').write_text(
        json.dumps({'public_origin':f'http://127.0.0.1:{port}'}),encoding='utf-8')
    return website, port, user, password
