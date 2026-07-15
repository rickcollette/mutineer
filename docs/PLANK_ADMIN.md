# PLANK/COVE Administration Guide

PLANK (Packet Link for Area Networked Knowledge) and COVE (Central Offline Vertex Exchange) provide store-and-forward message networking for Mutineer BBS.

## Architecture Overview

PLANK/COVE are implemented as **external utilities** that run outside the main Mutineer BBS process:

```
┌─────────────────────────────────────────────────────────────────┐
│                        Mutineer BBS                              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐        │
│  │ Sessions │  │  Menus   │  │ Messages │  │  Files   │        │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘        │
│                           │                                      │
│                    ┌──────┴──────┐                              │
│                    │  SQLite DB  │                              │
│                    └──────┬──────┘                              │
└───────────────────────────┼─────────────────────────────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        │                   │                   │
   ┌────┴────┐        ┌─────┴─────┐       ┌────┴────┐
   │ plankd  │        │   coved   │       │plankctl │
   │ (daemon)│        │ (daemon)  │       │  (CLI)  │
   └─────────┘        └───────────┘       └─────────┘
        │                   │
   ┌────┴────┐        ┌─────┴─────┐
   │plankpack│        │plank-     │
   │ (tool)  │        │offline    │
   └─────────┘        └───────────┘
```

The BBS core does not contain any PLANK protocol logic. All networking, bundle processing, and routing happens in the external daemons.

## Components

### plankd - Link Daemon

The primary daemon that manages node-to-node connections:

- Maintains TLS connections to configured peer nodes
- Authenticates peers using Ed25519 signatures
- Exchanges bundles (push/pull)
- Handles retry, acknowledgment, and deduplication
- Manages cursors for incremental sync

**Usage:**
```bash
plankd -c /etc/mutineer/mutineer.conf -p 5150 -f
```

**Options:**
- `-c, --config FILE` - Configuration file
- `-d, --database FILE` - Database file
- `-p, --port PORT` - Listen port (default: 5150)
- `-f, --foreground` - Run in foreground
- `-v, --verbose` - Verbose logging

### coved - COVE Service

Hub-style redistribution daemon for COVE nodes:

- Processes incoming messages from downstream nodes
- Fans out messages to all subscribed downstream nodes
- Forwards messages to upstream COVEs
- Enforces area policies and moderation
- Can run standalone as a COVE mail hub with a loopback-only management API

**Usage:**
```bash
coved -c /etc/mutineer/mutineer.conf -f
```

Standalone hub mode:

```bash
coved -mode=hub /etc/mutineer/cove-hub.conf
```

Hub mode makes COVE the owning process for mail-hub operation. It opens the
configured Mutineer/PLANK message base, keeps COVE fanout and upstream queues
moving, listens for PLANK node traffic, and exposes a management HTTP API bound
to `127.0.0.1` only.

Example `cove-hub.conf`:

```ini
message_base_path=/var/lib/mutineer/mutineer.db
base_id=blackflag-main
listen_bind=0.0.0.0
listen_port=5150
auth_db_path=/var/lib/mutineer/cove-auth.db
management_db_path=/var/lib/mutineer/cove-management.db
management_port=5151
foreground=1
```

Hub config keys:

- `message_base_path` - SQLite message/PLANK database used as the message base.
- `base_id` - Human-readable identifier for this COVE message base.
- `listen_bind` / `listen_port` - Public PLANK hub listener.
- `auth_db_path` - Reserved path for COVE auth material.
- `management_db_path` - COVE-owned management database for audit events,
  connection records, managed nodes, and operational history.
- `management_port` - Local management API port. The bind address is forced to
  `127.0.0.1` in hub mode.

Management API:

```bash
curl http://127.0.0.1:5151/health
curl http://127.0.0.1:5151/config
curl http://127.0.0.1:5151/nodes
curl http://127.0.0.1:5151/areas
curl http://127.0.0.1:5151/events

curl -X POST http://127.0.0.1:5151/nodes \
  -d 'node_addr=portroyal@blackflag&node_name=portroyal&network_name=blackflag&remote_host=portroyal.example.net&remote_port=5150'
```

