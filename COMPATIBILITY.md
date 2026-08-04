# Unreal Engine Compatibility

MatBP2FP 0.2 supports Unreal Engine 4.27 and every Unreal Engine 5 minor
release from 5.0 through 5.8.

## Verified build matrix

| Engine | Editor target | Game target | Windows toolchain used |
| --- | --- | --- | --- |
| UE 4.27 | Pass | Pass | MSVC 14.32 |
| UE 5.0 | Pass | Pass | MSVC 14.32 |
| UE 5.1 | Pass | Pass | MSVC 14.34 |
| UE 5.2 | Pass | Pass | MSVC 14.34 |
| UE 5.3 | Pass | Pass | MSVC 14.36 |
| UE 5.4 | Pass | Pass | MSVC 14.38 |
| UE 5.5 | Pass | Pass | MSVC 14.38 |
| UE 5.6 | Pass | Pass | MSVC 14.38 |
| UE 5.7 | Pass | Pass | MSVC 14.38 |
| UE 5.8 | Pass | Pass | MSVC 14.44 |

Both plugin modules and the editor automation tests are compiled in each
Editor target. The Game target verifies that the `MatBP2FP` runtime module
does not leak editor-only types into packaged builds.

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
  -CompilerVersion=14.32.31326
```

UE 5.0 AutomationTool targets .NET Core 3.1. When only a newer .NET runtime is
installed, `DOTNET_ROLL_FORWARD=Major` can be used for local build validation.
These are build-host requirements and do not affect plugin runtime behavior.
