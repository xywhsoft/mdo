"""Offline migration preserves private data and recovers partial writes."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('dependency_migration',ROOT/'tools/migrate_extension_dependencies.py')
migration=importlib.util.module_from_spec(spec);spec.loader.exec_module(migration)


class MigrationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mdo-dependency-migration-');self.addCleanup(self.temp.cleanup);self.home=Path(self.temp.name);self.ids={}

    def extension(self,kind,name,entry='entry.json',content=None):
        root=self.home/kind/name;root.mkdir(parents=True);manifest=dict(kind=kind,name=name,alias=name,entry=entry,platforms=['any']);(root/'extension.json').write_bytes(migration.encoded(manifest));(root/entry).write_bytes(migration.encoded(content or {}));self.ids[kind+'/'+name]=migration.PREFIX[kind]+':qa.'+name;return root

    def write(self,path,value):
        path=self.home/path;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(migration.encoded(value));return path

    def build(self):return migration.Migration(self.home,dict(ids=self.ids)).build()

    def test_missing_mapping_is_an_inert_dry_run(self):
        root=self.extension('skills','review');before=(root/'extension.json').read_bytes();plan=migration.Migration(self.home).build();self.assertFalse(plan.report()['ready']);self.assertEqual(before,(root/'extension.json').read_bytes())

    def test_roles_connections_and_project_selections_keep_their_identity(self):
        self.extension('skills','review');self.extension('mcp','repo');self.extension('agents','base');self.extension('agents','reviewer',content=dict(extends='agent.base',skills=['review'],mcp_servers=['private-account']))
        private=self.write('data/mcp/connections/private-account.json',dict(id='private-account',extension_id='repo',variant='default',values=dict(token='secret-ref:never-print'),enabled=True,approval_digest='a'*64))
        self.write('workspace/.mdo/skills.json',dict(review=False));plan=self.build();report=plan.report();self.assertTrue(report['ready'],report);self.assertNotIn('secret-ref:never-print',json.dumps(report));original=private.read_bytes()
        with migration.home_lease(self.home):backup=plan.apply(report['plan_sha256'])
        document=json.loads(private.read_bytes());self.assertEqual(document['extension_id'],'mcp:qa.repo');self.assertEqual(document['values']['token'],'secret-ref:never-print');self.assertEqual(document['approval_digest'],'')
        role=json.loads((self.home/'agents/reviewer/entry.json').read_bytes());self.assertEqual(role['extends'],'agent:qa.base');self.assertEqual(role['skills'],[dict(ref='skill:qa.review')]);self.assertEqual(role['mcp_servers'],[dict(ref='mcp:qa.repo')])
        bindings=json.loads((self.home/'data/extensions/bindings.json').read_bytes());self.assertEqual(bindings['agent:qa.reviewer']['global']['mcp:qa.repo'],'private-account');self.assertEqual(json.loads((self.home/'workspace/.mdo/skills.json').read_bytes()),{'skill:qa.review':False});self.assertTrue(any(path.read_bytes()==original for path in backup.glob('*.before')))

    def test_plugin_private_relations_move_to_the_common_manifest(self):
        self.extension('plugins','base');self.extension('plugins','other');root=self.extension('plugins','feature',content=dict(entry=dict(frontend='ui.js'),requires=[dict(id='base',reason='foundation')],conflicts=[dict(id='other')]))
        plan=self.build();self.assertTrue(plan.report()['ready']);plan.apply(plan.report()['plan_sha256']);manifest=json.loads((root/'extension.json').read_bytes());self.assertEqual(manifest['dependencies'][0]['id'],'plugin:qa.base');self.assertEqual(manifest['conflicts'][0]['id'],'plugin:qa.other');self.assertNotIn('requires',json.loads((root/'entry.json').read_bytes()))

    def test_managed_archive_files_are_not_rewritten(self):
        root=self.extension('skills','published');before=(root/'extension.json').read_bytes();self.write('data/extensions/state.json',{'skills:published':dict(source='store',xid='work',sha256='a'*64)})
        plan=self.build();self.assertTrue(plan.report()['ready']);self.assertEqual(plan.report()['managed'][0]['action'],'reinstall_verified_store_work');plan.apply(plan.report()['plan_sha256']);self.assertEqual(before,(root/'extension.json').read_bytes())

    def test_duplicate_ids_and_identity_changes_are_rejected(self):
        first=self.extension('skills','first');self.extension('skills','second');self.ids['skills/second']=self.ids['skills/first'];self.assertFalse(self.build().report()['ready'])
        document=json.loads((first/'extension.json').read_bytes());document['id']='skill:qa.original';(first/'extension.json').write_bytes(migration.encoded(document));self.assertFalse(self.build().report()['ready'])

    def test_knowledge_cannot_depend_on_executable_extensions(self):
        self.extension('knowledge','manual');plan=migration.Migration(self.home,dict(ids=self.ids,dependencies={'knowledge/manual':[dict(id='tool:qa.program')]})).build();self.assertFalse(plan.report()['ready'])

    def test_optional_dependencies_are_preserved_and_constraints_are_not_silently_dropped(self):
        root=self.extension('skills','review');plan=migration.Migration(self.home,dict(ids=self.ids,dependencies={'skills/review':[dict(id='knowledge:qa.reference',optional=True,reason='Extra reference')]})).build();self.assertTrue(plan.report()['ready']);manifest=json.loads(plan.changes[root/'extension.json']['after']);self.assertTrue(manifest['dependencies'][0]['optional'])
        self.extension('plugins','base');self.extension('plugins','feature',content=dict(requires=[dict(id='base',version='>=2')]))
        plan=self.build();self.assertFalse(plan.report()['ready']);self.assertTrue(any('constraints' in error for error in plan.report()['errors']))

    def test_relation_shape_and_selected_optional_capabilities_are_rejected(self):
        root=self.extension('skills','review');document=json.loads((root/'extension.json').read_bytes());document['dependencies']=[dict(id='knowledge:qa.reference',optional='true')];(root/'extension.json').write_bytes(migration.encoded(document));self.assertFalse(self.build().report()['ready'])
        document['dependencies']=['knowledge:qa.reference'];(root/'extension.json').write_bytes(migration.encoded(document));plan=migration.Migration(self.home,dict(ids=self.ids,dependencies={'skills/review':[dict(id='knowledge:qa.reference')]})).build();self.assertFalse(plan.report()['ready'])

    def test_stale_review_cannot_authorize_new_writes(self):
        root=self.extension('skills','review');plan=self.build();expected=plan.report()['plan_sha256'];(root/'extension.json').write_bytes(b'{"changed":true}')
        with self.assertRaises(RuntimeError):plan.apply(expected)
        self.assertEqual((root/'extension.json').read_bytes(),b'{"changed":true}')

    def test_failure_after_a_replacement_restores_every_file(self):
        self.extension('skills','review');plan=self.build();before={path:change['before'] for path,change in plan.changes.items()};actual=migration.atomic;failed=False
        def interrupted(path,data,mode=0o600):
            nonlocal failed
            if Path(path)==self.home/'data/extensions/state.json' and not failed:failed=True;raise OSError('injected storage failure')
            actual(path,data,mode)
        with patch.object(migration,'atomic',interrupted):
            with self.assertRaises(OSError):plan.apply(plan.report()['plan_sha256'])
        for path,content in before.items():self.assertEqual(path.read_bytes() if path.exists() else None,content)
        self.assertEqual(json.loads(next((self.home/'data/extension-migrations').glob('*/journal.json')).read_bytes())['phase'],'rolledback')

    def test_home_lease_blocks_another_writer(self):
        self.extension('skills','review');mapping=self.write('mapping.json',dict(ids=self.ids));plan=self.build()
        with migration.home_lease(self.home):result=subprocess.run([sys.executable,str(ROOT/'tools/migrate_extension_dependencies.py'),'--home',str(self.home),'--map',str(mapping),'--apply','--expected-plan',plan.report()['plan_sha256']],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0);self.assertIn('Close mdo',result.stderr)

    def test_links_are_not_followed(self):
        outside=self.home/'outside.json';outside.write_text('{}');(self.home/'skills/linked').mkdir(parents=True)
        try:(self.home/'skills/linked/extension.json').symlink_to(outside)
        except OSError:self.skipTest('Symlink creation is unavailable on this host')
        with self.assertRaises(ValueError):migration.Migration(self.home).build()


if __name__=='__main__':unittest.main()
