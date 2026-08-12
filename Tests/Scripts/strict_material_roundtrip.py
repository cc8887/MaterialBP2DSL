"""Three-process, disk-backed material round-trip validation.

Run this script in three separate Unreal Editor commandlet processes with the
same MATBP_RT_MATERIAL_PATH and MATBP_RT_RUN_ID and MATBP_RT_PHASE set to 1, 2,
then 3. Phase 3 compares all exports and removes the temporary material assets.
"""

import collections
import difflib
import hashlib
import os
import re

import unreal


MATERIAL_PATH = os.environ.get("MATBP_RT_MATERIAL_PATH", "").strip()
PHASE = os.environ.get("MATBP_RT_PHASE", "").strip()
RUN_ID = os.environ.get("MATBP_RT_RUN_ID", "").strip()

if not MATERIAL_PATH.startswith("/Game/") or "." not in MATERIAL_PATH:
    raise RuntimeError("MATBP_RT_MATERIAL_PATH must be a /Game object path")
if PHASE not in ("1", "2", "3"):
    raise RuntimeError("MATBP_RT_PHASE must be 1, 2, or 3")
if not re.match(r"^[A-Za-z0-9_]+$", RUN_ID):
    raise RuntimeError("MATBP_RT_RUN_ID must contain only letters, digits, and underscores")

MATERIAL_NAME = MATERIAL_PATH.rsplit("/", 1)[-1].split(".", 1)[0]
if not MATERIAL_NAME:
    raise RuntimeError("MATBP_RT_MATERIAL_PATH has no asset name")

TEMP_ROOT = "/Game/__MatBP2FPRoundTripTests/Run_{}".format(RUN_ID)
ASSET_A = "{}/A/{}.{}".format(TEMP_ROOT, MATERIAL_NAME, MATERIAL_NAME)
ASSET_B = "{}/B/{}.{}".format(TEMP_ROOT, MATERIAL_NAME, MATERIAL_NAME)
if MATERIAL_PATH.startswith(TEMP_ROOT + "/"):
    raise RuntimeError("The source material cannot be inside the temporary test root")

RESULT_ROOT = os.path.join(
    unreal.Paths.project_saved_dir(), "MatBP2FPRoundTripDisk", RUN_ID
)
DSL_SOURCE = os.path.join(RESULT_ROOT, "source.matlang")
DSL_A = os.path.join(RESULT_ROOT, "after_reload_a.matlang")
DSL_B = os.path.join(RESULT_ROOT, "after_reload_b.matlang")

ASSET_PATH_LINE = re.compile(r'^  :asset-path\s+"(?:\\.|[^"])*"\s*$', re.MULTILINE)
EXPRESSION_LINE = re.compile(
    r"^    \([a-z][a-z0-9-]*\s+\$[^\s()]+\)?\s*$", re.MULTILINE
)
QUOTED_PATH = re.compile(r'"((?:[^"\\]|\\.)*(?:/Game/|/Engine/)(?:[^"\\]|\\.)*)"')


def prop(result, name):
    return result.get_editor_property(name)


def require_result(label, result, require_saved=False):
    if not prop(result, "success"):
        warnings = list(prop(result, "warnings"))
        raise RuntimeError(
            "{} failed: {}; warnings={}".format(label, prop(result, "message"), warnings)
        )
    if require_saved and not prop(result, "saved_package"):
        raise RuntimeError("{} succeeded but did not save its package".format(label))
    unreal.log("MATBP_RT_{} success=True message={}".format(label, prop(result, "message")))
    return result


def require_asset(label, object_path):
    asset = unreal.load_asset(object_path)
    if asset is None:
        raise RuntimeError("{} failed to load saved asset: {}".format(label, object_path))
    unreal.log("MATBP_RT_{} loaded=True asset={}".format(label, object_path))
    return asset


def require_missing_asset(label, object_path):
    if unreal.EditorAssetLibrary.does_asset_exist(object_path):
        raise RuntimeError("{} refuses to overwrite existing asset: {}".format(label, object_path))


def normalize_identity(dsl):
    normalized = dsl.replace("\r\n", "\n").replace("\r", "\n")
    normalized, replacements = ASSET_PATH_LINE.subn(
        '  :asset-path "<ROUNDTRIP_ASSET>"', normalized
    )
    if replacements != 1:
        raise RuntimeError("Expected exactly one asset-path line, found {}".format(replacements))
    return "\n".join(line.rstrip() for line in normalized.splitlines()).strip() + "\n"


def fingerprint(dsl):
    normalized = normalize_identity(dsl)
    return {
        "normalized": normalized,
        "sha256": hashlib.sha256(normalized.encode("utf-8")).hexdigest(),
        "expressions": len(EXPRESSION_LINE.findall(normalized)),
        "connections": normalized.count("(connect "),
        "declarations": len(
            re.findall(r"^\s+\(named-reroute-declaration\s", normalized, re.MULTILINE)
        ),
        "usages": len(
            re.findall(r"^\s+\(named-reroute-usage\s", normalized, re.MULTILINE)
        ),
        "declaration_guids": normalized.count(":declaration-guid"),
        "legacy_declarations": len(
            re.findall(r"^\s+:declaration\s", normalized, re.MULTILINE)
        ),
        "external_paths": collections.Counter(QUOTED_PATH.findall(normalized)),
    }


