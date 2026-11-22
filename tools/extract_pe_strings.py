#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT
"""Extract Windows RT_STRING resources from a PE image.

Usage:
  extract_pe_strings.py <exe-or-dll> [out.json]

Requires pefile. The field registry uses PatPortDepot.exe string IDs at
field_id + 16.
"""

import json
import struct
import sys
from pathlib import Path

import pefile


def extract(path):
    pe = pefile.PE(str(path), fast_load=True)
    pe.parse_data_directories(
        directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]]
    )
    strings = {}
    for entry in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if entry.id != 6:
            continue
        for block in entry.directory.entries:
            for lang in block.directory.entries:
                data = lang.data.struct
                raw = pe.get_data(data.OffsetToData, data.Size)
                offset = 0
                for index in range(16):
                    if offset + 2 > len(raw):
                        break
                    length = struct.unpack_from("<H", raw, offset)[0]
                    offset += 2
                    if length == 0:
                        text = ""
                    else:
                        text = raw[offset:offset + length * 2].decode(
                            "utf-16le", "replace"
                        )
                        offset += length * 2
                    strings[block.id * 16 + index] = text
    return strings


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else path.with_suffix(".strings.json")
    strings = extract(path)
    out.write_text(
        json.dumps({str(k): v for k, v in strings.items()}, ensure_ascii=False, indent=2)
        + "\n",
        encoding="utf-8",
    )
    print(f"{len(strings)} strings")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
