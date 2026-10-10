"""Exercise the actual flash-serving C++ path, including gzip/binary lengths."""
import gzip
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("portal_build", ROOT / "Atlas/web/build.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)

with tempfile.TemporaryDirectory(prefix="turnhub-portal-") as temp:
    work = Path(temp)
    pack = build.build()
    (work / "web_assets.h").write_text(build.firmware_header(pack))
    (work / "serve.cpp").write_text(r'''
#include "web_pages.h"
#include <iostream>
int main(int argc, char **argv) {
  WebServer server(80);
  if (argc != 2 || !TurnHubWeb::serveFile(server, argv[1])) return 3;
  std::cout << server.status << "\n" << server.contentType << "\n"
            << server.responseHeaders["Content-Encoding"] << "\n"
            << server.responseHeaders["Cache-Control"] << "\n";
  std::cout.write(server.body.data(), server.body.size());
}
''')
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", 
                    "-I" + str(ROOT / "Atlas/tests/host/stubs"), "-I" + str(ROOT / "Atlas/include"),
                    "-I" + temp, str(work / "serve.cpp"), str(ROOT / "Atlas/src/web_pages.cpp"),
                    "-o", str(work / "serve")], check=True)
    for path, original in pack.files.items():
        result = subprocess.run([str(work / "serve"), path], capture_output=True, check=True)
        status, content_type, encoding, cache, data = result.stdout.split(b"\n", 4)
        assert status == b"200", path
        assert cache == (b"public, max-age=31536000, immutable" if path.startswith("assets/") else b"no-store"), path
        assert content_type.decode() == build.CONTENT_TYPES[Path(path).suffix], path
        assert (gzip.decompress(data) if encoding == b"gzip" else data) == original, path
    assert subprocess.run([str(work / "serve"), "../index.html"], capture_output=True).returncode == 3
    assert subprocess.run([str(work / "serve"), "assets/missing.js"], capture_output=True).returncode == 3
print(f"PASS firmware portal: {len(pack.files)} actual C++ responses, gzip/binary integrity, unknown paths")
