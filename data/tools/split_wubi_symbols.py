#!/usr/bin/env python3
"""Split extension symbols from a CxxIME Wubi dictionary source."""

import argparse
import json
import pathlib
import shutil
import sqlite3
import tempfile
import zipfile


SYMBOL_GROUPS = [
    ("bd", "标点", ("cobd",)),
    (
        "sz",
        "数字序号",
        ("coy", "coys", "cof", "coks", "cod", "cods", "coo", "coos", "codl", "coxl"),
    ),
    ("sx", "数学符号", ("cosx",)),
    ("jt", "箭头", ("coj", "cojt")),
    ("xl", "希腊字母", ("coxx", "codx")),
    ("ew", "西里尔字母", ("coxe", "code")),
    ("rw", "日文假名", ("copj", "cojp")),
    ("dn", "电脑符号", ("coap", "cocm", "cokg", "cosc", "cohc")),
    ("dw", "单位", ("cocc", "codw")),
    ("hb", "货币", ("cohb",)),
    ("ts", "特殊符号", ("cots",)),
    ("zy", "注音", ("cooy",)),
    (
        "py",
        "拼音字母",
        ("copy", "copa", "cope", "copi", "copo", "copu", "copv"),
    ),
    ("pp", "偏旁", ("copp",)),
]

SYMBOL_CODES = {
    source_code
    for _, _, source_codes in SYMBOL_GROUPS
    for source_code in source_codes
}

# Genuine Wubi entries in the same prefix range, verified against the 4.3 data.
ORDINARY_CO_CODES = {"coaw", "cogw", "coqh"}
CANDIDATES_PER_LINE = 10


def partition_entries(entries):
    """Return symbol categories after validating every co* source code."""
    symbol_values = {code: [] for code in SYMBOL_CODES}
    seen_symbols = {code: set() for code in SYMBOL_CODES}
    excluded_count = 0

    for text, code, _ in entries:
        if code in SYMBOL_CODES:
            excluded_count += 1
            if text not in seen_symbols[code]:
                symbol_values[code].append(text)
                seen_symbols[code].add(text)
        elif code.startswith("co") and code not in ORDINARY_CO_CODES:
            raise ValueError(f"unreviewed upstream co* code: {code} ({text})")

    categories = []
    for group_code, name, source_codes in SYMBOL_GROUPS:
        candidates = []
        seen_candidates = set()
        for source_code in source_codes:
            for text in symbol_values[source_code]:
                if text not in seen_candidates:
                    candidates.append(text)
                    seen_candidates.add(text)
        if not candidates:
            raise ValueError(f"symbol category has no candidates: {group_code}")
        categories.append({
            "code": group_code,
            "name": name,
            "candidates": candidates,
        })
    return categories, excluded_count


def read_entries(database_path):
    connection = sqlite3.connect(database_path)
    try:
        return connection.execute(
            "SELECT text, code, frequency FROM dict ORDER BY id"
        ).fetchall()
    finally:
        connection.close()


def write_symbols_json(output_path, categories):
    with open(output_path, "w", encoding="utf-8", newline="\n") as stream:
        stream.write("{\n")
        stream.write('    "version": 1,\n')
        stream.write('    "source": "KyleBing/rime-wubi86-jidian",\n')
        stream.write('    "license": "Apache-2.0",\n')
        stream.write('    "categories": [\n')
        for index, category in enumerate(categories):
            suffix = ",\n" if index + 1 < len(categories) else "\n"
            if len(category["candidates"]) <= CANDIDATES_PER_LINE:
                stream.write("        ")
                stream.write(json.dumps(category, ensure_ascii=False, separators=(", ", ": ")))
                stream.write(suffix)
                continue

            stream.write("        {")
            stream.write(f'"code": {json.dumps(category["code"], ensure_ascii=False)}, ')
            stream.write(f'"name": {json.dumps(category["name"], ensure_ascii=False)}, ')
            stream.write('"candidates": [\n')
            candidates = category["candidates"]
            for offset in range(0, len(candidates), CANDIDATES_PER_LINE):
                chunk = candidates[offset:offset + CANDIDATES_PER_LINE]
                stream.write("            ")
                stream.write(", ".join(json.dumps(value, ensure_ascii=False) for value in chunk))
                stream.write(",\n" if offset + len(chunk) < len(candidates) else "\n")
            stream.write("        ]}")
            stream.write(suffix)
        stream.write("    ]\n")
        stream.write("}\n")


def write_filtered_database(input_path, output_path):
    shutil.copy2(input_path, output_path)
    codes = tuple(sorted(SYMBOL_CODES))
    placeholders = ",".join("?" for _ in codes)
    connection = sqlite3.connect(output_path)
    try:
        cursor = connection.execute(f"DELETE FROM dict WHERE code IN ({placeholders})", codes)
        removed_count = cursor.rowcount
        connection.commit()
        connection.execute("VACUUM")
        return removed_count
    finally:
        connection.close()


def read_database_from_input(input_path, destination):
    if input_path.suffix.lower() != ".zip":
        shutil.copy2(input_path, destination)
        return

    with zipfile.ZipFile(input_path) as archive:
        members = [
            item for item in archive.infolist()
            if pathlib.PurePosixPath(item.filename).name.endswith(".db")
        ]
        if len(members) != 1:
            raise ValueError("input ZIP must contain exactly one SQLite database")
        with archive.open(members[0]) as source, open(destination, "wb") as target:
            shutil.copyfileobj(source, target)


def split(input_path, symbols_output, filtered_output=None):
    input_path = pathlib.Path(input_path)
    symbols_output = pathlib.Path(symbols_output)
    filtered_output = pathlib.Path(filtered_output) if filtered_output else None
    if input_path.resolve() == symbols_output.resolve():
        raise ValueError("symbols output must not overwrite the dictionary source")
    if filtered_output:
        if filtered_output.suffix.lower() == ".zip":
            raise ValueError("filtered output must be an SQLite database, not a ZIP")
        if input_path.resolve() == filtered_output.resolve():
            raise ValueError("filtered output must not overwrite the dictionary source")
        if symbols_output.resolve() == filtered_output.resolve():
            raise ValueError("symbols and filtered outputs must use different paths")

    with tempfile.TemporaryDirectory(prefix="cxxime-wubi-") as temp_dir:
        raw_database = pathlib.Path(temp_dir) / "raw.db"
        read_database_from_input(input_path, raw_database)
        entries = read_entries(raw_database)
        categories, excluded_count = partition_entries(entries)
        if filtered_output:
            removed_count = write_filtered_database(raw_database, filtered_output)
            if removed_count != excluded_count:
                raise RuntimeError(
                    f"symbol row count mismatch: expected {excluded_count}, removed {removed_count}"
                )
        write_symbols_json(symbols_output, categories)
    return len(entries), excluded_count, categories


def main():
    parser = argparse.ArgumentParser(
        description="Split extension symbols from a CxxIME Wubi dictionary source"
    )
    parser.add_argument("--input", required=True, help="Source SQLite DB or ZIP")
    parser.add_argument("--symbols-output", required=True, help="Generated symbols.json")
    parser.add_argument(
        "--filtered-output",
        help="Optional filtered SQLite database",
    )
    args = parser.parse_args()

    total, excluded, categories = split(
        args.input,
        args.symbols_output,
        args.filtered_output,
    )
    symbol_count = sum(len(category["candidates"]) for category in categories)
    print(f"Input rows: {total:,}")
    print(f"Split {excluded:,} source rows into {symbol_count:,} unique symbols")


if __name__ == "__main__":
    main()
