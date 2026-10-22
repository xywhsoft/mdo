"""Every product route must remain reachable from the public SDK directory."""
import re
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import application_sdk

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
