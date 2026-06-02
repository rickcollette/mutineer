<!-- generated-by: gsd-doc-writer -->

# PLANK Networking

PLANK (Packet Link for Area Networked Knowledge) is Mutineer's store-and-forward message networking protocol. Nodes exchange signed CBOR objects in bundles over TLS links or offline packet files.

## Overview

```
┌─────────────┐     TLS link      ┌─────────────┐
│  BBS Node A │ ◄──────────────► │  BBS Node B │
│  (mutineer) │                   │  (mutineer) │
└──────┬──────┘                   └──────┬──────┘
       │                                 │
       ▼                                 ▼
   plankd daemon                    plankd daemon
       │                                 │
       ▼                                 ▼
   SQLite plank_* tables          SQLite plank_* tables
```

PLANK runs **outside** the main BBS process. Daemons share the same SQLite database as `mutineer`.

## Protocol Stack

| Layer | Implementation |
|-------|----------------|
| Transport | TLS (OpenSSL) on default port 2930 |
| Framing | Length-prefixed frames (`max_frame_bytes`) |
| Bundles | Compressed object collections (optional ZSTD) |
| Objects | CBOR-encoded, Ed25519 signed envelopes |
| Storage | Content-addressed `plank_objects` table |
| Sync | Journal-based cursor tracking per link |

Sources: `src/plank/` — see `include/plank/plank.h` for public API.

## Daemons

| Daemon | Purpose |
|--------|---------|
| `plankd` | Link manager — outbound/inbound sync with peers |
| `coved` | COVE hub — fan-out to downstream nodes |

Build:

```bash
cmake --build build --target plank
```

Run (typical):

```bash
build/plankd -c conf/mutineer.conf
build/coved -c conf/mutineer.conf
```

## CLI Tools

| Tool | Purpose |
|------|---------|
| `plankctl` | Administrative control |
| `plankpack` | Bundle pack/unpack for offline exchange |
| `plank-offline` | Offline import/export operations |

### plankctl Commands

```bash
plankctl status              # Node status
plankctl init                # Initialize node identity
plankctl peers               # List configured peers
plankctl links               # List configured links
plankctl link-add            # Add new link
plankctl areas               # List PLANK areas
plankctl queue               # Outbound queue
plankctl journal             # Object journal entries
plankctl deadletters         # Failed deliveries
plankctl quarantine          # Quarantined objects
plankctl audit               # Audit log
plankctl rescan              # Trigger journal rescan
plankctl requeue             # Requeue dead letters
plankctl verify              # Integrity verification
plankctl export-key          # Export node public key
```

Options: `-c` config file, `-d` database file, `-V` version.

## Schema

PLANK tables are defined in `sql/plank_schema.sql`. Apply after base `schema.sql`:

```bash
sqlite3 data/mutineer.db < sql/schema.sql
sqlite3 data/mutineer.db < sql/plank_schema.sql
```

### Core Tables

| Table | Purpose |
|-------|---------|
| `plank_node_identity` | Local node ID, keys, name (singleton) |
| `plank_peers` | Known peer nodes and trust levels |
| `plank_links` | Configured links (host, port, direction) |
| `plank_link_sessions` | Active link sessions |
| `plank_link_cursors` | Sync cursors per link/direction |
| `plank_areas` | Network-aware message areas |
| `plank_area_policies` | Moderation and ACL policies |
| `plank_subscriptions` | Per-link area subscriptions |
| `plank_objects` | Immutable content-addressed objects |
| `plank_messages` | Denormalized message objects |
| `plank_attachments` | Attachment metadata |
| `plank_journal` | Append-only event log |
| `plank_outbound_queue` | Pending outbound objects |
| `plank_import_history` | Bundle import deduplication |
| `plank_dedupe` | Object deduplication window |
| `plank_deadletters` | Failed delivery queue |
| `plank_quarantine` | Policy-violating objects |
| `plank_moderation` | Moderation events |
| `plank_receipts` | Delivery acknowledgements |
| `plank_routes` | Direct message routing |
| `plank_user_exports` | Per-user offline packets |
| `plank_user_replies` | Imported user reply bundles |
| `plank_audit` | Audit trail |
| `plank_config` | PLANK-specific config key/value |

Full table reference: [Database Schema](reference/database.md).

### COVE Tables

| Table | Purpose |
|-------|---------|
| `cove_downstream` | Downstream link tracking |
| `cove_upstream` | Upstream link tracking |
| `cove_fanout_queue` | Area fan-out delivery queue |

## Node Identity

`plankctl init` generates:

- 16-byte random `node_id`
- Ed25519 signing keypair
- X25519 link authentication keys
- Node name and network name

Stored in `plank_node_identity` (singleton row, `id=1`).

Peer trust levels: 0=untrusted, 1=configured, 2=verified.

## Area Mapping

PLANK areas link to local BBS message areas via `plank_areas.local_area_id`:

```
area_addr = "slug@origin_node@network"
```

When a user posts to a mapped local area, a PLANK object may be journaled and queued for export to subscribed links.

## Offline Exchange

For air-gapped or dial-up nodes:

```bash
plankpack export -o bundle.plk ...
plankpack import -i bundle.plk ...
plank-offline export ...
plank-offline import ...
```

Bundles are signed, deduplicated on import via `plank_import_history`.

## Security

- Ed25519 signatures on all objects
- TLS with certificate fingerprint pinning (`tls_fingerprint` in peers)
- Quarantine for policy violations
- Moderation events propagated to peers
- Max hop count (default 16) prevents routing loops

## Optional Compression

When `libzstd` is available at build time, bundles may be ZSTD compressed (`HAVE_ZSTD=1`).

## Testing

```bash
ctest --test-dir build -R plank_
# test_plank_cbor, test_plank_crypto, test_plank_store,
# test_plank_bundle, test_plank_link, test_plank_route, test_plank_policy
```

## Integration with BBS

- BBS core does not embed PLANK protocol handling
- Shared SQLite database connects subsystems
- Message posts in linked areas create export candidates
- User offline packets tracked in `plank_user_exports`
- Scheduler/cron may invoke `plankd` and pack tools

## Related Documentation

- [Reference: Database](reference/database.md)
- [CLI Tools](cli-tools.md)
- [Messages and Mail](messages-and-mail.md)
- Legacy: `docs/PLANK_ADMIN.md`, `SPECS/PLANK/`
