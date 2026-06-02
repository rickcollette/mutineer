# PLANK Specification
## Packet Link for Area Networked Knowledge
### Complete protocol, packet, bundle, storage, and integration specification for Mutineer BBS

Version: 1.1-draft
Date: 2026-02-27
Status: Full Design Specification
Target Implementation Language: C
Target Host Application: Mutineer BBS

---

## 1. Purpose

PLANK is the native packet mail and area distribution network for the Mutineer Bulletin Board System. It is a standalone protocol suite, packet format, storage model, and operational contract for moving messages, replies, moderation actions, control events, and attachments between Mutineer systems.

PLANK is not a compatibility layer for FidoNet, QWK, SMTP, IMAP, or any other historical mail network. It is its own network model.

PLANK is built around four ideas:

1. store-and-forward delivery
2. offline-first user workflows
3. area-based distribution as a first-class feature
4. deterministic, signed, binary-safe exchange that is practical to implement in portable C

In PLANK terminology, a node operating as the network's central fan-out and redistribution role is called a **COVE** - **Central Offline Vertex Exchange**.

---

## 2. Scope

This specification defines all required v1 behavior for:

- node identity and trust
- addresses and identifiers
- object schemas
- canonical encoding rules
- wire framing
- session state
- bundle formats
- offline user packet formats
- routing and deduplication
- COVE responsibilities
- moderation and retention
- persistent storage
- error handling
- required Mutineer integration points
- required C module boundaries
- acceptance criteria and mandatory tests

A feature is part of PLANK v1 only if it directly supports:

- node-to-node message exchange
- area distribution
- direct message delivery
- offline packet export/import
- moderation and routing control
- deterministic storage, replay protection, and recovery

---

## 3. Primary Design Goals

PLANK MUST satisfy all of the following goals.

### 3.1 Native Mutineer integration

PLANK MUST be designed specifically for Mutineer user accounts, message bases, moderation flows, sysop tooling, and offline packet workflows.

### 3.2 Offline-first operation

A user MUST be able to export unread content into a portable packet, read and reply while disconnected, and later import the reply packet into Mutineer.

### 3.3 Store-and-forward networking

Mutineer nodes MUST be able to queue outbound work, exchange packets asynchronously, recover from disconnects, and resume without duplication.

### 3.4 Area-first distribution

Public discussion areas are a first-class network primitive. PLANK MUST treat area distribution as a core feature, not an afterthought.

### 3.5 Deterministic exchange

Re-sending the same bundle or object MUST NOT create duplicates. Import operations MUST be idempotent.

### 3.6 Small, practical C implementation

A production PLANK implementation MUST be realistic to build in portable C on Linux and BSD systems without unreasonable runtime complexity.

### 3.7 Security by default

Links MUST be authenticated. Objects MUST be signed. Bundles MUST be signed. Transport MUST be encrypted. Replays MUST be detectable.

### 3.8 Explicit operational behavior

Sync cursors, acknowledgements, retries, dead-letter behavior, quarantine, audit logging, and recovery semantics MUST be explicitly defined.

---

## 4. Non-goals

PLANK v1 intentionally does not attempt to do the following:

1. compatibility with FidoNet packet formats
2. compatibility with QWK packet formats
3. SMTP federation with the public Internet
4. universal identity beyond node-scoped user identity
5. real-time chat
6. distributed consensus across all nodes
7. global search across the full network
8. complex end-to-end encrypted group ratchets
9. arbitrary file synchronization beyond message attachments and offline packets

---

## 5. Normative Language

The key words **MUST**, **MUST NOT**, **REQUIRED**, **SHOULD**, **SHOULD NOT**, and **MAY** are to be interpreted as normative requirements.

---

## 6. Architecture Overview

PLANK consists of six layers:

1. **Local Store Layer**  
   The Mutineer database, spool, attachment store, journal, and packet staging area.

2. **Object Layer**  
   Immutable messages, immutable events, area definitions, policy definitions, receipts, and checkpoints.

3. **Bundle Layer**  
   Portable packet files used for link exchange and user offline packets.

4. **Link Layer**  
   Authenticated node-to-node sessions over TLS carrying framed PLANK control and bundle traffic.

5. **Routing Layer**  
   Per-area subscription logic, direct message route selection, retry policy, deduplication, and loop prevention.

6. **Presentation Layer**  
   Mutineer user interfaces, sysop tools, area readers, packet export/import menus, and admin views.

### 6.1 Core principle

PLANK replicates **objects and events**, not mutable rows.

- A message body is immutable once accepted.
- A moderation action is a separate event object.
- A withdrawal is a tombstone event, not a destructive delete.
- Local read state is local unless explicitly included in a user packet export.
- Duplicate delivery is harmless because object IDs and bundle IDs are deterministic.

---

## 7. Node Roles

A Mutineer instance may operate as one or more of the following roles.

### 7.1 Standalone node

A local-only BBS with no PLANK peers.

### 7.2 Leaf node

A participating node that originates and receives content and typically peers to one or more upstream systems.

### 7.3 COVE

**Central Offline Vertex Exchange.** A COVE is the network's central fan-out and redistribution role. A COVE is not special because of protocol privilege. It is special because of topology, routing, subscription concentration, and operational responsibility.

A COVE typically:

- maintains many peering links
- queues and redistributes area traffic
- tracks per-link area subscriptions
- stores sync cursors for many neighbors
- fans out moderation and control traffic
- acts as a stable routing point for a network or subnetwork

A COVE MAY also host local users, but that is not required.

### 7.4 Area authority

A node that owns the authoritative definition of one or more named areas.

### 7.5 Transit node

A node that forwards traffic without necessarily hosting local users for a given area.

---

## 8. Terminology

### 8.1 Node
A Mutineer system participating in PLANK exchange.

### 8.2 Link
A configured peering relationship between two nodes.

### 8.3 Area
A named distributed discussion space.

### 8.4 Direct message
A private message addressed to a user at a specific destination node.

### 8.5 Object
An immutable canonical payload with a deterministic ID.

### 8.6 Event
An immutable control object that changes state or policy without mutating the original object.

### 8.7 Bundle
A portable file containing a manifest, record directory, objects, optional attachments, and checkpoint metadata.

### 8.8 User packet
A portable offline packet created for one local Mutineer user and later imported back into Mutineer.

### 8.9 Journal
A monotonically increasing local record of accepted work and processing milestones.

### 8.10 Cursor
A per-link marker indicating how far export or import has progressed.

### 8.11 Tombstone
A moderation event marking an object as withdrawn, hidden, or otherwise suppressed.

### 8.12 COVE ring
A set of COVEs peered together to provide redundancy or a higher-level distribution mesh.

---

## 9. Addressing and Identifiers

PLANK uses human-readable addresses and binary identifiers.

