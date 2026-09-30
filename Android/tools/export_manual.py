"""Exports the newest user manual (.docx) to the Android app's manual asset.

The app shows the manual offline from app/src/main/assets/manual.md. This
script rebuilds that file from "Documentation/User Manual/TurnHub Manual V*.docx"
(the highest version) using the standard library only.

    python Android/tools/export_manual.py           # rewrite the asset
    python Android/tools/export_manual.py --check   # fail if the asset is stale

Output is a small Markdown subset that ManualParser.kt reads: "#", "##" and
"###" headings, "- " bullets, "N. " numbered steps, **bold**, and paragraphs
separated by blank lines (a newline inside a paragraph is a line break).
"""
import argparse
import re
import sys
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

W = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"
ROOT = Path(__file__).resolve().parents[2]
MANUAL_DIR = ROOT / "Documentation" / "User Manual"
ASSET = ROOT / "Android" / "app" / "src" / "main" / "assets" / "manual.md"


def newest_manual() -> Path:
    def version(path: Path):
        match = re.search(r"V(\d+)\.(\d+)", path.stem)
        return (int(match.group(1)), int(match.group(2))) if match else (-1, -1)
    manuals = sorted(MANUAL_DIR.glob("TurnHub Manual V*.docx"), key=version)
    if not manuals:
        sys.exit(f"no manual found in {MANUAL_DIR}")
    return manuals[-1]


def numbered_ids(docx: zipfile.ZipFile) -> set:
    """numIds whose first level is a decimal list rather than bullets."""
    try:
        root = ET.fromstring(docx.read("word/numbering.xml"))
    except KeyError:
        return set()
    decimal_abstract = set()
    for abstract in root.iter(W + "abstractNum"):
        fmt = abstract.find(f"{W}lvl/{W}numFmt")
        if fmt is not None and fmt.get(W + "val") == "decimal":
            decimal_abstract.add(abstract.get(W + "abstractNumId"))
    return {
        num.get(W + "numId")
        for num in root.iter(W + "num")
        if num.find(W + "abstractNumId").get(W + "val") in decimal_abstract
    }


def run_text(run) -> str:
    parts = []
    for child in run:
        if child.tag == W + "t":
            parts.append(child.text or "")
        elif child.tag in (W + "br", W + "cr"):
            parts.append("\n")
        elif child.tag == W + "tab":
            parts.append(" ")
    text = "".join(parts)
    props = run.find(W + "rPr")
    bold = props is not None and (
        props.find(W + "b") is not None
        or (props.find(W + "rStyle") is not None and props.find(W + "rStyle").get(W + "val") == "Strong")
    )
    if bold and text.strip():
        # Keep surrounding spaces and line breaks outside the markers.
        lead = text[: len(text) - len(text.lstrip())]
        trail = text[len(text.rstrip()):]
        text = f"{lead}**{text.strip()}**{trail}"
    return text


def paragraph_text(paragraph) -> str:
    text = "".join(run_text(r) for r in paragraph.iter(W + "r"))
    text = text.replace("****", "")  # adjacent bold runs
    lines = [re.sub(r"[ \t]+", " ", line).strip() for line in text.split("\n")]
    return "\n".join(line for line in lines if line)


def export(docx_path: Path) -> str:
    with zipfile.ZipFile(docx_path) as docx:
        decimal = numbered_ids(docx)
        body = ET.fromstring(docx.read("word/document.xml")).find(W + "body")
    blocks = []
    counter_key, counter = None, 0
    for paragraph in body.iter(W + "p"):
        text = paragraph_text(paragraph)
        if not text:
            continue
        props = paragraph.find(W + "pPr")
        style_el = props.find(W + "pStyle") if props is not None else None
        style = style_el.get(W + "val") if style_el is not None else ""
        num = props.find(W + "numPr") if props is not None else None
        heading = re.fullmatch(r"Heading([1-3])", style)
        if heading:
            blocks.append("#" * int(heading.group(1)) + " " + text.replace("\n", " "))
            counter_key = None
        elif num is not None:
            num_id = num.find(W + "numId").get(W + "val") if num.find(W + "numId") is not None else ""
            flat = text.replace("\n", " ")
            if num_id in decimal:
                counter = counter + 1 if counter_key == num_id else 1
                counter_key = num_id
                blocks.append(f"{counter}. {flat}")
            else:
                blocks.append(f"- {flat}")
        else:
            blocks.append(text)
            counter_key = None
    header = f"<!-- Generated from {docx_path.name} by Android/tools/export_manual.py. Do not edit by hand. -->"
    return header + "\n\n" + "\n\n".join(blocks) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if the asset is out of date")
    args = parser.parse_args()
    source = newest_manual()
    text = export(source)
    if args.check:
        current = ASSET.read_text(encoding="utf-8") if ASSET.is_file() else ""
        if current != text:
            print(f"{ASSET.relative_to(ROOT)} is out of date with {source.name}; "
                  "run python Android/tools/export_manual.py", file=sys.stderr)
            return 1
        print(f"manual asset matches {source.name}")
        return 0
    ASSET.parent.mkdir(parents=True, exist_ok=True)
    ASSET.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {ASSET.relative_to(ROOT)} from {source.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
