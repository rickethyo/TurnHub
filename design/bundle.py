#!/usr/bin/env python3
"""Bundles a TurnHub web page into one file (standard library only).

Markers in the source HTML:

  <!-- build:inline --> ... <!-- /build:inline -->
      every <link rel="stylesheet" href="..."> between them is replaced by an
      inline <style>; url(...) references inside are resolved against the CSS
      file and, with --embed-fonts, embedded as data: URIs.
  <!-- build:sprite -->
      replaced by design/dist/icons.svg (the icon <symbol>s).

  python3 design/bundle.py design/styleguide.html --out design/dist/styleguide.html --embed-fonts

The Atlas portal build (Atlas/web/build.py) imports bundle_html() and keeps the
fonts as separate files instead of embedding them.
"""

from __future__ import annotations

import argparse
import base64
import re
from pathlib import Path

DESIGN = Path(__file__).resolve().parent
SPRITE = DESIGN / "dist" / "icons.svg"

LINK = re.compile(r'<link\s+rel="stylesheet"\s+href="([^"]+)"\s*/?>')
URL = re.compile(r'url\("?([^")]+)"?\)')
MIME = {".woff2": "font/woff2", ".svg": "image/svg+xml", ".png": "image/png"}


def inline_css(css_path: Path, embed_fonts: bool, font_url: str | None) -> str:
    css = css_path.read_text(encoding="utf-8")

    def fix(m):
        ref = m.group(1)
        if ref.startswith("data:") or ref.startswith("#"):
            return m.group(0)
        target = (css_path.parent / ref).resolve()
        if embed_fonts:
            data = base64.b64encode(target.read_bytes()).decode("ascii")
            return f'url("data:{MIME.get(target.suffix, "application/octet-stream")};base64,{data}")'
        if font_url is not None:
            return f'url("{font_url}{target.name}")'
        return m.group(0)

    return URL.sub(fix, css)


def sprite_markup() -> str:
    text = SPRITE.read_text(encoding="utf-8")
    text = re.sub(r"^<!--.*?-->\s*", "", text, flags=re.S)
    return text.replace('style="display:none"', 'width="0" height="0" style="position:absolute" aria-hidden="true" focusable="false"')


def bundle_html(src: Path, embed_fonts: bool = False, font_url: str | None = None) -> str:
    html = src.read_text(encoding="utf-8")

    def inline_block(m):
        block = m.group(1)
        styles = []
        for href in LINK.findall(block):
            styles.append(inline_css((src.parent / href).resolve(), embed_fonts, font_url))
        return "<style>\n" + "\n".join(styles) + "</style>"

    html = re.sub(r"<!-- build:inline -->(.*?)<!-- /build:inline -->", inline_block, html, flags=re.S)
    html = html.replace("<!-- build:sprite -->", sprite_markup())
    return html


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("src", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--embed-fonts", action="store_true")
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(bundle_html(args.src, embed_fonts=args.embed_fonts), encoding="utf-8", newline="\n")
    print(f"Wrote {args.out} ({args.out.stat().st_size // 1024} KB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
