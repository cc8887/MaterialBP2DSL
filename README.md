# MatBP2FP

MatBP2FP 是 Unreal Engine 材质图与 Lisp 风格 S-expression DSL（MatLang）之间的转换插件。它把二进制 `.uasset` 中的材质 DAG 表达为可解析、可编辑、可审查的文本，并提供导出、导入、校验、引用查询和局部更新能力。

当前版本为 `0.2.0-alpha`。材质转换和资产写入均属于 Editor 功能。

## 当前支持的功能

### 材质图与 MatLang 双向转换

- 将 `UMaterial` 导出为 `.matlang`，也可从 MatLang 创建新材质或更新已有材质。
- 保存材质域、混合模式、着色模型、双面等材质属性，以及表达式节点、输入连线、材质输出槽和资产引用。
- 使用显式 `$id` 与 `(connect $id output-index)` 表达 DAG 中的共享节点和多输出连接。
- 常用常量、参数、纹理、数学、分支、Custom HLSL、Material Function Call 和 Named Reroute 节点使用专用转换逻辑；其他可实例化的 `UMaterialExpression` 类型会尝试通过类名和可编辑属性反射进行转换。
- 支持在编辑器菜单、Commandlet、Unreal Python 和 Blueprint Function Library 中调用核心工作流。

MatLang 示例：

```lisp
(material "M_Example"
  :domain surface
  :blend-mode opaque
  :shading-model default-lit
  :two-sided false

  (expressions
    (scalar-parameter $roughness
      :name "Roughness"
      :default 0.65)
    (vector-parameter $base-color
      :name "BaseColor"
      :default (1.0 0.8 0.6 1.0)))

  (outputs
    :base-color (connect $base-color 0)
    :roughness (connect $roughness 0)))
```

仓库中的完整样例位于 [`DSL/Examples`](DSL/Examples)。

### Material Function 导出与引用索引

- 批量导出 `UMaterial` 和 `UMaterialFunction`，并按 Unreal mount point 保留目录结构。
- Python 接口可递归导出某个材质引用的 Material Function 依赖。
- 每次批量导出生成 `matlang-index.json`，记录资产定义、文件位置和 Material Function 调用关系。
- `MatBP2FPRefs` Commandlet 可查询某个资产的入向和出向引用，并输出可定位的 `file:line` 位置。

当前仅支持 **Material Function 导出**；尚不支持从 `(material-function ...)` 创建或更新 `UMaterialFunction`。

### 语法检查、Stub 与往返验证

- Tokenizer、Parser 和 AST 可解析 MatLang，并输出带文件、行列和规则编号的诊断。
- Linter 检查枚举值、重复节点 ID、悬空引用、非法输出连接等问题，可在 CI 中批量运行。
- Stub 导出扫描当前引擎中的 Material Expression 类型和可编辑属性，生成 `matlang-stub.scm`，供工具和 AI 在生成 DSL 时查询。
- Round-trip 校验覆盖 `Export -> Parse -> Serialize -> Compare`；仓库还提供跨进程、落盘后重载的严格往返测试。

测试夹具与自动化命令见 [`Tests/README.md`](Tests/README.md)。

### 局部图修改与文本 Diff

`.matlang` 是普通文本，节点、属性和连接都有明确边界。修改一个参数或一条连线时，Git diff 通常只包含对应的几行，比二进制 `.uasset` 更容易审查、合并和定位冲突。

更新已有材质时，插件会先导出当前图并在 AST 层计算 diff：

- 节点属性、输入连接、材质属性和输出槽变化会优先就地 Patch，并重新编译材质。
- 节点新增、删除、类型变化和材质域变化属于结构变化，会自动回退到全量重建。
- 若 Patcher 在应用变更时失败，会自动回退到全量重建。Lint 失败，或 Patch 后的图规范化/校验失败时，更新会直接报错并停止，不会把失败结果当作成功资产。

为了可靠命中局部 Patch，应遵循 **Export -> Edit -> Update**：先导出当前材质，在导出文本上修改，并保留现有 `$id`。导出器的 `$id` 来自当前表达式顺序，并没有作为独立字段永久写入 `.uasset`；如果从头生成 DSL、重命名 `$id` 或大幅调整节点顺序，diff 可能被识别为结构变化。全量重建可以得到目标图，但不会保留原节点 GUID，布局也可能变化。

### 批处理、映射与自动导出

