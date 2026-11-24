#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT
"""Print zone/channel counts and the 0x0FFC header for local backups."""

import argparse
import glob
import os
import re
import subprocess


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("directory")
    ap.add_argument("--cli", required=True)
    args = ap.parse_args()
    for path in sorted(glob.glob(os.path.join(args.directory, "*.cpg"))):
        try:
            report = subprocess.check_output(
                [args.cli, "--report", path], text=True, errors="replace", timeout=20
            )
        except Exception:
            continue
        zones = len(re.findall(r"^  Zone ", report, re.M))
        channels = sum(int(x) for x in re.findall(r"\((\d+) channels\)", report))
        try:
            hexout = subprocess.check_output(
                [args.cli, "--hex", path, "0FFC", "0000"], text=True, errors="replace"
            )
        except subprocess.CalledProcessError:
            print(f"{os.path.basename(path):45} zones={zones} chans={channels:2} 0FFC=missing")
            continue
        match = re.search(r"0000  ((?:[0-9A-F]{2} ?){1,15})", hexout)
        payload = match.group(1).strip() if match else ""
        print(f"{os.path.basename(path):45} zones={zones} chans={channels:2} 0FFC={payload}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

