"""MDO-3 public module ABI contracts."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "include" / "mdo" / "module.h"
MANAGER = ROOT / "app" / "include" / "mdo" / "modules.h"


class ModuleContractTests(unittest.TestCase):
    def test_public_header_has_no_product_dependency(self) -> None:
        text = HEADER.read_text(encoding="utf-8")
        self.assertIn('#include <stdbool.h>', text)
        self.assertIn('#define MDO_MODULE_ABI_VERSION 1u', text)
        self.assertIn('#define MDO_MODULE_ENTRY_SYMBOL "mdoModuleEntry"', text)
        for private in ("xsbase.h", "xrt.h", "xllm.h", "xwork.h"):
            self.assertNotIn(private, text)
        for descriptor in ("mdo_tool_v1", "mdo_agent_v1", "mdo_module_v1",
                           "mdo_host_core_v1", "mdo_host_services_v1",
                           "mdo_registrar_v1"):
            self.assertIn(f"typedef struct {descriptor}", text)

    def test_manager_exposes_only_owned_snapshots(self) -> None:
        text = MANAGER.read_text(encoding="utf-8")
        self.assertIn("typedef struct MdoModuleCatalog MdoModuleCatalog;", text)
        self.assertIn("MdoModuleCatalogSnapshot(void)", text)
        self.assertIn("MdoModuleCatalogRelease", text)
        self.assertIn("MdoModuleDiagnosticsSnapshot(void)", text)
        self.assertNotIn("TCCState", text)

    def test_header_compiles_as_c11_and_cpp17(self) -> None:
        c = shutil.which("gcc")
        cpp = shutil.which("g++")
        if c is None or cpp is None:
            self.skipTest("GCC C/C++ compilers are unavailable")
        source = r'''
#include "mdo/module.h"
static mdo_result execute(void *data, const mdo_tool_context_v1 *context,
    const char *arguments, mdo_result_writer_v1 *writer,
    char *error, size_t error_capacity) {
    (void)data; (void)context; (void)arguments; (void)error;
    (void)error_capacity;
    return writer->WriteText(writer->Context, "ok") &&
        writer->SetSuccess(writer->Context, true) ? MDO_RESULT_OK :
        MDO_RESULT_LIMIT;
}
static const mdo_tool_v1 tool = {
    MDO_V1_HEADER(mdo_tool_v1), "contract.echo", "Echo", "Echo",
    "{\"type\":\"object\"}", MDO_TOOL_EFFECT_READ, MDO_TOOL_STRICT,
    0, 0, 1024, 0, 0, execute, 0
};
static mdo_result register_module(const mdo_host_services_v1 *host,
    const mdo_registrar_v1 *registrar, void **data, char *error,
    size_t error_capacity) {
    (void)host; (void)data;
    return registrar->AddTool(registrar->Context, &tool, error,
        error_capacity);
}
static const mdo_module_v1 module = {
    MDO_V1_HEADER(mdo_module_v1), "contract", "Contract", "Contract",
    "1.0.0", 0, 0, 0, register_module, 0
};
MDO_EXPORT const mdo_module_v1 *mdoModuleEntry(void) { return &module; }
int main(void) { return mdoModuleEntry()->AbiVersion == 1u ? 0 : 1; }
'''
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for suffix, compiler, standard in (("c", c, "c11"),
                                                ("cpp", cpp, "c++17")):
                path = root / f"contract.{suffix}"
                output = root / f"contract-{suffix}.exe"
                path.write_text(source, encoding="utf-8")
                subprocess.run([
                    compiler, f"-std={standard}", "-Wall", "-Wextra",
                    "-Werror", "-I", str(ROOT / "include"), str(path),
                    "-o", str(output),
                ], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
