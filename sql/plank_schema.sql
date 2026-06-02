-- PLANK/COVE Schema Extension for Mutineer BBS
-- Packet Link for Area Networked Knowledge / Central Offline Vertex Exchange
-- Version: 1.0
--
-- This schema extends the Mutineer BBS database with PLANK networking tables.
-- All tables use the plank_ or cove_ prefix for clear namespacing.
-- Run this after the base schema.sql has been applied.

PRAGMA foreign_keys = ON;

-- ============================================================================
-- NODE IDENTITY AND TRUST
-- ============================================================================

-- Local node identity and configuration
CREATE TABLE IF NOT EXISTS plank_node_identity (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  node_id BLOB NOT NULL,                      -- 16 bytes, randomly generated
  node_name TEXT NOT NULL,                    -- e.g., "portroyal"
  network_name TEXT NOT NULL,                 -- e.g., "blackflag"
  signing_key_pub BLOB NOT NULL,              -- Ed25519 public key (32 bytes)
  signing_key_priv BLOB NOT NULL,             -- Ed25519 private key (64 bytes, encrypted)
  link_key_pub BLOB,                          -- X25519 public key for link auth
  link_key_priv BLOB,                         -- X25519 private key (encrypted)
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at TEXT NOT NULL DEFAULT (datetime('now')),
  software_name TEXT NOT NULL DEFAULT 'Mutineer',
  software_version TEXT NOT NULL DEFAULT '1.0',
  is_cove INTEGER NOT NULL DEFAULT 0,         -- 1 if this node operates as a COVE
  max_bundle_bytes INTEGER NOT NULL DEFAULT 16777216,  -- 16MB default
  max_frame_bytes INTEGER NOT NULL DEFAULT 65536,      -- 64KB default
  poll_interval_sec INTEGER NOT NULL DEFAULT 300       -- 5 minutes default
);

-- Known peer nodes (trust store)
CREATE TABLE IF NOT EXISTS plank_peers (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  node_id BLOB NOT NULL UNIQUE,               -- 16 bytes
  node_name TEXT NOT NULL,
  network_name TEXT NOT NULL,
  node_addr TEXT NOT NULL UNIQUE,             -- "node_name@network_name"
  signing_key_pub BLOB NOT NULL,              -- Ed25519 public key
  tls_fingerprint TEXT,                       -- SHA-256 of TLS cert
  first_seen_at TEXT NOT NULL DEFAULT (datetime('now')),
  last_seen_at TEXT,
  trust_level INTEGER NOT NULL DEFAULT 0,     -- 0=untrusted, 1=configured, 2=verified
  status INTEGER NOT NULL DEFAULT 1,          -- 0=disabled, 1=active, 2=suspended
  notes TEXT
);

CREATE INDEX IF NOT EXISTS idx_plank_peers_addr ON plank_peers(node_addr);

-- ============================================================================
-- LINK CONFIGURATION AND STATE
-- ============================================================================

