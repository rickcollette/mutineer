# Buccaneer Semantic Analysis Specification

Version: 0.3-draft  
Status: Proposed

This document defines name binding, type checking, control-flow analysis, capability analysis, dataset validation, and policy-sensitive semantic rules for Buccaneer.

## 1. Purpose

Semantic analysis turns a parsed AST into a validated program that can be lowered to bytecode.

Major responsibilities:

- bind names
- build symbol tables
- validate declarations and entrypoints
- check types
- check control flow
- resolve host calls
- validate capabilities
- validate datasets and chaining
- prepare lowering information

## 2. Analysis phases

Recommended phase order:

1. metadata collection
2. declaration pre-scan
3. scope construction
4. name binding
5. host symbol resolution
6. type checking
7. control-flow analysis
8. capability and policy analysis
9. constant folding eligibility
10. lowering preparation

## 3. Symbol spaces

Buccaneer has distinct symbol categories:

- metadata declarations
- global variables
- local variables
- parameters
- procedures
- functions
- datasets
- host namespaces and functions

Case-insensitive lookup is required. Duplicate names in the same symbol space are errors.

## 4. Procedure and function rules

- `Main` must exist for executable programs
- handler names such as `OnEnter` and `OnInput` must match required signatures
- functions must return a value on all reachable non-throwing paths
- subs may not return a value
- procedures and functions may not overload by arity in v1
- `ON expr CALL` targets must all resolve to procedures or functions callable in statement position with compatible signatures

## 5. Type system rules

### 5.1 Primitive compatibility

- exact-type assignment is valid
- integer to double is allowed
- null assignment is allowed only where nullable values are supported
- implicit string-to-number and number-to-string conversion is not generally allowed except via explicit builtins

### 5.2 Operators

- arithmetic operators require numeric operands, except `+` which may also concatenate strings
- logical operators require boolean operands
- comparison operators require compatible comparable types
- array indexing requires integer index
- map indexing requires string key

## 6. Host call resolution

Dotted calls like `TERM.PRINTLN(...)` are resolved against the host API catalog.

Semantic analysis must verify:

- the symbol exists
- arity matches
- argument types are assignable
- the module declares the required capability

## 7. Dataset semantics

### 7.1 Declaration checks

- each logical dataset name is unique
- field names are unique within a schema
- field types are valid
- the program references only declared datasets

### 7.2 Ownership rule

Semantic analysis enforces logical ownership, not physical identity. Door source may reference only logical dataset names declared for the application. Physical database names are an install/runtime concern.

## 8. Chain semantics

`CHAIN` rules:

- target must be a string literal in v1
- target must resolve to a declared application program if the manifest is available
- cross-application chain targets are compile-time errors unless explicitly enabled
- args payload must be serializable if a transport format is used

## 9. Users directory semantics and throttling

Sensitive user directory helpers such as `USERS.FIND_BY_HANDLE` require `users.read`.

Rules:

- compile-time: capability required
- runtime: host-enforced throttling required
- linter: repeated lookup inside tight loops should warn
- policy: host may deny enumeration-oriented calls entirely

## 10. Filesystem semantics

For text file helpers:

- paths must be string values
- only allowed virtual roots may be referenced
- constant string paths with illegal roots should be compile-time errors
- non-constant paths are checked at runtime by the host

## 11. L-value and mutability rules

Legal assignment targets:

- local or global variable
- array element
- map entry
- mutable host or dataset field proxy if explicitly supported

Illegal assignment targets include:

- function call result
- literal
- read-only host properties
- read-only BBS database projections

## 12. Control-flow analysis

Required checks include:

- unreachable code after unconditional `RETURN`, `HALT`, `CHAIN`, `DOOR.EXIT`, or `THROW`
- `EXIT FOR` only inside `FOR`
- `EXIT DO` only inside `DO`
- all function paths return or throw
- `TRY/CATCH` blocks are structurally valid

## 13. Lowering preparation outputs

Successful analysis should produce:

- symbol tables
- resolved procedure ids
- local/global slot layouts
- resolved builtin and host import ids
- typed expression annotations
- constant pool candidates
- control-flow facts for branch lowering
- effect/capability summary for the module
