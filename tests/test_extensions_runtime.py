"""Current nine-type directory/ZIP contracts; replaces retired flat JSON editors."""
import argparse
from pathlib import Path
from native_suite import run

CONTRACTS = ['test_extension_zip_native','test_extension_directory_native','test_extension_directory_api_native',
    'test_command_directory_native','test_module_directory_native','test_reference_directory_native',
    'test_mcp_directory_native','test_mcp_automatic_native','test_mcp_runtime_data_native',
    'test_external_directory_native','test_plugin_directory_native','test_extension_save_native',
    'test_extension_tasks_native','test_todo_native']

def main():
    p=argparse.ArgumentParser(description=__doc__);s=p.add_mutually_exclusive_group(required=True)
    s.add_argument('--host',type=Path);s.add_argument('--packed',type=Path);a=p.parse_args()
    run(CONTRACTS,a.host.resolve() if a.host else None,a.packed.resolve() if a.packed else None)
    print('PASS 14 current extension contracts across nine directory types')

if __name__=='__main__':main()
