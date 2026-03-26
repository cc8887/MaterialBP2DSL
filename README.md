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
