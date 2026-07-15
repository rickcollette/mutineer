import { useState } from 'react';

type FileEntry = {
  id: string;
  name: string;
  desc: string;
  size: string;
  date: string;
  uploader: string;
  downloads: number;
  category: string;
  tags: string[];
};

const FILES: FileEntry[] = [
  { id: 'f001', name: 'LOTRD_21.ZIP',  desc: 'Legend of the Red Dragon v2.1 - Classic door game. Full install.',        size: '342 KB',  date: '05-12-97', uploader: 'SharkBait',    downloads: 4821,  category: 'DOOR GAMES',   tags: ['door','rpg'] },
  { id: 'f002', name: 'TRADEWRS.ZIP',  desc: 'TradeWars 2002 v3.09 - Space trading door. Fully registered.',            size: '891 KB',  date: '11-03-97', uploader: 'DavyScript',   downloads: 3109,  category: 'DOOR GAMES',   tags: ['door','strategy'] },
  { id: 'f003', name: 'PIRATEBX.ZIP',  desc: "Pirate's Booty door game - swashbuckling adventure. Unregistered.",      size: '218 KB',  date: '07-22-96', uploader: 'SharkBait',    downloads: 1654,  category: 'DOOR GAMES',   tags: ['door','pirate'] },
  { id: 'f004', name: 'BLACKJCK.ZIP',  desc: 'High Seas Blackjack v1.4 - Casino door with pirate theme.',               size: '156 KB',  date: '03-08-97', uploader: 'DavyScript',   downloads: 988,   category: 'DOOR GAMES',   tags: ['door','casino'] },
  { id: 'f005', name: 'MTNR_ANS.ZIP',  desc: 'Mutineer BBS official ANSI art pack - 47 screens by MadameSalt.',         size: '1.2 MB',  date: '01-15-97', uploader: 'MadameSalt',   downloads: 7204,  category: 'ANSI ART',     tags: ['ansi','art'] },
  { id: 'f006', name: 'JOLLY_RG.ZIP',  desc: 'Jolly Roger ANSI collection - skull & crossbones pack. 23 screens.',      size: '640 KB',  date: '09-04-96', uploader: 'MadameSalt',   downloads: 5432,  category: 'ANSI ART',     tags: ['ansi','pirate'] },
  { id: 'f007', name: 'OCEAN_BG.ZIP',  desc: 'Ocean & nautical themed ANSI backgrounds. 18 pieces.',                    size: '520 KB',  date: '04-19-97', uploader: 'MadameSalt',   downloads: 2891,  category: 'ANSI ART',     tags: ['ansi','art'] },
  { id: 'f008', name: 'PKZIP204.EXE',  desc: 'PKZIP v2.04g - Industry standard compression. Shareware.',                size: '202 KB',  date: '02-01-93', uploader: 'IronKeel',     downloads: 12304, category: 'UTILITIES',    tags: ['compression','essential'] },
  { id: 'f009', name: 'ARJZ250.ZIP',   desc: 'ARJ archiver v2.50 - alternative compression utility.',                  size: '178 KB',  date: '06-18-94', uploader: 'IronKeel',     downloads: 3201,  category: 'UTILITIES',    tags: ['compression'] },
  { id: 'f010', name: 'TELIX351.ZIP',  desc: 'TELIX v3.51 terminal emulator - dial-up essential. Registered.',          size: '847 KB',  date: '08-22-95', uploader: 'CapnBlackbyte',downloads: 8841,  category: 'UTILITIES',    tags: ['terminal','essential'] },
  { id: 'f011', name: 'ZMODEM.ZIP',    desc: 'ZMODEM transfer protocol suite. Batch transfers, crash recovery.',        size: '124 KB',  date: '03-14-94', uploader: 'IronKeel',     downloads: 6102,  category: 'UTILITIES',    tags: ['transfer'] },
  { id: 'f012', name: 'ACDSEE20.ZIP',  desc: 'ACDSee v2.0 image viewer - GIF/JPG/BMP/PCX. Shareware.',                  size: '1.1 MB',  date: '11-30-96', uploader: 'SharkBait',    downloads: 4299,  category: 'UTILITIES',    tags: ['graphics'] },
  { id: 'f013', name: 'RBBS_PC.ZIP',   desc: 'RBBS-PC v17.4 BBS software - run your own board. Full source.',           size: '2.4 MB',  date: '07-01-95', uploader: 'CapnBlackbyte',downloads: 1203,  category: 'BBS SOFTWARE', tags: ['bbs','sysop'] },
  { id: 'f014', name: 'FOSSIL.ZIP',    desc: 'FOSSIL driver v9.0 - serial communications standard for DOS.',            size: '88 KB',   date: '01-12-94', uploader: 'IronKeel',     downloads: 2844,  category: 'BBS SOFTWARE', tags: ['bbs','driver'] },
  { id: 'f015', name: 'DOORSDK.ZIP',   desc: 'Door game SDK v3.1 - develop your own BBS door games.',                   size: '560 KB',  date: '05-27-96', uploader: 'DavyScript',   downloads: 891,   category: 'BBS SOFTWARE', tags: ['bbs','dev'] },
  { id: 'f016', name: 'PIRATEMP.ZIP',  desc: 'Pirate themed MOD music pack - 14 tracks. Requires MOD player.',          size: '3.8 MB',  date: '10-01-97', uploader: 'MadameSalt',   downloads: 2108,  category: 'MUSIC / MODS', tags: ['music','mod'] },
  { id: 'f017', name: 'MODPLYR.ZIP',   desc: 'ModPlayer v1.7 - plays .MOD .S3M .XM .IT tracker files.',                 size: '412 KB',  date: '04-11-97', uploader: 'SharkBait',    downloads: 5602,  category: 'MUSIC / MODS', tags: ['music','player'] },
];

