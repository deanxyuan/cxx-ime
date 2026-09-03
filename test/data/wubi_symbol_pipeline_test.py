#!/usr/bin/env python3

import importlib.util
import json
import pathlib
import sqlite3
import sys
import tempfile
import zipfile


ROOT = pathlib.Path(__file__).parents[2]
TOOLS_DIR = ROOT / "data" / "tools"
sys.path.insert(0, str(TOOLS_DIR))

MODULE_PATH = TOOLS_DIR / "split_wubi_symbols.py"
SPEC = importlib.util.spec_from_file_location("split_wubi_symbols", MODULE_PATH)
PARTITION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PARTITION)


def make_entries():
    entries = [("鸡米花", "coaw", 100)]
    for _, _, source_codes in PARTITION.SYMBOL_GROUPS:
        entries.append((f"symbol-{source_codes[0]}", source_codes[0], 10))
    entries.append(("duplicate", "cobd", 9))
    entries.append(("duplicate", "cobd", 8))
    return entries


def write_database(path, entries):
    connection = sqlite3.connect(path)
    try:
        connection.execute(
            "CREATE TABLE dict ("
            "id INTEGER PRIMARY KEY, text TEXT NOT NULL, code TEXT NOT NULL, "
            "frequency INTEGER DEFAULT 0, syllable_ids TEXT)"
        )
        connection.execute("CREATE INDEX idx_code ON dict(code)")
        connection.executemany(
            "INSERT INTO dict (text, code, frequency, syllable_ids) VALUES (?, ?, ?, ?)",
            [(text, code, frequency, code) for text, code, frequency in entries],
        )
        connection.commit()
    finally:
        connection.close()


def test_zip_extraction():
    with tempfile.TemporaryDirectory(prefix="cxxime-wubi-test-") as temp_dir:
        temp_dir = pathlib.Path(temp_dir)
        database_path = temp_dir / "source.db"
        archive_path = temp_dir / "wubi86.dict.db.zip"
        symbols_path = temp_dir / "symbols.json"
        write_database(database_path, make_entries())
        with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.write(database_path, "wubi86.dict.db")
        source_bytes = archive_path.read_bytes()

        total, excluded, _ = PARTITION.split(archive_path, symbols_path)
        assert archive_path.read_bytes() == source_bytes
        assert total == len(make_entries())
        assert excluded == len(PARTITION.SYMBOL_GROUPS) + 2
        symbols = json.loads(symbols_path.read_text(encoding="utf-8"))
        assert symbols["categories"][0]["candidates"] == ["symbol-cobd", "duplicate"]
        category_lines = [
            line for line in symbols_path.read_text(encoding="utf-8").splitlines()
            if line.startswith('        {"code":')
        ]
        assert len(category_lines) == len(PARTITION.SYMBOL_GROUPS)

        with zipfile.ZipFile(archive_path) as archive:
            assert archive.namelist() == ["wubi86.dict.db"]


def test_temporary_database_filter():
    with tempfile.TemporaryDirectory(prefix="cxxime-wubi-test-") as temp_dir:
        source_path = pathlib.Path(temp_dir) / "source.db"
        filtered_path = pathlib.Path(temp_dir) / "filtered.db"
        write_database(source_path, make_entries())
        source_bytes = source_path.read_bytes()
        symbols_path = pathlib.Path(temp_dir) / "symbols.json"
        total, excluded, _ = PARTITION.split(source_path, symbols_path, filtered_path)
        assert total == len(make_entries())
        assert excluded == len(PARTITION.SYMBOL_GROUPS) + 2
        assert source_path.read_bytes() == source_bytes
        assert PARTITION.read_entries(filtered_path) == [("鸡米花", "coaw", 100)]


def test_rejects_source_overwrite():
    with tempfile.TemporaryDirectory(prefix="cxxime-wubi-test-") as temp_dir:
        database_path = pathlib.Path(temp_dir) / "source.db"
        write_database(database_path, make_entries())
        try:
            PARTITION.split(database_path, database_path)
        except ValueError as error:
            assert "must not overwrite" in str(error)
        else:
            raise AssertionError("dictionary source overwrite was not rejected")


def test_compact_json_wraps_large_categories():
    with tempfile.TemporaryDirectory(prefix="cxxime-wubi-test-") as temp_dir:
        symbols_path = pathlib.Path(temp_dir) / "symbols.json"
        candidates = [f"symbol-{index}" for index in range(25)]
        categories = [{"code": "bd", "name": "标点", "candidates": candidates}]
        PARTITION.write_symbols_json(symbols_path, categories)
        lines = symbols_path.read_text(encoding="utf-8").splitlines()
        candidate_lines = [line for line in lines if line.startswith("            ")]
        assert len(candidate_lines) == 3
        assert json.loads(symbols_path.read_text(encoding="utf-8"))["categories"][0][
            "candidates"
        ] == candidates


def main():
    categories, excluded = PARTITION.partition_entries(make_entries())
    assert excluded == len(PARTITION.SYMBOL_GROUPS) + 2
    assert categories[0]["code"] == "bd"
    assert categories[0]["candidates"] == ["symbol-cobd", "duplicate"]

    try:
        PARTITION.partition_entries(make_entries() + [("unknown", "cozz", 1)])
    except ValueError as error:
        assert "unreviewed upstream co* code" in str(error)
    else:
        raise AssertionError("unknown co* code was not rejected")

    test_zip_extraction()
    test_temporary_database_filter()
    test_rejects_source_overwrite()
    test_compact_json_wraps_large_categories()


if __name__ == "__main__":
    main()