### 9.1 Human-readable addresses

All human-readable addresses MUST be ASCII-safe and canonicalized to lowercase.

#### 9.1.1 Node address

Format:

`node-name@network-name`

Examples:

- `portroyal@blackflag`
- `brigand@mutineer-net`

Constraints:

- `node-name`: 1..32 characters
- `network-name`: 1..32 characters
- allowed characters: `a-z`, `0-9`, `_`, `-`

#### 9.1.2 User address

Format:

`user@node-name@network-name`

Examples:

- `rick@portroyal@blackflag`
- `sysop@brigand@mutineer-net`

Constraints:

- `user`: 1..32 characters
- allowed characters: `a-z`, `0-9`, `.`, `_`, `-`

#### 9.1.3 Area address

Format:

`area-slug@origin-node@network-name`

Examples:

- `general@portroyal@blackflag`
- `retro-tech@brigand@mutineer-net`

Constraints:

- `area-slug`: 1..48 characters
- allowed characters: `a-z`, `0-9`, `_`, `-`, `.`

### 9.2 Binary identifiers

#### 9.2.1 Node ID

16 bytes. Randomly generated when a node is initialized.

#### 9.2.2 Link ID

16 bytes. Randomly generated per configured link.

#### 9.2.3 Object ID

32 bytes. SHA-256 hash of the canonical to-be-signed object encoding.

#### 9.2.4 Attachment ID

32 bytes. SHA-256 hash of the raw attachment bytes.

#### 9.2.5 Bundle ID

32 bytes. SHA-256 hash of the canonical bundle identity material defined in Section 18.5.

#### 9.2.6 Export ID

16 bytes. Random identifier for a user packet export.

### 9.3 Why both names and binary IDs exist

Names are required for sysops and users. Binary IDs are required for exact references, deduplication, replay control, and storage keys.

---

## 10. Trust and Identity Model

PLANK trust is node-centric.

### 10.1 Required node identity material

Every PLANK node MUST have:

- long-term signing key pair
- long-term link authentication key pair
- node ID
- node address
- local trust store of peers

Recommended algorithms:

- Ed25519 for object and bundle signatures
- X25519 for link keying support if used outside TLS
- TLS 1.3 certificate using Ed25519 or ECDSA

### 10.2 Peer admission

A node MUST NOT accept a new link session from an unknown peer unless one of the following is true:

1. the peer is explicitly configured
2. the peer is provisioned through an out-of-band trust bundle
3. local policy explicitly enables controlled onboarding

Mutineer SHOULD ship with explicit peer configuration as the default.

### 10.3 User identity scope

User identity is local to the originating node. PLANK does not attempt to solve global user identity in v1. The tuple of:

- username
- origin node address
- origin node ID

is sufficient to identify an author.

### 10.4 Signature responsibility

Every object and every bundle MUST be signed by the originating node. User-level cryptographic identity MAY be added later, but is not required in v1.

---

## 11. Object Model Overview

PLANK v1 defines the following object classes.

| Class | Numeric Value | Purpose |
|---|---:|---|
| `AreaDefinition` | `0x0001` | Defines an area and its authority metadata |
| `AreaPolicy` | `0x0002` | Defines moderation, ACL, and retention rules |
| `Message` | `0x0003` | Public area post or direct message |
| `AttachmentMeta` | `0x0004` | Attachment metadata record |
| `SubscriptionEvent` | `0x0005` | Subscribe or unsubscribe instruction |
| `ModerationEvent` | `0x0006` | Hide, approve, reject, lock, pin, or tombstone |
| `RoutingEvent` | `0x0007` | Route, redirect, or hop update |
| `ReceiptEvent` | `0x0008` | Delivery, verification, or import receipt |
| `LinkEvent` | `0x0009` | Link state or key rollover event |
| `BundleCheckpoint` | `0x000A` | Cursor state for export or import |
| `NodeInfo` | `0x000B` | Node capabilities and descriptive metadata |

### 11.1 Immutable rule

All PLANK objects are immutable once accepted.

### 11.2 Mutation by event

Any change after object acceptance MUST be represented as a new event object.

---

## 12. Canonical Encoding Profile

PLANK uses canonical CBOR for all structured payloads.

### 12.1 Required rule

All structured payloads in PLANK v1 MUST be encoded as canonical CBOR. That includes:

- object to-be-signed envelopes
- object bodies
- wire frame payloads
- bundle manifests
- bundle checkpoints
- user packet metadata

### 12.2 Canonicalization rule

The bytes hashed to produce an `object_id` MUST be the canonical CBOR encoding of the object envelope excluding the `signature` field.

The bytes hashed to produce a `bundle_id` MUST be the canonical CBOR encoding of the bundle identity material defined in Section 18.5.

### 12.3 Text encoding

All text fields MUST be UTF-8.

### 12.4 Binary fields

All binary identifiers, hashes, keys, and signatures MUST be encoded as CBOR byte strings.

### 12.5 Integer widths

On the wire and in bundles, integer values MUST fit the following ranges:

- protocol version: u16
- class numbers: u16
- enum values: u16
- timestamps: u64 seconds since Unix epoch
- sizes and lengths: u32 unless otherwise stated
- offsets in bundle files: u64
- journal sequence numbers: u64

### 12.6 C implementation note

A C implementation MUST never hash a pretty-printed or reserialized debug form. Only the canonical CBOR byte stream counts.

---

## 13. Common Object Envelope

Every object MUST be represented as a CBOR map with the following top-level keys.

| Key | Type | Required | Meaning |
|---|---|---|---|
| `version` | uint | yes | Object format version |
| `class` | uint | yes | Object class number |
| `origin_node_id` | bytes(16) | yes | Origin node ID |
| `origin_node_addr` | text | yes | Canonical node address |
| `created_at` | uint | yes | Object creation time |
| `object_id` | bytes(32) | yes | SHA-256 of to-be-signed bytes |
| `body` | map | yes | Class-specific payload |
| `sig_alg` | uint | yes | Signature algorithm enum |
| `signature` | bytes | yes | Signature over canonical envelope without this field |

### 13.1 Signature algorithm enum

| Name | Value |
|---|---:|
| `SIG_ED25519` | `0x0001` |

### 13.2 To-be-signed object bytes

The to-be-signed object bytes are the canonical CBOR encoding of the full envelope with `signature` omitted and `object_id` set to the SHA-256 hash of that same canonical encoding.

This is a two-step process:

1. build envelope without `signature` and without `object_id`
2. canonical encode
3. hash with SHA-256 -> `object_id`
4. rebuild envelope with `object_id`, still without `signature`
5. canonical encode -> sign bytes
6. generate `signature`
7. write final envelope

A receiver MUST recompute the same bytes and reject on mismatch.

---

## 14. Class-specific Object Schemas

### 14.1 Message object

