#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT
"""Parse Motorola CPS table definition files (*.tbl) into JSON/Markdown.

Usage:
  parse_table_defs.py [TABLE_DIR] [OUT_JSON] [--markdown OUT_MD]

The table files are Rui/CgIsh grid definitions. Each column record is:

  u16 flags, u16 format, u16 value_type, u16 width,
  8 reserved bytes, u32 0x80, u16 name_length, name (NUL included),
  display trailer

NAME_LENGTH includes the terminating NUL. format 0x87/type 0x4C is text;
format 0x99/type 0x04 is a checkbox.
"""

import argparse
import hashlib
import json
import struct
from pathlib import Path

DEFAULT_DIR = Path(r"C:\ProgramData\Motorola\PatPort\Configure\Table")
DEFAULT_JSON = Path("docs/codeplug_table_schema.json")
DEFAULT_MD = Path("docs/CODEPLUG_TABLE_SCHEMA.md")
DEFAULT_FIELDS = Path("docs/codeplug_field_schema.json")

TABLE_TAGS = {
    "trunking_message_alias": [0x0FAA],
    "trunking_status_alias": [0x0059],
    "trunking_site_alias": [0x0FE6],
    "trunking_astro_site_alias": [0x0FE6],
    "trunking_obt_control_channels": [0x0FE6],
    "trunking_obt_astro_control_channels": [0x0FE6],
    "trunking_preferred_sites": [0x0FE6],
    "nat_list": [],
    "secure_multikey": [0x0034, 0x0057],
    "radio_configuration_features": [0x1004, 0x1005, 0x1006],
}

CORE_SECTION_TAGS = {
    "radio_information": [0x1004, 0x1005, 0x1006],
    "radio_wide": [
        0x000A, 0x000B, 0x000C, 0x0014, 0x0016, 0x0018, 0x0019, 0x0020,
        0x0028, 0x002A, 0x002C, 0x002D, 0x0048, 0x0058, 0x0070, 0x0071,
        0x0FAB,
    ],
    "test_channel_list": [0x0FF0, 0x0FF1],
    "narrow_band_list": [0x0029],
    "secure": [0x0034, 0x0057],
    "trunking_system": [0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F, 0x0FE6],
    "trunking_personality": [
        0x0052, 0x0053, 0x0FA8, 0x0FA9, 0x0FC6, 0x0FD0, 0x0FD1,
    ],
    "talkgroup": [
        0x0FD2, 0x0FD3, 0x0FD4, 0x0FD5, 0x0FD6, 0x0FD7, 0x0FD8, 0x0FD9,
        0x0FDA, 0x0FDB, 0x0FDC, 0x0FDD, 0x0FDE, 0x0FDF, 0x0FE0, 0x0FE1,
        0x0FE2, 0x0FE3, 0x0FE4, 0x0FE5, 0x0FE7, 0x0FE8, 0x0FE9, 0x0FEA,
        0x0FEB, 0x0FEC, 0x0FED, 0x0FEE, 0x0FEF,
    ],
    "astro_systems": [
        0x0FAC, 0x0FAD, 0x0FAE, 0x0FAF, 0x0FB0, 0x0FB1, 0x0FF2, 0x0FF3,
        0x0FF4, 0x0FF5, 0x0FF6, 0x0FF7, 0x0FF8, 0x0FF9,
    ],
    "zone_channel_assignment": [0x000D, 0x0FFC, 0x0FFD, 0x0FFE, 0x0FFF],
}

TEXT_FORMAT = 0x0087
TEXT_TYPE = 0x004C
BOOL_FORMAT = 0x0099
BOOL_TYPE = 0x0004

