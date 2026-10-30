"""Small ordinary-directory archives for platform format validation tests."""
import json
import zipfile
from mcp_template_fixture import template


def directory_zip(path, kind, *, alias='zip-fixture', version=None):
    prefix={'agents':'agent','subagents':'subagent','tools':'tool','skills':'skill','mcp':'mcp','external-tools':'external-tool','commands':'command','plugins':'plugin','knowledge':'knowledge'}[kind]
    meta = dict(kind=kind, id=prefix+':qa.'+alias.lower(), name='ZIP store fixture', alias=alias, platforms=['any'])
    files = {}
    if kind in ('agents', 'subagents'):
        entry = 'agent.json'
        files[entry] = json.dumps(dict(schema='mdo.agent.v1', name='Fixture role', description='Archive fixture', mode='primary' if kind == 'agents' else 'subagent', extends='mdo.general', tools='inherit', prompt='Read the supplied task.'))
    elif kind == 'skills':
        entry = 'SKILL.md'
        files[entry] = f'---\nname: {alias}\ndescription: Archive fixture\n---\nRead the reference.\n'
    elif kind in ('commands', 'tools'):
        entry = 'command.c' if kind == 'commands' else 'tool.c'
        files[entry] = '/* mdo-command '+json.dumps(dict(id=alias, description='Archive fixture'))+' */\n' if kind == 'commands' else '/* C tool source; never executed by archive inspection */\n'
    elif kind == 'mcp':
        entry = 'mcp.json'
        recipe = template(alias); recipe['variants'][0]['platforms'] = ['any']
        files[entry] = json.dumps(recipe)
    elif kind == 'knowledge':
        entry = 'knowledge.json'
        files[entry] = json.dumps(dict(format='mdo.knowledge.v1', name='Reference', description='Archive fixture', index='index.md', prompt='Read {$path}/index.md.', inject_prompt=True))
        files['index.md'] = '# Reference\n'
    elif kind == 'external-tools':
        entry = 'tool.json'
        files[entry] = json.dumps(dict(program='bin/program', command='fixture', prompt='Use this reference program.'))
        files['bin/program'] = b'Inert binary resource\0'
    elif kind == 'plugins':
        entry = 'plugin.json'
        files[entry] = json.dumps(dict(entry=dict(frontend='main.js'), overrides=[], exclusive=[]))
        files['main.js'] = 'export function activate() { return () => {}; }\n'
        meta['frontend'] = 'main.js'
    else:
        raise ValueError(kind)
    meta['entry'] = entry
    if version is not None: meta['version'] = version
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('extension.json', json.dumps(meta))
        for name, data in files.items(): archive.writestr(name, data)
        archive.writestr('.hidden/deep/data.bin', bytes(range(256))*7)
        archive.writestr('empty/', b'')


def platform_store_source(directory, sqlite_header='/app/qa/sqlite3.h'):
    source = (directory/'directory_store.inc.c').read_text(encoding='utf-8')
    for name in ('directory_content.inc.c', 'directory_lifecycle.inc.c', 'directory_versions.inc.c', 'directory_dependencies.inc.c'):
        source = source.replace(f'#include "{name}"', (directory/name).read_text(encoding='utf-8'))
    for name in ('extension_manifest.h', 'extension_graph.h', 'agent_config_format.h', 'knowledge_format.h', 'mcp_template.h', 'external_tool.h', 'plugin_format.h'):
        source = source.replace(f'#include "{name}"', f'#include "/app/include/mdo/{name}"')
    if sqlite_header: source = source.replace('#include <sqlite3.h>', f'#include "{sqlite_header}"')
    return source
