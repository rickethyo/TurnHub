#!/usr/bin/env python3
"""Build Atlas's embedded admin portal (standard library only).

  python3 Atlas/web/build.py              build dist/site/ for browser checks/preview
  python3 Atlas/web/build.py --check      validate assets in memory (CI)
  python3 Atlas/web/build.py --header X   generate compressed PROGMEM assets for firmware
  python3 Atlas/web/build.py --serve      preview with APIs proxied to a real Atlas

Source markers in src/*.html (besides design/bundle.py's build:inline and
build:sprite):

  <!-- build:css NAME.css --> <link rel="stylesheet" href="..."> ... <!-- /build:css -->
      the stylesheets are joined into one file, published as
      /assets/NAME.<hash>.css; url(...) references in them (fonts) become
      hashed assets too.
  href="./FILE" or src="./FILE"
      the file next to the page is published as /assets/FILE-stem.<hash>.ext.
      In a referenced .webmanifest, "src": "./FILE" entries (icons) are
      published the same way first.

Hashed names never change for the same content, so Atlas serves /assets/ as
immutable. index.html itself is served no-cache. Text files are stored gzip
(byte-for-byte reproducible) when that is smaller.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import http.client
import http.server
import re
import sys
from pathlib import Path

WEB = Path(__file__).resolve().parent
ROOT = WEB.parent.parent
SRC = WEB / "src"
DIST = WEB / "dist"
sys.path.insert(0, str(ROOT / "design"))

import bundle  # noqa: E402  (design/bundle.py)

GZIP_TYPES = {".html", ".css", ".js", ".json", ".svg", ".txt", ".webmanifest"}
SAFE_PATH = re.compile(r"^[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)*$")
# Files copied into the site as they are (path in site: source).
STATIC = {
    "assets/licenses/OFL-Inter.txt": ROOT / "design/fonts/OFL-Inter.txt",
    "assets/licenses/OFL-Cinzel.txt": ROOT / "design/fonts/OFL-Cinzel.txt",
}

CSS_BLOCK = re.compile(r"<!-- build:css ([A-Za-z0-9_-]+)\.css -->(.*?)<!-- /build:css -->", re.S)
LOCAL_REF = re.compile(r'(href|src)="\./([^"]+)"')
MANIFEST_REF = re.compile(r'"src":\s*"\./([^"]+)"')


class BuildError(Exception):
    pass


class Site:
    def __init__(self):
        self.files: dict[str, bytes] = {}

    def add(self, path: str, data: bytes) -> str:
        if not SAFE_PATH.match(path) or "/./" in f"/{path}/" or "/../" in f"/{path}/":
            raise BuildError(f"unsafe site path {path!r}")
        if path in self.files and self.files[path] != data:
            raise BuildError(f"two different files for {path}")
        self.files[path] = data
        return path

    def asset(self, name: str, data: bytes) -> str:
        """Adds a content-hashed asset; returns its URL."""
        stem, dot, ext = name.rpartition(".")
        if not dot:
            stem, ext = name, "bin"
        digest = hashlib.sha256(data).hexdigest()[:10]
        return "/" + self.add(f"assets/{Path(stem).name}.{digest}.{ext}", data)


def css_with_assets(site: Site, css_path: Path) -> str:
    css = css_path.read_text(encoding="utf-8")

    def fix(m):
        ref = m.group(1)
        if ref.startswith(("data:", "#", "/", "http")):
            return m.group(0)
        target = (css_path.parent / ref).resolve()
        return f'url("{site.asset(target.name, target.read_bytes())}")'

    return bundle.URL.sub(fix, css)


def build_page(site: Site, src: Path) -> str:
    html = src.read_text(encoding="utf-8")

    def css_block(m):
        parts = [css_with_assets(site, (src.parent / href).resolve()) for href in bundle.LINK.findall(m.group(2))]
        url = site.asset(f"{m.group(1)}.css", "\n".join(parts).encode("utf-8"))
        return f'<link rel="stylesheet" href="{url}">'

    html = CSS_BLOCK.sub(css_block, html)

    def local(m):
        target = (src.parent / m.group(2)).resolve()
        data = target.read_bytes()
        if target.suffix == ".webmanifest":
            data = MANIFEST_REF.sub(lambda r: f'"src": "{site.asset(Path(r.group(1)).name, (target.parent / r.group(1)).resolve().read_bytes())}"',
                                    data.decode("utf-8")).encode("utf-8")
        return f'{m.group(1)}="{site.asset(target.name, data)}"'

    html = LOCAL_REF.sub(local, html)
    # design/bundle.py's markers, with fonts as hashed assets instead of data: URIs.
    html = re.sub(r"<!-- build:inline -->(.*?)<!-- /build:inline -->",
                  lambda m: "<style>\n" + "\n".join(css_with_assets(site, (src.parent / h).resolve())
                                                    for h in bundle.LINK.findall(m.group(1))) + "</style>",
                  html, flags=re.S)
    return html.replace("<!-- build:sprite -->", bundle.sprite_markup())


def build() -> Site:
    site = Site()
    for page in sorted(SRC.glob("*.html")):
        html = build_page(site, page)
        # Atlas serves only /assets/ and the page itself: a relative reference
        # left over would load nothing on the table.
        leftover = re.findall(r'(?:href|src)="(\.[^"]*)"', html)
        if leftover:
            raise BuildError(f"{page.name}: unresolved local references {leftover}")
        site.add(page.name, html.encode("utf-8"))
    required = {"index.html", "login.html", "update.html", "sigil-update.html", "dev.html"}
    missing = required - site.files.keys()
    if missing:
        raise BuildError(f"missing administration pages: {sorted(missing)}")
    for path, source in STATIC.items():
        site.add(path, source.read_bytes())
    return site


CONTENT_TYPES = {
    ".html": "text/html", ".css": "text/css", ".js": "application/javascript",
    ".json": "application/json", ".svg": "image/svg+xml", ".txt": "text/plain",
    ".webmanifest": "application/manifest+json", ".png": "image/png", ".woff2": "font/woff2",
}


def encoded_files(site: Site):
    for path, raw in sorted(site.files.items()):
        data = gzip.compress(raw, compresslevel=9, mtime=0) if Path(path).suffix in GZIP_TYPES else raw
        compressed = len(data) < len(raw)
        yield path, data if compressed else raw, compressed


def firmware_header(site: Site) -> str:
    lines = ['// Generated by Atlas/web/build.py; do not edit.', '#pragma once',
             '#include <Arduino.h>', 'namespace TurnHubWeb {',
             'struct Asset { const char *path; const char *type; const uint8_t *data; size_t size; bool gzip; };']
    entries = []
    for i, (path, data, compressed) in enumerate(encoded_files(site)):
        lines.append(f'const uint8_t asset_{i}[] PROGMEM = {{')
        lines.extend('  ' + ','.join(str(b) for b in data[n:n+24]) + ',' for n in range(0, len(data), 24))
        lines.append('};')
        mime = CONTENT_TYPES.get(Path(path).suffix, 'application/octet-stream')
        entries.append(f'  {{"{path}", "{mime}", asset_{i}, sizeof(asset_{i}), {str(compressed).lower()}}},')
    lines.extend(['const Asset ASSETS[] PROGMEM = {', *entries, '};', '} // namespace TurnHubWeb', ''])
    return '\n'.join(lines)


def write_header(site: Site, target: Path):
    target.parent.mkdir(parents=True, exist_ok=True)
    text = firmware_header(site)
    if not target.exists() or target.read_text(encoding="utf-8") != text:
        target.write_text(text, encoding="utf-8")


def write_site(bundle: Site) -> Path:
    target = DIST / "site"
    if target.exists():
        for f in sorted(target.rglob("*"), reverse=True):
            f.unlink() if f.is_file() else f.rmdir()
    for path, data in bundle.files.items():
        dest = target / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    return target


def serve(atlas: str, port: int) -> None:
    site = DIST / "site"

    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=str(site), **kw)

        def local_path(self):
            path = self.path.split("?", 1)[0]
            if path in ("/", "/portal", "/tablet", "/stats"):
                return "/index.html"
            if path in ("/login", "/update", "/sigil-update", "/dev"):
                return path + ".html"
            if path.startswith("/assets/"):
                return path
            return None

        def do_GET(self):
            local = self.local_path()
            if local is None:
                return self.proxy()
            self.path = local
            return super().do_GET()

        def do_POST(self):
            self.proxy()

        def proxy(self):
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length else None
            headers = {k: v for k, v in self.headers.items() if k.lower() not in ("host", "connection")}
            try:
                conn = http.client.HTTPConnection(atlas, 80, timeout=10)
                conn.request(self.command, self.path, body=body, headers=headers)
                resp = conn.getresponse()
                data = resp.read()
            except OSError as e:
                self.send_error(502, f"Atlas at {atlas} did not answer: {e}")
                return
            self.send_response(resp.status)
            for k, v in resp.getheaders():
                if k.lower() not in ("transfer-encoding", "connection", "content-length"):
                    self.send_header(k, v)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"Portal preview on http://127.0.0.1:{port}/ (API from Atlas at {atlas}); Ctrl+C stops")
    server.serve_forever()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--header", type=Path, help="write firmware C++ assets to this header")
    parser.add_argument("--check", action="store_true", help="build in memory and check; write nothing")
    parser.add_argument("--serve", action="store_true", help="build, then run the local preview server")
    parser.add_argument("--atlas", default="192.168.4.1", help="Atlas address for --serve")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args(argv)
    try:
        site = build()
    except (BuildError, OSError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1
    total = sum(len(d) for d in site.files.values())
    stored = sum(len(d) for _, d, _ in encoded_files(site))
    print(f"Embedded admin portal: {len(site.files)} files, {total} bytes raw, {stored} bytes stored")
    if args.check:
        firmware_header(site)
        return 0
    if args.header:
        write_header(site, args.header)
    else:
        write_site(site)
        print(f"Wrote {DIST / 'site'}")
    if args.serve:
        serve(args.atlas, args.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
