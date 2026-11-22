#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT
"""Decode codeplug fields from docs/codeplug_field_schema.json.

Usage:
  decode_field_schema.py <schema.json> <model.json> [group ...]

The model JSON is the output of `opendepot-cli --model` (PowerShell may have
written it as UTF-16). Groups with a `record_tag` in the schema are decoded
against that record payload.
"""

import json
import sys

KNOWN_NAMES = {
    9271: "Out of Range Indicator",
    13335: "Imbalanced Coverage Indicator",
    25543: "System Registration Indicator",
    9425: "Time Format",
    9426: "Date Format",
}


def load_json(path):
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff") or (len(raw) > 1 and raw[1:2] == b"\x00"):
        return json.loads(raw.decode("utf-16"))
    return json.loads(raw.decode("utf-8"))


def read_bits(data, off, count, msb):
    if count <= 0:
        return 0
    pos = off * 8 + (7 - msb)
    value = 0
    for i in range(count):
        byte = data[(pos + i) // 8]
        bit = 7 - ((pos + i) % 8)
        value = (value << 1) | ((byte >> bit) & 1)
    return value


def main():
    if len(sys.argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    schema = load_json(sys.argv[1])
    model = load_json(sys.argv[2])
    groups = set(sys.argv[3:]) or {"0xf1a8450", "0xf1a7a30"}

    records = {}
    for node in model.get("recordNodes", []):
        records[node["tag"]] = bytes.fromhex(node["payload"])

    out = []
    for row in schema:
        if row.get("g") not in groups:
            continue
        tag = row.get("record_tag")
        if not tag or tag not in records:
            continue
        data = records[tag]
        value = read_bits(data, row["off"], row["bits"], row["msb"])
        out.append(
            {
                "record": tag,
                "group": row["g"],
                "block_id": row.get("block_id"),
                "block_name": row.get("block_name"),
                "fid": row["fid"],
                "name": row.get("name") or KNOWN_NAMES.get(row["fid"], ""),
                "table_key": row.get("table_key"),
                "table_title": row.get("table_title"),
                "off": row["off"],
                "bits": row["bits"],
                "msb": row["msb"],
                "value": value,
            }
        )

    json.dump(out, sys.stdout, indent=2)
    print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
