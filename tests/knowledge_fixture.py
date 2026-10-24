"""Portable inert documents used by native and website fixtures."""
import base64
import json

def metadata(**changes):
    return dict(format='mdo.knowledge.v1',name='XRT documentation',description='Static XRT reference',index='index.md',prompt='For XRT consult {$path}; index.md is the index. {$path}/reference/runtime.md',inject_prompt=True,**changes)
def file(path,text):return dict(path=path,base64=base64.b64encode(text.encode()).decode())
def package(version='1.0.0'):
    return dict(format='mdo.extension.v1',manifest=dict(slug='xrt-reference',name='XRT knowledge',description='Reference docs',readme='Use the index.',version=version,license='MIT',author='Original author',copyright='Copyright original',platforms=['windows-x86_64','linux-x86_64-glibc']),resources=[dict(kind='knowledge',id='xrt-reference',content=json.dumps(metadata()),files=[file('index.md','# Index\n[Runtime](reference/runtime.md)'),file('reference/runtime.md','XRT knowledge sentinel')])])