TABLES = {
    "ASTRO 25 Channel ID Table in Trunking System": {
        "key": "trunking_astro_channel_id",
        "block_id": 152,
        "block_name": "Trk Sys Apco Channel Identifier List Block",
        "core_section": "trunking_system",
    },
    "ASTRO 25 Site Alias Table in Trunking System": {
        "key": "trunking_astro_site_alias",
        "block_id": 117,
        "block_name": "Trk Site Alias Block",
        "core_section": "trunking_system",
    },
    "Channel List Table in Zone Channel Assignment": {
        "key": "zone_channel_assignment",
        "block_id": 45,
        "block_name": "Zone Channel Assignment",
        "core_section": "zone_channel_assignment",
    },
    "List in ASTRO Call List": {
        "key": "astro_call_list",
        "block_id": 127,
        "block_name": "List",
        "core_section": "astro_systems",
    },
    "List in Trunking Call List": {
        "key": "trunking_call_list",
        "block_id": 85,
        "block_name": "TypeII Trunking Call List",
        "core_section": "trunking_personality",
    },
    "Narrow Bandwidth 12.5 kHz Only Frequency Range List": {
        "key": "narrow_band_list",
        "block_id": 292,
        "block_name": "Narrow Bandwidth 12.5 kHz Only Frequency Range Table Block",
        "core_section": "narrow_band_list",
    },
    "NAT List": {
        "key": "nat_list",
        "block_id": 220,
        "block_name": "NAT Entry",
        "core_section": "radio_wide",
    },
    "OBT ASTRO 25 Control Channels Table in Trunking System": {
        "key": "trunking_obt_astro_control_channels",
        "block_id": 22,
        "block_name": "Trunking System Control Channel List",
        "core_section": "trunking_system",
    },
    "OBT Control Channels Table in Trunking System": {
        "key": "trunking_obt_control_channels",
        "block_id": 22,
        "block_name": "Trunking System Control Channel List",
        "core_section": "trunking_system",
    },
    "Secure Multikey": {
        "key": "secure_multikey",
        "block_id": 1,
        "block_name": "Multikey Radio Block",
        "core_section": "secure",
    },
    "SmartZone Preferred  Sites Table in Trunking Personality": {
        "key": "trunking_preferred_sites",
        "block_id": 98,
        "block_name": "Preferred Sites Status Block",
        "core_section": "trunking_personality",
    },
    "TalkGroup in ASTRO Talkgroup List": {
        "key": "astro_talkgroup_list",
        "block_id": 145,
        "block_name": "Astro Talkgroup ID Block",
        "core_section": "talkgroup",
    },
    "Talkgroup Table in Trunking Personality": {
        "key": "trunking_personality_talkgroups",
        "block_id": 224,
        "block_name": "Trunking Personality Talkgroup List Block",
        "core_section": "trunking_personality",
    },
    "Test Channel List": {
        "key": "test_channel_list",
        "block_id": 42,
        "block_name": "Test Channel Table Block",
        "core_section": "test_channel_list",
    },
    "Trunking Message Alias Table in Trunking System": {
        "key": "trunking_message_alias",
        "block_id": 115,
        "block_name": "Trk Message Alias Block",
        "core_section": "trunking_system",
    },
    "Trunking Site Alias Table in Trunking System": {
        "key": "trunking_site_alias",
        "block_id": 117,
        "block_name": "Trk Site Alias Block",
        "core_section": "trunking_system",
    },
    "Trunking Status Alias Table in Trunking System": {
        "key": "trunking_status_alias",
        "block_id": 113,
        "block_name": "Trk Status Alias Block",
        "core_section": "trunking_system",
    },
    "View Radio Configuration Features": {
        "key": "radio_configuration_features",
        "block_id": None,
        "block_name": None,
        "core_section": "radio_information",
    },
}


def parse_columns(data):
    columns = []
    for offset in range(0, len(data) - 22):
        if data[offset + 2:offset + 4] not in (struct.pack("<H", TEXT_FORMAT),
                                               struct.pack("<H", BOOL_FORMAT)):
            continue
        if data[offset + 4:offset + 6] not in (struct.pack("<H", TEXT_TYPE),
                                               struct.pack("<H", BOOL_TYPE)):
            continue
        if data[offset + 8:offset + 16] != b"\x00" * 8:
            continue
        if data[offset + 16:offset + 20] != b"\x80\x00\x00\x00":
            continue
        name_len = struct.unpack_from("<H", data, offset + 20)[0]
        if not 2 <= name_len <= 200:
            continue
        raw = data[offset + 22:offset + 22 + name_len]
        if len(raw) != name_len or raw[-1] != 0:
            continue
        try:
            name = raw[:-1].decode("ascii")
        except UnicodeDecodeError:
            continue
        if not name or "\x00" in name:
            continue
        flags, fmt, value_type, width = struct.unpack_from("<HHHH", data, offset)
        columns.append({
            "index": len(columns),
            "name": name.strip(),
            "name_raw": name,
            "control": "checkbox" if fmt == BOOL_FORMAT else "text",
            "value_type": "bool" if value_type == BOOL_TYPE else "string",
            "flags": flags,
            "flags_hex": f"0x{flags:04X}",
            "format_code": fmt,
            "type_code": value_type,
            "width": width,
            "offset": offset,
        })
    return columns


def load_layout_counts(path):
    if not path or not path.exists():
        return {}
    rows = json.loads(path.read_text(encoding="utf-8"))
    counts = {}
    groups = {}
    for row in rows:
        block_id = row.get("block_id")
        if block_id is None:
            continue
        counts[block_id] = counts.get(block_id, 0) + 1
        groups.setdefault(block_id, set()).add(row.get("g"))
    return {
        block_id: {"rows": counts[block_id], "groups": len(groups[block_id])}
        for block_id in counts
    }


