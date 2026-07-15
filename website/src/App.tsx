import { useEffect, useState } from 'react';
import { BbsStatus, loadBbsStatus } from './statusApi';

type Page = 'main' | 'active' | 'download' | 'documentation' | 'about';

const NAV_ITEMS: { id: Page; label: string; key: string }[] = [
  { id: 'main',          label: 'MAIN DECK',        key: 'M' },
  { id: 'active',        label: "ACTIVE BBS'S",     key: 'A' },
  { id: 'download',      label: 'TREASURE VAULT',   key: 'T' },
  { id: 'documentation', label: "CAPTAIN'S LOG", key: 'C' },
  { id: 'about',         label: 'CREW MANIFEST',    key: 'W' },
];

import HomePage      from './HomePage';
import ActiveBbs     from './ActiveBbs';
import Download      from './Download';
import Documentation from './Documentation';
import About         from './About';

export default function App() {
  const [page, setPage] = useState<Page>('main');
  const [status, setStatus] = useState<BbsStatus | null>(null);

  useEffect(() => {
    window.scrollTo({ top: 0, left: 0 });
  }, [page]);

  useEffect(() => {
    let cancelled = false;

    async function refresh() {
      try {
        const next = await loadBbsStatus();
        if (!cancelled) setStatus(next);
      } catch {
        if (!cancelled) setStatus(null);
      }
    }

    refresh();
    const timer = window.setInterval(refresh, 30000);
    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, []);

  const footerVersion = status?.summary.version ? `v${status.summary.version.replace(/^v/, '')}` : 'version pending';
  const footerPlank = status?.summary.plank_node ? `PLANK ${status.summary.plank_node}` : 'PLANK pending';
  const footerOnline = status?.summary.bbs_online ? 'ONLINE' : 'OFFLINE';

  return (
    <div className="min-h-screen bg-[#000011] text-[#55ffff]">
      {/* Top bar */}
      <header className="border-b border-[#5555ff] bg-[#000022] sticky top-0 z-50">
        <div className="max-w-6xl mx-auto px-4 py-2 flex items-center justify-between gap-4 flex-wrap">
          <div className="flex items-center gap-3">
            {/* skull in bright red */}
            <span className="text-[#ff5555] text-2xl select-none" style={{ textShadow: '0 0 8px #ff5555' }}>&#9760;</span>
            <span className="text-[#ffff55] text-3xl glow-yellow tracking-widest font-vt323">MUTINEER BBS</span>
            <span className="text-[#555555] text-lg hidden sm:block">// est. 1993</span>
          </div>
          <div className="flex items-center gap-1 text-sm text-[#555555]">
            <span className={status?.bbs.online ? 'text-[#55ff55]' : 'text-[#ff5555]'}>●</span>
            <span className="text-[#aaaaaa]">{status?.plank.identity.node_name ?? 'MUTINEER'}@{status?.plank.identity.network_name ?? 'LOCAL'}</span>
            <span className="mx-2 text-[#333355]">|</span>
            <span className="text-[#ff55ff]">{status?.nodes.active ?? 0}/{status?.nodes.total ?? 0} NODES ACTIVE</span>
            <span className="mx-2 text-[#333355]">|</span>
            <span className="text-[#aaaaaa]">{status?.bbs.version ?? 'status pending'}</span>
          </div>
        </div>

        {/* Nav */}
        <nav className="border-t border-[#0000aa] bg-[#000011]">
          <div className="max-w-6xl mx-auto px-4 flex flex-wrap">
            {NAV_ITEMS.map(item => (
              <button
                key={item.id}
                onClick={() => setPage(item.id)}
                className={`menu-item px-4 py-2 text-base tracking-wider ${page === item.id ? 'active font-bold' : ''}`}
              >
                <span className="nav-key mr-1">[{item.key}]</span>
                {item.label}
              </button>
            ))}
          </div>
        </nav>
      </header>

      {/* Main content */}
      <main className="max-w-6xl mx-auto px-4 py-6">
        {page === 'main'          && <HomePage status={status} onNavigate={(p) => setPage(p as Page)} />}
        {page === 'active'        && <ActiveBbs />}
        {page === 'download'      && <Download />}
        {page === 'documentation' && <Documentation />}
        {page === 'about'         && <About />}
      </main>

      {/* Footer */}
      <footer className="border-t border-[#5555ff] bg-[#000022] mt-8">
        <div className="max-w-6xl mx-auto px-4 py-4 flex flex-col items-center gap-2 text-sm text-[#555555]">
          <div className="text-center">
            <span className="text-[#aaaaaa]">MUTINEER BBS</span>
            <span className="text-[#333355]"> &nbsp;|&nbsp; </span>
            <span>All files shared for educational purposes</span>
            <span className="text-[#333355]"> &nbsp;|&nbsp; </span>
            <span className="text-[#ff55ff]">Fly the Jolly Roger</span>
          </div>
          <div className="flex flex-wrap justify-center gap-4 text-[#aaaaaa]">
            <span>{footerVersion}</span>
            <span className="text-[#555555]">|</span>
            <span>{footerPlank}</span>
            <span className="text-[#555555]">|</span>
            <span className={status?.summary.bbs_online ? 'text-[#55ff55]' : 'text-[#ff5555]'}>{footerOnline}</span>
          </div>
        </div>
      </footer>
    </div>
  );
}
