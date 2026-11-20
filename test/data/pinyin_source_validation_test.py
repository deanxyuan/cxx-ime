#!/usr/bin/env python3
"""Protect reading boundaries and publication failures at the real tool entries."""

import csv
import hashlib
import io
import json
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import unittest
import zipfile
from contextlib import closing
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "data/tools"
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "scripts"))

from dict_builder.pinyin_source import parse_rime_entries
from dict_builder.pinyin_source_repair import CORRECTION_FIELDS, DEFAULT_RULES
from dict_builder.pinyin_source_repair import load_corrections, prepare_source, sha256_file
from dict_builder.pinyin_syllables import CANONICAL_SYLLABLES, validate_dictionary
from dict_builder.pinyin_syllables import validate_syllables
from generate_pinyin_syllables import DEFAULT_OUTPUT, generate
from generate_pinyin_spellings import generate_spellings
from fetch_pinyin_dictionary import create_sqlite_db
import convert_rime_dictionary as converter
import generate_pinyin_source_corrections as corrections_generator
import prepare_dictionary_bundle as bundle


class SourceValidationTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="cxxime-reading-test-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.database = self.root / "pinyin.dict.db"

    def test_declared_columns_and_converter_keep_word_reading_boundaries(self):
        source = (
            "---\n"
            "name: fixture\n"
            "columns:\n"
            "  - weight\n"
            "  - text\n"
            "  - code\n"
            "...\n"
            "100\t西安\txi an\n"
            "90\t先\txian\n"
        )
        path = self.root / "input.dict.yaml"
        path.write_text(source, encoding="utf-8")
        _, rows = converter.parse_rime_yaml(path)
        self.assertEqual(
            rows,
            [
                ("西安", "xian", 100, "xi:an"),
                ("先", "xian", 90, "xian"),
            ],
        )
        converter.create_system_db(self.database, rows)
        self.assertEqual(validate_dictionary(self.database), {"xi", "an", "xian"})

    def test_unannotated_and_invalid_rows_never_become_readings(self):
        sources = (
            "---\ncolumns: [text, weight]\n...\n词语\t100\n",
            "---\ncolumns:\n  - text\n  - weight\n...\n词语\t100\n",
            "一年\tyin i an\t100\n",
            "词语\t100\n",
            "词语\t\t100\n",
            "词语\tci yu\tbad-weight\n",
        )
        self.database.write_bytes(b"existing database")
        for source in sources:
            with self.subTest(source=source):
                with self.assertRaisesRegex(ValueError, "fixture.yaml"):
                    _, rows = parse_rime_entries(source, "fixture.yaml")
                    converter.create_system_db(self.database, rows)
                self.assertEqual(self.database.read_bytes(), b"existing database")

    def test_shared_inventory_preserves_rare_readings_and_excludes_input_aliases(self):
        for syllable in (
            "m",
            "n",
            "ng",
            "yo",
            "den",
            "kei",
            "tei",
            "biang",
            "cei",
            "fiao",
        ):
            self.assertEqual(validate_syllables(syllable, syllable), [syllable])
        for fragment in ("i", "u", "v", "r", "100", "lue", "nue"):
            with self.assertRaises(ValueError):
                validate_syllables(fragment, fragment)
        output = self.root / "syllables.inc"
        generate(output)
        self.assertEqual(
            output.read_text(encoding="utf-8"),
            DEFAULT_OUTPUT.read_text(encoding="utf-8"),
        )
        generated = {
            token.strip().strip('"')
            for line in output.read_text(encoding="utf-8").splitlines()[1:]
            for token in line.strip().split(",")
            if token.strip()
        }
        self.assertEqual(generated, CANONICAL_SYLLABLES)

    def test_direct_build_entries_reject_invalid_annotations_before_writing(self):
        create_sqlite_db(
            self.database,
            [("一年", "yinian", 100, "yi:nian")],
        )
        with closing(sqlite3.connect(self.database)) as connection, connection:
            connection.execute("UPDATE dict SET syllable_ids = 'yin:i:an'")
        original = self.database.read_bytes()
        commands = (
            [TOOLS / "generate_pinyin_spellings.py", self.database],
            [
                TOOLS / "build_runtime_dictionary.py",
                "--input",
                self.database,
                "--output",
                self.root / "output",
            ],
            [
                ROOT / "scripts/build_pinyin_topn.py",
                "--input",
                self.database,
                "--output",
                self.root / "output.topn.bin",
            ],
        )
        for command in commands:
            with self.subTest(tool=command[0]):
                result = subprocess.run(
                    [sys.executable, *map(str, command)],
                    capture_output=True,
                    encoding="utf-8",
                    errors="replace",
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("yin:i:an", result.stderr)
                self.assertEqual(self.database.read_bytes(), original)
                self.assertEqual(list(self.root.glob("output*")), [])

    def test_complete_bundle_rejects_invalid_source_without_touching_existing_files(self):
        create_sqlite_db(
            self.database,
            [("一年", "yinian", 100, "yi:nian")],
        )
        with closing(sqlite3.connect(self.database)) as connection, connection:
            connection.execute("UPDATE dict SET syllable_ids = 'yin:i:an'")
        with closing(sqlite3.connect(self.root / "wubi86.dict.db")) as connection, connection:
            connection.execute(
                "CREATE TABLE dict (id INTEGER PRIMARY KEY, text, code, frequency)"
            )
            connection.execute("INSERT INTO dict VALUES (1, '词', 'a', 1)")
        for name in ("symbol_catalog.json", "symbols.json"):
            shutil.copy2(ROOT / "data" / name, self.root / name)
        output = self.root / "bundle"
        output.mkdir()
        (output / "dictionary_manifest.json").write_bytes(b"old manifest")
        (output / "pinyin.dict.bin").write_bytes(b"old dictionary")
        builder = self.root / "unused-builder.exe"
        builder.write_bytes(b"not executed")
        with self.assertRaisesRegex(ValueError, "yin:i:an"):
            bundle.prepare_dictionary_bundle(
                str(self.root), str(output), topn_builder=str(builder)
            )
        self.assertEqual((output / "dictionary_manifest.json").read_bytes(), b"old manifest")
        self.assertEqual((output / "pinyin.dict.bin").read_bytes(), b"old dictionary")
        self.assertEqual(len(list(output.iterdir())), 2)

    def test_stale_spelling_table_cannot_publish_pseudo_syllables(self):
        create_sqlite_db(self.database, [("关瑞", "guanrui", 100, "guan:rui")])
        with closing(sqlite3.connect(self.database)) as connection, connection:
            connection.execute(
                "CREATE TABLE spellings (input, syllable, type, credibility)"
            )
            connection.execute("INSERT INTO spellings VALUES ('i', 'i', 0, 0)")
        result = subprocess.run(
            [
                sys.executable,
                str(TOOLS / "build_runtime_dictionary.py"),
                "--input",
                str(self.database),
                "--output",
                str(self.root / "output"),
            ],
            capture_output=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(list(self.root.glob("output*")), [])

    def test_spelling_table_write_failure_rolls_back_old_table(self):
        create_sqlite_db(
            self.database,
            [("关瑞", "guanrui", 100, "guan:rui")],
        )
        with closing(sqlite3.connect(self.database)) as connection, connection:
            connection.execute(
                "CREATE TABLE spellings (input, syllable, type, credibility)"
            )
            connection.execute("INSERT INTO spellings VALUES ('gr', 'guan', 0, 0)")

        class FailingConnection(sqlite3.Connection):
            def executemany(self, *args, **kwargs):
                raise sqlite3.OperationalError("injected insertion failure")

        connect = sqlite3.connect
        with mock.patch.object(
            sqlite3,
            "connect",
            side_effect=lambda path: connect(path, factory=FailingConnection),
        ):
            with self.assertRaisesRegex(sqlite3.OperationalError, "injected"):
                generate_spellings(
                    self.database,
                    ROOT / "data/schemas/pinyin.full-pinyin.schema.json",
                )
        with closing(sqlite3.connect(self.database)) as connection:
            self.assertEqual(
                connection.execute("SELECT * FROM spellings").fetchall(),
                [("gr", "guan", 0, 0)],
            )


class SourceRepairTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="cxxime-source-repair-test-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source = self.root / "source.db"
        self.output = self.root / "prepared.db"
        create_sqlite_db(
            self.source,
            [
                ("西安", "xian", 90, "xian"),
                ("先", "xian", 80, "xian"),
            ],
        )
        with closing(sqlite3.connect(self.source)) as connection, connection:
            connection.execute(
                "INSERT INTO dict VALUES (3, '未注音', '100', 0, '1:0:0')"
            )
            connection.execute("CREATE TABLE spellings (input, syllable)")
            connection.execute("INSERT INTO spellings VALUES ('i', 'i')")
        self.original = self.source.read_bytes()
        self.rules = self.root / "rules"
        self.rules.mkdir()
        buffer = io.StringIO(newline="")
        writer = csv.writer(buffer, delimiter="\t", lineterminator="\n")
        writer.writerow(CORRECTION_FIELDS)
        writer.writerow((1, "西安", "xian", "xian", "xi:an"))
        self.corrections = buffer.getvalue()
        self.manifest = {
            "schema": 1,
            "source_db_sha256": sha256_file(self.source),
            "before_rows": 3,
            "after_rows": 2,
            "boundary_repairs": 1,
            "unannotated_removed": 1,
        }
        self.write_rules()

    def write_rules(self):
        self.manifest["corrections_sha256"] = hashlib.sha256(
            self.corrections.encode("utf-8")
        ).hexdigest()
        (self.rules / "reading_corrections.tsv").write_text(
            self.corrections,
            encoding="utf-8",
            newline="\n",
        )
        (self.rules / "manifest.json").write_text(
            json.dumps(self.manifest),
            encoding="utf-8",
        )

    def test_database_and_zip_prepare_same_readings_without_touching_sources(self):
        archive = self.root / "source.db.zip"
        with zipfile.ZipFile(archive, "w") as output:
            output.write(self.source, "nested/pinyin.dict.db")
        archive_bytes = archive.read_bytes()
        for source in (self.source, archive):
            with self.subTest(source=source):
                stats = prepare_source(source, self.output, self.rules)
                self.assertEqual(
                    stats,
                    {
                        "known_source": True,
                        "boundary_repairs": 1,
                        "unannotated_removed": 1,
                    },
                )
                with closing(sqlite3.connect(self.output)) as connection:
                    self.assertEqual(
                        connection.execute("SELECT * FROM dict ORDER BY id").fetchall(),
                        [
                            (1, "西安", "xian", 90, "xi:an"),
                            (2, "先", "xian", 80, "xian"),
                        ],
                    )
                    self.assertEqual(
                        connection.execute(
                            "SELECT name FROM sqlite_master WHERE name = 'spellings'"
                        ).fetchall(),
                        [],
                    )
                self.assertEqual(self.source.read_bytes(), self.original)
                self.assertEqual(archive.read_bytes(), archive_bytes)
                self.output.unlink()

    def test_mismatched_rules_fail_before_publishing(self):
        for mismatch in ("checksum", "old_reading", "removed_count", "after_count"):
            with self.subTest(mismatch=mismatch):
                manifest = dict(self.manifest)
                corrections = self.corrections
                if mismatch == "checksum":
                    (self.rules / "reading_corrections.tsv").write_text(
                        "changed",
                        encoding="utf-8",
                    )
                elif mismatch == "old_reading":
                    self.corrections = corrections.replace(
                        "\txian\txi:an",
                        "\txi:a:n\txi:an",
                    )
                    self.write_rules()
                else:
                    field = (
                        "unannotated_removed"
                        if mismatch == "removed_count"
                        else "after_rows"
                    )
                    self.manifest[field] += 1
                    self.write_rules()
                with self.assertRaises(ValueError):
                    prepare_source(self.source, self.output, self.rules)
                self.assertFalse(self.output.exists())
                self.assertEqual(self.source.read_bytes(), self.original)
                self.manifest = manifest
                self.corrections = corrections
                self.write_rules()

    def test_existing_outputs_and_input_aliases_are_not_overwritten(self):
        self.output.write_bytes(b"existing")
        for output in (self.output, self.source):
            with self.subTest(output=output):
                with self.assertRaises(ValueError):
                    prepare_source(self.source, output, self.rules)
        self.assertEqual(self.output.read_bytes(), b"existing")
        self.assertEqual(self.source.read_bytes(), self.original)

    def test_unknown_sources_are_validated_without_guessing_readings(self):
        with closing(sqlite3.connect(self.source)) as connection, connection:
            connection.execute("DELETE FROM dict WHERE code = '100'")
            connection.execute("DROP TABLE spellings")
        stats = prepare_source(self.source, self.output, self.rules)
        self.assertFalse(stats["known_source"])
        self.assertEqual(self.source.read_bytes(), self.output.read_bytes())
        with closing(sqlite3.connect(self.output)) as connection:
            self.assertEqual(
                connection.execute(
                    "SELECT syllable_ids FROM dict WHERE text = '西安'"
                ).fetchone(),
                ("xian",),
            )
        self.output.unlink()
        with closing(sqlite3.connect(self.source)) as connection, connection:
            connection.execute(
                "INSERT INTO dict VALUES (3, '未注音', '100', 0, '1:0:0')"
            )
        with self.assertRaisesRegex(ValueError, "100"):
            prepare_source(self.source, self.output, self.rules)
        self.assertFalse(self.output.exists())

    def test_frozen_corrections_are_valid_and_accept_checkout_newlines(self):
        manifest = json.loads(
            (DEFAULT_RULES / "manifest.json").read_text(encoding="utf-8")
        )
        corrections = load_corrections(DEFAULT_RULES, manifest)
        self.assertEqual(len(corrections), 33943)
        path = self.rules / "reading_corrections.tsv"
        path.write_bytes(self.corrections.replace("\n", "\r\n").encode("utf-8"))
        self.assertEqual(
            load_corrections(self.rules, self.manifest),
            [(1, "西安", "xian", "xian", "xi:an")],
        )

    def test_maintenance_audit_requires_unique_explicit_evidence_for_every_row(self):
        readings = {
            ("西安", "xian"): {"xi:an"},
            ("先", "xian"): {"xian"},
        }
        self.assertEqual(
            corrections_generator.audit_database(self.source, readings, {"未注音"}),
            ([(1, "西安", "xian", "xian", "xi:an")], 3, 1),
        )
        cases = (
            ({("西安", "xian"): {"xi:an"}}, {"未注音"}),
            ({**readings, ("西安", "xian"): {"xi:an", "xian"}}, {"未注音"}),
            (readings, set()),
        )
        for reading_map, unannotated in cases:
            with self.subTest(readings=reading_map, unannotated=unannotated):
                with self.assertRaises(ValueError):
                    corrections_generator.audit_database(
                        self.source,
                        reading_map,
                        unannotated,
                    )

    def test_upstream_directory_rejects_missing_or_modified_source_bytes(self):
        upstream = self.root / "upstream"
        dictionaries = upstream / "cn_dicts"
        dictionaries.mkdir(parents=True)
        source = dictionaries / "fixture.dict.yaml"
        original = "---\nname: fixture\n...\n西安\txi an\t1\n".encode("utf-8")
        expected = {source.name: hashlib.sha256(original).hexdigest()}
        with mock.patch.object(
            corrections_generator,
            "SOURCE_FILES",
            expected,
        ):
            with self.assertRaises(FileNotFoundError):
                corrections_generator.load_upstream(upstream)
            source.write_bytes(original)
            self.assertEqual(
                corrections_generator.load_upstream(upstream),
                ({("西安", "xian"): {"xi:an"}}, set()),
            )
            source.write_bytes(original.replace(b"xi an", b"xian"))
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                corrections_generator.load_upstream(upstream)


class BundlePublicationTest(unittest.TestCase):
    def test_failed_replacement_restores_bundle_and_success_publishes_manifest_last(self):
        with tempfile.TemporaryDirectory(prefix="cxxime-publish-test-") as directory:
            root = Path(directory)
            output = root / "bundle"
            stage = root / "stage"
            output.mkdir()
            stage.mkdir()
            names = (
                "pinyin.dict.bin",
                "pinyin.dict.idx",
                "dictionary_manifest.json",
            )
            for name in names:
                (output / name).write_bytes(b"old " + name.encode())
                (stage / name).write_bytes(b"new " + name.encode())
            (output / "user-notes.txt").write_bytes(b"unrelated")
            replace = bundle.os.replace

            for error_type in (OSError, KeyboardInterrupt):
                for source_dir in (output, stage):
                    for after_move in (False, True):
                        with self.subTest(
                            error=error_type,
                            source=source_dir.name,
                            after_move=after_move,
                        ):
                            def fail_once(source, destination):
                                if Path(source) == source_dir / "pinyin.dict.idx":
                                    if after_move:
                                        replace(source, destination)
                                    raise error_type("injected publication failure")
                                return replace(source, destination)

                            with mock.patch.object(
                                bundle.os,
                                "replace",
                                side_effect=fail_once,
                            ):
                                with self.assertRaisesRegex(error_type, "injected"):
                                    bundle.publish_dictionary_files(
                                        [str(stage / name) for name in names],
                                        str(output),
                                    )
                            for name in names:
                                self.assertEqual(
                                    (output / name).read_bytes(),
                                    b"old " + name.encode(),
                                )
                                (stage / name).write_bytes(b"new " + name.encode())
            self.assertEqual(
                (output / "user-notes.txt").read_bytes(),
                b"unrelated",
            )
            self.assertEqual(len(list(output.iterdir())), 4)

            def check_manifest_order(source, destination):
                if Path(source) == stage / "dictionary_manifest.json":
                    for name in names[:2]:
                        self.assertEqual(
                            (output / name).read_bytes(),
                            b"new " + name.encode(),
                        )
                return replace(source, destination)

            with mock.patch.object(
                bundle.os,
                "replace",
                side_effect=check_manifest_order,
            ):
                bundle.publish_dictionary_files(
                    [str(stage / name) for name in names],
                    str(output),
                )
            for name in names:
                self.assertEqual(
                    (output / name).read_bytes(),
                    b"new " + name.encode(),
                )
            self.assertEqual(
                (output / "user-notes.txt").read_bytes(),
                b"unrelated",
            )


if __name__ == "__main__":
    unittest.main()
