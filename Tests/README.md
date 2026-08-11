# Cross-Version Test Fixtures

These fixtures are asset-free and are intended to run unchanged on Unreal Engine
4.27 through 5.8. The automation tests create transient materials, so running the
suite does not add packages under `/Game`.

| Fixture | Coverage | Expected result |
| --- | --- | --- |
| `baseline_pbr.matlang` | Basic nodes, parameters, material outputs, import/export | 4 expressions and 6 connections |
| `dag_shared_inputs.matlang` | Shared inputs and fan-out in a directed acyclic graph | 8 expressions and 11 connections |
| `incremental_before.matlang` / `incremental_after.matlang` | Property-only smart update with stable generated IDs | Incremental patch; no structural changes |
| `strata_capability.matlang` | Version-specific shading-model capability | UE4.27: Default Lit fallback; UE5: Strata |
| `lint_invalid.matlang` | Lint commandlet failure and dangling-reference diagnostics | Fails with `ML1002` |

Run the complete compatibility group from a host project:

```text
UnrealEditor-Cmd.exe <Project>.uproject -unattended -nop4 -nosplash \
  -ExecCmds="Automation RunTests MatBP2FP.Compatibility; Quit" \
  -TestExit="Automation Test Queue Empty"
```

For UE4.27, use `UE4Editor-Cmd.exe` with the same arguments. Packaged plugins
include `Tests/Fixtures` through `Config/FilterPlugin.ini`.

Run lint without importing or modifying assets:

```text
UnrealEditor-Cmd.exe <Project>.uproject -unattended -nop4 -nosplash -NullRHI \
  -run=MatBP2FPLint -path=<file-or-directory>
```

The lint commandlet returns `0` on success, `1` when diagnostics fail policy,
and `2` for usage, input discovery, or I/O failures.

Run the headless material graph export and reference-index tests:

```text
UnrealEditor-Cmd.exe <Project>.uproject -unattended -nop4 -nosplash -NullRHI \
  -ExecCmds="Automation RunTests MatBP2FP.Export; Automation RunTests MatBP2FP.Lint; Quit" \
  -TestExit="Automation Test Queue Empty"
```

The production export command must use Unreal's correctly spelled `-Unattended`
switch. It exports `UMaterial` and `UMaterialFunction` assets and writes
`matlang-index.json` beneath the selected output root.
