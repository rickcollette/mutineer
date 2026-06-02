#!/usr/bin/env python3
"""Build static HTML documentation site from docs/*.md for GitHub Pages."""

import html
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")
OUT = os.path.join(ROOT, "website")
OUT_DOCS = os.path.join(OUT, "docs")

NAV = [
    ("Overview", "overview.html"),
    ("Getting Started", "getting-started.html"),
    ("Configuration", "configuration.html"),
    ("Architecture", "architecture.html"),
    ("Menus & UI", "menus-and-ui.html"),
    ("Messages & Mail", "messages-and-mail.html"),
    ("Files & Protocols", "files-and-protocols.html"),
    ("Chat & Social", "chat-and-social.html"),
    ("Doors & Scripting", "doors-and-scripting.html"),
    ("Sysop Guide", "sysop-guide.html"),
    ("PLANK Networking", "networking-plank.html"),
    ("Buccaneer", "buccaneer.html"),
    ("Plugins", "plugins.html"),
    ("Developer Guide", "developer-guide.html"),
    ("CLI Tools", "cli-tools.html"),
    ("Menu Actions", "reference/menu-actions.html"),
    ("Message Commands", "reference/message-commands.html"),
    ("File Commands", "reference/file-commands.html"),
    ("ACS & MCI", "reference/acs-mci.html"),
    ("Database Schema", "reference/database.html"),
]


def inline(text: str) -> str:
    text = html.escape(text)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", _link, text)
    text = re.sub(r"`([^`]+)`", r"<code>\1</code>", text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    return text


def _link(m):
    label, url = m.group(1), m.group(2)
    if url.endswith(".md"):
        url = url[:-3] + ".html"
    return f'<a href="{url}">{label}</a>'


def md_to_html(text: str) -> str:
    lines = text.split("\n")
    out = []
    i = 0
    in_code = False

    while i < len(lines):
        line = lines[i]

        if line.startswith("```"):
            if not in_code:
                in_code = True
                lang = html.escape(line[3:].strip())
                out.append(f'<pre><code class="{lang}">')
            else:
                in_code = False
                out.append("</code></pre>")
            i += 1
            continue

        if in_code:
            out.append(html.escape(line))
            i += 1
            continue

        if "|" in line and line.strip().startswith("|"):
            if not out or out[-1] != "<table>":
                out.append("<table>")
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if all(re.match(r"^[-:]+$", c) for c in cells):
                i += 1
                continue
            if out[-1] == "<table>":
                out.append("<tr>" + "".join(f"<th>{inline(c)}</th>" for c in cells) + "</tr>")
            else:
                out.append("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in cells) + "</tr>")
            i += 1
            if i >= len(lines) or "|" not in lines[i]:
                out.append("</table>")
            continue

        m = re.match(r"^(#{1,6})\s+(.*)$", line)
        if m:
            lvl = len(m.group(1))
            out.append(f"<h{lvl}>{inline(m.group(2))}</h{lvl}>")
            i += 1
            continue

        if re.match(r"^-{3,}$", line.strip()):
            out.append("<hr>")
            i += 1
            continue

        if re.match(r"^[-*]\s+", line):
            out.append("<ul>")
            while i < len(lines) and re.match(r"^[-*]\s+", lines[i]):
                out.append(f"<li>{inline(lines[i][2:].strip())}</li>")
                i += 1
            out.append("</ul>")
            continue

        if re.match(r"^\d+\.\s+", line):
            out.append("<ol>")
            while i < len(lines) and re.match(r"^\d+\.\s+", lines[i]):
                content = re.sub(r"^\d+\.\s+", "", lines[i])
                out.append(f"<li>{inline(content.strip())}</li>")
                i += 1
            out.append("</ol>")
            continue

        if not line.strip():
            i += 1
            continue

        out.append(f"<p>{inline(line)}</p>")
        i += 1

    if in_code:
        out.append("</code></pre>")
    return "\n".join(out)


def build_nav(current: str, depth: int) -> str:
    prefix = "../" if depth else ""
    home = ("../" if depth else "") + "index.html"
    items = []
    for label, href in NAV:
        full = prefix + href
        cls = ' class="active"' if os.path.basename(current) == href else ""
        items.append(f'<a href="{full}"{cls}>{label}</a>')
    return f'<nav class="doc-nav"><a href="{home}" class="home">← Home</a>{"".join(items)}</nav>'


def wrap(title: str, body: str, current: str, depth: int) -> str:
    css = ("../" if depth else "") + "../assets/css/site.css" if depth else "assets/css/site.css"
    if depth:
        css = "../assets/css/site.css"
    home = ("../" if depth else "") + "index.html"
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{html.escape(title)} — Mutineer BBS</title>
  <link rel="stylesheet" href="{css}">
</head>
<body>
  <header class="site-header">
    <a href="{home}" class="logo">MUTINEER BBS</a>
    <span class="tagline">Documentation</span>
  </header>
  {build_nav(current, depth)}
  <main class="doc-content">{body}</main>
  <footer class="site-footer">
    <p>Mutineer BBS — <a href="https://github.com/rickcollette/mutineer">GitHub</a> — MIT License</p>
  </footer>
</body>
</html>"""


def convert(src_rel: str, dst_rel: str, depth: int):
    src = os.path.join(DOCS, src_rel)
    if not os.path.exists(src):
        print(f"  SKIP: {src_rel}")
        return
    with open(src, encoding="utf-8") as f:
        md = f.read()
    title_m = re.search(r"^#\s+(.+)$", md, re.MULTILINE)
    title = title_m.group(1) if title_m else src_rel
    body = md_to_html(md)
    dst = os.path.join(OUT_DOCS, dst_rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w", encoding="utf-8") as f:
        f.write(wrap(title, body, dst_rel, depth))
    print(f"  {src_rel} → website/docs/{dst_rel}")


def main():
    os.makedirs(OUT_DOCS, exist_ok=True)
    pages = [
        ("index.md", "index.html", 0),
        ("overview.md", "overview.html", 0),
        ("getting-started.md", "getting-started.html", 0),
        ("configuration.md", "configuration.html", 0),
        ("architecture.md", "architecture.html", 0),
        ("menus-and-ui.md", "menus-and-ui.html", 0),
        ("messages-and-mail.md", "messages-and-mail.html", 0),
        ("files-and-protocols.md", "files-and-protocols.html", 0),
        ("chat-and-social.md", "chat-and-social.html", 0),
        ("doors-and-scripting.md", "doors-and-scripting.html", 0),
        ("sysop-guide.md", "sysop-guide.html", 0),
        ("networking-plank.md", "networking-plank.html", 0),
        ("buccaneer.md", "buccaneer.html", 0),
        ("plugins.md", "plugins.html", 0),
        ("developer-guide.md", "developer-guide.html", 0),
        ("cli-tools.md", "cli-tools.html", 0),
        ("reference/menu-actions.md", "reference/menu-actions.html", 1),
        ("reference/message-commands.md", "reference/message-commands.html", 1),
        ("reference/file-commands.md", "reference/file-commands.html", 1),
        ("reference/acs-mci.md", "reference/acs-mci.html", 1),
        ("reference/database.md", "reference/database.html", 1),
    ]
    print("Building documentation HTML...")
    for src, dst, depth in pages:
        convert(src, dst, depth)
    print("Done.")


if __name__ == "__main__":
    main()
