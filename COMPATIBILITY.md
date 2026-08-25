# Unreal Engine Compatibility

MatBP2FP 0.2 supports Unreal Engine 4.27 and every Unreal Engine 5 minor
release from 5.0 through 5.8.

## Verified build matrix

| Engine | Editor target | Game target | Windows toolchain used |
| --- | --- | --- | --- |
| UE 4.27 | Pass | Pass | MSVC 14.32, 14.44 |
| UE 5.0 | Pass | Pass | MSVC 14.32 |
| UE 5.1 | Pass | Pass | MSVC 14.34 |
| UE 5.2 | Pass | Pass | MSVC 14.34 |
| UE 5.3 | Pass | Pass | MSVC 14.36 |
| UE 5.4 | Pass | Pass | MSVC 14.38 |
| UE 5.5 | Pass | Pass | MSVC 14.38 |
| UE 5.6 | Pass | Pass | MSVC 14.38 |
| UE 5.7 | Pass | Pass | MSVC 14.38, 14.44 |
| UE 5.8 | Pass | Pass | MSVC 14.44 |

Both plugin modules and the editor automation tests are compiled in each
Editor target. The Game target verifies that the `MatBP2FP` runtime module
does not leak editor-only types into packaged builds. Game Development and
Game Shipping were both verified. This matrix was rerun on 2026-08-25 after
the static material cost analyzer and `MatBP2FPPerf` commandlet were added.

## Static material cost tool verification

The `MatBP2FP.Perf` automation group passed 4/4 tests when run in UE 4.27,
UE 5.0, and UE 5.8. These tests cover stage and reachability analysis, static
switch scenarios, material-function expansion, and unknown-cost risks.

The `MatBP2FPPerf` commandlet was also run against `DSL/Examples` in UE 4.27
and UE 5.8. Both engines produced three passing reports with identical pixel
ALU proxy values, texture sample-site counts, dead-node counts, and verdicts.
An additional source-engine UE6 build passed as a forward-compatibility check;
UE6 is not part of the advertised support range.

## Compatibility boundaries

Version-specific engine APIs are isolated in
`Source/MatBP2FP/Public/MatBP2FPVersionCompat.h`. It covers:

- raw pointers in UE 4.27 versus `TObjectPtr` in UE5;
- material and material-function expression access;
- the UE 5.1 material editor-only data and expression collection migration;
- the UE 5.2 `MaterialDomain.h` split;
- expression input enumeration changes in UE 5.3 and UE 5.5;
- Asset Registry class identifiers before and after UE 5.1;
- reflection property import APIs;
- ticker handles and the UE 5.8 post-engine-init delegate API;
- Strata/Substrate shading-model availability.

## Building old engines on a current workstation

UE 4.27 and early UE5 releases may select an incompatible newest MSVC by
default. Select a period-appropriate installed toolchain explicitly when
needed:

```powershell
UnrealBuildTool.exe UnrealEditor Win64 Development `
  -Project="X:\Project\Project.uproject" `
  -Compiler=VisualStudio2022 `
  "-CompilerVersion=14.32.31326"
```

The quotes around `-CompilerVersion` matter in PowerShell. Without them, a
multi-dot version can be tokenized incorrectly before it reaches UBT.

Some installed UE 4.27 AutomationTool builds append `-2017` while compiling
the plugin Game target. If Visual Studio 2017 is not installed, compile the
Editor, Game Development, and Game Shipping targets directly with UBT instead.

UE 5.0 AutomationTool targets .NET Core 3.1. When only a newer .NET runtime is
installed, `DOTNET_ROLL_FORWARD=Major` can be used for local build validation.
These are build-host requirements and do not affect plugin runtime behavior.
