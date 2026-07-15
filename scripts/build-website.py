#!/usr/bin/env python3
"""Build static HTML documentation site from docs/*.md for GitHub Pages."""

import html
import os
import re
import argparse
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")
OUT = os.path.join(ROOT, "website")
OUT_DOCS = os.path.join(OUT, "docs")

NAV = [
    ("Quick Start", "quick-start.html"),
    ("Windows", "windows.html"),
    ("Getting Started", "getting-started.html"),
    ("Deployment", "deployment.html"),
    ("Overview", "overview.html"),
    ("Configuration", "configuration.html"),
    ("Architecture", "architecture.html"),
    ("Menus & UI", "menus-and-ui.html"),
    ("Messages & Mail", "messages-and-mail.html"),
    ("Files & Protocols", "files-and-protocols.html"),
    ("Chat & Social", "chat-and-social.html"),
    ("Doors & Scripting", "doors-and-scripting.html"),
    ("Sysop Guide", "sysop-guide.html"),
    ("Console Protocol", "console-protocol.html"),
    ("PLANK", "networking-plank.html"),
    ("BUCC Guide", "buccaneer/programmers-guide.html"),
    ("BUCC API", "buccaneer/host-api.html"),
    ("Plugins", "plugins.html"),
    ("Developer", "developer-guide.html"),
    ("Website Source", "website-source.html"),
    ("CLI Tools", "cli-tools.html"),
    ("Menu Actions", "reference/menu-actions.html"),
    ("Messages Ref", "reference/message-commands.html"),
    ("Files Ref", "reference/file-commands.html"),
    ("ACS & MCI", "reference/acs-mci.html"),
    ("Database", "reference/database.html"),
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
    in_table = False
    table_header_done = False

    while i < len(lines):
        line = lines[i]

        stripped_line = line.lstrip()
        if stripped_line.startswith("```"):
            if not in_code:
                in_code = True
                lang = html.escape(stripped_line[3:].strip())
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

        if re.match(r"^\s*<!--.*-->\s*$", line):
            i += 1
            continue

        if "|" in line and line.strip().startswith("|"):
            if not in_table:
                out.append("<table>")
                in_table = True
                table_header_done = False
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if all(re.match(r"^[-:]+$", c) for c in cells):
                i += 1
                continue
            if not table_header_done:
                out.append("<tr>" + "".join(f"<th>{inline(c)}</th>" for c in cells) + "</tr>")
                table_header_done = True
            else:
                out.append("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in cells) + "</tr>")
            i += 1
            if i >= len(lines) or "|" not in lines[i] or not lines[i].strip().startswith("|"):
                out.append("</table>")
                in_table = False
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
    prefix = "../" * depth
    home = prefix + "index.html"
    items = []
    for label, href in NAV:
        full = prefix + href
        cls = ' class="active"' if os.path.basename(current) == href else ""
        items.append(f'<a href="{full}"{cls}>{label}</a>')
    return f'<nav class="doc-nav"><a href="{home}" class="home">← Home</a>{"".join(items)}</nav>'


def wrap(title: str, body: str, current: str, depth: int) -> str:
    prefix = "../" * depth
    css = prefix + "../assets/css/site.css"
    home = prefix + "index.html"
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
    <a href="{home}" class="logo"><span class="mark">☠</span> MUTINEER BBS</a>
    <span class="tagline">CAPTAIN'S MANUAL // STATIC ARCHIVE</span>
  </header>
  {build_nav(current, depth)}
  <main class="doc-content">{body}</main>
  <footer class="site-footer">
    <p>Mutineer BBS — <a href="https://github.com/rickcollette/mutineer">GitHub</a> — MIT License</p>
  </footer>
</body>
</html>"""


def convert(src_rel: str, dst_rel: str, depth: int, out_docs: str):
    src = os.path.join(DOCS, src_rel)
    if not os.path.exists(src):
        print(f"  SKIP: {src_rel}")
        return
    with open(src, encoding="utf-8") as f:
        md = f.read()
    title_m = re.search(r"^#\s+(.+)$", md, re.MULTILINE)
    title = title_m.group(1) if title_m else src_rel
    body = md_to_html(md)
    dst = os.path.join(out_docs, dst_rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w", encoding="utf-8") as f:
        f.write(wrap(title, body, dst_rel, depth))
    print(f"  {src_rel} → {os.path.relpath(dst, ROOT)}")


PAGES = [
        ("index.md", "index.html", 0),
        ("quick-start.md", "quick-start.html", 0),
        ("windows.md", "windows.html", 0),
        ("getting-started.md", "getting-started.html", 0),
        ("deployment.md", "deployment.html", 0),
        ("overview.md", "overview.html", 0),
        ("configuration.md", "configuration.html", 0),
        ("architecture.md", "architecture.html", 0),
        ("menus-and-ui.md", "menus-and-ui.html", 0),
        ("messages-and-mail.md", "messages-and-mail.html", 0),
        ("files-and-protocols.md", "files-and-protocols.html", 0),
        ("chat-and-social.md", "chat-and-social.html", 0),
        ("doors-and-scripting.md", "doors-and-scripting.html", 0),
        ("sysop-guide.md", "sysop-guide.html", 0),
        ("console-protocol.md", "console-protocol.html", 0),
        ("networking-plank.md", "networking-plank.html", 0),
        ("screenshots.md", "screenshots.html", 0),
        ("buccaneer.md", "buccaneer.html", 0),
        ("buccaneer/index.md", "buccaneer/index.html", 1),
        ("buccaneer/programmers-guide.md", "buccaneer/programmers-guide.html", 1),
        ("buccaneer/host-api.md", "buccaneer/host-api.html", 1),
        ("buccaneer/toolchain.md", "buccaneer/toolchain.html", 1),
        ("buccaneer/door-packages.md", "buccaneer/door-packages.html", 1),
        ("plugins.md", "plugins.html", 0),
        ("developer-guide.md", "developer-guide.html", 0),
        ("website-source.md", "website-source.html", 0),
        ("cli-tools.md", "cli-tools.html", 0),
        ("reference/menu-actions.md", "reference/menu-actions.html", 1),
        ("reference/message-commands.md", "reference/message-commands.html", 1),
        ("reference/file-commands.md", "reference/file-commands.html", 1),
        ("reference/acs-mci.md", "reference/acs-mci.html", 1),
        ("reference/database.md", "reference/database.html", 1),
]

STALE_PHRASES = [
    "Buccaneer VM",
    "Buccaneer scripting VM",
    "embedded scripting VM",
    "VM audit",
    "DOOR.CHAIN sets VM_HALT",
    "DOOR.EXIT discards exit code",
    "SHARED.CAS uses shallow equality",
]


def build(out_dir: str):
    out_docs = os.path.join(out_dir, "docs")
    os.makedirs(out_docs, exist_ok=True)
    print("Building documentation HTML...")
    for src, dst, depth in PAGES:
        convert(src, dst, depth, out_docs)
    print("Done.")


def check_output(out_dir: str) -> int:
    failures = []
    for dirpath, _, filenames in os.walk(out_dir):
        for name in filenames:
            if not name.endswith(".html"):
                continue
            path = os.path.join(dirpath, name)
            with open(path, encoding="utf-8") as f:
                data = f.read()
            if data.count("<table>") != data.count("</table>"):
                failures.append(f"{path}: unbalanced table tags")
            depth = 0
            for tag in re.findall(r"</?table>", data):
                if tag == "<table>":
                    depth += 1
                    if depth > 1:
                        failures.append(f"{path}: nested table output")
                        break
                else:
                    depth = max(0, depth - 1)
            for phrase in STALE_PHRASES:
                if phrase in data:
                    failures.append(f"{path}: stale phrase: {phrase}")
    if failures:
        print("Website generation check failed:")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print("Website generation check passed.")
    return 0


def self_test() -> int:
    fixture = """# Fixture

1. Build it
   ```sh
   echo "<safe>"
   ```
2. Check `inline`

| Name | Value |
| ---- | ----- |
| Link | [Docs](quick-start.md) |
"""
    rendered = md_to_html(fixture)
    checks = [
        ("<h1>Fixture</h1>" in rendered, "heading renders"),
        ("<ol>" in rendered and "<li>Build it</li>" in rendered, "ordered list renders"),
        ('<pre><code class="sh">' in rendered, "indented fence starts code block"),
        ("&lt;safe&gt;" in rendered, "code block is escaped"),
        ("<code>inline</code>" in rendered, "inline code renders"),
        ("<table>" in rendered and "<th>Name</th>" in rendered and "<td>Link</td>" in rendered,
         "table renders"),
        ('<a href="quick-start.html">Docs</a>' in rendered, "markdown links convert to html"),
    ]
    failures = [name for ok, name in checks if not ok]
    if failures:
        print("Website renderer self-test failed:")
        print(rendered)
        for name in failures:
            print(f"  {name}")
        return 1
    print("Website renderer self-test passed.")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=OUT, help="output website directory")
    parser.add_argument("--check-or-temp-output", action="store_true",
                        help="build into a temporary directory and validate generated HTML")
    parser.add_argument("--self-test", action="store_true",
                        help="run Markdown renderer fixture tests")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    if args.check_or_temp_output:
        tmp = tempfile.mkdtemp(prefix="mutineer_website_")
        try:
            build(tmp)
            return check_output(tmp)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    build(os.path.abspath(args.output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