def load_tables(table_dir, layout_counts):
    tables = []
    for path in sorted(table_dir.glob("*.tbl")):
        raw = path.read_bytes()
        columns = parse_columns(raw)
        meta = TABLES.get(path.stem, {
            "key": path.stem.lower().replace(" ", "_"),
            "block_id": None,
            "block_name": None,
            "core_section": None,
        })
        names = [c["name"] for c in columns]
        tags = TABLE_TAGS.get(meta["key"], CORE_SECTION_TAGS.get(meta["core_section"], []))
        layout = layout_counts.get(meta["block_id"], {})
        tables.append({
            "key": meta["key"],
            "title": path.stem,
            "file": path.name,
            "size": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest(),
            "block_id": meta["block_id"],
            "block_name": meta["block_name"],
            "core_section": meta["core_section"],
            "core_section_tags": [f"0x{t:04X}" for t in tags],
            "mapping_confidence": "inferred",
            "column_count": len(columns),
            "text_column_count": sum(c["control"] == "text" for c in columns),
            "checkbox_column_count": sum(c["control"] == "checkbox" for c in columns),
            "layout_rows": layout.get("rows", 0),
            "layout_groups": layout.get("groups", 0),
            "duplicate_column_names": sorted({n for n in names if names.count(n) > 1}),
            "columns": columns,
        })
    return tables


def write_markdown(path, doc):
    lines = [
        "# Codeplug table schema",
        "",
        "Derived from the portable Depot's Rui/CgIsh `.tbl` grid definitions.",
        "The definitions supply CPS column labels, order, display widths, and",
        "text/checkbox control types. They do not supply codeplug bit offsets;",
        "block/core mappings below are inferred from block names and section",
        "ownership. Duplicate labels are distinct columns and must be kept in order.",
        "Width is the stored display width, not a codeplug byte count. Flags are",
        "preserved raw (0x0000/0x0004/0x0008/0x0020); their bit meanings are unknown.",
        "",
        f"- Source: `{doc['source']}`",
        f"- Tables: {doc['table_count']}",
        f"- Columns: {doc['column_count']} "
        f"({doc['text_column_count']} text, {doc['checkbox_column_count']} checkbox)",
        f"- Codeplug bit offsets: not present",
        f"- Tables with layout rows: {doc['tables_with_layout_rows']}",
        f"- Generated by: `{doc['generated_by']}`",
        "",
        "| Table | Block | Core section | Cols | Text | Check | Layout | Tags |",
        "|---|---|---|---:|---:|---:|---:|---:|",
    ]
    for table in doc["tables"]:
        block = f"{table['block_id']} {table['block_name']}" if table["block_id"] else "unmapped"
        core = table["core_section"] or "-"
        lines.append(
            f"| {table['title']} | {block} | `{core}` | {table['column_count']} | "
            f"{table['text_column_count']} | {table['checkbox_column_count']} | "
            f"{table['layout_rows']} | {len(table['core_section_tags'])} |"
        )
    lines.append("")
    for table in doc["tables"]:
        lines += [
            f"## {table['title']}",
            "",
            f"`{table['file']}` | {table['size']} bytes | "
            f"`{table['sha256'][:16]}...` | {table['column_count']} columns",
            "",
        ]
        if table["duplicate_column_names"]:
            lines += [
                "Duplicate labels (distinct columns): "
                + ", ".join(f"`{n}`" for n in table["duplicate_column_names"]),
                "",
            ]
        lines += [
            f"Candidate records: {', '.join(f'`{t}`' for t in table['core_section_tags']) or 'unknown'}",
            f"Layout rows/groups: {table['layout_rows']}/{table['layout_groups']}",
            "",
        ]
        lines += [
            "| # | Column | Control | Value | Width | Flags | Format | Type | Offset |",
            "|---:|---|---|---|---:|---|---|---|---|",
        ]
        for col in table["columns"]:
            lines.append(
                f"| {col['index']} | {col['name']} | {col['control']} | "
                f"{col['value_type']} | {col['width']} | `{col['flags_hex']}` | "
                f"`0x{col['format_code']:04X}` | `0x{col['type_code']:04X}` | "
                f"`0x{col['offset']:04X}` |"
            )
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("table_dir", type=Path, nargs="?", default=DEFAULT_DIR)
    parser.add_argument("out_json", type=Path, nargs="?", default=DEFAULT_JSON)
    parser.add_argument("--markdown", type=Path, default=DEFAULT_MD)
    parser.add_argument("--field-schema", type=Path, default=DEFAULT_FIELDS)
    args = parser.parse_args()

    tables = load_tables(args.table_dir, load_layout_counts(args.field_schema))
    columns = [col for table in tables for col in table["columns"]]
    doc = {
        "source": str(args.table_dir),
        "field_schema": str(args.field_schema),
        "format": "Rui/CgIsh table definition (CArchive-like grid columns)",
        "generated_by": "OpenDepotCore/tools/parse_table_defs.py",
        "table_count": len(tables),
        "column_count": len(columns),
        "text_column_count": sum(col["control"] == "text" for col in columns),
        "checkbox_column_count": sum(col["control"] == "checkbox" for col in columns),
        "tables_with_layout_rows": sum(t["layout_rows"] > 0 for t in tables),
        "has_codeplug_bit_offsets": False,
        "tables": tables,
    }
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    write_markdown(args.markdown, doc)
    print(f"{len(tables)} tables, {len(columns)} columns")
    print(f"wrote {args.out_json}")
    print(f"wrote {args.markdown}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
