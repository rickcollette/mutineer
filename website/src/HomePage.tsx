import mutineerAnsi from './mutineer.ans?raw';
import AnsiArt from './AnsiArt';
import { BbsStatus } from './statusApi';

type HomePageProps = {
  status: BbsStatus | null;
  onNavigate: (page: 'download' | 'documentation' | 'about') => void;
};

const STATUS_ROWS = [
  ['Network', 'Telnet listener + per-caller sessions', 'ONLINE'],
  ['Mail', 'QWK, FidoNet netmail, echomail', 'ACTIVE'],
  ['Extensibility', 'Plugins, doors, Buccaneer bytecode', 'LOADED'],
];

const FEATURE_ROWS = [
  ['Messages', 'Threaded public areas, private email, QWK packets, offline replies.'],
  ['Files', 'ACS-gated file areas, uploads, downloads, credits, batch queues.'],
  ['Doors', 'Native doors, DOSBox programs, and Buccaneer scripts.'],
  ['PLANK', 'Object routing, bundles, coved/plankd tools, offline exchange.'],
];

function value(value: number | string | null | undefined, fallback = 'n/a') {
  if (value === null || value === undefined || value === '') return fallback;
  return String(value);
}

function RuntimePanel({ status }: { status: BbsStatus | null }) {
  const rows = [
    ['PLANK Node', status?.summary.plank_node],
    ['Callers Online', status?.summary.callers_online],
    ['Version', status?.summary.version],
    ['BBS', status?.summary.bbs_online ? `ONLINE :${status.bbs.port}` : 'OFFLINE'],
    ['Messages', status?.summary.aggregate_messages],
    ['Door Games', status?.summary.door_games],
    ['Teleconference', status?.summary.teleconference_chat_users],
  ];

  return (
    <div className="ascii-box p-4 bg-[#000011]">
      <div className="text-[#55ffff] text-xl border-b border-[#003333] pb-2 mb-3">## LIVE BBS API</div>
      <div className="space-y-2">
        {rows.map(([label, rowValue]) => (
          <div key={label} className="grid grid-cols-[8rem_1fr] gap-2 border-b border-[#000033] pb-2">
            <span className="text-[#ffff55]">{label}</span>
            <span className="text-[#55ff55]">{value(rowValue)}</span>
          </div>
        ))}
      </div>
      <div className="text-[#555555] text-sm mt-3">
        Updated: <span className="text-[#aaaaaa]">{status ? new Date(status.generated_at).toLocaleString() : 'waiting for /api/status.json'}</span>
      </div>
    </div>
  );
}

function PlankPanel({ status }: { status: BbsStatus | null }) {
  const counts = status?.plank.counts;

  return (
    <div className="ascii-box p-4 bg-[#000011]">
      <div className="text-[#ff55ff] text-xl border-b border-[#330033] pb-2 mb-3">## PLANK NODE STATUS</div>
      <div className="grid grid-cols-2 gap-2 text-base">
        <span className="text-[#55ffff]">Node</span><span className="text-[#aaaaaa]">{value(status?.plank.identity.node_name)}</span>
        <span className="text-[#55ffff]">Network</span><span className="text-[#aaaaaa]">{value(status?.plank.identity.network_name)}</span>
        <span className="text-[#55ffff]">Peers</span><span className="text-[#55ff55]">{counts ? `${counts.active_peers}/${counts.peers}` : 'n/a'}</span>
        <span className="text-[#55ffff]">Links</span><span className="text-[#55ff55]">{counts ? `${counts.enabled_links}/${counts.links}` : 'n/a'}</span>
        <span className="text-[#55ffff]">Areas</span><span className="text-[#55ff55]">{value(counts?.areas)}</span>
        <span className="text-[#55ffff]">Objects</span><span className="text-[#55ff55]">{value(counts?.objects)}</span>
        <span className="text-[#55ffff]">Outbound</span><span className="text-[#ffff55]">{value(counts?.outbound_queued)}</span>
        <span className="text-[#55ffff]">Deadletters</span><span className="text-[#ff5555]">{value(counts?.deadletters)}</span>
      </div>
      {status?.plank.links.length ? (
        <div className="mt-4 space-y-2">
          {status.plank.links.map(link => (
            <div key={`${link.name}-${link.host}`} className="border-t border-[#000033] pt-2">
              <span className="text-[#ffff55]">{link.name}</span>
              <span className="text-[#555555]"> :: </span>
              <span className={link.enabled && !link.paused ? 'text-[#55ff55]' : 'text-[#ff5555]'}>
                {link.enabled && !link.paused ? 'ENABLED' : 'PAUSED/OFF'}
              </span>
              <div className="text-[#aaaaaa] text-sm">{link.host}:{link.port} {link.last_error ? `- ${link.last_error}` : ''}</div>
            </div>
          ))}
        </div>
      ) : (
        <div className="text-[#555555] mt-4">No PLANK links configured yet.</div>
      )}
    </div>
  );
}

