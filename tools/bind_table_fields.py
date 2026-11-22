#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT
"""Bind CPS table columns to runtime field IDs and annotate the field schema.

Usage:
  bind_table_fields.py [--fields JSON] [--tables JSON] [--names JSON]
                       [--out-fields JSON] [--out-tables JSON] [--out-md MD]

The optional names JSON maps PatPortDepot.exe RT_STRING resource IDs to text.
Field names use resource ID fid + 16 (confirmed on five known Radio Wide IDs).
"""

import argparse
import json
import re
from collections import defaultdict
from pathlib import Path

DEFAULT_FIELDS = Path("docs/codeplug_field_schema.json")
DEFAULT_TABLES = Path("docs/codeplug_table_schema.json")
DEFAULT_MD = Path("docs/CODEPLUG_TABLE_BINDINGS.md")
BLOCK_TYPE_MAP = Path("docs/open_astro_block_type_map.json")

MANUAL_COLUMNS = {
    ("trunking_call_list", "call list id"): ["Type II Trk Call List Id CP"],
    ("astro_talkgroup_list", "key select reference"): ["Key Select"],
    ("trunking_personality_talkgroups", "f/s astro 25 tx frequency"):
        ["F/S ASTRO 25 Tx Freq (MHz)"],
    ("trunking_message_alias", "message alias number"): ["Message Alias Number"],
    ("trunking_message_alias", "message alias text"): ["Message Alias Text"],
    ("trunking_status_alias", "status alias number"): ["Status Alias Number"],
    ("trunking_status_alias", "status alias text"): ["Status Alias Text"],
    ("nat_list", "static nat ip address"):
        ["Static NAT IP Address 1", "Static NAT IP Address 2",
         "Static NAT IP Address 3", "Static NAT IP Address 4"],
    ("radio_configuration_features", "flashcode information"): ["FLASHcode"],
}

RECORD_TAGS_BY_GROUP = {
    "0xf62f930": "0x1004",
    "0xf62fc90": "0x0FBE",
    "0xf62f0c0": "0x0FF1",
    "0xf62f270": "0x0FE3",
    "0xf62f150": "0x0FF1",
    "0xf62f660": "0x0049",
    "0xf62f810": "0x1005",
    "0xf1a80f0": "0x0FE3",
    "0xf1a8450": "0x000A",
    "0xf1a7a30": "0x000C",
    "0x40ab1b8": "0x002C",
    "0x40aa288": "0x0FF9",
    "0x40ab488": "0x0FE3",
    "0x40aa9d8": "0x0FFB",
    "0xf19b610": "0x0FE3",
    "0xf19b220": "0x0FBE",
    "0xf19bdf0": "0x0FE3",
    "0xf19b8e0": "0x0FD5",
    "0xf19cd20": "0x0FBE",
    "0xf19c390": "0x0FBE",
    "0xf19c540": "0x0FBE",
    "0xf19c6f0": "0x0FBE",
    "0xede42a0": "0x0FBE",
    "0xede4690": "0x0FBE",
    "0xede51d0": "0x0FBE",
    "0xede49f0": "0x0FBE",
    "0xede4b10": "0x0FBE",
    "0xede4de0": "0x0FBE",
    "0xede4060": "0x0FBE",
    "0xede4840": "0x0FE7",
    "0xede47b0": "0x003A",
    "0xede40f0": "0x0049",
    "0xede4f90": "0x0FBE",
    "0xede4d50": "0x0FBE",
    "0xede4e70": "0x0FBE",
    "0xede5800": "0x0FD5",
    "0xebe8370": "0x0FBE",
    "0xebe8490": "0x0FBE",
    "0xebe7b90": "0x0FBE",
    "0xebe7f80": "0x0FBE",
    "0x107fe838": "0x0047",
}

MANUAL_GROUPS = {
    ("astro_talkgroup_list", "key select reference"): "0x40aba28",
    ("trunking_message_alias", "message alias number"): "0xebe8760",
    ("trunking_message_alias", "message alias text"): "0xebe8910",
    ("trunking_status_alias", "status alias number"): "0xebe81c0",
    ("trunking_status_alias", "status alias text"): "0xebe7b00",
}


