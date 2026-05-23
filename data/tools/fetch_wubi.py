#!/usr/bin/env python3
"""Download and convert rime-wubi86-jidian dictionary to CxxIME SQLite format.

Usage:
    python fetch_wubi.py [--output wubi86.dict.db]

Downloads the Wubi 86 dictionary from KyleBing/rime-wubi86-jidian on GitHub
and converts to CxxIME SQLite format.

Source: https://github.com/KyleBing/rime-wubi86-jidian (1500+ stars)

Requires: Python 3.6+, urllib (stdlib)
"""

import sys
import os
import sqlite3
import argparse
import urllib.request
import zipfile
import io

REPO_ZIP_URL = "https://github.com/KyleBing/rime-wubi86-jidian/archive/refs/heads/master.zip"

# Dict files to extract (in priority order)
DICT_FILES = [
    "wubi86_jidian.dict.yaml",
    "wubi86_jidian_extra.dict.yaml",
]


def parse_rime_dict(content):
    """Parse RIME dict.yaml content, yielding (text, code, frequency) tuples.

    RIME format: text<tab>code<tab>weight[<tab>stem]
    """
    in_header = True
    for line in content.splitlines():
        line = line.rstrip()
        if not line:
            continue
        if in_header:
            if line == "...":
                in_header = False
                continue
            if line.startswith("#") or line.startswith("---"):
                continue
            if "\t" not in line and ":" in line:
                continue
            in_header = False

        if line.startswith("#"):
            continue

        parts = line.split("\t")
        if len(parts) >= 2:
            text = parts[0].strip()
            code = parts[1].strip()
            freq = 0
            if len(parts) > 2:
                try:
                    freq = int(parts[2].strip())
                except ValueError:
                    pass
            if text and code:
                yield (text, code, freq)


def download_and_parse():
    """Download zip and extract dict entries."""
    print("  Downloading rime-wubi86-jidian repository...")
    req = urllib.request.Request(REPO_ZIP_URL, headers={"User-Agent": "CxxIME/0.1"})
    with urllib.request.urlopen(req, timeout=120) as resp:
        zip_data = resp.read()

    entries = []
    with zipfile.ZipFile(io.BytesIO(zip_data)) as zf:
        for dict_file in DICT_FILES:
            for name in zf.namelist():
                if name.endswith(f"/{dict_file}"):
                    print(f"  Extracting: {dict_file}")
                    with zf.open(name) as f:
                        content = f.read().decode("utf-8")
                        count = 0
                        for entry in parse_rime_dict(content):
                            entries.append(entry)
                            count += 1
                        print(f"    -> {count:,} entries")
                    break

    return entries


def create_sqlite_db(output_path, entries):
    """Create SQLite dictionary from parsed entries."""
    # Deduplicate: keep highest frequency for each (text, code) pair
    seen = {}
    for text, code, freq in entries:
        key = (text, code)
        if key not in seen or freq > seen[key]:
            seen[key] = freq

    # Sort by frequency descending
    sorted_entries = sorted(seen.items(), key=lambda x: -x[1])

    if os.path.exists(output_path):
        os.remove(output_path)

    conn = sqlite3.connect(output_path)
    cur = conn.cursor()

    cur.execute("""
        CREATE TABLE dict (
            id INTEGER PRIMARY KEY,
            text TEXT NOT NULL,
            code TEXT NOT NULL,
            frequency INTEGER DEFAULT 0
        )
    """)
    cur.execute("CREATE INDEX idx_code ON dict(code)")

    cur.executemany(
        "INSERT INTO dict (text, code, frequency) VALUES (?, ?, ?)",
        [(text, code, freq) for (text, code), freq in sorted_entries],
    )

    conn.commit()
    count = cur.execute("SELECT COUNT(*) FROM dict").fetchone()[0]
    conn.close()
    return count


def main():
    parser = argparse.ArgumentParser(
        description="Download and convert Wubi 86 dictionary to CxxIME format"
    )
    parser.add_argument(
        "--output", "-o",
        default="wubi86.dict.db",
        help="Output SQLite database path (default: wubi86.dict.db)",
    )
    args = parser.parse_args()

    print("=== CxxIME Wubi 86 Dictionary Fetcher ===")
    print(f"  Source: KyleBing/rime-wubi86-jidian")
    print(f"  Output: {args.output}")
    print()

    # Download
    print("[1/2] Downloading dictionary...")
    entries = download_and_parse()
    if not entries:
        print("\nERROR: Could not download dictionary data.")
        sys.exit(1)
    print(f"  Total: {len(entries):,} raw entries.")

    # Convert
    print(f"\n[2/2] Converting to SQLite...")
    count = create_sqlite_db(args.output, entries)
    print(f"  Written {count:,} entries to {args.output}")

    # Stats
    size_mb = os.path.getsize(args.output) / (1024 * 1024)
    print(f"  File size: {size_mb:.1f} MB")

    print(f"\n=== Done! Dictionary saved to: {args.output} ===")


if __name__ == "__main__":
    main()
