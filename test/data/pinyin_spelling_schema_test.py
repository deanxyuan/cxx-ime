#!/usr/bin/env python3

import json
import os
import sqlite3
import sys
import tempfile
import unittest
import zipfile
from contextlib import closing
from pathlib import Path
from unittest import mock


ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA_TOOLS = os.path.join(ROOT, "data", "tools")
FULL_PINYIN_SCHEMA_PATH = os.path.join(
    ROOT, "data", "schemas", "pinyin.full-pinyin.schema.json"
)
SCHEMA_CASES = (
    (
        "microsoft_shuangpin",
        "pinyin.microsoft-shuangpin.schema.json",
        {
            "ni": "ni",
            "hk": "hao",
            "vs": "zhong",
            "go": "guo",
            "ui": "shi",
            "jx": "jie",
            "y;": "ying",
            "oa": "a",
            "ol": "ai",
            "or": "er",
        },
    ),
    (
        "xiaohe_shuangpin",
        "pinyin.xiaohe-shuangpin.schema.json",
        {
            "ni": "ni",
            "hc": "hao",
            "vs": "zhong",
            "go": "guo",
            "ui": "shi",
            "jp": "jie",
            "yk": "ying",
            "ju": "ju",
            "yu": "yu",
            "aa": "a",
            "an": "an",
            "en": "en",
            "ou": "ou",
            "ad": "ai",
            "er": "er",
        },
    ),
    (
        "ziranma_shuangpin",
        "pinyin.ziranma-shuangpin.schema.json",
        {
            "ni": "ni",
            "hk": "hao",
            "vs": "zhong",
            "go": "guo",
            "ui": "shi",
            "jx": "jie",
            "yy": "ying",
            "ju": "ju",
            "yu": "yu",
            "aa": "a",
            "an": "an",
            "en": "en",
            "ou": "ou",
            "al": "ai",
            "er": "er",
        },
    ),
    (
        "sogou_shuangpin",
        "pinyin.sogou-shuangpin.schema.json",
        {
            "ni": "ni",
            "hk": "hao",
            "vs": "zhong",
            "go": "guo",
            "ui": "shi",
            "jx": "jie",
            "y;": "ying",
            "ju": "ju",
            "yu": "yu",
            "oa": "a",
            "ol": "ai",
            "or": "er",
        },
    ),
)
sys.path.insert(0, DATA_TOOLS)

from dict_builder.pinyin_spellings import PatriciaTrie
from generate_pinyin_spellings import build_shuangpin_script, load_schema
from fetch_pinyin_dictionary import create_sqlite_db, parse_rime_dict
import fetch_pinyin_dictionary as fetcher
from generate_pinyin_syllable_ids import load_syllables, segment_code, update_syllable_ids


def production_syllabary():
    archive_path = os.path.join(ROOT, "data", "pinyin.dict.db.zip")
    with zipfile.ZipFile(archive_path) as archive, tempfile.TemporaryDirectory() as directory:
        database_path = archive.extract(archive.namelist()[0], directory)
        connection = sqlite3.connect(database_path)
        try:
            rows = connection.execute("SELECT DISTINCT syllable_ids FROM dict")
            return {
                syllable
                for (value,) in rows
                if value
                for syllable in value.split(":")
                if syllable
            }
        finally:
            connection.close()


def serialize_script(script):
    trie = PatriciaTrie()
    for input_code, spellings in sorted(script.items()):
        for spelling in sorted(spellings, key=lambda item: (item.syllable, item.type)):
            trie.insert(
                input_code,
                spelling.syllable,
                spelling.type,
                spelling.credibility,
            )
    return trie.serialize()


class PinyinSpellingSchemaTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.syllabary = production_syllabary()
        cls.schemas = {}
        cls.scripts = {}
        for scheme_id, filename, _ in SCHEMA_CASES:
            schema = load_schema(os.path.join(ROOT, "data", "schemas", filename))
            cls.schemas[scheme_id] = schema
            cls.scripts[scheme_id] = build_shuangpin_script(cls.syllabary, schema)

    def assert_normal_mapping(self, scheme_id, code, syllable):
        script = self.scripts[scheme_id]
        self.assertIn(code, script)
        self.assertTrue(
            any(item.syllable == syllable and item.type == 0 for item in script[code]),
            f"{scheme_id}: {code} does not normally map to {syllable}",
        )

    def test_built_in_schemes_match_golden_mappings(self):
        for scheme_id, _, mappings in SCHEMA_CASES:
            self.assertEqual(self.schemas[scheme_id]["scheme_id"], scheme_id)
            for code, syllable in mappings.items():
                with self.subTest(scheme=scheme_id, code=code, syllable=syllable):
                    self.assert_normal_mapping(scheme_id, code, syllable)

    def test_schema_contract_rejects_unknown_types_and_fields(self):
        schema = self.schemas["microsoft_shuangpin"]
        full_pinyin = load_schema(FULL_PINYIN_SCHEMA_PATH)
        self.assertEqual(full_pinyin["scheme_id"], "full_pinyin")
        self.assertEqual(full_pinyin["speller"]["type"], "full_pinyin")

        unknown_root_field = json.loads(json.dumps(schema))
        unknown_root_field["display_name"] = "duplicate metadata"
        with self.assertRaisesRegex(ValueError, "unknown=\\['display_name'\\]"):
            build_shuangpin_script(self.syllabary, unknown_root_field)

        passthrough = json.loads(json.dumps(self.schemas["microsoft_shuangpin"]))
        passthrough["speller"]["passthrough_syllables"] = ["u"]
        with self.assertRaisesRegex(ValueError, "passthrough_syllables"):
            build_shuangpin_script(self.syllabary, passthrough)

        unknown_type = json.loads(json.dumps(schema))
        unknown_type["speller"]["type"] = "future_type"
        with self.assertRaisesRegex(ValueError, "unsupported speller.type"):
            build_shuangpin_script(self.syllabary, unknown_type)

        unknown_field = json.loads(json.dumps(schema))
        unknown_field["speller"]["future_option"] = True
        with self.assertRaisesRegex(ValueError, "unknown=\\['future_option'\\]"):
            build_shuangpin_script(self.syllabary, unknown_field)

        unsupported_key = json.loads(json.dumps(schema))
        unsupported_key["speller"]["alphabet"] += ","
        with self.assertRaisesRegex(ValueError, "unsupported by the input processor"):
            build_shuangpin_script(self.syllabary, unsupported_key)

    def test_declared_alternatives_and_collisions_are_explicit(self):
        for scheme_id, schema in self.schemas.items():
            for code, syllables in schema["speller"]["expected_collisions"].items():
                for syllable in syllables:
                    with self.subTest(scheme=scheme_id, code=code, syllable=syllable):
                        self.assert_normal_mapping(scheme_id, code, syllable)

        invalid = json.loads(json.dumps(self.schemas["microsoft_shuangpin"]))
        invalid["speller"]["expected_collisions"].pop("lt")
        with self.assertRaisesRegex(ValueError, "collisions differ"):
            build_shuangpin_script(self.syllabary, invalid)

    def test_unreachable_mapping_is_rejected(self):
        invalid = json.loads(json.dumps(self.schemas["microsoft_shuangpin"]))
        invalid["speller"]["finals"]["unused"] = "a"
        with self.assertRaisesRegex(ValueError, "unreachable shuangpin mappings"):
            build_shuangpin_script(self.syllabary, invalid)

    def test_fuzzy_rules_keep_normal_mappings_and_add_fuzzy_syllables(self):
        for scheme_id, script in self.scripts.items():
            self.assert_normal_mapping(scheme_id, "ni", "ni")
            self.assertTrue(
                any(item.syllable == "ni" and item.type == 1 for item in script["li"]),
                scheme_id,
            )

    def test_all_dictionary_syllables_are_covered_or_explicitly_ignored(self):
        for scheme_id, schema in self.schemas.items():
            ignored = set(schema["speller"]["ignored_syllables"])
            generated = {
                spelling.syllable
                for spellings in self.scripts[scheme_id].values()
                for spelling in spellings
                if spelling.type == 0
            }
            self.assertEqual(self.syllabary - ignored, generated, scheme_id)

    def test_legacy_fragments_cannot_become_normal_single_key_spellings(self):
        for scheme_id, script in self.scripts.items():
            for key in ("f", "g", "i", "l", "n", "r", "u", "v"):
                with self.subTest(scheme=scheme_id, key=key):
                    self.assertNotIn(key, script)
            # Explicit zero-initial mappings remain available.
            if scheme_id in ("microsoft_shuangpin", "sogou_shuangpin"):
                self.assert_normal_mapping(scheme_id, "o", "o")

    def test_binary_generation_is_deterministic(self):
        for scheme_id, schema in self.schemas.items():
            rebuilt = build_shuangpin_script(self.syllabary, schema)
            self.assertEqual(
                serialize_script(self.scripts[scheme_id]),
                serialize_script(rebuilt),
                scheme_id,
            )


