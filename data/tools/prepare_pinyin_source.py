#!/usr/bin/env python3
"""Prepare a validated private Pinyin SQLite source without changing the input."""

import argparse
import json
import sqlite3
import zipfile

from dict_builder.pinyin_source_repair import prepare_source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        required=True,
        help="Original .dict.db.zip or explicitly annotated .dict.db",
    )
    parser.add_argument(
        "--output",
        required=True,
        help="New SQLite .db path; existing files are never overwritten",
    )
    args = parser.parse_args()
    try:
        stats = prepare_source(args.input, args.output)
    except (OSError, ValueError, sqlite3.Error, zipfile.BadZipFile) as error:
        parser.exit(1, f"error: {error}\n")
    print(json.dumps(stats, ensure_ascii=False))


if __name__ == "__main__":
    main()
