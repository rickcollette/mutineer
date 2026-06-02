# Buccaneer AST Specification

Version: 0.3-draft  
Status: Proposed

This document defines the abstract syntax tree used between parsing and semantic analysis.

## 1. AST goals

- represent parsed Buccaneer syntax losslessly enough for diagnostics
- normalize superficial syntax differences
- separate syntax from semantics
- support formatter round-tripping where practical
- make later compiler phases deterministic

## 2. Global AST invariants

- every node has a source span
- every declaration node has a stable node id
- identifier casing from source is preserved for diagnostics
- comments may be attached as trivia, not semantic children
- no semantic type info is required on initial parse nodes
- parser output may include error nodes for recovery

## 3. Root structure

```text
Module
  metadata: [MetadataDecl]
  globals: [VarDecl]
  procedures: [ProcDecl | FuncDecl]
  trivia: [CommentTrivia]
```

## 4. Metadata nodes

Kinds:

- `ProgramMeta`
- `VersionMeta`
- `AuthorMeta`
- `DescriptionMeta`
- `CapabilityMeta`
- `OptionMeta`
- `DatasetMeta`

## 5. Declaration nodes

### 5.1 Variable declaration

```text
VarDecl
  name: Identifier
  type_ref: TypeRef
  initializer: Expr?
  storage_class: Global | Local
  span: SourceSpan
```

### 5.2 Procedure declaration

```text
ProcDecl
  name: Identifier
  params: [ParamDecl]
  body: BlockStmt
  flags: ProcFlags
  span: SourceSpan
```

### 5.3 Function declaration

```text
FuncDecl
  name: Identifier
  params: [ParamDecl]
  return_type: TypeRef
  body: BlockStmt
  flags: ProcFlags
  span: SourceSpan
```

## 6. Type nodes

```text
TypeRef =
    ScalarTypeRef(name)
  | ArrayTypeRef(element_type)
  | MapTypeRef(key_type=STRING, value_type)
  | ErrorTypeRef
```

## 7. Statement nodes

```text
Stmt =
    BlockStmt
  | VarDeclStmt
  | AssignStmt
  | ExprStmt
  | IfStmt
  | SelectStmt
  | WhileStmt
  | DoLoopStmt
  | ForStmt
  | TryCatchStmt
  | ReturnStmt
  | ExitStmt
  | HaltStmt
  | ThrowStmt
  | ChainStmt
```

## 8. Expression nodes

```text
Expr =
    IdentifierExpr
  | LiteralExpr
  | UnaryExpr
  | BinaryExpr
  | CallExpr
  | MemberExpr
  | IndexExpr
  | ArrayLiteralExpr
  | MapLiteralExpr
  | QualifiedNameExpr
  | ErrorExpr
```

## 9. L-value subset

Only these expressions are assignable:

- `IdentifierExpr`
- `IndexExpr`
- `MemberExpr` if semantically allowed

## 10. Parser normalization rules

Parser should normalize the following into common AST shapes:

- `LET x = y` and `x = y` -> `AssignStmt`
- `CALL Foo()` and `Foo()` in statement position -> `ExprStmt(CallExpr(...))`
- `HALT` -> dedicated `HaltStmt`
- dotted names such as `TERM.PRINTLN` -> `QualifiedNameExpr` or nested `MemberExpr`

## 11. Source spans

Every node has:

```text
SourceSpan
  file_id
  start_line
  start_col
  end_line
  end_col
```

## 12. AST-to-semantics contract

Semantic analysis may annotate or side-table the AST with:

- symbol bindings
- inferred or checked types
- constant values
- control-flow facts
- capability usage
- procedure ids
