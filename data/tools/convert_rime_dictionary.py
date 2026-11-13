#!/usr/bin/env python3
"""Convert RIME YAML dictionary to CxxIME SQLite format.

Usage:
    python convert_rime_dictionary.py input.yaml output.db [--user]

The input YAML should have the format:
    ---
    name: dict_name
    version: "1.0"
    ...
    # text\tcode\tweight
    你  ni  100
    尼  ni  80
    好  hao 90

With --user flag, creates a user dictionary (for frequency updates).
"""

import sys
import sqlite3
import argparse

from dict_builder.pinyin_source import parse_rime_entries
from fetch_pinyin_dictionary import create_sqlite_db

def parse_rime_yaml(filepath):
    """Read standard text/code/weight rows or an explicit columns declaration."""
    with open(filepath, "r", encoding="utf-8-sig") as source:
        return parse_rime_entries(source.read(), str(filepath))


def create_system_db(output_path, entries):
    return create_sqlite_db(output_path, entries)


def create_user_db(output_path):
    """Create an empty user dictionary SQLite database."""
    conn = sqlite3.connect(output_path)
    cur = conn.cursor()

    cur.execute('''
        CREATE TABLE IF NOT EXISTS user_dict (
            id INTEGER PRIMARY KEY,
            text TEXT NOT NULL,
            code TEXT NOT NULL,
            frequency INTEGER DEFAULT 1,
            last_used TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(text, code)
        )
    ''')
    cur.execute('CREATE INDEX IF NOT EXISTS idx_user_code ON user_dict(code)')

    conn.commit()
    conn.close()
    return 0


def main():
    parser = argparse.ArgumentParser(description='Convert RIME YAML dict to CxxIME SQLite format')
    parser.add_argument('input', help='Input YAML file (or empty for user dict)')
    parser.add_argument('output', help='Output SQLite database path')
    parser.add_argument('--user', action='store_true', help='Create user dictionary (empty)')
    args = parser.parse_args()

    if args.user:
        create_user_db(args.output)
        print(f'Created user dictionary: {args.output}')
    else:
        metadata, entries = parse_rime_yaml(args.input)
        count = create_system_db(args.output, entries)
        print(f'Created system dictionary: {args.output} ({count} entries)')


if __name__ == '__main__':
    main()
