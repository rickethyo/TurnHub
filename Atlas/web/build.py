#!/usr/bin/env python3
"""Builds the Atlas web portal pack (standard library only).

The pack is the portal Atlas serves from its microSD card at /portal. Design
and rules: Documentation/engineering/PORTAL_PACK.md; the archive layout is
Atlas/include/portal_pack.h, which this script must match.

  python3 Atlas/web/build.py                      build dist/site/ and dist/portal-<ver>.bin
  python3 Atlas/web/build.py --key KEY.pem        also sign it: dist/portal-<ver>.thfw
  python3 Atlas/web/build.py --check              build in memory and run the checks only (CI)
  python3 Atlas/web/build.py --serve [--atlas IP] local preview: serves dist/site/ and passes
                                                  everything else to a real Atlas (default 192.168.4.1)

Source markers in src/*.html (besides design/bundle.py's build:inline and
build:sprite):

  <!-- build:css NAME.css --> <link rel="stylesheet" href="..."> ... <!-- /build:css -->
      the stylesheets are joined into one file, published as
      /assets/NAME.<hash>.css; url(...) references in them (fonts) become
      hashed assets too.
  href="./FILE" or src="./FILE"
      the file next to the page is published as /assets/FILE-stem.<hash>.ext.

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
import struct
import sys
from pathlib import Path

WEB = Path(__file__).resolve().parent
ROOT = WEB.parent.parent
SRC = WEB / "src"
DIST = WEB / "dist"
sys.path.insert(0, str(ROOT / "design"))
sys.path.insert(0, str(ROOT / "tools" / "firmware"))

import bundle  # noqa: E402  (design/bundle.py)

DESCRIPTOR_MAGIC = b"THFWDSC1"
ARCHIVE_MAGIC = b"THWEBAR1"
PRODUCT_PORTAL = 4
MAX_PATH = 96
MAX_FILES = 512
MAX_PACK_BYTES = 16 * 1024 * 1024
FLAG_GZIP = 0x01
GZIP_TYPES = {".html", ".css", ".js", ".json", ".svg", ".txt", ".webmanifest"}
SAFE_PATH = re.compile(r"^[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)*$")
# Files copied into the pack as they are (path in pack: source).
STATIC = {
    "assets/licenses/OFL-Inter.txt": ROOT / "design/fonts/OFL-Inter.txt",
    "assets/licenses/OFL-Cinzel.txt": ROOT / "design/fonts/OFL-Cinzel.txt",
}

CSS_BLOCK = re.compile(r"<!-- build:css ([A-Za-z0-9_-]+)\.css -->(.*?)<!-- /build:css -->", re.S)
LOCAL_REF = re.compile(r'(href|src)="\./([^"]+)"')


class BuildError(Exception):
    pass


def read_version() -> tuple[int, int, int]:
    text = (WEB / "VERSION").read_text(encoding="ascii").strip()
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", text)
    if not m or any(int(p) > 255 for p in m.groups()):
        raise BuildError(f"Atlas/web/VERSION must be major.minor.patch (0..255), not {text!r}")
    return tuple(int(p) for p in m.groups())


class Pack:
    def __init__(self):
        self.files: dict[str, bytes] = {}

    def add(self, path: str, data: bytes) -> str:
        if not SAFE_PATH.match(path) or len(path) > MAX_PATH or path == "VERSION" or "/./" in f"/{path}/" or "/../" in f"/{path}/":
            raise BuildError(f"unsafe pack path {path!r}")
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


def css_with_assets(pack: Pack, css_path: Path) -> str:
    css = css_path.read_text(encoding="utf-8")

    def fix(m):
        ref = m.group(1)
        if ref.startswith(("data:", "#", "/", "http")):
            return m.group(0)
        target = (css_path.parent / ref).resolve()
        return f'url("{pack.asset(target.name, target.read_bytes())}")'

    return bundle.URL.sub(fix, css)


def build_page(pack: Pack, src: Path) -> str:
    html = src.read_text(encoding="utf-8")

    def css_block(m):
        parts = [css_with_assets(pack, (src.parent / href).resolve()) for href in bundle.LINK.findall(m.group(2))]
        url = pack.asset(f"{m.group(1)}.css", "\n".join(parts).encode("utf-8"))
        return f'<link rel="stylesheet" href="{url}">'

    html = CSS_BLOCK.sub(css_block, html)

    def local(m):
        target = (src.parent / m.group(2)).resolve()
        return f'{m.group(1)}="{pack.asset(target.name, target.read_bytes())}"'

    html = LOCAL_REF.sub(local, html)
    # design/bundle.py's markers, with fonts as hashed assets instead of data: URIs.
    html = re.sub(r"<!-- build:inline -->(.*?)<!-- /build:inline -->",
                  lambda m: "<style>\n" + "\n".join(css_with_assets(pack, (src.parent / h).resolve())
                                                    for h in bundle.LINK.findall(m.group(1))) + "</style>",
                  html, flags=re.S)
    return html.replace("<!-- build:sprite -->", bundle.sprite_markup())


def build() -> tuple[tuple[int, int, int], Pack]:
    version = read_version()
    pack = Pack()
    for page in sorted(SRC.glob("*.html")):
        html = build_page(pack, page)
        # Atlas serves only /assets/ and the page itself: a relative reference
        # left over would load nothing on the table.
        leftover = re.findall(r'(?:href|src)="(\.[^"]*)"', html)
        if leftover:
            raise BuildError(f"{page.name}: unresolved local references {leftover}")
        pack.add(page.name, html.encode("utf-8"))
    if "index.html" not in pack.files:
        raise BuildError("the pack needs src/index.html")
    for path, source in STATIC.items():
        pack.add(path, source.read_bytes())
    return version, pack


def archive(version: tuple[int, int, int], pack: Pack) -> bytes:
    if not 0 < len(pack.files) <= MAX_FILES:
        raise BuildError(f"a pack holds 1..{MAX_FILES} files, not {len(pack.files)}")
    descriptor = DESCRIPTOR_MAGIC + bytes([PRODUCT_PORTAL, *version, 0, 0, 0, 0])
    out = bytearray(descriptor + ARCHIVE_MAGIC + struct.pack("<HH", len(pack.files), 0))
    for path in sorted(pack.files):
        data = pack.files[path]
        flags = 0
        if Path(path).suffix in GZIP_TYPES:
            packed = gzip.compress(data, compresslevel=9, mtime=0)
            if len(packed) < len(data):
                data, flags = packed, FLAG_GZIP
        name = path.encode("ascii")
        out += struct.pack("<BBI", len(name), flags, len(data)) + name + data
    image = bytes(out)
    # thfw.py finds the descriptor by its magic: it must appear exactly once.
    if image.count(DESCRIPTOR_MAGIC) != 1:
        raise BuildError("a pack file contains the descriptor magic THFWDSC1")
    if len(image) > MAX_PACK_BYTES:
        raise BuildError(f"pack is {len(image)} bytes; Atlas accepts {MAX_PACK_BYTES}")
    return image


def write_site(pack: Pack) -> Path:
    site = DIST / "site"
    if site.exists():
        for f in sorted(site.rglob("*"), reverse=True):
            f.unlink() if f.is_file() else f.rmdir()
    for path, data in pack.files.items():
        target = site / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return site


def serve(atlas: str, port: int) -> None:
    site = DIST / "site"

    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=str(site), **kw)

        def local_path(self):
            path = self.path.split("?", 1)[0]
            if path in ("/", "/portal"):
                return "/index.html"
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
    parser.add_argument("--out", type=Path, help="archive path (default dist/portal-<version>.bin)")
    parser.add_argument("--key", help="PEM signing key: also write the signed .thfw next to the archive")
    parser.add_argument("--build-id", default="", help="hex build id recorded in the signed package")
    parser.add_argument("--check", action="store_true", help="build in memory and check; write nothing")
    parser.add_argument("--serve", action="store_true", help="build, then run the local preview server")
    parser.add_argument("--atlas", default="192.168.4.1", help="Atlas address for --serve")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args(argv)
    try:
        version, pack = build()
        image = archive(version, pack)
    except (BuildError, OSError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1
    vtext = ".".join(map(str, version))
    total = sum(len(d) for d in pack.files.values())
    print(f"Portal pack {vtext}: {len(pack.files)} files, {total // 1024} KB raw, {len(image) // 1024} KB packed")
    if args.check:
        return 0
    out = args.out or DIST / f"portal-{vtext}.bin"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(image)
    write_site(pack)
    print(f"Wrote {out} and {DIST / 'site'}")
    if args.key:
        import thfw
        package = thfw.build_package(image, thfw.load_private_key(args.key), args.build_id)
        signed = out.with_suffix(".thfw")
        signed.write_bytes(package)
        print(f"Signed {signed}")
    if args.serve:
        serve(args.atlas, args.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
