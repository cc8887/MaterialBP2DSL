# MatBP2FP — Material Blueprint to Functional Programming DSL

将 Unreal Engine 材质蓝图 (UMaterial) 转换为 S-expression 函数式 DSL (MatLang)，支持双向转换。

## 概述

MatBP2FP 是 AnimBP2FP 的姊妹项目，采用相同的架构思路将材质蓝图转换为可读、可编辑、可版本控制的 DSL 文本格式。

### 材质 vs 动画蓝图的关键差异

| 特性 | AnimBP (AnimLang) | Material (MatLang) |
|------|------------------|-------------------|
| 图结构 | 树 (单根) | DAG (有向无环图) |
| 节点引用 | `(ref "Title")` | `(connect $id output-idx)` |
| 节点标识 | NodeType + 位置 | 显式 `$id` |
| 输出目标 | Root Pose | 多个材质输入槽 |
| 状态机 | 有 | 无 |
| 变量绑定 | `(define ...)` | 表达式 DAG 天然共享 |

## DSL 语法 (MatLang)

```lisp
(material "M_Example"
  :domain surface
  :blend-mode opaque
  :shading-model default-lit
  :two-sided false

  (expressions
    (texture-sample $tex1
      :texture (asset "/Game/Textures/T_Brick_D")
      :uv (connect $uv1))
    (texture-coordinate $uv1
      :coordinate-index 0
      :u-tiling 2.0)
    (multiply $mul1
      :a (connect $tex1 0)
      :b (connect $color1 0))
    (vector-parameter $color1
      :name "TintColor"
      :default (1.0 1.0 1.0 1.0))
    (constant $const1
      :value 0.5))

  (outputs
    :base-color (connect $mul1 0)
    :metallic (connect $const1 0)
    :roughness 0.5))
```

### 语法要素

| 语法 | 含义 |
|------|------|
| `(material "Name" ...)` | 材质顶层 |
| `:domain surface` | 材质域 |
| `:blend-mode opaque` | 混合模式 |
| `:shading-model default-lit` | 着色模型 |
| `(expressions ...)` | 表达式节点列表 |
| `(expr-type $id :prop val ...)` | 表达式节点定义 |
| `(connect $target-id output-idx)` | DAG 连接引用 |
| `(asset "/Game/Path")` | 资产路径引用 |
| `(outputs :slot (connect $id) ...)` | 材质输出连接 |

## 模块架构

```
MatBP2FP/                    (Runtime, PreDefault)
├── MatLangAST.h/cpp         AST 数据结构 (DAG)
├── MatLangTokenizer.h/cpp   S-expression 词法分析
├── MatLangParser.h/cpp      递归下降语法分析
├── MatLangRoundTrip.h/cpp   往返验证
├── MatBPExporter.h/cpp      UMaterial → DSL
└── MatBPImporter.h/cpp      DSL → UMaterial

MatBP2FPEditor/              (Editor, Default)
├── MatBP2FPEditorModule      编辑器菜单
├── MatBP2FPSettings           项目设置
├── MatBP2FPExportCommandlet   导出命令行
├── MatBP2FPImportCommandlet   导入命令行
└── MatBP2FPRoundTripCommandlet 验证命令行
```

## 使用方法

### 编辑器菜单
`Tools → MatBP2FP → Export All Materials to DSL`
`Tools → MatBP2FP → Run Round-Trip Validation`

### Commandlet
```bash
# 导出所有游戏材质
UnrealEditor.exe "Project.uproject" -run=MatBP2FPExport

# 导出指定材质
UnrealEditor.exe "Project.uproject" -run=MatBP2FPExport -material=M_Brick

# 往返验证
UnrealEditor.exe "Project.uproject" -run=MatBP2FPRoundTrip

# 导入
UnrealEditor.exe "Project.uproject" -run=MatBP2FPImport
UnrealEditor.exe "Project.uproject" -run=MatBP2FPImport -test
UnrealEditor.exe "Project.uproject" -run=MatBP2FPImport -update
UnrealEditor.exe "Project.uproject" -run=MatBP2FPImport -file=path/to/material.matlang
```

### 项目设置
`Edit → Project Settings → Plugins → MatBP2FP`
- Export Output Path (默认: `MatLang/Exported/`)
- Include Editor Positions
- Include Comments
- Auto Compile After Import

## 支持的表达式类型

### 数学运算
`add`, `subtract`, `multiply`, `divide`, `power`, `dot-product`, `cross-product`, `abs`, `clamp`, `linear-interpolate`, `one-minus`, `normalize`, `floor`, `ceil`, `frac`, `sine`, `cosine`

### 常量
`constant`, `constant2-vector`, `constant3-vector`, `constant4-vector`

### 参数
`scalar-parameter`, `vector-parameter`, `static-switch-parameter`, `texture-object-parameter`

### 纹理
`texture-sample`, `texture-coordinate`, `panner`

### 工具
`component-mask`, `append-vector`, `if`, `static-switch`, `desaturation`, `fresnel`, `distance`, `transform`