def norm(value):
    value = value.strip().lower()
    value = re.sub(r"\([^)]*\)", " ", value)
    value = re.sub(r"[^a-z0-9/]+", " ", value)
    return " ".join(value.split())


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def bind(fields, tables, names):
    type_to_item = {}
    if BLOCK_TYPE_MAP.exists():
        try:
            data = json.loads(BLOCK_TYPE_MAP.read_text(encoding="utf-8"))
            type_to_item = {int(b["type"]): int(b["item_id"]) for b in data.get("blocks", [])}
        except Exception:
            type_to_item = {}
    if names:
        for field in fields:
            if field.get("name"):
                continue
            resource_id = str(int(field["fid"]) + 16)
            name = names.get(resource_id, "")
            if name:
                field["name"] = name
                field["name_resource_id"] = int(resource_id)

    for field in fields:
        if not field.get("record_tag") and field.get("g") in RECORD_TAGS_BY_GROUP:
            field["record_tag"] = RECORD_TAGS_BY_GROUP[field["g"]]
        if not field.get("record_tag") and field.get("block_id") in type_to_item:
            field["record_tag"] = f"0x{type_to_item[field['block_id']]:04X}"

    by_name = defaultdict(list)
    for field in fields:
        if field.get("name"):
            by_name[norm(field["name"])].append(field)

    group_info = {}
    for field in fields:
        info = group_info.setdefault(field["g"], {"names": set(), "fields": 0})
        info["fields"] += 1
        if field.get("name"):
            info["names"].add(norm(field["name"]))

    for table in tables["tables"]:
        group_columns = defaultdict(set)
        for column in table["columns"]:
            for field in by_name.get(norm(column["name"]), []):
                group_columns[field["g"]].add(norm(column["name"]))
        ranked = sorted(
            group_columns.items(),
            key=lambda item: (-len(item[1]), -group_info[item[0]]["fields"]),
        )
        scores = {group: len(cols) for group, cols in ranked}
        primary = ranked[0][0] if ranked else None

        matched_groups = []
        for group, cols in ranked:
            info = group_info[group]
            names_count = len(info["names"])
            matched_groups.append({
                "group": group,
                "columns": len(cols),
                "fields": info["fields"],
                "unique_names": names_count,
                "group_coverage": round(len(cols) / names_count, 3) if names_count else 0,
            })
        table["primary_group"] = primary
        table["primary_group_columns"] = scores.get(primary, 0)
        table["matched_groups"] = matched_groups
        primary_fields = [f for f in fields if f["g"] == primary] if primary else []
        primary_blocks = {(f.get("block_id"), f.get("block_name"))
                          for f in primary_fields if f.get("block_id") is not None}
        if len(primary_blocks) == 1:
            block_id, block_name = next(iter(primary_blocks))
            table["primary_block_id"] = block_id
            table["primary_block_name"] = block_name
        else:
            table["primary_block_id"] = None
            table["primary_block_name"] = None

        bound = 0
        ambiguous = 0
        missing = 0
        for column in table["columns"]:
            key = (table["key"], norm(column["name"]))
            manual_names = MANUAL_COLUMNS.get(key)
            if manual_names:
                chosen = []
                for name in manual_names:
                    chosen.extend(by_name.get(norm(name), []))
                group = MANUAL_GROUPS.get(key)
                if group:
                    chosen = [f for f in chosen if f["g"] == group]
            else:
                chosen = by_name.get(norm(column["name"]), [])
            if not chosen:
                column["match"] = "missing"
                column["bindings"] = []
                missing += 1
                continue
            best_score = max(scores.get(field["g"], 0) for field in chosen)
            if not manual_names and primary in {field["g"] for field in chosen}:
                best_score = max(best_score, scores.get(primary, 0))
                chosen = [f for f in chosen if f["g"] == primary]
            elif not manual_names:
                chosen = [f for f in chosen if scores.get(f["g"], 0) == best_score]
            bindings = []
            seen = set()
            for field in chosen:
                key = (field["g"], field["fid"])
                if key in seen:
                    continue
                seen.add(key)
                bindings.append({
                    "fid": field["fid"],
                    "group": field["g"],
                    "off": field["off"],
                    "bits": field["bits"],
                    "msb": field["msb"],
                    "table_score": scores.get(field["g"], 0),
                    "block_id": field.get("block_id"),
                    "block_name": field.get("block_name"),
                })
            column["bindings"] = bindings
            if not bindings:
                column["match"] = "missing"
                missing += 1
            elif len({b["group"] for b in bindings}) == 1 and len(bindings) == 1:
                column["match"] = "bound"
                bound += 1
            elif manual_names and len({b["group"] for b in bindings}) == 1:
                column["match"] = "composite"
                bound += 1
            else:
                column["match"] = "ambiguous"
                ambiguous += 1
        table["bound_column_count"] = bound
        table["ambiguous_column_count"] = ambiguous
        table["missing_column_count"] = missing
        table["binding_coverage"] = round(bound / table["column_count"], 3)

    group_tables = defaultdict(list)
    table_titles = {}
    for table in tables["tables"]:
        table_titles[table["key"]] = table["title"]
        for match in table["matched_groups"]:
            group_tables[match["group"]].append({
                "table_key": table["key"],
                "table_title": table["title"],
                "columns": match["columns"],
                "group_coverage": match["group_coverage"],
            })
    for field in fields:
        for key in ("table_key", "table_title", "table_match_columns",
                    "table_group_coverage", "table_candidates"):
            field.pop(key, None)
        candidates = group_tables.get(field["g"], [])
        if not candidates:
            continue
        candidates.sort(key=lambda item: (-item["columns"], -item["group_coverage"]))
        top = candidates[0]
        tied = [c for c in candidates if c["columns"] == top["columns"]
                and c["group_coverage"] == top["group_coverage"]]
        if len(tied) == 1 and top["group_coverage"] >= 0.5:
            field["table_key"] = top["table_key"]
            field["table_title"] = top["table_title"]
            field["table_match_columns"] = top["columns"]
            field["table_group_coverage"] = top["group_coverage"]
        else:
            field["table_candidates"] = candidates[:3]
    return fields, tables