`POST /nodes` creates or updates a COVE-managed node record and ensures the
corresponding PLANK peer/link rows exist in the message base.

### plankctl - Admin Control Tool

Administrative CLI for inspecting and managing PLANK state:

```bash
# Show node status
plankctl status

# Initialize node identity
plankctl init --name mynode --network mynetwork

# List configured links
plankctl links

# Add a new link
plankctl link-add --name upstream --host hub.example.com --port 5150

# Show outbound queue
plankctl queue

# Show dead letters
plankctl deadletters

# Show quarantined items
plankctl quarantine

# View audit log
plankctl audit --limit 100

# Trigger journal rescan
plankctl rescan

# Requeue dead letters
plankctl requeue --id 123
plankctl requeue --all

# Run integrity verification
plankctl verify

# Export node public key
plankctl export-key
```

### plankpack - Bundle Tool

Create, inspect, and manipulate bundle files:

```bash
# Create bundle from directory
plankpack create --input ./objects --output bundle.plb --type node

# Inspect bundle contents
plankpack inspect bundle.plb
plankpack inspect bundle.plb --verbose

# Extract bundle to directory
plankpack extract bundle.plb --output ./extracted

# Verify bundle integrity
plankpack verify bundle.plb
plankpack verify bundle.plb --key <hex-pubkey>

# Import bundle into database
plankpack import bundle.plb --database /var/mutineer/mutineer.db
```

### plank-offline - Offline Packet Tool

Export and import user offline packets (.plp files):

```bash
# Export packet for user
plank-offline export --username johndoe --output johndoe.plp
plank-offline export --user-id 42 --output user42.plp --include-read --max 1000

# Import reply packet
plank-offline import --input johndoe-reply.plp --username johndoe

# List pending exports
plank-offline list
plank-offline list --username johndoe

# Show statistics
plank-offline status
```

## Database Schema

PLANK adds tables to the existing Mutineer database with `plank_` and `cove_` prefixes:

### Core Tables

- `plank_node_identity` - Local node identity and keys
- `plank_peers` - Known peer nodes
- `plank_links` - Configured link connections
- `plank_link_cursors` - Sync cursors per link
- `plank_areas` - PLANK area definitions
- `plank_subscriptions` - Area subscriptions per link

### Object Storage

- `plank_objects` - Immutable PLANK objects (CBOR-encoded)
- `plank_messages` - Denormalized message data (linked to BBS messages)
- `plank_attachments` - Attachment metadata
- `plank_journal` - Object processing journal

### Operations

- `plank_import_history` - Bundle import records
- `plank_dedupe` - Deduplication tracking
- `plank_deadletters` - Failed delivery queue
- `plank_quarantine` - Policy-violating content
- `plank_outbound_queue` - Pending outbound messages
- `plank_audit` - Audit log

### COVE-Specific

- `cove_downstream` - Downstream node subscriptions
- `cove_upstream` - Upstream COVE connections
- `cove_fanout_queue` - Pending fanout operations

## Configuration

PLANK uses the standard Mutineer configuration file with additional sections.

### Node Identity

Initialize node identity before first use:

```bash
plankctl init --name mynode --network mynetwork
```

This generates:
- 16-byte random node ID
- Ed25519 signing key pair
- X25519 link encryption key pair

### Link Configuration

Add links via `plankctl` or direct database insertion:

```bash
# Add outbound link to hub
plankctl link-add \
  --name upstream-hub \
  --host hub.example.com \
  --port 5150

# Add inbound-only link (accept connections)
# Configure in database with direction=2
```

### Area Configuration

Areas are defined by PLANK objects from authoritative nodes. Local areas can be created:

```sql
INSERT INTO plank_areas (area_addr, area_slug, origin_node_addr, title, 
                         distribution_mode, posting_policy) 
VALUES ('general@mynode@mynetwork', 'general', 'mynode@mynetwork', 
        'General Discussion', 1, 1);
```

### COVE Configuration

To operate as a COVE hub:

1. Set `is_cove = 1` in `plank_node_identity`
2. Configure upstream COVE connections in `cove_upstream`
3. Run `coved` daemon

## Operations

### Starting Services

