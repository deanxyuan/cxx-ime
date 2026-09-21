#!/usr/bin/env python3

import json
import os
import sqlite3
import sys
import tempfile
import unittest
import zipfile


ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA_TOOLS = os.path.join(ROOT, "data", "tools")
SCHEMA_PATH = os.path.join(
    ROOT, "data", "schemas", "pinyin.microsoft-shuangpin.schema.json"
)
FULL_PINYIN_SCHEMA_PATH = os.path.join(
    ROOT, "data", "schemas", "pinyin.full-pinyin.schema.json"
)
sys.path.insert(0, DATA_TOOLS)

from dict_builder.pinyin_spellings import PatriciaTrie
from generate_pinyin_spellings import build_shuangpin_script, load_schema


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
        cls.schema = load_schema(SCHEMA_PATH)
        cls.syllabary = production_syllabary()
        cls.script = build_shuangpin_script(cls.syllabary, cls.schema)

    def assert_normal_mapping(self, code, syllable):
        self.assertIn(code, self.script)
        self.assertTrue(
            any(item.syllable == syllable and item.type == 0 for item in self.script[code]),
            f"{code} does not normally map to {syllable}",
        )

    def test_microsoft_golden_mappings(self):
        self.assertEqual(self.schema["scheme_id"], "microsoft_shuangpin")
        for code, syllable in {
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
        }.items():
            with self.subTest(code=code, syllable=syllable):
                self.assert_normal_mapping(code, syllable)

    def test_schema_contract_rejects_unknown_types_and_fields(self):
        full_pinyin = load_schema(FULL_PINYIN_SCHEMA_PATH)
        self.assertEqual(full_pinyin["scheme_id"], "full_pinyin")
        self.assertEqual(full_pinyin["speller"]["type"], "full_pinyin")

        unknown_root_field = json.loads(json.dumps(self.schema))
        unknown_root_field["display_name"] = "duplicate metadata"
        with self.assertRaisesRegex(ValueError, "unknown=\\['display_name'\\]"):
            build_shuangpin_script(self.syllabary, unknown_root_field)

        unknown_type = json.loads(json.dumps(self.schema))
        unknown_type["speller"]["type"] = "future_type"
        with self.assertRaisesRegex(ValueError, "unsupported speller.type"):
            build_shuangpin_script(self.syllabary, unknown_type)

        unknown_field = json.loads(json.dumps(self.schema))
        unknown_field["speller"]["future_option"] = True
        with self.assertRaisesRegex(ValueError, "unknown=\\['future_option'\\]"):
            build_shuangpin_script(self.syllabary, unknown_field)

        unsupported_key = json.loads(json.dumps(self.schema))
        unsupported_key["speller"]["alphabet"] += ","
        with self.assertRaisesRegex(ValueError, "unsupported by the input processor"):
            build_shuangpin_script(self.syllabary, unsupported_key)

    def test_declared_alternatives_and_collisions_are_explicit(self):
        self.assert_normal_mapping("lt", "lue")
        self.assert_normal_mapping("lv", "lve")
        self.assert_normal_mapping("nt", "nue")
        self.assert_normal_mapping("nv", "nve")

        invalid = json.loads(json.dumps(self.schema))
        invalid["speller"]["expected_collisions"].pop("lt")
        with self.assertRaisesRegex(ValueError, "collisions differ"):
            build_shuangpin_script(self.syllabary, invalid)

    def test_unreachable_mapping_is_rejected(self):
        invalid = json.loads(json.dumps(self.schema))
        invalid["speller"]["finals"]["unused"] = "a"
        with self.assertRaisesRegex(ValueError, "unreachable shuangpin mappings"):
            build_shuangpin_script(self.syllabary, invalid)

    def test_fuzzy_rules_keep_normal_mappings_and_add_fuzzy_syllables(self):
        self.assert_normal_mapping("ni", "ni")
        self.assertTrue(
            any(item.syllable == "ni" and item.type == 1 for item in self.script["li"])
        )

    def test_all_dictionary_syllables_are_covered_or_explicitly_ignored(self):
        ignored = set(self.schema["speller"]["ignored_syllables"])
        generated = {
            spelling.syllable
            for spellings in self.script.values()
            for spelling in spellings
            if spelling.type == 0
        }
        self.assertEqual(self.syllabary - ignored, generated)

    def test_binary_generation_is_deterministic(self):
        rebuilt = build_shuangpin_script(self.syllabary, self.schema)
        self.assertEqual(serialize_script(self.script), serialize_script(rebuilt))


if __name__ == "__main__":
    unittest.main()