def write_markdown(path, fields, tables):
    named = sum(1 for field in fields if field.get("name"))
    assigned = sum(1 for field in fields if field.get("table_key"))
    field_names = {(f["g"], f["fid"]): f.get("name") or "" for f in fields}
    lines = [
        "# Codeplug table bindings",
        "",
        "Table columns joined to runtime field IDs by normalized CPS field name.",
        "Each binding carries the layout group and payload offset used by the",
        "Depot's `CcgIshDataLayout::AddField` objects.",
        "",
        f"- Field names: {named}/{len(fields)}",
        f"- Fields assigned to a table group: {assigned}/{len(fields)}",
        f"- Tables: {tables['table_count']}",
        f"- Columns: {tables['column_count']}",
        "",
        "| Table | Columns | Bound | Ambiguous | Missing | Primary group | Primary block |",
        "|---|---:|---:|---:|---:|---|---|",
    ]
    for table in tables["tables"]:
        lines.append(
            f"| {table['title']} | {table['column_count']} | "
            f"{table['bound_column_count']} | {table['ambiguous_column_count']} | "
            f"{table['missing_column_count']} | `{table.get('primary_group') or '-'}` | "
            f"{table.get('primary_block_id') or '-'} {table.get('primary_block_name') or ''} |"
        )
    lines.append("")
    for table in tables["tables"]:
        lines += [
            f"## {table['title']}",
            "",
            f"Bound {table['bound_column_count']}/{table['column_count']} columns; "
            f"ambiguous {table['ambiguous_column_count']}; "
            f"missing {table['missing_column_count']}.",
            "",
            "| # | Column | Match | Field | Name | Group | Off | Bits | MSB | Block |",
            "|---:|---|---|---:|---|---|---:|---:|---:|---|",
        ]
        for column in table["columns"]:
            if column["bindings"]:
                for binding in column["bindings"]:
                    name = field_names.get((binding["group"], binding["fid"]), "")
                    lines.append(
                        f"| {column['index']} | {column['name']} | {column['match']} | "
                        f"{binding['fid']} | {name} | `{binding['group']}` | "
                        f"{binding['off']} | {binding['bits']} | {binding['msb']} | "
                        f"{binding.get('block_id') or '-'} |"
                    )
            else:
                lines.append(
                    f"| {column['index']} | {column['name']} | missing | "
                    f"- | - | - | - | - | - | - |"
                )
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--fields", type=Path, default=DEFAULT_FIELDS)
    parser.add_argument("--tables", type=Path, default=DEFAULT_TABLES)
    parser.add_argument("--names", type=Path)
    parser.add_argument("--out-fields", type=Path, default=DEFAULT_FIELDS)
    parser.add_argument("--out-tables", type=Path, default=DEFAULT_TABLES)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_MD)
    args = parser.parse_args()

    fields = load(args.fields)
    tables = load(args.tables)
    names = load(args.names) if args.names else {}
    fields, tables = bind(fields, tables, names)
    args.out_fields.write_text(json.dumps(fields, indent=2) + "\n", encoding="utf-8")
    args.out_tables.write_text(json.dumps(tables, indent=2) + "\n", encoding="utf-8")
    write_markdown(args.out_md, fields, tables)
    named = sum(1 for field in fields if field.get("name"))
    bound = sum(t["bound_column_count"] for t in tables["tables"])
    print(f"named fields {named}/{len(fields)}")
    print(f"bound columns {bound}/{tables['column_count']}")
    print(f"wrote {args.out_fields}")
    print(f"wrote {args.out_tables}")
    print(f"wrote {args.out_md}")


if __name__ == "__main__":
    raise SystemExit(main())