const CATEGORIES = ['ALL', ...Array.from(new Set(FILES.map(f => f.category)))];

const CAT_COLORS: Record<string, string> = {
  'DOOR GAMES':   'text-[#55ff55]',
  'ANSI ART':     'text-[#ff55ff]',
  'UTILITIES':    'text-[#55ffff]',
  'BBS SOFTWARE': 'text-[#ffff55]',
  'MUSIC / MODS': 'text-[#ff5555]',
};

export default function Download() {
  const [activeCategory, setActiveCategory] = useState('ALL');
  const [search, setSearch] = useState('');
  const [downloading, setDownloading] = useState<string | null>(null);
  const [downloadProgress, setDownloadProgress] = useState(0);

  const filtered = FILES.filter(f => {
    const matchCat    = activeCategory === 'ALL' || f.category === activeCategory;
    const matchSearch = search === '' ||
      f.name.toLowerCase().includes(search.toLowerCase()) ||
      f.desc.toLowerCase().includes(search.toLowerCase()) ||
      f.tags.some(t => t.toLowerCase().includes(search.toLowerCase()));
    return matchCat && matchSearch;
  });

  function handleDownload(file: FileEntry) {
    if (downloading) return;
    setDownloading(file.id);
    setDownloadProgress(0);
    let progress = 0;
    const interval = setInterval(() => {
      const increment = Math.random() * 15 + 3;
      progress = Math.min(100, progress + increment);
      setDownloadProgress(Math.floor(progress));
      if (progress >= 100) {
        clearInterval(interval);
        setTimeout(() => {
          setDownloading(null);
          setDownloadProgress(0);
        }, 800);
      }
    }, 120);
  }

  return (
    <div className="p-4 space-y-6 crt">
      {/* Header */}
      <div className="ascii-box p-4 bg-[#000022]">
        <pre className="text-[#ffff55] glow-yellow text-sm leading-tight select-none">
{`
 ╔══════════════════════════════════════════════════════╗
 ║         FILE LIBRARY  //  THE TREASURE VAULT         ║
 ║         ${FILES.length} FILES  |  FREE DOWNLOADS                   ║
 ╚══════════════════════════════════════════════════════╝
`}
        </pre>
      </div>

      {/* Download in progress */}
      {downloading && (() => {
        const file = FILES.find(f => f.id === downloading)!;
        return (
          <div className="ascii-box-yellow p-4 space-y-2 bg-[#110a00]">
            <div className="text-[#ffff55] glow-yellow text-lg">
              &gt;&gt; ZMODEM SEND: <span className="text-[#ffffff]">{file.name}</span>
            </div>
            <div className="text-[#55ff55] text-base">Sending file... {downloadProgress}%</div>
            <div className="w-full bg-[#000022] border border-[#0000aa] h-5 relative overflow-hidden">
              <div
                className="h-full bg-[#55ffff] transition-all duration-100"
                style={{ width: `${downloadProgress}%` }}
              />
              <div className="absolute inset-0 flex items-center justify-center text-[#000011] text-xs font-bold mix-blend-exclusion">
                {'='.repeat(Math.floor(downloadProgress / 3))}{'>'}&nbsp;{downloadProgress}%
              </div>
            </div>
            <div className="text-[#555555] text-sm">
              Estimated time: {Math.max(0, Math.ceil((100 - downloadProgress) / 8))}s at 28.8k
            </div>
          </div>
        );
      })()}

      {/* Search */}
      <div className="flex gap-2 items-center ascii-box p-3 bg-[#000011]">
        <span className="text-[#ffff55] text-lg shrink-0">&gt; SEARCH:</span>
        <input
          type="text"
          value={search}
          onChange={e => setSearch(e.target.value)}
          className="flex-1 bg-transparent border-none outline-none text-[#55ffff] text-lg placeholder-[#333355] font-[inherit]"
          placeholder="filename, description, or tag..."
        />
        {search && (
          <button onClick={() => setSearch('')} className="text-[#555555] hover:text-[#ff5555] text-lg">
            [CLR]
          </button>
        )}
      </div>

      {/* Category tabs */}
      <div className="flex flex-wrap gap-1">
        {CATEGORIES.map(cat => (
          <button
            key={cat}
            onClick={() => setActiveCategory(cat)}
            className={`px-3 py-1 text-base border transition-colors ${
              activeCategory === cat
                ? 'bg-[#55ffff] text-[#000011] border-[#55ffff]'
                : `border-[#0000aa] ${CAT_COLORS[cat] ?? 'text-[#55ffff]'} hover:border-[#5555ff]`
            }`}
          >
            [{cat}]
          </button>
        ))}
      </div>

      {/* File count */}
      <div className="text-[#555555] text-base">
        &gt; <span className="text-[#55ffff]">{filtered.length}</span> file{filtered.length !== 1 ? 's' : ''} found
        {activeCategory !== 'ALL' && <span className="text-[#ffff55]"> in [{activeCategory}]</span>}
        {search && <span className="text-[#ff55ff]"> matching "{search}"</span>}
      </div>

      {/* File table */}
      <div className="ascii-box overflow-x-auto bg-[#000011]">
        <table className="w-full text-base">
          <thead>
            <tr className="text-[#555555] border-b border-[#0000aa] bg-[#000022]">
              <td className="p-2 pr-4">FILENAME</td>
              <td className="p-2 pr-4">DESCRIPTION</td>
              <td className="p-2 pr-4">SIZE</td>
              <td className="p-2 pr-4">DATE</td>
              <td className="p-2 pr-4">BY</td>
              <td className="p-2 pr-4">DLs</td>
              <td className="p-2">ACTION</td>
            </tr>
          </thead>
          <tbody>
            {filtered.length === 0 ? (
              <tr>
                <td colSpan={7} className="p-4 text-center text-[#555555]">
                  &gt; No files found. Search terms failed. Try again, sailor.
                </td>
              </tr>
            ) : (
              filtered.map(file => (
                <tr key={file.id} className="border-b border-[#000033] hover:bg-[#000022] transition-colors">
                  <td className="p-2 pr-4 text-[#ffff55] glow-yellow font-bold whitespace-nowrap">{file.name}</td>
                  <td className="p-2 pr-4 text-[#aaaaaa] max-w-xs">
                    <div>{file.desc}</div>
                    <div className="text-[#5555ff] text-xs mt-1">{file.tags.map(t => `[${t}]`).join(' ')}</div>
                  </td>
                  <td className="p-2 pr-4 text-[#55ff55] whitespace-nowrap">{file.size}</td>
                  <td className="p-2 pr-4 text-[#555555] whitespace-nowrap">{file.date}</td>
                  <td className="p-2 pr-4 text-[#ff55ff] whitespace-nowrap">{file.uploader}</td>
                  <td className="p-2 pr-4 text-[#aaaaaa] whitespace-nowrap">{file.downloads.toLocaleString()}</td>
                  <td className="p-2 whitespace-nowrap">
                    <button
                      onClick={() => handleDownload(file)}
                      disabled={!!downloading}
                      className={`text-base px-2 py-0.5 border transition-colors disabled:opacity-40 ${
                        downloading === file.id
                          ? 'border-[#ffff55] text-[#ffff55]'
                          : 'border-[#0000aa] text-[#55ffff] hover:bg-[#55ffff] hover:text-[#000011] hover:border-[#55ffff]'
                      }`}
                    >
                      {downloading === file.id ? `[${downloadProgress}%]` : '[GET]'}
                    </button>
                  </td>
                </tr>
              ))
            )}
          </tbody>
        </table>
      </div>

      {/* Upload notice */}
      <div className="ascii-box p-3 bg-[#000011] space-y-1">
        <div className="text-[#55ffff]">&gt; UPLOAD SECTION</div>
        <div className="text-[#aaaaaa]">To upload files, you must have a registered account with a ratio of at least 1:2.</div>
        <div className="text-[#aaaaaa]">Use ZMODEM protocol only. Maximum single file size: 10 MB. Files reviewed before posting.</div>
        <div className="text-[#aaaaaa]">Contact <span className="text-[#ff55ff]">SharkBait</span> or <span className="text-[#ff55ff]">IronKeel</span> for upload access.</div>
      </div>
    </div>
  );
}
