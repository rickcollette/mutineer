import { useState, useEffect } from 'react';

const CREW = [
  { handle: 'CapnBlackbyte', rank: 'SysOp / Captain',    since: '1993', specialty: 'Kernel hacking, network plunder',  post_count: 14823, status: 'ONLINE'  },
  { handle: 'MadameSalt',    rank: 'Co-SysOp / First Mate',since: '1993', specialty: 'ANSI art, file libraries',        post_count: 9201,  status: 'ONLINE'  },
  { handle: 'IronKeel',      rank: 'Node Admin',          since: '1994', specialty: 'Hardware, modem pools',            post_count: 6740,  status: 'IDLE'    },
  { handle: 'SharkBait',     rank: 'File Wrangler',       since: '1995', specialty: 'Warez sorting, compression',       post_count: 3389,  status: 'OFFLINE' },
  { handle: 'DavyScript',    rank: 'Coder / Rigger',      since: '1996', specialty: 'Door games, scripting',            post_count: 2108,  status: 'IDLE'    },
];

const TIMELINE = [
  { year: '1993', event: 'Mutineer BBS founded in a damp Galveston basement. First 2400 baud node goes live.' },
  { year: '1994', event: 'Expanded to 4 nodes. Pirate-themed ANSI art overhaul by MadameSalt. 300+ users.' },
  { year: '1995', event: 'File libraries reach 2GB across 47 ZIP drives. SharkBait joins the crew.' },
  { year: '1996', event: 'Upgraded to 28.8k modems. Door game suite "The Jolly Roger Arcade" launched.' },
  { year: '1997', event: 'Internet gateway added. Mutineer reaches 1,200 registered buccaneers.' },
  { year: '1998', event: 'FidoNet backbone established. Cross-board messaging with 60+ BBSes.' },
  { year: '1999', event: 'Survived the Y2K scare. Upgraded all nodes to 56k. Still sailing strong.' },
];

const STATUS_STYLE: Record<string, string> = {
  ONLINE:  'text-[#55ff55]',
  IDLE:    'text-[#ffff55]',
  OFFLINE: 'text-[#555555]',
};

function StatusDot({ status }: { status: string }) {
  return (
    <span className={`${STATUS_STYLE[status] ?? 'text-[#555555]'} font-bold`}>
      [{status}]
    </span>
  );
}

