import { ReactNode, useEffect, useMemo, useRef, useState } from 'react';

import architecture from '../../docs/architecture.md?raw';
import buccaneer from '../../docs/buccaneer/index.md?raw';
import buccaneerTop from '../../docs/buccaneer.md?raw';
import buccaneerDoorPackages from '../../docs/buccaneer/door-packages.md?raw';
import buccaneerHostApi from '../../docs/buccaneer/host-api.md?raw';
import buccaneerProgrammersGuide from '../../docs/buccaneer/programmers-guide.md?raw';
import buccaneerToolchain from '../../docs/buccaneer/toolchain.md?raw';
import chat from '../../docs/chat-and-social.md?raw';
import cliTools from '../../docs/cli-tools.md?raw';
import configuration from '../../docs/configuration.md?raw';
import databaseReference from '../../docs/reference/database.md?raw';
import deployment from '../../docs/deployment.md?raw';
import developerGuide from '../../docs/developer-guide.md?raw';
import doors from '../../docs/doors-and-scripting.md?raw';
import fileCommands from '../../docs/reference/file-commands.md?raw';
import files from '../../docs/files-and-protocols.md?raw';
import gettingStarted from '../../docs/getting-started.md?raw';
import manualIndex from '../../docs/index.md?raw';
import menuActions from '../../docs/reference/menu-actions.md?raw';
import menus from '../../docs/menus-and-ui.md?raw';
import messageCommands from '../../docs/reference/message-commands.md?raw';
import messages from '../../docs/messages-and-mail.md?raw';
import mciReference from '../../docs/reference/acs-mci.md?raw';
import networkingPlank from '../../docs/networking-plank.md?raw';
import overview from '../../docs/overview.md?raw';
import plugins from '../../docs/plugins.md?raw';
import quickStart from '../../docs/quick-start.md?raw';
import screenshots from '../../docs/screenshots.md?raw';
import sysop from '../../docs/sysop-guide.md?raw';
import windows from '../../docs/windows.md?raw';
import license from '../../LICENSE?raw';

type DocCategory = 'Start' | 'Operate' | 'Build' | 'Extend' | 'Reference' | 'Project';

type DocArticle = {
  id: string;
  title: string;
  category: DocCategory;
  source: string;
  body: string;
};

const ARTICLES: DocArticle[] = [
  { id: 'manual-index', title: 'Manual Index', category: 'Start', source: 'docs/index.md', body: manualIndex },
  { id: 'quick-start', title: 'Quick Start', category: 'Start', source: 'docs/quick-start.md', body: quickStart },
  { id: 'windows', title: 'Running on Windows', category: 'Start', source: 'docs/windows.md', body: windows },
  { id: 'getting-started', title: 'Getting Started', category: 'Start', source: 'docs/getting-started.md', body: gettingStarted },
  { id: 'deployment', title: 'Deployment', category: 'Start', source: 'docs/deployment.md', body: deployment },
  { id: 'overview', title: 'Overview', category: 'Build', source: 'docs/overview.md', body: overview },
  { id: 'configuration', title: 'Configuration', category: 'Operate', source: 'docs/configuration.md', body: configuration },
  { id: 'sysop', title: 'Sysop Guide', category: 'Operate', source: 'docs/sysop-guide.md', body: sysop },
  { id: 'messages', title: 'Messages and Mail', category: 'Operate', source: 'docs/messages-and-mail.md', body: messages },
  { id: 'files', title: 'Files and Protocols', category: 'Operate', source: 'docs/files-and-protocols.md', body: files },
  { id: 'chat', title: 'Chat and Social', category: 'Operate', source: 'docs/chat-and-social.md', body: chat },
  { id: 'architecture', title: 'Architecture', category: 'Build', source: 'docs/architecture.md', body: architecture },
  { id: 'menus', title: 'Menus and UI', category: 'Build', source: 'docs/menus-and-ui.md', body: menus },
  { id: 'networking-plank', title: 'PLANK Networking', category: 'Build', source: 'docs/networking-plank.md', body: networkingPlank },
  { id: 'developer-guide', title: 'Developer Guide', category: 'Build', source: 'docs/developer-guide.md', body: developerGuide },
  { id: 'cli-tools', title: 'CLI Tools', category: 'Build', source: 'docs/cli-tools.md', body: cliTools },
  { id: 'screenshots', title: 'Screenshots', category: 'Build', source: 'docs/screenshots.md', body: screenshots },
  { id: 'doors', title: 'Doors and Scripting', category: 'Extend', source: 'docs/doors-and-scripting.md', body: doors },
  { id: 'plugins', title: 'Plugins', category: 'Extend', source: 'docs/plugins.md', body: plugins },
  { id: 'buccaneer-top', title: 'Buccaneer Overview', category: 'Extend', source: 'docs/buccaneer.md', body: buccaneerTop },
  { id: 'buccaneer', title: 'Buccaneer', category: 'Extend', source: 'docs/buccaneer/index.md', body: buccaneer },
  { id: 'buccaneer-guide', title: "Buccaneer Programmer's Guide", category: 'Extend', source: 'docs/buccaneer/programmers-guide.md', body: buccaneerProgrammersGuide },
  { id: 'buccaneer-host-api', title: 'Buccaneer Host API', category: 'Extend', source: 'docs/buccaneer/host-api.md', body: buccaneerHostApi },
  { id: 'buccaneer-toolchain', title: 'Buccaneer Toolchain', category: 'Extend', source: 'docs/buccaneer/toolchain.md', body: buccaneerToolchain },
  { id: 'buccaneer-door-packages', title: 'Buccaneer Door Packages', category: 'Extend', source: 'docs/buccaneer/door-packages.md', body: buccaneerDoorPackages },
  { id: 'menu-actions', title: 'Menu Actions Reference', category: 'Reference', source: 'docs/reference/menu-actions.md', body: menuActions },
  { id: 'message-commands', title: 'Message Commands Reference', category: 'Reference', source: 'docs/reference/message-commands.md', body: messageCommands },
  { id: 'file-commands', title: 'File Commands Reference', category: 'Reference', source: 'docs/reference/file-commands.md', body: fileCommands },
  { id: 'acs-mci', title: 'ACS and MCI Reference', category: 'Reference', source: 'docs/reference/acs-mci.md', body: mciReference },
  { id: 'database', title: 'Database Reference', category: 'Reference', source: 'docs/reference/database.md', body: databaseReference },
  { id: 'license', title: 'MIT License', category: 'Project', source: 'LICENSE', body: `# MIT License\n\n${license.replace(/^MIT License\\s*/, '')}` },
];

