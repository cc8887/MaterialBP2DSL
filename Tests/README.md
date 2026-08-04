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

Run the complete compatibility group from a host project:

```text
UnrealEditor-Cmd.exe <Project>.uproject -unattended -nop4 -nosplash \
  -ExecCmds="Automation RunTests MatBP2FP.Compatibility; Quit" \
  -TestExit="Automation Test Queue Empty"
```

For UE4.27, use `UE4Editor-Cmd.exe` with the same arguments. Packaged plugins
include `Tests/Fixtures` through `Config/FilterPlugin.ini`.