The `body` map for a `Message` MUST contain the following keys.

| Key | Type | Required | Notes |
|---|---|---|---|
| `message_type` | uint | yes | `AREA_POST`, `DIRECT_POST`, `SYSTEM_POST` |
| `author_user` | text | yes | Local username at origin |
| `author_display` | text | no | Display name |
| `from_addr` | text | yes | Canonical user address |
| `to_addrs` | array(text) | yes | Empty for area posts, one or more for direct |
| `area_addr` | text | conditional | Required for `AREA_POST` |
| `subject` | text | yes | UTF-8 subject |
| `body_format` | uint | yes | Body format enum |
| `body_text` | text | yes | Primary text body |
| `thread_root_id` | bytes(32) | no | Root object ID |
| `parent_id` | bytes(32) | no | Direct parent object ID |
| `reply_to` | text | no | Free-form reply address |
| `attachment_refs` | array(bytes(32)) | yes | Attachment IDs |
| `retention_class` | uint | yes | Retention enum |
| `visibility` | uint | yes | Visibility enum |
| `flags` | uint | yes | Bitmask |
| `path` | array(bytes(16)) | yes | Node path list, possibly empty |
| `hop_count` | uint | yes | Current hop count |

#### 14.1.1 Message type enum

| Name | Value |
|---|---:|
| `AREA_POST` | `0x0001` |
| `DIRECT_POST` | `0x0002` |
| `SYSTEM_POST` | `0x0003` |

#### 14.1.2 Body format enum

| Name | Value |
|---|---:|
| `PLAIN_UTF8` | `0x0001` |
| `MUTINEER_ANSI` | `0x0002` |
| `MARKDOWN_SAFE` | `0x0003` |

#### 14.1.3 Visibility enum

| Name | Value |
|---|---:|
| `VISIBLE` | `0x0001` |
| `LOCAL_ONLY` | `0x0002` |
| `MODERATED_HOLD` | `0x0003` |
| `HIDDEN` | `0x0004` |

#### 14.1.4 Message flag bits

| Flag | Bit |
|---|---:|
| `NO_REPLY` | `1 << 0` |
| `SYSOP_ONLY` | `1 << 1` |
| `PINNED` | `1 << 2` |
| `LOCKED` | `1 << 3` |
| `PRIVATE` | `1 << 4` |
| `MODERATED` | `1 << 5` |
| `ATTACHMENTS_PRESENT` | `1 << 6` |

### 14.2 AttachmentMeta object

The `body` map for `AttachmentMeta` MUST contain:

| Key | Type | Required |
|---|---|---|
| `attachment_id` | bytes(32) | yes |
| `filename` | text | yes |
| `mime_type` | text | yes |
| `size_bytes` | uint | yes |
| `sha256` | bytes(32) | yes |
| `compression` | uint | yes |
| `retention_class` | uint | yes |

#### 14.2.1 Compression enum

| Name | Value |
|---|---:|
| `COMP_NONE` | `0x0000` |
| `COMP_ZSTD` | `0x0001` |

### 14.3 AreaDefinition object

The `body` map for `AreaDefinition` MUST contain:

- `area_addr`
- `title`
- `description`
- `origin_node_addr`
- `distribution_mode`
- `default_retention_class`
- `posting_policy`
- `attachment_policy`
- `created_by`
- `status`

#### 14.3.1 Distribution mode enum

| Name | Value |
|---|---:|
| `FANOUT` | `0x0001` |
| `SUBSCRIPTION_ONLY` | `0x0002` |
| `RESTRICTED` | `0x0003` |
| `LOCAL_MIRROR` | `0x0004` |

### 14.4 AreaPolicy object

The `body` map for `AreaPolicy` MUST contain:

- `area_addr`
- `moderation_mode`
- `allowed_roles`
- `link_acl`
- `max_message_bytes`
- `max_attachment_bytes`
- `allowed_body_formats`
- `retention_days`
- `quarantine_on_violation`
- `duplicate_window_days`
- `max_hops`

### 14.5 SubscriptionEvent object

The `body` map for `SubscriptionEvent` MUST contain:

- `area_addr`
- `link_id`
- `action`
- `effective_at`

#### 14.5.1 Subscription action enum

| Name | Value |
|---|---:|
| `SUBSCRIBE` | `0x0001` |
| `UNSUBSCRIBE` | `0x0002` |
| `PAUSE` | `0x0003` |
| `RESUME` | `0x0004` |

### 14.6 ModerationEvent object

The `body` map for `ModerationEvent` MUST contain:

- `target_object_id`
- `area_addr`
- `action`
- `reason_code`
- `reason_text`
- `issued_by_node`
- `issued_by_user`
- `effective_at`
- `visibility_scope`

#### 14.6.1 Moderation action enum

| Name | Value |
|---|---:|
| `APPROVE` | `0x0001` |
| `REJECT` | `0x0002` |
| `HIDE` | `0x0003` |
| `UNHIDE` | `0x0004` |
| `LOCK_THREAD` | `0x0005` |
| `UNLOCK_THREAD` | `0x0006` |
| `PIN` | `0x0007` |
| `UNPIN` | `0x0008` |
| `TOMBSTONE` | `0x0009` |
| `RESTORE` | `0x000A` |

### 14.7 RoutingEvent object

The `body` map for `RoutingEvent` MUST contain:

- `target_object_id`
- `route_type`
- `next_hop_link_id`
- `path_append_node_id`
- `hop_count`
- `details`

### 14.8 ReceiptEvent object

The `body` map for `ReceiptEvent` MUST contain:

- `target_id`
- `target_kind`
- `receipt_type`
- `link_id`
- `received_at`
- `details`

#### 14.8.1 Receipt type enum

| Name | Value |
|---|---:|
| `BUNDLE_ACCEPTED` | `0x0001` |
| `BUNDLE_DUPLICATE` | `0x0002` |
| `BUNDLE_PARTIAL` | `0x0003` |
| `BUNDLE_REJECTED` | `0x0004` |
| `OBJECT_STORED` | `0x0005` |
| `OBJECT_VERIFIED` | `0x0006` |
| `OBJECT_REJECTED` | `0x0007` |
| `AREA_IMPORTED` | `0x0008` |
| `LOCAL_DELIVERED` | `0x0009` |
| `ROUTED_ONWARD` | `0x000A` |
| `FINALIZED` | `0x000B` |

### 14.9 LinkEvent object

The `body` map for `LinkEvent` MUST contain:

- `link_id`
- `event_type`
- `effective_at`
- `old_key_fingerprint`
- `new_key_fingerprint`
- `notes`

### 14.10 BundleCheckpoint object

The `body` map for `BundleCheckpoint` MUST contain:

- `link_id`
- `direction`
- `journal_seq_low`
- `journal_seq_high`
- `exported_at`
- `imported_at`
- `notes`

