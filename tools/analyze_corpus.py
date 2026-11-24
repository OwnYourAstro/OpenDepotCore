#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT
"""Summarize a corpus-analyzer JSON and emit a model/option manifest."""

import argparse
import json
from pathlib import Path


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def print_model(model: str, data: dict, option: str | None) -> None:
    m = data["models"][model]
    print(f"model {model}")
    for name, opt in sorted(m["options"].items()):
        print(f"  option {name}: files={opt['files']} tags={len(opt['tags'])}")
    print(f"  base tags: {len(m['baseTags'])}")
    print(f"  union tags: {len(m['unionTags'])}")
    if option is None:
        return
    opt = m["options"][option]
    print(f"\n{option}: {len(opt['records'])} tag records")
    print(f"{'tag':8} {'inst':5} {'len':9} {'fields':6} {'block':38} section")
    for tag in sorted(opt["records"]):
        r = opt["records"][tag]
        inst = len(r["instances"])
        length = str(r["minLen"]) if r["minLen"] == r["maxLen"] else f"{r['minLen']}-{r['maxLen']}"
        sec = ",".join(r["sections"])
        print(f"{tag:8} {inst:<5} {length:9} {r['fieldCount']:<6} "
              f"{(r['block'] or '-')[:38]:38} {sec}")


def print_deltas(model: str, data: dict) -> None:
    m = data["models"][model]
    base = set(m["baseTags"])
    print("\noption deltas (tags not in base):")
    for name, opt in sorted(m["options"].items()):
        only = sorted(set(opt["tags"]) - base)
        print(f"  {name}: {len(only)}")
        for tag in only:
            print(f"    {tag}")


def print_sections(model: str, data: dict, option: str) -> None:
    opt = data["models"][model]["options"][option]
    sections: dict[str, list[str]] = {}
    for tag, rec in opt["records"].items():
        for section in rec["sections"] or ["(unmapped)"]:
            sections.setdefault(section, []).append(tag)
    print("\nsection tags:")
    for section in sorted(sections):
        tags = sorted(sections[section])
        print(f"  {section}: {', '.join(tags)}")


def write_manifest(model: str, option: str, data: dict, out: Path) -> None:
    m = data["models"][model]
    opt = m["options"][option]
    capacities: dict[str, dict] = {}
    for item in m.get("readSet", []):
        cap = capacities.setdefault(item["tag"], {"instances": set(), "maxLen": 0})
        cap["instances"].add(item["inst"])
        cap["maxLen"] = max(cap["maxLen"], item["len"])
    records = {}
    for tag, rec in opt["records"].items():
        record = dict(rec)
        cap = capacities.get(tag)
        if cap:
            record["capacity"] = {
                "instances": len(cap["instances"]),
                "maxLen": cap["maxLen"],
            }
        records[tag] = record
    manifest = {
        "model": model,
        "option": option,
        "baseTags": m["baseTags"],
        "sectionSource": "CodeplugModel (not authoritative yet)",
        "capacitySource": "ModelReadRequests",
        "records": records,
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {out}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("corpus", type=Path)
    ap.add_argument("--model", required=True)
    ap.add_argument("--option")
    ap.add_argument("--manifest", type=Path)
    ap.add_argument("--deltas", action="store_true")
    ap.add_argument("--sections", action="store_true")
    args = ap.parse_args()
    data = load(args.corpus)
    if args.model not in data["models"]:
        raise SystemExit(f"model not in corpus: {args.model}")
    print_model(args.model, data, args.option)
    if args.deltas:
        print_deltas(args.model, data)
    if args.sections:
        if not args.option:
            raise SystemExit("--sections requires --option")
        print_sections(args.model, data, args.option)
    if args.manifest:
        if not args.option:
            raise SystemExit("--manifest requires --option")
        write_manifest(args.model, args.option, data, args.manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