- 编辑器启动时建立 Material 与 DSL 文件的映射表，可通过 Python 查询 `Synced`、`MatOnly`、`DSLOnly` 和 `OutOfSync` 状态。
- `Auto Sync Mode = Mat2FP` 时，材质编译完成后会延迟导出对应 `.matlang` 文件。
- 导入生命周期提供节点、属性和完成阶段的扩展 Hook，供自动布局等下游编辑器插件接入。

`FP2Mat` 文件监听模式目前尚未实现；从 DSL 自动回写材质仍需显式调用导入或更新接口。

## 支持的 Unreal Engine 版本

MatBP2FP 支持 UE 4.27，以及 UE 5.0 至 UE 5.8。以下版本已在 **Windows** 上逐版本验证 Editor 与 Game target 编译：

| Unreal Engine | Editor target | Game target | 验证工具链 |
| --- | --- | --- | --- |
| UE 4.27 | 通过 | 通过 | MSVC 14.32 |
| UE 5.0 | 通过 | 通过 | MSVC 14.32 |
| UE 5.1 | 通过 | 通过 | MSVC 14.34 |
| UE 5.2 | 通过 | 通过 | MSVC 14.34 |
| UE 5.3 | 通过 | 通过 | MSVC 14.36 |
| UE 5.4 | 通过 | 通过 | MSVC 14.38 |
| UE 5.5 | 通过 | 通过 | MSVC 14.38 |
| UE 5.6 | 通过 | 通过 | MSVC 14.38 |
| UE 5.7 | 通过 | 通过 | MSVC 14.38 |
| UE 5.8 | 通过 | 通过 | MSVC 14.44 |

Game target 用于确认运行时模块不会把 Editor-only 类型带入打包构建；材质图的导入、导出和修改仍只在 Editor 中执行。

插件清单允许 `Win64`、`Mac` 和 `Linux`，但当前逐版本构建矩阵只覆盖 Windows。Mac 和 Linux 尚未完成同等强度的验证，不应视为已保证的生产支持范围。

版本适配细节、旧版引擎工具链注意事项见 [`COMPATIBILITY.md`](COMPATIBILITY.md)。

## 安装

### 前置条件

- 已安装目标 Unreal Engine 版本及其对应的 C++ 编译工具链。
- 项目可以编译 C++ 模块。纯 Blueprint 项目首次安装源码插件时，也需要安装编译工具链；必要时先为项目添加一个空 C++ 类。
- 不要在不同引擎版本之间复用插件的 `Binaries` 或 `Intermediate` 目录，应由目标引擎重新编译。

### 从 Git 安装到项目

推荐使用项目级安装。关闭 Unreal Editor，然后在 PowerShell 中执行：

```powershell
New-Item -ItemType Directory -Force -Path "<Project>\Plugins"
git clone https://github.com/cc8887/MaterialBP2DSL.git "<Project>\Plugins\MatBP2FP"
```

也可以下载仓库 ZIP 并解压到同一位置。最终目录应满足：

```text
<Project>/
  Plugins/
    MatBP2FP/
      MatBP2FP.uplugin
      Source/
      Config/
```

随后：

1. 重新生成项目文件，并使用目标引擎构建项目的 `Development Editor` target；使用 Epic Launcher 引擎时，也可以在首次打开项目时接受“编译缺失模块”的提示。
2. 打开项目，在 `Edit -> Plugins` 中搜索 `MatBP2FP` 并启用插件。
3. 需要使用下文的 Unreal Python 接口时，同时启用引擎内置的 `Python Editor Script Plugin`。
4. 按提示重启编辑器。
5. 在主菜单 `Tools` 的 `MatBP2FP` 分区中确认可以看到导出、Stub 和 Round-trip 命令。

若需要让多个项目共用插件，可将目录放入对应引擎的 `Engine/Plugins` 下，但项目级安装更容易固定插件版本，也能避免影响其他工程。

## 快速使用

### 编辑器菜单

主菜单 `Tools` 的 `MatBP2FP` 分区提供：

- `Export All Materials to DSL`：批量导出项目中的 Material 与 Material Function，并生成引用索引。
- `Export MatLang Stub`：导出当前引擎的表达式类型签名。
- `Run Round-Trip Validation`：验证项目材质的文本往返一致性。

默认输出目录为：

```text
<Project>/Saved/BP2DSL/MatBP/
```

例如 `/Game/Materials/M_Main` 会导出为 `Game/Materials/M_Main.matlang`。