### 高级
`material-function-call`, `custom` (HLSL), `function-input`, `function-output`

### 世界数据
`time`, `world-position`, `vertex-normal-ws`, `camera-position-ws`

## 已知限制

1. **Material Functions**: 函数内部表达式不展开，仅保存函数资产引用
2. **Custom HLSL**: 代码以转义字符串保存
3. **Material Instances**: 当前仅支持基础材质 (UMaterial)，不支持 Material Instance
4. **编辑器位置**: 默认不导出节点编辑器位置（可在设置中启用）
5. **Dynamic 表达式**: 部分动态生成的表达式类型可能需要通过反射回退处理
6. **增量 patch 与 `$id` 稳定性**（重要，下节详述）

## 增量更新与 `$id` 稳定性

`update_material_from_text` 设计上有**增量补丁**（只改属性 / 连线）和**全量重建**两条路径，由 Differ 输出的 `NumStructural` 决定走哪条：

```
NumStructural == 0  →  FMatLangPatcher.Apply（增量；仅改属性 / 连线）
NumStructural >  0  →  ClearMaterial + 重新创建所有 expression（全量）
```

但 Differ 把 `$id` 当作匹配两次 export 之间"同一节点"的唯一 key（`MatLangDiffer.cpp::DiffExpressions` 用 `Expr->Id` 建 `OldById` / `NewById`）。而 Exporter 给每个 `UMaterialExpression` 分配 `$id` 时**不读取任何持久化字段**，是按 `$<typeprefix><counter>` 当场生成的（`MatBPExporter.cpp::GetOrAssignId`）。

后果：

- **第一次 import 之后**，DSL 里写的语义 ID（`$base_color`、`$rim_fresnel`）不会被保留——`UMaterialExpression` 上没有任何字段记录"我在 matlang 里叫什么"。
- **下一次 `update_material_from_text` 时**，Step 1 重新 export 当前材质得到 OldAST，IDs 是 `$vec31`、`$mul3`、`$sparam2`...；调用方传入的 NewAST 用的还是 `$base_color` 等语义 ID。两套集合零交集 → Differ 视所有旧节点为 Removed、所有新节点为 Added，全部按 Structural 计数 → 触发全量重建。

实测：41 个节点的描边材质，仅修改两个 scalar 默认值，Diff 报告 87 changes (82 structural)，最终走 full rebuild。

### 什么场景下增量 patch 真的会生效？

只有在**调用方先 export、再修改、再 import**的回路里：

```python
# ✅ 增量 patch 命中
exp = unreal.MatBP2FPPythonBridge.export_material_to_text("/Game/M_Foo.M_Foo")
new_dsl = exp.dsl_text.replace(":default 0.5", ":default 0.85")  # 仅改属性值
unreal.MatBP2FPPythonBridge.update_material_from_text("/Game/M_Foo.M_Foo", new_dsl, True)

# ❌ 全量重建（典型 AI workflow）
my_dsl = build_dsl_with_semantic_ids()  # $base_color, $rim_fresnel, ...
unreal.MatBP2FPPythonBridge.update_material_from_text("/Game/M_Foo.M_Foo", my_dsl, True)
```

### 推荐工作流（Export-Edit-Import 模式）

如果你需要享受增量 patch 的速度（避免大材质全图重排）和精度（节点 GUID 稳定，引用关系不重建），把流程改成：

1. `export_material_to_text` 取当前 DSL；
2. 在导出文本上**就地修改**——不要重写、不要换 `$id`；
3. 把改完的文本通过 `update_material_from_text` 推回。

### 何时无所谓

如果你只关心"材质最终长什么样"，不在意是否走增量 patch，那么这个限制实际不影响功能正确性——全量重建会忠实地按 NewAST 重建图，行为与增量 patch 等价，只是更慢且不保留节点 GUID。

### 对下游钩子（如 BlueprintAutoLayout / DrivenHighlight）的影响

由于全量重建会广播 `OnNodePhase(PostNodeChanges)` 上 N 个 `Added` 事件、且**不**走 `OnPropertyPhase` 路径，依赖生命周期事件的下游消费者会观察到：

- 每次"小改 DSL"都收到一波 N×Added 事件，而不是预期的少量 Modified；
- `OnPropertyPhase` 事件在材质场景下基本不会触发（只有真正 export-edit-import 的回路才会发）；
- 下游若按 NodeGuid 跨 import 跟踪某个语义节点，跟丢——节点 GUID 在每次全量重建时都会变。

## 相关插件

- **AnimBP2FP**：动画蓝图 ⇄ AnimLang DSL 转换（姊妹项目，架构思路一致）
- **BlueprintLisp**：EventGraph ⇄ BlueprintLisp DSL 转换
- **BlueprintAutoLayout**：图节点自动排版。MatBP2FP 通过导入生命周期钩子（`PostNodeChanges` 阶段）与之集成——当导入上下文请求 `AutoLayout` 行为时，材质 DSL 导入完成后会自动整理新增/变更节点的布局。集成为可选：BlueprintAutoLayout 未启用时不影响导入，只是不自动排版。