---

## 15. Retention and Moderation Enums

### 15.1 Retention class enum

| Name | Value |
|---|---:|
| `EPHEMERAL` | `0x0001` |
| `STANDARD` | `0x0002` |
| `LONG_TERM` | `0x0003` |
| `ARCHIVE` | `0x0004` |
| `LEGAL_HOLD` | `0x0005` |

### 15.2 Moderation mode enum

| Name | Value |
|---|---:|
| `OPEN` | `0x0001` |
| `PREMOD` | `0x0002` |
| `POSTMOD` | `0x0003` |
| `SYSOP_ONLY` | `0x0004` |
| `READ_ONLY` | `0x0005` |
| `PRIVATE_INVITE` | `0x0006` |

---

## 16. Wire Link Protocol

PLANK link transport defines how two Mutineer nodes exchange framed control traffic and bundle payloads.

### 16.1 Required transport

PLANK v1 MUST support:

- TCP
- TLS 1.3
- binary framed messages over the TLS stream

### 16.2 Session phases

A link session consists of:

1. TCP connect
2. TLS handshake
3. `HELLO`
4. `HELLO_ACK`
5. `AUTH_PROOF`
6. `CAPS`
7. bundle offer and request exchange
8. bundle transfer
9. receipts
10. graceful close

### 16.3 Frame header

Every PLANK wire frame MUST begin with this fixed 24-byte header in network byte order.

| Field | Size | Description |
|---|---:|---|
| Magic `"PLK1"` | 4 | Literal bytes `0x50 0x4C 0x4B 0x31` |
| Frame type | 2 | u16 |
| Flags | 2 | u16 |
| Payload length | 4 | u32, bytes after header |
| Correlation ID | 8 | u64 |
| Reserved | 4 | MUST be zero |

Header size: 24 bytes.

A receiver MUST reject any frame with bad magic or impossible payload length.

### 16.4 Frame type enum

| Frame | Value |
|---|---:|
| `HELLO` | `0x0001` |
| `HELLO_ACK` | `0x0002` |
| `AUTH_PROOF` | `0x0003` |
| `CAPS` | `0x0004` |
| `BUNDLE_OFFER` | `0x0005` |
| `BUNDLE_REQUEST` | `0x0006` |
| `BUNDLE_DATA` | `0x0007` |
| `RECEIPT` | `0x0008` |
| `PING` | `0x0009` |
| `PONG` | `0x000A` |
| `ERROR` | `0x000B` |
| `CLOSE` | `0x000C` |

### 16.5 Header flags

#### 16.5.1 General frame flags

| Name | Bit |
|---|---:|
| `FLAG_MORE` | `1 << 0` |
| `FLAG_ACK_REQ` | `1 << 1` |
| `FLAG_COMPRESSED` | `1 << 2` |
| `FLAG_FINAL` | `1 << 3` |

### 16.6 Frame payload encoding

All frame payloads MUST be canonical CBOR maps.

### 16.7 `HELLO` payload schema

Keys:

- `protocol_version` (uint)
- `node_id` (bytes16)
- `node_addr` (text)
- `link_id` (bytes16)
- `session_nonce` (bytes32)
- `software_name` (text)
- `software_version` (text)
- `timestamp` (uint)
- `max_frame_size` (uint)
- `max_bundle_size` (uint)
- `cap_bitmap` (uint64)

### 16.8 `HELLO_ACK` payload schema

Keys:

- `protocol_version`
- `node_id`
- `node_addr`
- `link_id`
- `session_nonce`
- `accepted_max_frame_size`
- `accepted_max_bundle_size`
- `cap_bitmap`

### 16.9 Authentication transcript

`AUTH_PROOF` MUST sign the following exact byte sequence:

```text
"PLANK-AUTH-v1" ||
initiator_node_id ||
responder_node_id ||
initiator_link_id ||
initiator_nonce ||
responder_nonce ||
initiator_timestamp ||
responder_timestamp
```

Where:

- `||` means concatenation
- node IDs and link IDs are raw bytes
- timestamps are big-endian 64-bit integers

The side that initiated the TCP connection is the initiator.

### 16.10 `AUTH_PROOF` payload schema

Keys:

- `signing_node_id`
- `link_id`
- `signature_alg`
- `signature`
- `transcript_hash`

The receiver MUST:

1. locate the configured peer
2. verify node ID and link ID match policy
3. recompute transcript bytes
4. verify signature against trusted peer key
5. terminate session on failure

### 16.11 Capability bitmap

PLANK v1 required capability bits:

| Capability | Bit |
|---|---:|
| `CAP_BUNDLES_ZSTD` | `1 << 0` |
| `CAP_OBJECT_SIGS` | `1 << 1` |
| `CAP_UTF8_TEXT` | `1 << 2` |
| `CAP_ATTACHMENTS` | `1 << 3` |
| `CAP_OFFLINE_USER_PACKETS` | `1 << 4` |
| `CAP_RECEIPTS` | `1 << 5` |
| `CAP_CURSOR_SYNC` | `1 << 6` |

Optional capability bits:

| Capability | Bit |
|---|---:|
| `CAP_INLINE_ATTACHMENTS` | `1 << 16` |
| `CAP_STREAM_ATTACHMENTS` | `1 << 17` |
| `CAP_BODY_MARKDOWN_SAFE` | `1 << 18` |
| `CAP_BODY_MUTINEER_ANSI` | `1 << 19` |
| `CAP_KEY_ROLLOVER` | `1 << 20` |
| `CAP_DELTA_REQUESTS` | `1 << 21` |

### 16.12 `CAPS` payload schema

Keys:

- `cap_bitmap`
- `max_frame_size`
- `max_bundle_size`
- `bundle_compressions` (array uint)
- `supported_body_formats` (array uint)

### 16.13 `BUNDLE_OFFER` payload schema

Keys:

- `offer_id` (bytes16)
- `bundle_ids` (array bytes32)
- `bundle_types` (array uint)
- `object_counts` (array uint)
- `encoded_sizes` (array uint)
- `cursor_low` (uint)
- `cursor_high` (uint)
- `notes` (text, optional)

The arrays MUST be equal length.

### 16.14 `BUNDLE_REQUEST` payload schema

Keys:

- `offer_id`
- `request_bundle_ids` (array bytes32)
- `max_items` (uint)
- `notes` (optional)

### 16.15 `BUNDLE_DATA` payload schema

Keys:

- `bundle_id`
- `bundle_type`
- `segment_index`
- `segment_count`
- `segment_bytes`

If a bundle exceeds `max_frame_size`, it MUST be segmented across multiple `BUNDLE_DATA` frames.

### 16.16 `RECEIPT` payload schema

Keys:

