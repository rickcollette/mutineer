# Buccaneer Bytecode and VM Specification

Version: 0.3-draft  
Status: Proposed

This document defines the tokenized `.bc` module format, bytecode instruction set, verifier requirements, and VM execution model for Buccaneer.

## 1. Design goals

- compact tokenized modules
- deterministic execution
- easy implementation in portable C
- strong validation before execution
- immutable shareable loaded modules
- per-session isolated VM state
- explicit host call boundary

A Buccaneer VM is stack-based.

## 2. Module format overview

A `.bc` file contains:

1. fixed header
2. module metadata
3. string pool
4. constant pool
5. symbol tables
6. procedure table
7. dataset declaration table
8. host call import table
9. bytecode section
10. optional debug map section
11. checksum / signature section

### 2.1 Header

```text
Offset  Size  Field
0       4     Magic = "BUCC"
4       2     Format major
6       2     Format minor
8       4     Flags
12      4     Header size
16      4     Metadata offset
20      4     String pool offset
24      4     Constant pool offset
28      4     Symbol table offset
32      4     Procedure table offset
36      4     Dataset table offset
40      4     Host import table offset
44      4     Bytecode offset
48      4     Debug map offset
52      4     File length
56      4     CRC32
```

## 3. Runtime model

A loaded module is immutable. A VM instance contains:

- operand stack
- call stack / frame stack
- local variable slots
- global variable storage
- heap references for strings, arrays, and maps
- pending exception state
- current instruction pointer
- host session binding
- resource counters

## 4. Value model

Runtime value tags:

- `VAL_NULL`
- `VAL_BOOL`
- `VAL_I64`
- `VAL_F64`
- `VAL_STRING`
- `VAL_DATE`
- `VAL_DATETIME`
- `VAL_ARRAY`
- `VAL_MAP`
- `VAL_ERROR`

## 5. Core opcode set

### 5.1 Stack and constants

```text
0x00 NOP
0x01 HALT
0x02 PUSH_NULL
0x03 PUSH_TRUE
0x04 PUSH_FALSE
0x05 PUSH_I64 const_index
0x06 PUSH_F64 const_index
0x07 PUSH_STR string_index
0x08 PUSH_DATE const_index
0x09 PUSH_DATETIME const_index
0x0A POP
0x0B DUP
0x0C SWAP
```

### 5.2 Variable access

```text
0x10 LOAD_GLOBAL slot
0x11 STORE_GLOBAL slot
0x12 LOAD_LOCAL slot
0x13 STORE_LOCAL slot
0x14 LOAD_ARG slot
```

### 5.3 Arithmetic and logic

```text
0x20 ADD
0x21 SUB
0x22 MUL
0x23 DIV
0x24 MOD
0x25 NEG
0x26 EQ
0x27 NE
0x28 LT
0x29 LE
0x2A GT
0x2B GE
0x2C AND
0x2D OR
0x2E NOT
0x2F CONCAT
```

### 5.4 Control flow

```text
0x30 JMP rel32
0x31 JMP_FALSE rel32
0x32 JMP_TRUE rel32
0x33 CALL proc_id argc
0x34 CALL_HOST import_id argc
0x35 RETURN
0x36 RETURN_VALUE
0x37 CHAIN program_name_string_index has_args
0x38 YIELD
```

### 5.5 Aggregate operations

```text
0x40 ARRAY_NEW count
0x41 ARRAY_GET
0x42 ARRAY_SET
0x43 ARRAY_PUSH
0x44 ARRAY_POP
0x45 MAP_NEW count
0x46 MAP_GET
0x47 MAP_SET
0x48 MAP_HAS
0x49 MAP_KEYS
```

### 5.6 Dispatch and select support

```text
0x50 DISPATCH_CALL base_proc_id case_count
0x51 RANGE_TEST
```

### 5.7 Exceptions

```text
0x60 TRY_BEGIN handler_rel32
0x61 TRY_END
0x62 THROW
0x63 RE_THROW
```

### 5.8 Type and conversion helpers

```text
0x70 CAST_I64
0x71 CAST_F64
0x72 CAST_STRING
0x73 CAST_BOOL
0x74 CAST_DATE
0x75 CAST_DATETIME
0x76 TYPEOF
```

### 5.9 Profiling and debug support

```text
0x7E DEBUG_LINE line_id
0x7F PROF_TICK proc_id
```

## 6. Execution semantics

- `CALL` allocates a new frame and transfers control
- `CALL_HOST` resolves a host import and marshals args/return values
- `CHAIN` returns a structured chain result to the host; the VM does not load the target itself
- `HALT` returns a clean exit result equivalent to `DOOR.EXIT(0)`
- `YIELD` returns control to the host scheduler with resumable state

## 7. Exception model

Exceptions are structured runtime events.

Sources include:
- divide by zero
- null or missing value misuse
- bad index
- wrong host return type
- quota exceeded
- host capability denial
- path policy violation
- dataset policy violation
- shared-state conflict if surfaced as exception

## 8. Resource accounting

Each VM instance tracks at least:

- total instructions executed
- current and max stack depth
- heap bytes
- string bytes
- host call count
- users.read throttle counters
- wall-clock slice time
- per-procedure profile ticks if enabled

## 9. Verifier requirements

Before execution, the loader or verifier must reject modules that are malformed or policy-incompatible.

Required checks include:

- valid header and section offsets
- valid CRC or signature if required
- valid string and constant pool references
- valid procedure boundaries
- legal exception regions
- legal stack effects on all reachable paths
- declared capabilities do not exceed policy
- dataset declarations conform to manifest and install metadata

## 10. VM-host interaction results

VM run calls return one of:

- `BUCC_RUN_OK`
- `BUCC_RUN_EXIT`
- `BUCC_RUN_YIELD`
- `BUCC_RUN_CHAIN`
- `BUCC_RUN_ERROR`
- `BUCC_RUN_DISCONNECT`
