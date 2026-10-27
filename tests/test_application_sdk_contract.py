"""Every product route must remain reachable from the public SDK directory."""
import re
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import application_sdk
import application_contracts

class ApplicationSdkContractTests(unittest.TestCase):
    def test_directory_covers_every_product_route_and_mutation(self):
        records=application_sdk.services(ROOT)
        source=(ROOT/'app/src/api/router.c').read_text(encoding='utf-8')
        source=source[source.index('g_MdoApiRoutes[]'):source.index('static bool MdoApiViewEqualText')]
        paths=set(re.findall(r'"(/api/v1/[^"\n]+)"',source))
        self.assertEqual({r['path'] for r in records},paths)
        self.assertGreater(len(records),200)
        self.assertEqual(len(records),len({r['id'] for r in records}))
        self.assertTrue({'POST','PATCH','PUT','DELETE','GET'}<={r['method'] for r in records})
        self.assertIn('/api/v1/update/install',paths)
        self.assertIn('/api/v1/account/login',paths)
        self.assertIn('/api/v1/device-transfers',paths)

    def test_aliases_are_backed_by_an_actual_product_route(self):
        rows={r['id']:r for r in application_sdk.services(ROOT)}
        for alias,pair in application_sdk.ALIASES.items():
            self.assertEqual((rows[alias]['method'],rows[alias]['path']),pair)

    def test_native_exports_cover_product_api_domains(self):
        symbols=dict(application_sdk.native_symbols(ROOT))
        self.assertGreater(len(symbols),500)
        for name in ('MdoModelClientCreate','MdoAgentSessionCreateWithRuntime','MdoSessionCreate',
                     'MdoSkillCatalogSnapshot','MdoRemoteSnapshot','MdoApprovalOnPermission'):
            self.assertIn(name,symbols)

    def test_every_native_service_has_parameter_discovery_metadata(self):
        names=set()
        for source in (ROOT/'app/src/application').glob('*.c'):
            text=source.read_text(encoding='utf-8')
            for block in re.findall(r'Sdk(?:Builtins|RuntimeMethods|JobMethods|StateMethods|StreamMethods|TerminalMethods|PluginMethods|UiMethods|WorkbenchMethods)\[\]\s*=\s*\{(.*?)\};',text,re.S):
                names.update(re.findall(r'"([a-z]+\.[a-z._]+)"',block))
        self.assertGreater(len(names),50)
        self.assertEqual(names,set(application_sdk.native_contracts()))

    def test_product_contracts_cover_all_routes_with_resolvable_typed_references(self):
        def visit(value,definitions):
            if isinstance(value,dict):
                self.assertTrue(value,'An empty schema conceals the wire contract')
                if '$ref' in value:self.assertIn(value['$ref'].removeprefix('#/$defs/'),definitions)
                for key,child in value.items():
                    # Empty properties/required are valid for a bodyless call.
                    if key not in ('properties','$defs'):visit(child,definitions)
                    elif isinstance(child,dict):
                        for item in child.values():visit(item,definitions)
            elif isinstance(value,list):
                for child in value:visit(child,definitions)
        for row in application_sdk.services(ROOT):
            for schema in application_contracts.contract(row):visit(schema,schema.get('$defs',{}))
        with self.assertRaisesRegex(ValueError,'no SDK contract'):
            application_contracts.contract({'handler':'MdoApiUnknownRoute','path':'/api/v1/new','method':'POST'})

    def test_business_fields_binary_bodies_and_context_fallback_are_described(self):
        rows={row['id']:row for row in application_sdk.services(ROOT)}
        project,_=application_contracts.contract(rows['project.create'])
        self.assertEqual(project['$defs']['ProjectCreateInput']['required'],['id','name'])
        self.assertIn('workspace_root',project['$defs']['ProjectCreateInput']['properties'])
        session,_=application_contracts.contract(rows['session.get'])
        self.assertEqual(session['properties']['path']['required'],[])
        schedule,_=application_contracts.contract(rows['product.schedules.post'])
        self.assertIn('timezone',schedule['$defs']['ScheduleInput']['properties'])
        image,_=application_contracts.contract(rows['product.projects.by_project.sessions.by_session.attachments.post'])
        self.assertIn('body_base64',image['properties']);self.assertNotIn('body',image['properties'])
        queue,_=application_contracts.contract(rows['product.projects.by_project.sessions.by_session.queue.post'])
        self.assertEqual(queue['$defs']['QueueInput']['properties']['priority']['type'],'boolean')
        preview,_=application_contracts.contract(rows['product.session_backups.uploads.by_upload.preview.post'])
        self.assertNotIn('body',preview['properties'])
        account,_=application_contracts.contract(rows['account.logout'])
        self.assertEqual(account['$defs']['EmptyInput']['properties'],{})
        self.assertIn('body',account['required'])

    def test_project_selection_and_directory_distribution_contracts(self):
        rows={row['id']:row for row in application_sdk.services(ROOT)}
        for name in ('product.commands.project.get','product.commands.project.by_command.enabled.post','product.skills.project.by_id.enabled.post'):
            schema,_=application_contracts.contract(rows[name])
            self.assertIn('project',schema['properties']['query']['properties'])
        self.assertNotIn('product.commands.project.by_command.put',rows)
        self.assertNotIn('product.skills.project.by_id.put',rows)
        schema,_=application_contracts.contract(rows['product.ecosystem.post'])
        body=schema['$defs']['EcosystemInput']['properties']
        self.assertIn('task',body);self.assertIn('version_id',body)
        self.assertNotIn('files',body);self.assertNotIn('package',body)
        self.assertFalse(schema['$defs']['EcosystemInput']['additionalProperties'])

    def test_extension_archives_use_file_backed_raw_sdk_bodies(self):
        rows={row['id']:row for row in application_sdk.services(ROOT)}
        upload,_=application_contracts.contract(rows['product.extension_tasks.by_task.chunks.by_offset.put'])
        self.assertIn('body_file',upload['required']);self.assertIn('body_bytes',upload['required'])
        self.assertNotIn('body_base64',upload['properties'])
        download,result=application_contracts.contract(rows['product.extension_tasks.by_task.file.by_offset.get'])
        self.assertIn('response_file',download['required']);self.assertNotIn('base64',result['properties'])
        methods=application_sdk.native_contracts()
        self.assertNotIn('plugin.apply',methods);self.assertNotIn('plugin.rollback',methods)
        self.assertIn('extension.create',methods);self.assertIn('extension.remove',methods)