def assert_same(label, left, right):
    scalar_keys = (
        "sha256",
        "expressions",
        "connections",
        "declarations",
        "usages",
        "declaration_guids",
        "legacy_declarations",
        "external_paths",
    )
    mismatches = [key for key in scalar_keys if left[key] != right[key]]
    if left["normalized"] != right["normalized"]:
        diff = list(
            difflib.unified_diff(
                left["normalized"].splitlines(),
                right["normalized"].splitlines(),
                fromfile=label + "_left",
                tofile=label + "_right",
                lineterm="",
            )
        )
        for line in diff[:120]:
            unreal.log_error("MATBP_RT_DIFF {}".format(line))
        raise RuntimeError(
            "{} DSL differs after identity normalization; keys={}; diff_lines={}".format(
                label, mismatches, len(diff)
            )
        )
    if mismatches:
        raise RuntimeError("{} fingerprints differ: {}".format(label, mismatches))
    unreal.log("MATBP_RT_COMPARE {} exact=True sha256={}".format(label, left["sha256"]))


def read_dsl(path):
    with open(path, "r", encoding="utf-8-sig") as handle:
        return handle.read()


if PHASE == "1":
    require_missing_asset("PHASE_1", ASSET_A)
    require_missing_asset("PHASE_1", ASSET_B)
    os.makedirs(RESULT_ROOT, exist_ok=False)
    require_asset("LOAD_SOURCE", MATERIAL_PATH)
    require_result(
        "BUILTIN_SOURCE",
        unreal.MatBP2FPPythonBridge.validate_material_round_trip(MATERIAL_PATH),
    )
    require_result(
        "EXPORT_SOURCE",
        unreal.MatBP2FPPythonBridge.export_material_to_file(MATERIAL_PATH, DSL_SOURCE),
    )
    imported = require_result(
        "IMPORT_A",
        unreal.MatBP2FPPythonBridge.import_material_from_file(
            DSL_SOURCE, TEMP_ROOT + "/A", True
        ),
        require_saved=True,
    )
    if prop(imported, "asset_path") != ASSET_A:
        raise RuntimeError(
            "Unexpected A asset path: {} != {}".format(prop(imported, "asset_path"), ASSET_A)
        )
    unreal.log(
        "MATBP_RT_PHASE_COMPLETE phase=1 saved={} expressions={} connections={}".format(
            ASSET_A,
            prop(imported, "num_expressions_created"),
            prop(imported, "num_connections_made"),
        )
    )

elif PHASE == "2":
    require_asset("RELOAD_A", ASSET_A)
    require_missing_asset("PHASE_2", ASSET_B)
    require_result(
        "BUILTIN_A", unreal.MatBP2FPPythonBridge.validate_material_round_trip(ASSET_A)
    )
    require_result(
        "EXPORT_A", unreal.MatBP2FPPythonBridge.export_material_to_file(ASSET_A, DSL_A)
    )
    imported = require_result(
        "IMPORT_B",
        unreal.MatBP2FPPythonBridge.import_material_from_file(
            DSL_A, TEMP_ROOT + "/B", True
        ),
        require_saved=True,
    )
    if prop(imported, "asset_path") != ASSET_B:
        raise RuntimeError(
            "Unexpected B asset path: {} != {}".format(prop(imported, "asset_path"), ASSET_B)
        )
    unreal.log(
        "MATBP_RT_PHASE_COMPLETE phase=2 saved={} expressions={} connections={}".format(
            ASSET_B,
            prop(imported, "num_expressions_created"),
            prop(imported, "num_connections_made"),
        )
    )

else:
    try:
        require_asset("RELOAD_B", ASSET_B)
        require_result(
            "BUILTIN_B", unreal.MatBP2FPPythonBridge.validate_material_round_trip(ASSET_B)
        )
        require_result(
            "EXPORT_B", unreal.MatBP2FPPythonBridge.export_material_to_file(ASSET_B, DSL_B)
        )

        fingerprints = [fingerprint(read_dsl(path)) for path in (DSL_SOURCE, DSL_A, DSL_B)]
        assert_same("SOURCE_TO_RELOADED_A", fingerprints[0], fingerprints[1])
        assert_same("RELOADED_A_TO_RELOADED_B", fingerprints[1], fingerprints[2])
        if fingerprints[0]["legacy_declarations"] != 0:
            raise RuntimeError("Legacy named-reroute declaration object paths remain")

        unreal.log(
            "MATBP_RT_COMPLETE success=True expressions={} connections={} declarations={} "
            "usages={} declaration_guids={} external_refs={} canonical_sha256={}".format(
                fingerprints[0]["expressions"],
                fingerprints[0]["connections"],
                fingerprints[0]["declarations"],
                fingerprints[0]["usages"],
                fingerprints[0]["declaration_guids"],
                sum(fingerprints[0]["external_paths"].values()),
                fingerprints[0]["sha256"],
            )
        )
    finally:
        for asset_path in (ASSET_B, ASSET_A):
            deleted = unreal.EditorAssetLibrary.delete_asset(asset_path)
            unreal.log("MATBP_RT_CLEANUP asset={} deleted={}".format(asset_path, deleted))
