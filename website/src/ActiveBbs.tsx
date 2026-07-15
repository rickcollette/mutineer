import { useEffect, useState } from 'react';

type ActiveBoard = {
  id: string;
  plank_node: string;
  plank_number: string;
  bbs_name: string;
  sysop_name: string;
  location?: string;
  telnet_host: string;
  telnet_port: number;
  website?: string;
  notes?: string;
  online: boolean;
  last_checkin: string;
  age_seconds: number;
  reported: {
    version?: string;
    callers_online?: number;
    aggregate_messages?: number;
    door_games?: number;
    teleconference_chat_users?: number;
    bbs_online?: boolean;
  };
};

type ActiveBbsResponse = {
  generated_at: string;
  max_age_hours: number;
  count: number;
  boards: ActiveBoard[];
};

function fmt(value: string | number | null | undefined, fallback = 'n/a') {
  if (value === null || value === undefined || value === '') return fallback;
  return String(value);
}

function ageLabel(seconds: number) {
  const minutes = Math.floor(seconds / 60);
  if (minutes < 1) return 'just now';
  if (minutes < 60) return `${minutes}m ago`;
  return `${Math.floor(minutes / 60)}h ${minutes % 60}m ago`;
}

export default function ActiveBbs() {
  const [data, setData] = useState<ActiveBbsResponse | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;

    async function refresh() {
      try {
        const response = await fetch('/api/active-bbs.json', {
          cache: 'no-store',
          headers: { Accept: 'application/json' },
        });
        if (!response.ok) throw new Error(`status ${response.status}`);
        const next = await response.json() as ActiveBbsResponse;
        if (!cancelled) {
          setData(next);
          setError(null);
        }
      } catch (err) {
        if (!cancelled) setError(err instanceof Error ? err.message : 'request failed');
      }
    }

    refresh();
    const timer = window.setInterval(refresh, 60000);
    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, []);

  return (
    <div className="p-4 space-y-6 crt">
      <section className="ascii-box p-4 bg-[#000022]">
        <div className="text-[#ffff55] glow-yellow text-3xl">ACTIVE MUTINEER BBS'S</div>
        <div className="text-[#aaaaaa] mt-2">
          Boards listed here are allowlisted in the Mutineer directory TOML and have checked in with the private key during the last {data?.max_age_hours ?? 6} hours.
        </div>
        <div className="text-[#555555] text-sm mt-3">
          Directory refresh: <span className="text-[#aaaaaa]">{data ? new Date(data.generated_at).toLocaleString() : 'loading'}</span>
          {error && <span className="text-[#ff5555]"> :: {error}</span>}
        </div>
      </section>

      {!data?.boards.length && (
        <section className="ascii-box p-4 bg-[#000011]">
          <div className="text-[#ff55ff] text-xl">NO ACTIVE CHECK-INS</div>
          <div className="text-[#aaaaaa] mt-2">
            No allowlisted BBS has checked in within the active window.
          </div>
        </section>
      )}

      <section className="grid gap-4">
        {data?.boards.map(board => (
          <article key={board.id} className="ascii-box p-4 bg-[#000011]">
            <div className="flex flex-col md:flex-row md:items-start md:justify-between gap-3 border-b border-[#000033] pb-3">
              <div>
                <div className="text-[#ffff55] glow-yellow text-2xl">{board.bbs_name}</div>
                <div className="text-[#55ffff]">
                  PLANK {fmt(board.plank_number)} :: {fmt(board.plank_node)}
                </div>
                <div className="text-[#aaaaaa] text-sm">
                  Sysop: {fmt(board.sysop_name)} {board.location ? `// ${board.location}` : ''}
                </div>
              </div>
              <div className="text-right">
                <div className={board.online ? 'text-[#55ff55]' : 'text-[#ff5555]'}>
                  [{board.online ? 'UP' : 'DOWN'}]
                </div>
                <div className="text-[#555555] text-sm">check-in {ageLabel(board.age_seconds)}</div>
              </div>
            </div>

            <div className="grid gap-2 md:grid-cols-4 mt-4">
              <div>
                <div className="text-[#555555]">Version</div>
                <div className="text-[#55ff55]">{fmt(board.reported.version)}</div>
              </div>
              <div>
                <div className="text-[#555555]">Callers</div>
                <div className="text-[#55ff55]">{fmt(board.reported.callers_online, '0')}</div>
              </div>
              <div>
                <div className="text-[#555555]">Messages</div>
                <div className="text-[#55ff55]">{fmt(board.reported.aggregate_messages, '0')}</div>
              </div>
              <div>
                <div className="text-[#555555]">Doors / Chat</div>
                <div className="text-[#55ff55]">{fmt(board.reported.door_games, '0')} / {fmt(board.reported.teleconference_chat_users, '0')}</div>
              </div>
            </div>

            <div className="flex flex-wrap gap-x-4 gap-y-1 mt-4 text-sm">
              <span className="text-[#aaaaaa]">telnet {board.telnet_host} {board.telnet_port}</span>
              {board.website && <a className="text-[#55ffff] hover:text-[#ffff55]" href={board.website}>website</a>}
              {board.notes && <span className="text-[#555555]">{board.notes}</span>}
            </div>
          </article>
        ))}
      </section>
    </div>
  );
}
