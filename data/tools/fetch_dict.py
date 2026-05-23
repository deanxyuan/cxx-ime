#!/usr/bin/env python3
"""Download and convert rime-ice pinyin dictionary to CxxIME SQLite format.

Usage:
    python fetch_dict.py [--output pinyin.dict.db] [--limit 100000]

Downloads the pinyin dictionary from rime-ice (iDvel/rime-ice) on GitHub,
merges multiple dict files, and converts to CxxIME SQLite format.

Requires: Python 3.6+, urllib (stdlib)
"""

import sys
import os
import sqlite3
import argparse
import urllib.request
import urllib.error
import io
import zipfile

# rime-ice dictionary files to download (in priority order)
DICT_URLS = [
    # Core dictionaries from rime-ice
    "https://raw.githubusercontent.com/iDvel/rime-ice/main/cn_dicts/8105.dict.yaml",
    "https://raw.githubusercontent.com/iDvel/rime-ice/main/cn_dicts/49649.dict.yaml",
    "https://raw.githubusercontent.com/iDvel/rime-ice/main/cn_dicts/base.dict.yaml",
    "https://raw.githubusercontent.com/iDvel/rime-ice/main/cn_dicts/ext.dict.yaml",
]

# Alternative: download as zip archive
REPO_ZIP_URL = "https://github.com/iDvel/rime-ice/archive/refs/heads/main.zip"

# Fallback: rime-luna-pinyin
LUNA_URL = "https://raw.githubusercontent.com/rime/rime-luna-pinyin/master/luna_pinyin.dict.yaml"


def download_text(url, timeout=30):
    """Download text content from URL."""
    print(f"  Downloading: {url.split('/')[-1]}...")
    req = urllib.request.Request(url, headers={"User-Agent": "CxxIME/0.1"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read().decode("utf-8")


def parse_rime_dict(content):
    """Parse RIME dict.yaml content, yielding (text, code, frequency) tuples.

    RIME dict.yaml format: text<tab>code<tab>frequency
    text = Chinese characters, code = pinyin syllables, frequency = weight.

    Note: RIME uses space-separated syllables (e.g. "ni hao"), but CxxIME
    engine uses concatenated syllables (e.g. "nihao"). We strip spaces from codes.
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
            code = parts[1].strip().replace(" ", "")  # "ni hao" -> "nihao"
            freq = 0
            if len(parts) > 2:
                try:
                    freq = int(parts[2].strip())
                except ValueError:
                    pass
            if text and code:
                yield (text, code, freq)


def download_dicts_from_zip():
    """Download all dict files from rime-ice zip archive."""
    print("  Downloading rime-ice repository as zip archive...")
    req = urllib.request.Request(REPO_ZIP_URL, headers={"User-Agent": "CxxIME/0.1"})
    with urllib.request.urlopen(req, timeout=120) as resp:
        zip_data = resp.read()

    entries = []
    with zipfile.ZipFile(io.BytesIO(zip_data)) as zf:
        for name in zf.namelist():
            if "/cn_dicts/" in name and name.endswith(".dict.yaml"):
                print(f"  Extracting: {name.split('/')[-1]}")
                with zf.open(name) as f:
                    content = f.read().decode("utf-8")
                    entries.extend(parse_rime_dict(content))

    return entries


def download_dicts_individual():
    """Download dict files one by one."""
    entries = []
    for url in DICT_URLS:
        try:
            content = download_text(url)
            entries.extend(parse_rime_dict(content))
        except (urllib.error.URLError, urllib.error.HTTPError) as e:
            print(f"  WARNING: Failed to download {url}: {e}")
            continue
    return entries


def try_fallback():
    """Try fallback dictionary (rime-luna-pinyin)."""
    print("  Trying fallback: rime-luna-pinyin...")
    try:
        content = download_text(LUNA_URL)
        return list(parse_rime_dict(content))
    except Exception as e:
        print(f"  Fallback also failed: {e}")
        return []


def create_sqlite_db(output_path, entries, limit=0):
    """Create SQLite dictionary from parsed entries."""
    # Deduplicate: keep highest frequency for each (text, code) pair
    seen = {}
    for text, code, freq in entries:
        key = (text, code)
        if key not in seen or freq > seen[key]:
            seen[key] = freq

    # Sort by frequency descending for better query performance
    sorted_entries = sorted(seen.items(), key=lambda x: -x[1])

    if limit > 0:
        sorted_entries = sorted_entries[:limit]

    # Write to SQLite
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
        description="Download and convert rime-ice pinyin dictionary to CxxIME format"
    )
    parser.add_argument(
        "--output", "-o",
        default="pinyin.dict.db",
        help="Output SQLite database path (default: pinyin.dict.db)",
    )
    parser.add_argument(
        "--limit", "-l",
        type=int, default=0,
        help="Max number of entries (0 = unlimited)",
    )
    parser.add_argument(
        "--method", "-m",
        choices=["zip", "individual"],
        default="zip",
        help="Download method: zip archive or individual files (default: zip)",
    )
    args = parser.parse_args()

    print("=== CxxIME Dictionary Fetcher ===")
    print(f"  Source: rime-ice (iDvel/rime-ice)")
    print(f"  Output: {args.output}")
    print()

    # Download
    print("[1/2] Downloading dictionaries...")
    entries = []
    try:
        if args.method == "zip":
            entries = download_dicts_from_zip()
        else:
            entries = download_dicts_individual()
    except Exception as e:
        print(f"  ERROR: Download failed: {e}")

    if not entries:
        print("  Primary download failed, trying fallback...")
        entries = try_fallback()

    if not entries:
        print("\nERROR: Could not download any dictionary data.")
        print("Please check your network connection and try again.")
        print("Alternative: manually download from https://github.com/iDvel/rime-ice")
        sys.exit(1)

    print(f"  Downloaded {len(entries)} raw entries.")

    # Convert
    print(f"\n[2/2] Converting to SQLite...")
    count = create_sqlite_db(args.output, entries, args.limit)
    print(f"  Written {count} entries to {args.output}")

    # Stats
    size_mb = os.path.getsize(args.output) / (1024 * 1024)
    print(f"  File size: {size_mb:.1f} MB")

    print(f"\n=== Done! Dictionary saved to: {args.output} ===")
    print(f"Copy this file to the server directory or install directory.")


if __name__ == "__main__":
    main()
