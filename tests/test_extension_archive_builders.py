"""Whole tool-directory packaging and one-time operator conversion."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from extension_archives import package_tool
from convert_legacy_external_archives import convert

def metadata():return dict(id='fixture',name='Fixture tool',version='1.0.0',platform='windows-x86_64',description='For people',prompt='For the model',entry='bin/fixture.exe',license='MIT',copyright='Original authors',links=['https://example.org/source'],commands=[dict(name='fixture',entry='bin/fixture.exe',args=[])])

class ArchiveBuilders(unittest.TestCase):
 def test_whole_directory_resources_and_mode(self):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);stage=root/'stage';(stage/'bin').mkdir(parents=True);(stage/'bin/fixture.exe').write_bytes(b'MZ\0fixture');(stage/'.hidden').mkdir();(stage/'.hidden/resource').write_bytes(b'\0\xffdata');(stage/'empty').mkdir()
   row=package_tool(stage,metadata(),root/'out')
   with zipfile.ZipFile(root/'out'/row['file']) as archive:
    self.assertIsNone(archive.testzip());self.assertIn('empty/',archive.namelist());self.assertEqual(archive.read('.hidden/resource'),b'\0\xffdata');self.assertTrue(archive.getinfo('bin/fixture.exe').external_attr>>16&0o111)
    manifest=json.loads(archive.read('extension.json'));self.assertEqual(manifest['id'],'external-tool:mdo.fixture');self.assertEqual(manifest['alias'],'fixture')
    self.assertEqual(manifest['description'],'For people');self.assertEqual(json.loads(archive.read('tool.json'))['prompt'],'For the model')
   (stage/'Straße.txt').write_bytes(b'one');(stage/'strasse.txt').write_bytes(b'collision')
   with self.assertRaises(ValueError):package_tool(stage,metadata(),root/'out')
 def test_converter_streams_and_preserves_upstream_bytes_and_permissions(self):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);source=root/'old-object';data=b'MZ\0fixture';meta=metadata();meta['files']=[dict(path='bin/fixture.exe',size=len(data),sha256=hashlib.sha256(data).hexdigest(),mode=0o755)];encoded=json.dumps(meta).encode();source.write_bytes(b'MDOCLI1\n'+struct.pack('<I',len(encoded))+encoded+data);before=source.read_bytes()
   row=convert(source,root/'new.zip',hashlib.sha256(before).hexdigest());self.assertEqual(source.read_bytes(),before)
   with zipfile.ZipFile(root/'new.zip') as archive:
    self.assertEqual(archive.read('bin/fixture.exe'),data);self.assertEqual(set(archive.namelist()),{'extension.json','tool.json','bin/fixture.exe'});self.assertEqual(json.loads(archive.read('tool.json'))['program'],'bin/fixture.exe');self.assertIsNone(archive.testzip())
   self.assertEqual(row['sha256'],hashlib.sha256((root/'new.zip').read_bytes()).hexdigest());convert(source,root/'new.zip')
   definitions={'fixture':{'description':'Current human description'}}
   with self.assertRaises(ValueError):convert(source,root/'new.zip',definitions=definitions)
   convert(source,root/'new.zip',definitions=definitions,replace_derived=True)
   with zipfile.ZipFile(root/'new.zip') as archive:
    self.assertEqual(json.loads(archive.read('extension.json'))['description'],'Current human description');self.assertEqual(json.loads(archive.read('tool.json'))['prompt'],'For the model');self.assertEqual(archive.read('bin/fixture.exe'),data)
   self.assertEqual(source.read_bytes(),before)
   source.write_bytes(before[:-1]+b'X')
   with self.assertRaises(ValueError):convert(source,root/'bad.zip')
   self.assertFalse((root/'bad.zip').exists());self.assertFalse((root/'bad.converting').exists())
 def test_converter_refuses_traversal_lengths_unknown_header_and_original_overwrite(self):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);source=root/'old';source.write_bytes(b'not a tool')
   with self.assertRaises(ValueError):convert(source,root/'new.zip')
   for path in ('../escape','C:/outside','a\\b','CON.txt'):
    meta=metadata();meta['files']=[dict(path=path,size=1,sha256='0'*64)];encoded=json.dumps(meta).encode();source.write_bytes(b'MDOCLI1\n'+struct.pack('<I',len(encoded))+encoded+b'x')
    with self.assertRaises(ValueError):convert(source,root/'new.zip')
   with self.assertRaises(ValueError):convert(source,source)
if __name__=='__main__':unittest.main()