- `receipt_code`
- `target_kind`
- `target_id`
- `accepted_count`
- `duplicate_count`
- `rejected_count`
- `quarantine_count`
- `details`

#### 16.16.1 Receipt code enum

| Name | Value |
|---|---:|
| `RC_OK` | `0x0001` |
| `RC_DUPLICATE` | `0x0002` |
| `RC_PARTIAL` | `0x0003` |
| `RC_REJECTED` | `0x0004` |
| `RC_QUARANTINED` | `0x0005` |

#### 16.16.2 Target kind enum

| Name | Value |
|---|---:|
| `TARGET_BUNDLE` | `0x0001` |
| `TARGET_OBJECT` | `0x0002` |
| `TARGET_LINK` | `0x0003` |

### 16.17 `ERROR` payload schema

Keys:

- `error_code`
- `fatal` (bool)
- `text`
- `target_id` (optional)
- `details` (optional map)

### 16.18 `PING` and `PONG` payload schema

Keys:

- `timestamp`
- `nonce`

### 16.19 `CLOSE` payload schema

Keys:

- `reason_code`
- `text`

---

## 17. Link State Machine

### 17.1 Local link states

| State | Meaning |
|---|---|
| `DISABLED` | Link disabled by config or admin |
| `IDLE` | No active socket |
| `CONNECTING` | TCP connect in progress |
| `TLS_OK` | TLS complete |
| `AUTH_OK` | PLANK authentication complete |
| `SYNCING` | Bundle exchange active |
| `BACKOFF` | Waiting for retry |
| `FAILED` | Last attempt failed |

### 17.2 Required transitions

- `IDLE -> CONNECTING`
- `CONNECTING -> TLS_OK` on TLS success
- `TLS_OK -> AUTH_OK` on valid `HELLO`, `HELLO_ACK`, and `AUTH_PROOF`
- `AUTH_OK -> SYNCING` on successful capability negotiation
- `SYNCING -> IDLE` on graceful completion
- any state -> `BACKOFF` on transient failure
- any state -> `FAILED` on fatal policy or auth failure

Every transition MUST be logged.

---

## 18. Bundle Format

PLANK defines a standalone bundle file format for both node-to-node transfer and offline packets.

### 18.1 File extensions

Recommended extensions:

- `.plb` for node link bundles
- `.plp` for user offline packets

### 18.2 Bundle file layout

A bundle file MUST contain:

1. fixed binary header
2. canonical CBOR manifest
3. fixed-size directory entries
4. payload records
5. terminal marker

### 18.3 Bundle header

The bundle header is 32 bytes.

| Field | Size | Description |
|---|---:|---|
| Magic `"PLKB"` | 4 | Literal bytes `0x50 0x4C 0x4B 0x42` |
| Format version | 2 | u16 |
| Bundle type | 2 | u16 |
| Flags | 4 | u32 |
| Manifest length | 4 | u32 |
| Directory count | 4 | u32 |
| Directory entry size | 2 | u16, MUST be 88 in v1 |
| Reserved | 10 | MUST be zero |

### 18.4 Bundle type enum

| Type | Value |
|---|---:|
| `LINK_SYNC` | `0x0001` |
| `USER_EXPORT` | `0x0002` |
| `USER_REPLY` | `0x0003` |
| `ADMIN_TRANSFER` | `0x0004` |

### 18.5 Bundle identity material

The `bundle_id` MUST be computed as the SHA-256 of the canonical CBOR encoding of this map:

- `format_version`
- `bundle_type`
- `source_node_id`
- `source_node_addr`
- `target_node_id_or_export_id`
- `created_at`
- `record_count`
- `record_directory_hash`

### 18.6 Bundle manifest schema

The canonical CBOR manifest MUST contain:

- `bundle_id`
- `bundle_type`
- `source_node_id`
- `source_node_addr`
- `target_node_id` or `export_id`
- `created_at`
- `record_count`
- `object_count`
- `attachment_count`
- `compression_mode`
- `record_directory_hash`
- `scope`
- `cursor_low`
- `cursor_high`
- `notes`
- `sig_alg`
- `signature`

### 18.7 Record directory entry

Each bundle record is described by one fixed-size 88-byte directory entry in network byte order.

| Field | Size |
|---|---:|
| Record type | 2 |
| Flags | 2 |
| Reserved | 4 |
| Offset | 8 |
| Encoded length | 8 |
| Decoded length | 8 |
| Digest | 32 |
| Record ID | 24 |

Directory entry size is exactly 88 bytes.

#### 18.7.1 Record ID rules

- For `OBJECT`, `Record ID` is the first 24 bytes of `object_id`
- For `ATTACHMENT`, `Record ID` is the first 24 bytes of `attachment_id`
- For `CHECKPOINT`, `INDEX`, or `NOTE`, `Record ID` is implementation-defined but stable within the bundle

The full digest in the entry is authoritative.

### 18.8 Record type enum

| Record Type | Value |
|---|---:|
| `OBJECT` | `0x0001` |
| `ATTACHMENT` | `0x0002` |
| `CHECKPOINT` | `0x0003` |
| `INDEX` | `0x0004` |
| `NOTE` | `0x0005` |

### 18.9 Record payload rules

- `OBJECT` payloads are canonical CBOR object envelopes
- `ATTACHMENT` payloads are raw bytes or zstd-compressed bytes
- `CHECKPOINT` payloads are canonical CBOR `BundleCheckpoint` objects
- `INDEX` payloads are canonical CBOR maps
- `NOTE` payloads are UTF-8 text or canonical CBOR note maps

### 18.10 Compression rules

PLANK v1 MUST support:

- `COMP_NONE`
- `COMP_ZSTD`

If `COMP_ZSTD` is used, the manifest MUST declare it and the directory entry MUST report both encoded and decoded lengths.

### 18.11 Terminal marker

A bundle file MUST end with the 8-byte terminal marker:

```text
"PLEND1\0\0"
```

### 18.12 Bundle signing rule

The exporting node MUST sign the canonical manifest without the `signature` field. The resulting bytes go in `signature`. A receiver MUST reject the bundle if:

- the manifest signature is invalid
- `bundle_id` does not match recomputation
- the record directory hash does not match
- any record digest fails
- any record offset or length escapes the file bounds

---

## 19. User Offline Packet Format

A user packet is a PLANK bundle with `bundle_type` of `USER_EXPORT` or `USER_REPLY`.

### 19.1 `USER_EXPORT` requirements

A user export packet MUST contain:

- exactly one `export_id`
- export owner local user ID
- export time
- included areas
- included direct mail
- local read state snapshot
- selected messages
- selected attachments as allowed by policy
- one checkpoint record
- optional index record for offline reader convenience

### 19.2 `USER_REPLY` requirements

A user reply packet MUST contain:

- `export_id`
- original export `bundle_id`
- one or more reply `Message` objects
- optional attachment records
- optional packet editor notes
- optional client metadata

