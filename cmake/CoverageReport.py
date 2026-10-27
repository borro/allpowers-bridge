"""Normalize host Cobertura line coverage; no build or device operations."""

import argparse
import os
from pathlib import Path
import posixpath
import xml.etree.ElementTree as ET


PREFIXES = ("core/", "firmware/R2500Adapter/", "apps/windows/")


def normalize_report(root, inputs):
    root = Path(root).resolve()
    # Preserve Git/source spelling even when Windows PDB paths are lowercased.
    inventory = {}
    for prefix in PREFIXES:
        for path in (root / prefix).rglob("*"):
            if path.is_file() and not path.is_symlink() and path.suffix in (".h", ".cpp", ".ino"):
                relative = path.relative_to(root).as_posix()
                inventory[relative.casefold()] = relative
    checkout = root.as_posix().casefold().rstrip("/") + "/"
    files = {}
    for report in inputs:
        tree = ET.parse(report)
        sources = [s.text or "" for s in tree.findall("./sources/source")]
        for cls in tree.findall(".//class"):
            filename = cls.attrib["filename"].replace("\\", "/")
            candidates = [filename] + [s.replace("\\", "/").rstrip("/") + "/" + filename for s in sources]
            relative = None
            for candidate in candidates:
                candidate = posixpath.normpath(candidate).casefold()
                if candidate.startswith(checkout):
                    candidate = candidate[len(checkout):]
                if candidate in inventory:
                    relative = inventory[candidate]
                    break
            if relative is None:
                continue
            for line in cls.findall("./lines/line"):
                number, hits = int(line.attrib["number"]), int(line.attrib["hits"])
                if number <= 0 or hits < 0:
                    raise ValueError("Invalid Cobertura line number or hit count")
                lines = files.setdefault(relative, {})
                lines[number] = max(lines.get(number, 0), int(hits > 0))
    if not files or not any(files.values()):
        raise ValueError("Coverage contains no measured product lines")
    return files


def counts(files):
    return sum(sum(lines.values()) for lines in files.values()), sum(len(lines) for lines in files.values())


def write_report(files, output):
    covered, total = counts(files)
    tree = ET.Element("coverage", {"line-rate": str(covered / total), "lines-covered": str(covered),
                                 "lines-valid": str(total), "version": "allpowers-line-coverage"})
    ET.SubElement(ET.SubElement(tree, "sources"), "source").text = "."
    packages = ET.SubElement(tree, "packages")
    package = ET.SubElement(packages, "package", {"name": "allpowers", "line-rate": str(covered / total)})
    classes = ET.SubElement(package, "classes")
    for filename, lines in sorted(files.items()):
        cls = ET.SubElement(classes, "class", {"name": filename, "filename": filename,
                                              "line-rate": str(sum(lines.values()) / len(lines))})
        ET.SubElement(cls, "methods")
        line_elements = ET.SubElement(cls, "lines")
        for number, hits in sorted(lines.items()):
            ET.SubElement(line_elements, "line", {"number": str(number), "hits": str(hits)})
    ET.indent(tree)
    ET.ElementTree(tree).write(output, encoding="utf-8", xml_declaration=True)


def summary(files, incomplete=False):
    covered, total = counts(files)
    text = ["## Host C++ line coverage", ""]
    if incomplete:
        text += ["**Incomplete: tests failed. Do not use this report as a baseline.**", ""]
    text += [f"**{100 * covered / total:.2f}%** — {covered:,} / {total:,} measured lines.", "",
             "| Source area | Covered / measured lines | Coverage |", "| --- | ---: | ---: |"]
    for prefix in PREFIXES:
        subset = {p: v for p, v in files.items() if p.startswith(prefix)}
        hit, size = counts(subset)
        if size:
            text.append(f"| `{prefix}` | {hit:,} / {size:,} | {100 * hit / size:.2f}% |")
    text += ["", "Only product lines represented in measured host binaries are counted. "
             "Tests, dependencies and generated files are excluded. Device-only firmware, "
             "kernel driver execution and hardware behavior are not measured.", ""]
    return "\n".join(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--input", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--require-prefix", action="append", default=[])
    parser.add_argument("--incomplete", action="store_true")
    args = parser.parse_args()
    files = normalize_report(args.root, args.input)
    for prefix in args.require_prefix:
        if not any(p.startswith(prefix) and lines for p, lines in files.items()):
            raise ValueError(f"Missing measured source area: {prefix}")
    write_report(files, args.output)
    markdown = summary(files, args.incomplete)
    args.summary.write_text(markdown, encoding="utf-8")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as stream:
            stream.write(markdown)
    print(markdown)


if __name__ == "__main__":
    main()
