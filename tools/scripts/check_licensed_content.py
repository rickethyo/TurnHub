#!/usr/bin/env python3
"""Fail if licensed third-party game content creeps back into the repository.

Owner, 2026-11-04: one licensed board game lives only in a separate local-only
app on the owner's PC, and nothing about it (content, code, docs or file names)
may be committed here. This check scans every tracked file's path and text for
its terms. The terms are stored as salted SHA-256 prefixes so this file does
not name the game either.

Words are split at camelCase and non-alphanumeric boundaries, lowercased, and
checked alone and joined with the next one or two words ("Foo Bar Baz",
"foo-bar-baz" and "FooBarBaz" all match "foobarbaz").

Run from anywhere: python3 tools/scripts/check_licensed_content.py
"""
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
import zipfile
from pathlib import Path

SALT = "turnhub-licensed:"
TERMS = {
    "7a67fa9fa8a77053", "bb53acfc8024e8f0", "80037d34d85d1cfe", "771093cc1de927dc",
    "623572068dff4d88", "0af4908f06d3786f", "a73fccedbe08405f", "9dbddf821c45d429",
    "b639065e3ea1e0fc",
}
# First words of the multi-word terms: only these start a joined check.
PREFIXES = {"a55fe638f1cd9e5b", "89fa3418e6ca7179"}
ALLOWED: set[str] = set()
MAX_BYTES = 4 * 1024 * 1024

WORD = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+|[0-9]+")
RAW = re.compile(r"[A-Za-z0-9]+")
_cache: dict[str, str] = {}


def digest(word: str) -> str:
    value = _cache.get(word)
    if value is None:
        value = hashlib.sha256((SALT + word).encode()).hexdigest()[:16]
        _cache[word] = value
    return value


def hits(text: str) -> bool:
    for run in RAW.findall(text):
        if digest(run.lower()) in TERMS:
            return True
    words = [w.lower() for w in WORD.findall(text)]
    for i, word in enumerate(words):
        d = digest(word)
        if d in TERMS:
            return True
        if d in PREFIXES:
            joined = word
            for extra in words[i + 1:i + 3]:
                joined += extra
                if digest(joined) in TERMS:
                    return True
    return False


def main() -> int:
    root = Path(subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True).strip())
    files = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
    bad: list[str] = []
    for name in filter(None, files):
        if name in ALLOWED:
            continue
        if hits(name):
            bad.append(f"{name}: path")
            continue
        path = root / name
        if not path.is_file() or path.stat().st_size > MAX_BYTES:
            continue
        if name.endswith(".docx"):
            # The manuals: check each paragraph's text, runs joined.
            with zipfile.ZipFile(path) as docx:
                xml = docx.read("word/document.xml").decode("utf-8", "replace")
            text = "\n".join(re.sub(r"<[^>]+>", "", p) for p in re.findall(r"<w:p[ >].*?</w:p>", xml, re.S))
            if any(hits(line) for line in text.splitlines()):
                bad.append(f"{name}: text")
            continue
        data = path.read_bytes()
        if b"\0" in data:
            continue
        for number, line in enumerate(data.decode("utf-8", "replace").splitlines(), 1):
            if hits(line):
                bad.append(f"{name}:{number}")
                break
    if bad:
        print("Local-only licensed content found (it belongs in the separate local app):")
        print("\n".join(f"  {entry}" for entry in bad))
        return 1
    print(f"Licensed-content check: {len(files) - 1} tracked files clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