### 19.3 Import rules for `USER_REPLY`

Mutineer MUST reject a user reply packet if:

- the target local user does not exist
- `export_id` is unknown and policy requires known exports
- the reply references an area not present in the original export and policy disallows it
- the bundle signature is invalid
- any object fails canonical verification
- any reply duplicates an existing object ID

### 19.4 User packet idempotency

Importing the same `.plp` more than once MUST NOT create duplicate local posts.

---

## 20. Routing Model

PLANK routing is area-aware and subscription-aware.

### 20.1 Traffic classes

PLANK routes three traffic classes:

1. `AREA_TRAFFIC`
2. `DIRECT_TRAFFIC`
3. `CONTROL_TRAFFIC`

### 20.2 Area routing rule

A node MUST only forward an area post to peers that are eligible to receive the area. Eligibility is determined by:

- active subscription state
- area ACL
- moderation policy
- loop prevention checks
- route health
- retry state

### 20.3 Direct traffic rule

A direct message SHOULD be routed toward the destination node using the best explicit configured route.

PLANK v1 SHOULD prefer explicit neighbor and COVE routes, not dynamic path discovery.

### 20.4 Path list and hop count

Every routed `Message` MUST maintain:

- `path`: ordered array of node IDs that handled it
- `hop_count`: incremented at every forwarding step

A node MUST drop or quarantine a message if:

- its own node ID already appears in `path`
- `hop_count` exceeds area or node policy

### 20.5 Required deduplication windows

A node MUST retain enough deduplication history to avoid duplicate import for at least:

- `bundle_id`: 30 days by default
- `object_id`: 90 days by default

These values MUST be configurable.

### 20.6 Retry behavior

Outbound work MUST use exponential backoff. Required configuration:

- initial retry interval
- max retry interval
- retry ceiling
- dead-letter timeout

### 20.7 Dead-letter rules

When delivery policy is exhausted, the node MUST move work to dead-letter state and record:

- target link
- target node
- related object IDs
- first failure time
- last failure time
- last error code
- retry count

---

## 21. COVE Responsibilities

A COVE is the standard central distribution role in a PLANK network.

### 21.1 COVE required capabilities

A node acting as a COVE MUST support:

- many concurrent configured links
- per-link subscription tracking
- per-link export cursors
- receipt retention
- dead-letter review and requeue
- quarantine review
- area fan-out scheduling
- moderation propagation
- admin reports on area and link health

### 21.2 COVE routing behavior

A COVE MUST NOT receive protocol privilege that other nodes do not have. It is simply a highly connected, trusted routing point.

A COVE SHOULD:

- act as an upstream for leaf nodes
- propagate `AreaDefinition`, `AreaPolicy`, and moderation events
- maintain link health metrics
- batch work per link for efficient polling
- prefer deterministic fan-out over dynamic route invention

### 21.3 COVE and area authority

A COVE MAY also be an area authority, but these roles are separate. If a COVE is not the area authority, it MUST NOT rewrite the authoritative area definition.

### 21.4 COVE redundancy

Multiple COVEs MAY exist within a network. If two or more COVEs peer with each other:

- loop prevention rules still apply
- each COVE MUST track subscriptions independently
- one COVE MUST NOT assume another COVE imported content unless a receipt confirms it

### 21.5 COVE operational minimums

A production COVE SHOULD expose sysop views for:

- link backlog
- per-area outbound queue depth
- last successful poll per peer
- receipt failures
- dead-letter counts
- quarantine counts
- stalled cursors
- moderation propagation status

---

## 22. Moderation and Policy Model

Moderation is built into PLANK.

### 22.1 Required policy checks on inbound object

Before accepting an incoming object into an area, a node MUST verify:

- object signature validity
- area existence
- subscription or authority eligibility
- attachment size limits
- body size limits
- allowed body format
- moderation mode
- duplicate window
- banned node or user policy
- thread lock state
- hop count
- path loop safety

### 22.2 Quarantine

A node MUST support quarantine for suspicious or policy-violating traffic. Quarantined objects MUST NOT appear to normal users until released or rejected.

### 22.3 Tombstones

If a message is withdrawn or suppressed network-wide, PLANK MUST use a `ModerationEvent` with action `TOMBSTONE`.

### 22.4 Audit log

All moderation actions MUST produce tamper-evident audit entries containing:

- issuer
- time
- target object
- reason
- propagation status

---

## 23. Persistent Storage Model

PLANK storage is split into:

1. relational metadata
2. append-only journal
3. file-backed spool
4. attachment blob store
5. exported packet store

### 23.1 Required SQL tables

At minimum:

- `plank_nodes`
- `plank_links`
- `plank_areas`
- `plank_area_policies`
- `plank_objects`
- `plank_messages`
- `plank_attachments`
- `plank_object_attachments`
- `plank_journal`
- `plank_link_cursors`
- `plank_receipts`
- `plank_subscriptions`
- `plank_moderation`
- `plank_import_history`
- `plank_deadletters`
- `plank_quarantine`
- `plank_user_packet_exports`

### 23.2 Journal semantics

Each accepted object MUST produce one journal row containing:

- local sequence number
- object ID
- class
- accepted_at
- source kind
- source link ID if applicable
- processing state

`local_sequence` MUST be monotonically increasing and never reused.

### 23.3 Required import history

Import history MUST record at minimum:

- bundle ID
- first seen time
- last seen time
- import result
- source link ID
- object counts
- duplicate counts

### 23.4 Blob store layout

Recommended path layout:

```text
mutineer/
  plank/
    objects/
      aa/
        bb/
          <object_id>.obj
    attachments/
      aa/
        bb/
          <attachment_id>.bin
    spool/
      inbound/
      outbound/
      quarantine/
      dead/
    bundles/
      link-export/
      link-import/
      user-export/
      user-import/
    keys/
    logs/
```

---

## 24. Security Model

### 24.1 Baseline requirements

A production PLANK implementation MUST provide:

- TLS 1.3 for all node links
- peer authentication using configured trust
- signed objects
- signed bundles
- replay detection
- import history
- tamper-evident audit logging for key security events

### 24.2 Optional direct-payload encryption

PLANK v1 MAY support encrypted direct-message bodies and attachments. If implemented, the recommended design is:

- per-message random content key
- content encrypted with an AEAD
- content key wrapped for each recipient
- encryption metadata stored in the `Message` body

Area traffic is normally not end-to-end encrypted in v1.

### 24.3 Replay protection

A node MUST reject or safely ignore any already-known object ID and any already-known bundle ID.

### 24.4 Clock tolerance

Time validation MUST allow a configurable drift window. Time checks MUST NOT be the only replay control.

### 24.5 Key rollover

A node SHOULD support planned key rollover using a signed `LinkEvent` with a future activation time.

