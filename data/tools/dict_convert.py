#!/usr/bin/env python3
"""Convert RIME YAML dictionary to CxxIME SQLite format.

Usage:
    python dict_convert.py input.yaml output.db [--user]

The input YAML should have the format:
    ---
    name: dict_name
    version: "1.0"
    ...
    # syllable\tcharacter\tfrequency
    ni	你	100
    ni	尼	80
    hao	好	90

With --user flag, creates a user dictionary (for frequency updates).
"""

import sys
import sqlite3
import argparse

def parse_rime_yaml(filepath):
    """Parse a RIME YAML dict file, returning (metadata, entries)."""
    metadata = {}
    entries = []
    in_header = True

    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line:
                continue

            if in_header:
                if line == '...':
                    in_header = False
                    continue
                if line.startswith('#') or line.startswith('---'):
                    continue
                if '\t' not in line and ':' in line:
                    key, _, value = line.partition(':')
                    metadata[key.strip()] = value.strip()
                    continue
                # Fall through if we hit data before '...'
                in_header = False

            if line.startswith('#'):
                continue

            parts = line.split('\t')
            if len(parts) >= 2:
                syllable = parts[0].strip()
                text = parts[1].strip()
                freq = int(parts[2].strip()) if len(parts) > 2 and parts[2].strip().isdigit() else 0
                entries.append((syllable, text, freq))

    return metadata, entries


def create_system_db(output_path, entries):
    """Create a system dictionary SQLite database."""
    conn = sqlite3.connect(output_path)
    cur = conn.cursor()

    cur.execute('''
        CREATE TABLE IF NOT EXISTS dict (
            id INTEGER PRIMARY KEY,
            text TEXT NOT NULL,
            code TEXT NOT NULL,
            frequency INTEGER DEFAULT 0,
            syllable_ids TEXT
        )
    ''')
    cur.execute('CREATE INDEX IF NOT EXISTS idx_code ON dict(code)')

    cur.executemany(
        'INSERT INTO dict (text, code, frequency, syllable_ids) VALUES (?, ?, ?, ?)',
        [(text, syllable, freq, syllable) for syllable, text, freq in entries]
    )

    conn.commit()
    count = cur.execute('SELECT COUNT(*) FROM dict').fetchone()[0]
    conn.close()
    return count


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
