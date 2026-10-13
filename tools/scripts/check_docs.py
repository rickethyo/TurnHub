#!/usr/bin/env python3
"""Documentation hygiene check (AGENT_WORKFLOW.md, "Documentation hygiene").

Fails when a Markdown file outside Documentation/engineering/history/ links to
a repository path that no longer exists, or names a path retired from master
(it lives on legacy/gen1 now). Standard library only; prints one line per
problem so the fix is to delete or update that line.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
SKIP_DIRS = {".git", "node_modules", ".pio", "build", ".gradle", "history"}
# Moved to legacy/gen1 (or retired) on 2026-10-13, including the Gen 1 Atlas
# board layer and the spare Sigil path. A mention is fine only in a
# paragraph that says where it went.
RETIRED = ("TestHarness/", "KiCad/", "3D/", "Sigil/WOKWI.md", "Sigil/DISPLAY.md",
           "flash-all", "setup-boards", "sigil-wokwi", "sigil-spare",
           "agent/experimental", "atlas_display.cpp", "atlas_art", "atlas_speaker.cpp",
           "atlas_battery.cpp", "render-atlas-screens", "tools/fonts",
           "battery_firmware", "CAPABILITY_SPARE", "PairRequestSpare")
CONTEXT = ("legacy/gen1", "retired", "Gen 1")
LINK = re.compile(r"\]\(([^)\s]+)\)")


def markdown_files():
    for path in ROOT.rglob("*.md"):
        rel = path.relative_to(ROOT)
        if not SKIP_DIRS.intersection(rel.parts[:-1]):
            yield path


def paragraphs(lines):
    """Yields (line number, line, the paragraph or table it belongs to)."""
    start = 0
    for end in range(len(lines) + 1):
        if end == len(lines) or not lines[end].strip():
            block = "\n".join(lines[start:end])
            for number in range(start, end):
                yield number + 1, lines[number], block
            start = end + 1


def problems():
    for path in markdown_files():
        rel = path.relative_to(ROOT)
        lines = path.read_text(encoding="utf-8").splitlines()
        for number, line, block in paragraphs(lines):
            for target in LINK.findall(line):
                target = target.split("#", 1)[0]
                if not target or re.match(r"^[a-z][a-z0-9+.-]*:", target):
                    continue  # In-page anchor, URL, mailto, etc.
                resolved = (path.parent / target.replace("%20", " ")).resolve()
                if not resolved.exists():
                    yield f"{rel}:{number}: link to missing {target}"
            if not any(word in block for word in CONTEXT):
                for name in RETIRED:
                    if name in line:
                        yield f"{rel}:{number}: names retired {name} without pointing to legacy/gen1"


if __name__ == "__main__":
    found = list(problems())
    for problem in found:
        print(problem)
    if found:
        sys.exit(f"{len(found)} documentation problem(s); delete or update the lines above")
    print("Documentation links and retired-path mentions are clean")
