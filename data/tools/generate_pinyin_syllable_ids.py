#!/usr/bin/env python3
"""Generate colon-separated Pinyin syllable IDs for a SQLite dictionary.

Preserves explicit syllable boundaries. Missing or malformed annotations are
repaired only when the continuous code has one complete, unambiguous parse.
"""

import sys
import sqlite3

from dict_builder.pinyin_syllables import CANONICAL_SYLLABLES, validate_syllables


def load_syllables():
    return sorted(CANONICAL_SYLLABLES, key=lambda value: (-len(value), value))


def segment_code(code, syllables):
    """Return the unique full parse; never invent syllables or guess boundaries."""
    paths = [[] for _ in range(len(code) + 1)]
    paths[-1] = [()]
    for position in range(len(code) - 1, -1, -1):
        for syllable in syllables:
            if not code.startswith(syllable, position):
                continue
            for tail in paths[position + len(syllable)]:
                paths[position].append((syllable,) + tail)
                if len(paths[position]) == 2:
                    break
            if len(paths[position]) == 2:
                break
    if not code or not paths[0]:
        raise ValueError(f"cannot segment pinyin code: {code!r}")
    if len(paths[0]) != 1:
        raise ValueError(f"ambiguous pinyin code requires explicit syllables: {code!r}")
    return list(paths[0][0])




def update_syllable_ids(db_path):
    """Validate every row before publishing any changes to the dictionary."""
    syllables = load_syllables()
    connection = sqlite3.connect(db_path)
    try:
        columns = {row[1] for row in connection.execute("PRAGMA table_info(dict)")}
        annotation = "syllable_ids" if "syllable_ids" in columns else "NULL"
        rows = connection.execute(f"SELECT id, code, {annotation} FROM dict").fetchall()
        updates = []
        for row_id, code, existing in rows:
            if existing:
                try:
                    validate_syllables(code, existing)
                    continue
                except ValueError:
                    pass
            try:
                replacement = ":".join(segment_code(code, syllables))
            except ValueError as error:
                raise ValueError(f"dictionary row {row_id}: {error}") from error
            updates.append((replacement, row_id))
        with connection:
            connection.execute("BEGIN")
            if "syllable_ids" not in columns:
                connection.execute("ALTER TABLE dict ADD COLUMN syllable_ids TEXT")
            connection.executemany(
                "UPDATE dict SET syllable_ids = ? WHERE id = ?", updates
            )
        return len(updates)
    finally:
        connection.close()

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <dict.db>")
        sys.exit(1)

    try:
        count = update_syllable_ids(sys.argv[1])
    except (ValueError, sqlite3.Error) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
    print(f"Updated {count} rows; existing valid annotations were preserved.")

if __name__ == '__main__':
    main()
