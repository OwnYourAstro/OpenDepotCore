#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT
"""Compare a real codeplug's record instances/lengths with a manifest."""

import argparse
import csv
import io
import json
import subprocess
from pathlib import Path


def parse_records(text: str) -> dict:
    rows = list(csv.reader(io.StringIO(text)))
    out = {}
    for row in rows[1:]:
        if len(row) < 4:
            continue
        rid = int(row[0], 16)
        tag = rid >> 16
        inst = rid & 0xFFFF
        size = int(row[2])
        length = max(0, size - 8)
        d = out.setdefault(tag, {"instances": set(), "min": 10**9, "max": 0})
        d["instances"].add(inst)
        d["min"] = min(d["min"], length)
        d["max"] = max(d["max"], length)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("manifest", type=Path)
    ap.add_argument("codeplug", type=Path)
    ap.add_argument("--cli", type=Path, required=True)
    args = ap.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    out = subprocess.check_output(
        [str(args.cli), "--records", str(args.codeplug)],
        text=True,
        errors="replace",
    )
    user = parse_records(out)
    print(f"manifest tags={len(manifest['records'])} codeplug tags={len(user)}")
    for tag, clean in sorted(manifest["records"].items()):
        t = int(tag, 16)
        if t not in user:
            print(f"{tag}: missing in codeplug (clean len {clean['minLen']}-{clean['maxLen']})")
            continue
        real = user[t]
        if (
            len(real["instances"]) != len(clean["instances"])
            or real["min"] != clean["minLen"]
            or real["max"] != clean["maxLen"]
        ):
            print(
                f"{tag}: clean inst={len(clean['instances'])} len={clean['minLen']}-{clean['maxLen']} "
                f"codeplug inst={len(real['instances'])} len={real['min']}-{real['max']}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

