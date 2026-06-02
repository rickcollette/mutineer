# door.json Schema Specification

Version: 0.3-draft  
Status: Proposed

This document defines the `door.json` manifest for installed Buccaneer applications.

## 1. Purpose

`door.json` defines:

- application identity
- programs included in the application
- entry/default program
- capabilities
- datasets
- virtual assets
- install/runtime policy hints

## 2. Required top-level fields

```json
{
  "schema_version": "1.0",
  "app_id": "trivia",
  "name": "Trivia Door",
  "version": "1.0.0",
  "default_program": "main",
  "programs": {
    "main": { "module": "main.bc" }
  }
}
```

Required fields:

- `schema_version`
- `app_id`
- `name`
- `version`
- `default_program`
- `programs`

## 3. Full recommended shape

```json
{
  "schema_version": "1.0",
  "app_id": "trivia",
  "name": "Trivia Door",
  "version": "1.0.0",
  "default_program": "main",
  "min_host_api": "1.0",
  "capabilities": [
    "term.io",
    "user.read",
    "data.read",
    "data.write",
    "door.chain"
  ],
  "programs": {
    "main": {
      "module": "main.bc",
      "source": "main.bucc",
      "description": "Main menu"
    },
    "play_round": { "module": "play_round.bc" },
    "scores": { "module": "scores.bc" }
  },
  "datasets": {
    "scores": {
      "schema_version": 1,
      "fields": {
        "name": "STRING",
        "points": "INTEGER",
        "updated_at": "DATETIME"
      }
    }
  }
}
```

## 4. Validation rules

- `default_program` must exist in `programs`
- every referenced module path must be inside the package root
- every program id must be unique
- every dataset name must be unique
- no path traversal (`..`) in module or asset paths