### Commandlet

UE5 使用 `UnrealEditor-Cmd.exe`；UE4.27 请替换为 `UE4Editor-Cmd.exe`。

```powershell
# 导出 Material 和 Material Function；可用 -path、-material 或 -output 过滤/改写输出
UnrealEditor-Cmd.exe "<Project>.uproject" -run=MatBP2FPExport -all -NullRHI -Unattended -NoSplash -NoP4 -UTF8Output

# 只检查文件或目录，不修改资产；可追加 -fail-on-warning
UnrealEditor-Cmd.exe "<Project>.uproject" -run=MatBP2FPLint -path="<FileOrDirectory>" -NullRHI -Unattended -NoSplash -NoP4 -UTF8Output

# 从文件创建材质；追加 -update 时按文件名查找并更新已有材质（当前不会保存 package）
UnrealEditor-Cmd.exe "<Project>.uproject" -run=MatBP2FPImport -file="<Material.matlang>" -NullRHI -Unattended -NoSplash -NoP4 -UTF8Output

# 查询 Material Function 定义及入向/出向引用
UnrealEditor-Cmd.exe "<Project>.uproject" -run=MatBP2FPRefs -asset="/Game/Functions/MF_Noise.MF_Noise" -direction=both -NullRHI -Unattended -NoSplash -NoP4 -UTF8Output
```

导出命令还支持 `-materials-only`、`-functions-only` 和 `-include-engine`。默认不导出 `/Engine`、`/Script`、`/Temp` 和 `/Transient` 内容。

当前 `MatBP2FPImport` Commandlet 只创建或更新内存中的资产并将 package 标记为 dirty，没有调用保存接口；在无头进程退出后不能依赖这些改动持久化。需要保存资产时，请使用下述 Python/Blueprint Bridge 并将 `bSavePackage` 设为 `true`，或在 Editor 会话中显式保存 package。

### Unreal Python

```python
import unreal

exported = unreal.MatBP2FPPythonBridge.export_material_to_text(
    "/Game/Materials/M_Example.M_Example"
)
if not exported.success:
    raise RuntimeError(exported.message)

# 在 exported.dsl_text 上做局部修改，并保留原有 $id
updated_text = exported.dsl_text.replace(":default 0.65", ":default 0.8")

result = unreal.MatBP2FPPythonBridge.update_material_from_text(
    "/Game/Materials/M_Example.M_Example",
    updated_text,
    True,
)
if not result.success:
    raise RuntimeError(result.message)
if not result.saved_package:
    raise RuntimeError("Material updated but package save failed")
```

Python/Blueprint Bridge 还提供文件导入导出、递归依赖导出、Round-trip 验证、路径映射查询和 Stub 导出接口，完整签名见 [`MatBP2FPPythonBridge.h`](Source/MatBP2FPEditor/Public/MatBP2FPPythonBridge.h)。

## 配套 AI Skill

MatBP2FP 的配套 AI Skill 收录在公开仓库 [UE-Editor-MCPServer-Skills](https://github.com/cc8887/UE-Editor-MCPServer-Skills) 中：

- Git：`https://github.com/cc8887/UE-Editor-MCPServer-Skills.git`
- Skill 直达：[plugins/matbp2fp](https://github.com/cc8887/UE-Editor-MCPServer-Skills/tree/main/plugins/matbp2fp)

该 Skill 是通过 MCP 和 `unreal.MatBP2FPPythonBridge` 调用本插件的可选自动化层，不是 MatBP2FP 的编译或运行依赖。插件当前支持范围和能力边界以本 README 与当前代码为准。

## 已知边界

- 当前处理基础 `UMaterial`，不支持 `UMaterialInstance`。
- `UMaterialFunction` 当前可导出和建立引用索引，但不可从 DSL 导入或更新。
- 复杂数组、内部对象引用或引擎私有结构不一定能通过通用反射无损恢复。跨版本或使用特殊表达式时，应先运行 Lint 和 Round-trip 验证。
- 导入中的纹理、Material Function 等资产路径必须能在目标工程中解析；缺失依赖会产生降级警告。
- 局部 Patch 依赖当前导出文本中的 `$id` 与现有图匹配；结构变化、ID 不匹配或 Patch 失败会回退到全量重建。
- `Mat2FP` 自动导出可用，`FP2Mat` 文件监听自动导入尚未实现。
- 当前完整验证矩阵仅覆盖 Windows。
