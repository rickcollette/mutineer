#!/usr/bin/env python3
"""Convert raw telnet ANSI captures to HTML and PNG-ready gallery."""

import html
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, "screenshots", "raw")
OUT = os.path.join(ROOT, "screenshots")
GALLERY = os.path.join(OUT, "gallery.html")

# Basic ANSI SGR → HTML (green terminal theme)
SGR = {
    "0": "</span>",
    "1": '<span style="font-weight:bold">',
    "30": '<span style="color:#000">',
    "31": '<span style="color:#f44">',
    "32": '<span style="color:#0c6">',
    "33": '<span style="color:#fc0">',
    "34": '<span style="color:#48f">',
    "35": '<span style="color:#f4f">',
    "36": '<span style="color:#0cc">',
    "37": '<span style="color:#ccc">',
    "90": '<span style="color:#666">',
    "92": '<span style="color:#0f6">',
    "93": '<span style="color:#ff0">',
    "97": '<span style="color:#fff">',
}


def ansi_to_html(text: str) -> str:
    text = text.replace("\r", "")
    out = []
    i = 0
    open_spans = 0
    while i < len(text):
        if text[i] == "\x1b" and i + 1 < len(text) and text[i + 1] == "[":
            j = text.find("m", i)
            if j == -1:
                break
            codes = text[i + 2 : j].split(";")
            for code in codes:
                if code == "":
                    code = "0"
                if code == "0":
                    out.append("</span>" * open_spans)
                    open_spans = 0
                elif code in SGR:
                    out.append(SGR[code])
                    if code != "0":
                        open_spans += 1
            i = j + 1
            continue
        ch = text[i]
        if ch in "\x08\x7f":
            i += 1
            continue
        if ord(ch) < 32 and ch not in "\n\t":
            i += 1
            continue
        out.append(html.escape(ch))
        i += 1
    out.append("</span>" * open_spans)
    return "".join(out)


def strip_telnet_noise(text: str) -> str:
    # Remove telnet negotiation and bare IAC sequences
    text = re.sub(r"\xff[\xfb-\xfe].", "", text)
    text = re.sub(r"(?m)^spawn telnet.*\n", "", text)
    text = re.sub(r"(?m)^Trying .*?\n", "", text)
    text = re.sub(r"(?m)^Connected to .*?\n", "", text)
    text = re.sub(r"(?m)^Escape character.*?\n", "", text)
    text = re.sub(r"(?m)^Script started.*\n", "", text)
    text = re.sub(r"(?m)^Script done.*\n", "", text)
    text = re.sub(r"(?m)^==> .*?\n", "", text)
    return text


def convert_file(path: str) -> str:
    with open(path, encoding="utf-8", errors="replace") as f:
        raw = f.read()
    body = ansi_to_html(strip_telnet_noise(raw))
    name = os.path.basename(path).replace(".ansi", "")
    out_path = os.path.join(OUT, f"{name}.html")
    page = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>{name}</title>
<style>
body {{ background:#0a0f0a; margin:0; padding:24px; }}
pre {{
  background:#000; color:#0c6; font:14px/1.35 'Courier New', monospace;
  padding:16px 20px; border:2px solid #094; border-radius:4px;
  max-width:900px; white-space:pre-wrap; word-break:break-word;
  box-shadow:0 0 24px rgba(0,204,102,0.15);
}}
h1 {{ color:#0c6; font:18px monospace; margin-bottom:12px; }}
</style></head><body>
<h1>{name}</h1>
<pre>{body}</pre>
</body></html>"""
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(page)
    return out_path


def try_png(html_path: str) -> str | None:
    """Use headless Chrome/Chromium to screenshot rendered HTML."""
    png_path = html_path.replace(".html", ".png")
    chrome = None
    for c in (
        "google-chrome", "chromium", "chromium-browser",
        "/snap/bin/chromium", "/usr/bin/chromium-browser",
    ):
        if os.path.isfile(c) or subprocess.call(
            ["which", c], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        ) == 0:
            chrome = c
            break
    if not chrome:
        return None
    with open(html_path, encoding="utf-8") as f:
        lines = f.read().count("\n")
    height = min(1400, max(520, 80 + lines * 16))
    url = "file://" + os.path.abspath(html_path)
    subprocess.run(
        [
            chrome,
            "--headless=new",
            "--disable-gpu",
            "--no-sandbox",
            f"--window-size=920,{height}",
            f"--screenshot={png_path}",
            url,
        ],
        check=False,
        capture_output=True,
    )
    if os.path.isfile(png_path) and os.path.getsize(png_path) > 1000:
        return png_path
    return None


def main():
    os.makedirs(RAW, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    files = sorted(f for f in os.listdir(RAW) if f.endswith(".ansi"))
    if not files:
        print("No .ansi files in screenshots/raw/", file=sys.stderr)
        sys.exit(1)

    pages = []
    pngs = []
    for fname in files:
        html_path = convert_file(os.path.join(RAW, fname))
        pages.append((fname, html_path))
        png = try_png(html_path)
        if png:
            pngs.append(png)
            print(f"PNG  {png}")
        print(f"HTML {html_path}")

    items = ""
    for fname, html_path in pages:
        base = os.path.basename(html_path)
        png = html_path.replace(".html", ".png")
        if os.path.isfile(png):
            items += f'<div class="card"><h2>{base}</h2><img src="{os.path.basename(png)}" alt="{base}"></div>\n'
        else:
            items += f'<div class="card"><h2>{base}</h2><iframe src="{base}" width="920" height="520"></iframe></div>\n'

    gallery = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Mutineer BBS Screenshots</title>
<style>
body {{ background:#0a0f0a; color:#ccc; font-family:monospace; padding:24px; }}
h1 {{ color:#0c6; }}
.grid {{ display:grid; gap:24px; max-width:980px; }}
.card {{ background:#111; border:1px solid #094; padding:16px; border-radius:6px; }}
.card h2 {{ color:#fc0; font-size:14px; margin:0 0 12px; }}
img {{ max-width:100%; border:1px solid #094; }}
iframe {{ border:1px solid #094; background:#000; }}
</style></head><body>
<h1>Mutineer BBS — Docker Session Screenshots</h1>
<div class="grid">
{items}
</div>
</body></html>"""
    with open(GALLERY, "w", encoding="utf-8") as f:
        f.write(gallery)
    print(f"Gallery {GALLERY}")


if __name__ == "__main__":
    main()
