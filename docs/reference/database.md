<!-- generated-by: gsd-doc-writer -->

# Database Schema Reference

Mutineer BBS uses SQLite with schema defined in `sql/schema.sql` (core BBS) and `sql/plank_schema.sql` (PLANK extension). Apply both for full functionality.

```bash
sqlite3 data/mutineer.db < sql/schema.sql
sqlite3 data/mutineer.db < sql/plank_schema.sql
```

`PRAGMA foreign_keys = ON` is enabled.

## Meta

| Table | Purpose |
|-------|---------|
| `meta` | Key/value store for schema version and flags |

## Users and Security

| Table | Purpose |
|-------|---------|
| `security_levels` | Level definitions, time limits, ratios, permissions |
| `validation_levels` | Account validation workflow rules |
| `users` | User accounts (handle, password hash, profile, stats) |
| `access_lists` | Named ACS expressions |
| `subscription_types` | Subscription product definitions |
| `user_subscriptions` | Active/expired user subscriptions |
| `user_votes` | Per-user vote answers (topics 1–25) |

### users Key Columns

| Column | Purpose |
|--------|---------|
| `handle` | Login name (unique) |
| `pw_hash` | Password hash (PBKDF2/Argon2) |
| `security_level_id` | FK to security_levels |
| `flags` | AR flags bitset (A–Z) |
| `ac_flags` | Restriction flags |
| `status_flags` | locked/deleted/expert/ansi/etc. |
| `credits` / `file_points` | Economy |
| `timebank` | Stored minutes |
| `logged_on` / `msg_post` / `uploads` / `downloads` | Stats |
| `expires_at` | Account expiration |
| `signature` / `tagline` | Message appendages |

## Nodes and Sessions

| Table | Purpose |
|-------|---------|
| `nodes` | Active node status (node_num, user_id, status, IP) |
| `call_history` | Login/logout history with duration |

## Conferences

| Table | Purpose |
|-------|---------|
| `conferences` | Conference definitions (key, name, ACS) |
| `conference_membership` | User ↔ conference joins |

## Messages

| Table | Purpose |
|-------|---------|
| `message_areas` | Area definitions with ACS and anon policy |
| `messages` | Message records (threading, attributes) |
| `mail_packets` | Generated QWK/Fido mail packets |
| `drafts` | Auto-saved composition drafts |
| `user_msg_scan_areas` | Per-user scan toggle (MZ command) |

### messages Key Columns

| Column | Purpose |
|--------|---------|
| `area_id` | FK to message_areas |
| `reply_to` / `thread_root` | Threading |
| `to_user` | Private mail recipient |
| `attr` / `net_attr` | Local and FidoNet attributes |
| `file_attached` | Attachment filename |

## Files

| Table | Purpose |
|-------|---------|
| `file_areas` | File area definitions |
| `files` | File records with metadata and hash |
| `upload_queue` | Pending upload approvals |
| `download_queue` | Pending download requests |

## Voting

| Table | Purpose |
|-------|---------|
| `votes` | Vote topics |
| `vote_choices` | Answer choices |
| `vote_ballots` | Cast ballots (one per user per vote) |

## System Content

| Table | Purpose |
|-------|---------|
| `automsg` | Single auto-message (id=1) |
| `bulletins` | Sysop bulletins with ACS |
| `oneliners` | Public one-liner messages |
| `short_messages` | Private short messages (SMW) |

## Events and Doors

| Table | Purpose |
|-------|---------|
| `events` | Scheduler events (cron, logon, permission) |
| `doors` | Door program definitions |
| `protocols` | File transfer protocol definitions |

## Statistics

| Table | Purpose |
|-------|---------|
| `stats` | Lifetime counters (singleton id=1) |
| `daily_stats` | Today's counters (singleton) |
| `history` | Per-day archived stats |
| `system_info` | BBS metadata (name, totals) |
| `logs` | Application log entries |

## FidoNet

| Table | Purpose |
|-------|---------|
| `fido_akas` | AKA addresses (zone:net/node.point) |
| `fido_echolinks` | Echo tag ↔ message area links |
| `fido_netmail` | Outbound netmail queue |
| `fido_echomail_queue` | Echomail export queue |

## QWK Network

| Table | Purpose |
|-------|---------|
| `qwk_hubs` | QWK hub definitions |
| `qwk_area_links` | Local area ↔ hub conference mapping |
| `qwk_packet_queue` | Pending QWK/REP packets |

## Chat

| Table | Purpose |
|-------|---------|
| `chat_logs` | Structured chat message log |

## PLANK Core (`sql/plank_schema.sql`)

| Table | Purpose |
|-------|---------|
| `plank_node_identity` | Local node ID and keys (singleton) |
| `plank_peers` | Known peer nodes |
| `plank_links` | Configured network links |
| `plank_link_sessions` | Active link sessions |
| `plank_link_cursors` | Sync cursors per link |
| `plank_areas` | Network message areas |
| `plank_area_policies` | Moderation policies |
| `plank_subscriptions` | Per-link area subscriptions |
| `plank_objects` | Content-addressed CBOR objects |
| `plank_messages` | Denormalized message objects |
| `plank_attachments` | Attachment metadata |
| `plank_message_attachments` | Message ↔ attachment map |
| `plank_journal` | Append-only object journal |
| `plank_moderation` | Moderation events |
| `plank_receipts` | Delivery acknowledgements |
| `plank_import_history` | Bundle import dedup |
| `plank_dedupe` | Object deduplication window |
| `plank_deadletters` | Failed delivery queue |
| `plank_quarantine` | Policy-violating objects |
| `plank_user_exports` | User offline packet exports |
| `plank_user_replies` | Imported user reply bundles |
| `plank_routes` | Direct message routing |
| `plank_outbound_queue` | Pending outbound objects |
| `plank_audit` | Audit trail |
| `plank_config` | PLANK key/value settings |

## COVE Tables

| Table | Purpose |
|-------|---------|
| `cove_downstream` | Downstream link tracking |
| `cove_upstream` | Upstream link tracking |
| `cove_fanout_queue` | Fan-out delivery queue |

## Key Indexes

| Index | Table | Column(s) |
|-------|-------|-----------|
| `idx_users_handle` | users | handle |
| `idx_messages_area` | messages | area_id |
| `idx_messages_user` | messages | user_id |
| `idx_files_area` | files | area_id |
| `idx_nodes_status` | nodes | status |
| `idx_oneliners_posted` | oneliners | posted_at DESC |
| `idx_fido_netmail_status` | fido_netmail | status |
| `idx_qwk_packet_queue_status` | qwk_packet_queue | status |
| `idx_plank_objects_class` | plank_objects | object_class |
| `idx_plank_journal_state` | plank_journal | processing_state |

## Schema Application

`mutineer-initbbs` applies schema automatically. Manual application:

```bash
sqlite3 data/mutineer.db < sql/schema.sql
sqlite3 data/mutineer.db < sql/plank_schema.sql
```

Verify:

```bash
sqlite3 data/mutineer.db ".tables"
mutineer-maint integrity -v
```

## Maintenance

| Command | Effect |
|---------|--------|
| `mutineer-maint vacuum` | Reclaim space |
| `mutineer-maint reindex` | Rebuild indexes |
| `mutineer-msgpack` | Prune old messages |
| `mutineer-filepack` | Prune file records |
| `mutineer-userpack` | Prune inactive users |

## Related Documentation

- [Architecture](../architecture.md)
- [PLANK Networking](../networking-plank.md)
- [Messages and Mail](../messages-and-mail.md)
- [Files and Protocols](../files-and-protocols.md)