const DIRECTORY_TARGETS: Record<string, string> = {
  'docs/reference': 'docs/reference/menu-actions.md',
  'docs/buccaneer': 'docs/buccaneer/index.md',
};

const CATEGORY_COLORS: Record<DocCategory, string> = {
  Start: 'text-[#55ffff]',
  Operate: 'text-[#55ff55]',
  Build: 'text-[#ffff55]',
  Extend: 'text-[#ff55ff]',
  Reference: 'text-[#ff5555]',
  Project: 'text-[#aaaaaa]',
};

const CATEGORIES: Array<DocCategory | 'All'> = ['All', 'Start', 'Operate', 'Build', 'Extend', 'Reference', 'Project'];

function stripComments(markdown: string) {
  return markdown.replace(/^\s*<!--[\s\S]*?-->\s*$/gm, '').trim();
}

function normalizeDocPath(source: string, href: string) {
  if (/^[a-z]+:\/\//i.test(href) || href.startsWith('#')) return href;
  if (href.startsWith('/')) return href.replace(/^\//, '');

  const sourceParts = source.split('/');
  sourceParts.pop();
  const parts = [...sourceParts, ...href.split('/')];
  const normalized: string[] = [];

  for (const part of parts) {
    if (!part || part === '.') continue;
    if (part === '..') normalized.pop();
    else normalized.push(part);
  }

  return normalized.join('/');
}

function docFallbackHref(source: string, href: string) {
  const normalized = normalizeDocPath(source, href);
  if (/^[a-z]+:\/\//i.test(normalized) || normalized.startsWith('#')) return normalized;
  if (normalized === 'LICENSE') return '#license';
  if (DIRECTORY_TARGETS[normalized]) {
    return `/docs/${DIRECTORY_TARGETS[normalized].replace(/^docs\//, '').replace(/\.md$/, '.html')}`;
  }
  if (normalized.endsWith('.md')) return `/docs/${normalized.replace(/^docs\//, '').replace(/\.md$/, '.html')}`;
  return normalized.startsWith('docs/') ? `/${normalized}` : normalized;
}

function inline(text: string, source: string, onSelectDoc: (source: string) => void): ReactNode[] {
  const nodes: ReactNode[] = [];
  const pattern = /(`[^`]+`|\*\*[^*]+\*\*|\[[^\]]+\]\([^)]+\))/g;
  let cursor = 0;
  let match: RegExpExecArray | null;

  while ((match = pattern.exec(text)) !== null) {
    if (match.index > cursor) {
      nodes.push(text.slice(cursor, match.index));
    }

    const token = match[0];
    if (token.startsWith('`')) {
      nodes.push(<code key={`${match.index}-code`}>{token.slice(1, -1)}</code>);
    } else if (token.startsWith('**')) {
      nodes.push(<strong key={`${match.index}-strong`}>{token.slice(2, -2)}</strong>);
    } else {
      const link = token.match(/^\[([^\]]+)\]\(([^)]+)\)$/);
      if (link) {
        const normalizedTarget = normalizeDocPath(source, link[2]);
        const targetSource = DIRECTORY_TARGETS[normalizedTarget] ?? normalizedTarget;
        const article = ARTICLES.find((item) => item.source === targetSource || item.source === normalizedTarget);
        nodes.push(
          <a
            key={`${match.index}-link`}
            href={article ? `#${article.id}` : docFallbackHref(source, link[2])}
            onClick={article ? (event) => {
              event.preventDefault();
              onSelectDoc(article.id);
            } : undefined}
          >
            {link[1]}
          </a>,
        );
      }
    }

    cursor = match.index + token.length;
  }

  if (cursor < text.length) {
    nodes.push(text.slice(cursor));
  }

  return nodes;
}

