# Buccaneer Packaging and Install Specification

Version: 0.3-draft  
Status: Proposed

This document defines how Buccaneer applications are packaged, installed, activated, upgraded, and removed.

## 1. Packaging unit

A Buccaneer application package contains:

- `door.json`
- one or more `.bc` modules
- optional `.bucc` source
- optional `.bmap` debug maps
- optional `assets/`

## 2. Recommended package layout

```text
trivia/
  door.json
  main.bc
  play_round.bc
  scores.bc
  assets/
    welcome.ans
```

## 3. Install stages

### 3.1 Stage and validate

Installer must:

1. unpack to staging
2. validate `door.json`
3. validate module headers and verifier rules
4. validate program table and module paths
5. resolve capabilities against host policy
6. generate physical dataset identities
7. prepare install metadata

### 3.2 Activate

Activation copies the validated package into the runtime location, registers the application, and warms module cache if desired.

### 3.3 Rollback

Upgrade should be atomic enough that failure leaves either the old version active or the new version fully active.

## 4. Generated dataset mappings

During install, the host generates physical backing identities for each logical dataset. Door source and manifest never see or control those names.

## 5. CHAIN resolution

`CHAIN "scores"` resolves against the installed application's `programs` table.

Cross-application chain is denied unless host policy explicitly allows it.
