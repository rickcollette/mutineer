-- Mutineer parity schema (fresh SQLite layout; no legacy .DAT compatibility)

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS meta (
  k TEXT PRIMARY KEY,
  v TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS security_levels (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  level INTEGER NOT NULL DEFAULT 10,
  time_limit_min INTEGER NOT NULL DEFAULT 60,
  call_allow INTEGER NOT NULL DEFAULT 99,        -- calls per day allowed
  dl_one_day INTEGER NOT NULL DEFAULT 99,        -- downloads per day
  dl_k_one_day INTEGER NOT NULL DEFAULT 10000,   -- download KB per day
  download_ratio_num INTEGER NOT NULL DEFAULT 1,
  download_ratio_den INTEGER NOT NULL DEFAULT 1,
  post_ratio_num INTEGER NOT NULL DEFAULT 1,
  post_ratio_den INTEGER NOT NULL DEFAULT 1,
  ul_dl_ratio_num INTEGER NOT NULL DEFAULT 0,    -- UL/DL ratio numerator (0=disabled)
  ul_dl_ratio_den INTEGER NOT NULL DEFAULT 1,    -- UL/DL ratio denominator
  post_call_ratio INTEGER NOT NULL DEFAULT 0,    -- posts per call ratio (0=disabled)
  email_allow INTEGER NOT NULL DEFAULT 1,        -- can send email
  vote_allow INTEGER NOT NULL DEFAULT 1,         -- can vote
  anon_allow INTEGER NOT NULL DEFAULT 0,         -- can post anonymously
  flags INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS validation_levels (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  key TEXT NOT NULL UNIQUE,                      -- single char A-Z
  description TEXT NOT NULL DEFAULT '',
  user_msg TEXT NOT NULL DEFAULT '',             -- message shown to user
  new_sl INTEGER NOT NULL DEFAULT 10,            -- new security level
  new_dsl INTEGER NOT NULL DEFAULT 10,           -- new download security level
  new_menu INTEGER NOT NULL DEFAULT 0,           -- new starting menu
  expiration INTEGER NOT NULL DEFAULT 0,         -- days until expiration (0=never)
  expire_to INTEGER NOT NULL DEFAULT 0,          -- validation level to expire to
  new_fp INTEGER NOT NULL DEFAULT 0,             -- new file points
  new_credit INTEGER NOT NULL DEFAULT 0,         -- new credits
  soft_ar INTEGER NOT NULL DEFAULT 0,            -- soft AR flags (add, don't replace)
  soft_ac INTEGER NOT NULL DEFAULT 0,            -- soft AC flags (add, don't replace)
  new_ar INTEGER NOT NULL DEFAULT 0,             -- new AR flags (hard set)
  new_ac INTEGER NOT NULL DEFAULT 0              -- new AC flags (hard set)
);

CREATE TABLE IF NOT EXISTS users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  handle TEXT NOT NULL UNIQUE,
  real_name TEXT,
  pw_hash TEXT NOT NULL,
  email TEXT,
  phone TEXT,
  street TEXT,
  city_state TEXT,
  zip_code TEXT,
  caller_id TEXT,
  forgot_pw_question TEXT,                       -- security question
  forgot_pw_answer TEXT,                         -- security answer (hashed)
  sex TEXT DEFAULT 'U',                          -- M/F/U
  birth_date TEXT,                               -- YYYY-MM-DD
  security_level_id INTEGER NOT NULL DEFAULT 1,
  dsl INTEGER NOT NULL DEFAULT 10,               -- download security level
  flags INTEGER NOT NULL DEFAULT 0,              -- AR flags (A-Z bitset)
  ac_flags INTEGER NOT NULL DEFAULT 0,           -- activity/restriction flags
  status_flags INTEGER NOT NULL DEFAULT 0,       -- locked/deleted/etc
  credits INTEGER NOT NULL DEFAULT 0,
  file_points INTEGER NOT NULL DEFAULT 0,
  time_limit_min INTEGER,
  on_today INTEGER NOT NULL DEFAULT 0,           -- calls today
  illegal INTEGER NOT NULL DEFAULT 0,            -- illegal logon attempts
  def_arc_type INTEGER NOT NULL DEFAULT 0,       -- default archive type
  color_scheme INTEGER NOT NULL DEFAULT 0,       -- user color scheme
  user_start_menu INTEGER NOT NULL DEFAULT 0,    -- starting menu
  first_on TEXT,                                 -- first logon date
  t_time_on INTEGER NOT NULL DEFAULT 0,          -- total time on (minutes)
  last_qwk TEXT,                                 -- last QWK packet date
  uploads INTEGER NOT NULL DEFAULT 0,            -- upload count
  downloads INTEGER NOT NULL DEFAULT 0,          -- download count
  uk INTEGER NOT NULL DEFAULT 0,                 -- upload KB
  dk INTEGER NOT NULL DEFAULT 0,                 -- download KB
  logged_on INTEGER NOT NULL DEFAULT 0,          -- total logons
  msg_post INTEGER NOT NULL DEFAULT 0,           -- messages posted
  email_sent INTEGER NOT NULL DEFAULT 0,         -- emails sent
  feedback INTEGER NOT NULL DEFAULT 0,           -- feedback sent
  timebank INTEGER NOT NULL DEFAULT 0,           -- time bank balance
  timebank_add INTEGER NOT NULL DEFAULT 0,       -- daily timebank addition
  dl_k_today INTEGER NOT NULL DEFAULT 0,         -- download KB today
  dl_today INTEGER NOT NULL DEFAULT 0,           -- downloads today
  usr_def_str1 TEXT,                             -- custom field 1
  usr_def_str2 TEXT,                             -- custom field 2
  usr_def_str3 TEXT,                             -- custom field 3
  social_link TEXT,                              -- social media link (optional)
  sysop_msg TEXT,                                -- message to sysop from registration
  note TEXT,                                     -- sysop note
  locked_file TEXT,                              -- lockout message file
  last_conf INTEGER NOT NULL DEFAULT 0,
  last_login_at TEXT,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  expires_at TEXT,
  pw_changed_at TEXT NOT NULL DEFAULT (datetime('now')),
  subscription INTEGER NOT NULL DEFAULT 0,
  alert_sysop INTEGER NOT NULL DEFAULT 0,
  smw INTEGER NOT NULL DEFAULT 0,                -- short msg waiting
  signature TEXT,                                -- user signature for messages
  tagline TEXT,                                  -- user tagline for messages
  use_signature INTEGER NOT NULL DEFAULT 0,     -- 1=append signature to posts
  use_tagline INTEGER NOT NULL DEFAULT 0,       -- 1=append tagline to posts
  FOREIGN KEY(security_level_id) REFERENCES security_levels(id)
);

CREATE TABLE IF NOT EXISTS nodes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  node_num INTEGER NOT NULL UNIQUE,
  user_id INTEGER,
  status TEXT NOT NULL DEFAULT 'idle',           -- idle, online, locked, down
  activity TEXT,
  ip TEXT,
  updated_at TEXT NOT NULL DEFAULT (datetime('now')),
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS node_locks (
  node_num INTEGER PRIMARY KEY,
  locked INTEGER NOT NULL DEFAULT 1,
  actor TEXT,
  updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS bucc_kv (
  scope TEXT NOT NULL,
  key TEXT NOT NULL,
  value TEXT NOT NULL,
  updated_at TEXT NOT NULL DEFAULT (datetime('now')),
  PRIMARY KEY(scope, key)
);

CREATE TABLE IF NOT EXISTS bucc_data_records (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  scope TEXT NOT NULL,
  dataset TEXT NOT NULL,
  value TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_bucc_data_scope_dataset ON bucc_data_records(scope, dataset, id);

CREATE TABLE IF NOT EXISTS door_leaderboard (
  door_id INTEGER,
  game_key TEXT NOT NULL,
  game_name TEXT NOT NULL,
  score_label TEXT NOT NULL DEFAULT 'Score',
  score_order TEXT NOT NULL DEFAULT 'desc',
  handle TEXT NOT NULL,
  score INTEGER NOT NULL,
  detail TEXT NOT NULL DEFAULT '',
  achieved_at TEXT NOT NULL DEFAULT (datetime('now')),
  PRIMARY KEY(game_key, handle)
);

CREATE INDEX IF NOT EXISTS idx_door_leaderboard_rank
  ON door_leaderboard(game_key, score DESC, achieved_at ASC);

CREATE TABLE IF NOT EXISTS conferences (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  key TEXT NOT NULL UNIQUE,
  name TEXT NOT NULL,
  description TEXT,
  acs TEXT,
  flags INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS conference_membership (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  conf_id INTEGER NOT NULL,
  joined_at TEXT NOT NULL DEFAULT (datetime('now')),
  UNIQUE(user_id, conf_id),
  FOREIGN KEY(user_id) REFERENCES users(id),
  FOREIGN KEY(conf_id) REFERENCES conferences(id)
);

CREATE INDEX IF NOT EXISTS idx_conf_membership_user ON conference_membership(user_id);

CREATE TABLE IF NOT EXISTS message_areas (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  conf_id INTEGER,
  name TEXT NOT NULL UNIQUE,
  filename TEXT,                                 -- base filename for area
  acs_read TEXT,                                 -- ACS to read messages
  acs_post TEXT,                                 -- ACS to post messages
  acs_sysop TEXT,                                -- ACS for sysop functions
  acs TEXT,                                      -- general ACS (legacy)
  anon_policy INTEGER NOT NULL DEFAULT 0,        -- 0=No, 1=Yes, 2=Forced, 3=DearAbby, 4=AnyName
  flags INTEGER NOT NULL DEFAULT 0,              -- area flags bitset
  password TEXT,                                 -- area password
  origin TEXT,                                   -- origin line for echomail
  max_msgs INTEGER NOT NULL DEFAULT 500,         -- max messages in area
  FOREIGN KEY(conf_id) REFERENCES conferences(id)
);

CREATE TABLE IF NOT EXISTS messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  area_id INTEGER NOT NULL,
  user_id INTEGER,
  to_user INTEGER,
  reply_to INTEGER,
  thread_root INTEGER,
  posted_at TEXT NOT NULL DEFAULT (datetime('now')),
  subject TEXT NOT NULL,
  body TEXT NOT NULL,
  from_name TEXT,                                -- for anonymous/alias posting
  to_name TEXT,                                  -- recipient name
  attr INTEGER NOT NULL DEFAULT 0,              -- message attributes bitset
  net_attr INTEGER NOT NULL DEFAULT 0,          -- FidoNet attributes
  file_attached TEXT,                           -- attached filename
  origin TEXT,                                  -- origin line
  FOREIGN KEY(area_id) REFERENCES message_areas(id),
  FOREIGN KEY(user_id) REFERENCES users(id),
  FOREIGN KEY(to_user) REFERENCES users(id),
  FOREIGN KEY(reply_to) REFERENCES messages(id),
  FOREIGN KEY(thread_root) REFERENCES messages(id)
);

CREATE TABLE IF NOT EXISTS mail_packets (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  kind TEXT NOT NULL,                            -- qwk/fido-netmail/etc.
  path TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS file_areas (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  path TEXT NOT NULL,
  acs_list TEXT,                                   -- ACS to list area
  acs_download TEXT,                               -- ACS to download files
  acs_upload TEXT,                                 -- ACS to upload files
  acs_sysop TEXT,                                  -- ACS for sysop operations
  password TEXT,                                   -- optional password
  max_files INTEGER NOT NULL DEFAULT 0,            -- 0 = unlimited
  archive_type TEXT,                               -- default archive type for area
  sort_type INTEGER NOT NULL DEFAULT 0,            -- 0=name, 1=date, 2=size
  show_uploader INTEGER NOT NULL DEFAULT 1,        -- show who uploaded
  check_dupes INTEGER NOT NULL DEFAULT 1,          -- check for duplicate files
  free_files INTEGER NOT NULL DEFAULT 0,           -- files don't cost credits
  flags INTEGER NOT NULL DEFAULT 0                 -- FA_FLAG_*
);

CREATE TABLE IF NOT EXISTS files (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  area_id INTEGER NOT NULL,
  filename TEXT NOT NULL,
  size_bytes INTEGER NOT NULL DEFAULT 0,
  uploaded_at TEXT NOT NULL DEFAULT (datetime('now')),
  uploaded_by INTEGER,
  description TEXT,
  extended_desc TEXT,                              -- verbose/extended description
  file_id_diz TEXT,                                -- FILE_ID.DIZ content
  sha256 TEXT,
  file_points INTEGER NOT NULL DEFAULT 0,          -- file points cost
  download_count INTEGER NOT NULL DEFAULT 0,
  owner_credit INTEGER NOT NULL DEFAULT 0,         -- credits to owner per download
  flags INTEGER NOT NULL DEFAULT 0,                -- FILE_FLAG_*
  FOREIGN KEY(area_id) REFERENCES file_areas(id),
  FOREIGN KEY(uploaded_by) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS upload_queue (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  area_id INTEGER NOT NULL,
  filename TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'pending',        -- pending/approved/rejected
  requested_at TEXT NOT NULL DEFAULT (datetime('now')),
  FOREIGN KEY(user_id) REFERENCES users(id),
  FOREIGN KEY(area_id) REFERENCES file_areas(id)
);

CREATE TABLE IF NOT EXISTS download_queue (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  file_id INTEGER NOT NULL,
  status TEXT NOT NULL DEFAULT 'pending',
  requested_at TEXT NOT NULL DEFAULT (datetime('now')),
  FOREIGN KEY(user_id) REFERENCES users(id),
  FOREIGN KEY(file_id) REFERENCES files(id)
);

CREATE TABLE IF NOT EXISTS votes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  title TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  closes_at TEXT,
  flags INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS vote_choices (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  vote_id INTEGER NOT NULL,
  label TEXT NOT NULL,
  FOREIGN KEY(vote_id) REFERENCES votes(id)
);

CREATE TABLE IF NOT EXISTS vote_ballots (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  vote_id INTEGER NOT NULL,
  user_id INTEGER NOT NULL,
  choice_id INTEGER NOT NULL,
  cast_at TEXT NOT NULL DEFAULT (datetime('now')),
  UNIQUE(vote_id, user_id),
  FOREIGN KEY(vote_id) REFERENCES votes(id),
  FOREIGN KEY(choice_id) REFERENCES vote_choices(id),
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS automsg (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  msg TEXT,
  set_by INTEGER,
  set_at TEXT NOT NULL DEFAULT (datetime('now')),
  FOREIGN KEY(set_by) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS bulletins (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  title TEXT NOT NULL,
  body TEXT NOT NULL,
  posted_at TEXT NOT NULL DEFAULT (datetime('now')),
  posted_by INTEGER,
  acs TEXT,
  FOREIGN KEY(posted_by) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  schedule TEXT NOT NULL,                        -- cron-ish string
  command TEXT NOT NULL,
  last_run TEXT,
  next_run TEXT,
  event_type TEXT NOT NULL DEFAULT 'scheduled',  -- scheduled, logon, permission
  acs TEXT,                                       -- ACS for permission events
  warning_min INTEGER NOT NULL DEFAULT 0,        -- minutes warning before event
  enabled INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE IF NOT EXISTS doors (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  dropfile TEXT NOT NULL,                        -- DOOR.SYS, DORINFO1.DEF, etc.
  command TEXT NOT NULL,
  workdir TEXT,
  acs TEXT,
  flags INTEGER NOT NULL DEFAULT 0,
  runner TEXT NOT NULL DEFAULT 'native',
  manifest TEXT NOT NULL DEFAULT '',
  enabled INTEGER NOT NULL DEFAULT 1,
  timeout_sec INTEGER NOT NULL DEFAULT 0,
  lb_enable INTEGER NOT NULL DEFAULT 0,
  lb_key TEXT NOT NULL DEFAULT '',
  lb_label TEXT NOT NULL DEFAULT 'Score',
  lb_order TEXT NOT NULL DEFAULT 'desc'
);

CREATE TABLE IF NOT EXISTS protocols (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  direction TEXT NOT NULL,                       -- up/down/both
  command TEXT NOT NULL,
  active INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE IF NOT EXISTS logs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts TEXT NOT NULL DEFAULT (datetime('now')),
  level TEXT NOT NULL,
  node INTEGER,
  user_id INTEGER,
  msg TEXT NOT NULL,
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS stats (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  calls INTEGER NOT NULL DEFAULT 0,
  uploads INTEGER NOT NULL DEFAULT 0,
  downloads INTEGER NOT NULL DEFAULT 0,
  posts INTEGER NOT NULL DEFAULT 0,
  emails INTEGER NOT NULL DEFAULT 0,
  last_reset TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS call_history (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  handle TEXT NOT NULL,
  node_num INTEGER NOT NULL DEFAULT 0,
  login_at TEXT NOT NULL DEFAULT (datetime('now')),
  logout_at TEXT,
  duration_min INTEGER,
  ip_address TEXT,
  FOREIGN KEY(user_id) REFERENCES users(id)
);
CREATE INDEX IF NOT EXISTS idx_call_history_login ON call_history(login_at DESC);

CREATE TABLE IF NOT EXISTS daily_stats (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  date TEXT NOT NULL DEFAULT (date('now')),
  calls INTEGER NOT NULL DEFAULT 0,
  posts INTEGER NOT NULL DEFAULT 0,
  emails INTEGER NOT NULL DEFAULT 0,
  newusers INTEGER NOT NULL DEFAULT 0,
  feedback INTEGER NOT NULL DEFAULT 0,
  uploads INTEGER NOT NULL DEFAULT 0,
  downloads INTEGER NOT NULL DEFAULT 0,
  ul_kb INTEGER NOT NULL DEFAULT 0,
  dl_kb INTEGER NOT NULL DEFAULT 0,
  minutes INTEGER NOT NULL DEFAULT 0,
  errors INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS history (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  date TEXT NOT NULL UNIQUE,
  calls INTEGER NOT NULL DEFAULT 0,
  posts INTEGER NOT NULL DEFAULT 0,
  emails INTEGER NOT NULL DEFAULT 0,
  newusers INTEGER NOT NULL DEFAULT 0,
  feedback INTEGER NOT NULL DEFAULT 0,
  uploads INTEGER NOT NULL DEFAULT 0,
  downloads INTEGER NOT NULL DEFAULT 0,
  ul_kb INTEGER NOT NULL DEFAULT 0,
  dl_kb INTEGER NOT NULL DEFAULT 0,
  minutes INTEGER NOT NULL DEFAULT 0,
  errors INTEGER NOT NULL DEFAULT 0,
  active INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS system_info (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  bbs_name TEXT NOT NULL DEFAULT 'Mutineer BBS',
  sysop_name TEXT NOT NULL DEFAULT 'Sysop',
  first_online TEXT NOT NULL DEFAULT (date('now')),
  total_calls INTEGER NOT NULL DEFAULT 0,
  total_posts INTEGER NOT NULL DEFAULT 0,
  total_uploads INTEGER NOT NULL DEFAULT 0,
  total_downloads INTEGER NOT NULL DEFAULT 0,
  total_usage INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_history_date ON history(date DESC);

CREATE TABLE IF NOT EXISTS access_lists (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  expr TEXT NOT NULL                               -- textual ACS expression
);

CREATE TABLE IF NOT EXISTS user_votes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  vote_num INTEGER NOT NULL,                     -- vote topic 1-25
  answer INTEGER NOT NULL DEFAULT 0,             -- user's answer
  UNIQUE(user_id, vote_num),
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS subscription_types (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  days INTEGER NOT NULL DEFAULT 30,              -- subscription duration
  security_level_id INTEGER NOT NULL,            -- level while subscribed
  expired_level_id INTEGER NOT NULL DEFAULT 1,   -- level after expiry
  price INTEGER NOT NULL DEFAULT 0,              -- price in credits
  description TEXT
);

CREATE TABLE IF NOT EXISTS user_subscriptions (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  subscription_type_id INTEGER NOT NULL,
  started_at TEXT NOT NULL DEFAULT (datetime('now')),
  expires_at TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'active',         -- active, expired, cancelled
  FOREIGN KEY(user_id) REFERENCES users(id),
  FOREIGN KEY(subscription_type_id) REFERENCES subscription_types(id)
);
CREATE INDEX IF NOT EXISTS idx_user_subscriptions_user ON user_subscriptions(user_id);
CREATE INDEX IF NOT EXISTS idx_user_subscriptions_expires ON user_subscriptions(expires_at);

CREATE TABLE IF NOT EXISTS oneliners (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  user_handle TEXT NOT NULL,
  text TEXT NOT NULL,
  posted_at TEXT NOT NULL DEFAULT (datetime('now')),
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS short_messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  from_user INTEGER NOT NULL,
  to_user INTEGER NOT NULL,
  from_handle TEXT NOT NULL,
  to_handle TEXT NOT NULL,
  message TEXT NOT NULL,
  sent_at TEXT NOT NULL DEFAULT (datetime('now')),
  read_flag INTEGER NOT NULL DEFAULT 0,
  FOREIGN KEY(from_user) REFERENCES users(id),
  FOREIGN KEY(to_user) REFERENCES users(id)
);

CREATE INDEX IF NOT EXISTS idx_short_messages_to ON short_messages(to_user, read_flag);
CREATE INDEX IF NOT EXISTS idx_users_handle ON users(handle);
CREATE INDEX IF NOT EXISTS idx_oneliners_posted ON oneliners(posted_at DESC);
CREATE INDEX IF NOT EXISTS idx_user_votes_user ON user_votes(user_id);
CREATE INDEX IF NOT EXISTS idx_messages_area ON messages(area_id);
CREATE INDEX IF NOT EXISTS idx_messages_user ON messages(user_id);
CREATE INDEX IF NOT EXISTS idx_files_area ON files(area_id);
CREATE INDEX IF NOT EXISTS idx_files_user ON files(uploaded_by);
CREATE INDEX IF NOT EXISTS idx_nodes_status ON nodes(status);
CREATE INDEX IF NOT EXISTS idx_logs_level ON logs(level);

-- FidoNet AKA addresses (up to 20 per BBS)
CREATE TABLE IF NOT EXISTS fido_akas (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  zone INTEGER NOT NULL,
  net INTEGER NOT NULL,
  node INTEGER NOT NULL,
  point INTEGER NOT NULL DEFAULT 0,
  domain TEXT,                                    -- optional domain name
  is_primary INTEGER NOT NULL DEFAULT 0,         -- primary AKA flag
  UNIQUE(zone, net, node, point)
);

-- FidoNet echomail area links
CREATE TABLE IF NOT EXISTS fido_echolinks (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  area_id INTEGER NOT NULL,                      -- links to message_areas
  echotag TEXT NOT NULL,                         -- FidoNet echo tag (e.g., "BBS_CARNIVAL")
  aka_id INTEGER NOT NULL,                       -- which AKA to use
  origin TEXT,                                   -- origin line override
  high_water INTEGER NOT NULL DEFAULT 0,         -- last exported message id
  FOREIGN KEY(area_id) REFERENCES message_areas(id),
  FOREIGN KEY(aka_id) REFERENCES fido_akas(id),
  UNIQUE(echotag)
);

-- FidoNet netmail queue (outbound)
CREATE TABLE IF NOT EXISTS fido_netmail (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  from_zone INTEGER NOT NULL,
  from_net INTEGER NOT NULL,
  from_node INTEGER NOT NULL,
  from_point INTEGER NOT NULL DEFAULT 0,
  from_name TEXT NOT NULL,
  to_zone INTEGER NOT NULL,
  to_net INTEGER NOT NULL,
  to_node INTEGER NOT NULL,
  to_point INTEGER NOT NULL DEFAULT 0,
  to_name TEXT NOT NULL,
  subject TEXT NOT NULL,
  body TEXT NOT NULL,
  attr INTEGER NOT NULL DEFAULT 0,               -- NET_ATTR_* flags
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  sent_at TEXT,
  status TEXT NOT NULL DEFAULT 'pending'         -- pending, sent, failed
);
CREATE INDEX IF NOT EXISTS idx_fido_netmail_status ON fido_netmail(status);

-- FidoNet echomail export queue
CREATE TABLE IF NOT EXISTS fido_echomail_queue (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  echolink_id INTEGER NOT NULL,
  message_id INTEGER NOT NULL,
  status TEXT NOT NULL DEFAULT 'pending',        -- pending, exported, failed
  queued_at TEXT NOT NULL DEFAULT (datetime('now')),
  exported_at TEXT,
  FOREIGN KEY(echolink_id) REFERENCES fido_echolinks(id),
  FOREIGN KEY(message_id) REFERENCES messages(id)
);
CREATE INDEX IF NOT EXISTS idx_fido_echomail_queue_status ON fido_echomail_queue(status);

-- QWK Network Hubs
CREATE TABLE IF NOT EXISTS qwk_hubs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  bbs_id TEXT NOT NULL,                          -- BBS ID for packet naming
  call_schedule TEXT,                            -- cron-like schedule for calling
  last_call TEXT,
  enabled INTEGER NOT NULL DEFAULT 1
);

-- QWK Network Area Links
CREATE TABLE IF NOT EXISTS qwk_area_links (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  hub_id INTEGER NOT NULL,
  area_id INTEGER NOT NULL,                      -- local message area
  remote_conf INTEGER NOT NULL,                  -- conference number on hub
  high_water_in INTEGER NOT NULL DEFAULT 0,      -- last imported msg
  high_water_out INTEGER NOT NULL DEFAULT 0,     -- last exported msg
  FOREIGN KEY(hub_id) REFERENCES qwk_hubs(id),
  FOREIGN KEY(area_id) REFERENCES message_areas(id),
  UNIQUE(hub_id, area_id)
);

-- QWK Network Packet Queue
CREATE TABLE IF NOT EXISTS qwk_packet_queue (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  hub_id INTEGER NOT NULL,
  packet_type TEXT NOT NULL,                     -- 'qwk' (incoming) or 'rep' (outgoing)
  packet_path TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'pending',        -- pending, processed, failed
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  processed_at TEXT,
  FOREIGN KEY(hub_id) REFERENCES qwk_hubs(id)
);
CREATE INDEX IF NOT EXISTS idx_qwk_packet_queue_status ON qwk_packet_queue(status);

-- Chat logs
CREATE TABLE IF NOT EXISTS chat_logs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  chat_type TEXT NOT NULL,                       -- 'split', 'teleconf', 'sysop'
  room_id INTEGER,                               -- for teleconf rooms
  from_user INTEGER,
  from_handle TEXT NOT NULL,
  to_user INTEGER,                               -- for split chat
  to_handle TEXT,                                -- for split chat
  message TEXT NOT NULL,
  logged_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_chat_logs_type ON chat_logs(chat_type);
CREATE INDEX IF NOT EXISTS idx_chat_logs_time ON chat_logs(logged_at DESC);

-- Per-user message area scan flags (MZ command)
CREATE TABLE IF NOT EXISTS user_msg_scan_areas (
  user_id INTEGER NOT NULL,
  area_id INTEGER NOT NULL,
  scan_enabled INTEGER NOT NULL DEFAULT 1,
  PRIMARY KEY (user_id, area_id)
);

-- Message drafts (auto-saved on disconnect during composition)
CREATE TABLE IF NOT EXISTS drafts (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  area_id INTEGER NOT NULL DEFAULT 0,
  to_user_id INTEGER NOT NULL DEFAULT 0,
  to_name TEXT NOT NULL DEFAULT '',
  subject TEXT NOT NULL DEFAULT '',
  body TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_drafts_user ON drafts(user_id);
