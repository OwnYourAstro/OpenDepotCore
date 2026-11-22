#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT
"""Derive a tag/section schema from `opendepot-cli --hexall` dumps.

Usage:
    python analyze_schema.py --dir <analysis dir> --out <report dir>

The input directory contains one `<name>.hex.txt` per codeplug (and optionally
`<name>.model.json`). The tool prints and writes a tag catalog with observed
lengths, models, ASCII strings, references, structural families, and per-byte
variability.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import sys

RECORD_RE = re.compile(r"^record 0x([0-9A-Fa-f]{8})\s+payload=(\d+)\s+type=0x([0-9A-Fa-f]{4})")
HEXLINE_RE = re.compile(r"^\s+[0-9A-Fa-f]{4}\s+((?:[0-9A-Fa-f]{2}\s+)+)")


def parse_hex(path: str) -> list[dict]:
    records = []
    cur = None
    with open(path, "r", encoding="utf-8-sig", errors="replace") as f:
        for line in f:
            m = RECORD_RE.match(line.strip())
            if m:
                if cur:
                    records.append(cur)
                cur = {
                    "id": int(m.group(1), 16),
                    "payload_len": int(m.group(2)),
                    "type": int(m.group(3), 16),
                    "data": bytearray(),
                }
                continue
            m = HEXLINE_RE.match(line)
            if m and cur is not None:
                cur["data"].extend(int(b, 16) for b in m.group(1).split())
    if cur:
        records.append(cur)
    return records


def u16(data: bytes, off: int) -> int:
    return (data[off] << 8) | data[off + 1]


def u32(data: bytes, off: int) -> int:
    return (data[off] << 24) | (data[off + 1] << 16) | (data[off + 2] << 8) | data[off + 3]


def strings(data: bytes, minlen: int = 4) -> list[str]:
    out = []
    cur = bytearray()
    for b in data:
        if 0x20 <= b < 0x7F:
            cur.append(b)
        else:
            if len(cur) >= minlen:
                out.append(cur.decode("ascii"))
            cur.clear()
    if len(cur) >= minlen:
        out.append(cur.decode("ascii"))
    return out


def refs(data: bytes, known: set[int], self_tag: int) -> list[tuple[int, int, int]]:
    out = []
    for off in range(0, len(data) - 1, 2):
        v = u16(data, off)
        if v in known and v != self_tag:
            inst = u16(data, off + 2) if off + 3 < len(data) else None
            out.append((off, v, inst))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    files = sorted(f for f in os.listdir(args.dir) if f.endswith(".hex.txt"))
    if not files:
        print("no .hex.txt files found", file=sys.stderr)
        return 2

    models = {}
    node_meta = {}
    for f in files:
        base = f[: -len(".hex.txt")]
        jpath = os.path.join(args.dir, base + ".model.json")
        meta = {}
        if os.path.exists(jpath):
            try:
                with open(jpath, "r", encoding="utf-8-sig") as fh:
                    m = json.load(fh)
                models[base] = (m.get("model", "?"), m.get("serial", "?"))
                for n in m.get("recordNodes", []):
                    t = int(n["tag"], 16)
                    meta[t] = {
                        "section": n.get("section", ""),
                        "family": n.get("family", ""),
                        "names": n.get("names", []),
                    }
            except Exception:
                models[base] = ("?", "?")
        else:
            models[base] = ("?", "?")
        node_meta[base] = meta

    dataset = {}
    all_tags = set()
    for f in files:
        base = f[: -len(".hex.txt")]
        recs = parse_hex(os.path.join(args.dir, f))
        dataset[base] = recs
        for r in recs:
            all_tags.add(r["id"] >> 16)

    catalog = {}
    for base, recs in dataset.items():
        for r in recs:
            tag = r["id"] >> 16
            inst = r["id"] & 0xFFFF
            e = catalog.setdefault(
                tag,
                {
                    "tag": tag,
                    "files": set(),
                    "models": set(),
                    "lengths": collections.Counter(),
                    "instances": set(),
                    "examples": [],
                    "sections": collections.Counter(),
                    "families": collections.Counter(),
                    "decoded_names": set(),
                },
            )
            e["files"].add(base)
            e["models"].add(models[base][0])
            e["lengths"][r["payload_len"]] += 1
            e["instances"].add(inst)
            meta = node_meta.get(base, {}).get(tag)
            if meta:
                if meta["section"]:
                    e["sections"][meta["section"]] += 1
                if meta["family"]:
                    e["families"][meta["family"]] += 1
                for name in meta["names"]:
                    if name:
                        e["decoded_names"].add(name)
            if len(e["examples"]) < 3:
                e["examples"].append((base, r))

    report = {"files": {}, "tags": {}}
    for base in sorted(dataset):
        recs = dataset[base]
        report["files"][base] = {
            "model": models[base][0],
            "serial": models[base][1],
            "records": len(recs),
            "tags": sorted({r["id"] >> 16 for r in recs}),
        }

    for tag in sorted(catalog):
        e = catalog[tag]
        lens = e["lengths"]
        modal_len = lens.most_common(1)[0][0]
        same = []
        for base, r in ((b, r) for b, recs in dataset.items() for r in recs if r["id"] >> 16 == tag):
            if r["payload_len"] == modal_len:
                same.append(r["data"])

        # Constant/variable map over the modal length.
        variable = []
        if same:
            for off in range(modal_len):
                vals = {d[off] for d in same}
                if len(vals) > 1:
                    variable.append(off)

        ex = e["examples"][0][1]
        d = ex["data"]
        families = []
        if len(d) >= 8 and u16(d, 2) == 17:
            count = u16(d, 6)
            if count <= 256 and 8 + 17 * count <= len(d):
                families.append(f"name_table_17(count={count})")
        if len(d) >= 6 and u16(d, 0) == tag:
            families.append("self_header")
            count = u16(d, 4)
            if count and (len(d) - 6) % count == 0:
                families.append(f"entries(entry={((len(d) - 6) // count)},count={count})")
        rl = refs(d, all_tags, tag)
        if rl:
            families.append("refs(" + ",".join(f"{o}:{t:04X}" for o, t, _ in rl[:8]) + ")")
        ss = strings(d)
        if ss:
            families.append('strings("' + '","'.join(ss[:4]) + '")')

        section = e["sections"].most_common(1)[0][0] if e["sections"] else ""
        report["tags"][f"0x{tag:04X}"] = {
            "files": len(e["files"]),
            "models": sorted(e["models"]),
            "lengths": dict(sorted(lens.items())),
            "modal_len": modal_len,
            "section": section,
            "node_family": (e["families"].most_common(1)[0][0] if e["families"] else ""),
            "decoded_names": sorted(e["decoded_names"])[:12],
            "instances": sorted(e["instances"])[:32],
            "families": families,
            "strings": sorted({s for b, recs in dataset.items() for r in recs if r["id"] >> 16 == tag for s in strings(r["data"], 3)})[:12],
            "variable_offsets": variable[:40],
            "examples": [
                {"file": b, "id": f"0x{r['id']:08X}", "len": r["payload_len"],
                 "data": r["data"].hex().upper()}
                for b, r in e["examples"]
            ],
        }

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "schema.json"), "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2)

    lines = ["# Codeplug record schema (derived)", ""]
    lines.append(f"- files: {len(report['files'])}")
    lines.append(f"- distinct tags: {len(report['tags'])}")
    lines.append("")
    lines.append("## Files")
    lines.append("")
    lines.append("| file | model | serial | records | tags |")
    lines.append("|---|---|---|---|---|")
    for base, fi in report["files"].items():
        lines.append(f"| {base} | {fi['model']} | {fi['serial']} | {fi['records']} | {len(fi['tags'])} |")
    lines.append("")
    lines.append("## Tags")
    lines.append("")
    lines.append("| tag | files | lengths | section | node family | families |")
    lines.append("|---|---|---|---|---|---|")
    for key, t in sorted(report["tags"].items()):
        fam = "; ".join(t["families"])
        lines.append(
            f"| {key} | {t['files']} | {','.join(str(x) for x in t['lengths'])} | "
            f"{t['section']} | {t['node_family']} | {fam} |"
        )
    lines.append("")
    lines.append("## Sections")
    lines.append("")
    lines.append("| section | tags |")
    lines.append("|---|---|")
    by_section = collections.defaultdict(list)
    for key, t in report["tags"].items():
        by_section[t["section"] or "(unmapped)"].append(key)
    for sec in sorted(by_section):
        lines.append(f"| {sec} | {', '.join(by_section[sec])} |")
    with open(os.path.join(args.out, "schema.md"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")

    print(f"files: {len(report['files'])}, tags: {len(report['tags'])}")
    print(f"wrote {os.path.join(args.out, 'schema.json')}")
    print(f"wrote {os.path.join(args.out, 'schema.md')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
