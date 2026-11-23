#!/usr/bin/env python3
"""Map decoded codeplug sections/records to the Depot field registry.

This produces a repeatable coverage report. It does not guess field layouts:
a record is "direct" only when its tag has explicit field bindings, and
"fallback" only when a single field group exactly matches the payload size.
"""
import argparse
import json
import math
import pathlib
from collections import defaultdict


def field_bytes(fields):
    end = 0
    for field in fields:
        end = max(end, int(field.get("off", 0)) * 8 + int(field.get("bits", 0)))
    return math.ceil(end / 8)


def read_json(path):
    raw = pathlib.Path(path).read_bytes()
    if raw.startswith(b"\xff\xfe") or raw.startswith(b"\xfe\xff"):
        return json.loads(raw.decode("utf-16"))
    if raw.startswith(b"\xef\xbb\xbf"):
        return json.loads(raw.decode("utf-8-sig"))
    return json.loads(raw.decode("utf-8"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True, help="opendepot-cli --model JSON")
    parser.add_argument("--fields", required=True, help="codeplug_field_schema.json")
    parser.add_argument("--out", required=True, help="Markdown report path")
    args = parser.parse_args()

    model = read_json(args.model)
    fields = read_json(args.fields)
    nodes = {node["id"]: node for node in model.get("recordNodes", [])}

    by_tag = defaultdict(list)
    groups = defaultdict(list)
    for field in fields:
        tag = field.get("record_tag")
        if tag:
            by_tag[tag.upper()].append(field)
        groups[field.get("g", "")].append(field)
    group_bytes = {key: field_bytes(value) for key, value in groups.items()}

    def candidates(payload_len):
        return [
            key for key, size in group_bytes.items()
            if groups.get(key) and abs(size - payload_len) <= 8
        ][:6]

    sections = []
    total = direct = fallback = missing = 0
    for section in model.get("sections", []):
        rows = []
        for record_id in section.get("records", []):
            node = nodes.get(record_id)
            if not node:
                continue
            total += 1
            tag = str(node.get("tag", "")).upper()
            payload_len = int(node.get("length", 0)) - 8
            direct_fields = by_tag.get(tag, [])
            fallback_groups = [
                key for key, size in group_bytes.items()
                if size == payload_len and groups.get(key)
            ]
            if direct_fields:
                status = "direct"
                count = len(direct_fields)
                direct += 1
            elif fallback_groups:
                status = "fallback"
                count = sum(len(groups[key]) for key in fallback_groups)
                fallback += 1
            else:
                status = "missing"
                count = 0
                missing += 1
            rows.append({
                "id": record_id,
                "tag": tag,
                "inst": node.get("inst", 0),
                "bytes": payload_len,
                "status": status,
                "fields": count,
                "groups": fallback_groups,
                "candidates": candidates(payload_len),
            })
        sections.append((section, rows))

    lines = [
        "# Codeplug field-mapping coverage",
        "",
        f"Model: `{model.get('model', '')}`  ",
        f"Sections: {len(sections)}  ",
        f"Records: {total}  ",
        f"Direct tag mappings: {direct}  ",
        f"Size-fallback mappings: {fallback}  ",
        f"Missing mappings: {missing}",
        "",
        "| Section | Records | Direct | Fallback | Missing |",
        "|---|---:|---:|---:|---:|",
    ]
    for section, rows in sections:
        lines.append(
            f"| {section.get('name', section.get('id', ''))} | {len(rows)} | "
            f"{sum(r['status'] == 'direct' for r in rows)} | "
            f"{sum(r['status'] == 'fallback' for r in rows)} | "
            f"{sum(r['status'] == 'missing' for r in rows)} |"
        )
    lines += ["", "## Detail", ""]
    for section, rows in sections:
        lines.append(f"### {section.get('name', section.get('id', ''))}")
        for row in rows:
            extra = ""
            if row["groups"]:
                extra = " groups=" + ",".join(row["groups"])
            if row["candidates"]:
                extra += " candidates=" + ",".join(row["candidates"])
            lines.append(
                f"- `{row['id']}` tag `{row['tag']}` inst {row['inst']} "
                f"{row['bytes']}B — **{row['status']}** {row['fields']} field(s){extra}"
            )
        lines.append("")
    pathlib.Path(args.out).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {args.out}: records={total} direct={direct} fallback={fallback} missing={missing}")


if __name__ == "__main__":
    main()
