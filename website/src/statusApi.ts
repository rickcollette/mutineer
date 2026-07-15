export type BbsStatus = {
  generated_at: string;
  bbs: {
    name: string;
    online: boolean;
    host: string;
    port: number;
    version: string;
    pid: number | null;
    uptime_seconds: number | null;
  };
  stats: {
    users: number;
    messages: number;
    files: number;
    door_games: number;
    message_areas: number;
    file_areas: number;
    today: {
      calls: number;
      posts: number;
      emails: number;
      newusers: number;
      uploads: number;
      downloads: number;
    };
    totals: {
      calls: number;
      posts: number;
      uploads: number;
      downloads: number;
      days_online: number;
    };
  };
  nodes: {
    active: number;
    total: number;
    rows: Array<{
      node: number;
      status: string;
      user: string | null;
      updated_at: string | null;
    }>;
  };
  summary: {
    plank_node: string | null;
    callers_online: number;
    version: string;
    bbs_online: boolean;
    aggregate_messages: number;
    door_games: number;
    teleconference_chat_users: number;
  };
  plank: {
    identity: {
      node_name: string | null;
      network_name: string | null;
      software_name: string | null;
      software_version: string | null;
      is_cove: boolean;
    };
    counts: {
      peers: number;
      active_peers: number;
      links: number;
      enabled_links: number;
      areas: number;
      objects: number;
      outbound_queued: number;
      deadletters: number;
    };
    links: Array<{
      name: string;
      peer: string | null;
      host: string;
      port: number;
      enabled: boolean;
      paused: boolean;
      state: number;
      last_success_at: string | null;
      last_error: string | null;
    }>;
  };
};

export async function loadBbsStatus(): Promise<BbsStatus | null> {
  const response = await fetch('/api/status.json', {
    cache: 'no-store',
    headers: { Accept: 'application/json' },
  });

  if (!response.ok) {
    return null;
  }

  return response.json() as Promise<BbsStatus>;
}