class PinyinSourceAnnotationTest(unittest.TestCase):
    def test_import_preserves_explicit_boundaries_and_highest_frequency(self):
        source = (
            "---\nname: fixture\n...\n"
            "尽管\tjin guan\t100\n"
            "韩国\than guo\t90\n"
            "西安\txi an\t80\n"
            "先\txian\t70\n"
            "尽管\tjin guan\t120\n"
        )
        entries = list(parse_rime_dict(source))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pinyin.db"
            self.assertEqual(create_sqlite_db(path, entries), 4)
            with closing(sqlite3.connect(path)) as connection:
                rows = connection.execute(
                    "SELECT text, code, frequency, syllable_ids FROM dict ORDER BY id"
                ).fetchall()
            self.assertEqual(rows, [
                ("尽管", "jinguan", 120, "jin:guan"),
                ("韩国", "hanguo", 90, "han:guo"),
                ("西安", "xian", 80, "xi:an"),
                ("先", "xian", 70, "xian"),
            ])
            original = path.read_bytes()
            with self.assertRaisesRegex(ValueError, "invalid syllable annotation"):
                create_sqlite_db(path, [("尽管", "jinguan", 100, "jin:guo")])
            self.assertEqual(path.read_bytes(), original)

    def test_continuous_code_requires_one_complete_parse(self):
        syllables = load_syllables()
        self.assertEqual(segment_code("xingu", syllables), ["xin", "gu"])
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            segment_code("xian", syllables)
        with self.assertRaisesRegex(ValueError, "cannot segment"):
            segment_code("nihaob", syllables)
        with self.assertRaisesRegex(ValueError, "invalid syllable annotation"):
            list(parse_rime_dict("尽管\tjin 9guan\t100\n"))

    def test_explicit_rare_readings_are_not_limited_by_the_legacy_segmentor(self):
        source = "𤭢\tcei\t1000\n𰻝\tbiang\t1000\n覅\tfiao\t1000\n挼\trua\t1000\n"
        entries = list(parse_rime_dict(source))
        self.assertEqual([entry[3] for entry in entries], ["cei", "biang", "fiao", "rua"])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pinyin.db"
            self.assertEqual(create_sqlite_db(path, entries), 4)
            self.assertEqual(update_syllable_ids(path), 0)
            with closing(sqlite3.connect(path)) as connection:
                self.assertEqual(connection.execute(
                    "SELECT syllable_ids FROM dict ORDER BY id"
                ).fetchall(), [("cei",), ("biang",), ("fiao",), ("rua",)])

    def test_invalid_source_does_not_switch_dictionary_or_publish(self):
        with (
            mock.patch.object(sys, "argv", ["fetch_pinyin_dictionary.py"]),
            mock.patch.object(
                fetcher,
                "download_dicts_from_zip",
                side_effect=ValueError("invalid source"),
            ),
            mock.patch.object(fetcher, "try_fallback") as fallback,
            mock.patch.object(fetcher, "create_sqlite_db") as publish,
        ):
            with self.assertRaisesRegex(ValueError, "invalid source"):
                fetcher.main()
            fallback.assert_not_called()
            publish.assert_not_called()

    def test_maintenance_preserves_explicit_readings_and_rejects_partial_updates(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pinyin.db"
            create_sqlite_db(path, [("西安", "xian", 100, "xi:an")])
            with closing(sqlite3.connect(path)) as connection, connection:
                connection.execute(
                    "INSERT INTO dict (text, code, syllable_ids) VALUES (?, ?, ?)",
                    ("新股", "xingu", "xing::u"),
                )
            self.assertEqual(update_syllable_ids(path), 1)
            with closing(sqlite3.connect(path)) as connection, connection:
                self.assertEqual(connection.execute(
                    "SELECT syllable_ids FROM dict ORDER BY id"
                ).fetchall(), [("xi:an",), ("xin:gu",)])
                connection.execute(
                    "UPDATE dict SET syllable_ids = NULL WHERE text = ?", ("新股",)
                )
                connection.execute(
                    "INSERT INTO dict (text, code) VALUES (?, ?)", ("先", "xian")
                )
            original = path.read_bytes()
            with self.assertRaisesRegex(ValueError, "ambiguous"):
                update_syllable_ids(path)
            self.assertEqual(path.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