---

## 25. Error Model

### 25.1 Standard link errors

| Code | Numeric Value | Meaning |
|---|---:|---|
| `ERR_PROTOCOL_VERSION` | `0x0001` | Unsupported protocol version |
| `ERR_AUTH_FAILED` | `0x0002` | Peer authentication failed |
| `ERR_UNKNOWN_LINK` | `0x0003` | Link not configured |
| `ERR_CAPS_MISMATCH` | `0x0004` | Required capability missing |
| `ERR_BUNDLE_TOO_LARGE` | `0x0005` | Bundle exceeds policy |
| `ERR_BUNDLE_CORRUPT` | `0x0006` | Bundle failed integrity check |
| `ERR_OBJECT_REJECTED` | `0x0007` | Object rejected by policy |
| `ERR_DUPLICATE` | `0x0008` | Already known |
| `ERR_AREA_UNKNOWN` | `0x0009` | Area not defined locally |
| `ERR_SUBSCRIPTION_DENIED` | `0x000A` | Peer not allowed to receive area |
| `ERR_RATE_LIMIT` | `0x000B` | Peer exceeded rate policy |
| `ERR_INTERNAL` | `0x000C` | Local processing failure |

### 25.2 Bundle import outcomes

A bundle import MUST end in one of:

- accepted
- accepted with duplicates skipped
- partially accepted
- quarantined
- rejected

A partial import MUST include per-object results in receipt details.

---

## 26. Required Operational Flows

### 26.1 Area post creation

1. local user writes post
2. Mutineer creates `Message`
3. object is canonicalized and signed
4. attachment metadata and blobs are stored
5. local delivery occurs
6. journal entry is written
7. eligible outbound links are marked dirty
8. next poll or scheduled sync exports to each eligible peer

### 26.2 Inbound area bundle

1. peer authenticates
2. peer offers bundle
3. receiver requests bundle
4. receiver stages bundle
5. bundle manifest signature verified
6. directory and record digests verified
7. objects iterated in record order
8. duplicates skipped
9. policy checks applied
10. accepted objects stored and journaled
11. local indexes updated
12. receipt returned

### 26.3 Direct message flow

1. local user sends direct message
2. message uses `DIRECT_POST`
3. route lookup selects next hop
4. object enters outbound queue
5. next link sync exports bundle
6. intermediate nodes forward based on route policy
7. destination node files into recipient mailbox

### 26.4 Offline user packet flow

1. user selects packet export
2. Mutineer gathers selected unread content
3. `.plp` file created
4. packet signed
5. user reads and replies offline
6. reply packet imported
7. reply objects validated
8. replies enter normal local delivery and outbound routing

### 26.5 Moderation flow

1. moderator selects target
2. Mutineer creates `ModerationEvent`
3. event signed and journaled
4. local state updates
5. eligible links receive the event on next sync
6. receiving peers apply policy and authority checks

---

## 27. Mutineer Integration

PLANK is a subsystem of Mutineer, not a bolt-on.

### 27.1 Required C modules

A complete integration SHOULD include at least:

- `plank_core.c`
- `plank_store.c`
- `plank_object.c`
- `plank_bundle.c`
- `plank_link.c`
- `plank_route.c`
- `plank_import.c`
- `plank_export.c`
- `plank_policy.c`
- `plank_moderation.c`
- `plank_offline.c`
- `plank_admin.c`
- `plank_cli.c`

### 27.2 BBS-visible features

Mutineer users SHOULD see PLANK through normal BBS flows:

- area list
- subscribed areas
- new message scan
- message reader
- reply editor
- direct mail inbox
- packet export
- packet import
- attachment save or fetch
- packet-based catch-up
- sysop routing and moderation menus

### 27.3 Sysop features

The Mutineer sysop MUST be able to:

- define node identity
- manage peer links
- add or remove subscriptions
- create areas
- assign moderators
- review quarantine
- requeue dead-letter work
- trigger outbound poll now
- rotate keys
- inspect sync cursors
- verify bundle signatures
- inspect COVE backlog if the node operates as a COVE

---

## 28. Internal API Contracts

### 28.1 Store API

Required operations:

- create object
- fetch object by ID
- fetch attachment by ID
- append journal entry
- fetch objects since cursor
- set link cursor
- get link cursor
- add receipt
- quarantine object
- move to dead-letter
- mark local delivery
- create user packet export
- import user reply packet

### 28.2 Bundle API

Required operations:

- create bundle manifest
- append object record
- append attachment record
- append checkpoint record
- finalize bundle
- verify bundle
- iterate bundle records
- extract checkpoint
- emit import receipt

### 28.3 Link API

Required operations:

- open session
- verify peer
- exchange capabilities
- advertise outbound work
- request inbound work
- send bundle
- receive bundle
- return receipt
- update retry state
- close session

### 28.4 Policy API

Required operations:

- validate object for area
- validate attachment policy
- validate moderation action
- validate subscription change
- evaluate quarantine rules
- calculate retention class

---

## 29. Recommended Repository Layout for Mutineer

```text
mutineer/
  src/
    plank/
      plank_core.c
      plank_store.c
      plank_object.c
      plank_bundle.c
      plank_link.c
      plank_route.c
      plank_import.c
      plank_export.c
      plank_policy.c
      plank_moderation.c
      plank_offline.c
      plank_admin.c
      plank_cli.c
    include/
      mutineer/
        plank.h
        plank_core.h
        plank_store.h
        plank_object.h
        plank_bundle.h
        plank_link.h
        plank_route.h
        plank_policy.h
        plank_offline.h
  tests/
    plank/
      test_object_hash.c
      test_signature.c
      test_bundle_roundtrip.c
      test_link_handshake.c
      test_dedupe.c
      test_policy.c
      fixtures/
  docs/
    PLANK_SPEC.md
```

---

## 30. Reference C Structures

The following structures are normative references for field sizing and layout guidance. They are not the only valid implementation, but any implementation MUST preserve the same field semantics.

### 30.1 Wire frame header

```c
#pragma pack(push, 1)
typedef struct {
    uint8_t  magic[4];        /* "PLK1" */
    uint16_t frame_type;      /* network byte order */
    uint16_t flags;           /* network byte order */
    uint32_t payload_len;     /* network byte order */
    uint64_t correlation_id;  /* network byte order */
    uint32_t reserved;        /* MUST be zero */
} plank_frame_hdr_t;
#pragma pack(pop)
```

### 30.2 Bundle header

```c
#pragma pack(push, 1)
typedef struct {
    uint8_t  magic[4];            /* "PLKB" */
    uint16_t format_version;      /* network byte order */
    uint16_t bundle_type;         /* network byte order */
    uint32_t flags;               /* network byte order */
    uint32_t manifest_len;        /* network byte order */
    uint32_t dir_count;           /* network byte order */
    uint16_t dir_entry_size;      /* network byte order, MUST be 88 */
    uint8_t  reserved[10];        /* MUST be zero */
} plank_bundle_hdr_t;
#pragma pack(pop)
```

