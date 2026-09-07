from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLUGIN_ROOT = ROOT / "Plugins" / "MatBP2FPMCP"
DESCRIPTOR = PLUGIN_ROOT / "MatBP2FPMCP.uplugin"
VERSION_HEADER = PLUGIN_ROOT / "Source" / "MatBP2FPMCP" / "Public" / "MatBP2FPMCPVersion.h"
TOOLSET_HEADER = PLUGIN_ROOT / "Source" / "MatBP2FPMCP" / "Public" / "MatBP2FPMCPToolset.h"
TOOLSET_CPP = PLUGIN_ROOT / "Source" / "MatBP2FPMCP" / "Private" / "MatBP2FPMCPToolset.cpp"
MODULE_CPP = PLUGIN_ROOT / "Source" / "MatBP2FPMCP" / "Private" / "MatBP2FPMCPModule.cpp"
BUILD_CS = PLUGIN_ROOT / "Source" / "MatBP2FPMCP" / "MatBP2FPMCP.Build.cs"


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(path: Path) -> str:
    if not path.is_file():
        fail(f"missing file: {path.relative_to(ROOT)}")
    return path.read_text(encoding="utf-8")


try:
    descriptor = json.loads(read(DESCRIPTOR))
except json.JSONDecodeError as exc:
    fail(f"invalid plugin JSON: {exc}")

if descriptor.get("EnabledByDefault") is not False:
    fail("MCP plugin must remain opt-in")

modules = descriptor.get("Modules", [])
if not any(module.get("Name") == "MatBP2FPMCP" and module.get("Type") == "Editor" for module in modules):
    fail("MatBP2FPMCP must be an Editor module")

plugin_refs = {plugin.get("Name"): plugin for plugin in descriptor.get("Plugins", [])}
for required in ("MatBP2FP", "ToolsetRegistry", "ModelContextProtocol"):
    if required not in plugin_refs:
        fail(f"missing plugin reference: {required}")
for optional_name in ("ToolsetRegistry", "ModelContextProtocol"):
    if plugin_refs[optional_name].get("Optional") is not True:
        fail(f"{optional_name} must be optional for older engine compatibility")

version_header = read(VERSION_HEADER)
if "MATBP2FP_WITH_UE58_MCP" not in version_header:
    fail("UE 5.8 feature macro is missing")
if "ENGINE_MINOR_VERSION >= 8" not in version_header:
    fail("UE 5.8 minor-version guard is missing")

build_cs = read(BUILD_CS)
if "Target.Version.MajorVersion" not in build_cs or "Target.Version.MinorVersion >= 8" not in build_cs:
    fail("Build.cs does not conditionally add ToolsetRegistry for UE 5.8+")

module_cpp = read(MODULE_CPP)
toolset_header = read(TOOLSET_HEADER)
toolset_cpp = read(TOOLSET_CPP)
for name, source in (("module", module_cpp), ("toolset header", toolset_header), ("toolset implementation", toolset_cpp)):
    if source.count("#if MATBP2FP_WITH_UE58_MCP") < 1:
        fail(f"{name} is not guarded by MATBP2FP_WITH_UE58_MCP")

if "ToolsetRegistry/" not in module_cpp or "ToolsetRegistry/" not in toolset_header:
    fail("ToolsetRegistry includes are missing")

callable_count = len(re.findall(r"UFUNCTION\s*\(\s*meta\s*=\s*\(AICallable\)", toolset_header))
if callable_count != 7:
    fail(f"expected 7 AICallable functions, found {callable_count}")

if "bool bSavePackage = false" not in toolset_header:
    fail("write tools must default bSavePackage to false")
if "ValidateGamePath" not in toolset_cpp or 'Path.Contains(TEXT(".."))' not in toolset_cpp:
    fail("write/read path validation is missing")
backslash_guard = 'Path.Contains(TEXT("' + chr(92) * 2 + '"))'
if backslash_guard not in toolset_cpp:
    fail("path validation must reject Windows backslashes with a valid C++ string literal")
if "RegisterToolsetClass" not in module_cpp or "UnregisterToolsetClass" not in module_cpp:
    fail("toolset startup/shutdown registration is missing")

print("MatBP2FPMCP static validation passed")
print(f"- plugin: {DESCRIPTOR.relative_to(ROOT)}")
print(f"- callable tools: {callable_count}")
print("- UE 5.8 macro and Build.cs dependency guards: present")
print("- ToolsetRegistry startup/shutdown registration: present")