function MarkdownDocument({
  markdown,
  source,
  onSelectDoc,
}: {
  markdown: string;
  source: string;
  onSelectDoc: (articleId: string) => void;
}) {
  const blocks: ReactNode[] = [];
  const lines = stripComments(markdown).split('\n');
  let i = 0;

  while (i < lines.length) {
    const line = lines[i];

    if (!line.trim()) {
      i += 1;
      continue;
    }

    if (line.startsWith('```')) {
      const lang = line.slice(3).trim();
      const code: string[] = [];
      i += 1;
      while (i < lines.length && !lines[i].startsWith('```')) {
        code.push(lines[i]);
        i += 1;
      }
      i += 1;
      blocks.push(
        <pre key={`code-${i}`} data-lang={lang || undefined}>
          <code>{code.join('\n')}</code>
        </pre>,
      );
      continue;
    }

    const heading = line.match(/^(#{1,4})\s+(.+)$/);
    if (heading) {
      const level = heading[1].length;
      const content = inline(heading[2], source, onSelectDoc);
      const className = `doc-heading doc-heading-${level}`;
      if (level === 1) blocks.push(<h1 key={`h-${i}`} className={className}>{content}</h1>);
      if (level === 2) blocks.push(<h2 key={`h-${i}`} className={className}>{content}</h2>);
      if (level === 3) blocks.push(<h3 key={`h-${i}`} className={className}>{content}</h3>);
      if (level === 4) blocks.push(<h4 key={`h-${i}`} className={className}>{content}</h4>);
      i += 1;
      continue;
    }

    if (line.trim().startsWith('|')) {
      const rows: string[][] = [];
      while (i < lines.length && lines[i].trim().startsWith('|')) {
        const cells = lines[i].trim().replace(/^\||\|$/g, '').split('|').map((cell) => cell.trim());
        if (!cells.every((cell) => /^[-:]+$/.test(cell))) rows.push(cells);
        i += 1;
      }
      const [head, ...body] = rows;
      blocks.push(
        <div key={`table-${i}`} className="doc-table-wrap">
          <table>
            <thead>
              <tr>{head.map((cell, index) => <th key={index}>{inline(cell, source, onSelectDoc)}</th>)}</tr>
            </thead>
            <tbody>
              {body.map((row, rowIndex) => (
                <tr key={rowIndex}>
                  {row.map((cell, cellIndex) => <td key={cellIndex}>{inline(cell, source, onSelectDoc)}</td>)}
                </tr>
              ))}
            </tbody>
          </table>
        </div>,
      );
      continue;
    }

    if (/^[-*]\s+/.test(line)) {
      const items: string[] = [];
      while (i < lines.length && /^[-*]\s+/.test(lines[i])) {
        items.push(lines[i].replace(/^[-*]\s+/, ''));
        i += 1;
      }
      blocks.push(<ul key={`ul-${i}`}>{items.map((item) => <li key={item}>{inline(item, source, onSelectDoc)}</li>)}</ul>);
      continue;
    }

    if (/^\d+\.\s+/.test(line)) {
      const items: string[] = [];
      while (i < lines.length && /^\d+\.\s+/.test(lines[i])) {
        items.push(lines[i].replace(/^\d+\.\s+/, ''));
        i += 1;
      }
      blocks.push(<ol key={`ol-${i}`}>{items.map((item) => <li key={item}>{inline(item, source, onSelectDoc)}</li>)}</ol>);
      continue;
    }

    const paragraph: string[] = [];
    while (
      i < lines.length &&
      lines[i].trim() &&
      !lines[i].startsWith('```') &&
      !/^(#{1,4})\s+/.test(lines[i]) &&
      !lines[i].trim().startsWith('|') &&
      !/^[-*]\s+/.test(lines[i]) &&
      !/^\d+\.\s+/.test(lines[i])
    ) {
      paragraph.push(lines[i]);
      i += 1;
    }
    blocks.push(<p key={`p-${i}`}>{inline(paragraph.join(' '), source, onSelectDoc)}</p>);
  }

  return <div className="doc-markdown">{blocks}</div>;
}

export default function Documentation() {
  const [category, setCategory] = useState<DocCategory | 'All'>('All');
  const [query, setQuery] = useState('');
  const [activeId, setActiveId] = useState(ARTICLES[0].id);
  const pageTopRef = useRef<HTMLDivElement | null>(null);

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    return ARTICLES.filter((article) => {
      const inCategory = category === 'All' || article.category === category;
      const searchable = `${article.title} ${article.category} ${article.source} ${article.body}`.toLowerCase();
      return inCategory && (!q || searchable.includes(q));
    });
  }, [category, query]);

  const activeArticle = ARTICLES.find((article) => article.id === activeId) ?? filtered[0] ?? ARTICLES[0];

  useEffect(() => {
    pageTopRef.current?.scrollIntoView({ block: 'start' });
  }, [activeArticle.id]);

  return (
    <div className="p-4 space-y-4 crt">
      <div ref={pageTopRef} />
      <div className="ascii-box p-4 bg-[#000022]">
        <div className="text-[#ffff55] glow-yellow text-2xl">## CAPTAIN'S LOG</div>
        <div className="text-[#aaaaaa] mt-1">
          Live repo documentation rendered inside the Mutineer console.
        </div>
      </div>

      <div className="grid gap-4 lg:grid-cols-[18rem_minmax(0,1fr)] items-start">
        <aside className="ascii-box bg-[#000011] lg:sticky lg:top-32">
          <div className="p-3 border-b border-[#0000aa]">
            <label className="block text-[#555555] text-sm mb-1" htmlFor="doc-search">-- SEARCH MANUAL --</label>
            <input
              id="doc-search"
              value={query}
              onChange={(event) => setQuery(event.target.value)}
              className="w-full bg-[#000022] border border-[#0000aa] px-2 py-1 text-[#55ffff] outline-none focus:border-[#55ffff] font-[inherit]"
              placeholder="docker, menu, qwk..."
            />
          </div>

          <div className="p-2 border-b border-[#0000aa] flex flex-wrap gap-1">
            {CATEGORIES.map((cat) => (
              <button
                key={cat}
                onClick={() => setCategory(cat)}
                className={`doc-filter px-2 py-1 text-sm border ${
                  category === cat ? 'is-active' : cat === 'All' ? 'text-[#aaaaaa]' : CATEGORY_COLORS[cat]
                }`}
              >
                [{cat}]
              </button>
            ))}
          </div>

          <div className="p-2 text-[#555555] text-sm border-b border-[#0000aa]">
            -- ARTICLES: <span className="text-[#55ffff]">{filtered.length}</span>/{ARTICLES.length} --
          </div>

          <div className="max-h-[32rem] overflow-y-auto">
            {filtered.map((article) => (
              <button
                key={article.id}
                onClick={() => setActiveId(article.id)}
                className={`doc-article-link ${activeArticle.id === article.id ? 'is-active' : ''}`}
              >
                <span className="block">{activeArticle.id === article.id ? '>' : ' '} {article.title}</span>
                <span className="block text-xs opacity-70">{article.source}</span>
              </button>
            ))}
            {filtered.length === 0 && (
              <div className="p-3 text-[#555555]">No docs matched that signal.</div>
            )}
          </div>
        </aside>

        <article className="ascii-box bg-[#000011] min-w-0">
          <div className="px-4 py-3 bg-[#000022] border-b border-[#0000aa] flex flex-wrap justify-between gap-2">
            <div>
              <div className={`${CATEGORY_COLORS[activeArticle.category]} text-xl glow-cyan`}>
                {activeArticle.title}
              </div>
              <div className="text-[#555555] text-sm">{activeArticle.source}</div>
            </div>
            <div className="text-[#aaaaaa] text-sm">[{activeArticle.category.toUpperCase()}]</div>
          </div>
          <div className="p-4">
            <MarkdownDocument
              markdown={activeArticle.body}
              source={activeArticle.source}
              onSelectDoc={setActiveId}
            />
          </div>
        </article>
      </div>
    </div>
  );
}