export default function About() {
  const [visibleTimeline, setVisibleTimeline] = useState(0);

  useEffect(() => {
    if (visibleTimeline >= TIMELINE.length) return;
    const t = setTimeout(() => setVisibleTimeline(v => v + 1), 180);
    return () => clearTimeout(t);
  }, [visibleTimeline]);

  return (
    <div className="p-4 space-y-8 crt">
      {/* Header */}
      <div className="ascii-box p-4 bg-[#000022]">
        <pre className="text-[#ffff55] glow-yellow text-sm leading-tight select-none">
{`
 ╔══════════════════════════════════════════════════════╗
 ║          ABOUT MUTINEER BBS  //  CREW MANIFEST       ║
 ║            "We Sail the Digital Seas"                ║
 ╚══════════════════════════════════════════════════════╝
`}
        </pre>
      </div>

      {/* The Story */}
      <div className="ascii-box p-4 space-y-3 bg-[#000011]">
        <div className="text-[#ffff55] glow-yellow text-xl border-b border-[#333300] pb-2">
          ## THE LEGEND OF MUTINEER BBS
        </div>
        <p className="text-[#ffffff] text-lg leading-relaxed">
          It started with a single phone line, a battered 386, and a dream of digital piracy. In the
          autumn of 1993, CapnBlackbyte lashed a USRobotics modem to a surplus PC in a Galveston, TX
          basement and lit the beacon for all sea-faring data pirates of the Gulf Coast.
        </p>
        <p className="text-[#aaaaaa] text-lg leading-relaxed">
          What began as a small harbor for local hackers and phreaks grew into one of the most
          notorious BBSes in the Southwest. Mutineer became famous for its hand-crafted ANSI art,
          its legendary door game tournaments, and a file section that rivals any warez vault on
          the seven digital seas.
        </p>
        <p className="text-[#aaaaaa] text-lg leading-relaxed">
          We do not bow to Ma Bell. We do not fear the FCC. We fly the Jolly Roger and we keep
          our nodes online through hurricane season. If you've found this board, you're already
          one of us.
        </p>
        <div className="border-t border-[#0000aa] pt-3 text-[#555555] text-base">
          &gt; Node: <span className="text-[#55ffff]">GALVESTON-TX-01</span>
          &nbsp;|&nbsp; System: <span className="text-[#55ffff]">PCBoard 15.3</span>
          &nbsp;|&nbsp; Lines: <span className="text-[#55ff55]">8 active nodes</span>
          &nbsp;|&nbsp; Speed: <span className="text-[#ffff55]">up to 56k</span>
        </div>
      </div>

      {/* Crew */}
      <div className="ascii-box p-4 space-y-3 bg-[#000011]">
        <div className="text-[#ff55ff] glow-magenta text-xl border-b border-[#330033] pb-2">
          ## THE CREW  //  SHIP'S MANIFEST
        </div>
        <div className="overflow-x-auto">
          <table className="w-full text-base">
            <thead>
              <tr className="text-[#555555] border-b border-[#0000aa]">
                <td className="py-1 pr-4">HANDLE</td>
                <td className="py-1 pr-4">RANK</td>
                <td className="py-1 pr-4">ABOARD SINCE</td>
                <td className="py-1 pr-4">SPECIALTY</td>
                <td className="py-1 pr-4">POSTS</td>
                <td className="py-1">STATUS</td>
              </tr>
            </thead>
            <tbody>
              {CREW.map((c) => (
                <tr key={c.handle} className="border-b border-[#000033] hover:bg-[#000022] transition-colors">
                  <td className="py-2 pr-4 text-[#55ffff] glow-cyan font-bold">{c.handle}</td>
                  <td className="py-2 pr-4 text-[#ffff55]">{c.rank}</td>
                  <td className="py-2 pr-4 text-[#aaaaaa]">{c.since}</td>
                  <td className="py-2 pr-4 text-[#555555] text-sm">{c.specialty}</td>
                  <td className="py-2 pr-4 text-[#ff55ff]">{c.post_count.toLocaleString()}</td>
                  <td className="py-2"><StatusDot status={c.status} /></td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>

      {/* Timeline */}
      <div className="ascii-box p-4 space-y-3 bg-[#000011]">
        <div className="text-[#55ffff] glow-cyan text-xl border-b border-[#003333] pb-2">
          ## SHIP'S LOG  //  VOYAGE TIMELINE
        </div>
        <div className="space-y-2">
          {TIMELINE.slice(0, visibleTimeline).map((entry, i) => (
            <div key={i} className="flex gap-4 text-lg">
              <span className="text-[#ffff55] font-bold shrink-0">[{entry.year}]</span>
              <span className="text-[#aaaaaa]">{entry.event}</span>
            </div>
          ))}
          {visibleTimeline < TIMELINE.length && (
            <div className="text-[#555555] text-base">
              &gt; Loading ship's log<span className="cursor"></span>
            </div>
          )}
        </div>
      </div>

      {/* Rules */}
      <div className="ascii-box-magenta p-4 space-y-3 bg-[#110011]">
        <div className="text-[#ff55ff] glow-magenta text-xl border-b border-[#330033] pb-2">
          ## THE PIRATE CODE  //  RULES OF THE SHIP
        </div>
        <div className="space-y-1 text-lg">
          {[
            ["I.",    "No narc'in'. What happens on Mutineer stays on Mutineer."],
            ["II.",   "Upload ratio enforced. Leeches walk the plank."],
            ["III.",  "No flooding the message boards. Quality over quantity."],
            ["IV.",   "Respect the crew. Personal attacks get you keelhauled."],
            ["V.",    "Keep your ratio above 1:3 or face disciplinary action."],
            ["VI.",   "No posting personal info (real names, addresses, etc.)."],
            ["VII.",  "Warez shared here are for educational purposes only. Arr."],
            ["VIII.", "The SysOp's word is law. Appeals go to the First Mate."],
          ].map(([num, rule]) => (
            <div key={num} className="leading-relaxed">
              <span className="text-[#ff5555] mr-2">{num}</span>
              <span className="text-[#aaaaaa]">{rule}</span>
            </div>
          ))}
        </div>
      </div>

      {/* Contact */}
      <div className="ascii-box p-4 text-center space-y-2 bg-[#000022]">
        <div className="text-[#ffff55] text-xl glow-yellow">-- HAIL THE CAPTAIN --</div>
        <div className="text-[#55ffff] text-lg">
          Dial: <span className="text-[#ffffff]">(409) 555-0BBS</span>
          &nbsp;|&nbsp; FidoNet: <span className="text-[#ffffff]">1:106/1887</span>
          &nbsp;|&nbsp; Internet: <span className="text-[#ffffff]">mutineer@bbs.net</span>
        </div>
        <div className="text-[#555555] text-base">
          SysOp hours: 18:00-02:00 CST &nbsp;|&nbsp; Emergency: leave a comment on node 1
        </div>
      </div>
    </div>
  );
}