export default function HomePage({ status, onNavigate }: HomePageProps) {
  return (
    <div className="p-4 space-y-6 crt">
      <section className="ascii-box p-4 bg-[#000022]">
        <div className="flex justify-center overflow-x-auto">
          <AnsiArt
            content={mutineerAnsi}
            className="text-xs sm:text-sm leading-tight select-none font-mono"
          />
        </div>
        <div className="mt-4 text-center text-[#ffff55] glow-yellow text-xl sm:text-2xl">
          Avast ye scurvy corporate dogs, prepare to be boarded
        </div>
        <div className="border-t border-[#0000aa] pt-4 grid gap-4 lg:grid-cols-[1fr_18rem]">
          <div className="space-y-3">
            <h1 className="text-[#ffff55] glow-yellow text-3xl m-0">Classic BBS Core, Modern Toolchain</h1>
            <p className="text-[#aaaaaa] text-lg leading-relaxed">
              Mutineer is a multi-threaded telnet BBS with real menus, ANSI art, message boards,
              file areas, doors, plugins, and network mail. The website now exposes the repo docs
              inside this Vite console while preserving the static HTML manual.
            </p>
          </div>
          <div className="ascii-box-yellow bg-[#110a00] p-3 space-y-2">
            <div className="text-[#ffff55]">QUICK CONNECT</div>
            <pre className="text-[#55ff55] text-base overflow-x-auto">{`docker compose up -d
telnet localhost 2929

prod: telnet mutineerbbs.com 23

login: sysop
pass:  mutineer`}</pre>
          </div>
        </div>
      </section>

      <section className="grid gap-4 md:grid-cols-3">
        <button
          onClick={() => onNavigate('documentation')}
          className="ascii-box p-4 bg-[#000011] text-left hover:bg-[#000033] transition-colors"
        >
          <div className="text-[#ffff55] text-xl glow-yellow">[C] CAPTAIN'S MANUAL</div>
          <div className="text-[#aaaaaa] mt-2">Search real docs for setup, ops, architecture, Buccaneer, PLANK, and references.</div>
        </button>
        <button
          onClick={() => onNavigate('download')}
          className="ascii-box p-4 bg-[#000011] text-left hover:bg-[#000033] transition-colors"
        >
          <div className="text-[#55ff55] text-xl glow-green">[T] TREASURE VAULT</div>
          <div className="text-[#aaaaaa] mt-2">Browse the themed file library and transfer UI.</div>
        </button>
        <button
          onClick={() => onNavigate('about')}
          className="ascii-box p-4 bg-[#000011] text-left hover:bg-[#000033] transition-colors"
        >
          <div className="text-[#ff55ff] text-xl glow-magenta">[W] CREW MANIFEST</div>
          <div className="text-[#aaaaaa] mt-2">Read the project story, crew panel, and retro system profile.</div>
        </button>
      </section>

      <section className="grid gap-4 lg:grid-cols-[1fr_1fr]">
        <RuntimePanel status={status} />
        <PlankPanel status={status} />
      </section>

      <section className="grid gap-4 lg:grid-cols-[1fr_1fr]">
        <div className="ascii-box p-4 bg-[#000011]">
          <div className="text-[#55ffff] text-xl border-b border-[#003333] pb-2 mb-3">## SYSTEM STATUS</div>
          <div className="space-y-2">
            {STATUS_ROWS.map(([label, detail, status]) => (
              <div key={label} className="grid grid-cols-[7rem_1fr_5rem] gap-2 text-base border-b border-[#000033] pb-2">
                <span className="text-[#ffff55]">{label}</span>
                <span className="text-[#aaaaaa]">{detail}</span>
                <span className="text-[#55ff55] text-right">[{status}]</span>
              </div>
            ))}
          </div>
        </div>

        <div className="ascii-box p-4 bg-[#000011]">
          <div className="text-[#ff55ff] text-xl border-b border-[#330033] pb-2 mb-3">## FEATURE MAP</div>
          <div className="space-y-2">
            {FEATURE_ROWS.map(([label, detail]) => (
              <div key={label} className="flex gap-3 text-base border-b border-[#000033] pb-2">
                <span className="text-[#55ffff] w-24 shrink-0">{label}</span>
                <span className="text-[#aaaaaa]">{detail}</span>
              </div>
            ))}
          </div>
        </div>
      </section>
    </div>
  );
}