### 30.3 Bundle directory entry

```c
#pragma pack(push, 1)
typedef struct {
    uint16_t record_type;         /* network byte order */
    uint16_t flags;               /* network byte order */
    uint32_t reserved;            /* MUST be zero */
    uint64_t offset;              /* network byte order */
    uint64_t encoded_len;         /* network byte order */
    uint64_t decoded_len;         /* network byte order */
    uint8_t  digest[32];          /* SHA-256 of payload bytes as stored */
    uint8_t  record_id[24];       /* abbreviated stable ID */
} plank_bundle_dirent_t;
#pragma pack(pop)
```

---

## 31. Configuration Model

PLANK SHOULD use a structured config file. TOML is recommended.

### 31.1 Node section

Required fields:

- `node_name`
- `network_name`
- `node_id`
- `listen_addr`
- `tls_cert_path`
- `tls_key_path`
- `signing_key_path`
- `data_root`
- `max_bundle_bytes`
- `max_frame_bytes`
- `poll_interval_seconds`

### 31.2 Link section

Per peer:

- `link_name`
- `remote_node_addr`
- `remote_host`
- `remote_port`
- `expected_node_id`
- `expected_signing_key`
- `expected_tls_fingerprint`
- `outbound_enabled`
- `inbound_enabled`
- `retry_initial_seconds`
- `retry_max_seconds`
- `retry_limit`
- `allowed_areas`
- `paused`

### 31.3 Area section

Per area:

- `area_addr`
- `title`
- `description`
- `origin`
- `posting_policy`
- `moderation_mode`
- `retention_days`
- `allow_attachments`
- `max_attachment_bytes`
- `subscribers`

### 31.4 Offline packet section

- `export_root`
- `import_root`
- `max_packet_bytes`
- `include_direct_mail`
- `include_threads`
- `include_attachments`
- `signature_required_on_import`

---

## 32. Acceptance Criteria

A PLANK v1 implementation for Mutineer is complete only if all of the following are true.

### 32.1 Core protocol

- two configured nodes can authenticate and exchange `HELLO`
- bundle offers and requests work in both directions
- receipts are returned and stored
- duplicate bundles do not create duplicate local messages
- duplicate objects are skipped cleanly
- invalid signatures are rejected
- unknown peers are rejected by default

### 32.2 Area networking

- a new area can be defined and subscribed to by another node
- area posts propagate only to eligible subscribers
- unsubscribed nodes do not receive the area's posts
- moderation events propagate correctly
- thread relationships survive transfer

### 32.3 Direct messaging

- a direct message from node A to node B is routed and delivered once
- reply metadata survives delivery

### 32.4 Offline packets

- a user can export unread content into a `.plp`
- a reply `.plp` can be imported back into Mutineer
- repeated import of the same reply packet does not duplicate posts

### 32.5 Storage and recovery

- restart does not lose accepted objects or link cursors
- dead-letter and quarantine survive restart
- a partially imported bundle can be safely retried or resumed by policy
- journal sequence integrity survives restart

### 32.6 COVE operation

If a node operates as a COVE:

- it can track subscriptions for multiple downstream peers
- it can fan out area posts to all eligible peers
- it can show stalled or failed links
- it can requeue dead-letter work
- it can preserve cursor correctness across restart

---

## 33. Required Test Classes

The implementation MUST include automated tests for:

1. canonical object hashing
2. object signature verification
3. bundle build and parse round trip
4. bundle duplicate detection
5. attachment hash verification
6. `HELLO` and `AUTH_PROOF` validation
7. retry and dead-letter transitions
8. area subscription filtering
9. moderation event application
10. offline packet export/import round trip
11. corrupted bundle rejection
12. partial bundle receipt generation
13. cursor persistence across restart
14. tombstone visibility behavior
15. max hop enforcement
16. COVE fan-out correctness
17. bundle segmentation and reassembly

---

## 34. Example Debug Objects

The following examples are conceptual debug views. They are not canonical wire forms.

### 34.1 Example `Message`

```json
{
  "version": 1,
  "class": 3,
  "origin_node_addr": "portroyal@blackflag",
  "created_at": 1772235600,
  "body": {
    "message_type": 1,
    "author_user": "rick",
    "author_display": "Rick",
    "from_addr": "rick@portroyal@blackflag",
    "to_addrs": [],
    "area_addr": "general@portroyal@blackflag",
    "subject": "PLANK has legs",
    "body_format": 1,
    "body_text": "Testing area distribution from Port Royal.",
    "attachment_refs": [],
    "retention_class": 2,
    "visibility": 1,
    "flags": 0,
    "path": [],
    "hop_count": 0
  }
}
```

### 34.2 Example `ModerationEvent`

```json
{
  "version": 1,
  "class": 6,
  "origin_node_addr": "portroyal@blackflag",
  "created_at": 1772235900,
  "body": {
    "target_object_id": "7c8d...",
    "area_addr": "general@portroyal@blackflag",
    "action": 5,
    "reason_code": "FLAMEWAR",
    "reason_text": "Thread locked by moderator",
    "issued_by_user": "sysop",
    "effective_at": 1772235900,
    "visibility_scope": 1
  }
}
```

---

## 35. Implementation Guidance for Portable C

The implementation SHOULD follow these rules:

1. use fixed-width integer types everywhere
2. isolate byte-order and framing helpers
3. keep canonical encoding deterministic
4. never hash or sign non-canonical bytes
5. treat all inbound data as hostile
6. cap all allocations by configured limits
7. stream large bundles instead of loading everything into memory
8. verify attachment hashes during import
9. use crash-safe file writes for bundle staging
10. write journal state before marking delivery complete

Recommended libraries:

- OpenSSL or LibreSSL for TLS
- libsodium or a minimal Ed25519 implementation for signatures
- zstd for compression
- SQLite for metadata
- a small CBOR library with deterministic encoding support

---

## 36. Final Summary

PLANK is the Mutineer-native answer to the old problem of BBS mail and area networking.

It keeps what mattered:

- packetized exchange
- offline reading and replying
- store-and-forward delivery
- area-centric discussion
- sysop-visible operational control

It removes legacy baggage:

- no Fido packet assumptions
- no QWK packet assumptions
- no text-only transport assumptions
- no weak duplicate handling
- no vague moderation behavior
- no undefined hub semantics

COVE gives PLANK a clear, named central distribution role without changing the protocol's trust model.

When implemented according to this specification, PLANK gives Mutineer a complete, standalone, practical network mail subsystem that is realistic to build in C and suitable for real multi-node BBS operation.