-- Configured links to peer nodes
CREATE TABLE IF NOT EXISTS plank_links (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  link_id BLOB NOT NULL UNIQUE,               -- 16 bytes, randomly generated
  link_name TEXT NOT NULL,                    -- human-readable name
  peer_id INTEGER NOT NULL,                   -- references plank_peers
  remote_host TEXT NOT NULL,                  -- hostname or IP
  remote_port INTEGER NOT NULL DEFAULT 2930,  -- PLANK default port
  direction INTEGER NOT NULL DEFAULT 3,       -- 1=inbound, 2=outbound, 3=both
  enabled INTEGER NOT NULL DEFAULT 1,
  paused INTEGER NOT NULL DEFAULT 0,
  retry_initial_sec INTEGER NOT NULL DEFAULT 60,
  retry_max_sec INTEGER NOT NULL DEFAULT 3600,
  retry_limit INTEGER NOT NULL DEFAULT 10,
  last_connect_at TEXT,
  last_success_at TEXT,
  last_error TEXT,
  retry_count INTEGER NOT NULL DEFAULT 0,
  state INTEGER NOT NULL DEFAULT 0,           -- link state enum
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at TEXT NOT NULL DEFAULT (datetime('now')),
  FOREIGN KEY(peer_id) REFERENCES plank_peers(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_links_peer ON plank_links(peer_id);
CREATE INDEX IF NOT EXISTS idx_plank_links_state ON plank_links(state);

-- Link session state (active sessions)
CREATE TABLE IF NOT EXISTS plank_link_sessions (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  link_id INTEGER NOT NULL,
  session_nonce BLOB NOT NULL,                -- 32 bytes
  peer_nonce BLOB,                            -- 32 bytes from peer
  started_at TEXT NOT NULL DEFAULT (datetime('now')),
  authenticated_at TEXT,
  last_activity_at TEXT,
  state INTEGER NOT NULL DEFAULT 0,
  correlation_seq INTEGER NOT NULL DEFAULT 0,
  bytes_sent INTEGER NOT NULL DEFAULT 0,
  bytes_recv INTEGER NOT NULL DEFAULT 0,
  bundles_sent INTEGER NOT NULL DEFAULT 0,
  bundles_recv INTEGER NOT NULL DEFAULT 0,
  FOREIGN KEY(link_id) REFERENCES plank_links(id)
);

-- Link cursors for sync state
CREATE TABLE IF NOT EXISTS plank_link_cursors (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  link_id INTEGER NOT NULL,
  direction INTEGER NOT NULL,                 -- 1=export, 2=import
  journal_seq INTEGER NOT NULL DEFAULT 0,     -- last processed journal sequence
  updated_at TEXT NOT NULL DEFAULT (datetime('now')),
  UNIQUE(link_id, direction),
  FOREIGN KEY(link_id) REFERENCES plank_links(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_link_cursors_link ON plank_link_cursors(link_id);

-- ============================================================================
-- AREA DEFINITIONS AND SUBSCRIPTIONS
-- ============================================================================

-- PLANK area definitions (network-aware areas)
CREATE TABLE IF NOT EXISTS plank_areas (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  area_addr TEXT NOT NULL UNIQUE,             -- "slug@origin_node@network"
  area_slug TEXT NOT NULL,
  origin_node_addr TEXT NOT NULL,
  title TEXT NOT NULL,
  description TEXT,
  distribution_mode INTEGER NOT NULL DEFAULT 1,  -- FANOUT, SUBSCRIPTION_ONLY, etc.
  default_retention INTEGER NOT NULL DEFAULT 2,  -- retention class
  posting_policy INTEGER NOT NULL DEFAULT 1,     -- moderation mode
  attachment_policy INTEGER NOT NULL DEFAULT 1,
  max_message_bytes INTEGER NOT NULL DEFAULT 65536,
  max_attachment_bytes INTEGER NOT NULL DEFAULT 10485760,
  retention_days INTEGER NOT NULL DEFAULT 365,
  max_hops INTEGER NOT NULL DEFAULT 16,
  status INTEGER NOT NULL DEFAULT 1,          -- 0=inactive, 1=active, 2=readonly
  local_area_id INTEGER,                      -- link to BBS message_areas if mapped
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at TEXT NOT NULL DEFAULT (datetime('now')),
  object_id BLOB,                             -- hash of AreaDefinition object
  FOREIGN KEY(local_area_id) REFERENCES message_areas(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_areas_slug ON plank_areas(area_slug);
CREATE INDEX IF NOT EXISTS idx_plank_areas_origin ON plank_areas(origin_node_addr);
CREATE INDEX IF NOT EXISTS idx_plank_areas_local ON plank_areas(local_area_id);

-- Area policies (moderation, ACL, limits)
CREATE TABLE IF NOT EXISTS plank_area_policies (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  area_id INTEGER NOT NULL,
  moderation_mode INTEGER NOT NULL DEFAULT 1,
  allowed_roles TEXT,                         -- JSON array of role names
  link_acl TEXT,                              -- JSON array of allowed link IDs
  allowed_body_formats TEXT,                  -- JSON array of format enums
  quarantine_on_violation INTEGER NOT NULL DEFAULT 1,
  duplicate_window_days INTEGER NOT NULL DEFAULT 90,
  effective_at TEXT NOT NULL DEFAULT (datetime('now')),
  object_id BLOB,
  FOREIGN KEY(area_id) REFERENCES plank_areas(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_area_policies_area ON plank_area_policies(area_id);

-- Area subscriptions per link
CREATE TABLE IF NOT EXISTS plank_subscriptions (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  link_id INTEGER NOT NULL,
  area_id INTEGER NOT NULL,
  action INTEGER NOT NULL DEFAULT 1,          -- SUBSCRIBE, UNSUBSCRIBE, PAUSE, RESUME
  effective_at TEXT NOT NULL DEFAULT (datetime('now')),
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  object_id BLOB,                             -- hash of SubscriptionEvent
  UNIQUE(link_id, area_id),
  FOREIGN KEY(link_id) REFERENCES plank_links(id),
  FOREIGN KEY(area_id) REFERENCES plank_areas(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_subscriptions_link ON plank_subscriptions(link_id);
CREATE INDEX IF NOT EXISTS idx_plank_subscriptions_area ON plank_subscriptions(area_id);

-- ============================================================================
-- OBJECT STORAGE
-- ============================================================================

-- All PLANK objects (immutable, content-addressed)
CREATE TABLE IF NOT EXISTS plank_objects (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  object_id BLOB NOT NULL UNIQUE,             -- 32 bytes SHA-256
  object_class INTEGER NOT NULL,              -- class enum
  origin_node_id BLOB NOT NULL,               -- 16 bytes
  origin_node_addr TEXT NOT NULL,
  created_at_ts INTEGER NOT NULL,             -- Unix timestamp
  accepted_at TEXT NOT NULL DEFAULT (datetime('now')),
  signature BLOB NOT NULL,
  sig_alg INTEGER NOT NULL DEFAULT 1,         -- SIG_ED25519
  body_cbor BLOB NOT NULL,                    -- canonical CBOR of body
  envelope_cbor BLOB NOT NULL,                -- full canonical CBOR envelope
  verified INTEGER NOT NULL DEFAULT 0,        -- 1 if signature verified
  local_seq INTEGER                           -- journal sequence if journaled
);

CREATE INDEX IF NOT EXISTS idx_plank_objects_class ON plank_objects(object_class);
CREATE INDEX IF NOT EXISTS idx_plank_objects_origin ON plank_objects(origin_node_addr);
CREATE INDEX IF NOT EXISTS idx_plank_objects_created ON plank_objects(created_at_ts);
CREATE INDEX IF NOT EXISTS idx_plank_objects_local_seq ON plank_objects(local_seq);

-- Message objects (denormalized for efficient queries)
CREATE TABLE IF NOT EXISTS plank_messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  object_id BLOB NOT NULL UNIQUE,             -- references plank_objects
  message_type INTEGER NOT NULL,              -- AREA_POST, DIRECT_POST, SYSTEM_POST
  author_user TEXT NOT NULL,
  author_display TEXT,
  from_addr TEXT NOT NULL,
  to_addrs TEXT,                              -- JSON array
  area_addr TEXT,                             -- for area posts
  subject TEXT NOT NULL,
  body_format INTEGER NOT NULL,
  body_text TEXT NOT NULL,
  thread_root_id BLOB,                        -- 32 bytes
  parent_id BLOB,                             -- 32 bytes
  reply_to TEXT,
  retention_class INTEGER NOT NULL,
  visibility INTEGER NOT NULL,
  flags INTEGER NOT NULL DEFAULT 0,
  path TEXT,                                  -- JSON array of node IDs (hex)
  hop_count INTEGER NOT NULL DEFAULT 0,
  local_msg_id INTEGER,                       -- link to BBS messages table
  FOREIGN KEY(object_id) REFERENCES plank_objects(object_id),
  FOREIGN KEY(local_msg_id) REFERENCES messages(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_messages_type ON plank_messages(message_type);
CREATE INDEX IF NOT EXISTS idx_plank_messages_area ON plank_messages(area_addr);
CREATE INDEX IF NOT EXISTS idx_plank_messages_author ON plank_messages(from_addr);
CREATE INDEX IF NOT EXISTS idx_plank_messages_thread ON plank_messages(thread_root_id);
CREATE INDEX IF NOT EXISTS idx_plank_messages_local ON plank_messages(local_msg_id);

-- Attachment metadata objects
CREATE TABLE IF NOT EXISTS plank_attachments (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  object_id BLOB NOT NULL UNIQUE,             -- references plank_objects
  attachment_id BLOB NOT NULL UNIQUE,         -- 32 bytes SHA-256 of content
  filename TEXT NOT NULL,
  mime_type TEXT NOT NULL,
  size_bytes INTEGER NOT NULL,
  sha256 BLOB NOT NULL,                       -- 32 bytes
  compression INTEGER NOT NULL DEFAULT 0,
  retention_class INTEGER NOT NULL,
  blob_path TEXT,                             -- local filesystem path
  FOREIGN KEY(object_id) REFERENCES plank_objects(object_id)
);

CREATE INDEX IF NOT EXISTS idx_plank_attachments_aid ON plank_attachments(attachment_id);

-- Message-to-attachment mapping
CREATE TABLE IF NOT EXISTS plank_message_attachments (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  message_object_id BLOB NOT NULL,
  attachment_id BLOB NOT NULL,
  UNIQUE(message_object_id, attachment_id),
  FOREIGN KEY(message_object_id) REFERENCES plank_messages(object_id),
  FOREIGN KEY(attachment_id) REFERENCES plank_attachments(attachment_id)
);

-- ============================================================================
-- JOURNAL (append-only event log)
-- ============================================================================

CREATE TABLE IF NOT EXISTS plank_journal (
  local_seq INTEGER PRIMARY KEY AUTOINCREMENT,
  object_id BLOB NOT NULL,
  object_class INTEGER NOT NULL,
  accepted_at TEXT NOT NULL DEFAULT (datetime('now')),
  source_kind INTEGER NOT NULL,               -- 1=local, 2=link, 3=import, 4=offline
  source_link_id INTEGER,                     -- link ID if from network
  processing_state INTEGER NOT NULL DEFAULT 0, -- 0=pending, 1=processed, 2=failed
  FOREIGN KEY(object_id) REFERENCES plank_objects(object_id),
  FOREIGN KEY(source_link_id) REFERENCES plank_links(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_journal_object ON plank_journal(object_id);
CREATE INDEX IF NOT EXISTS idx_plank_journal_class ON plank_journal(object_class);
CREATE INDEX IF NOT EXISTS idx_plank_journal_state ON plank_journal(processing_state);

-- ============================================================================
-- MODERATION
-- ============================================================================

CREATE TABLE IF NOT EXISTS plank_moderation (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  object_id BLOB NOT NULL UNIQUE,             -- references plank_objects (ModerationEvent)
  target_object_id BLOB NOT NULL,             -- object being moderated
  area_addr TEXT,
  action INTEGER NOT NULL,                    -- moderation action enum
  reason_code TEXT,
  reason_text TEXT,
  issued_by_node TEXT NOT NULL,
  issued_by_user TEXT,
  effective_at TEXT NOT NULL,
  visibility_scope INTEGER NOT NULL DEFAULT 1,
  propagated INTEGER NOT NULL DEFAULT 0,      -- 1 if sent to peers
  FOREIGN KEY(object_id) REFERENCES plank_objects(object_id)
);

CREATE INDEX IF NOT EXISTS idx_plank_moderation_target ON plank_moderation(target_object_id);
CREATE INDEX IF NOT EXISTS idx_plank_moderation_area ON plank_moderation(area_addr);
CREATE INDEX IF NOT EXISTS idx_plank_moderation_action ON plank_moderation(action);

-- ============================================================================
-- RECEIPTS AND ACKNOWLEDGEMENTS
-- ============================================================================

CREATE TABLE IF NOT EXISTS plank_receipts (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  object_id BLOB UNIQUE,                      -- if this receipt is an object
  target_id BLOB NOT NULL,                    -- bundle or object ID
  target_kind INTEGER NOT NULL,               -- TARGET_BUNDLE, TARGET_OBJECT, TARGET_LINK
  receipt_type INTEGER NOT NULL,              -- receipt type enum
  link_id INTEGER,
  received_at TEXT NOT NULL DEFAULT (datetime('now')),
  accepted_count INTEGER NOT NULL DEFAULT 0,
  duplicate_count INTEGER NOT NULL DEFAULT 0,
  rejected_count INTEGER NOT NULL DEFAULT 0,
  quarantine_count INTEGER NOT NULL DEFAULT 0,
  details TEXT,                               -- JSON
  FOREIGN KEY(link_id) REFERENCES plank_links(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_receipts_target ON plank_receipts(target_id);
CREATE INDEX IF NOT EXISTS idx_plank_receipts_link ON plank_receipts(link_id);
CREATE INDEX IF NOT EXISTS idx_plank_receipts_type ON plank_receipts(receipt_type);

-- ============================================================================
-- IMPORT HISTORY AND DEDUPLICATION
-- ============================================================================

-- Bundle import history
CREATE TABLE IF NOT EXISTS plank_import_history (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  bundle_id BLOB NOT NULL UNIQUE,             -- 32 bytes
  bundle_type INTEGER NOT NULL,
  source_node_id BLOB NOT NULL,
  source_node_addr TEXT NOT NULL,
  first_seen_at TEXT NOT NULL DEFAULT (datetime('now')),
  last_seen_at TEXT NOT NULL DEFAULT (datetime('now')),
  import_result INTEGER NOT NULL,             -- accepted, duplicate, partial, rejected
  source_link_id INTEGER,
  object_count INTEGER NOT NULL DEFAULT 0,
  duplicate_count INTEGER NOT NULL DEFAULT 0,
  rejected_count INTEGER NOT NULL DEFAULT 0,
  FOREIGN KEY(source_link_id) REFERENCES plank_links(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_import_history_source ON plank_import_history(source_node_addr);
CREATE INDEX IF NOT EXISTS idx_plank_import_history_link ON plank_import_history(source_link_id);
CREATE INDEX IF NOT EXISTS idx_plank_import_history_result ON plank_import_history(import_result);

-- Object deduplication window
CREATE TABLE IF NOT EXISTS plank_dedupe (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  object_id BLOB NOT NULL UNIQUE,             -- 32 bytes
  first_seen_at TEXT NOT NULL DEFAULT (datetime('now')),
  last_seen_at TEXT NOT NULL DEFAULT (datetime('now')),
  seen_count INTEGER NOT NULL DEFAULT 1
);

CREATE INDEX IF NOT EXISTS idx_plank_dedupe_first ON plank_dedupe(first_seen_at);

-- ============================================================================
-- DEAD LETTERS AND QUARANTINE
-- ============================================================================

CREATE TABLE IF NOT EXISTS plank_deadletters (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  target_link_id INTEGER NOT NULL,
  target_node_addr TEXT NOT NULL,
  object_ids TEXT NOT NULL,                   -- JSON array of object IDs (hex)
  bundle_id BLOB,
  first_failure_at TEXT NOT NULL DEFAULT (datetime('now')),
  last_failure_at TEXT NOT NULL DEFAULT (datetime('now')),
  last_error_code INTEGER,
  last_error_text TEXT,
  retry_count INTEGER NOT NULL DEFAULT 0,
  state INTEGER NOT NULL DEFAULT 0,           -- 0=pending, 1=requeued, 2=abandoned
  FOREIGN KEY(target_link_id) REFERENCES plank_links(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_deadletters_link ON plank_deadletters(target_link_id);
CREATE INDEX IF NOT EXISTS idx_plank_deadletters_state ON plank_deadletters(state);

CREATE TABLE IF NOT EXISTS plank_quarantine (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  object_id BLOB NOT NULL UNIQUE,
  object_class INTEGER NOT NULL,
  source_link_id INTEGER,
  source_node_addr TEXT,
  quarantine_reason INTEGER NOT NULL,         -- policy violation code
  quarantine_text TEXT,
  quarantined_at TEXT NOT NULL DEFAULT (datetime('now')),
  reviewed_at TEXT,
  reviewed_by TEXT,
  resolution INTEGER NOT NULL DEFAULT 0,      -- 0=pending, 1=released, 2=rejected
  envelope_cbor BLOB NOT NULL,
  FOREIGN KEY(source_link_id) REFERENCES plank_links(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_quarantine_source ON plank_quarantine(source_node_addr);
CREATE INDEX IF NOT EXISTS idx_plank_quarantine_reason ON plank_quarantine(quarantine_reason);
CREATE INDEX IF NOT EXISTS idx_plank_quarantine_resolution ON plank_quarantine(resolution);

-- ============================================================================
-- USER PACKET EXPORTS
-- ============================================================================

CREATE TABLE IF NOT EXISTS plank_user_exports (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  export_id BLOB NOT NULL UNIQUE,             -- 16 bytes
  user_id INTEGER NOT NULL,
  bundle_id BLOB,                             -- 32 bytes, set after creation
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  exported_at TEXT,
  packet_path TEXT,
  areas_included TEXT,                        -- JSON array of area addrs
  message_count INTEGER NOT NULL DEFAULT 0,
  attachment_count INTEGER NOT NULL DEFAULT 0,
  read_state_snapshot TEXT,                   -- JSON of read pointers
  cursor_low INTEGER,
  cursor_high INTEGER,
  status INTEGER NOT NULL DEFAULT 0,          -- 0=pending, 1=exported, 2=imported_reply
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_user_exports_user ON plank_user_exports(user_id);
CREATE INDEX IF NOT EXISTS idx_plank_user_exports_status ON plank_user_exports(status);
CREATE INDEX IF NOT EXISTS idx_plank_user_exports_user_status ON plank_user_exports(user_id, status);
CREATE INDEX IF NOT EXISTS idx_plank_user_exports_cursor ON plank_user_exports(user_id, cursor_high);

-- User packet reply imports
CREATE TABLE IF NOT EXISTS plank_user_replies (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  export_id BLOB NOT NULL,                    -- references original export
  bundle_id BLOB NOT NULL UNIQUE,             -- 32 bytes
  user_id INTEGER NOT NULL,
  imported_at TEXT NOT NULL DEFAULT (datetime('now')),
  message_count INTEGER NOT NULL DEFAULT 0,
  attachment_count INTEGER NOT NULL DEFAULT 0,
  import_result INTEGER NOT NULL,
  details TEXT,                               -- JSON
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_user_replies_export ON plank_user_replies(export_id);
CREATE INDEX IF NOT EXISTS idx_plank_user_replies_user ON plank_user_replies(user_id);
CREATE INDEX IF NOT EXISTS idx_plank_user_replies_result ON plank_user_replies(import_result);

-- ============================================================================
-- COVE-SPECIFIC TABLES
-- ============================================================================

-- COVE downstream link tracking
CREATE TABLE IF NOT EXISTS cove_downstream (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  link_id INTEGER NOT NULL UNIQUE,
  subscribed_areas TEXT,                      -- JSON array of area addrs
  last_sync_at TEXT,
  backlog_count INTEGER NOT NULL DEFAULT 0,
  status INTEGER NOT NULL DEFAULT 1,
  FOREIGN KEY(link_id) REFERENCES plank_links(id)
);

-- COVE upstream link tracking
CREATE TABLE IF NOT EXISTS cove_upstream (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  link_id INTEGER NOT NULL UNIQUE,
  is_primary INTEGER NOT NULL DEFAULT 0,
  last_sync_at TEXT,
  status INTEGER NOT NULL DEFAULT 1,
  FOREIGN KEY(link_id) REFERENCES plank_links(id)
);

-- COVE area fan-out queue
CREATE TABLE IF NOT EXISTS cove_fanout_queue (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  object_id BLOB NOT NULL,
  area_addr TEXT NOT NULL,
  target_link_id INTEGER NOT NULL,
  queued_at TEXT NOT NULL DEFAULT (datetime('now')),
  sent_at TEXT,
  status INTEGER NOT NULL DEFAULT 0,          -- 0=pending, 1=sent, 2=failed
  retry_count INTEGER NOT NULL DEFAULT 0,
  FOREIGN KEY(object_id) REFERENCES plank_objects(object_id),
  FOREIGN KEY(target_link_id) REFERENCES plank_links(id)
);

CREATE INDEX IF NOT EXISTS idx_cove_fanout_status ON cove_fanout_queue(status);
CREATE INDEX IF NOT EXISTS idx_cove_fanout_link ON cove_fanout_queue(target_link_id);
CREATE INDEX IF NOT EXISTS idx_cove_fanout_area ON cove_fanout_queue(area_addr);

-- ============================================================================
-- AUDIT LOG
-- ============================================================================

CREATE TABLE IF NOT EXISTS plank_audit (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  event_type TEXT NOT NULL,                   -- link_connect, bundle_recv, moderation, etc.
  event_time TEXT NOT NULL DEFAULT (datetime('now')),
  link_id INTEGER,
  node_addr TEXT,
  user_handle TEXT,
  object_id BLOB,
  details TEXT,                               -- JSON
  FOREIGN KEY(link_id) REFERENCES plank_links(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_audit_type ON plank_audit(event_type);
CREATE INDEX IF NOT EXISTS idx_plank_audit_time ON plank_audit(event_time);
CREATE INDEX IF NOT EXISTS idx_plank_audit_link ON plank_audit(link_id);

-- ============================================================================
-- ROUTING TABLES
-- ============================================================================

-- Direct message routing table
CREATE TABLE IF NOT EXISTS plank_routes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  dest_node_addr TEXT NOT NULL,               -- destination node
  next_hop_link_id INTEGER NOT NULL,          -- which link to use
  hop_count INTEGER NOT NULL DEFAULT 1,
  priority INTEGER NOT NULL DEFAULT 0,        -- higher = preferred
  status INTEGER NOT NULL DEFAULT 1,          -- 0=disabled, 1=active
  updated_at TEXT NOT NULL DEFAULT (datetime('now')),
  UNIQUE(dest_node_addr, next_hop_link_id),
  FOREIGN KEY(next_hop_link_id) REFERENCES plank_links(id)
);

CREATE INDEX IF NOT EXISTS idx_plank_routes_dest ON plank_routes(dest_node_addr);

-- ============================================================================
-- OUTBOUND QUEUE
-- ============================================================================

CREATE TABLE IF NOT EXISTS plank_outbound_queue (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  link_id INTEGER NOT NULL,
  object_id BLOB NOT NULL,
  queued_at TEXT NOT NULL DEFAULT (datetime('now')),
  priority INTEGER NOT NULL DEFAULT 0,
  status INTEGER NOT NULL DEFAULT 0,          -- 0=pending, 1=bundled, 2=sent, 3=acked
  bundle_id BLOB,                             -- set when bundled
  FOREIGN KEY(link_id) REFERENCES plank_links(id),
  FOREIGN KEY(object_id) REFERENCES plank_objects(object_id)
);

CREATE INDEX IF NOT EXISTS idx_plank_outbound_link ON plank_outbound_queue(link_id);
CREATE INDEX IF NOT EXISTS idx_plank_outbound_status ON plank_outbound_queue(status);

-- ============================================================================
-- CONFIGURATION METADATA
-- ============================================================================

CREATE TABLE IF NOT EXISTS plank_config (
  k TEXT PRIMARY KEY,
  v TEXT NOT NULL
);

-- Default configuration values
INSERT OR IGNORE INTO plank_config (k, v) VALUES
  ('bundle_dedupe_days', '30'),
  ('object_dedupe_days', '90'),
  ('retry_initial_sec', '60'),
  ('retry_max_sec', '3600'),
  ('retry_limit', '10'),
  ('deadletter_timeout_sec', '86400'),
  ('quarantine_auto_reject_days', '30'),
  ('max_hop_count', '16'),
  ('clock_drift_tolerance_sec', '300'),
  ('journal_prune_days', '365'),
  ('audit_prune_days', '90');