```bash
# Start link daemon
systemctl start plankd

# Start COVE service (if hub)
systemctl start coved
```

### Monitoring

```bash
# Check status
plankctl status

# View recent activity
plankctl audit --limit 50

# Check link health
plankctl links

# Check queue sizes
plankctl queue
plankctl deadletters
```

### Troubleshooting

**Link not connecting:**
1. Check `plankctl links` for error messages
2. Verify peer host/port is reachable
3. Check TLS certificate validity
4. Verify peer's public key is trusted

**Messages not propagating:**
1. Check subscriptions: `SELECT * FROM plank_subscriptions`
2. Check outbound queue: `plankctl queue`
3. Check for dead letters: `plankctl deadletters`
4. Check journal processing state

**Quarantined content:**
1. Review: `plankctl quarantine`
2. Release valid content: `plankctl quarantine-release --id <id>`
3. Reject invalid: `plankctl quarantine-reject --id <id>`

### Maintenance

```bash
# Prune old dedupe records (automatic, but can force)
# Records older than bundle_dedupe_days are removed

# Verify database integrity
plankctl verify

# Rescan journal for stuck entries
plankctl rescan

# Requeue failed deliveries
plankctl requeue --all
```

## Security

### Authentication

All node-to-node communication uses:
- TLS 1.3 for transport encryption
- Ed25519 signatures for peer authentication
- Mutual authentication via signed transcripts

### Object Integrity

Every PLANK object includes:
- SHA-256 content hash as object ID
- Ed25519 signature from origin node
- Hop path for loop detection

### Policy Enforcement

The policy layer validates:
- Signature authenticity
- Object ID integrity
- Timestamp freshness (clock drift tolerance)
- Size limits
- Hop count limits
- Banned node/user lists

Invalid content is either rejected or quarantined for review.

## Bundle Formats

### .plb - Node Bundle

Used for node-to-node exchange:
- Binary header with magic "PLB1"
- Directory of entries
- CBOR-encoded objects
- Optional compressed attachments
- CBOR manifest
- Optional bundle signature

### .plp - User Packet

Used for offline user exchange:
- Same format as .plb
- Bundle type = USER or REPLY
- Contains messages for specific user
- May include area posts and direct mail

## Protocol Details

### Wire Protocol

Frames over TLS:
```
┌────────┬──────────┬───────┬────────────┬─────────────┬──────────┐
│ Magic  │ Type     │ Flags │ Payload    │ Correlation │ Reserved │
│ "PLK1" │ (16-bit) │       │ Length     │ ID          │          │
│ 4 bytes│ 2 bytes  │2 bytes│ 4 bytes    │ 8 bytes     │ 4 bytes  │
└────────┴──────────┴───────┴────────────┴─────────────┴──────────┘
```

Frame types:
- HELLO (0x0001) - Initial handshake
- AUTH_PROOF (0x0002) - Authentication signature
- CAPS (0x0003) - Capability exchange
- BUNDLE_OFFER (0x0010) - Offer bundle for transfer
- BUNDLE_REQUEST (0x0011) - Request offered bundle
- BUNDLE_DATA (0x0012) - Bundle payload
- RECEIPT (0x0020) - Acknowledge bundle
- PING (0x0030) - Keepalive
- PONG (0x0031) - Keepalive response
- ERROR (0x00F0) - Error notification
- CLOSE (0x00FF) - Session close

### Handshake Flow

1. Initiator sends HELLO with node ID, public key, nonce, timestamp
2. Responder sends HELLO with same fields
3. Both compute auth transcript and send AUTH_PROOF with signature
4. Both verify peer's signature
5. Exchange CAPS for feature negotiation
6. Session authenticated, begin bundle exchange

## Glossary

- **PLANK** - Packet Link for Area Networked Knowledge
- **COVE** - Central Offline Vertex Exchange (hub node)
- **Bundle** - Collection of objects for transfer (.plb/.plp)
- **Object** - Immutable CBOR-encoded data unit
- **Area** - Message conference/forum
- **Link** - Configured connection to peer node
- **Cursor** - Sync position for incremental exchange
- **Dead Letter** - Failed delivery awaiting retry
- **Quarantine** - Policy-violating content awaiting review
