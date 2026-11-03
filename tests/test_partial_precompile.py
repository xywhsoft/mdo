"""Transformation boundaries for the reviewed partial precompile experiment."""
import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import benchmark_partial_precompile as pipeline

class PartialPrecompileTests(unittest.TestCase):
    def test_function_extraction_keeps_strings_comments_and_local_tables(self):
        text='''#include <xsbase.h>
typedef struct Box {int value;} Box;
static const char* const MdoExtensionKinds[]={"tools"};
static inline int MdoExample(const char* s) {
 static const int local[]={1,2};
 /* } is not an end */ if(s[0]=='{')return local[0];
 return "}"[0]; // { is not a start
}
'''
        thin,data,functions,names=pipeline.extract_header(text)
        self.assertEqual(names,['MdoExample']);self.assertEqual(data,[])
        self.assertIn('int MdoExample(const char* s);',thin)
        self.assertIn('MdoExtensionKinds[]={"tools"}',thin)
        self.assertNotIn('local[]',thin);self.assertIn('local[]={1,2}',functions[0])
        self.assertIn('typedef struct Box',thin)

    def test_private_unicode_table_moves_without_erasing_its_type(self):
        text='typedef struct Fold {unsigned From,To[3];} Fold;\nstatic const Fold MdoExtensionFolds[]={\n{65,{97,0,0}},\n};\n'
        thin,data,functions,names=pipeline.extract_header(text)
        self.assertIn('typedef struct Fold',thin);self.assertNotIn('65',thin)
        self.assertIn('65',data[0]);self.assertEqual(functions,[])

    def test_original_build_globals_restore_on_failure(self):
        before={key:getattr(pipeline.build_mdo,key) for key in ('ROOT','APP','UNITY_PATH','BUILTIN_KEY_PATH')}
        with self.assertRaises(RuntimeError):
            with pipeline.staged_build_globals(Path('D:/fixture-project')):
                self.assertEqual(pipeline.build_mdo.APP,Path('D:/fixture-project/app'))
                self.assertTrue(str(pipeline.build_mdo.BUILTIN_KEY_PATH).startswith('D:'))
                raise RuntimeError('fixture')
        self.assertEqual(before,{key:getattr(pipeline.build_mdo,key) for key in before})

if __name__=='__main__':unittest.main()
